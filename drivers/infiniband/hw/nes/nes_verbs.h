/*
 * Copyright (c) 2006 - 2008 NetEffect, Inc. All rights reserved.
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

#ifndef NES_VERBS_H
#define NES_VERBS_H

struct nes_device;

#define NES_MAX_USER_DB_REGIONS  4096
#define NES_MAX_USER_WQ_REGIONS  4096

struct nes_ucontext {
	struct ib_ucontext ibucontext;
	struct nes_device  *nesdev;
	unsigned long      mmap_wq_offset;
	unsigned long      mmap_cq_offset; /* to be removed */
	int                index;		/* rnic index (minor) */
	unsigned long      allocated_doorbells[BITS_TO_LONGS(NES_MAX_USER_DB_REGIONS)];
	u16                mmap_db_index[NES_MAX_USER_DB_REGIONS];
	u16                first_free_db;
	unsigned long      allocated_wqs[BITS_TO_LONGS(NES_MAX_USER_WQ_REGIONS)];
	struct nes_qp      *mmap_nesqp[NES_MAX_USER_WQ_REGIONS];
	u16                first_free_wq;
	struct list_head   cq_reg_mem_list;
	struct list_head   qp_reg_mem_list;
	u32                mcrqf;
	atomic_t	   usecnt;
};

struct nes_pd {
	struct ib_pd ibpd;
	u16          pd_id;
	atomic_t     sqp_count;
	u16          mmap_db_index;
};

struct nes_mr {
	union {
		struct ib_mr  ibmr;
		struct ib_mw  ibmw;
		struct ib_fmr ibfmr;
	};
	struct ib_umem    *region;
	u16               pbls_used;
	u8                mode;
	u8                pbl_4k;
};

struct nes_hw_pb {
	__le32 pa_low;
	__le32 pa_high;
};

struct nes_vpbl {
	dma_addr_t       pbl_pbase;
	struct nes_hw_pb *pbl_vbase;
};

struct nes_root_vpbl {
	dma_addr_t       pbl_pbase;
	struct nes_hw_pb *pbl_vbase;
	struct nes_vpbl  *leaf_vpbl;
};

struct nes_fmr {
	struct nes_mr        nesmr;
	u32                  leaf_pbl_cnt;
	struct nes_root_vpbl root_vpbl;
	struct ib_qp         *ib_qp;
	int                  access_rights;
	struct ib_fmr_attr   attr;
};

struct nes_av;

struct nes_cq {
	struct ib_cq     ibcq;
	struct nes_hw_cq hw_cq;
	u32              polled_completions;
	u32              cq_mem_size;
	spinlock_t       lock;
	u8               virtual_cq;
	u8               pad[3];
};

struct nes_wq {
	spinlock_t lock;
};

struct iw_cm_id;
struct ietf_mpa_frame;

struct nes_qp {
	struct ib_qp          ibqp;
	void                  *allocated_buffer;
	struct iw_cm_id       *cm_id;
	struct workqueue_struct *wq;
	struct work_struct    disconn_work;
	struct nes_cq         *nesscq;
	struct nes_cq         *nesrcq;
	struct nes_pd         *nespd;
	void *cm_node; /* handle of the node this QP is associated with */
	struct ietf_mpa_frame *ietf_frame;
	dma_addr_t            ietf_frame_pbase;
	wait_queue_head_t     state_waitq;
	struct ib_mr          *lsmm_mr;
	unsigned long         socket;
	struct nes_hw_qp      hwqp;
	struct work_struct    work;
	struct work_struct    ae_work;
	enum ib_qp_state      ibqp_state;
	u32                   iwarp_state;
	u32                   hte_index;
	u32                   last_aeq;
	u32                   qp_mem_size;
	atomic_t              refcount;
	atomic_t              close_timer_started;
	u32                   mmap_sq_db_index;
	u32                   mmap_rq_db_index;
	spinlock_t            lock;
	struct nes_qp_context *nesqp_context;
	dma_addr_t            nesqp_context_pbase;
	void	              *pbl_vbase;
	dma_addr_t            pbl_pbase;
	struct page           *page;
	wait_queue_head_t     kick_waitq;
	u16                   in_disconnect;
	u16                   private_data_len;
	u8                    active_conn;
	u8                    skip_lsmm;
	u8                    user_mode;
	u8                    hte_added;
	u8                    hw_iwarp_state;
	u8                    flush_issued;
	u8                    hw_tcp_state;
	u8                    disconn_pending;
	u8                    destroyed;
};
#endif			/* NES_VERBS_H */
