#ifndef _HFI1_USER_EXP_RCV_H
#define _HFI1_USER_EXP_RCV_H
/*
 * Copyright(c) 2015 - 2017 Intel Corporation.
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

#include "hfi.h"
#include "exp_rcv.h"

struct tid_pageset {
	u16 idx;
	u16 count;
};

struct tid_user_buf {
	unsigned long vaddr;
	unsigned long length;
	unsigned int npages;
	struct page **pages;
	struct tid_pageset *psets;
	unsigned int n_psets;
};

struct tid_rb_node {
	struct mmu_interval_notifier notifier;
	struct hfi1_filedata *fdata;
	unsigned long phys;
	struct tid_group *grp;
	u32 rcventry;
	dma_addr_t dma_addr;
	bool freed;
	unsigned int npages;
	struct page *pages[0];
};

static inline int num_user_pages(unsigned long addr,
				 unsigned long len)
{
	const unsigned long spage = addr & PAGE_MASK;
	const unsigned long epage = (addr + len - 1) & PAGE_MASK;

	return 1 + ((epage - spage) >> PAGE_SHIFT);
}

int hfi1_user_exp_rcv_init(struct hfi1_filedata *fd,
			   struct hfi1_ctxtdata *uctxt);
void hfi1_user_exp_rcv_free(struct hfi1_filedata *fd);
int hfi1_user_exp_rcv_setup(struct hfi1_filedata *fd,
			    struct hfi1_tid_info *tinfo);
int hfi1_user_exp_rcv_clear(struct hfi1_filedata *fd,
			    struct hfi1_tid_info *tinfo);
int hfi1_user_exp_rcv_invalid(struct hfi1_filedata *fd,
			      struct hfi1_tid_info *tinfo);

#endif /* _HFI1_USER_EXP_RCV_H */
