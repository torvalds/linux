/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __FCPPROTO_H__
#define __FCPPROTO_H__

#include <linux/bitops.h>
#include <protocol/scsi.h>

#pragma pack(1)

enum {
	FCP_RJT		= 0x01000000,	/* SRR reject */
	FCP_SRR_ACCEPT	= 0x02000000,	/* SRR accept */
	FCP_SRR		= 0x14000000,	/* Sequence Retransmission Request */
};

/*
 * SRR FC-4 LS payload
 */
struct fc_srr_s{
	u32	ls_cmd;
	u32        ox_id:16;	/* ox-id */
	u32        rx_id:16;	/* rx-id */
	u32        ro;		/* relative offset */
	u32        r_ctl:8;		/* R_CTL for I.U. */
	u32        res:24;
};


/*
 * FCP_CMND definitions
 */
#define FCP_CMND_CDB_LEN    16
#define FCP_CMND_LUN_LEN    8

struct fcp_cmnd_s{
	lun_t           lun;		/* 64-bit LU number */
	u8         crn;		/* command reference number */
#ifdef __BIGENDIAN
	u8         resvd:1,
			priority:4,	/* FCP-3: SAM-3 priority */
			taskattr:3;	/* scsi task attribute */
#else
	u8         taskattr:3,	/* scsi task attribute */
			priority:4,	/* FCP-3: SAM-3 priority */
			resvd:1;
#endif
	u8         tm_flags;	/* task management flags */
#ifdef __BIGENDIAN
	u8         addl_cdb_len:6,	/* additional CDB length words */
			iodir:2;	/* read/write FCP_DATA IUs */
#else
	u8         iodir:2,	/* read/write FCP_DATA IUs */
			addl_cdb_len:6;	/* additional CDB length */
#endif
	struct scsi_cdb_s      cdb;

	/*
	 * !!! additional cdb bytes follows here!!!
	 */
	u32        fcp_dl;	/* bytes to be transferred */
};

#define fcp_cmnd_cdb_len(_cmnd) ((_cmnd)->addl_cdb_len * 4 + FCP_CMND_CDB_LEN)
#define fcp_cmnd_fcpdl(_cmnd)	((&(_cmnd)->fcp_dl)[(_cmnd)->addl_cdb_len])

/*
 * fcp_cmnd_t.iodir field values
 */
enum fcp_iodir{
	FCP_IODIR_NONE	= 0,
	FCP_IODIR_WRITE = 1,
	FCP_IODIR_READ	= 2,
	FCP_IODIR_RW	= 3,
};

/*
 * Task attribute field
 */
enum {
	FCP_TASK_ATTR_SIMPLE	= 0,
	FCP_TASK_ATTR_HOQ	= 1,
	FCP_TASK_ATTR_ORDERED	= 2,
	FCP_TASK_ATTR_ACA	= 4,
	FCP_TASK_ATTR_UNTAGGED	= 5,	/* obsolete in FCP-3 */
};

/*
 * Task management flags field - only one bit shall be set
 */
enum fcp_tm_cmnd{
	FCP_TM_ABORT_TASK_SET	= BIT(1),
	FCP_TM_CLEAR_TASK_SET	= BIT(2),
	FCP_TM_LUN_RESET	= BIT(4),
	FCP_TM_TARGET_RESET	= BIT(5),	/* obsolete in FCP-3 */
	FCP_TM_CLEAR_ACA	= BIT(6),
};

/*
 * FCP_XFER_RDY IU defines
 */
struct fcp_xfer_rdy_s{
	u32        data_ro;
	u32        burst_len;
	u32        reserved;
};

/*
 * FCP_RSP residue flags
 */
enum fcp_residue{
	FCP_NO_RESIDUE = 0,	/* no residue */
	FCP_RESID_OVER = 1,	/* more data left that was not sent */
	FCP_RESID_UNDER = 2,	/* less data than requested */
};

enum {
	FCP_RSPINFO_GOOD = 0,
	FCP_RSPINFO_DATALEN_MISMATCH = 1,
	FCP_RSPINFO_CMND_INVALID = 2,
	FCP_RSPINFO_ROLEN_MISMATCH = 3,
	FCP_RSPINFO_TM_NOT_SUPP = 4,
	FCP_RSPINFO_TM_FAILED = 5,
};

struct fcp_rspinfo_s{
	u32        res0:24;
	u32        rsp_code:8;	/* response code (as above) */
	u32        res1;
};

struct fcp_resp_s{
	u32        reserved[2];	/* 2 words reserved */
	u16        reserved2;
#ifdef __BIGENDIAN
	u8         reserved3:3;
	u8         fcp_conf_req:1;	/* FCP_CONF is requested */
	u8         resid_flags:2;	/* underflow/overflow */
	u8         sns_len_valid:1;/* sense len is valid */
	u8         rsp_len_valid:1;/* response len is valid */
#else
	u8         rsp_len_valid:1;/* response len is valid */
	u8         sns_len_valid:1;/* sense len is valid */
	u8         resid_flags:2;	/* underflow/overflow */
	u8         fcp_conf_req:1;	/* FCP_CONF is requested */
	u8         reserved3:3;
#endif
	u8         scsi_status;	/* one byte SCSI status */
	u32        residue;	/* residual data bytes */
	u32        sns_len;	/* length od sense info */
	u32        rsp_len;	/* length of response info */
};

#define fcp_snslen(__fcprsp)	((__fcprsp)->sns_len_valid ? 		\
					(__fcprsp)->sns_len : 0)
#define fcp_rsplen(__fcprsp)	((__fcprsp)->rsp_len_valid ? 		\
					(__fcprsp)->rsp_len : 0)
#define fcp_rspinfo(__fcprsp)	((struct fcp_rspinfo_s *)((__fcprsp) + 1))
#define fcp_snsinfo(__fcprsp)	(((u8 *)fcp_rspinfo(__fcprsp)) + 	\
						fcp_rsplen(__fcprsp))

struct fcp_cmnd_fr_s{
	struct fchs_s          fchs;
	struct fcp_cmnd_s      fcp;
};

#pragma pack()

#endif
