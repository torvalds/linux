/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2021 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.  *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
 * Portions Copyright (C) 2004-2005 Christoph Hellwig              *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 ********************************************************************/

#include <linux/nvme.h>
#include <linux/nvme-fc-driver.h>
#include <linux/nvme-fc.h>

#define LPFC_NVME_DEFAULT_SEGS		(64 + 1)	/* 256K IOs */

#define LPFC_NVME_ERSP_LEN		0x20

#define LPFC_NVME_WAIT_TMO              10
#define LPFC_NVME_EXPEDITE_XRICNT	8
#define LPFC_NVME_FB_SHIFT		9
#define LPFC_NVME_MAX_FB		(1 << 20)	/* 1M */

#define lpfc_ndlp_get_nrport(ndlp)				\
	((!ndlp->nrport || (ndlp->fc4_xpt_flags & NVME_XPT_UNREG_WAIT))\
	? NULL : ndlp->nrport)

struct lpfc_nvme_qhandle {
	uint32_t index;		/* WQ index to use */
	uint32_t qidx;		/* queue index passed to create */
	uint32_t cpu_id;	/* current cpu id at time of create */
};

/* Declare nvme-based local and remote port definitions. */
struct lpfc_nvme_lport {
	struct lpfc_vport *vport;
	struct completion *lport_unreg_cmp;
	/* Add stats counters here */
	atomic_t fc4NvmeLsRequests;
	atomic_t fc4NvmeLsCmpls;
	atomic_t xmt_fcp_noxri;
	atomic_t xmt_fcp_bad_ndlp;
	atomic_t xmt_fcp_qdepth;
	atomic_t xmt_fcp_wqerr;
	atomic_t xmt_fcp_err;
	atomic_t xmt_fcp_abort;
	atomic_t xmt_ls_abort;
	atomic_t xmt_ls_err;
	atomic_t cmpl_fcp_xb;
	atomic_t cmpl_fcp_err;
	atomic_t cmpl_ls_xb;
	atomic_t cmpl_ls_err;
};

struct lpfc_nvme_rport {
	struct lpfc_nvme_lport *lport;
	struct nvme_fc_remote_port *remoteport;
	struct lpfc_nodelist *ndlp;
	struct completion rport_unreg_done;
};

struct lpfc_nvme_fcpreq_priv {
	struct lpfc_io_buf *nvme_buf;
};

/*
 * set NVME LS request timeouts to 30s. It is larger than the 2*R_A_TOV
 * set by the spec, which appears to have issues with some devices.
 */
#define LPFC_NVME_LS_TIMEOUT		30


#define LPFC_NVMET_DEFAULT_SEGS		(64 + 1)	/* 256K IOs */
#define LPFC_NVMET_RQE_MIN_POST		128
#define LPFC_NVMET_RQE_DEF_POST		512
#define LPFC_NVMET_RQE_DEF_COUNT	2048
#define LPFC_NVMET_SUCCESS_LEN		12

#define LPFC_NVMET_MRQ_AUTO		0
#define LPFC_NVMET_MRQ_MAX		16

#define LPFC_NVMET_WAIT_TMO		(5 * MSEC_PER_SEC)

/* Used for NVME Target */
#define LPFC_NVMET_INV_HOST_ACTIVE      1

struct lpfc_nvmet_tgtport {
	struct lpfc_hba *phba;
	struct completion *tport_unreg_cmp;
	atomic_t state;		/* tracks nvmet hosthandle invalidation */

	/* Stats counters - lpfc_nvmet_unsol_ls_buffer */
	atomic_t rcv_ls_req_in;
	atomic_t rcv_ls_req_out;
	atomic_t rcv_ls_req_drop;
	atomic_t xmt_ls_abort;
	atomic_t xmt_ls_abort_cmpl;

	/* Stats counters - lpfc_nvmet_xmt_ls_rsp */
	atomic_t xmt_ls_rsp;
	atomic_t xmt_ls_drop;

	/* Stats counters - lpfc_nvmet_xmt_ls_rsp_cmp */
	atomic_t xmt_ls_rsp_error;
	atomic_t xmt_ls_rsp_aborted;
	atomic_t xmt_ls_rsp_xb_set;
	atomic_t xmt_ls_rsp_cmpl;

	/* Stats counters - lpfc_nvmet_unsol_fcp_buffer */
	atomic_t rcv_fcp_cmd_in;
	atomic_t rcv_fcp_cmd_out;
	atomic_t rcv_fcp_cmd_drop;
	atomic_t rcv_fcp_cmd_defer;
	atomic_t xmt_fcp_release;

	/* Stats counters - lpfc_nvmet_xmt_fcp_op */
	atomic_t xmt_fcp_drop;
	atomic_t xmt_fcp_read_rsp;
	atomic_t xmt_fcp_read;
	atomic_t xmt_fcp_write;
	atomic_t xmt_fcp_rsp;

	/* Stats counters - lpfc_nvmet_xmt_fcp_op_cmp */
	atomic_t xmt_fcp_rsp_xb_set;
	atomic_t xmt_fcp_rsp_cmpl;
	atomic_t xmt_fcp_rsp_error;
	atomic_t xmt_fcp_rsp_aborted;
	atomic_t xmt_fcp_rsp_drop;

	/* Stats counters - lpfc_nvmet_xmt_fcp_abort */
	atomic_t xmt_fcp_xri_abort_cqe;
	atomic_t xmt_fcp_abort;
	atomic_t xmt_fcp_abort_cmpl;
	atomic_t xmt_abort_sol;
	atomic_t xmt_abort_unsol;
	atomic_t xmt_abort_rsp;
	atomic_t xmt_abort_rsp_error;

	/* Stats counters - defer IO */
	atomic_t defer_ctx;
	atomic_t defer_fod;
	atomic_t defer_wqfull;
};

struct lpfc_nvmet_ctx_info {
	struct list_head nvmet_ctx_list;
	spinlock_t	nvmet_ctx_list_lock; /* lock per CPU */
	struct lpfc_nvmet_ctx_info *nvmet_ctx_next_cpu;
	struct lpfc_nvmet_ctx_info *nvmet_ctx_start_cpu;
	uint16_t	nvmet_ctx_list_cnt;
	char pad[16];  /* pad to a cache-line */
};

/* This retrieves the context info associated with the specified cpu / mrq */
#define lpfc_get_ctx_list(phba, cpu, mrq)  \
	(phba->sli4_hba.nvmet_ctx_info + ((cpu * phba->cfg_nvmet_mrq) + mrq))

/* Values for state field of struct lpfc_async_xchg_ctx */
#define LPFC_NVME_STE_LS_RCV		1
#define LPFC_NVME_STE_LS_ABORT		2
#define LPFC_NVME_STE_LS_RSP		3
#define LPFC_NVME_STE_RCV		4
#define LPFC_NVME_STE_DATA		5
#define LPFC_NVME_STE_ABORT		6
#define LPFC_NVME_STE_DONE		7
#define LPFC_NVME_STE_FREE		0xff

/* Values for flag field of struct lpfc_async_xchg_ctx */
#define LPFC_NVME_IO_INP		0x1  /* IO is in progress on exchange */
#define LPFC_NVME_ABORT_OP		0x2  /* Abort WQE issued on exchange */
#define LPFC_NVME_XBUSY			0x4  /* XB bit set on IO cmpl */
#define LPFC_NVME_CTX_RLS		0x8  /* ctx free requested */
#define LPFC_NVME_ABTS_RCV		0x10  /* ABTS received on exchange */
#define LPFC_NVME_CTX_REUSE_WQ		0x20  /* ctx reused via WQ */
#define LPFC_NVME_DEFER_WQFULL		0x40  /* Waiting on a free WQE */
#define LPFC_NVME_TNOTIFY		0x80  /* notify transport of abts */

struct lpfc_async_xchg_ctx {
	union {
		struct nvmefc_tgt_fcp_req fcp_req;
	} hdlrctx;
	struct list_head list;
	struct lpfc_hba *phba;
	struct lpfc_nodelist *ndlp;
	struct nvmefc_ls_req *ls_req;
	struct nvmefc_ls_rsp ls_rsp;
	struct lpfc_iocbq *wqeq;
	struct lpfc_iocbq *abort_wqeq;
	spinlock_t ctxlock; /* protect flag access */
	uint32_t sid;
	uint32_t offset;
	uint16_t oxid;
	uint16_t size;
	uint16_t entry_cnt;
	uint16_t cpu;
	uint16_t idx;
	uint16_t state;
	uint16_t flag;
	void *payload;
	struct rqb_dmabuf *rqb_buffer;
	struct lpfc_nvmet_ctxbuf *ctxbuf;
	struct lpfc_sli4_hdw_queue *hdwq;

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint64_t ts_isr_cmd;
	uint64_t ts_cmd_nvme;
	uint64_t ts_nvme_data;
	uint64_t ts_data_wqput;
	uint64_t ts_isr_data;
	uint64_t ts_data_nvme;
	uint64_t ts_nvme_status;
	uint64_t ts_status_wqput;
	uint64_t ts_isr_status;
	uint64_t ts_status_nvme;
#endif
};


/* routines found in lpfc_nvme.c */
int __lpfc_nvme_ls_req(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		struct nvmefc_ls_req *pnvme_lsreq,
		void (*gen_req_cmp)(struct lpfc_hba *phba,
				struct lpfc_iocbq *cmdwqe,
				struct lpfc_iocbq *rspwqe));
void __lpfc_nvme_ls_req_cmp(struct lpfc_hba *phba,  struct lpfc_vport *vport,
		struct lpfc_iocbq *cmdwqe, struct lpfc_wcqe_complete *wcqe);
int __lpfc_nvme_ls_abort(struct lpfc_vport *vport,
		struct lpfc_nodelist *ndlp, struct nvmefc_ls_req *pnvme_lsreq);

/* routines found in lpfc_nvmet.c */
int lpfc_nvme_unsol_ls_issue_abort(struct lpfc_hba *phba,
			struct lpfc_async_xchg_ctx *ctxp, uint32_t sid,
			uint16_t xri);
int __lpfc_nvme_xmt_ls_rsp(struct lpfc_async_xchg_ctx *axchg,
			struct nvmefc_ls_rsp *ls_rsp,
			void (*xmt_ls_rsp_cmp)(struct lpfc_hba *phba,
				struct lpfc_iocbq *cmdwqe,
				struct lpfc_iocbq *rspwqe));
void __lpfc_nvme_xmt_ls_rsp_cmp(struct lpfc_hba *phba,
		struct lpfc_iocbq *cmdwqe, struct lpfc_iocbq *rspwqe);
