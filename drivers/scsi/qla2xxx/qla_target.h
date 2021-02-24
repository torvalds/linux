/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2004 - 2010 Vladislav Bolkhovitin <vst@vlnb.net>
 *  Copyright (C) 2004 - 2005 Leonid Stoljar
 *  Copyright (C) 2006 Nathaniel Clark <nate@misrule.us>
 *  Copyright (C) 2007 - 2010 ID7 Ltd.
 *
 *  Forward port and refactoring to modern qla2xxx and target/configfs
 *
 *  Copyright (C) 2010-2011 Nicholas A. Bellinger <nab@kernel.org>
 *
 *  Additional file for the target driver support.
 */
/*
 * This is the global def file that is useful for including from the
 * target portion.
 */

#ifndef __QLA_TARGET_H
#define __QLA_TARGET_H

#include "qla_def.h"
#include "qla_dsd.h"

/*
 * Must be changed on any change in any initiator visible interfaces or
 * data in the target add-on
 */
#define QLA2XXX_TARGET_MAGIC	269

/*
 * Must be changed on any change in any target visible interfaces or
 * data in the initiator
 */
#define QLA2XXX_INITIATOR_MAGIC   57222

#define QLA2XXX_INI_MODE_STR_EXCLUSIVE	"exclusive"
#define QLA2XXX_INI_MODE_STR_DISABLED	"disabled"
#define QLA2XXX_INI_MODE_STR_ENABLED	"enabled"
#define QLA2XXX_INI_MODE_STR_DUAL		"dual"

#define QLA2XXX_INI_MODE_EXCLUSIVE	0
#define QLA2XXX_INI_MODE_DISABLED	1
#define QLA2XXX_INI_MODE_ENABLED	2
#define QLA2XXX_INI_MODE_DUAL	3

#define QLA2XXX_COMMAND_COUNT_INIT	250
#define QLA2XXX_IMMED_NOTIFY_COUNT_INIT 250

/*
 * Used to mark which completion handles (for RIO Status's) are for CTIO's
 * vs. regular (non-target) info. This is checked for in
 * qla2x00_process_response_queue() to see if a handle coming back in a
 * multi-complete should come to the tgt driver or be handled there by qla2xxx
 */
#define CTIO_COMPLETION_HANDLE_MARK	BIT_29
#if (CTIO_COMPLETION_HANDLE_MARK <= DEFAULT_OUTSTANDING_COMMANDS)
#error "CTIO_COMPLETION_HANDLE_MARK not larger than "
	"DEFAULT_OUTSTANDING_COMMANDS"
#endif
#define HANDLE_IS_CTIO_COMP(h) (h & CTIO_COMPLETION_HANDLE_MARK)

/* Used to mark CTIO as intermediate */
#define CTIO_INTERMEDIATE_HANDLE_MARK	BIT_30
#define QLA_TGT_NULL_HANDLE	0

#define QLA_TGT_HANDLE_MASK  0xF0000000
#define QLA_QPID_HANDLE_MASK 0x00FF0000 /* qpair id mask */
#define QLA_CMD_HANDLE_MASK  0x0000FFFF
#define QLA_TGT_SKIP_HANDLE	(0xFFFFFFFF & ~QLA_TGT_HANDLE_MASK)

#define QLA_QPID_HANDLE_SHIFT 16
#define GET_QID(_h) ((_h & QLA_QPID_HANDLE_MASK) >> QLA_QPID_HANDLE_SHIFT)


#ifndef OF_SS_MODE_0
/*
 * ISP target entries - Flags bit definitions.
 */
#define OF_SS_MODE_0        0
#define OF_SS_MODE_1        1
#define OF_SS_MODE_2        2
#define OF_SS_MODE_3        3

#define OF_EXPL_CONF        BIT_5       /* Explicit Confirmation Requested */
#define OF_DATA_IN          BIT_6       /* Data in to initiator */
					/*  (data from target to initiator) */
#define OF_DATA_OUT         BIT_7       /* Data out from initiator */
					/*  (data from initiator to target) */
#define OF_NO_DATA          (BIT_7 | BIT_6)
#define OF_INC_RC           BIT_8       /* Increment command resource count */
#define OF_FAST_POST        BIT_9       /* Enable mailbox fast posting. */
#define OF_CONF_REQ         BIT_13      /* Confirmation Requested */
#define OF_TERM_EXCH        BIT_14      /* Terminate exchange */
#define OF_SSTS             BIT_15      /* Send SCSI status */
#endif

#ifndef QLA_TGT_DATASEGS_PER_CMD32
#define QLA_TGT_DATASEGS_PER_CMD32	3
#define QLA_TGT_DATASEGS_PER_CONT32	7
#define QLA_TGT_MAX_SG32(ql) \
	(((ql) > 0) ? (QLA_TGT_DATASEGS_PER_CMD32 + \
		QLA_TGT_DATASEGS_PER_CONT32*((ql) - 1)) : 0)

#define QLA_TGT_DATASEGS_PER_CMD64	2
#define QLA_TGT_DATASEGS_PER_CONT64	5
#define QLA_TGT_MAX_SG64(ql) \
	(((ql) > 0) ? (QLA_TGT_DATASEGS_PER_CMD64 + \
		QLA_TGT_DATASEGS_PER_CONT64*((ql) - 1)) : 0)
#endif

#ifndef QLA_TGT_DATASEGS_PER_CMD_24XX
#define QLA_TGT_DATASEGS_PER_CMD_24XX	1
#define QLA_TGT_DATASEGS_PER_CONT_24XX	5
#define QLA_TGT_MAX_SG_24XX(ql) \
	(min(1270, ((ql) > 0) ? (QLA_TGT_DATASEGS_PER_CMD_24XX + \
		QLA_TGT_DATASEGS_PER_CONT_24XX*((ql) - 1)) : 0))
#endif
#endif

#define GET_TARGET_ID(ha, iocb) ((HAS_EXTENDED_IDS(ha))			\
			 ? le16_to_cpu((iocb)->u.isp2x.target.extended)	\
			 : (uint16_t)(iocb)->u.isp2x.target.id.standard)

#ifndef NOTIFY_ACK_TYPE
#define NOTIFY_ACK_TYPE 0x0E	  /* Notify acknowledge entry. */
/*
 * ISP queue -	notify acknowledge entry structure definition.
 *		This is sent to the ISP from the target driver.
 */
struct nack_to_isp {
	uint8_t	 entry_type;		    /* Entry type. */
	uint8_t	 entry_count;		    /* Entry count. */
	uint8_t	 sys_define;		    /* System defined. */
	uint8_t	 entry_status;		    /* Entry Status. */
	union {
		struct {
			__le32	sys_define_2; /* System defined. */
			target_id_t target;
			uint8_t	 target_id;
			uint8_t	 reserved_1;
			__le16	flags;
			__le16	resp_code;
			__le16	status;
			__le16	task_flags;
			__le16	seq_id;
			__le16	srr_rx_id;
			__le32	srr_rel_offs;
			__le16	srr_ui;
			__le16	srr_flags;
			__le16	srr_reject_code;
			uint8_t  srr_reject_vendor_uniq;
			uint8_t  srr_reject_code_expl;
			uint8_t  reserved_2[24];
		} isp2x;
		struct {
			uint32_t handle;
			__le16	nport_handle;
			uint16_t reserved_1;
			__le16	flags;
			__le16	srr_rx_id;
			__le16	status;
			uint8_t  status_subcode;
			uint8_t  fw_handle;
			__le32	exchange_address;
			__le32	srr_rel_offs;
			__le16	srr_ui;
			__le16	srr_flags;
			uint8_t  reserved_4[19];
			uint8_t  vp_index;
			uint8_t  srr_reject_vendor_uniq;
			uint8_t  srr_reject_code_expl;
			uint8_t  srr_reject_code;
			uint8_t  reserved_5[5];
		} isp24;
	} u;
	uint8_t  reserved[2];
	__le16	ox_id;
} __packed;
#define NOTIFY_ACK_FLAGS_TERMINATE	BIT_3
#define NOTIFY_ACK_SRR_FLAGS_ACCEPT	0
#define NOTIFY_ACK_SRR_FLAGS_REJECT	1

#define NOTIFY_ACK_SRR_REJECT_REASON_UNABLE_TO_PERFORM	0x9

#define NOTIFY_ACK_SRR_FLAGS_REJECT_EXPL_NO_EXPL		0
#define NOTIFY_ACK_SRR_FLAGS_REJECT_EXPL_UNABLE_TO_SUPPLY_DATA	0x2a

#define NOTIFY_ACK_SUCCESS      0x01
#endif

#ifndef ACCEPT_TGT_IO_TYPE
#define ACCEPT_TGT_IO_TYPE 0x16 /* Accept target I/O entry. */
#endif

#ifndef CONTINUE_TGT_IO_TYPE
#define CONTINUE_TGT_IO_TYPE 0x17
/*
 * ISP queue -	Continue Target I/O (CTIO) entry for status mode 0 structure.
 *		This structure is sent to the ISP 2xxx from target driver.
 */
struct ctio_to_2xxx {
	uint8_t	 entry_type;		/* Entry type. */
	uint8_t	 entry_count;		/* Entry count. */
	uint8_t	 sys_define;		/* System defined. */
	uint8_t	 entry_status;		/* Entry Status. */
	uint32_t handle;		/* System defined handle */
	target_id_t target;
	__le16	rx_id;
	__le16	flags;
	__le16	status;
	__le16	timeout;		/* 0 = 30 seconds, 0xFFFF = disable */
	__le16	dseg_count;		/* Data segment count. */
	__le32	relative_offset;
	__le32	residual;
	__le16	reserved_1[3];
	__le16	scsi_status;
	__le32	transfer_length;
	struct dsd32 dsd[3];
} __packed;
#define ATIO_PATH_INVALID       0x07
#define ATIO_CANT_PROV_CAP      0x16
#define ATIO_CDB_VALID          0x3D

#define ATIO_EXEC_READ          BIT_1
#define ATIO_EXEC_WRITE         BIT_0
#endif

#ifndef CTIO_A64_TYPE
#define CTIO_A64_TYPE 0x1F
#define CTIO_SUCCESS			0x01
#define CTIO_ABORTED			0x02
#define CTIO_INVALID_RX_ID		0x08
#define CTIO_TIMEOUT			0x0B
#define CTIO_DIF_ERROR			0x0C     /* DIF error detected  */
#define CTIO_LIP_RESET			0x0E
#define CTIO_TARGET_RESET		0x17
#define CTIO_PORT_UNAVAILABLE		0x28
#define CTIO_PORT_LOGGED_OUT		0x29
#define CTIO_PORT_CONF_CHANGED		0x2A
#define CTIO_SRR_RECEIVED		0x45
#endif

#ifndef CTIO_RET_TYPE
#define CTIO_RET_TYPE	0x17		/* CTIO return entry */
#define ATIO_TYPE7 0x06 /* Accept target I/O entry for 24xx */

struct fcp_hdr {
	uint8_t  r_ctl;
	be_id_t  d_id;
	uint8_t  cs_ctl;
	be_id_t  s_id;
	uint8_t  type;
	uint8_t  f_ctl[3];
	uint8_t  seq_id;
	uint8_t  df_ctl;
	uint16_t seq_cnt;
	__be16   ox_id;
	uint16_t rx_id;
	__le32	parameter;
};

struct fcp_hdr_le {
	le_id_t  d_id;
	uint8_t  r_ctl;
	le_id_t  s_id;
	uint8_t  cs_ctl;
	uint8_t  f_ctl[3];
	uint8_t  type;
	__le16	seq_cnt;
	uint8_t  df_ctl;
	uint8_t  seq_id;
	__le16	rx_id;
	__le16	ox_id;
	__le32	parameter;
};

#define F_CTL_EXCH_CONTEXT_RESP	BIT_23
#define F_CTL_SEQ_CONTEXT_RESIP	BIT_22
#define F_CTL_LAST_SEQ		BIT_20
#define F_CTL_END_SEQ		BIT_19
#define F_CTL_SEQ_INITIATIVE	BIT_16

#define R_CTL_BASIC_LINK_SERV	0x80
#define R_CTL_B_ACC		0x4
#define R_CTL_B_RJT		0x5

struct atio7_fcp_cmnd {
	uint64_t lun;
	uint8_t  cmnd_ref;
	uint8_t  task_attr:3;
	uint8_t  reserved:5;
	uint8_t  task_mgmt_flags;
#define FCP_CMND_TASK_MGMT_CLEAR_ACA		6
#define FCP_CMND_TASK_MGMT_TARGET_RESET		5
#define FCP_CMND_TASK_MGMT_LU_RESET		4
#define FCP_CMND_TASK_MGMT_CLEAR_TASK_SET	2
#define FCP_CMND_TASK_MGMT_ABORT_TASK_SET	1
	uint8_t  wrdata:1;
	uint8_t  rddata:1;
	uint8_t  add_cdb_len:6;
	uint8_t  cdb[16];
	/*
	 * add_cdb is optional and can absent from struct atio7_fcp_cmnd. Size 4
	 * only to make sizeof(struct atio7_fcp_cmnd) be as expected by
	 * BUILD_BUG_ON in qlt_init().
	 */
	uint8_t  add_cdb[4];
	/* __le32	data_length; */
} __packed;

/*
 * ISP queue -	Accept Target I/O (ATIO) type entry IOCB structure.
 *		This is sent from the ISP to the target driver.
 */
struct atio_from_isp {
	union {
		struct {
			__le16	entry_hdr;
			uint8_t  sys_define;   /* System defined. */
			uint8_t  entry_status; /* Entry Status.   */
			__le32	sys_define_2; /* System defined. */
			target_id_t target;
			__le16	rx_id;
			__le16	flags;
			__le16	status;
			uint8_t  command_ref;
			uint8_t  task_codes;
			uint8_t  task_flags;
			uint8_t  execution_codes;
			uint8_t  cdb[MAX_CMDSZ];
			__le32	data_length;
			__le16	lun;
			uint8_t  initiator_port_name[WWN_SIZE]; /* on qla23xx */
			__le16	reserved_32[6];
			__le16	ox_id;
		} isp2x;
		struct {
			__le16	entry_hdr;
			uint8_t  fcp_cmnd_len_low;
			uint8_t  fcp_cmnd_len_high:4;
			uint8_t  attr:4;
			__le32	exchange_addr;
#define ATIO_EXCHANGE_ADDRESS_UNKNOWN	0xFFFFFFFF
			struct fcp_hdr fcp_hdr;
			struct atio7_fcp_cmnd fcp_cmnd;
		} isp24;
		struct {
			uint8_t  entry_type;	/* Entry type. */
			uint8_t  entry_count;	/* Entry count. */
			__le16	 attr_n_length;
#define FCP_CMD_LENGTH_MASK 0x0fff
#define FCP_CMD_LENGTH_MIN  0x38
			uint8_t  data[56];
			__le32	signature;
#define ATIO_PROCESSED 0xDEADDEAD		/* Signature */
		} raw;
	} u;
} __packed;

static inline int fcpcmd_is_corrupted(struct atio *atio)
{
	if (atio->entry_type == ATIO_TYPE7 &&
	    ((le16_to_cpu(atio->attr_n_length) & FCP_CMD_LENGTH_MASK) <
	     FCP_CMD_LENGTH_MIN))
		return 1;
	else
		return 0;
}

/* adjust corrupted atio so we won't trip over the same entry again. */
static inline void adjust_corrupted_atio(struct atio_from_isp *atio)
{
	atio->u.raw.attr_n_length = cpu_to_le16(FCP_CMD_LENGTH_MIN);
	atio->u.isp24.fcp_cmnd.add_cdb_len = 0;
}

static inline int get_datalen_for_atio(struct atio_from_isp *atio)
{
	int len = atio->u.isp24.fcp_cmnd.add_cdb_len;

	return get_unaligned_be32(&atio->u.isp24.fcp_cmnd.add_cdb[len * 4]);
}

#define CTIO_TYPE7 0x12 /* Continue target I/O entry (for 24xx) */

/*
 * ISP queue -	Continue Target I/O (ATIO) type 7 entry (for 24xx) structure.
 *		This structure is sent to the ISP 24xx from the target driver.
 */

struct ctio7_to_24xx {
	uint8_t	 entry_type;		    /* Entry type. */
	uint8_t	 entry_count;		    /* Entry count. */
	uint8_t	 sys_define;		    /* System defined. */
	uint8_t	 entry_status;		    /* Entry Status. */
	uint32_t handle;		    /* System defined handle */
	__le16	nport_handle;
#define CTIO7_NHANDLE_UNRECOGNIZED	0xFFFF
	__le16	timeout;
	__le16	dseg_count;		    /* Data segment count. */
	uint8_t  vp_index;
	uint8_t  add_flags;
	le_id_t  initiator_id;
	uint8_t  reserved;
	__le32	exchange_addr;
	union {
		struct {
			__le16	reserved1;
			__le16 flags;
			__le32	residual;
			__le16 ox_id;
			__le16	scsi_status;
			__le32	relative_offset;
			__le32	reserved2;
			__le32	transfer_length;
			__le32	reserved3;
			struct dsd64 dsd;
		} status0;
		struct {
			__le16	sense_length;
			__le16 flags;
			__le32	residual;
			__le16 ox_id;
			__le16	scsi_status;
			__le16	response_len;
			__le16	reserved;
			uint8_t sense_data[24];
		} status1;
	} u;
} __packed;

/*
 * ISP queue - CTIO type 7 from ISP 24xx to target driver
 * returned entry structure.
 */
struct ctio7_from_24xx {
	uint8_t	 entry_type;		    /* Entry type. */
	uint8_t	 entry_count;		    /* Entry count. */
	uint8_t	 sys_define;		    /* System defined. */
	uint8_t	 entry_status;		    /* Entry Status. */
	uint32_t handle;		    /* System defined handle */
	__le16	status;
	__le16	timeout;
	__le16	dseg_count;		    /* Data segment count. */
	uint8_t  vp_index;
	uint8_t  reserved1[5];
	__le32	exchange_address;
	__le16	reserved2;
	__le16	flags;
	__le32	residual;
	__le16	ox_id;
	__le16	reserved3;
	__le32	relative_offset;
	uint8_t  reserved4[24];
} __packed;

/* CTIO7 flags values */
#define CTIO7_FLAGS_SEND_STATUS		BIT_15
#define CTIO7_FLAGS_TERMINATE		BIT_14
#define CTIO7_FLAGS_CONFORM_REQ		BIT_13
#define CTIO7_FLAGS_DONT_RET_CTIO	BIT_8
#define CTIO7_FLAGS_STATUS_MODE_0	0
#define CTIO7_FLAGS_STATUS_MODE_1	BIT_6
#define CTIO7_FLAGS_STATUS_MODE_2	BIT_7
#define CTIO7_FLAGS_EXPLICIT_CONFORM	BIT_5
#define CTIO7_FLAGS_CONFIRM_SATISF	BIT_4
#define CTIO7_FLAGS_DSD_PTR		BIT_2
#define CTIO7_FLAGS_DATA_IN		BIT_1 /* data to initiator */
#define CTIO7_FLAGS_DATA_OUT		BIT_0 /* data from initiator */

#define ELS_PLOGI			0x3
#define ELS_FLOGI			0x4
#define ELS_LOGO			0x5
#define ELS_PRLI			0x20
#define ELS_PRLO			0x21
#define ELS_TPRLO			0x24
#define ELS_PDISC			0x50
#define ELS_ADISC			0x52

/*
 *CTIO Type CRC_2 IOCB
 */
struct ctio_crc2_to_fw {
	uint8_t entry_type;		/* Entry type. */
#define CTIO_CRC2 0x7A
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */
	__le16	nport_handle;		/* N_PORT handle. */
	__le16 timeout;		/* Command timeout. */

	__le16	dseg_count;		/* Data segment count. */
	uint8_t  vp_index;
	uint8_t  add_flags;		/* additional flags */
#define CTIO_CRC2_AF_DIF_DSD_ENA BIT_3

	le_id_t  initiator_id;		/* initiator ID */
	uint8_t  reserved1;
	__le32	exchange_addr;		/* rcv exchange address */
	__le16	reserved2;
	__le16 flags;			/* refer to CTIO7 flags values */
	__le32	residual;
	__le16 ox_id;
	__le16	scsi_status;
	__le32 relative_offset;
	__le32	reserved5;
	__le32 transfer_length;		/* total fc transfer length */
	__le32	reserved6;
	__le64	 crc_context_address __packed; /* Data segment address. */
	__le16	crc_context_len;	/* Data segment length. */
	__le16	reserved_1;		/* MUST be set to 0. */
};

/* CTIO Type CRC_x Status IOCB */
struct ctio_crc_from_fw {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */
	__le16	status;
	__le16	timeout;		/* Command timeout. */
	__le16	dseg_count;		/* Data segment count. */
	__le32	reserved1;
	__le16	state_flags;
#define CTIO_CRC_SF_DIF_CHOPPED BIT_4

	__le32	exchange_address;	/* rcv exchange address */
	__le16	reserved2;
	__le16	flags;
	__le32	resid_xfer_length;
	__le16	ox_id;
	uint8_t  reserved3[12];
	__le16	runt_guard;		/* reported runt blk guard */
	uint8_t  actual_dif[8];
	uint8_t  expected_dif[8];
} __packed;

/*
 * ISP queue - ABTS received/response entries structure definition for 24xx.
 */
#define ABTS_RECV_24XX		0x54 /* ABTS received (for 24xx) */
#define ABTS_RESP_24XX		0x55 /* ABTS responce (for 24xx) */

/*
 * ISP queue -	ABTS received IOCB entry structure definition for 24xx.
 *		The ABTS BLS received from the wire is sent to the
 *		target driver by the ISP 24xx.
 *		The IOCB is placed on the response queue.
 */
struct abts_recv_from_24xx {
	uint8_t	 entry_type;		    /* Entry type. */
	uint8_t	 entry_count;		    /* Entry count. */
	uint8_t	 sys_define;		    /* System defined. */
	uint8_t	 entry_status;		    /* Entry Status. */
	uint8_t  reserved_1[6];
	__le16	nport_handle;
	uint8_t  reserved_2[2];
	uint8_t  vp_index;
	uint8_t  reserved_3:4;
	uint8_t  sof_type:4;
	__le32	exchange_address;
	struct fcp_hdr_le fcp_hdr_le;
	uint8_t  reserved_4[16];
	__le32	exchange_addr_to_abort;
} __packed;

#define ABTS_PARAM_ABORT_SEQ		BIT_0

struct ba_acc_le {
	__le16	reserved;
	uint8_t  seq_id_last;
	uint8_t  seq_id_valid;
#define SEQ_ID_VALID	0x80
#define SEQ_ID_INVALID	0x00
	__le16	rx_id;
	__le16	ox_id;
	__le16	high_seq_cnt;
	__le16	low_seq_cnt;
} __packed;

struct ba_rjt_le {
	uint8_t vendor_uniq;
	uint8_t reason_expl;
	uint8_t reason_code;
#define BA_RJT_REASON_CODE_INVALID_COMMAND	0x1
#define BA_RJT_REASON_CODE_UNABLE_TO_PERFORM	0x9
	uint8_t reserved;
} __packed;

/*
 * ISP queue -	ABTS Response IOCB entry structure definition for 24xx.
 *		The ABTS response to the ABTS received is sent by the
 *		target driver to the ISP 24xx.
 *		The IOCB is placed on the request queue.
 */
struct abts_resp_to_24xx {
	uint8_t	 entry_type;		    /* Entry type. */
	uint8_t	 entry_count;		    /* Entry count. */
	uint8_t	 sys_define;		    /* System defined. */
	uint8_t	 entry_status;		    /* Entry Status. */
	uint32_t handle;
	__le16	reserved_1;
	__le16	nport_handle;
	__le16	control_flags;
#define ABTS_CONTR_FLG_TERM_EXCHG	BIT_0
	uint8_t  vp_index;
	uint8_t  reserved_3:4;
	uint8_t  sof_type:4;
	__le32	exchange_address;
	struct fcp_hdr_le fcp_hdr_le;
	union {
		struct ba_acc_le ba_acct;
		struct ba_rjt_le ba_rjt;
	} __packed payload;
	__le32	reserved_4;
	__le32	exchange_addr_to_abort;
} __packed;

/*
 * ISP queue -	ABTS Response IOCB from ISP24xx Firmware entry structure.
 *		The ABTS response with completion status to the ABTS response
 *		(sent by the target driver to the ISP 24xx) is sent by the
 *		ISP24xx firmware to the target driver.
 *		The IOCB is placed on the response queue.
 */
struct abts_resp_from_24xx_fw {
	uint8_t	 entry_type;		    /* Entry type. */
	uint8_t	 entry_count;		    /* Entry count. */
	uint8_t	 sys_define;		    /* System defined. */
	uint8_t	 entry_status;		    /* Entry Status. */
	uint32_t handle;
	__le16	compl_status;
#define ABTS_RESP_COMPL_SUCCESS		0
#define ABTS_RESP_COMPL_SUBCODE_ERROR	0x31
	__le16	nport_handle;
	__le16	reserved_1;
	uint8_t  reserved_2;
	uint8_t  reserved_3:4;
	uint8_t  sof_type:4;
	__le32	exchange_address;
	struct fcp_hdr_le fcp_hdr_le;
	uint8_t reserved_4[8];
	__le32	error_subcode1;
#define ABTS_RESP_SUBCODE_ERR_ABORTED_EXCH_NOT_TERM	0x1E
	__le32	error_subcode2;
	__le32	exchange_addr_to_abort;
} __packed;

/********************************************************************\
 * Type Definitions used by initiator & target halves
\********************************************************************/

struct qla_tgt_mgmt_cmd;
struct fc_port;
struct qla_tgt_cmd;

/*
 * This structure provides a template of function calls that the
 * target driver (from within qla_target.c) can issue to the
 * target module (tcm_qla2xxx).
 */
struct qla_tgt_func_tmpl {
	struct qla_tgt_cmd *(*find_cmd_by_tag)(struct fc_port *, uint64_t);
	int (*handle_cmd)(struct scsi_qla_host *, struct qla_tgt_cmd *,
			unsigned char *, uint32_t, int, int, int);
	void (*handle_data)(struct qla_tgt_cmd *);
	int (*handle_tmr)(struct qla_tgt_mgmt_cmd *, u64, uint16_t,
			uint32_t);
	struct qla_tgt_cmd *(*get_cmd)(struct fc_port *);
	void (*rel_cmd)(struct qla_tgt_cmd *);
	void (*free_cmd)(struct qla_tgt_cmd *);
	void (*free_mcmd)(struct qla_tgt_mgmt_cmd *);
	void (*free_session)(struct fc_port *);

	int (*check_initiator_node_acl)(struct scsi_qla_host *, unsigned char *,
					struct fc_port *);
	void (*update_sess)(struct fc_port *, port_id_t, uint16_t, bool);
	struct fc_port *(*find_sess_by_loop_id)(struct scsi_qla_host *,
						const uint16_t);
	struct fc_port *(*find_sess_by_s_id)(struct scsi_qla_host *,
					     const be_id_t);
	void (*clear_nacl_from_fcport_map)(struct fc_port *);
	void (*put_sess)(struct fc_port *);
	void (*shutdown_sess)(struct fc_port *);
	int (*get_dif_tags)(struct qla_tgt_cmd *cmd, uint16_t *pfw_prot_opts);
	int (*chk_dif_tags)(uint32_t tag);
	void (*add_target)(struct scsi_qla_host *);
	void (*remove_target)(struct scsi_qla_host *);
};

int qla2x00_wait_for_hba_online(struct scsi_qla_host *);

#include <target/target_core_base.h>

#define QLA_TGT_TIMEOUT			10	/* in seconds */

#define QLA_TGT_MAX_HW_PENDING_TIME	60 /* in seconds */

/* Immediate notify status constants */
#define IMM_NTFY_LIP_RESET          0x000E
#define IMM_NTFY_LIP_LINK_REINIT    0x000F
#define IMM_NTFY_IOCB_OVERFLOW      0x0016
#define IMM_NTFY_ABORT_TASK         0x0020
#define IMM_NTFY_PORT_LOGOUT        0x0029
#define IMM_NTFY_PORT_CONFIG        0x002A
#define IMM_NTFY_GLBL_TPRLO         0x002D
#define IMM_NTFY_GLBL_LOGO          0x002E
#define IMM_NTFY_RESOURCE           0x0034
#define IMM_NTFY_MSG_RX             0x0036
#define IMM_NTFY_SRR                0x0045
#define IMM_NTFY_ELS                0x0046

/* Immediate notify task flags */
#define IMM_NTFY_TASK_MGMT_SHIFT    8

#define QLA_TGT_CLEAR_ACA               0x40
#define QLA_TGT_TARGET_RESET            0x20
#define QLA_TGT_LUN_RESET               0x10
#define QLA_TGT_CLEAR_TS                0x04
#define QLA_TGT_ABORT_TS                0x02
#define QLA_TGT_ABORT_ALL_SESS          0xFFFF
#define QLA_TGT_ABORT_ALL               0xFFFE
#define QLA_TGT_NEXUS_LOSS_SESS         0xFFFD
#define QLA_TGT_NEXUS_LOSS              0xFFFC
#define QLA_TGT_ABTS			0xFFFB
#define QLA_TGT_2G_ABORT_TASK		0xFFFA

/* Notify Acknowledge flags */
#define NOTIFY_ACK_RES_COUNT        BIT_8
#define NOTIFY_ACK_CLEAR_LIP_RESET  BIT_5
#define NOTIFY_ACK_TM_RESP_CODE_VALID BIT_4

/* Command's states */
#define QLA_TGT_STATE_NEW		0 /* New command + target processing */
#define QLA_TGT_STATE_NEED_DATA		1 /* target needs data to continue */
#define QLA_TGT_STATE_DATA_IN		2 /* Data arrived + target processing */
#define QLA_TGT_STATE_PROCESSED		3 /* target done processing */

/* ATIO task_codes field */
#define ATIO_SIMPLE_QUEUE           0
#define ATIO_HEAD_OF_QUEUE          1
#define ATIO_ORDERED_QUEUE          2
#define ATIO_ACA_QUEUE              4
#define ATIO_UNTAGGED               5

/* TM failed response codes, see FCP (9.4.11 FCP_RSP_INFO) */
#define	FC_TM_SUCCESS               0
#define	FC_TM_BAD_FCP_DATA          1
#define	FC_TM_BAD_CMD               2
#define	FC_TM_FCP_DATA_MISMATCH     3
#define	FC_TM_REJECT                4
#define FC_TM_FAILED                5

#define QLA_TGT_SENSE_VALID(sense)  ((sense != NULL) && \
				(((const uint8_t *)(sense))[0] & 0x70) == 0x70)

struct qla_port_24xx_data {
	uint8_t port_name[WWN_SIZE];
	uint16_t loop_id;
	uint16_t reserved;
};

struct qla_qpair_hint {
	struct list_head hint_elem;
	struct qla_qpair *qpair;
	u16 cpuid;
	uint8_t cmd_cnt;
};

struct qla_tgt {
	struct scsi_qla_host *vha;
	struct qla_hw_data *ha;
	struct btree_head64 lun_qpair_map;
	struct qla_qpair_hint *qphints;
	/*
	 * To sync between IRQ handlers and qlt_target_release(). Needed,
	 * because req_pkt() can drop/reaquire HW lock inside. Protected by
	 * HW lock.
	 */
	int atio_irq_cmd_count;

	int sg_tablesize;

	/* Target's flags, serialized by pha->hardware_lock */
	unsigned int link_reinit_iocb_pending:1;

	/*
	 * Protected by tgt_mutex AND hardware_lock for writing and tgt_mutex
	 * OR hardware_lock for reading.
	 */
	int tgt_stop; /* the target mode driver is being stopped */
	int tgt_stopped; /* the target mode driver has been stopped */

	/* Count of sessions refering qla_tgt. Protected by hardware_lock. */
	int sess_count;

	/* Protected by hardware_lock */
	struct list_head del_sess_list;

	spinlock_t sess_work_lock;
	struct list_head sess_works_list;
	struct work_struct sess_work;

	struct imm_ntfy_from_isp link_reinit_iocb;
	wait_queue_head_t waitQ;
	int notify_ack_expected;
	int abts_resp_expected;
	int modify_lun_expected;
	atomic_t tgt_global_resets_count;
	struct list_head tgt_list_entry;
};

struct qla_tgt_sess_op {
	struct scsi_qla_host *vha;
	uint32_t chip_reset;
	struct atio_from_isp atio;
	struct work_struct work;
	struct list_head cmd_list;
	bool aborted;
	struct rsp_que *rsp;
};

enum trace_flags {
	TRC_NEW_CMD = BIT_0,
	TRC_DO_WORK = BIT_1,
	TRC_DO_WORK_ERR = BIT_2,
	TRC_XFR_RDY = BIT_3,
	TRC_XMIT_DATA = BIT_4,
	TRC_XMIT_STATUS = BIT_5,
	TRC_SRR_RSP =  BIT_6,
	TRC_SRR_XRDY = BIT_7,
	TRC_SRR_TERM = BIT_8,
	TRC_SRR_CTIO = BIT_9,
	TRC_FLUSH = BIT_10,
	TRC_CTIO_ERR = BIT_11,
	TRC_CTIO_DONE = BIT_12,
	TRC_CTIO_ABORTED =  BIT_13,
	TRC_CTIO_STRANGE = BIT_14,
	TRC_CMD_DONE = BIT_15,
	TRC_CMD_CHK_STOP = BIT_16,
	TRC_CMD_FREE = BIT_17,
	TRC_DATA_IN = BIT_18,
	TRC_ABORT = BIT_19,
	TRC_DIF_ERR = BIT_20,
};

struct qla_tgt_cmd {
	/*
	 * Do not move cmd_type field. it needs to line up with srb->cmd_type
	 */
	uint8_t cmd_type;
	uint8_t pad[7];
	struct se_cmd se_cmd;
	struct list_head sess_cmd_list;
	struct fc_port *sess;
	struct qla_qpair *qpair;
	uint32_t reset_count;
	int state;
	struct work_struct work;
	/* Sense buffer that will be mapped into outgoing status */
	unsigned char sense_buffer[TRANSPORT_SENSE_BUFFER];

	spinlock_t cmd_lock;
	/* to save extra sess dereferences */
	unsigned int conf_compl_supported:1;
	unsigned int sg_mapped:1;
	unsigned int free_sg:1;
	unsigned int write_data_transferred:1;
	unsigned int q_full:1;
	unsigned int term_exchg:1;
	unsigned int cmd_sent_to_fw:1;
	unsigned int cmd_in_wq:1;

	/*
	 * This variable may be set from outside the LIO and I/O completion
	 * callback functions. Do not declare this member variable as a
	 * bitfield to avoid a read-modify-write operation when this variable
	 * is set.
	 */
	unsigned int aborted;

	struct scatterlist *sg;	/* cmd data buffer SG vector */
	int sg_cnt;		/* SG segments count */
	int bufflen;		/* cmd buffer length */
	int offset;
	u64 unpacked_lun;
	enum dma_data_direction dma_data_direction;

	uint16_t ctio_flags;
	uint16_t vp_idx;
	uint16_t loop_id;	/* to save extra sess dereferences */
	struct qla_tgt *tgt;	/* to save extra sess dereferences */
	struct scsi_qla_host *vha;
	struct list_head cmd_list;

	struct atio_from_isp atio;

	uint8_t ctx_dsd_alloced;

	/* T10-DIF */
#define DIF_ERR_NONE 0
#define DIF_ERR_GRD 1
#define DIF_ERR_REF 2
#define DIF_ERR_APP 3
	int8_t dif_err_code;
	struct scatterlist *prot_sg;
	uint32_t prot_sg_cnt;
	uint32_t blk_sz, num_blks;
	uint8_t scsi_status, sense_key, asc, ascq;

	struct crc_context *ctx;
	const uint8_t	*cdb;
	uint64_t	lba;
	uint16_t	a_guard, e_guard, a_app_tag, e_app_tag;
	uint32_t	a_ref_tag, e_ref_tag;
#define DIF_BUNDL_DMA_VALID 1
	uint16_t prot_flags;

	uint64_t jiffies_at_alloc;
	uint64_t jiffies_at_free;

	enum trace_flags trc_flags;
};

struct qla_tgt_sess_work_param {
	struct list_head sess_works_list_entry;

#define QLA_TGT_SESS_WORK_ABORT	1
#define QLA_TGT_SESS_WORK_TM	2
	int type;

	union {
		struct abts_recv_from_24xx abts;
		struct imm_ntfy_from_isp tm_iocb;
		struct atio_from_isp tm_iocb2;
	};
};

struct qla_tgt_mgmt_cmd {
	uint8_t cmd_type;
	uint8_t pad[3];
	uint16_t tmr_func;
	uint8_t fc_tm_rsp;
	uint8_t abort_io_attr;
	struct fc_port *sess;
	struct qla_qpair *qpair;
	struct scsi_qla_host *vha;
	struct se_cmd se_cmd;
	struct work_struct free_work;
	unsigned int flags;
#define QLA24XX_MGMT_SEND_NACK	BIT_0
#define QLA24XX_MGMT_ABORT_IO_ATTR_VALID BIT_1
	uint32_t reset_count;
	struct work_struct work;
	uint64_t unpacked_lun;
	union {
		struct atio_from_isp atio;
		struct imm_ntfy_from_isp imm_ntfy;
		struct abts_recv_from_24xx abts;
	} __packed orig_iocb;
};

struct qla_tgt_prm {
	struct qla_tgt_cmd *cmd;
	struct qla_tgt *tgt;
	void *pkt;
	struct scatterlist *sg;	/* cmd data buffer SG vector */
	unsigned char *sense_buffer;
	int seg_cnt;
	int req_cnt;
	uint16_t rq_result;
	int sense_buffer_len;
	int residual;
	int add_status_pkt;
	/* dif */
	struct scatterlist *prot_sg;
	uint16_t prot_seg_cnt;
	uint16_t tot_dsds;
};

/* Check for Switch reserved address */
#define IS_SW_RESV_ADDR(_s_id) \
	((_s_id.b.domain == 0xff) && ((_s_id.b.area & 0xf0) == 0xf0))

#define QLA_TGT_XMIT_DATA		1
#define QLA_TGT_XMIT_STATUS		2
#define QLA_TGT_XMIT_ALL		(QLA_TGT_XMIT_STATUS|QLA_TGT_XMIT_DATA)


extern struct qla_tgt_data qla_target;

/*
 * Function prototypes for qla_target.c logic used by qla2xxx LLD code.
 */
extern int qlt_add_target(struct qla_hw_data *, struct scsi_qla_host *);
extern int qlt_remove_target(struct qla_hw_data *, struct scsi_qla_host *);
extern int qlt_lport_register(void *, u64, u64, u64,
			int (*callback)(struct scsi_qla_host *, void *, u64, u64));
extern void qlt_lport_deregister(struct scsi_qla_host *);
extern void qlt_unreg_sess(struct fc_port *);
extern void qlt_fc_port_added(struct scsi_qla_host *, fc_port_t *);
extern void qlt_fc_port_deleted(struct scsi_qla_host *, fc_port_t *, int);
extern int __init qlt_init(void);
extern void qlt_exit(void);
extern void qlt_update_vp_map(struct scsi_qla_host *, int);
extern void qlt_free_session_done(struct work_struct *);
/*
 * This macro is used during early initializations when host->active_mode
 * is not set. Right now, ha value is ignored.
 */
#define QLA_TGT_MODE_ENABLED() (ql2x_ini_mode != QLA2XXX_INI_MODE_ENABLED)

extern int ql2x_ini_mode;

static inline bool qla_tgt_mode_enabled(struct scsi_qla_host *ha)
{
	return ha->host->active_mode == MODE_TARGET;
}

static inline bool qla_ini_mode_enabled(struct scsi_qla_host *ha)
{
	return ha->host->active_mode == MODE_INITIATOR;
}

static inline bool qla_dual_mode_enabled(struct scsi_qla_host *ha)
{
	return (ha->host->active_mode == MODE_DUAL);
}

static inline uint32_t sid_to_key(const be_id_t s_id)
{
	return s_id.domain << 16 |
		s_id.area << 8 |
		s_id.al_pa;
}

/*
 * Exported symbols from qla_target.c LLD logic used by qla2xxx code..
 */
extern void qlt_response_pkt_all_vps(struct scsi_qla_host *, struct rsp_que *,
	response_t *);
extern int qlt_rdy_to_xfer(struct qla_tgt_cmd *);
extern int qlt_xmit_response(struct qla_tgt_cmd *, int, uint8_t);
extern int qlt_abort_cmd(struct qla_tgt_cmd *);
extern void qlt_xmit_tm_rsp(struct qla_tgt_mgmt_cmd *);
extern void qlt_free_mcmd(struct qla_tgt_mgmt_cmd *);
extern void qlt_free_cmd(struct qla_tgt_cmd *cmd);
extern void qlt_async_event(uint16_t, struct scsi_qla_host *, uint16_t *);
extern void qlt_enable_vha(struct scsi_qla_host *);
extern void qlt_vport_create(struct scsi_qla_host *, struct qla_hw_data *);
extern u8 qlt_rff_id(struct scsi_qla_host *);
extern void qlt_init_atio_q_entries(struct scsi_qla_host *);
extern void qlt_24xx_process_atio_queue(struct scsi_qla_host *, uint8_t);
extern void qlt_24xx_config_rings(struct scsi_qla_host *);
extern void qlt_24xx_config_nvram_stage1(struct scsi_qla_host *,
	struct nvram_24xx *);
extern void qlt_24xx_config_nvram_stage2(struct scsi_qla_host *,
	struct init_cb_24xx *);
extern void qlt_81xx_config_nvram_stage2(struct scsi_qla_host *,
	struct init_cb_81xx *);
extern void qlt_81xx_config_nvram_stage1(struct scsi_qla_host *,
	struct nvram_81xx *);
extern int qlt_24xx_process_response_error(struct scsi_qla_host *,
	struct sts_entry_24xx *);
extern void qlt_modify_vp_config(struct scsi_qla_host *,
	struct vp_config_entry_24xx *);
extern void qlt_probe_one_stage1(struct scsi_qla_host *, struct qla_hw_data *);
extern int qlt_mem_alloc(struct qla_hw_data *);
extern void qlt_mem_free(struct qla_hw_data *);
extern int qlt_stop_phase1(struct qla_tgt *);
extern void qlt_stop_phase2(struct qla_tgt *);
extern irqreturn_t qla83xx_msix_atio_q(int, void *);
extern void qlt_83xx_iospace_config(struct qla_hw_data *);
extern int qlt_free_qfull_cmds(struct qla_qpair *);
extern void qlt_logo_completion_handler(fc_port_t *, int);
extern void qlt_do_generation_tick(struct scsi_qla_host *, int *);

void qlt_send_resp_ctio(struct qla_qpair *, struct qla_tgt_cmd *, uint8_t,
    uint8_t, uint8_t, uint8_t);

#endif /* __QLA_TARGET_H */
