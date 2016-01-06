/*
 * Copyright(c) 2015 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/slab.h>
#include "mr.h"

/**
 * rvt_get_dma_mr - get a DMA memory region
 * @pd: protection domain for this memory region
 * @acc: access flags
 *
 * Returns the memory region on success, otherwise returns an errno.
 * Note that all DMA addresses should be created via the
 * struct ib_dma_mapping_ops functions (see dma.c).
 */
struct ib_mr *rvt_get_dma_mr(struct ib_pd *pd, int acc)
{
	/*
	 * Alloc mr and init it.
	 * Alloc lkey.
	 */
	return ERR_PTR(-EOPNOTSUPP);
}

/**
 * rvt_reg_user_mr - register a userspace memory region
 * @pd: protection domain for this memory region
 * @start: starting userspace address
 * @length: length of region to register
 * @mr_access_flags: access flags for this memory region
 * @udata: unused by the driver
 *
 * Returns the memory region on success, otherwise returns an errno.
 */
struct ib_mr *rvt_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			      u64 virt_addr, int mr_access_flags,
			      struct ib_udata *udata)
{
	return ERR_PTR(-EOPNOTSUPP);
}

/**
 * rvt_dereg_mr - unregister and free a memory region
 * @ibmr: the memory region to free
 *
 * Returns 0 on success.
 *
 * Note that this is called to free MRs created by rvt_get_dma_mr()
 * or rvt_reg_user_mr().
 */
int rvt_dereg_mr(struct ib_mr *ibmr)
{
	return -EOPNOTSUPP;
}

/**
 * rvt_alloc_mr - Allocate a memory region usable with the
 * @pd: protection domain for this memory region
 * @mr_type: mem region type
 * @max_num_sg: Max number of segments allowed
 *
 * Return the memory region on success, otherwise return an errno.
 */
struct ib_mr *rvt_alloc_mr(struct ib_pd *pd,
			   enum ib_mr_type mr_type,
			   u32 max_num_sg)
{
	return ERR_PTR(-EOPNOTSUPP);
}

/**
 * rvt_alloc_fmr - allocate a fast memory region
 * @pd: the protection domain for this memory region
 * @mr_access_flags: access flags for this memory region
 * @fmr_attr: fast memory region attributes
 *
 * Returns the memory region on success, otherwise returns an errno.
 */
struct ib_fmr *rvt_alloc_fmr(struct ib_pd *pd, int mr_access_flags,
			     struct ib_fmr_attr *fmr_attr)
{
	return ERR_PTR(-EOPNOTSUPP);
}

/**
 * rvt_map_phys_fmr - set up a fast memory region
 * @ibmfr: the fast memory region to set up
 * @page_list: the list of pages to associate with the fast memory region
 * @list_len: the number of pages to associate with the fast memory region
 * @iova: the virtual address of the start of the fast memory region
 *
 * This may be called from interrupt context.
 */

int rvt_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list,
		     int list_len, u64 iova)
{
	return -EOPNOTSUPP;
}

/**
 * rvt_unmap_fmr - unmap fast memory regions
 * @fmr_list: the list of fast memory regions to unmap
 *
 * Returns 0 on success.
 */
int rvt_unmap_fmr(struct list_head *fmr_list)
{
	return -EOPNOTSUPP;
}

/**
 * rvt_dealloc_fmr - deallocate a fast memory region
 * @ibfmr: the fast memory region to deallocate
 *
 * Returns 0 on success.
 */
int rvt_dealloc_fmr(struct ib_fmr *ibfmr)
{
	return -EOPNOTSUPP;
}
