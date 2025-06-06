/*
 * SPDX-FileCopyrightText: Copyright (c) 1999-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define  __NO_VERSION__

#include "os-interface.h"
#include "nv-linux.h"

NV_STATUS NV_API_CALL nv_add_mapping_context_to_file(
    nv_state_t *nv,
    nv_usermap_access_params_t *nvuap,
    NvU32       prot,
    void       *pAllocPriv,
    NvU64       pageIndex,
    NvU32       fd
)
{
    NV_STATUS status = NV_OK;
    nv_alloc_mapping_context_t *nvamc = NULL;
    nv_file_private_t *nvfp = NULL;
    nv_linux_file_private_t *nvlfp = NULL;
    nv_linux_state_t *nvl = NV_GET_NVL_FROM_NV_STATE(nv);
    void *priv = NULL;

    nvfp = nv_get_file_private(fd, NV_IS_CTL_DEVICE(nv), &priv);
    if (nvfp == NULL)
        return NV_ERR_INVALID_ARGUMENT;

    nvlfp = nv_get_nvlfp_from_nvfp(nvfp);

    nvamc = &nvlfp->mmap_context;

    if (nvamc->valid)
    {
        status = NV_ERR_STATE_IN_USE;
        goto done;
    }
    
    os_mem_set((void*) nvamc, 0, sizeof(nv_alloc_mapping_context_t));

    if (NV_IS_CTL_DEVICE(nv))
    {
        nvamc->alloc = pAllocPriv;
        nvamc->page_index = pageIndex;
    }
    else
    {
        if (NV_STATE_PTR(nvlfp->nvptr) != nv)
        {
            status = NV_ERR_INVALID_ARGUMENT;
            goto done;
        }

        status = os_alloc_mem((void**) &nvamc->memArea.pRanges,
            sizeof(MemoryRange) * nvuap->memArea.numRanges);

        if (status != NV_OK)
        {
            nvamc->memArea.pRanges = NULL;
            goto done;
        }
        nvamc->memArea.numRanges = nvuap->memArea.numRanges;
        os_mem_copy(nvamc->memArea.pRanges, nvuap->memArea.pRanges,
            sizeof(MemoryRange) * nvuap->memArea.numRanges);

        if (nv_get_numa_status(nvl) == NV_NUMA_STATUS_ONLINE)
        {
            nvamc->page_array = nvuap->page_array;
            nvamc->num_pages = nvuap->num_pages;
        }
        nvamc->access_start = nvuap->access_start;
        nvamc->access_size = nvuap->access_size;
    }

    nvamc->prot = prot;
    nvamc->valid = NV_TRUE;
    nvamc->caching = nvuap->caching;

done:
    nv_put_file_private(priv);

    return status;
}

NV_STATUS NV_API_CALL nv_alloc_user_mapping(
    nv_state_t *nv,
    void       *pAllocPrivate,
    NvU64       pageIndex,
    NvU32       pageOffset,
    NvU64       size,
    NvU32       protect,
    NvU64      *pUserAddress,
    void      **ppPrivate
)
{
    nv_alloc_t *at = pAllocPrivate;

    if (at->flags.contig)
        *pUserAddress = (at->page_table[0]->phys_addr + (pageIndex * PAGE_SIZE) + pageOffset);
    else
        *pUserAddress = (at->page_table[pageIndex]->phys_addr + pageOffset);

    return NV_OK;
}

NV_STATUS NV_API_CALL nv_free_user_mapping(
    nv_state_t *nv,
    void       *pAllocPrivate,
    NvU64       userAddress,
    void       *pPrivate
)
{
    return NV_OK;
}

/*
 * This function checks if a user mapping should be allowed given the GPU's 4K
 * page isolation requirements.
 */
NV_STATUS NV_API_CALL nv_check_usermap_access_params(
    nv_state_t *nv,
    const nv_usermap_access_params_t *nvuap
)
{
    const NvU64 addr = nvuap->addr;
    const NvU64 size = nvuap->size;

    if (rm_gpu_need_4k_page_isolation(nv) &&
        NV_4K_PAGE_ISOLATION_REQUIRED(addr, size))
    {
        NV_DEV_PRINTF(NV_DBG_ERRORS, nv, "4K page isolation required but not available!\n");
        return NV_ERR_OPERATING_SYSTEM;
    }

    return NV_OK;
}
