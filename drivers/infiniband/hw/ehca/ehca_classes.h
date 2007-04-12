/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  Struct definition for eHCA internal structures
 *
 *  Authors: Heiko J Schick <schickhj@de.ibm.com>
 *           Christoph Raisch <raisch@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __EHCA_CLASSES_H__
#define __EHCA_CLASSES_H__


struct ehca_module;
struct ehca_qp;
struct ehca_cq;
struct ehca_eq;
struct ehca_mr;
struct ehca_mw;
struct ehca_pd;
struct ehca_av;

#include <linux/wait.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>

#ifdef CONFIG_PPC64
#include "ehca_classes_pSeries.h"
#endif
#include "ipz_pt_fn.h"
#include "ehca_qes.h"
#include "ehca_irq.h"

#define EHCA_EQE_CACHE_SIZE 20

struct ehca_eqe_cache_entry {
	struct ehca_eqe *eqe;
	struct ehca_cq *cq;
};

struct ehca_eq {
	u32 length;
	struct ipz_queue ipz_queue;
	struct ipz_eq_handle ipz_eq_handle;
	struct work_struct work;
	struct h_galpas galpas;
	int is_initialized;
	struct ehca_pfeq pf;
	spinlock_t spinlock;
	struct tasklet_struct interrupt_task;
	u32 ist;
	spinlock_t irq_spinlock;
	struct ehca_eqe_cache_entry eqe_cache[EHCA_EQE_CACHE_SIZE];
};

struct ehca_sport {
	struct ib_cq *ibcq_aqp1;
	struct ib_qp *ibqp_aqp1;
	enum ib_rate  rate;
	enum ib_port_state port_state;
};

struct ehca_shca {
	struct ib_device ib_device;
	struct ibmebus_dev *ibmebus_dev;
	u8 num_ports;
	int hw_level;
	struct list_head shca_list;
	struct ipz_adapter_handle ipz_hca_handle;
	struct ehca_sport sport[2];
	struct ehca_eq eq;
	struct ehca_eq neq;
	struct ehca_mr *maxmr;
	struct ehca_pd *pd;
	struct h_galpas galpas;
};

struct ehca_pd {
	struct ib_pd ib_pd;
	struct ipz_pd fw_pd;
	u32 ownpid;
};

struct ehca_qp {
	struct ib_qp ib_qp;
	u32 qp_type;
	struct ipz_queue ipz_squeue;
	struct ipz_queue ipz_rqueue;
	struct h_galpas galpas;
	u32 qkey;
	u32 real_qp_num;
	u32 token;
	spinlock_t spinlock_s;
	spinlock_t spinlock_r;
	u32 sq_max_inline_data_size;
	struct ipz_qp_handle ipz_qp_handle;
	struct ehca_pfqp pf;
	struct ib_qp_init_attr init_attr;
	struct ehca_cq *send_cq;
	struct ehca_cq *recv_cq;
	unsigned int sqerr_purgeflag;
	struct hlist_node list_entries;
	/* mmap counter for resources mapped into user space */
	u32 mm_count_squeue;
	u32 mm_count_rqueue;
	u32 mm_count_galpa;
};

/* must be power of 2 */
#define QP_HASHTAB_LEN 8

struct ehca_cq {
	struct ib_cq ib_cq;
	struct ipz_queue ipz_queue;
	struct h_galpas galpas;
	spinlock_t spinlock;
	u32 cq_number;
	u32 token;
	u32 nr_of_entries;
	struct ipz_cq_handle ipz_cq_handle;
	struct ehca_pfcq pf;
	spinlock_t cb_lock;
	struct hlist_head qp_hashtab[QP_HASHTAB_LEN];
	struct list_head entry;
	u32 nr_callbacks; /* #events assigned to cpu by scaling code */
	u32 nr_events;    /* #events seen */
	wait_queue_head_t wait_completion;
	spinlock_t task_lock;
	u32 ownpid;
	/* mmap counter for resources mapped into user space */
	u32 mm_count_queue;
	u32 mm_count_galpa;
};

enum ehca_mr_flag {
	EHCA_MR_FLAG_FMR = 0x80000000,	 /* FMR, created with ehca_alloc_fmr */
	EHCA_MR_FLAG_MAXMR = 0x40000000, /* max-MR                           */
};

struct ehca_mr {
	union {
		struct ib_mr ib_mr;	/* must always be first in ehca_mr */
		struct ib_fmr ib_fmr;	/* must always be first in ehca_mr */
	} ib;
	spinlock_t mrlock;

	enum ehca_mr_flag flags;
	u32 num_pages;		/* number of MR pages */
	u32 num_4k;		/* number of 4k "page" portions to form MR */
	int acl;		/* ACL (stored here for usage in reregister) */
	u64 *start;		/* virtual start address (stored here for */
	                        /* usage in reregister) */
	u64 size;		/* size (stored here for usage in reregister) */
	u32 fmr_page_size;	/* page size for FMR */
	u32 fmr_max_pages;	/* max pages for FMR */
	u32 fmr_max_maps;	/* max outstanding maps for FMR */
	u32 fmr_map_cnt;	/* map counter for FMR */
	/* fw specific data */
	struct ipz_mrmw_handle ipz_mr_handle;	/* MR handle for h-calls */
	struct h_galpas galpas;
	/* data for userspace bridge */
	u32 nr_of_pages;
	void *pagearray;
};

struct ehca_mw {
	struct ib_mw ib_mw;	/* gen2 mw, must always be first in ehca_mw */
	spinlock_t mwlock;

	u8 never_bound;		/* indication MW was never bound */
	struct ipz_mrmw_handle ipz_mw_handle;	/* MW handle for h-calls */
	struct h_galpas galpas;
};

enum ehca_mr_pgi_type {
	EHCA_MR_PGI_PHYS   = 1,  /* type of ehca_reg_phys_mr,
				  * ehca_rereg_phys_mr,
				  * ehca_reg_internal_maxmr */
	EHCA_MR_PGI_USER   = 2,  /* type of ehca_reg_user_mr */
	EHCA_MR_PGI_FMR    = 3   /* type of ehca_map_phys_fmr */
};

struct ehca_mr_pginfo {
	enum ehca_mr_pgi_type type;
	u64 num_pages;
	u64 page_cnt;
	u64 num_4k;       /* number of 4k "page" portions */
	u64 page_4k_cnt;  /* counter for 4k "page" portions */
	u64 next_4k;      /* next 4k "page" portion in buffer/chunk/listelem */

	/* type EHCA_MR_PGI_PHYS section */
	int num_phys_buf;
	struct ib_phys_buf *phys_buf_array;
	u64 next_buf;

	/* type EHCA_MR_PGI_USER section */
	struct ib_umem *region;
	struct ib_umem_chunk *next_chunk;
	u64 next_nmap;

	/* type EHCA_MR_PGI_FMR section */
	u64 *page_list;
	u64 next_listelem;
	/* next_4k also used within EHCA_MR_PGI_FMR */
};

/* output parameters for MR/FMR hipz calls */
struct ehca_mr_hipzout_parms {
	struct ipz_mrmw_handle handle;
	u32 lkey;
	u32 rkey;
	u64 len;
	u64 vaddr;
	u32 acl;
};

/* output parameters for MW hipz calls */
struct ehca_mw_hipzout_parms {
	struct ipz_mrmw_handle handle;
	u32 rkey;
};

struct ehca_av {
	struct ib_ah ib_ah;
	struct ehca_ud_av av;
};

struct ehca_ucontext {
	struct ib_ucontext ib_ucontext;
};

int ehca_init_pd_cache(void);
void ehca_cleanup_pd_cache(void);
int ehca_init_cq_cache(void);
void ehca_cleanup_cq_cache(void);
int ehca_init_qp_cache(void);
void ehca_cleanup_qp_cache(void);
int ehca_init_av_cache(void);
void ehca_cleanup_av_cache(void);
int ehca_init_mrmw_cache(void);
void ehca_cleanup_mrmw_cache(void);

extern spinlock_t ehca_qp_idr_lock;
extern spinlock_t ehca_cq_idr_lock;
extern struct idr ehca_qp_idr;
extern struct idr ehca_cq_idr;

extern int ehca_static_rate;
extern int ehca_port_act_time;
extern int ehca_use_hp_mr;
extern int ehca_scaling_code;

struct ipzu_queue_resp {
	u32 qe_size;      /* queue entry size */
	u32 act_nr_of_sg;
	u32 queue_length; /* queue length allocated in bytes */
	u32 pagesize;
	u32 toggle_state;
	u32 dummy; /* padding for 8 byte alignment */
};

struct ehca_create_cq_resp {
	u32 cq_number;
	u32 token;
	struct ipzu_queue_resp ipz_queue;
};

struct ehca_create_qp_resp {
	u32 qp_num;
	u32 token;
	u32 qp_type;
	u32 qkey;
	/* qp_num assigned by ehca: sqp0/1 may have got different numbers */
	u32 real_qp_num;
	u32 dummy; /* padding for 8 byte alignment */
	struct ipzu_queue_resp ipz_squeue;
	struct ipzu_queue_resp ipz_rqueue;
};

struct ehca_alloc_cq_parms {
	u32 nr_cqe;
	u32 act_nr_of_entries;
	u32 act_pages;
	struct ipz_eq_handle eq_handle;
};

struct ehca_alloc_qp_parms {
	int servicetype;
	int sigtype;
	int daqp_ctrl;
	int max_send_sge;
	int max_recv_sge;
	int ud_av_l_key_ctl;

	u16 act_nr_send_wqes;
	u16 act_nr_recv_wqes;
	u8  act_nr_recv_sges;
	u8  act_nr_send_sges;

	u32 nr_rq_pages;
	u32 nr_sq_pages;

	struct ipz_eq_handle ipz_eq_handle;
	struct ipz_pd pd;
};

int ehca_cq_assign_qp(struct ehca_cq *cq, struct ehca_qp *qp);
int ehca_cq_unassign_qp(struct ehca_cq *cq, unsigned int qp_num);
struct ehca_qp* ehca_cq_get_qp(struct ehca_cq *cq, int qp_num);

#endif
