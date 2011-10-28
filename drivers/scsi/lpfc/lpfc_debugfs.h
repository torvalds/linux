/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2007-2011 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
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

/* hbqinfo output buffer size */
#define LPFC_HBQINFO_SIZE 8192

/* pciConf */
#define LPFC_PCI_CFG_BROWSE 0xffff
#define LPFC_PCI_CFG_RD_CMD_ARG 2
#define LPFC_PCI_CFG_WR_CMD_ARG 3
#define LPFC_PCI_CFG_SIZE 4096
#define LPFC_PCI_CFG_RD_BUF_SIZE (LPFC_PCI_CFG_SIZE/2)
#define LPFC_PCI_CFG_RD_SIZE (LPFC_PCI_CFG_SIZE/4)

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

/* doorbell acc */
#define LPFC_DRB_ACC_ALL 0xffff
#define LPFC_DRB_ACC_RD_CMD_ARG 1
#define LPFC_DRB_ACC_WR_CMD_ARG 2
#define LPFC_DRB_ACC_BUF_SIZE 256

#define LPFC_DRB_EQCQ 1
#define LPFC_DRB_MQ   2
#define LPFC_DRB_WQ   3
#define LPFC_DRB_RQ   4

#define LPFC_DRB_MAX  4

#define SIZE_U8  sizeof(uint8_t)
#define SIZE_U16 sizeof(uint16_t)
#define SIZE_U32 sizeof(uint32_t)

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

#define LPFC_IDIAG_CMD_QUEACC_RD 0x00000011
#define LPFC_IDIAG_CMD_QUEACC_WR 0x00000012
#define LPFC_IDIAG_CMD_QUEACC_ST 0x00000013
#define LPFC_IDIAG_CMD_QUEACC_CL 0x00000014

#define LPFC_IDIAG_CMD_DRBACC_RD 0x00000021
#define LPFC_IDIAG_CMD_DRBACC_WR 0x00000022
#define LPFC_IDIAG_CMD_DRBACC_ST 0x00000023
#define LPFC_IDIAG_CMD_DRBACC_CL 0x00000024
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
