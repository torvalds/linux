/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  Struct definition for eHCA internal structures
 *
 *  Authors: Heiko J Schick <schickhj@de.ibm.com>
 *           Christoph Raisch <raisch@de.ibm.com>
 *           Joachim Fenkes <fenkes@de.ibm.com>
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
#include <linux/mutex.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>

#ifdef CONFIG_PPC64
#include "ehca_classes_pSeries.h"
#endif
#include "ipz_pt_fn.h"
#include "ehca_qes.h"
#include "ehca_irq.h"

#define EHCA_EQE_CACHE_SIZE 20
#define EHCA_MAX_NUM_QUEUES 0xffff

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

struct ehca_sma_attr {
	u16 lid, lmc, sm_sl, sm_lid;
	u16 pkey_tbl_len, pkeys[16];
};

struct ehca_sport {
	struct ib_cq *ibcq_aqp1;
	struct ib_qp *ibqp_sqp[2];
	/* lock to serialze modify_qp() calls for sqp in normal
	 * and irq path (when event PORT_ACTIVE is received first time)
	 */
	spinlock_t mod_sqp_lock;
	enum ib_port_state port_state;
	struct ehca_sma_attr saved_attr;
	u32 pma_qp_nr;
};

#define HCA_CAP_MR_PGSIZE_4K  0x80000000
#define HCA_CAP_MR_PGSIZE_64K 0x40000000
#define HCA_CAP_MR_PGSIZE_1M  0x20000000
#define HCA_CAP_MR_PGSIZE_16M 0x10000000

struct ehca_shca {
	struct ib_device ib_device;
	struct platform_device *ofdev;
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
	struct mutex modify_mutex;
	u64 hca_cap;
	/* MR pgsize: bit 0-3 means 4K, 64K, 1M, 16M respectively */
	u32 hca_cap_mr_pgsize;
	int max_mtu;
	int max_num_qps;
	int max_num_cqs;
	atomic_t num_cqs;
	atomic_t num_qps;
};

struct ehca_pd {
	struct ib_pd ib_pd;
	struct ipz_pd fw_pd;
	/* small queue mgmt */
	struct mutex lock;
	struct list_head free[2];
	struct list_head full[2];
};

enum ehca_ext_qp_type {
	EQPT_NORMAL    = 0,
	EQPT_LLQP      = 1,
	EQPT_SRQBASE   = 2,
	EQPT_SRQ       = 3,
};

/* struct to cache modify_qp()'s parms for GSI/SMI qp */
struct ehca_mod_qp_parm {
	int mask;
	struct ib_qp_attr attr;
};

#define EHCA_MOD_QP_PARM_MAX 4

#define QMAP_IDX_MASK 0xFFFFULL

/* struct for tracking if cqes have been reported to the application */
struct ehca_qmap_entry {
	u16 app_wr_id;
	u8 reported;
	u8 cqe_req;
};

struct ehca_queue_map {
	struct ehca_qmap_entry *map;
	unsigned int entries;
	unsigned int tail;
	unsigned int left_to_poll;
	unsigned int next_wqe_idx;   /* Idx to first wqe to be flushed */
};

/* function to calculate the next index for the qmap */
static inline unsigned int next_index(unsigned int cur_index, unsigned int limit)
{
	unsigned int temp = cur_index + 1;
	return (temp == limit) ? 0 : temp;
}

struct ehca_qp {
	union {
		struct ib_qp ib_qp;
		struct ib_srq ib_srq;
	};
	u32 qp_type;
	enum ehca_ext_qp_type ext_type;
	enum ib_qp_state state;
	struct ipz_queue ipz_squeue;
	struct ehca_queue_map sq_map;
	struct ipz_queue ipz_rqueue;
	struct ehca_queue_map rq_map;
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
	/* array to cache modify_qp()'s parms for GSI/SMI qp */
	struct ehca_mod_qp_parm *mod_qp_parm;
	int mod_qp_parm_idx;
	/* mmap counter for resources mapped into user space */
	u32 mm_count_squeue;
	u32 mm_count_rqueue;
	u32 mm_count_galpa;
	/* unsolicited ack circumvention */
	int unsol_ack_circ;
	int mtu_shift;
	u32 message_count;
	u32 packet_count;
	atomic_t nr_events; /* events seen */
	wait_queue_head_t wait_completion;
	int mig_armed;
	struct list_head sq_err_node;
	struct list_head rq_err_node;
};

#define IS_SRQ(qp) (qp->ext_type == EQPT_SRQ)
#define HAS_SQ(qp) (qp->ext_type != EQPT_SRQ)
#define HAS_RQ(qp) (qp->ext_type != EQPT_SRQBASE)

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
	u32 nr_callbacks;   /* #events assigned to cpu by scaling code */
	atomic_t nr_events; /* #events seen */
	wait_queue_head_t wait_completion;
	spinlock_t task_lock;
	/* mmap counter for resources mapped into user space */
	u32 mm_count_queue;
	u32 mm_count_galpa;
	struct list_head sqp_err_list;
	struct list_head rqp_err_list;
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
	struct ib_umem *umem;
	spinlock_t mrlock;

	enum ehca_mr_flag flags;
	u32 num_kpages;		/* number of kernel pages */
	u32 num_hwpages;	/* number of hw pages to form MR */
	u64 hwpage_size;	/* hw page size used for this MR */
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
	u64 num_kpages;
	u64 kpage_cnt;
	u64 hwpage_size;     /* hw page size used for this MR */
	u64 num_hwpages;     /* number of hw pages */
	u64 hwpage_cnt;      /* counter for hw pages */
	u64 next_hwpage;     /* next hw page in buffer/chunk/listelem */

	union {
		struct { /* type EHCA_MR_PGI_PHYS section */
			int num_phys_buf;
			struct ib_phys_buf *phys_buf_array;
			u64 next_buf;
		} phy;
		struct { /* type EHCA_MR_PGI_USER section */
			struct ib_umem *region;
			struct ib_umem_chunk *next_chunk;
			u64 next_nmap;
		} usr;
		struct { /* type EHCA_MR_PGI_FMR section */
			u64 fmr_pgsize;
			u64 *page_list;
			u64 next_listelem;
		} fmr;
	} u;
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
int ehca_init_small_qp_cache(void);
void ehca_cleanup_small_qp_cache(void);

extern rwlock_t ehca_qp_idr_lock;
extern rwlock_t ehca_cq_idr_lock;
extern struct idr ehca_qp_idr;
extern struct idr ehca_cq_idr;
extern spinlock_t shca_list_lock;

extern int ehca_static_rate;
extern int ehca_port_act_time;
extern bool ehca_use_hp_mr;
extern bool ehca_scaling_code;
extern int ehca_lock_hcalls;
extern int ehca_nr_ports;
extern int ehca_max_cq;
extern int ehca_max_qp;

struct ipzu_queue_resp {
	u32 qe_size;      /* queue entry size */
	u32 act_nr_of_sg;
	u32 queue_length; /* queue length allocated in bytes */
	u32 pagesize;
	u32 toggle_state;
	u32 offset; /* save offset within a page for small_qp */
};

struct ehca_create_cq_resp {
	u32 cq_number;
	u32 token;
	struct ipzu_queue_resp ipz_queue;
	u32 fw_handle_ofs;
	u32 dummy;
};

struct ehca_create_qp_resp {
	u32 qp_num;
	u32 token;
	u32 qp_type;
	u32 ext_type;
	u32 qkey;
	/* qp_num assigned by ehca: sqp0/1 may have got different numbers */
	u32 real_qp_num;
	u32 fw_handle_ofs;
	u32 dummy;
	struct ipzu_queue_resp ipz_squeue;
	struct ipzu_queue_resp ipz_rqueue;
};

struct ehca_alloc_cq_parms {
	u32 nr_cqe;
	u32 act_nr_of_entries;
	u32 act_pages;
	struct ipz_eq_handle eq_handle;
};

enum ehca_service_type {
	ST_RC  = 0,
	ST_UC  = 1,
	ST_RD  = 2,
	ST_UD  = 3,
};

enum ehca_ll_comp_flags {
	LLQP_SEND_COMP = 0x20,
	LLQP_RECV_COMP = 0x40,
	LLQP_COMP_MASK = 0x60,
};

struct ehca_alloc_queue_parms {
	/* input parameters */
	int max_wr;
	int max_sge;
	int page_size;
	int is_small;

	/* output parameters */
	u16 act_nr_wqes;
	u8  act_nr_sges;
	u32 queue_size; /* bytes for small queues, pages otherwise */
};

struct ehca_alloc_qp_parms {
	struct ehca_alloc_queue_parms squeue;
	struct ehca_alloc_queue_parms rqueue;

	/* input parameters */
	enum ehca_service_type servicetype;
	int qp_storage;
	int sigtype;
	enum ehca_ext_qp_type ext_type;
	enum ehca_ll_comp_flags ll_comp_flags;
	int ud_av_l_key_ctl;

	u32 token;
	struct ipz_eq_handle eq_handle;
	struct ipz_pd pd;
	struct ipz_cq_handle send_cq_handle, recv_cq_handle;

	u32 srq_qpn, srq_token, srq_limit;

	/* output parameters */
	u32 real_qp_num;
	struct ipz_qp_handle qp_handle;
	struct h_galpas galpas;
};

int ehca_cq_assign_qp(struct ehca_cq *cq, struct ehca_qp *qp);
int ehca_cq_unassign_qp(struct ehca_cq *cq, unsigned int qp_num);
struct ehca_qp *ehca_cq_get_qp(struct ehca_cq *cq, int qp_num);

#endif
