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
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */
/*
 * This is the global def file that is useful for including from the
 * target portion.
 */

#ifndef __QLA_TARGET_H
#define __QLA_TARGET_H

#include "qla_def.h"

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

#define QLA2XXX_INI_MODE_EXCLUSIVE	0
#define QLA2XXX_INI_MODE_DISABLED	1
#define QLA2XXX_INI_MODE_ENABLED	2

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

#ifndef IMMED_NOTIFY_TYPE
#define IMMED_NOTIFY_TYPE 0x0D		/* Immediate notify entry. */
/*
 * ISP queue -	immediate notify entry structure definition.
 *		This is sent by the ISP to the Target driver.
 *		This IOCB would have report of events sent by the
 *		initiator, that needs to be handled by the target
 *		driver immediately.
 */
struct imm_ntfy_from_isp {
	uint8_t	 entry_type;		    /* Entry type. */
	uint8_t	 entry_count;		    /* Entry count. */
	uint8_t	 sys_define;		    /* System defined. */
	uint8_t	 entry_status;		    /* Entry Status. */
	union {
		struct {
			uint32_t sys_define_2; /* System defined. */
			target_id_t target;
			uint16_t lun;
			uint8_t  target_id;
			uint8_t  reserved_1;
			uint16_t status_modifier;
			uint16_t status;
			uint16_t task_flags;
			uint16_t seq_id;
			uint16_t srr_rx_id;
			uint32_t srr_rel_offs;
			uint16_t srr_ui;
#define SRR_IU_DATA_IN	0x1
#define SRR_IU_DATA_OUT	0x5
#define SRR_IU_STATUS	0x7
			uint16_t srr_ox_id;
			uint8_t reserved_2[28];
		} isp2x;
		struct {
			uint32_t reserved;
			uint16_t nport_handle;
			uint16_t reserved_2;
			uint16_t flags;
#define NOTIFY24XX_FLAGS_GLOBAL_TPRLO   BIT_1
#define NOTIFY24XX_FLAGS_PUREX_IOCB     BIT_0
			uint16_t srr_rx_id;
			uint16_t status;
			uint8_t  status_subcode;
			uint8_t  fw_handle;
			uint32_t exchange_address;
			uint32_t srr_rel_offs;
			uint16_t srr_ui;
			uint16_t srr_ox_id;
			uint8_t  reserved_4[19];
			uint8_t  vp_index;
			uint32_t reserved_5;
			uint8_t  port_id[3];
			uint8_t  reserved_6;
		} isp24;
	} u;
	uint16_t reserved_7;
	uint16_t ox_id;
} __packed;
#endif

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
			uint32_t sys_define_2; /* System defined. */
			target_id_t target;
			uint8_t	 target_id;
			uint8_t	 reserved_1;
			uint16_t flags;
			uint16_t resp_code;
			uint16_t status;
			uint16_t task_flags;
			uint16_t seq_id;
			uint16_t srr_rx_id;
			uint32_t srr_rel_offs;
			uint16_t srr_ui;
			uint16_t srr_flags;
			uint16_t srr_reject_code;
			uint8_t  srr_reject_vendor_uniq;
			uint8_t  srr_reject_code_expl;
			uint8_t  reserved_2[24];
		} isp2x;
		struct {
			uint32_t handle;
			uint16_t nport_handle;
			uint16_t reserved_1;
			uint16_t flags;
			uint16_t srr_rx_id;
			uint16_t status;
			uint8_t  status_subcode;
			uint8_t  fw_handle;
			uint32_t exchange_address;
			uint32_t srr_rel_offs;
			uint16_t srr_ui;
			uint16_t srr_flags;
			uint8_t  reserved_4[19];
			uint8_t  vp_index;
			uint8_t  srr_reject_vendor_uniq;
			uint8_t  srr_reject_code_expl;
			uint8_t  srr_reject_code;
			uint8_t  reserved_5[5];
		} isp24;
	} u;
	uint8_t  reserved[2];
	uint16_t ox_id;
} __packed;
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
	uint16_t rx_id;
	uint16_t flags;
	uint16_t status;
	uint16_t timeout;		/* 0 = 30 seconds, 0xFFFF = disable */
	uint16_t dseg_count;		/* Data segment count. */
	uint32_t relative_offset;
	uint32_t residual;
	uint16_t reserved_1[3];
	uint16_t scsi_status;
	uint32_t transfer_length;
	uint32_t dseg_0_address;	/* Data segment 0 address. */
	uint32_t dseg_0_length;		/* Data segment 0 length. */
	uint32_t dseg_1_address;	/* Data segment 1 address. */
	uint32_t dseg_1_length;		/* Data segment 1 length. */
	uint32_t dseg_2_address;	/* Data segment 2 address. */
	uint32_t dseg_2_length;		/* Data segment 2 length. */
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
	uint8_t  d_id[3];
	uint8_t  cs_ctl;
	uint8_t  s_id[3];
	uint8_t  type;
	uint8_t  f_ctl[3];
	uint8_t  seq_id;
	uint8_t  df_ctl;
	uint16_t seq_cnt;
	__be16   ox_id;
	uint16_t rx_id;
	uint32_t parameter;
} __packed;

struct fcp_hdr_le {
	uint8_t  d_id[3];
	uint8_t  r_ctl;
	uint8_t  s_id[3];
	uint8_t  cs_ctl;
	uint8_t  f_ctl[3];
	uint8_t  type;
	uint16_t seq_cnt;
	uint8_t  df_ctl;
	uint8_t  seq_id;
	uint16_t rx_id;
	uint16_t ox_id;
	uint32_t parameter;
} __packed;

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
	/* uint32_t data_length; */
} __packed;

/*
 * ISP queue -	Accept Target I/O (ATIO) type entry IOCB structure.
 *		This is sent from the ISP to the target driver.
 */
struct atio_from_isp {
	union {
		struct {
			uint16_t entry_hdr;
			uint8_t  sys_define;   /* System defined. */
			uint8_t  entry_status; /* Entry Status.   */
			uint32_t sys_define_2; /* System defined. */
			target_id_t target;
			uint16_t rx_id;
			uint16_t flags;
			uint16_t status;
			uint8_t  command_ref;
			uint8_t  task_codes;
			uint8_t  task_flags;
			uint8_t  execution_codes;
			uint8_t  cdb[MAX_CMDSZ];
			uint32_t data_length;
			uint16_t lun;
			uint8_t  initiator_port_name[WWN_SIZE]; /* on qla23xx */
			uint16_t reserved_32[6];
			uint16_t ox_id;
		} isp2x;
		struct {
			uint16_t entry_hdr;
			uint8_t  fcp_cmnd_len_low;
			uint8_t  fcp_cmnd_len_high:4;
			uint8_t  attr:4;
			uint32_t exchange_addr;
#define ATIO_EXCHANGE_ADDRESS_UNKNOWN	0xFFFFFFFF
			struct fcp_hdr fcp_hdr;
			struct atio7_fcp_cmnd fcp_cmnd;
		} isp24;
		struct {
			uint8_t  entry_type;	/* Entry type. */
			uint8_t  entry_count;	/* Entry count. */
			uint8_t  data[58];
			uint32_t signature;
#define ATIO_PROCESSED 0xDEADDEAD		/* Signature */
		} raw;
	} u;
} __packed;

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
	uint16_t nport_handle;
#define CTIO7_NHANDLE_UNRECOGNIZED	0xFFFF
	uint16_t timeout;
	uint16_t dseg_count;		    /* Data segment count. */
	uint8_t  vp_index;
	uint8_t  add_flags;
	uint8_t  initiator_id[3];
	uint8_t  reserved;
	uint32_t exchange_addr;
	union {
		struct {
			uint16_t reserved1;
			__le16 flags;
			uint32_t residual;
			__le16 ox_id;
			uint16_t scsi_status;
			uint32_t relative_offset;
			uint32_t reserved2;
			uint32_t transfer_length;
			uint32_t reserved3;
			/* Data segment 0 address. */
			uint32_t dseg_0_address[2];
			/* Data segment 0 length. */
			uint32_t dseg_0_length;
		} status0;
		struct {
			uint16_t sense_length;
			uint16_t flags;
			uint32_t residual;
			__le16 ox_id;
			uint16_t scsi_status;
			uint16_t response_len;
			uint16_t reserved;
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
	uint16_t status;
	uint16_t timeout;
	uint16_t dseg_count;		    /* Data segment count. */
	uint8_t  vp_index;
	uint8_t  reserved1[5];
	uint32_t exchange_address;
	uint16_t reserved2;
	uint16_t flags;
	uint32_t residual;
	uint16_t ox_id;
	uint16_t reserved3;
	uint32_t relative_offset;
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
	uint16_t nport_handle;		/* N_PORT handle. */
	__le16 timeout;		/* Command timeout. */

	uint16_t dseg_count;		/* Data segment count. */
	uint8_t  vp_index;
	uint8_t  add_flags;		/* additional flags */
#define CTIO_CRC2_AF_DIF_DSD_ENA BIT_3

	uint8_t  initiator_id[3];	/* initiator ID */
	uint8_t  reserved1;
	uint32_t exchange_addr;		/* rcv exchange address */
	uint16_t reserved2;
	__le16 flags;			/* refer to CTIO7 flags values */
	uint32_t residual;
	__le16 ox_id;
	uint16_t scsi_status;
	__le32 relative_offset;
	uint32_t reserved5;
	__le32 transfer_length;		/* total fc transfer length */
	uint32_t reserved6;
	__le32 crc_context_address[2];/* Data segment address. */
	uint16_t crc_context_len;	/* Data segment length. */
	uint16_t reserved_1;		/* MUST be set to 0. */
} __packed;

/* CTIO Type CRC_x Status IOCB */
struct ctio_crc_from_fw {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */
	uint16_t status;
	uint16_t timeout;		/* Command timeout. */
	uint16_t dseg_count;		/* Data segment count. */
	uint32_t reserved1;
	uint16_t state_flags;
#define CTIO_CRC_SF_DIF_CHOPPED BIT_4

	uint32_t exchange_address;	/* rcv exchange address */
	uint16_t reserved2;
	uint16_t flags;
	uint32_t resid_xfer_length;
	uint16_t ox_id;
	uint8_t  reserved3[12];
	uint16_t runt_guard;		/* reported runt blk guard */
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
	uint16_t nport_handle;
	uint8_t  reserved_2[2];
	uint8_t  vp_index;
	uint8_t  reserved_3:4;
	uint8_t  sof_type:4;
	uint32_t exchange_address;
	struct fcp_hdr_le fcp_hdr_le;
	uint8_t  reserved_4[16];
	uint32_t exchange_addr_to_abort;
} __packed;

#define ABTS_PARAM_ABORT_SEQ		BIT_0

struct ba_acc_le {
	uint16_t reserved;
	uint8_t  seq_id_last;
	uint8_t  seq_id_valid;
#define SEQ_ID_VALID	0x80
#define SEQ_ID_INVALID	0x00
	uint16_t rx_id;
	uint16_t ox_id;
	uint16_t high_seq_cnt;
	uint16_t low_seq_cnt;
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
	uint16_t reserved_1;
	uint16_t nport_handle;
	uint16_t control_flags;
#define ABTS_CONTR_FLG_TERM_EXCHG	BIT_0
	uint8_t  vp_index;
	uint8_t  reserved_3:4;
	uint8_t  sof_type:4;
	uint32_t exchange_address;
	struct fcp_hdr_le fcp_hdr_le;
	union {
		struct ba_acc_le ba_acct;
		struct ba_rjt_le ba_rjt;
	} __packed payload;
	uint32_t reserved_4;
	uint32_t exchange_addr_to_abort;
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
	uint16_t compl_status;
#define ABTS_RESP_COMPL_SUCCESS		0
#define ABTS_RESP_COMPL_SUBCODE_ERROR	0x31
	uint16_t nport_handle;
	uint16_t reserved_1;
	uint8_t  reserved_2;
	uint8_t  reserved_3:4;
	uint8_t  sof_type:4;
	uint32_t exchange_address;
	struct fcp_hdr_le fcp_hdr_le;
	uint8_t reserved_4[8];
	uint32_t error_subcode1;
#define ABTS_RESP_SUBCODE_ERR_ABORTED_EXCH_NOT_TERM	0x1E
	uint32_t error_subcode2;
	uint32_t exchange_addr_to_abort;
} __packed;

/********************************************************************\
 * Type Definitions used by initiator & target halves
\********************************************************************/

struct qla_tgt_mgmt_cmd;
struct qla_tgt_sess;

/*
 * This structure provides a template of function calls that the
 * target driver (from within qla_target.c) can issue to the
 * target module (tcm_qla2xxx).
 */
struct qla_tgt_func_tmpl {

	int (*handle_cmd)(struct scsi_qla_host *, struct qla_tgt_cmd *,
			unsigned char *, uint32_t, int, int, int);
	void (*handle_data)(struct qla_tgt_cmd *);
	void (*handle_dif_err)(struct qla_tgt_cmd *);
	int (*handle_tmr)(struct qla_tgt_mgmt_cmd *, uint32_t, uint8_t,
			uint32_t);
	void (*free_cmd)(struct qla_tgt_cmd *);
	void (*free_mcmd)(struct qla_tgt_mgmt_cmd *);
	void (*free_session)(struct qla_tgt_sess *);

	int (*check_initiator_node_acl)(struct scsi_qla_host *, unsigned char *,
					void *, uint8_t *, uint16_t);
	void (*update_sess)(struct qla_tgt_sess *, port_id_t, uint16_t, bool);
	struct qla_tgt_sess *(*find_sess_by_loop_id)(struct scsi_qla_host *,
						const uint16_t);
	struct qla_tgt_sess *(*find_sess_by_s_id)(struct scsi_qla_host *,
						const uint8_t *);
	void (*clear_nacl_from_fcport_map)(struct qla_tgt_sess *);
	void (*put_sess)(struct qla_tgt_sess *);
	void (*shutdown_sess)(struct qla_tgt_sess *);
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

/* Notify Acknowledge flags */
#define NOTIFY_ACK_RES_COUNT        BIT_8
#define NOTIFY_ACK_CLEAR_LIP_RESET  BIT_5
#define NOTIFY_ACK_TM_RESP_CODE_VALID BIT_4

/* Command's states */
#define QLA_TGT_STATE_NEW		0 /* New command + target processing */
#define QLA_TGT_STATE_NEED_DATA		1 /* target needs data to continue */
#define QLA_TGT_STATE_DATA_IN		2 /* Data arrived + target processing */
#define QLA_TGT_STATE_PROCESSED		3 /* target done processing */
#define QLA_TGT_STATE_ABORTED		4 /* Command aborted */

/* Special handles */
#define QLA_TGT_NULL_HANDLE	0
#define QLA_TGT_SKIP_HANDLE	(0xFFFFFFFF & ~CTIO_COMPLETION_HANDLE_MARK)

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

/*
 * Error code of qlt_pre_xmit_response() meaning that cmd's exchange was
 * terminated, so no more actions is needed and success should be returned
 * to target.
 */
#define QLA_TGT_PRE_XMIT_RESP_CMD_ABORTED	0x1717

#if (BITS_PER_LONG > 32) || defined(CONFIG_HIGHMEM64G)
#define pci_dma_lo32(a) (a & 0xffffffff)
#define pci_dma_hi32(a) ((((a) >> 16)>>16) & 0xffffffff)
#else
#define pci_dma_lo32(a) (a & 0xffffffff)
#define pci_dma_hi32(a) 0
#endif

#define QLA_TGT_SENSE_VALID(sense)  ((sense != NULL) && \
				(((const uint8_t *)(sense))[0] & 0x70) == 0x70)

struct qla_port_24xx_data {
	uint8_t port_name[WWN_SIZE];
	uint16_t loop_id;
	uint16_t reserved;
};

struct qla_tgt {
	struct scsi_qla_host *vha;
	struct qla_hw_data *ha;

	/*
	 * To sync between IRQ handlers and qlt_target_release(). Needed,
	 * because req_pkt() can drop/reaquire HW lock inside. Protected by
	 * HW lock.
	 */
	int irq_cmd_count;

	int datasegs_per_cmd, datasegs_per_cont, sg_tablesize;

	/* Target's flags, serialized by pha->hardware_lock */
	unsigned int tgt_enable_64bit_addr:1; /* 64-bits PCI addr enabled */
	unsigned int link_reinit_iocb_pending:1;

	/*
	 * Protected by tgt_mutex AND hardware_lock for writing and tgt_mutex
	 * OR hardware_lock for reading.
	 */
	int tgt_stop; /* the target mode driver is being stopped */
	int tgt_stopped; /* the target mode driver has been stopped */

	/* Count of sessions refering qla_tgt. Protected by hardware_lock. */
	int sess_count;

	/* Protected by hardware_lock. Addition also protected by tgt_mutex. */
	struct list_head sess_list;

	/* Protected by hardware_lock */
	struct list_head del_sess_list;
	struct delayed_work sess_del_work;

	spinlock_t sess_work_lock;
	struct list_head sess_works_list;
	struct work_struct sess_work;

	struct imm_ntfy_from_isp link_reinit_iocb;
	wait_queue_head_t waitQ;
	int notify_ack_expected;
	int abts_resp_expected;
	int modify_lun_expected;

	int ctio_srr_id;
	int imm_srr_id;
	spinlock_t srr_lock;
	struct list_head srr_ctio_list;
	struct list_head srr_imm_list;
	struct work_struct srr_work;

	atomic_t tgt_global_resets_count;

	struct list_head tgt_list_entry;
};

struct qla_tgt_sess_op {
	struct scsi_qla_host *vha;
	struct atio_from_isp atio;
	struct work_struct work;
};

/*
 * Equivilant to IT Nexus (Initiator-Target)
 */
struct qla_tgt_sess {
	uint16_t loop_id;
	port_id_t s_id;

	unsigned int conf_compl_supported:1;
	unsigned int deleted:1;
	unsigned int local:1;

	struct se_session *se_sess;
	struct scsi_qla_host *vha;
	struct qla_tgt *tgt;

	struct list_head sess_list_entry;
	unsigned long expires;
	struct list_head del_list_entry;

	uint8_t port_name[WWN_SIZE];
	struct work_struct free_work;
};

struct qla_tgt_cmd {
	struct se_cmd se_cmd;
	struct qla_tgt_sess *sess;
	int state;
	struct work_struct free_work;
	struct work_struct work;
	/* Sense buffer that will be mapped into outgoing status */
	unsigned char sense_buffer[TRANSPORT_SENSE_BUFFER];

	/* to save extra sess dereferences */
	unsigned int conf_compl_supported:1;
	unsigned int sg_mapped:1;
	unsigned int free_sg:1;
	unsigned int aborted:1; /* Needed in case of SRR */
	unsigned int write_data_transferred:1;
	unsigned int ctx_dsd_alloced:1;
	unsigned int q_full:1;
	unsigned int term_exchg:1;

	struct scatterlist *sg;	/* cmd data buffer SG vector */
	int sg_cnt;		/* SG segments count */
	int bufflen;		/* cmd buffer length */
	int offset;
	uint32_t tag;
	uint32_t unpacked_lun;
	enum dma_data_direction dma_data_direction;
	uint32_t reset_count;

	uint16_t loop_id;	/* to save extra sess dereferences */
	struct qla_tgt *tgt;	/* to save extra sess dereferences */
	struct scsi_qla_host *vha;
	struct list_head cmd_list;

	struct atio_from_isp atio;
	/* t10dif */
	struct scatterlist *prot_sg;
	uint32_t prot_sg_cnt;
	uint32_t blk_sz;
	struct crc_context *ctx;

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
	uint8_t tmr_func;
	uint8_t fc_tm_rsp;
	struct qla_tgt_sess *sess;
	struct se_cmd se_cmd;
	struct work_struct free_work;
	unsigned int flags;
	uint32_t reset_count;
#define QLA24XX_MGMT_SEND_NACK	1
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
	int seg_cnt;
	int req_cnt;
	uint16_t rq_result;
	uint16_t scsi_status;
	unsigned char *sense_buffer;
	int sense_buffer_len;
	int residual;
	int add_status_pkt;
	/* dif */
	struct scatterlist *prot_sg;
	uint16_t prot_seg_cnt;
	uint16_t tot_dsds;
};

struct qla_tgt_srr_imm {
	struct list_head srr_list_entry;
	int srr_id;
	struct imm_ntfy_from_isp imm_ntfy;
};

struct qla_tgt_srr_ctio {
	struct list_head srr_list_entry;
	int srr_id;
	struct qla_tgt_cmd *cmd;
};

#define QLA_TGT_XMIT_DATA		1
#define QLA_TGT_XMIT_STATUS		2
#define QLA_TGT_XMIT_ALL		(QLA_TGT_XMIT_STATUS|QLA_TGT_XMIT_DATA)


extern struct qla_tgt_data qla_target;
/*
 * Internal function prototypes
 */
void qlt_disable_vha(struct scsi_qla_host *);

/*
 * Function prototypes for qla_target.c logic used by qla2xxx LLD code.
 */
extern int qlt_add_target(struct qla_hw_data *, struct scsi_qla_host *);
extern int qlt_remove_target(struct qla_hw_data *, struct scsi_qla_host *);
extern int qlt_lport_register(void *, u64, u64, u64,
			int (*callback)(struct scsi_qla_host *, void *, u64, u64));
extern void qlt_lport_deregister(struct scsi_qla_host *);
extern void qlt_unreg_sess(struct qla_tgt_sess *);
extern void qlt_fc_port_added(struct scsi_qla_host *, fc_port_t *);
extern void qlt_fc_port_deleted(struct scsi_qla_host *, fc_port_t *);
extern void qlt_set_mode(struct scsi_qla_host *ha);
extern void qlt_clear_mode(struct scsi_qla_host *ha);
extern int __init qlt_init(void);
extern void qlt_exit(void);
extern void qlt_update_vp_map(struct scsi_qla_host *, int);

/*
 * This macro is used during early initializations when host->active_mode
 * is not set. Right now, ha value is ignored.
 */
#define QLA_TGT_MODE_ENABLED() (ql2x_ini_mode != QLA2XXX_INI_MODE_ENABLED)
extern int ql2x_ini_mode;

static inline bool qla_tgt_mode_enabled(struct scsi_qla_host *ha)
{
	return ha->host->active_mode & MODE_TARGET;
}

static inline bool qla_ini_mode_enabled(struct scsi_qla_host *ha)
{
	return ha->host->active_mode & MODE_INITIATOR;
}

static inline void qla_reverse_ini_mode(struct scsi_qla_host *ha)
{
	if (ha->host->active_mode & MODE_INITIATOR)
		ha->host->active_mode &= ~MODE_INITIATOR;
	else
		ha->host->active_mode |= MODE_INITIATOR;
}

/*
 * Exported symbols from qla_target.c LLD logic used by qla2xxx code..
 */
extern void qlt_24xx_atio_pkt_all_vps(struct scsi_qla_host *,
	struct atio_from_isp *);
extern void qlt_response_pkt_all_vps(struct scsi_qla_host *, response_t *);
extern int qlt_rdy_to_xfer(struct qla_tgt_cmd *);
extern int qlt_xmit_response(struct qla_tgt_cmd *, int, uint8_t);
extern int qlt_rdy_to_xfer_dif(struct qla_tgt_cmd *);
extern int qlt_xmit_response_dif(struct qla_tgt_cmd *, int, uint8_t);
extern void qlt_xmit_tm_rsp(struct qla_tgt_mgmt_cmd *);
extern void qlt_free_mcmd(struct qla_tgt_mgmt_cmd *);
extern void qlt_free_cmd(struct qla_tgt_cmd *cmd);
extern void qlt_async_event(uint16_t, struct scsi_qla_host *, uint16_t *);
extern void qlt_enable_vha(struct scsi_qla_host *);
extern void qlt_vport_create(struct scsi_qla_host *, struct qla_hw_data *);
extern void qlt_rff_id(struct scsi_qla_host *, struct ct_sns_req *);
extern void qlt_init_atio_q_entries(struct scsi_qla_host *);
extern void qlt_24xx_process_atio_queue(struct scsi_qla_host *);
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
extern int qlt_free_qfull_cmds(struct scsi_qla_host *);

#endif /* __QLA_TARGET_H */
