/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2026- Marvell.
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#ifndef __QLA_FW29_H
#define __QLA_FW29_H

#include "qla_fw.h"

/* Control Flags 2 common for cmd6 and 7 */
#define CF2_VMID_ENABLE			BIT_0
#define CF2_CSCTL_PRIORITY_TAG		BIT_1
#define CF2_NO_TRNF_READY_ENABLE	BIT_2
#define CF2_RX_ID_ENABLE		BIT_3

/*
 * vp_index layout for 29xx extended command IOCBs
 * (cmd_type_6_ext, cmd_type_7_ext, cmd_type_crc_2_ext, ...):
 *   bits [8:0]   - VP index (9 bits)
 *   bits [15:9]  - reserved, must be zero
 * Access on a host-endian value via le16_to_cpu(vp_index) & CMD_EXT_VP_INDEX_MASK.
 */
#define CMD_EXT_VP_INDEX_MASK		0x01ff

/*
 * Combined vp_index/sof_type field layout (used by ELS and ABTS ext IOCBs):
 *   bits [8:0]   - VP index (9 bits)
 *   bits [11:9]  - reserved
 *   bits [15:12] - SOF type (4 bits)
 */
#define EXT_VP_SOF_VP_INDEX_MASK	0x01ff
#define EXT_VP_SOF_SOF_TYPE_SHIFT	12
#define EXT_VP_SOF_SOF_TYPE_MASK	0xf000

static inline u16 qla_ext_get_vp_index(__le16 vp_sof)
{
	return le16_to_cpu(vp_sof) & EXT_VP_SOF_VP_INDEX_MASK;
}

static inline u16 qla_ext_get_sof_type(__le16 vp_sof)
{
	return (le16_to_cpu(vp_sof) >> EXT_VP_SOF_SOF_TYPE_SHIFT) & 0xf;
}

static inline __le16 qla_ext_build_vp_sof(u16 vp_idx, u16 sof_type)
{
	return cpu_to_le16((vp_idx & EXT_VP_SOF_VP_INDEX_MASK) |
			   ((sof_type & 0xf) << EXT_VP_SOF_SOF_TYPE_SHIFT));
}

/*
 * Combined vp_idx/vp_status field layout (vp_rpt_id_entry_24xx_ext):
 *   bits [8:0]   - VP index (9 bits)
 *   bits [15:9]  - VP status (7 bits)
 */
#define EXT_VP_STATUS_VP_INDEX_MASK	0x01ff
#define EXT_VP_STATUS_VP_STATUS_SHIFT	9
#define EXT_VP_STATUS_VP_STATUS_MASK	0xfe00

/*
 * ISP queue - command entry structure definition.
 */
#define NUM_CMD67_DSDS	4
struct cmd_type_6_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	__le16	nport_handle;		/* N_PORT handle. */
	__le16	timeout;		/* Command timeout. */

	__le16	dseg_count;		/* Data segment count. */

	__le16	fcp_rsp_dsd_len;	/* FCP_RSP DSD length. */

	struct scsi_lun lun;		/* FCP LUN (BE). */

	__le16	control_flags;		/* Control flags. */

	__le16	fcp_cmnd_dseg_len;	/* Data segment length. */
					/* Data segment address. */
	__le64	 fcp_cmnd_dseg_address __packed;
					/* Data segment address. */
	__le64	 fcp_rsp_dseg_address __packed;

	__le32	byte_count;		/* Total byte count. */
	__le16	control_flags_2;		/* Control flags 2. */

	__le16	vp_index;		/* VP Index 9bits*/
	__le32	fburstlen_rxid;		/* First Burst length/RX ID */
	__le16 io_tag;			/* I/O Tag */
	uint8_t vl_n_fctl;		/* VL (7-4) | RSVD (3-2) | F_CTL [17] (1) | RSVD (0) */
	uint8_t prtag_csctl;		/* Priority Tag or CS_CTL */
	__le32	src_vm_id;		/* Source VM ID */
	uint8_t reserved_2[16];		/* Reserved */
	struct dsd64 dsd[NUM_CMD67_DSDS];		/* Data Segment Descriptors */
};

struct cmd_type_7_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */
	uint32_t handle;		/* System handle. */
	__le16	nport_handle;		/* N_PORT handle. */
	__le16	timeout;		/* Command timeout. */

	__le16	dseg_count;		/* Data segment count. */
	uint16_t reserved_1;

	struct scsi_lun lun;		/* FCP LUN (BE). */

	__le16	task_mgmt_flags;	/* Task management flags. */

	uint8_t task;
	uint8_t crn;
	uint8_t fcp_cdb[MAX_CMDSZ];	/* SCSI command words. */
	__le32	byte_count;		/* Total byte count. */
	__le16	ctrl_flags_2;		/* Control flags 2 */
	__le16	vp_index;		/* VP Index 9bits*/
	__le32	rx_id;			/* Receive Exchange ID */
	__le16	io_tag;			/* I/O Tag */
	uint8_t vl_n_fctl;		/* VL (7-4) | RSVD (3-2) | F_CTL [17] (1) | RSVD (0) */
	uint8_t reserved_3[21];		/* Reserved */
	struct dsd64 dsd[NUM_CMD67_DSDS];	/* Data Segment Descriptors */
};

/*
 * Inline data-DSD capacity of the 29xx cmd_type_crc_2_ext IOCB.  Unlike
 * cmd_type_6_ext / cmd_type_7_ext (which carry NUM_CMD67_DSDS inline DSDs),
 * CRC_2 places the bulk of its DSDs in the separate CRC-context DMA; only
 * a single data_dsd is carried inline in both the u.nobundling and
 * u.bundling variants.  Use this constant wherever the IOCB-reservation
 * calculator needs the CRC_2 ext inline capacity so it stays in sync with
 * the firmware-facing layout below.
 */
#define NUM_CRC2_EXT_INLINE_DSDS	1

struct cmd_type_crc_2_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	__le16	nport_handle;		/* N_PORT handle. */
	__le16	timeout;		/* Command timeout. */

	__le16	dseg_count;		/* Data segment count. */
	__le16	fcp_rsp_dseg_len;	/* FCP_RSP DSD length. */

	struct scsi_lun lun;		/* FCP LUN (BE). */

	__le16	control_flags_1;		/* Control flags. */
	__le16	fcp_cmnd_dseg_len;	/* Data segment length. */

	__le64	 fcp_cmnd_dseg_address __packed;
					/* Data segment address. */
	__le64	 fcp_rsp_dseg_address __packed;

	__le32	byte_count;		/* Total byte count. */

	__le16	control_flags_2;		/* Control flags - 2 */
	__le16	vp_index;		/* VP Index (bits [8:0]); bits [15:9] reserved.
					 * See CMD_EXT_VP_INDEX_MASK.
					 */

	uint32_t reserved_1;

	__le16	 iocb_tag; /* Unused */
	__le16 vl_prio; /* Bit 1 - F_CTL, Bits 4-7 VL, rest are rsvd */

	uint32_t reserved_2; /* 3C-3F offset */

	__le32 ref_tag;
	uint8_t ref_tag_mask[4];	/* Validation/Replacement Mask*/

	__le16 app_tag;
	uint8_t app_tag_mask[2];	/* Validation/Replacement Mask*/

	__le16 blk_size;		/* Data size in bytes */
	__le16 prot_opts;		/* Requested Data Protection Mode */

	__le32 tot_byte_count;		/* Total byte count/ total data
					 * transfer count
					 */
	union {
		struct {
			uint32_t	reserved_1; /* offset 54 */
			uint16_t	reserved_2;
			__le16		guard_seed; /* offset 5A */
			struct dsd64	data_dsd[1];
			uint32_t	reserved_5[2];
			uint32_t	reserved_6;
		} nobundling;
		struct {
			__le32	dif_byte_count;	/* Total DIF byte
						 * count
						 */
			__le16	dseg_count;	/* Data segment count */
			__le16 guard_seed;      /* Initial Guard Seed */
			struct dsd64	data_dsd[1];
			struct dsd64	dif_dsd;
		} bundling;
	} u;
	uint8_t reserved_3[12];			/* MUST be set to 0. */
};

/*
 * ISP queue - status entry structure definition.
 */
struct sts_entry_24xx_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	__le16	comp_status;		/* Completion status. */
	__le16	ox_id;			/* OX_ID used by the firmware. */

	__le32	residual_len;		/* FW calc residual transfer length. */

	union {
		__le16 reserved_1;
		__le16 nvme_rsp_pyld_len;
	} u1;

	__le16	state_flags;		/* State flags. */

	__le16 read_sa_index;
	__le16 wr_sa_index;
	uint8_t	reserved_2[8];
	uint8_t act_dif[8];
	uint8_t exp_dif[8];
	union {
		struct {
			__le32	rsp_data_len_dma;	/* FCP response data length  */
			uint8_t reserved_3[76];
		};
		struct {
			uint8_t nvme_ersp_data[32];
			uint8_t reserved_4[48];
		};
		struct {
			__le32	bid_rd_rsp_residual_count;	/* BID read rsp residual cnt */
			__le16	retry_delay_timer;	/* Retry delay timer. */
			__le16	scsi_status;		/* SCSI status. */
			__le32	rsp_residual_count;	/* FCP RSP residual count. */
			__le32	sense_len;		/* FCP SENSE length. */
			__le32	rsp_data_len_ndma;	/* FCP response data length  */
			uint8_t	data[60];	/* FCP rsp/sense information */
		};
	} u2;

	/*
	 * If DIF Error is set in comp_status, these additional fields are
	 * defined:
	 *
	 * !!! NOTE: Firmware sends expected/actual DIF data in big endian
	 * format; but all of the "data" field gets swab32-d in the beginning
	 * of qla2900_status_entry().
	 *
	 * &data[10] : uint8_t report_runt_bg[2];	- computed guard
	 * &data[12] : uint8_t actual_dif[8];		- DIF Data received
	 * &data[20] : uint8_t expected_dif[8];		- DIF Data computed
	 */
};

/*
 * ISP queue - marker entry structure definition.
 */
struct mrk_entry_24xx_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	__le16	nport_handle;		/* N_PORT handle. */

	uint8_t modifier;		/* Modifier (7-0). */
	uint8_t reserved_1;

	__le16	vp_index;	/* VP Index. 9bits*/
	uint16_t reserved_3;

	uint8_t lun[8];			/* FCP LUN (BE). */
	uint8_t reserved_4[104];
};

/*
 * ISP queue - CT Pass-Through entry structure definition.
 */
#define NUM_CT_DSDS	5
struct ct_entry_24xx_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System Defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	__le16	comp_status;		/* Completion status. */

	__le16	nport_handle;		/* N_PORT handle. */

	__le16	cmd_dsd_count;

	__le16	vp_index;		/* vp index 9 bits*/

	__le16	timeout;		/* Command timeout. */
	uint16_t reserved_2;

	__le16	rsp_dsd_count;

	uint8_t reserved_3[10];
	uint8_t reserved_4[28];		/* Reserved. */

	__le32	rsp_byte_count;
	__le32	cmd_byte_count;
	struct dsd64 dsd[NUM_CT_DSDS];	/* Data Segment Descriptors */
};

/*
 * 29xx extended Link Service pass-through request IOCB (128 bytes).
 *
 * Same wire purpose as the 64-byte struct pt_ls4_request used on 24xx-class
 * adapters, but laid out for the 128-byte 29xx request ring:
 *   - vp_index widened to __le16 (bits [8:0] meaningful, see
 *     CMD_EXT_VP_INDEX_MASK).
 *   - reserved area expanded to 32 bytes between exchange_address and
 *     rx_byte_count.
 *   - inline DSD capacity grown from 2 to 5.
 * Header through 'tx_dseg_count' (offset 14) and the control_flags /
 * exchange_address fields keep the same offsets as struct pt_ls4_request,
 * so common code can populate them via either type once IS_QLA29XX(ha) is
 * branched for the layout-divergent fields.
 */
#define NUM_PT_LS4_EXT_DSDS	5
struct pt_ls4_request_ext {
	uint8_t entry_type;
	uint8_t entry_count;
	uint8_t sys_define;
	uint8_t entry_status;
	uint32_t handle;
	__le16	status;
	__le16	nport_handle;
	__le16	tx_dseg_count;
	__le16	vp_index;	/* VP Index 9 bits; see CMD_EXT_VP_INDEX_MASK */
	__le16	timeout;
	__le16	control_flags;	/* CF_LS4_* (see struct pt_ls4_request) */
	__le16	rx_dseg_count;
	__le16	rsvd2;
	__le32	exchange_address;
	uint8_t rsvd3[32];
	__le32	rx_byte_count;
	__le32	tx_byte_count;
	struct dsd64 dsd[NUM_PT_LS4_EXT_DSDS];
};

/*
 * ISP queue - PUREX IOCB entry structure definition
 */
struct purex_entry_24xx_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	__le16	reserved1;
	__le16	vp_idx;			/* VP index 9 bits*/

	__le16	status_flags;
	__le16	nport_handle;

	__le16	frame_size;
	__le16	trunc_frame_size;

	__le32	rx_xchg_addr;

	uint8_t d_id[3];
	uint8_t r_ctl;

	uint8_t s_id[3];
	uint8_t cs_ctl;

	uint8_t f_ctl[3];
	uint8_t type;

	__le16	seq_cnt;
	uint8_t df_ctl;
	uint8_t seq_id;

	__le16	rx_id;
	__le16	ox_id;
	__le32	param;

	uint8_t els_frame_payload[84];
};

/*
 * ISP queue - ELS Pass-Through entry structure definition.
 * ELS_EXT_EST_SOFI*: 4-bit sof_type for extended IOCBs (qla_fw.h EST_SOFI*
 * is for els_entry_24xx byte layout).
 */
#define ELS_EXT_EST_SOFI3	(1 << 1)
#define ELS_EXT_EST_SOFI2	(3 << 3)

struct els_entry_24xx_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System Defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	__le16	comp_status;		/* response only */
	__le16	nport_handle;

	__le16	tx_dsd_count;

	__le16	vp_index_sof;		/* bits [8:0]=VP index, [15:12]=SOF type */

	__le32	rx_xchg_address;	/* Receive exchange address. */
	__le16	rx_dsd_count;

	uint8_t opcode;
	uint8_t reserved_2;

	uint8_t d_id[3];
	uint8_t s_id[3];

	__le16	control_flags;		/* Control flags. */

	union {
		struct {
			__le32	 rx_byte_count;
			__le32	 tx_byte_count;

			__le64	 tx_address __packed;	/* DSD 0 address. */
			__le32	 tx_len;		/* DSD 0 length. */

			__le64	 rx_address __packed;	/* DSD 1 address. */
			__le32	 rx_len;		/* DSD 1 length. */
		};
		struct {
			__le32	total_byte_count;
			__le32	error_subcode_1;
			__le32	error_subcode_2;
			__le32	error_subcode_3;
			uint8_t reserved_3[16];
		};
	};
	uint8_t reserved_4[64];
};

struct els_sts_entry_24xx_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System Defined. */
	uint8_t entry_status;		/* Entry Status. */

	__le32	handle;		/* System handle. */

	__le16	comp_status;

	__le16	nport_handle;		/* N_PORT handle. */

	__le16	reserved_1;

	__le16	vp_index_sof;		/* bits [8:0]=VP index, [15:12]=SOF type */

	__le32	rx_xchg_address;	/* Receive exchange address. */
	__le16	reserved_2;

	uint8_t opcode;
	uint8_t reserved_3;

	uint8_t d_id[3];
	uint8_t s_id[3];

	__le16	control_flags;		/* Control flags. */
	__le32	total_byte_count;
	__le32	error_subcode_1;
	__le32	error_subcode_2;
	__le32	error_subcode_3;

	uint8_t	reserved_4[80];
};

struct logio_entry_24xx_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	__le16	comp_status;		/* Completion status. */

	__le16	nport_handle;		/* N_PORT handle. */

	__le16	control_flags;		/* Control flags. */

	__le16	vp_index;		/* VP Index 9bits*/

	uint8_t port_id[3];		/* PortID of destination port. */

	uint8_t rsp_size;		/* Response size in 32bit words. */

	__le32	io_parameter[11];	/* General I/O parameters. */
	uint8_t reserved_2[64];		/* Reserved*/
};

struct tsk_mgmt_entry_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	__le16	nport_handle;		/* N_PORT handle. */

	__le16	reserved_1;

	__le16	delay;			/* Activity delay in seconds. */

	__le16	timeout;		/* Command timeout. */

	struct scsi_lun lun;		/* FCP LUN (BE). */

	__le32	control_flags;		/* Control Flags. */

	__le16	vp_index;	/* VP Index 9bits */

	uint8_t reserved_3[98];
};

struct abort_entry_24xx_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;		/* Handle count. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	union {
		__le16 nport_handle;            /* N_PORT handle. */
		__le16 comp_status;             /* Completion status. */
	};

	__le16	options;		/* Options. */

	uint32_t handle_to_abort;	/* System handle to abort. */

	__le16	req_que_no;

	__le16	vp_index;		/* VP Index 9bits*/
	u8	reserved_2[4];
	union {
		struct {
			__le16 abts_rty_cnt;
			__le16 rsp_timeout;
		} drv;
		struct {
			u8	ba_rjt_vendorUnique;
			u8	ba_rjt_reasonCodeExpl;
			u8	ba_rjt_reasonCode;
			u8	reserved_3;
		} fw;
	};
	u8	reserved_4[100];
};

struct abts_entry_24xx_ext {
	uint8_t entry_type;
	uint8_t entry_count;
	uint8_t handle_count;
	uint8_t entry_status;

	__le32	handle;		/* type 0x55 only */

	__le16	comp_status;		/* type 0x55 only */
	__le16	nport_handle;		/* type 0x54 only */

	__le16	control_flags;		/* type 0x55 only */
	__le16	vp_idx_sof;		/* bits [8:0]=VP index, [15:12]=SOF type */

	__le32	rx_xch_addr;

	uint8_t d_id[3];
	uint8_t r_ctl;

	uint8_t s_id[3];
	uint8_t cs_ctl;

	uint8_t f_ctl[3];
	uint8_t type;

	__le16	seq_cnt;
	uint8_t df_ctl;
	uint8_t seq_id;

	__le16	rx_id;
	__le16	ox_id;

	__le32	param;

	union {
		struct {
			__le32	subcode3;
			__le32	rsvd;
			__le32	subcode1;
			__le32	subcode2;
		} error;
		struct {
			__le16	rsrvd1;
			uint8_t last_seq_id;
			uint8_t seq_id_valid;
			__le16	aborted_rx_id;
			__le16	aborted_ox_id;
			__le16	high_seq_cnt;
			__le16	low_seq_cnt;
		} ba_acc;
		struct {
			uint8_t vendor_unique;
			uint8_t explanation;
			uint8_t reason;
		} ba_rjt;
	} payload;

	__le32	rx_xch_addr_to_abort;
	uint8_t reserved_2[64];
} __packed;
/*
 * Virtual Port Control IOCB
 */
struct vp_ctrl_entry_24xx_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	__le16	vp_idx_failed;

	__le16	comp_status;		/* Completion status. */

	__le16	command;

	__le16	vp_count;

	uint8_t vp_idx_map[16];
	__le16	flags;
	__le16	id;
	uint16_t reserved_4;
	__le16	hopct;
	uint8_t reserved_5[88];
};

/*
 * Modify Virtual Port Configuration IOCB
 */
struct vp_config_entry_24xx_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t handle_count;
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */

	__le16	flags;

	__le16	comp_status;		/* Completion status. */

	uint8_t command;

	uint8_t vp_count;

	uint8_t vp_index1;
	uint8_t vp_index2;

	uint8_t options_idx1;
	uint8_t hard_address_idx1;
	uint16_t reserved_vp1;
	uint8_t port_name_idx1[WWN_SIZE];
	uint8_t node_name_idx1[WWN_SIZE];

	uint8_t options_idx2;
	uint8_t hard_address_idx2;
	uint16_t reserved_vp2;
	uint8_t port_name_idx2[WWN_SIZE];
	uint8_t node_name_idx2[WWN_SIZE];
	__le16	id;
	uint16_t reserved_4;
	__le16	hopct;
	uint8_t reserved_5[66];
};

struct vp_rpt_id_entry_24xx_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */
	__le32 resv1;
	uint8_t vp_acquired;
	uint8_t vp_setup;
	__le16	vp_idx_status;		/* bits [8:0]=VP index, [15:9]=VP status */

	uint8_t port_id[3];
	uint8_t format;
	union {
		struct vp_rpt_id_ext_f1 {
			/* format 1 fabric */
			uint8_t vpstat1_subcode; /* vp_status=1 subcode */
			uint8_t flags;

			__le16 fip_flags;
			uint8_t rsv2[12];

			uint8_t ls_rjt_vendor;
			uint8_t ls_rjt_explanation;
			uint8_t ls_rjt_reason;
			uint8_t rsv3;
			__le16	rsv8;
			__le16	flogi_acc_payload_size;	/* bits [8:0] meaningful */
			uint8_t port_name[8];
			uint8_t node_name[8];
			__le16 bbcr;
			uint8_t reserved_5[6];
		} f1;
		struct vp_rpt_id_ext_f2 { /* format 2: N2N direct connect */
			uint8_t vpstat1_subcode;
			uint8_t flags;
			__le16 fip_flags;
			uint8_t rsv2[12];

			uint8_t ls_rjt_vendor;
			uint8_t ls_rjt_explanation;
			uint8_t ls_rjt_reason;
			uint8_t rsv3[5];

			uint8_t port_name[8];
			uint8_t node_name[8];
			__le16 bbcr;
			uint8_t reserved_5[2];
			uint8_t remote_nport_id[4];
		} f2;
	} u;
	uint8_t reserved_end[64];
};

/*
 * ISP queue - 64-Bit addressing, continuation entry structure definition
 * for the 29xx extended (128-byte) IOCB ring.  Mirrors cont_a64_entry_t
 * but carries 10 DSDs per entry instead of 5.
 */
#define NUM_CONT1_DSDS	10
struct cont_a64_entry_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */
	uint32_t reserved;
	struct dsd64 dsd[NUM_CONT1_DSDS];
};

/*
 * 29xx extended Command Type FC-NVMe IOCB (128 bytes).
 *
 * The header layout up through 'byte_count' (offset 48) is identical to the
 * 64-byte struct cmd_nvme used by 24xx-class adapters, so common code can
 * populate those fields via either type.  Fields beyond 'byte_count' diverge:
 * 29xx adds control_flags_2/vp_index/first_burst_rx_id/io_tag/..., drops
 * port_id[3]+vp_index(byte), and carries NUM_NVME_DSDS inline DSDs.
 */
#define NUM_NVME_DSDS	4
struct cmd_nvme_ext {
	uint8_t entry_type;		/* Entry type. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */

	uint32_t handle;		/* System handle. */
	__le16	nport_handle;		/* N_PORT handle. */
	__le16	timeout;		/* Command timeout. */

	__le16	dseg_count;		/* Data segment count. */
	__le16	nvme_rsp_dsd_len;	/* NVMe RSP DSD length */

	uint64_t rsvd;

	__le16	control_flags;		/* Control Flags (see struct cmd_nvme) */
	__le16	nvme_cmnd_dseg_len;			/* Data segment length. */

	__le64	 nvme_cmnd_dseg_address __packed;	/* Data segment address. */

	__le64	 nvme_rsp_dseg_address __packed;	/* Data segment address. */

	__le32	byte_count;		/* Total byte count. */

	__le16	control_flags_2;
	/*
	 * vp_index layout matches the other 29xx extended IOCBs: only bits
	 * [8:0] are meaningful (see CMD_EXT_VP_INDEX_MASK).
	 */
	__le16	vp_index;
	__le32	first_burst_rx_id;
	__le16	io_tag;
	uint8_t vl_n_fctl;	/* VL(7:4) | RSVD(3:2) | F_CTL[17](1) | RSVD(0) */
	uint8_t prtag_csctl;	/* Priority Tag or CS_CTL */
	__le32	src_vm_id;	/* Source VM ID */
	uint8_t reserved_2[16];

	struct dsd64 nvme_dsd[NUM_NVME_DSDS];
};

#endif
