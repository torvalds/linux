/* fcp.h: Definitions for Fibre Channel Protocol.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 */

#ifndef __FCP_H
#define __FCP_H

/* FCP addressing is hierarchical with up to 4 layers, MS first.
   Exact meaning of the addresses is up to the vendor */

/* fcp_cntl field */   
#define FCP_CNTL_WRITE		0x00000001	/* Initiator write */
#define FCP_CNTL_READ		0x00000002	/* Initiator read */
#define FCP_CNTL_ABORT_TSK	0x00000200	/* Abort task set */
#define FCP_CNTL_CLR_TASK	0x00000400	/* Clear task set */
#define FCP_CNTL_RESET		0x00002000	/* Reset */
#define FCP_CNTL_CLR_ACA	0x00004000	/* Clear ACA */
#define FCP_CNTL_KILL_TASK	0x00008000	/* Terminate task */
#define FCP_CNTL_QTYPE_MASK	0x00070000	/* Tagged queueing type */
#define 	FCP_CNTL_QTYPE_SIMPLE		0x00000000
#define 	FCP_CNTL_QTYPE_HEAD_OF_Q	0x00010000
#define		FCP_CNTL_QTYPE_ORDERED		0x00020000
#define 	FCP_CNTL_QTYPE_ACA_Q_TAG	0x00040000
#define 	FCP_CNTL_QTYPE_UNTAGGED		0x00050000

typedef struct {
	u16	fcp_addr[4];
	u32	fcp_cntl;
	u8	fcp_cdb[16];
	u32	fcp_data_len;
} fcp_cmd;

/* fcp_status field */
#define	FCP_STATUS_MASK		0x000000ff	/* scsi status of command */
#define FCP_STATUS_RSP_LEN	0x00000100	/* response_len != 0 */
#define FCP_STATUS_SENSE_LEN	0x00000200	/* sense_len != 0 */
#define FCP_STATUS_RESID	0x00000400	/* resid != 0 */

typedef struct {
	u32	xxx[2];
	u32	fcp_status;
	u32	fcp_resid;
	u32	fcp_sense_len;
	u32	fcp_response_len;
	/* u8	fcp_sense[fcp_sense_len]; */
	/* u8	fcp_response[fcp_response_len]; */
} fcp_rsp;

/* fcp errors */

/* rsp_info_type field */
#define FCP_RSP_SCSI_BUS_ERR	0x01
#define FCP_RSP_SCSI_PORT_ERR	0x02
#define FCP_RSP_CARD_ERR	0x03

/* isp_status field */
#define FCP_RSP_CMD_COMPLETE	0x0000
#define FCP_RSP_CMD_INCOMPLETE	0x0001
#define FCP_RSP_CMD_DMA_ERR	0x0002
#define FCP_RSP_CMD_TRAN_ERR	0x0003
#define FCP_RSP_CMD_RESET	0x0004
#define FCP_RSP_CMD_ABORTED	0x0005
#define FCP_RSP_CMD_TIMEOUT	0x0006
#define FCP_RSP_CMD_OVERRUN	0x0007

/* isp_state_flags field */
#define FCP_RSP_ST_GOT_BUS	0x0100
#define FCP_RSP_ST_GOT_TARGET	0x0200
#define FCP_RSP_ST_SENT_CMD	0x0400
#define FCP_RSP_ST_XFRD_DATA	0x0800
#define FCP_RSP_ST_GOT_STATUS	0x1000
#define FCP_RSP_ST_GOT_SENSE	0x2000

/* isp_stat_flags field */
#define FCP_RSP_STAT_DISC	0x0001
#define FCP_RSP_STAT_SYNC	0x0002
#define FCP_RSP_STAT_PERR	0x0004
#define FCP_RSP_STAT_BUS_RESET	0x0008
#define FCP_RSP_STAT_DEV_RESET	0x0010
#define FCP_RSP_STAT_ABORTED	0x0020
#define FCP_RSP_STAT_TIMEOUT	0x0040
#define FCP_RSP_STAT_NEGOTIATE	0x0080

typedef struct {
	u8	rsp_info_type;
	u8	xxx;
	u16	isp_status;
	u16	isp_state_flags;
	u16	isp_stat_flags;
} fcp_scsi_err;

#endif /* !(__FCP_H) */
