/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017 Broadcom. All Rights Reserved. The term      *
 * “Broadcom” refers to Broadcom Limited and/or its subsidiaries.  *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
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

#include <asm/byteorder.h>

struct lpfc_hba;
#define LPFC_FCP_CDB_LEN 16

#define list_remove_head(list, entry, type, member)		\
	do {							\
	entry = NULL;						\
	if (!list_empty(list)) {				\
		entry = list_entry((list)->next, type, member);	\
		list_del_init(&entry->member);			\
	}							\
	} while(0)

#define list_get_first(list, type, member)			\
	(list_empty(list)) ? NULL :				\
	list_entry((list)->next, type, member)

/* per-port data that is allocated in the FC transport for us */
struct lpfc_rport_data {
	struct lpfc_nodelist *pnode;	/* Pointer to the node structure. */
};

struct lpfc_device_id {
	struct lpfc_name vport_wwpn;
	struct lpfc_name target_wwpn;
	uint64_t lun;
};

struct lpfc_device_data {
	struct list_head listentry;
	struct lpfc_rport_data *rport_data;
	struct lpfc_device_id device_id;
	uint8_t priority;
	bool oas_enabled;
	bool available;
};

struct fcp_rsp {
	uint32_t rspRsvd1;	/* FC Word 0, byte 0:3 */
	uint32_t rspRsvd2;	/* FC Word 1, byte 0:3 */

	uint8_t rspStatus0;	/* FCP_STATUS byte 0 (reserved) */
	uint8_t rspStatus1;	/* FCP_STATUS byte 1 (reserved) */
	uint8_t rspStatus2;	/* FCP_STATUS byte 2 field validity */
#define RSP_LEN_VALID  0x01	/* bit 0 */
#define SNS_LEN_VALID  0x02	/* bit 1 */
#define RESID_OVER     0x04	/* bit 2 */
#define RESID_UNDER    0x08	/* bit 3 */
	uint8_t rspStatus3;	/* FCP_STATUS byte 3 SCSI status byte */

	uint32_t rspResId;	/* Residual xfer if residual count field set in
				   fcpStatus2 */
	/* Received in Big Endian format */
	uint32_t rspSnsLen;	/* Length of sense data in fcpSnsInfo */
	/* Received in Big Endian format */
	uint32_t rspRspLen;	/* Length of FCP response data in fcpRspInfo */
	/* Received in Big Endian format */

	uint8_t rspInfo0;	/* FCP_RSP_INFO byte 0 (reserved) */
	uint8_t rspInfo1;	/* FCP_RSP_INFO byte 1 (reserved) */
	uint8_t rspInfo2;	/* FCP_RSP_INFO byte 2 (reserved) */
	uint8_t rspInfo3;	/* FCP_RSP_INFO RSP_CODE byte 3 */

#define RSP_NO_FAILURE       0x00
#define RSP_DATA_BURST_ERR   0x01
#define RSP_CMD_FIELD_ERR    0x02
#define RSP_RO_MISMATCH_ERR  0x03
#define RSP_TM_NOT_SUPPORTED 0x04	/* Task mgmt function not supported */
#define RSP_TM_NOT_COMPLETED 0x05	/* Task mgmt function not performed */
#define RSP_TM_INVALID_LU    0x09	/* Task mgmt function to invalid LU */

	uint32_t rspInfoRsvd;	/* FCP_RSP_INFO bytes 4-7 (reserved) */

	uint8_t rspSnsInfo[128];
#define SNS_ILLEGAL_REQ 0x05	/* sense key is byte 3 ([2]) */
#define SNSCOD_BADCMD 0x20	/* sense code is byte 13 ([12]) */
};

struct fcp_cmnd {
	struct scsi_lun  fcp_lun;

	uint8_t fcpCntl0;	/* FCP_CNTL byte 0 (reserved) */
	uint8_t fcpCntl1;	/* FCP_CNTL byte 1 task codes */
#define  SIMPLE_Q        0x00
#define  HEAD_OF_Q       0x01
#define  ORDERED_Q       0x02
#define  ACA_Q           0x04
#define  UNTAGGED        0x05
	uint8_t fcpCntl2;	/* FCP_CTL byte 2 task management codes */
#define  FCP_ABORT_TASK_SET  0x02	/* Bit 1 */
#define  FCP_CLEAR_TASK_SET  0x04	/* bit 2 */
#define  FCP_BUS_RESET       0x08	/* bit 3 */
#define  FCP_LUN_RESET       0x10	/* bit 4 */
#define  FCP_TARGET_RESET    0x20	/* bit 5 */
#define  FCP_CLEAR_ACA       0x40	/* bit 6 */
#define  FCP_TERMINATE_TASK  0x80	/* bit 7 */
	uint8_t fcpCntl3;
#define  WRITE_DATA      0x01	/* Bit 0 */
#define  READ_DATA       0x02	/* Bit 1 */

	uint8_t fcpCdb[LPFC_FCP_CDB_LEN]; /* SRB cdb field is copied here */
	uint32_t fcpDl;		/* Total transfer length */

};

struct lpfc_scsicmd_bkt {
	uint32_t cmd_count;
};

struct lpfc_scsi_buf {
	struct list_head list;
	struct scsi_cmnd *pCmd;
	struct lpfc_rport_data *rdata;

	uint32_t timeout;

	uint16_t flags;  /* TBD convert exch_busy to flags */
#define LPFC_SBUF_XBUSY         0x1     /* SLI4 hba reported XB on WCQE cmpl */
	uint16_t exch_busy;     /* SLI4 hba reported XB on complete WCQE */
	uint16_t status;	/* From IOCB Word 7- ulpStatus */
	uint32_t result;	/* From IOCB Word 4. */

	uint32_t   seg_cnt;	/* Number of scatter-gather segments returned by
				 * dma_map_sg.  The driver needs this for calls
				 * to dma_unmap_sg. */
	uint32_t prot_seg_cnt;  /* seg_cnt's counterpart for protection data */

	dma_addr_t nonsg_phys;	/* Non scatter-gather physical address. */

	/*
	 * data and dma_handle are the kernel virtual and bus address of the
	 * dma-able buffer containing the fcp_cmd, fcp_rsp and a scatter
	 * gather bde list that supports the sg_tablesize value.
	 */
	void *data;
	dma_addr_t dma_handle;

	struct fcp_cmnd *fcp_cmnd;
	struct fcp_rsp *fcp_rsp;
	struct ulp_bde64 *fcp_bpl;

	dma_addr_t dma_phys_bpl;

	/* cur_iocbq has phys of the dma-able buffer.
	 * Iotag is in here
	 */
	struct lpfc_iocbq cur_iocbq;
	uint16_t cpu;

	wait_queue_head_t *waitq;
	unsigned long start_time;

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	/* Used to restore any changes to protection data for error injection */
	void *prot_data_segment;
	uint32_t prot_data;
	uint32_t prot_data_type;
#define	LPFC_INJERR_REFTAG	1
#define	LPFC_INJERR_APPTAG	2
#define	LPFC_INJERR_GUARD	3
#endif
};

#define LPFC_SCSI_DMA_EXT_SIZE	264
#define LPFC_BPL_SIZE		1024
#define MDAC_DIRECT_CMD		0x22

#define FIND_FIRST_OAS_LUN	0
#define NO_MORE_OAS_LUN		-1
#define NOT_OAS_ENABLED_LUN	NO_MORE_OAS_LUN

#define TXRDY_PAYLOAD_LEN	12

int lpfc_sli4_scmd_to_wqidx_distr(struct lpfc_hba *phba,
				  struct lpfc_scsi_buf *lpfc_cmd);
