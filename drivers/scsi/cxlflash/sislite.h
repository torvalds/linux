/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CXL Flash Device Driver
 *
 * Written by: Manoj N. Kumar <manoj@linux.vnet.ibm.com>, IBM Corporation
 *             Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2015 IBM Corporation
 */

#ifndef _SISLITE_H
#define _SISLITE_H

#include <linux/types.h>

typedef u16 ctx_hndl_t;
typedef u32 res_hndl_t;

#define SIZE_4K		4096
#define SIZE_64K	65536

/*
 * IOARCB: 64 bytes, min 16 byte alignment required, host native endianness
 * except for SCSI CDB which remains big endian per SCSI standards.
 */
struct sisl_ioarcb {
	u16 ctx_id;		/* ctx_hndl_t */
	u16 req_flags;
#define SISL_REQ_FLAGS_RES_HNDL       0x8000U	/* bit 0 (MSB) */
#define SISL_REQ_FLAGS_PORT_LUN_ID    0x0000U

#define SISL_REQ_FLAGS_SUP_UNDERRUN   0x4000U	/* bit 1 */

#define SISL_REQ_FLAGS_TIMEOUT_SECS   0x0000U	/* bits 8,9 */
#define SISL_REQ_FLAGS_TIMEOUT_MSECS  0x0040U
#define SISL_REQ_FLAGS_TIMEOUT_USECS  0x0080U
#define SISL_REQ_FLAGS_TIMEOUT_CYCLES 0x00C0U

#define SISL_REQ_FLAGS_TMF_CMD        0x0004u	/* bit 13 */

#define SISL_REQ_FLAGS_AFU_CMD        0x0002U	/* bit 14 */

#define SISL_REQ_FLAGS_HOST_WRITE     0x0001U	/* bit 15 (LSB) */
#define SISL_REQ_FLAGS_HOST_READ      0x0000U

	union {
		u32 res_hndl;	/* res_hndl_t */
		u32 port_sel;	/* this is a selection mask:
				 * 0x1 -> port#0 can be selected,
				 * 0x2 -> port#1 can be selected.
				 * Can be bitwise ORed.
				 */
	};
	u64 lun_id;
	u32 data_len;		/* 4K for read/write */
	u32 ioadl_len;
	union {
		u64 data_ea;	/* min 16 byte aligned */
		u64 ioadl_ea;
	};
	u8 msi;			/* LISN to send on RRQ write */
#define SISL_MSI_CXL_PFAULT        0	/* reserved for CXL page faults */
#define SISL_MSI_SYNC_ERROR        1	/* recommended for AFU sync error */
#define SISL_MSI_RRQ_UPDATED       2	/* recommended for IO completion */
#define SISL_MSI_ASYNC_ERROR       3	/* master only - for AFU async error */

	u8 rrq;			/* 0 for a single RRQ */
	u16 timeout;		/* in units specified by req_flags */
	u32 rsvd1;
	u8 cdb[16];		/* must be in big endian */
#define SISL_AFU_CMD_SYNC		0xC0	/* AFU sync command */
#define SISL_AFU_CMD_LUN_PROVISION	0xD0	/* AFU LUN provision command */
#define SISL_AFU_CMD_DEBUG		0xE0	/* AFU debug command */

#define SISL_AFU_LUN_PROVISION_CREATE	0x00	/* LUN provision create type */
#define SISL_AFU_LUN_PROVISION_DELETE	0x01	/* LUN provision delete type */

	union {
		u64 reserved;			/* Reserved for IOARRIN mode */
		struct sisl_ioasa *ioasa;	/* IOASA EA for SQ Mode */
	};
} __packed;

struct sisl_rc {
	u8 flags;
#define SISL_RC_FLAGS_SENSE_VALID         0x80U
#define SISL_RC_FLAGS_FCP_RSP_CODE_VALID  0x40U
#define SISL_RC_FLAGS_OVERRUN             0x20U
#define SISL_RC_FLAGS_UNDERRUN            0x10U

	u8 afu_rc;
#define SISL_AFU_RC_RHT_INVALID           0x01U	/* user error */
#define SISL_AFU_RC_RHT_UNALIGNED         0x02U	/* should never happen */
#define SISL_AFU_RC_RHT_OUT_OF_BOUNDS     0x03u	/* user error */
#define SISL_AFU_RC_RHT_DMA_ERR           0x04u	/* see afu_extra
						 * may retry if afu_retry is off
						 * possible on master exit
						 */
#define SISL_AFU_RC_RHT_RW_PERM           0x05u	/* no RW perms, user error */
#define SISL_AFU_RC_LXT_UNALIGNED         0x12U	/* should never happen */
#define SISL_AFU_RC_LXT_OUT_OF_BOUNDS     0x13u	/* user error */
#define SISL_AFU_RC_LXT_DMA_ERR           0x14u	/* see afu_extra
						 * may retry if afu_retry is off
						 * possible on master exit
						 */
#define SISL_AFU_RC_LXT_RW_PERM           0x15u	/* no RW perms, user error */

#define SISL_AFU_RC_NOT_XLATE_HOST        0x1au	/* possible if master exited */

	/* NO_CHANNELS means the FC ports selected by dest_port in
	 * IOARCB or in the LXT entry are down when the AFU tried to select
	 * a FC port. If the port went down on an active IO, it will set
	 * fc_rc to =0x54(NOLOGI) or 0x57(LINKDOWN) instead.
	 */
#define SISL_AFU_RC_NO_CHANNELS           0x20U	/* see afu_extra, may retry */
#define SISL_AFU_RC_CAP_VIOLATION         0x21U	/* either user error or
						 * afu reset/master restart
						 */
#define SISL_AFU_RC_OUT_OF_DATA_BUFS      0x30U	/* always retry */
#define SISL_AFU_RC_DATA_DMA_ERR          0x31U	/* see afu_extra
						 * may retry if afu_retry is off
						 */

	u8 scsi_rc;		/* SCSI status byte, retry as appropriate */
#define SISL_SCSI_RC_CHECK                0x02U
#define SISL_SCSI_RC_BUSY                 0x08u

	u8 fc_rc;		/* retry */
	/*
	 * We should only see fc_rc=0x57 (LINKDOWN) or 0x54(NOLOGI) for
	 * commands that are in flight when a link goes down or is logged out.
	 * If the link is down or logged out before AFU selects the port, either
	 * it will choose the other port or we will get afu_rc=0x20 (no_channel)
	 * if there is no valid port to use.
	 *
	 * ABORTPEND/ABORTOK/ABORTFAIL/TGTABORT can be retried, typically these
	 * would happen if a frame is dropped and something times out.
	 * NOLOGI or LINKDOWN can be retried if the other port is up.
	 * RESIDERR can be retried as well.
	 *
	 * ABORTFAIL might indicate that lots of frames are getting CRC errors.
	 * So it maybe retried once and reset the link if it happens again.
	 * The link can also be reset on the CRC error threshold interrupt.
	 */
#define SISL_FC_RC_ABORTPEND	0x52	/* exchange timeout or abort request */
#define SISL_FC_RC_WRABORTPEND	0x53	/* due to write XFER_RDY invalid */
#define SISL_FC_RC_NOLOGI	0x54	/* port not logged in, in-flight cmds */
#define SISL_FC_RC_NOEXP	0x55	/* FC protocol error or HW bug */
#define SISL_FC_RC_INUSE	0x56	/* tag already in use, HW bug */
#define SISL_FC_RC_LINKDOWN	0x57	/* link down, in-flight cmds */
#define SISL_FC_RC_ABORTOK	0x58	/* pending abort completed w/success */
#define SISL_FC_RC_ABORTFAIL	0x59	/* pending abort completed w/fail */
#define SISL_FC_RC_RESID	0x5A	/* ioasa underrun/overrun flags set */
#define SISL_FC_RC_RESIDERR	0x5B	/* actual data len does not match SCSI
					 * reported len, possibly due to dropped
					 * frames
					 */
#define SISL_FC_RC_TGTABORT	0x5C	/* command aborted by target */
};

#define SISL_SENSE_DATA_LEN     20	/* Sense data length         */
#define SISL_WWID_DATA_LEN	16	/* WWID data length          */

/*
 * IOASA: 64 bytes & must follow IOARCB, min 16 byte alignment required,
 * host native endianness
 */
struct sisl_ioasa {
	union {
		struct sisl_rc rc;
		u32 ioasc;
#define SISL_IOASC_GOOD_COMPLETION        0x00000000U
	};

	union {
		u32 resid;
		u32 lunid_hi;
	};

	u8 port;
	u8 afu_extra;
	/* when afu_rc=0x04, 0x14, 0x31 (_xxx_DMA_ERR):
	 * afu_exta contains PSL response code. Useful codes are:
	 */
#define SISL_AFU_DMA_ERR_PAGE_IN	0x0A	/* AFU_retry_on_pagein Action
						 *  Enabled            N/A
						 *  Disabled           retry
						 */
#define SISL_AFU_DMA_ERR_INVALID_EA	0x0B	/* this is a hard error
						 * afu_rc	Implies
						 * 0x04, 0x14	master exit.
						 * 0x31         user error.
						 */
	/* when afu rc=0x20 (no channels):
	 * afu_extra bits [4:5]: available portmask,  [6:7]: requested portmask.
	 */
#define SISL_AFU_NO_CLANNELS_AMASK(afu_extra) (((afu_extra) & 0x0C) >> 2)
#define SISL_AFU_NO_CLANNELS_RMASK(afu_extra) ((afu_extra) & 0x03)

	u8 scsi_extra;
	u8 fc_extra;

	union {
		u8 sense_data[SISL_SENSE_DATA_LEN];
		struct {
			u32 lunid_lo;
			u8 wwid[SISL_WWID_DATA_LEN];
		};
	};

	/* These fields are defined by the SISlite architecture for the
	 * host to use as they see fit for their implementation.
	 */
	union {
		u64 host_use[4];
		u8 host_use_b[32];
	};
} __packed;

#define SISL_RESP_HANDLE_T_BIT        0x1ULL	/* Toggle bit */

/* MMIO space is required to support only 64-bit access */

/*
 * This AFU has two mechanisms to deal with endian-ness.
 * One is a global configuration (in the afu_config) register
 * below that specifies the endian-ness of the host.
 * The other is a per context (i.e. application) specification
 * controlled by the endian_ctrl field here. Since the master
 * context is one such application the master context's
 * endian-ness is set to be the same as the host.
 *
 * As per the SISlite spec, the MMIO registers are always
 * big endian.
 */
#define SISL_ENDIAN_CTRL_BE           0x8000000000000080ULL
#define SISL_ENDIAN_CTRL_LE           0x0000000000000000ULL

#ifdef __BIG_ENDIAN
#define SISL_ENDIAN_CTRL              SISL_ENDIAN_CTRL_BE
#else
#define SISL_ENDIAN_CTRL              SISL_ENDIAN_CTRL_LE
#endif

/* per context host transport MMIO  */
struct sisl_host_map {
	__be64 endian_ctrl;	/* Per context Endian Control. The AFU will
				 * operate on whatever the context is of the
				 * host application.
				 */

	__be64 intr_status;	/* this sends LISN# programmed in ctx_ctrl.
				 * Only recovery in a PERM_ERR is a context
				 * exit since there is no way to tell which
				 * command caused the error.
				 */
#define SISL_ISTATUS_PERM_ERR_LISN_3_EA		0x0400ULL /* b53, user error */
#define SISL_ISTATUS_PERM_ERR_LISN_2_EA		0x0200ULL /* b54, user error */
#define SISL_ISTATUS_PERM_ERR_LISN_1_EA		0x0100ULL /* b55, user error */
#define SISL_ISTATUS_PERM_ERR_LISN_3_PASID	0x0080ULL /* b56, user error */
#define SISL_ISTATUS_PERM_ERR_LISN_2_PASID	0x0040ULL /* b57, user error */
#define SISL_ISTATUS_PERM_ERR_LISN_1_PASID	0x0020ULL /* b58, user error */
#define SISL_ISTATUS_PERM_ERR_CMDROOM		0x0010ULL /* b59, user error */
#define SISL_ISTATUS_PERM_ERR_RCB_READ		0x0008ULL /* b60, user error */
#define SISL_ISTATUS_PERM_ERR_SA_WRITE		0x0004ULL /* b61, user error */
#define SISL_ISTATUS_PERM_ERR_RRQ_WRITE		0x0002ULL /* b62, user error */
	/* Page in wait accessing RCB/IOASA/RRQ is reported in b63.
	 * Same error in data/LXT/RHT access is reported via IOASA.
	 */
#define SISL_ISTATUS_TEMP_ERR_PAGEIN		0x0001ULL /* b63, can only be
							   * generated when AFU
							   * auto retry is
							   * disabled. If user
							   * can determine the
							   * command that caused
							   * the error, it can
							   * be retried.
							   */
#define SISL_ISTATUS_UNMASK	(0x07FFULL)		/* 1 means unmasked */
#define SISL_ISTATUS_MASK	~(SISL_ISTATUS_UNMASK)	/* 1 means masked */

	__be64 intr_clear;
	__be64 intr_mask;
	__be64 ioarrin;		/* only write what cmd_room permits */
	__be64 rrq_start;	/* start & end are both inclusive */
	__be64 rrq_end;		/* write sequence: start followed by end */
	__be64 cmd_room;
	__be64 ctx_ctrl;	/* least significant byte or b56:63 is LISN# */
#define SISL_CTX_CTRL_UNMAP_SECTOR	0x8000000000000000ULL /* b0 */
#define SISL_CTX_CTRL_LISN_MASK		(0xFFULL)
	__be64 mbox_w;		/* restricted use */
	__be64 sq_start;	/* Submission Queue (R/W): write sequence and */
	__be64 sq_end;		/* inclusion semantics are the same as RRQ    */
	__be64 sq_head;		/* Submission Queue Head (R): for debugging   */
	__be64 sq_tail;		/* Submission Queue TAIL (R/W): next IOARCB   */
	__be64 sq_ctx_reset;	/* Submission Queue Context Reset (R/W)	      */
};

/* per context provisioning & control MMIO */
struct sisl_ctrl_map {
	__be64 rht_start;
	__be64 rht_cnt_id;
	/* both cnt & ctx_id args must be ULL */
#define SISL_RHT_CNT_ID(cnt, ctx_id)  (((cnt) << 48) | ((ctx_id) << 32))

	__be64 ctx_cap;	/* afu_rc below is when the capability is violated */
#define SISL_CTX_CAP_PROXY_ISSUE       0x8000000000000000ULL /* afu_rc 0x21 */
#define SISL_CTX_CAP_REAL_MODE         0x4000000000000000ULL /* afu_rc 0x21 */
#define SISL_CTX_CAP_HOST_XLATE        0x2000000000000000ULL /* afu_rc 0x1a */
#define SISL_CTX_CAP_PROXY_TARGET      0x1000000000000000ULL /* afu_rc 0x21 */
#define SISL_CTX_CAP_AFU_CMD           0x0000000000000008ULL /* afu_rc 0x21 */
#define SISL_CTX_CAP_GSCSI_CMD         0x0000000000000004ULL /* afu_rc 0x21 */
#define SISL_CTX_CAP_WRITE_CMD         0x0000000000000002ULL /* afu_rc 0x21 */
#define SISL_CTX_CAP_READ_CMD          0x0000000000000001ULL /* afu_rc 0x21 */
	__be64 mbox_r;
	__be64 lisn_pasid[2];
	/* pasid _a arg must be ULL */
#define SISL_LISN_PASID(_a, _b)	(((_a) << 32) | (_b))
	__be64 lisn_ea[3];
};

/* single copy global regs */
struct sisl_global_regs {
	__be64 aintr_status;
	/*
	 * In cxlflash, FC port/link are arranged in port pairs, each
	 * gets a byte of status:
	 *
	 *	*_OTHER:	other err, FC_ERRCAP[31:20]
	 *	*_LOGO:		target sent FLOGI/PLOGI/LOGO while logged in
	 *	*_CRC_T:	CRC threshold exceeded
	 *	*_LOGI_R:	login state machine timed out and retrying
	 *	*_LOGI_F:	login failed, FC_ERROR[19:0]
	 *	*_LOGI_S:	login succeeded
	 *	*_LINK_DN:	link online to offline
	 *	*_LINK_UP:	link offline to online
	 */
#define SISL_ASTATUS_FC2_OTHER	 0x80000000ULL /* b32 */
#define SISL_ASTATUS_FC2_LOGO    0x40000000ULL /* b33 */
#define SISL_ASTATUS_FC2_CRC_T   0x20000000ULL /* b34 */
#define SISL_ASTATUS_FC2_LOGI_R  0x10000000ULL /* b35 */
#define SISL_ASTATUS_FC2_LOGI_F  0x08000000ULL /* b36 */
#define SISL_ASTATUS_FC2_LOGI_S  0x04000000ULL /* b37 */
#define SISL_ASTATUS_FC2_LINK_DN 0x02000000ULL /* b38 */
#define SISL_ASTATUS_FC2_LINK_UP 0x01000000ULL /* b39 */

#define SISL_ASTATUS_FC3_OTHER   0x00800000ULL /* b40 */
#define SISL_ASTATUS_FC3_LOGO    0x00400000ULL /* b41 */
#define SISL_ASTATUS_FC3_CRC_T   0x00200000ULL /* b42 */
#define SISL_ASTATUS_FC3_LOGI_R  0x00100000ULL /* b43 */
#define SISL_ASTATUS_FC3_LOGI_F  0x00080000ULL /* b44 */
#define SISL_ASTATUS_FC3_LOGI_S  0x00040000ULL /* b45 */
#define SISL_ASTATUS_FC3_LINK_DN 0x00020000ULL /* b46 */
#define SISL_ASTATUS_FC3_LINK_UP 0x00010000ULL /* b47 */

#define SISL_ASTATUS_FC0_OTHER	 0x00008000ULL /* b48 */
#define SISL_ASTATUS_FC0_LOGO    0x00004000ULL /* b49 */
#define SISL_ASTATUS_FC0_CRC_T   0x00002000ULL /* b50 */
#define SISL_ASTATUS_FC0_LOGI_R  0x00001000ULL /* b51 */
#define SISL_ASTATUS_FC0_LOGI_F  0x00000800ULL /* b52 */
#define SISL_ASTATUS_FC0_LOGI_S  0x00000400ULL /* b53 */
#define SISL_ASTATUS_FC0_LINK_DN 0x00000200ULL /* b54 */
#define SISL_ASTATUS_FC0_LINK_UP 0x00000100ULL /* b55 */

#define SISL_ASTATUS_FC1_OTHER   0x00000080ULL /* b56 */
#define SISL_ASTATUS_FC1_LOGO    0x00000040ULL /* b57 */
#define SISL_ASTATUS_FC1_CRC_T   0x00000020ULL /* b58 */
#define SISL_ASTATUS_FC1_LOGI_R  0x00000010ULL /* b59 */
#define SISL_ASTATUS_FC1_LOGI_F  0x00000008ULL /* b60 */
#define SISL_ASTATUS_FC1_LOGI_S  0x00000004ULL /* b61 */
#define SISL_ASTATUS_FC1_LINK_DN 0x00000002ULL /* b62 */
#define SISL_ASTATUS_FC1_LINK_UP 0x00000001ULL /* b63 */

#define SISL_FC_INTERNAL_UNMASK	0x0000000300000000ULL	/* 1 means unmasked */
#define SISL_FC_INTERNAL_MASK	~(SISL_FC_INTERNAL_UNMASK)
#define SISL_FC_INTERNAL_SHIFT	32

#define SISL_FC_SHUTDOWN_NORMAL		0x0000000000000010ULL
#define SISL_FC_SHUTDOWN_ABRUPT		0x0000000000000020ULL

#define SISL_STATUS_SHUTDOWN_ACTIVE	0x0000000000000010ULL
#define SISL_STATUS_SHUTDOWN_COMPLETE	0x0000000000000020ULL

#define SISL_ASTATUS_UNMASK	0xFFFFFFFFULL		/* 1 means unmasked */
#define SISL_ASTATUS_MASK	~(SISL_ASTATUS_UNMASK)	/* 1 means masked */

	__be64 aintr_clear;
	__be64 aintr_mask;
	__be64 afu_ctrl;
	__be64 afu_hb;
	__be64 afu_scratch_pad;
	__be64 afu_port_sel;
#define SISL_AFUCONF_AR_IOARCB	0x4000ULL
#define SISL_AFUCONF_AR_LXT	0x2000ULL
#define SISL_AFUCONF_AR_RHT	0x1000ULL
#define SISL_AFUCONF_AR_DATA	0x0800ULL
#define SISL_AFUCONF_AR_RSRC	0x0400ULL
#define SISL_AFUCONF_AR_IOASA	0x0200ULL
#define SISL_AFUCONF_AR_RRQ	0x0100ULL
/* Aggregate all Auto Retry Bits */
#define SISL_AFUCONF_AR_ALL	(SISL_AFUCONF_AR_IOARCB|SISL_AFUCONF_AR_LXT| \
				 SISL_AFUCONF_AR_RHT|SISL_AFUCONF_AR_DATA|   \
				 SISL_AFUCONF_AR_RSRC|SISL_AFUCONF_AR_IOASA| \
				 SISL_AFUCONF_AR_RRQ)
#ifdef __BIG_ENDIAN
#define SISL_AFUCONF_ENDIAN            0x0000ULL
#else
#define SISL_AFUCONF_ENDIAN            0x0020ULL
#endif
#define SISL_AFUCONF_MBOX_CLR_READ     0x0010ULL
	__be64 afu_config;
	__be64 rsvd[0xf8];
	__le64 afu_version;
	__be64 interface_version;
#define SISL_INTVER_CAP_SHIFT			16
#define SISL_INTVER_MAJ_SHIFT			8
#define SISL_INTVER_CAP_MASK			0xFFFFFFFF00000000ULL
#define SISL_INTVER_MAJ_MASK			0x00000000FFFF0000ULL
#define SISL_INTVER_MIN_MASK			0x000000000000FFFFULL
#define SISL_INTVER_CAP_IOARRIN_CMD_MODE	0x800000000000ULL
#define SISL_INTVER_CAP_SQ_CMD_MODE		0x400000000000ULL
#define SISL_INTVER_CAP_RESERVED_CMD_MODE_A	0x200000000000ULL
#define SISL_INTVER_CAP_RESERVED_CMD_MODE_B	0x100000000000ULL
#define SISL_INTVER_CAP_LUN_PROVISION		0x080000000000ULL
#define SISL_INTVER_CAP_AFU_DEBUG		0x040000000000ULL
#define SISL_INTVER_CAP_OCXL_LISN		0x020000000000ULL
};

#define CXLFLASH_NUM_FC_PORTS_PER_BANK	2	/* fixed # of ports per bank */
#define CXLFLASH_MAX_FC_BANKS		2	/* max # of banks supported */
#define CXLFLASH_MAX_FC_PORTS	(CXLFLASH_NUM_FC_PORTS_PER_BANK *	\
				 CXLFLASH_MAX_FC_BANKS)
#define CXLFLASH_MAX_CONTEXT	512	/* number of contexts per AFU */
#define CXLFLASH_NUM_VLUNS	512	/* number of vluns per AFU/port */
#define CXLFLASH_NUM_REGS	512	/* number of registers per port */

struct fc_port_bank {
	__be64 fc_port_regs[CXLFLASH_NUM_FC_PORTS_PER_BANK][CXLFLASH_NUM_REGS];
	__be64 fc_port_luns[CXLFLASH_NUM_FC_PORTS_PER_BANK][CXLFLASH_NUM_VLUNS];
};

struct sisl_global_map {
	union {
		struct sisl_global_regs regs;
		char page0[SIZE_4K];	/* page 0 */
	};

	char page1[SIZE_4K];	/* page 1 */

	struct fc_port_bank bank[CXLFLASH_MAX_FC_BANKS]; /* pages 2 - 9 */

	/* pages 10 - 15 are reserved */

};

/*
 * CXL Flash Memory Map
 *
 *	+-------------------------------+
 *	|    512 * 64 KB User MMIO      |
 *	|        (per context)          |
 *	|       User Accessible         |
 *	+-------------------------------+
 *	|    512 * 128 B per context    |
 *	|    Provisioning and Control   |
 *	|   Trusted Process accessible  |
 *	+-------------------------------+
 *	|         64 KB Global          |
 *	|   Trusted Process accessible  |
 *	+-------------------------------+
 */
struct cxlflash_afu_map {
	union {
		struct sisl_host_map host;
		char harea[SIZE_64K];	/* 64KB each */
	} hosts[CXLFLASH_MAX_CONTEXT];

	union {
		struct sisl_ctrl_map ctrl;
		char carea[cache_line_size()];	/* 128B each */
	} ctrls[CXLFLASH_MAX_CONTEXT];

	union {
		struct sisl_global_map global;
		char garea[SIZE_64K];	/* 64KB single block */
	};
};

/*
 * LXT - LBA Translation Table
 * LXT control blocks
 */
struct sisl_lxt_entry {
	u64 rlba_base;	/* bits 0:47 is base
			 * b48:55 is lun index
			 * b58:59 is write & read perms
			 * (if no perm, afu_rc=0x15)
			 * b60:63 is port_sel mask
			 */
};

/*
 * RHT - Resource Handle Table
 * Per the SISlite spec, RHT entries are to be 16-byte aligned
 */
struct sisl_rht_entry {
	struct sisl_lxt_entry *lxt_start;
	u32 lxt_cnt;
	u16 rsvd;
	u8 fp;			/* format & perm nibbles.
				 * (if no perm, afu_rc=0x05)
				 */
	u8 nmask;
} __packed __aligned(16);

struct sisl_rht_entry_f1 {
	u64 lun_id;
	union {
		struct {
			u8 valid;
			u8 rsvd[5];
			u8 fp;
			u8 port_sel;
		};

		u64 dw;
	};
} __packed __aligned(16);

/* make the fp byte */
#define SISL_RHT_FP(fmt, perm) (((fmt) << 4) | (perm))

/* make the fp byte for a clone from a source fp and clone flags
 * flags must be only 2 LSB bits.
 */
#define SISL_RHT_FP_CLONE(src_fp, cln_flags) ((src_fp) & (0xFC | (cln_flags)))

#define RHT_PERM_READ  0x01U
#define RHT_PERM_WRITE 0x02U
#define RHT_PERM_RW    (RHT_PERM_READ | RHT_PERM_WRITE)

/* extract the perm bits from a fp */
#define SISL_RHT_PERM(fp) ((fp) & RHT_PERM_RW)

#define PORT0  0x01U
#define PORT1  0x02U
#define PORT2  0x04U
#define PORT3  0x08U
#define PORT_MASK(_n)	((1 << (_n)) - 1)

/* AFU Sync Mode byte */
#define AFU_LW_SYNC 0x0U
#define AFU_HW_SYNC 0x1U
#define AFU_GSYNC   0x2U

/* Special Task Management Function CDB */
#define TMF_LUN_RESET  0x1U
#define TMF_CLEAR_ACA  0x2U

#endif /* _SISLITE_H */
