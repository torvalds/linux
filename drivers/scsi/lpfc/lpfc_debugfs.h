/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017 Broadcom. All Rights Reserved. The term      *
 * “Broadcom” refers to Broadcom Limited and/or its subsidiaries.  *
 * Copyright (C) 2007-2011 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
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
 *******************************************************************/

#ifndef _H_LPFC_DEBUG_FS
#define _H_LPFC_DEBUG_FS

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS

/* size of output line, for discovery_trace and slow_ring_trace */
#define LPFC_DEBUG_TRC_ENTRY_SIZE 100

/* nodelist output buffer size */
#define LPFC_NODELIST_SIZE 8192
#define LPFC_NODELIST_ENTRY_SIZE 120

/* dumpHBASlim output buffer size */
#define LPFC_DUMPHBASLIM_SIZE 4096

/* dumpHostSlim output buffer size */
#define LPFC_DUMPHOSTSLIM_SIZE 4096

/* dumpSLIqinfo output buffer size */
#define	LPFC_DUMPSLIQINFO_SIZE 4096

/* hbqinfo output buffer size */
#define LPFC_HBQINFO_SIZE 8192

enum {
	DUMP_FCP,
	DUMP_NVME,
	DUMP_MBX,
	DUMP_ELS,
	DUMP_NVMELS,
};

/* nvmestat output buffer size */
#define LPFC_NVMESTAT_SIZE 8192
#define LPFC_NVMEKTIME_SIZE 8192
#define LPFC_CPUCHECK_SIZE 8192
#define LPFC_NVMEIO_TRC_SIZE 8192

#define LPFC_DEBUG_OUT_LINE_SZ	80

/*
 * For SLI4 iDiag debugfs diagnostics tool
 */

/* pciConf */
#define LPFC_PCI_CFG_BROWSE 0xffff
#define LPFC_PCI_CFG_RD_CMD_ARG 2
#define LPFC_PCI_CFG_WR_CMD_ARG 3
#define LPFC_PCI_CFG_SIZE 4096
#define LPFC_PCI_CFG_RD_SIZE (LPFC_PCI_CFG_SIZE/4)

#define IDIAG_PCICFG_WHERE_INDX 0
#define IDIAG_PCICFG_COUNT_INDX 1
#define IDIAG_PCICFG_VALUE_INDX 2

/* barAcc */
#define LPFC_PCI_BAR_BROWSE 0xffff
#define LPFC_PCI_BAR_RD_CMD_ARG 3
#define LPFC_PCI_BAR_WR_CMD_ARG 3

#define LPFC_PCI_IF0_BAR0_SIZE (1024 *  16)
#define LPFC_PCI_IF0_BAR1_SIZE (1024 * 128)
#define LPFC_PCI_IF0_BAR2_SIZE (1024 * 128)
#define LPFC_PCI_IF2_BAR0_SIZE (1024 *  32)

#define LPFC_PCI_BAR_RD_BUF_SIZE 4096
#define LPFC_PCI_BAR_RD_SIZE (LPFC_PCI_BAR_RD_BUF_SIZE/4)

#define LPFC_PCI_IF0_BAR0_RD_SIZE (LPFC_PCI_IF0_BAR0_SIZE/4)
#define LPFC_PCI_IF0_BAR1_RD_SIZE (LPFC_PCI_IF0_BAR1_SIZE/4)
#define LPFC_PCI_IF0_BAR2_RD_SIZE (LPFC_PCI_IF0_BAR2_SIZE/4)
#define LPFC_PCI_IF2_BAR0_RD_SIZE (LPFC_PCI_IF2_BAR0_SIZE/4)

#define IDIAG_BARACC_BAR_NUM_INDX 0
#define IDIAG_BARACC_OFF_SET_INDX 1
#define IDIAG_BARACC_ACC_MOD_INDX 2
#define IDIAG_BARACC_REG_VAL_INDX 2
#define IDIAG_BARACC_BAR_SZE_INDX 3

#define IDIAG_BARACC_BAR_0 0
#define IDIAG_BARACC_BAR_1 1
#define IDIAG_BARACC_BAR_2 2

#define SINGLE_WORD 1

/* queue info */
#define LPFC_QUE_INFO_GET_BUF_SIZE 4096

/* queue acc */
#define LPFC_QUE_ACC_BROWSE 0xffff
#define LPFC_QUE_ACC_RD_CMD_ARG 4
#define LPFC_QUE_ACC_WR_CMD_ARG 6
#define LPFC_QUE_ACC_BUF_SIZE 4096
#define LPFC_QUE_ACC_SIZE (LPFC_QUE_ACC_BUF_SIZE/2)

#define LPFC_IDIAG_EQ 1
#define LPFC_IDIAG_CQ 2
#define LPFC_IDIAG_MQ 3
#define LPFC_IDIAG_WQ 4
#define LPFC_IDIAG_RQ 5

#define IDIAG_QUEACC_QUETP_INDX 0
#define IDIAG_QUEACC_QUEID_INDX 1
#define IDIAG_QUEACC_INDEX_INDX 2
#define IDIAG_QUEACC_COUNT_INDX 3
#define IDIAG_QUEACC_OFFST_INDX 4
#define IDIAG_QUEACC_VALUE_INDX 5

/* doorbell register acc */
#define LPFC_DRB_ACC_ALL 0xffff
#define LPFC_DRB_ACC_RD_CMD_ARG 1
#define LPFC_DRB_ACC_WR_CMD_ARG 2
#define LPFC_DRB_ACC_BUF_SIZE 256

#define LPFC_DRB_EQCQ 1
#define LPFC_DRB_MQ   2
#define LPFC_DRB_WQ   3
#define LPFC_DRB_RQ   4

#define LPFC_DRB_MAX  4

#define IDIAG_DRBACC_REGID_INDX 0
#define IDIAG_DRBACC_VALUE_INDX 1

/* control register acc */
#define LPFC_CTL_ACC_ALL 0xffff
#define LPFC_CTL_ACC_RD_CMD_ARG 1
#define LPFC_CTL_ACC_WR_CMD_ARG 2
#define LPFC_CTL_ACC_BUF_SIZE 256

#define LPFC_CTL_PORT_SEM  1
#define LPFC_CTL_PORT_STA  2
#define LPFC_CTL_PORT_CTL  3
#define LPFC_CTL_PORT_ER1  4
#define LPFC_CTL_PORT_ER2  5
#define LPFC_CTL_PDEV_CTL  6

#define LPFC_CTL_MAX  6

#define IDIAG_CTLACC_REGID_INDX 0
#define IDIAG_CTLACC_VALUE_INDX 1

/* mailbox access */
#define LPFC_MBX_DMP_ARG 4

#define LPFC_MBX_ACC_BUF_SIZE 512
#define LPFC_MBX_ACC_LBUF_SZ 128

#define LPFC_MBX_DMP_MBX_WORD 0x00000001
#define LPFC_MBX_DMP_MBX_BYTE 0x00000002
#define LPFC_MBX_DMP_MBX_ALL (LPFC_MBX_DMP_MBX_WORD | LPFC_MBX_DMP_MBX_BYTE)

#define LPFC_BSG_DMP_MBX_RD_MBX 0x00000001
#define LPFC_BSG_DMP_MBX_RD_BUF 0x00000002
#define LPFC_BSG_DMP_MBX_WR_MBX 0x00000004
#define LPFC_BSG_DMP_MBX_WR_BUF 0x00000008
#define LPFC_BSG_DMP_MBX_ALL (LPFC_BSG_DMP_MBX_RD_MBX | \
			      LPFC_BSG_DMP_MBX_RD_BUF | \
			      LPFC_BSG_DMP_MBX_WR_MBX | \
			      LPFC_BSG_DMP_MBX_WR_BUF)

#define LPFC_MBX_DMP_ALL 0xffff
#define LPFC_MBX_ALL_CMD 0xff

#define IDIAG_MBXACC_MBCMD_INDX 0
#define IDIAG_MBXACC_DPMAP_INDX 1
#define IDIAG_MBXACC_DPCNT_INDX 2
#define IDIAG_MBXACC_WDCNT_INDX 3

/* extents access */
#define LPFC_EXT_ACC_CMD_ARG 1
#define LPFC_EXT_ACC_BUF_SIZE 4096

#define LPFC_EXT_ACC_AVAIL 0x1
#define LPFC_EXT_ACC_ALLOC 0x2
#define LPFC_EXT_ACC_DRIVR 0x4
#define LPFC_EXT_ACC_ALL   (LPFC_EXT_ACC_DRIVR | \
			    LPFC_EXT_ACC_AVAIL | \
			    LPFC_EXT_ACC_ALLOC)

#define IDIAG_EXTACC_EXMAP_INDX 0

#define SIZE_U8  sizeof(uint8_t)
#define SIZE_U16 sizeof(uint16_t)
#define SIZE_U32 sizeof(uint32_t)

#define lpfc_nvmeio_data(phba, fmt, arg...) \
	{ \
	if (phba->nvmeio_trc_on) \
		lpfc_debugfs_nvme_trc(phba, fmt, ##arg); \
	}

struct lpfc_debug {
	char *i_private;
	char op;
#define LPFC_IDIAG_OP_RD 1
#define LPFC_IDIAG_OP_WR 2
	char *buffer;
	int  len;
};

struct lpfc_debugfs_trc {
	char *fmt;
	uint32_t data1;
	uint32_t data2;
	uint32_t data3;
	uint32_t seq_cnt;
	unsigned long jif;
};

struct lpfc_debugfs_nvmeio_trc {
	char *fmt;
	uint16_t data1;
	uint16_t data2;
	uint32_t data3;
};

struct lpfc_idiag_offset {
	uint32_t last_rd;
};

#define LPFC_IDIAG_CMD_DATA_SIZE 8
struct lpfc_idiag_cmd {
	uint32_t opcode;
#define LPFC_IDIAG_CMD_PCICFG_RD 0x00000001
#define LPFC_IDIAG_CMD_PCICFG_WR 0x00000002
#define LPFC_IDIAG_CMD_PCICFG_ST 0x00000003
#define LPFC_IDIAG_CMD_PCICFG_CL 0x00000004

#define LPFC_IDIAG_CMD_BARACC_RD 0x00000008
#define LPFC_IDIAG_CMD_BARACC_WR 0x00000009
#define LPFC_IDIAG_CMD_BARACC_ST 0x0000000a
#define LPFC_IDIAG_CMD_BARACC_CL 0x0000000b

#define LPFC_IDIAG_CMD_QUEACC_RD 0x00000011
#define LPFC_IDIAG_CMD_QUEACC_WR 0x00000012
#define LPFC_IDIAG_CMD_QUEACC_ST 0x00000013
#define LPFC_IDIAG_CMD_QUEACC_CL 0x00000014

#define LPFC_IDIAG_CMD_DRBACC_RD 0x00000021
#define LPFC_IDIAG_CMD_DRBACC_WR 0x00000022
#define LPFC_IDIAG_CMD_DRBACC_ST 0x00000023
#define LPFC_IDIAG_CMD_DRBACC_CL 0x00000024

#define LPFC_IDIAG_CMD_CTLACC_RD 0x00000031
#define LPFC_IDIAG_CMD_CTLACC_WR 0x00000032
#define LPFC_IDIAG_CMD_CTLACC_ST 0x00000033
#define LPFC_IDIAG_CMD_CTLACC_CL 0x00000034

#define LPFC_IDIAG_CMD_MBXACC_DP 0x00000041
#define LPFC_IDIAG_BSG_MBXACC_DP 0x00000042

#define LPFC_IDIAG_CMD_EXTACC_RD 0x00000051

	uint32_t data[LPFC_IDIAG_CMD_DATA_SIZE];
};

struct lpfc_idiag {
	uint32_t active;
	struct lpfc_idiag_cmd cmd;
	struct lpfc_idiag_offset offset;
	void *ptr_private;
};
#endif

/* Mask for discovery_trace */
#define LPFC_DISC_TRC_ELS_CMD		0x1	/* Trace ELS commands */
#define LPFC_DISC_TRC_ELS_RSP		0x2	/* Trace ELS response */
#define LPFC_DISC_TRC_ELS_UNSOL		0x4	/* Trace ELS rcv'ed   */
#define LPFC_DISC_TRC_ELS_ALL		0x7	/* Trace ELS */
#define LPFC_DISC_TRC_MBOX_VPORT	0x8	/* Trace vport MBOXs */
#define LPFC_DISC_TRC_MBOX		0x10	/* Trace other MBOXs */
#define LPFC_DISC_TRC_MBOX_ALL		0x18	/* Trace all MBOXs */
#define LPFC_DISC_TRC_CT		0x20	/* Trace disc CT requests */
#define LPFC_DISC_TRC_DSM		0x40    /* Trace DSM events */
#define LPFC_DISC_TRC_RPORT		0x80    /* Trace rport events */
#define LPFC_DISC_TRC_NODE		0x100   /* Trace ndlp state changes */

#define LPFC_DISC_TRC_DISCOVERY		0xef    /* common mask for general
						 * discovery */
#endif /* H_LPFC_DEBUG_FS */


/*
 * Driver debug utility routines outside of debugfs. The debug utility
 * routines implemented here is intended to be used in the instrumented
 * debug driver for debugging host or port issues.
 */

/**
 * lpfc_debug_dump_qe - dump an specific entry from a queue
 * @q: Pointer to the queue descriptor.
 * @idx: Index to the entry on the queue.
 *
 * This function dumps an entry indexed by @idx from a queue specified by the
 * queue descriptor @q.
 **/
static inline void
lpfc_debug_dump_qe(struct lpfc_queue *q, uint32_t idx)
{
	char line_buf[LPFC_LBUF_SZ];
	int i, esize, qe_word_cnt, len;
	uint32_t *pword;

	/* sanity checks */
	if (!q)
		return;
	if (idx >= q->entry_count)
		return;

	esize = q->entry_size;
	qe_word_cnt = esize / sizeof(uint32_t);
	pword = q->qe[idx].address;

	len = 0;
	len += snprintf(line_buf+len, LPFC_LBUF_SZ-len, "QE[%04d]: ", idx);
	if (qe_word_cnt > 8)
		printk(KERN_ERR "%s\n", line_buf);

	for (i = 0; i < qe_word_cnt; i++) {
		if (!(i % 8)) {
			if (i != 0)
				printk(KERN_ERR "%s\n", line_buf);
			if (qe_word_cnt > 8) {
				len = 0;
				memset(line_buf, 0, LPFC_LBUF_SZ);
				len += snprintf(line_buf+len, LPFC_LBUF_SZ-len,
						"%03d: ", i);
			}
		}
		len += snprintf(line_buf+len, LPFC_LBUF_SZ-len, "%08x ",
				((uint32_t)*pword) & 0xffffffff);
		pword++;
	}
	if (qe_word_cnt <= 8 || (i - 1) % 8)
		printk(KERN_ERR "%s\n", line_buf);
}

/**
 * lpfc_debug_dump_q - dump all entries from an specific queue
 * @q: Pointer to the queue descriptor.
 *
 * This function dumps all entries from a queue specified by the queue
 * descriptor @q.
 **/
static inline void
lpfc_debug_dump_q(struct lpfc_queue *q)
{
	int idx, entry_count;

	/* sanity check */
	if (!q)
		return;

	dev_printk(KERN_ERR, &(((q->phba))->pcidev)->dev,
		"%d: [qid:%d, type:%d, subtype:%d, "
		"qe_size:%d, qe_count:%d, "
		"host_index:%d, port_index:%d]\n",
		(q->phba)->brd_no,
		q->queue_id, q->type, q->subtype,
		q->entry_size, q->entry_count,
		q->host_index, q->hba_index);
	entry_count = q->entry_count;
	for (idx = 0; idx < entry_count; idx++)
		lpfc_debug_dump_qe(q, idx);
	printk(KERN_ERR "\n");
}

/**
 * lpfc_debug_dump_wq - dump all entries from the fcp or nvme work queue
 * @phba: Pointer to HBA context object.
 * @wqidx: Index to a FCP or NVME work queue.
 *
 * This function dumps all entries from a FCP or NVME work queue specified
 * by the wqidx.
 **/
static inline void
lpfc_debug_dump_wq(struct lpfc_hba *phba, int qtype, int wqidx)
{
	struct lpfc_queue *wq;
	char *qtypestr;

	if (qtype == DUMP_FCP) {
		wq = phba->sli4_hba.fcp_wq[wqidx];
		qtypestr = "FCP";
	} else if (qtype == DUMP_NVME) {
		wq = phba->sli4_hba.nvme_wq[wqidx];
		qtypestr = "NVME";
	} else if (qtype == DUMP_MBX) {
		wq = phba->sli4_hba.mbx_wq;
		qtypestr = "MBX";
	} else if (qtype == DUMP_ELS) {
		wq = phba->sli4_hba.els_wq;
		qtypestr = "ELS";
	} else if (qtype == DUMP_NVMELS) {
		wq = phba->sli4_hba.nvmels_wq;
		qtypestr = "NVMELS";
	} else
		return;

	if (qtype == DUMP_FCP || qtype == DUMP_NVME)
		pr_err("%s WQ: WQ[Idx:%d|Qid:%d]\n",
			qtypestr, wqidx, wq->queue_id);
	else
		pr_err("%s WQ: WQ[Qid:%d]\n",
			qtypestr, wq->queue_id);

	lpfc_debug_dump_q(wq);
}

/**
 * lpfc_debug_dump_cq - dump all entries from a fcp or nvme work queue's
 * cmpl queue
 * @phba: Pointer to HBA context object.
 * @wqidx: Index to a FCP work queue.
 *
 * This function dumps all entries from a FCP or NVME completion queue
 * which is associated to the work queue specified by the @wqidx.
 **/
static inline void
lpfc_debug_dump_cq(struct lpfc_hba *phba, int qtype, int wqidx)
{
	struct lpfc_queue *wq, *cq, *eq;
	char *qtypestr;
	int eqidx;

	/* fcp/nvme wq and cq are 1:1, thus same indexes */

	if (qtype == DUMP_FCP) {
		wq = phba->sli4_hba.fcp_wq[wqidx];
		cq = phba->sli4_hba.fcp_cq[wqidx];
		qtypestr = "FCP";
	} else if (qtype == DUMP_NVME) {
		wq = phba->sli4_hba.nvme_wq[wqidx];
		cq = phba->sli4_hba.nvme_cq[wqidx];
		qtypestr = "NVME";
	} else if (qtype == DUMP_MBX) {
		wq = phba->sli4_hba.mbx_wq;
		cq = phba->sli4_hba.mbx_cq;
		qtypestr = "MBX";
	} else if (qtype == DUMP_ELS) {
		wq = phba->sli4_hba.els_wq;
		cq = phba->sli4_hba.els_cq;
		qtypestr = "ELS";
	} else if (qtype == DUMP_NVMELS) {
		wq = phba->sli4_hba.nvmels_wq;
		cq = phba->sli4_hba.nvmels_cq;
		qtypestr = "NVMELS";
	} else
		return;

	for (eqidx = 0; eqidx < phba->io_channel_irqs; eqidx++) {
		eq = phba->sli4_hba.hba_eq[eqidx];
		if (cq->assoc_qid == eq->queue_id)
			break;
	}
	if (eqidx == phba->io_channel_irqs) {
		pr_err("Couldn't find EQ for CQ. Using EQ[0]\n");
		eqidx = 0;
		eq = phba->sli4_hba.hba_eq[0];
	}

	if (qtype == DUMP_FCP || qtype == DUMP_NVME)
		pr_err("%s CQ: WQ[Idx:%d|Qid%d]->CQ[Idx%d|Qid%d]"
			"->EQ[Idx:%d|Qid:%d]:\n",
			qtypestr, wqidx, wq->queue_id, wqidx, cq->queue_id,
			eqidx, eq->queue_id);
	else
		pr_err("%s CQ: WQ[Qid:%d]->CQ[Qid:%d]"
			"->EQ[Idx:%d|Qid:%d]:\n",
			qtypestr, wq->queue_id, cq->queue_id,
			eqidx, eq->queue_id);

	lpfc_debug_dump_q(cq);
}

/**
 * lpfc_debug_dump_hba_eq - dump all entries from a fcp work queue's evt queue
 * @phba: Pointer to HBA context object.
 * @fcp_wqidx: Index to a FCP work queue.
 *
 * This function dumps all entries from a FCP event queue which is
 * associated to the FCP work queue specified by the @fcp_wqidx.
 **/
static inline void
lpfc_debug_dump_hba_eq(struct lpfc_hba *phba, int qidx)
{
	struct lpfc_queue *qp;

	qp = phba->sli4_hba.hba_eq[qidx];

	pr_err("EQ[Idx:%d|Qid:%d]\n", qidx, qp->queue_id);

	lpfc_debug_dump_q(qp);
}

/**
 * lpfc_debug_dump_dat_rq - dump all entries from the receive data queue
 * @phba: Pointer to HBA context object.
 *
 * This function dumps all entries from the receive data queue.
 **/
static inline void
lpfc_debug_dump_dat_rq(struct lpfc_hba *phba)
{
	printk(KERN_ERR "DAT RQ: RQ[Qid:%d]\n",
		phba->sli4_hba.dat_rq->queue_id);
	lpfc_debug_dump_q(phba->sli4_hba.dat_rq);
}

/**
 * lpfc_debug_dump_hdr_rq - dump all entries from the receive header queue
 * @phba: Pointer to HBA context object.
 *
 * This function dumps all entries from the receive header queue.
 **/
static inline void
lpfc_debug_dump_hdr_rq(struct lpfc_hba *phba)
{
	printk(KERN_ERR "HDR RQ: RQ[Qid:%d]\n",
		phba->sli4_hba.hdr_rq->queue_id);
	lpfc_debug_dump_q(phba->sli4_hba.hdr_rq);
}

/**
 * lpfc_debug_dump_wq_by_id - dump all entries from a work queue by queue id
 * @phba: Pointer to HBA context object.
 * @qid: Work queue identifier.
 *
 * This function dumps all entries from a work queue identified by the queue
 * identifier.
 **/
static inline void
lpfc_debug_dump_wq_by_id(struct lpfc_hba *phba, int qid)
{
	int wq_idx;

	for (wq_idx = 0; wq_idx < phba->cfg_fcp_io_channel; wq_idx++)
		if (phba->sli4_hba.fcp_wq[wq_idx]->queue_id == qid)
			break;
	if (wq_idx < phba->cfg_fcp_io_channel) {
		pr_err("FCP WQ[Idx:%d|Qid:%d]\n", wq_idx, qid);
		lpfc_debug_dump_q(phba->sli4_hba.fcp_wq[wq_idx]);
		return;
	}

	for (wq_idx = 0; wq_idx < phba->cfg_nvme_io_channel; wq_idx++)
		if (phba->sli4_hba.nvme_wq[wq_idx]->queue_id == qid)
			break;
	if (wq_idx < phba->cfg_nvme_io_channel) {
		pr_err("NVME WQ[Idx:%d|Qid:%d]\n", wq_idx, qid);
		lpfc_debug_dump_q(phba->sli4_hba.nvme_wq[wq_idx]);
		return;
	}

	if (phba->sli4_hba.els_wq->queue_id == qid) {
		pr_err("ELS WQ[Qid:%d]\n", qid);
		lpfc_debug_dump_q(phba->sli4_hba.els_wq);
		return;
	}

	if (phba->sli4_hba.nvmels_wq->queue_id == qid) {
		pr_err("NVME LS WQ[Qid:%d]\n", qid);
		lpfc_debug_dump_q(phba->sli4_hba.nvmels_wq);
	}
}

/**
 * lpfc_debug_dump_mq_by_id - dump all entries from a mbox queue by queue id
 * @phba: Pointer to HBA context object.
 * @qid: Mbox work queue identifier.
 *
 * This function dumps all entries from a mbox work queue identified by the
 * queue identifier.
 **/
static inline void
lpfc_debug_dump_mq_by_id(struct lpfc_hba *phba, int qid)
{
	if (phba->sli4_hba.mbx_wq->queue_id == qid) {
		printk(KERN_ERR "MBX WQ[Qid:%d]\n", qid);
		lpfc_debug_dump_q(phba->sli4_hba.mbx_wq);
	}
}

/**
 * lpfc_debug_dump_rq_by_id - dump all entries from a receive queue by queue id
 * @phba: Pointer to HBA context object.
 * @qid: Receive queue identifier.
 *
 * This function dumps all entries from a receive queue identified by the
 * queue identifier.
 **/
static inline void
lpfc_debug_dump_rq_by_id(struct lpfc_hba *phba, int qid)
{
	if (phba->sli4_hba.hdr_rq->queue_id == qid) {
		printk(KERN_ERR "HDR RQ[Qid:%d]\n", qid);
		lpfc_debug_dump_q(phba->sli4_hba.hdr_rq);
		return;
	}
	if (phba->sli4_hba.dat_rq->queue_id == qid) {
		printk(KERN_ERR "DAT RQ[Qid:%d]\n", qid);
		lpfc_debug_dump_q(phba->sli4_hba.dat_rq);
	}
}

/**
 * lpfc_debug_dump_cq_by_id - dump all entries from a cmpl queue by queue id
 * @phba: Pointer to HBA context object.
 * @qid: Complete queue identifier.
 *
 * This function dumps all entries from a complete queue identified by the
 * queue identifier.
 **/
static inline void
lpfc_debug_dump_cq_by_id(struct lpfc_hba *phba, int qid)
{
	int cq_idx;

	for (cq_idx = 0; cq_idx < phba->cfg_fcp_io_channel; cq_idx++)
		if (phba->sli4_hba.fcp_cq[cq_idx]->queue_id == qid)
			break;

	if (cq_idx < phba->cfg_fcp_io_channel) {
		pr_err("FCP CQ[Idx:%d|Qid:%d]\n", cq_idx, qid);
		lpfc_debug_dump_q(phba->sli4_hba.fcp_cq[cq_idx]);
		return;
	}

	for (cq_idx = 0; cq_idx < phba->cfg_nvme_io_channel; cq_idx++)
		if (phba->sli4_hba.nvme_cq[cq_idx]->queue_id == qid)
			break;

	if (cq_idx < phba->cfg_nvme_io_channel) {
		pr_err("NVME CQ[Idx:%d|Qid:%d]\n", cq_idx, qid);
		lpfc_debug_dump_q(phba->sli4_hba.nvme_cq[cq_idx]);
		return;
	}

	if (phba->sli4_hba.els_cq->queue_id == qid) {
		pr_err("ELS CQ[Qid:%d]\n", qid);
		lpfc_debug_dump_q(phba->sli4_hba.els_cq);
		return;
	}

	if (phba->sli4_hba.nvmels_cq->queue_id == qid) {
		pr_err("NVME LS CQ[Qid:%d]\n", qid);
		lpfc_debug_dump_q(phba->sli4_hba.nvmels_cq);
		return;
	}

	if (phba->sli4_hba.mbx_cq->queue_id == qid) {
		pr_err("MBX CQ[Qid:%d]\n", qid);
		lpfc_debug_dump_q(phba->sli4_hba.mbx_cq);
	}
}

/**
 * lpfc_debug_dump_eq_by_id - dump all entries from an event queue by queue id
 * @phba: Pointer to HBA context object.
 * @qid: Complete queue identifier.
 *
 * This function dumps all entries from an event queue identified by the
 * queue identifier.
 **/
static inline void
lpfc_debug_dump_eq_by_id(struct lpfc_hba *phba, int qid)
{
	int eq_idx;

	for (eq_idx = 0; eq_idx < phba->io_channel_irqs; eq_idx++)
		if (phba->sli4_hba.hba_eq[eq_idx]->queue_id == qid)
			break;

	if (eq_idx < phba->io_channel_irqs) {
		printk(KERN_ERR "FCP EQ[Idx:%d|Qid:%d]\n", eq_idx, qid);
		lpfc_debug_dump_q(phba->sli4_hba.hba_eq[eq_idx]);
		return;
	}
}

void lpfc_debug_dump_all_queues(struct lpfc_hba *);
