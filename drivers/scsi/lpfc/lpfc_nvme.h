/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2018 Broadcom. All Rights Reserved. The term *
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

#define LPFC_NVME_DEFAULT_SEGS		(64 + 1)	/* 256K IOs */

#define LPFC_NVME_ERSP_LEN		0x20

#define LPFC_NVME_WAIT_TMO              10
#define LPFC_NVME_EXPEDITE_XRICNT	8
#define LPFC_NVME_FB_SHIFT		9
#define LPFC_NVME_MAX_FB		(1 << 20)	/* 1M */

#define lpfc_ndlp_get_nrport(ndlp)					\
	((!ndlp->nrport || (ndlp->upcall_flags & NLP_WAIT_FOR_UNREG))	\
	? NULL : ndlp->nrport)

struct lpfc_nvme_qhandle {
	uint32_t index;		/* WQ index to use */
	uint32_t qidx;		/* queue index passed to create */
	uint32_t cpu_id;	/* current cpu id at time of create */
};

struct lpfc_nvme_ctrl_stat {
	atomic_t fc4NvmeInputRequests;
	atomic_t fc4NvmeOutputRequests;
	atomic_t fc4NvmeControlRequests;
	atomic_t fc4NvmeIoCmpls;
};

/* Declare nvme-based local and remote port definitions. */
struct lpfc_nvme_lport {
	struct lpfc_vport *vport;
	struct completion *lport_unreg_cmp;
	/* Add stats counters here */
	struct lpfc_nvme_ctrl_stat *cstat;
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

struct lpfc_nvme_buf {
	struct list_head list;
	struct nvmefc_fcp_req *nvmeCmd;
	struct lpfc_nvme_rport *nrport;
	struct lpfc_nodelist *ndlp;

	uint32_t timeout;

	uint16_t flags;  /* TBD convert exch_busy to flags */
#define LPFC_SBUF_XBUSY         0x1     /* SLI4 hba reported XB on WCQE cmpl */
#define LPFC_BUMP_QDEPTH	0x2	/* bumped queue depth counter */
	uint16_t exch_busy;     /* SLI4 hba reported XB on complete WCQE */
	uint16_t status;	/* From IOCB Word 7- ulpStatus */
	uint16_t cpu;
	uint16_t qidx;
	uint16_t sqid;
	uint32_t result;	/* From IOCB Word 4. */

	uint32_t   seg_cnt;	/* Number of scatter-gather segments returned by
				 * dma_map_sg.  The driver needs this for calls
				 * to dma_unmap_sg.
				 */
	dma_addr_t nonsg_phys;	/* Non scatter-gather physical address. */

	/*
	 * data and dma_handle are the kernel virtual and bus address of the
	 * dma-able buffer containing the fcp_cmd, fcp_rsp and a scatter
	 * gather bde list that supports the sg_tablesize value.
	 */
	void *data;
	dma_addr_t dma_handle;

	struct sli4_sge *nvme_sgl;
	dma_addr_t dma_phys_sgl;

	/* cur_iocbq has phys of the dma-able buffer.
	 * Iotag is in here
	 */
	struct lpfc_iocbq cur_iocbq;

	wait_queue_head_t *waitq;
	unsigned long start_time;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint64_t ts_cmd_start;
	uint64_t ts_last_cmd;
	uint64_t ts_cmd_wqput;
	uint64_t ts_isr_cmpl;
	uint64_t ts_data_nvme;
#endif
};

struct lpfc_nvme_fcpreq_priv {
	struct lpfc_nvme_buf *nvme_buf;
};
