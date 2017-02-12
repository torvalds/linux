/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
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

#define LPFC_NVMET_MIN_SEGS		16
#define LPFC_NVMET_DEFAULT_SEGS		64	/* 256K IOs */
#define LPFC_NVMET_MAX_SEGS		510
#define LPFC_NVMET_SUCCESS_LEN	12

/* Used for NVME Target */
struct lpfc_nvmet_tgtport {
	struct lpfc_hba *phba;
	struct completion tport_unreg_done;

	/* Stats counters - lpfc_nvmet_unsol_ls_buffer */
	atomic_t rcv_ls_req_in;
	atomic_t rcv_ls_req_out;
	atomic_t rcv_ls_req_drop;
	atomic_t xmt_ls_abort;

	/* Stats counters - lpfc_nvmet_xmt_ls_rsp */
	atomic_t xmt_ls_rsp;
	atomic_t xmt_ls_drop;

	/* Stats counters - lpfc_nvmet_xmt_ls_rsp_cmp */
	atomic_t xmt_ls_rsp_error;
	atomic_t xmt_ls_rsp_cmpl;

	/* Stats counters - lpfc_nvmet_unsol_fcp_buffer */
	atomic_t rcv_fcp_cmd_in;
	atomic_t rcv_fcp_cmd_out;
	atomic_t rcv_fcp_cmd_drop;

	/* Stats counters - lpfc_nvmet_xmt_fcp_op */
	atomic_t xmt_fcp_abort;
	atomic_t xmt_fcp_drop;
	atomic_t xmt_fcp_read_rsp;
	atomic_t xmt_fcp_read;
	atomic_t xmt_fcp_write;
	atomic_t xmt_fcp_rsp;

	/* Stats counters - lpfc_nvmet_xmt_fcp_op_cmp */
	atomic_t xmt_fcp_rsp_cmpl;
	atomic_t xmt_fcp_rsp_error;
	atomic_t xmt_fcp_rsp_drop;


	/* Stats counters - lpfc_nvmet_unsol_issue_abort */
	atomic_t xmt_abort_rsp;
	atomic_t xmt_abort_rsp_error;

	/* Stats counters - lpfc_nvmet_xmt_abort_cmp */
	atomic_t xmt_abort_cmpl;
};

struct lpfc_nvmet_rcv_ctx {
	union {
		struct nvmefc_tgt_ls_req ls_req;
		struct nvmefc_tgt_fcp_req fcp_req;
	} ctx;
	struct lpfc_hba *phba;
	struct lpfc_iocbq *wqeq;
	struct lpfc_iocbq *abort_wqeq;
	dma_addr_t txrdy_phys;
	uint32_t *txrdy;
	uint32_t sid;
	uint32_t offset;
	uint16_t oxid;
	uint16_t size;
	uint16_t entry_cnt;
	uint16_t cpu;
	uint16_t state;
	/* States */
#define LPFC_NVMET_STE_FREE		0
#define LPFC_NVMET_STE_RCV		1
#define LPFC_NVMET_STE_DATA		2
#define LPFC_NVMET_STE_ABORT		3
#define LPFC_NVMET_STE_RSP		4
#define LPFC_NVMET_STE_DONE		5
	uint16_t flag;
#define LPFC_NVMET_IO_INP		1
#define LPFC_NVMET_ABORT_OP		2
	struct rqb_dmabuf *rqb_buffer;

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
