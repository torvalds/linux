/*
 * Copyright (c) 2006 - 2008 NetEffect.  All rights reserved.
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef NES_USER_H
#define NES_USER_H

#include <linux/types.h>

#define NES_ABI_USERSPACE_VER 1
#define NES_ABI_KERNEL_VER    1

/*
 * Make sure that all structs defined in this file remain laid out so
 * that they pack the same way on 32-bit and 64-bit architectures (to
 * avoid incompatibility between 32-bit userspace and 64-bit kernels).
 * In particular do not use pointer types -- pass pointers in __u64
 * instead.
 */

struct nes_alloc_ucontext_req {
	__u32 reserved32;
	__u8  userspace_ver;
	__u8  reserved8[3];
};

struct nes_alloc_ucontext_resp {
	__u32 max_pds; /* maximum pds allowed for this user process */
	__u32 max_qps; /* maximum qps allowed for this user process */
	__u32 wq_size; /* size of the WQs (sq+rq) allocated to the mmaped area */
	__u8  virtwq;  /* flag to indicate if virtual WQ are to be used or not */
	__u8  kernel_ver;
	__u8  reserved[2];
};

struct nes_alloc_pd_resp {
	__u32 pd_id;
	__u32 mmap_db_index;
};

struct nes_create_cq_req {
	__u64 user_cq_buffer;
	__u32 mcrqf;
	__u8 reserved[4];
};

struct nes_create_qp_req {
	__u64 user_wqe_buffers;
};

enum iwnes_memreg_type {
	IWNES_MEMREG_TYPE_MEM = 0x0000,
	IWNES_MEMREG_TYPE_QP = 0x0001,
	IWNES_MEMREG_TYPE_CQ = 0x0002,
	IWNES_MEMREG_TYPE_MW = 0x0003,
	IWNES_MEMREG_TYPE_FMR = 0x0004,
};

struct nes_mem_reg_req {
	__u32 reg_type;	/* indicates if id is memory, QP or CQ */
	__u32 reserved;
};

struct nes_create_cq_resp {
	__u32 cq_id;
	__u32 cq_size;
	__u32 mmap_db_index;
	__u32 reserved;
};

struct nes_create_qp_resp {
	__u32 qp_id;
	__u32 actual_sq_size;
	__u32 actual_rq_size;
	__u32 mmap_sq_db_index;
	__u32 mmap_rq_db_index;
	__u32 nes_drv_opt;
};

#endif				/* NES_USER_H */
