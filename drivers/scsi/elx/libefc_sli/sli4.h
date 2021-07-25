/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 *
 */

/*
 * All common SLI-4 structures and function prototypes.
 */

#ifndef _SLI4_H
#define _SLI4_H

#include <linux/pci.h>
#include <linux/delay.h>
#include "scsi/fc/fc_els.h"
#include "scsi/fc/fc_fs.h"
#include "../include/efc_common.h"

/*************************************************************************
 * Common SLI-4 register offsets and field definitions
 */

/* SLI_INTF - SLI Interface Definition Register */
#define SLI4_INTF_REG			0x0058
enum sli4_intf {
	SLI4_INTF_REV_SHIFT		= 4,
	SLI4_INTF_REV_MASK		= 0xf0,

	SLI4_INTF_REV_S3		= 0x30,
	SLI4_INTF_REV_S4		= 0x40,

	SLI4_INTF_FAMILY_SHIFT		= 8,
	SLI4_INTF_FAMILY_MASK		= 0x0f00,

	SLI4_FAMILY_CHECK_ASIC_TYPE	= 0x0f00,

	SLI4_INTF_IF_TYPE_SHIFT		= 12,
	SLI4_INTF_IF_TYPE_MASK		= 0xf000,

	SLI4_INTF_IF_TYPE_2		= 0x2000,
	SLI4_INTF_IF_TYPE_6		= 0x6000,

	SLI4_INTF_VALID_SHIFT		= 29,
	SLI4_INTF_VALID_MASK		= 0xe0000000,

	SLI4_INTF_VALID_VALUE		= 0xc0000000,
};

/* ASIC_ID - SLI ASIC Type and Revision Register */
#define SLI4_ASIC_ID_REG	0x009c
enum sli4_asic {
	SLI4_ASIC_GEN_SHIFT	= 8,
	SLI4_ASIC_GEN_MASK	= 0xff00,
	SLI4_ASIC_GEN_5		= 0x0b00,
	SLI4_ASIC_GEN_6		= 0x0c00,
	SLI4_ASIC_GEN_7		= 0x0d00,
};

enum sli4_acic_revisions {
	SLI4_ASIC_REV_A0	= 0x00,
	SLI4_ASIC_REV_A1	= 0x01,
	SLI4_ASIC_REV_A2	= 0x02,
	SLI4_ASIC_REV_A3	= 0x03,
	SLI4_ASIC_REV_B0	= 0x10,
	SLI4_ASIC_REV_B1	= 0x11,
	SLI4_ASIC_REV_B2	= 0x12,
	SLI4_ASIC_REV_C0	= 0x20,
	SLI4_ASIC_REV_C1	= 0x21,
	SLI4_ASIC_REV_C2	= 0x22,
	SLI4_ASIC_REV_D0	= 0x30,
};

struct sli4_asic_entry_t {
	u32 rev_id;
	u32 family;
};

/* BMBX - Bootstrap Mailbox Register */
#define SLI4_BMBX_REG		0x0160
enum sli4_bmbx {
	SLI4_BMBX_MASK_HI	= 0x3,
	SLI4_BMBX_MASK_LO	= 0xf,
	SLI4_BMBX_RDY		= 1 << 0,
	SLI4_BMBX_HI		= 1 << 1,
	SLI4_BMBX_SIZE		= 256,
};

static inline u32
sli_bmbx_write_hi(u64 addr) {
	u32 val;

	val = upper_32_bits(addr) & ~SLI4_BMBX_MASK_HI;
	val |= SLI4_BMBX_HI;

	return val;
}

static inline u32
sli_bmbx_write_lo(u64 addr) {
	u32 val;

	val = (upper_32_bits(addr) & SLI4_BMBX_MASK_HI) << 30;
	val |= ((addr) & ~SLI4_BMBX_MASK_LO) >> 2;

	return val;
}

/* SLIPORT_CONTROL - SLI Port Control Register */
#define SLI4_PORT_CTRL_REG	0x0408
enum sli4_port_ctrl {
	SLI4_PORT_CTRL_IP	= 1u << 27,
	SLI4_PORT_CTRL_IDIS	= 1u << 22,
	SLI4_PORT_CTRL_FDD	= 1u << 31,
};

/* SLI4_SLIPORT_ERROR - SLI Port Error Register */
#define SLI4_PORT_ERROR1	0x040c
#define SLI4_PORT_ERROR2	0x0410

/* EQCQ_DOORBELL - EQ and CQ Doorbell Register */
#define SLI4_EQCQ_DB_REG	0x120
enum sli4_eqcq_e {
	SLI4_EQ_ID_LO_MASK	= 0x01ff,

	SLI4_CQ_ID_LO_MASK	= 0x03ff,

	SLI4_EQCQ_CI_EQ		= 0x0200,

	SLI4_EQCQ_QT_EQ		= 0x00000400,
	SLI4_EQCQ_QT_CQ		= 0x00000000,

	SLI4_EQCQ_ID_HI_SHIFT	= 11,
	SLI4_EQCQ_ID_HI_MASK	= 0xf800,

	SLI4_EQCQ_NUM_SHIFT	= 16,
	SLI4_EQCQ_NUM_MASK	= 0x1fff0000,

	SLI4_EQCQ_ARM		= 0x20000000,
	SLI4_EQCQ_UNARM		= 0x00000000,
};

static inline u32
sli_format_eq_db_data(u16 num_popped, u16 id, u32 arm) {
	u32 reg;

	reg = (id & SLI4_EQ_ID_LO_MASK) | SLI4_EQCQ_QT_EQ;
	reg |= (((id) >> 9) << SLI4_EQCQ_ID_HI_SHIFT) & SLI4_EQCQ_ID_HI_MASK;
	reg |= ((num_popped) << SLI4_EQCQ_NUM_SHIFT) & SLI4_EQCQ_NUM_MASK;
	reg |= arm | SLI4_EQCQ_CI_EQ;

	return reg;
}

static inline u32
sli_format_cq_db_data(u16 num_popped, u16 id, u32 arm) {
	u32 reg;

	reg = ((id) & SLI4_CQ_ID_LO_MASK) | SLI4_EQCQ_QT_CQ;
	reg |= (((id) >> 10) << SLI4_EQCQ_ID_HI_SHIFT) & SLI4_EQCQ_ID_HI_MASK;
	reg |= ((num_popped) << SLI4_EQCQ_NUM_SHIFT) & SLI4_EQCQ_NUM_MASK;
	reg |= arm;

	return reg;
}

/* EQ_DOORBELL - EQ Doorbell Register for IF_TYPE = 6*/
#define SLI4_IF6_EQ_DB_REG	0x120
enum sli4_eq_e {
	SLI4_IF6_EQ_ID_MASK	= 0x0fff,

	SLI4_IF6_EQ_NUM_SHIFT	= 16,
	SLI4_IF6_EQ_NUM_MASK	= 0x1fff0000,
};

static inline u32
sli_format_if6_eq_db_data(u16 num_popped, u16 id, u32 arm) {
	u32 reg;

	reg = id & SLI4_IF6_EQ_ID_MASK;
	reg |= (num_popped << SLI4_IF6_EQ_NUM_SHIFT) & SLI4_IF6_EQ_NUM_MASK;
	reg |= arm;

	return reg;
}

/* CQ_DOORBELL - CQ Doorbell Register for IF_TYPE = 6 */
#define SLI4_IF6_CQ_DB_REG	0xc0
enum sli4_cq_e {
	SLI4_IF6_CQ_ID_MASK	= 0xffff,

	SLI4_IF6_CQ_NUM_SHIFT	= 16,
	SLI4_IF6_CQ_NUM_MASK	= 0x1fff0000,
};

static inline u32
sli_format_if6_cq_db_data(u16 num_popped, u16 id, u32 arm) {
	u32 reg;

	reg = id & SLI4_IF6_CQ_ID_MASK;
	reg |= ((num_popped) << SLI4_IF6_CQ_NUM_SHIFT) & SLI4_IF6_CQ_NUM_MASK;
	reg |= arm;

	return reg;
}

/* MQ_DOORBELL - MQ Doorbell Register */
#define SLI4_MQ_DB_REG		0x0140
#define SLI4_IF6_MQ_DB_REG	0x0160
enum sli4_mq_e {
	SLI4_MQ_ID_MASK		= 0xffff,

	SLI4_MQ_NUM_SHIFT	= 16,
	SLI4_MQ_NUM_MASK	= 0x3fff0000,
};

static inline u32
sli_format_mq_db_data(u16 id) {
	u32 reg;

	reg = id & SLI4_MQ_ID_MASK;
	reg |= (1 << SLI4_MQ_NUM_SHIFT) & SLI4_MQ_NUM_MASK;

	return reg;
}

/* RQ_DOORBELL - RQ Doorbell Register */
#define SLI4_RQ_DB_REG		0x0a0
#define SLI4_IF6_RQ_DB_REG	0x0080
enum sli4_rq_e {
	SLI4_RQ_DB_ID_MASK	= 0xffff,

	SLI4_RQ_DB_NUM_SHIFT	= 16,
	SLI4_RQ_DB_NUM_MASK	= 0x3fff0000,
};

static inline u32
sli_format_rq_db_data(u16 id) {
	u32 reg;

	reg = id & SLI4_RQ_DB_ID_MASK;
	reg |= (1 << SLI4_RQ_DB_NUM_SHIFT) & SLI4_RQ_DB_NUM_MASK;

	return reg;
}

/* WQ_DOORBELL - WQ Doorbell Register */
#define SLI4_IO_WQ_DB_REG	0x040
#define SLI4_IF6_WQ_DB_REG	0x040
enum sli4_wq_e {
	SLI4_WQ_ID_MASK		= 0xffff,

	SLI4_WQ_IDX_SHIFT	= 16,
	SLI4_WQ_IDX_MASK	= 0xff0000,

	SLI4_WQ_NUM_SHIFT	= 24,
	SLI4_WQ_NUM_MASK	= 0x0ff00000,
};

static inline u32
sli_format_wq_db_data(u16 id) {
	u32 reg;

	reg = id & SLI4_WQ_ID_MASK;
	reg |= (1 << SLI4_WQ_NUM_SHIFT) & SLI4_WQ_NUM_MASK;

	return reg;
}

/* SLIPORT_STATUS - SLI Port Status Register */
#define SLI4_PORT_STATUS_REGOFF	0x0404
enum sli4_port_status {
	SLI4_PORT_STATUS_FDP	= 1u << 21,
	SLI4_PORT_STATUS_RDY	= 1u << 23,
	SLI4_PORT_STATUS_RN	= 1u << 24,
	SLI4_PORT_STATUS_DIP	= 1u << 25,
	SLI4_PORT_STATUS_OTI	= 1u << 29,
	SLI4_PORT_STATUS_ERR	= 1u << 31,
};

#define SLI4_PHYDEV_CTRL_REG	0x0414
#define SLI4_PHYDEV_CTRL_FRST	(1 << 1)
#define SLI4_PHYDEV_CTRL_DD	(1 << 2)

/* Register name enums */
enum sli4_regname_en {
	SLI4_REG_BMBX,
	SLI4_REG_EQ_DOORBELL,
	SLI4_REG_CQ_DOORBELL,
	SLI4_REG_RQ_DOORBELL,
	SLI4_REG_IO_WQ_DOORBELL,
	SLI4_REG_MQ_DOORBELL,
	SLI4_REG_PHYSDEV_CONTROL,
	SLI4_REG_PORT_CONTROL,
	SLI4_REG_PORT_ERROR1,
	SLI4_REG_PORT_ERROR2,
	SLI4_REG_PORT_SEMAPHORE,
	SLI4_REG_PORT_STATUS,
	SLI4_REG_UNKWOWN			/* must be last */
};

struct sli4_reg {
	u32	rset;
	u32	off;
};

struct sli4_dmaaddr {
	__le32 low;
	__le32 high;
};

/*
 * a 3-word Buffer Descriptor Entry with
 * address 1st 2 words, length last word
 */
struct sli4_bufptr {
	struct sli4_dmaaddr addr;
	__le32 length;
};

/* Buffer Descriptor Entry (BDE) */
enum sli4_bde_e {
	SLI4_BDE_LEN_MASK	= 0x00ffffff,
	SLI4_BDE_TYPE_MASK	= 0xff000000,
};

struct sli4_bde {
	__le32		bde_type_buflen;
	union {
		struct sli4_dmaaddr data;
		struct {
			__le32	offset;
			__le32	rsvd2;
		} imm;
		struct sli4_dmaaddr blp;
	} u;
};

/* Buffer Descriptors */
enum sli4_bde_type {
	SLI4_BDE_TYPE_SHIFT	= 24,
	SLI4_BDE_TYPE_64	= 0x00,	/* Generic 64-bit data */
	SLI4_BDE_TYPE_IMM	= 0x01,	/* Immediate data */
	SLI4_BDE_TYPE_BLP	= 0x40,	/* Buffer List Pointer */
};

#define SLI4_BDE_TYPE_VAL(type) \
	(SLI4_BDE_TYPE_##type << SLI4_BDE_TYPE_SHIFT)

/* Scatter-Gather Entry (SGE) */
#define SLI4_SGE_MAX_RESERVED		3

enum sli4_sge_type {
	/* DW2 */
	SLI4_SGE_DATA_OFFSET_MASK	= 0x07ffffff,
	/*DW2W1*/
	SLI4_SGE_TYPE_SHIFT		= 27,
	SLI4_SGE_TYPE_MASK		= 0x78000000,
	/*SGE Types*/
	SLI4_SGE_TYPE_DATA		= 0x00,
	SLI4_SGE_TYPE_DIF		= 0x04,	/* Data Integrity Field */
	SLI4_SGE_TYPE_LSP		= 0x05,	/* List Segment Pointer */
	SLI4_SGE_TYPE_PEDIF		= 0x06,	/* Post Encryption Engine DIF */
	SLI4_SGE_TYPE_PESEED		= 0x07,	/* Post Encryption DIF Seed */
	SLI4_SGE_TYPE_DISEED		= 0x08,	/* DIF Seed */
	SLI4_SGE_TYPE_ENC		= 0x09,	/* Encryption */
	SLI4_SGE_TYPE_ATM		= 0x0a,	/* DIF Application Tag Mask */
	SLI4_SGE_TYPE_SKIP		= 0x0c,	/* SKIP */

	SLI4_SGE_LAST			= 1u << 31,
};

struct sli4_sge {
	__le32		buffer_address_high;
	__le32		buffer_address_low;
	__le32		dw2_flags;
	__le32		buffer_length;
};

/* T10 DIF Scatter-Gather Entry (SGE) */
struct sli4_dif_sge {
	__le32		buffer_address_high;
	__le32		buffer_address_low;
	__le32		dw2_flags;
	__le32		rsvd12;
};

/* Data Integrity Seed (DISEED) SGE */
enum sli4_diseed_sge_flags {
	/* DW2W1 */
	SLI4_DISEED_SGE_HS		= 1 << 2,
	SLI4_DISEED_SGE_WS		= 1 << 3,
	SLI4_DISEED_SGE_IC		= 1 << 4,
	SLI4_DISEED_SGE_ICS		= 1 << 5,
	SLI4_DISEED_SGE_ATRT		= 1 << 6,
	SLI4_DISEED_SGE_AT		= 1 << 7,
	SLI4_DISEED_SGE_FAT		= 1 << 8,
	SLI4_DISEED_SGE_NA		= 1 << 9,
	SLI4_DISEED_SGE_HI		= 1 << 10,

	/* DW3W1 */
	SLI4_DISEED_SGE_BS_MASK		= 0x0007,
	SLI4_DISEED_SGE_AI		= 1 << 3,
	SLI4_DISEED_SGE_ME		= 1 << 4,
	SLI4_DISEED_SGE_RE		= 1 << 5,
	SLI4_DISEED_SGE_CE		= 1 << 6,
	SLI4_DISEED_SGE_NR		= 1 << 7,

	SLI4_DISEED_SGE_OP_RX_SHIFT	= 8,
	SLI4_DISEED_SGE_OP_RX_MASK	= 0x0f00,
	SLI4_DISEED_SGE_OP_TX_SHIFT	= 12,
	SLI4_DISEED_SGE_OP_TX_MASK	= 0xf000,
};

/* Opcode values */
enum sli4_diseed_sge_opcodes {
	SLI4_DISEED_SGE_OP_IN_NODIF_OUT_CRC,
	SLI4_DISEED_SGE_OP_IN_CRC_OUT_NODIF,
	SLI4_DISEED_SGE_OP_IN_NODIF_OUT_CSUM,
	SLI4_DISEED_SGE_OP_IN_CSUM_OUT_NODIF,
	SLI4_DISEED_SGE_OP_IN_CRC_OUT_CRC,
	SLI4_DISEED_SGE_OP_IN_CSUM_OUT_CSUM,
	SLI4_DISEED_SGE_OP_IN_CRC_OUT_CSUM,
	SLI4_DISEED_SGE_OP_IN_CSUM_OUT_CRC,
	SLI4_DISEED_SGE_OP_IN_RAW_OUT_RAW,
};

#define SLI4_DISEED_SGE_OP_RX_VALUE(stype) \
	(SLI4_DISEED_SGE_OP_##stype << SLI4_DISEED_SGE_OP_RX_SHIFT)
#define SLI4_DISEED_SGE_OP_TX_VALUE(stype) \
	(SLI4_DISEED_SGE_OP_##stype << SLI4_DISEED_SGE_OP_TX_SHIFT)

struct sli4_diseed_sge {
	__le32		ref_tag_cmp;
	__le32		ref_tag_repl;
	__le16		app_tag_repl;
	__le16		dw2w1_flags;
	__le16		app_tag_cmp;
	__le16		dw3w1_flags;
};

/* List Segment Pointer Scatter-Gather Entry (SGE) */
#define SLI4_LSP_SGE_SEGLEN	0x00ffffff

struct sli4_lsp_sge {
	__le32		buffer_address_high;
	__le32		buffer_address_low;
	__le32		dw2_flags;
	__le32		dw3_seglen;
};

enum sli4_eqe_e {
	SLI4_EQE_VALID	= 1,
	SLI4_EQE_MJCODE	= 0xe,
	SLI4_EQE_MNCODE	= 0xfff0,
};

struct sli4_eqe {
	__le16		dw0w0_flags;
	__le16		resource_id;
};

#define SLI4_MAJOR_CODE_STANDARD	0
#define SLI4_MAJOR_CODE_SENTINEL	1

/* Sentinel EQE indicating the EQ is full */
#define SLI4_EQE_STATUS_EQ_FULL		2

enum sli4_mcqe_e {
	SLI4_MCQE_CONSUMED	= 1u << 27,
	SLI4_MCQE_COMPLETED	= 1u << 28,
	SLI4_MCQE_AE		= 1u << 30,
	SLI4_MCQE_VALID		= 1u << 31,
};

/* Entry was consumed but not completed */
#define SLI4_MCQE_STATUS_NOT_COMPLETED	-2

struct sli4_mcqe {
	__le16		completion_status;
	__le16		extended_status;
	__le32		mqe_tag_low;
	__le32		mqe_tag_high;
	__le32		dw3_flags;
};

enum sli4_acqe_e {
	SLI4_ACQE_AE	= 1 << 6, /* async event - this is an ACQE */
	SLI4_ACQE_VAL	= 1 << 7, /* valid - contents of CQE are valid */
};

struct sli4_acqe {
	__le32		event_data[3];
	u8		rsvd12;
	u8		event_code;
	u8		event_type;
	u8		ae_val;
};

enum sli4_acqe_event_code {
	SLI4_ACQE_EVENT_CODE_LINK_STATE		= 0x01,
	SLI4_ACQE_EVENT_CODE_FIP		= 0x02,
	SLI4_ACQE_EVENT_CODE_DCBX		= 0x03,
	SLI4_ACQE_EVENT_CODE_ISCSI		= 0x04,
	SLI4_ACQE_EVENT_CODE_GRP_5		= 0x05,
	SLI4_ACQE_EVENT_CODE_FC_LINK_EVENT	= 0x10,
	SLI4_ACQE_EVENT_CODE_SLI_PORT_EVENT	= 0x11,
	SLI4_ACQE_EVENT_CODE_VF_EVENT		= 0x12,
	SLI4_ACQE_EVENT_CODE_MR_EVENT		= 0x13,
};

enum sli4_qtype {
	SLI4_QTYPE_EQ,
	SLI4_QTYPE_CQ,
	SLI4_QTYPE_MQ,
	SLI4_QTYPE_WQ,
	SLI4_QTYPE_RQ,
	SLI4_QTYPE_MAX,			/* must be last */
};

#define SLI4_USER_MQ_COUNT	1
#define SLI4_MAX_CQ_SET_COUNT	16
#define SLI4_MAX_RQ_SET_COUNT	16

enum sli4_qentry {
	SLI4_QENTRY_ASYNC,
	SLI4_QENTRY_MQ,
	SLI4_QENTRY_RQ,
	SLI4_QENTRY_WQ,
	SLI4_QENTRY_WQ_RELEASE,
	SLI4_QENTRY_OPT_WRITE_CMD,
	SLI4_QENTRY_OPT_WRITE_DATA,
	SLI4_QENTRY_XABT,
	SLI4_QENTRY_MAX			/* must be last */
};

enum sli4_queue_flags {
	SLI4_QUEUE_FLAG_MQ	= 1 << 0,	/* CQ has MQ/Async completion */
	SLI4_QUEUE_FLAG_HDR	= 1 << 1,	/* RQ for packet headers */
	SLI4_QUEUE_FLAG_RQBATCH	= 1 << 2,	/* RQ index increment by 8 */
};

/* Generic Command Request header */
enum sli4_cmd_version {
	CMD_V0,
	CMD_V1,
	CMD_V2,
};

struct sli4_rqst_hdr {
	u8		opcode;
	u8		subsystem;
	__le16		rsvd2;
	__le32		timeout;
	__le32		request_length;
	__le32		dw3_version;
};

/* Generic Command Response header */
struct sli4_rsp_hdr {
	u8		opcode;
	u8		subsystem;
	__le16		rsvd2;
	u8		status;
	u8		additional_status;
	__le16		rsvd6;
	__le32		response_length;
	__le32		actual_response_length;
};

#define SLI4_QUEUE_RQ_BATCH	8

#define SZ_DMAADDR		sizeof(struct sli4_dmaaddr)
#define SLI4_RQST_CMDSZ(stype)	sizeof(struct sli4_rqst_##stype)

#define SLI4_RQST_PYLD_LEN(stype) \
		cpu_to_le32(sizeof(struct sli4_rqst_##stype) - \
			sizeof(struct sli4_rqst_hdr))

#define SLI4_RQST_PYLD_LEN_VAR(stype, varpyld) \
		cpu_to_le32((sizeof(struct sli4_rqst_##stype) + \
			varpyld) - sizeof(struct sli4_rqst_hdr))

#define SLI4_CFG_PYLD_LENGTH(stype) \
		max(sizeof(struct sli4_rqst_##stype), \
		sizeof(struct sli4_rsp_##stype))

enum sli4_create_cqv2_e {
	/* DW5_flags values*/
	SLI4_CREATE_CQV2_CLSWM_MASK	= 0x00003000,
	SLI4_CREATE_CQV2_NODELAY	= 0x00004000,
	SLI4_CREATE_CQV2_AUTOVALID	= 0x00008000,
	SLI4_CREATE_CQV2_CQECNT_MASK	= 0x18000000,
	SLI4_CREATE_CQV2_VALID		= 0x20000000,
	SLI4_CREATE_CQV2_EVT		= 0x80000000,
	/* DW6W1_flags values*/
	SLI4_CREATE_CQV2_ARM		= 0x8000,
};

struct sli4_rqst_cmn_create_cq_v2 {
	struct sli4_rqst_hdr	hdr;
	__le16			num_pages;
	u8			page_size;
	u8			rsvd19;
	__le32			dw5_flags;
	__le16			eq_id;
	__le16			dw6w1_arm;
	__le16			cqe_count;
	__le16			rsvd30;
	__le32			rsvd32;
	struct sli4_dmaaddr	page_phys_addr[0];
};

enum sli4_create_cqset_e {
	/* DW5_flags values*/
	SLI4_CREATE_CQSETV0_CLSWM_MASK	= 0x00003000,
	SLI4_CREATE_CQSETV0_NODELAY	= 0x00004000,
	SLI4_CREATE_CQSETV0_AUTOVALID	= 0x00008000,
	SLI4_CREATE_CQSETV0_CQECNT_MASK	= 0x18000000,
	SLI4_CREATE_CQSETV0_VALID	= 0x20000000,
	SLI4_CREATE_CQSETV0_EVT		= 0x80000000,
	/* DW5W1_flags values */
	SLI4_CREATE_CQSETV0_CQE_COUNT	= 0x7fff,
	SLI4_CREATE_CQSETV0_ARM		= 0x8000,
};

struct sli4_rqst_cmn_create_cq_set_v0 {
	struct sli4_rqst_hdr	hdr;
	__le16			num_pages;
	u8			page_size;
	u8			rsvd19;
	__le32			dw5_flags;
	__le16			num_cq_req;
	__le16			dw6w1_flags;
	__le16			eq_id[16];
	struct sli4_dmaaddr	page_phys_addr[0];
};

/* CQE count */
enum sli4_cq_cnt {
	SLI4_CQ_CNT_256,
	SLI4_CQ_CNT_512,
	SLI4_CQ_CNT_1024,
	SLI4_CQ_CNT_LARGE,
};

#define SLI4_CQ_CNT_SHIFT	27
#define SLI4_CQ_CNT_VAL(type)	(SLI4_CQ_CNT_##type << SLI4_CQ_CNT_SHIFT)

#define SLI4_CQE_BYTES		(4 * sizeof(u32))

#define SLI4_CREATE_CQV2_MAX_PAGES	8

/* Generic Common Create EQ/CQ/MQ/WQ/RQ Queue completion */
struct sli4_rsp_cmn_create_queue {
	struct sli4_rsp_hdr	hdr;
	__le16	q_id;
	u8	rsvd18;
	u8	ulp;
	__le32	db_offset;
	__le16	db_rs;
	__le16	db_fmt;
};

struct sli4_rsp_cmn_create_queue_set {
	struct sli4_rsp_hdr	hdr;
	__le16	q_id;
	__le16	num_q_allocated;
};

/* Common Destroy Queue */
struct sli4_rqst_cmn_destroy_q {
	struct sli4_rqst_hdr	hdr;
	__le16	q_id;
	__le16	rsvd;
};

struct sli4_rsp_cmn_destroy_q {
	struct sli4_rsp_hdr	hdr;
};

/* Modify the delay multiplier for EQs */
struct sli4_eqdelay_rec {
	__le32  eq_id;
	__le32  phase;
	__le32  delay_multiplier;
};

struct sli4_rqst_cmn_modify_eq_delay {
	struct sli4_rqst_hdr	hdr;
	__le32			num_eq;
	struct sli4_eqdelay_rec eq_delay_record[8];
};

struct sli4_rsp_cmn_modify_eq_delay {
	struct sli4_rsp_hdr	hdr;
};

enum sli4_create_cq_e {
	/* DW5 */
	SLI4_CREATE_EQ_AUTOVALID		= 1u << 28,
	SLI4_CREATE_EQ_VALID			= 1u << 29,
	SLI4_CREATE_EQ_EQESZ			= 1u << 31,
	/* DW6 */
	SLI4_CREATE_EQ_COUNT			= 7 << 26,
	SLI4_CREATE_EQ_ARM			= 1u << 31,
	/* DW7 */
	SLI4_CREATE_EQ_DELAYMULTI_SHIFT		= 13,
	SLI4_CREATE_EQ_DELAYMULTI_MASK		= 0x007fe000,
	SLI4_CREATE_EQ_DELAYMULTI		= 0x00040000,
};

struct sli4_rqst_cmn_create_eq {
	struct sli4_rqst_hdr	hdr;
	__le16			num_pages;
	__le16			rsvd18;
	__le32			dw5_flags;
	__le32			dw6_flags;
	__le32			dw7_delaymulti;
	__le32			rsvd32;
	struct sli4_dmaaddr	page_address[8];
};

struct sli4_rsp_cmn_create_eq {
	struct sli4_rsp_cmn_create_queue q_rsp;
};

/* EQ count */
enum sli4_eq_cnt {
	SLI4_EQ_CNT_256,
	SLI4_EQ_CNT_512,
	SLI4_EQ_CNT_1024,
	SLI4_EQ_CNT_2048,
	SLI4_EQ_CNT_4096 = 3,
};

#define SLI4_EQ_CNT_SHIFT	26
#define SLI4_EQ_CNT_VAL(type)	(SLI4_EQ_CNT_##type << SLI4_EQ_CNT_SHIFT)

#define SLI4_EQE_SIZE_4		0
#define SLI4_EQE_SIZE_16	1

/* Create a Mailbox Queue; accommodate v0 and v1 forms. */
enum sli4_create_mq_flags {
	/* DW6W1 */
	SLI4_CREATE_MQEXT_RINGSIZE	= 0xf,
	SLI4_CREATE_MQEXT_CQID_SHIFT	= 6,
	SLI4_CREATE_MQEXT_CQIDV0_MASK	= 0xffc0,
	/* DW7 */
	SLI4_CREATE_MQEXT_VAL		= 1u << 31,
	/* DW8 */
	SLI4_CREATE_MQEXT_ACQV		= 1u << 0,
	SLI4_CREATE_MQEXT_ASYNC_CQIDV0	= 0x7fe,
};

struct sli4_rqst_cmn_create_mq_ext {
	struct sli4_rqst_hdr	hdr;
	__le16			num_pages;
	__le16			cq_id_v1;
	__le32			async_event_bitmap;
	__le16			async_cq_id_v1;
	__le16			dw6w1_flags;
	__le32			dw7_val;
	__le32			dw8_flags;
	__le32			rsvd36;
	struct sli4_dmaaddr	page_phys_addr[0];
};

struct sli4_rsp_cmn_create_mq_ext {
	struct sli4_rsp_cmn_create_queue q_rsp;
};

enum sli4_mqe_size {
	SLI4_MQE_SIZE_16 = 0x05,
	SLI4_MQE_SIZE_32,
	SLI4_MQE_SIZE_64,
	SLI4_MQE_SIZE_128,
};

enum sli4_async_evt {
	SLI4_ASYNC_EVT_LINK_STATE	= 1 << 1,
	SLI4_ASYNC_EVT_FIP		= 1 << 2,
	SLI4_ASYNC_EVT_GRP5		= 1 << 5,
	SLI4_ASYNC_EVT_FC		= 1 << 16,
	SLI4_ASYNC_EVT_SLI_PORT		= 1 << 17,
};

#define	SLI4_ASYNC_EVT_FC_ALL \
		(SLI4_ASYNC_EVT_LINK_STATE	| \
		 SLI4_ASYNC_EVT_FIP		| \
		 SLI4_ASYNC_EVT_GRP5		| \
		 SLI4_ASYNC_EVT_FC		| \
		 SLI4_ASYNC_EVT_SLI_PORT)

/* Create a Completion Queue. */
struct sli4_rqst_cmn_create_cq_v0 {
	struct sli4_rqst_hdr	hdr;
	__le16			num_pages;
	__le16			rsvd18;
	__le32			dw5_flags;
	__le32			dw6_flags;
	__le32			rsvd28;
	__le32			rsvd32;
	struct sli4_dmaaddr	page_phys_addr[0];
};

enum sli4_create_rq_e {
	SLI4_RQ_CREATE_DUA		= 0x1,
	SLI4_RQ_CREATE_BQU		= 0x2,

	SLI4_RQE_SIZE			= 8,
	SLI4_RQE_SIZE_8			= 0x2,
	SLI4_RQE_SIZE_16		= 0x3,
	SLI4_RQE_SIZE_32		= 0x4,
	SLI4_RQE_SIZE_64		= 0x5,
	SLI4_RQE_SIZE_128		= 0x6,

	SLI4_RQ_PAGE_SIZE_4096		= 0x1,
	SLI4_RQ_PAGE_SIZE_8192		= 0x2,
	SLI4_RQ_PAGE_SIZE_16384		= 0x4,
	SLI4_RQ_PAGE_SIZE_32768		= 0x8,
	SLI4_RQ_PAGE_SIZE_64536		= 0x10,

	SLI4_RQ_CREATE_V0_MAX_PAGES	= 8,
	SLI4_RQ_CREATE_V0_MIN_BUF_SIZE	= 128,
	SLI4_RQ_CREATE_V0_MAX_BUF_SIZE	= 2048,
};

struct sli4_rqst_rq_create {
	struct sli4_rqst_hdr	hdr;
	__le16			num_pages;
	u8			dua_bqu_byte;
	u8			ulp;
	__le16			rsvd16;
	u8			rqe_count_byte;
	u8			rsvd19;
	__le32			rsvd20;
	__le16			buffer_size;
	__le16			cq_id;
	__le32			rsvd28;
	struct sli4_dmaaddr	page_phys_addr[SLI4_RQ_CREATE_V0_MAX_PAGES];
};

struct sli4_rsp_rq_create {
	struct sli4_rsp_cmn_create_queue rsp;
};

enum sli4_create_rqv1_e {
	SLI4_RQ_CREATE_V1_DNB		= 0x80,
	SLI4_RQ_CREATE_V1_MAX_PAGES	= 8,
	SLI4_RQ_CREATE_V1_MIN_BUF_SIZE	= 64,
	SLI4_RQ_CREATE_V1_MAX_BUF_SIZE	= 2048,
};

struct sli4_rqst_rq_create_v1 {
	struct sli4_rqst_hdr	hdr;
	__le16			num_pages;
	u8			rsvd14;
	u8			dim_dfd_dnb;
	u8			page_size;
	u8			rqe_size_byte;
	__le16			rqe_count;
	__le32			rsvd20;
	__le16			rsvd24;
	__le16			cq_id;
	__le32			buffer_size;
	struct sli4_dmaaddr	page_phys_addr[SLI4_RQ_CREATE_V1_MAX_PAGES];
};

struct sli4_rsp_rq_create_v1 {
	struct sli4_rsp_cmn_create_queue rsp;
};

#define	SLI4_RQCREATEV2_DNB	0x80

struct sli4_rqst_rq_create_v2 {
	struct sli4_rqst_hdr	hdr;
	__le16			num_pages;
	u8			rq_count;
	u8			dim_dfd_dnb;
	u8			page_size;
	u8			rqe_size_byte;
	__le16			rqe_count;
	__le16			hdr_buffer_size;
	__le16			payload_buffer_size;
	__le16			base_cq_id;
	__le16			rsvd26;
	__le32			rsvd42;
	struct sli4_dmaaddr	page_phys_addr[0];
};

struct sli4_rsp_rq_create_v2 {
	struct sli4_rsp_cmn_create_queue rsp;
};

#define SLI4_CQE_CODE_OFFSET	14

enum sli4_cqe_code {
	SLI4_CQE_CODE_WORK_REQUEST_COMPLETION = 0x01,
	SLI4_CQE_CODE_RELEASE_WQE,
	SLI4_CQE_CODE_RSVD,
	SLI4_CQE_CODE_RQ_ASYNC,
	SLI4_CQE_CODE_XRI_ABORTED,
	SLI4_CQE_CODE_RQ_COALESCING,
	SLI4_CQE_CODE_RQ_CONSUMPTION,
	SLI4_CQE_CODE_MEASUREMENT_REPORTING,
	SLI4_CQE_CODE_RQ_ASYNC_V1,
	SLI4_CQE_CODE_RQ_COALESCING_V1,
	SLI4_CQE_CODE_OPTIMIZED_WRITE_CMD,
	SLI4_CQE_CODE_OPTIMIZED_WRITE_DATA,
};

#define SLI4_WQ_CREATE_MAX_PAGES		8

struct sli4_rqst_wq_create {
	struct sli4_rqst_hdr	hdr;
	__le16			num_pages;
	__le16			cq_id;
	u8			page_size;
	u8			wqe_size_byte;
	__le16			wqe_count;
	__le32			rsvd;
	struct	sli4_dmaaddr	page_phys_addr[SLI4_WQ_CREATE_MAX_PAGES];
};

struct sli4_rsp_wq_create {
	struct sli4_rsp_cmn_create_queue rsp;
};

enum sli4_link_attention_flags {
	SLI4_LNK_ATTN_TYPE_LINK_UP		= 0x01,
	SLI4_LNK_ATTN_TYPE_LINK_DOWN		= 0x02,
	SLI4_LNK_ATTN_TYPE_NO_HARD_ALPA		= 0x03,

	SLI4_LNK_ATTN_P2P			= 0x01,
	SLI4_LNK_ATTN_FC_AL			= 0x02,
	SLI4_LNK_ATTN_INTERNAL_LOOPBACK		= 0x03,
	SLI4_LNK_ATTN_SERDES_LOOPBACK		= 0x04,
};

struct sli4_link_attention {
	u8		link_number;
	u8		attn_type;
	u8		topology;
	u8		port_speed;
	u8		port_fault;
	u8		shared_link_status;
	__le16		logical_link_speed;
	__le32		event_tag;
	u8		rsvd12;
	u8		event_code;
	u8		event_type;
	u8		flags;
};

enum sli4_link_event_type {
	SLI4_EVENT_LINK_ATTENTION		= 0x01,
	SLI4_EVENT_SHARED_LINK_ATTENTION	= 0x02,
};

enum sli4_wcqe_flags {
	SLI4_WCQE_XB = 0x10,
	SLI4_WCQE_QX = 0x80,
};

struct sli4_fc_wcqe {
	u8		hw_status;
	u8		status;
	__le16		request_tag;
	__le32		wqe_specific_1;
	__le32		wqe_specific_2;
	u8		rsvd12;
	u8		qx_byte;
	u8		code;
	u8		flags;
};

/* FC WQ consumed CQ queue entry */
struct sli4_fc_wqec {
	__le32		rsvd0;
	__le32		rsvd1;
	__le16		wqe_index;
	__le16		wq_id;
	__le16		rsvd12;
	u8		code;
	u8		vld_byte;
};

/* FC Completion Status Codes. */
enum sli4_wcqe_status {
	SLI4_FC_WCQE_STATUS_SUCCESS,
	SLI4_FC_WCQE_STATUS_FCP_RSP_FAILURE,
	SLI4_FC_WCQE_STATUS_REMOTE_STOP,
	SLI4_FC_WCQE_STATUS_LOCAL_REJECT,
	SLI4_FC_WCQE_STATUS_NPORT_RJT,
	SLI4_FC_WCQE_STATUS_FABRIC_RJT,
	SLI4_FC_WCQE_STATUS_NPORT_BSY,
	SLI4_FC_WCQE_STATUS_FABRIC_BSY,
	SLI4_FC_WCQE_STATUS_RSVD,
	SLI4_FC_WCQE_STATUS_LS_RJT,
	SLI4_FC_WCQE_STATUS_RX_BUF_OVERRUN,
	SLI4_FC_WCQE_STATUS_CMD_REJECT,
	SLI4_FC_WCQE_STATUS_FCP_TGT_LENCHECK,
	SLI4_FC_WCQE_STATUS_RSVD1,
	SLI4_FC_WCQE_STATUS_ELS_CMPLT_NO_AUTOREG,
	SLI4_FC_WCQE_STATUS_RSVD2,
	SLI4_FC_WCQE_STATUS_RQ_SUCCESS,
	SLI4_FC_WCQE_STATUS_RQ_BUF_LEN_EXCEEDED,
	SLI4_FC_WCQE_STATUS_RQ_INSUFF_BUF_NEEDED,
	SLI4_FC_WCQE_STATUS_RQ_INSUFF_FRM_DISC,
	SLI4_FC_WCQE_STATUS_RQ_DMA_FAILURE,
	SLI4_FC_WCQE_STATUS_FCP_RSP_TRUNCATE,
	SLI4_FC_WCQE_STATUS_DI_ERROR,
	SLI4_FC_WCQE_STATUS_BA_RJT,
	SLI4_FC_WCQE_STATUS_RQ_INSUFF_XRI_NEEDED,
	SLI4_FC_WCQE_STATUS_RQ_INSUFF_XRI_DISC,
	SLI4_FC_WCQE_STATUS_RX_ERROR_DETECT,
	SLI4_FC_WCQE_STATUS_RX_ABORT_REQUEST,

	/* driver generated status codes */
	SLI4_FC_WCQE_STATUS_DISPATCH_ERROR	= 0xfd,
	SLI4_FC_WCQE_STATUS_SHUTDOWN		= 0xfe,
	SLI4_FC_WCQE_STATUS_TARGET_WQE_TIMEOUT	= 0xff,
};

/* DI_ERROR Extended Status */
enum sli4_fc_di_error_status {
	SLI4_FC_DI_ERROR_GE			= 1 << 0,
	SLI4_FC_DI_ERROR_AE			= 1 << 1,
	SLI4_FC_DI_ERROR_RE			= 1 << 2,
	SLI4_FC_DI_ERROR_TDPV			= 1 << 3,
	SLI4_FC_DI_ERROR_UDB			= 1 << 4,
	SLI4_FC_DI_ERROR_EDIR			= 1 << 5,
};

/* WQE DIF field contents */
enum sli4_dif_fields {
	SLI4_DIF_DISABLED,
	SLI4_DIF_PASS_THROUGH,
	SLI4_DIF_STRIP,
	SLI4_DIF_INSERT,
};

/* Work Queue Entry (WQE) types */
enum sli4_wqe_types {
	SLI4_WQE_ABORT				= 0x0f,
	SLI4_WQE_ELS_REQUEST64			= 0x8a,
	SLI4_WQE_FCP_IBIDIR64			= 0xac,
	SLI4_WQE_FCP_IREAD64			= 0x9a,
	SLI4_WQE_FCP_IWRITE64			= 0x98,
	SLI4_WQE_FCP_ICMND64			= 0x9c,
	SLI4_WQE_FCP_TRECEIVE64			= 0xa1,
	SLI4_WQE_FCP_CONT_TRECEIVE64		= 0xe5,
	SLI4_WQE_FCP_TRSP64			= 0xa3,
	SLI4_WQE_FCP_TSEND64			= 0x9f,
	SLI4_WQE_GEN_REQUEST64			= 0xc2,
	SLI4_WQE_SEND_FRAME			= 0xe1,
	SLI4_WQE_XMIT_BCAST64			= 0x84,
	SLI4_WQE_XMIT_BLS_RSP			= 0x97,
	SLI4_WQE_ELS_RSP64			= 0x95,
	SLI4_WQE_XMIT_SEQUENCE64		= 0x82,
	SLI4_WQE_REQUEUE_XRI			= 0x93,
};

/* WQE command types */
enum sli4_wqe_cmds {
	SLI4_CMD_FCP_IREAD64_WQE		= 0x00,
	SLI4_CMD_FCP_ICMND64_WQE		= 0x00,
	SLI4_CMD_FCP_IWRITE64_WQE		= 0x01,
	SLI4_CMD_FCP_TRECEIVE64_WQE		= 0x02,
	SLI4_CMD_FCP_TRSP64_WQE			= 0x03,
	SLI4_CMD_FCP_TSEND64_WQE		= 0x07,
	SLI4_CMD_GEN_REQUEST64_WQE		= 0x08,
	SLI4_CMD_XMIT_BCAST64_WQE		= 0x08,
	SLI4_CMD_XMIT_BLS_RSP64_WQE		= 0x08,
	SLI4_CMD_ABORT_WQE			= 0x08,
	SLI4_CMD_XMIT_SEQUENCE64_WQE		= 0x08,
	SLI4_CMD_REQUEUE_XRI_WQE		= 0x0a,
	SLI4_CMD_SEND_FRAME_WQE			= 0x0a,
};

#define SLI4_WQE_SIZE		0x05
#define SLI4_WQE_EXT_SIZE	0x06

#define SLI4_WQE_BYTES		(16 * sizeof(u32))
#define SLI4_WQE_EXT_BYTES	(32 * sizeof(u32))

/* Mask for ccp (CS_CTL) */
#define SLI4_MASK_CCP		0xfe

/* Generic WQE */
enum sli4_gen_wqe_flags {
	SLI4_GEN_WQE_EBDECNT	= 0xf,
	SLI4_GEN_WQE_LEN_LOC	= 0x3 << 7,
	SLI4_GEN_WQE_QOSD	= 1 << 9,
	SLI4_GEN_WQE_XBL	= 1 << 11,
	SLI4_GEN_WQE_HLM	= 1 << 12,
	SLI4_GEN_WQE_IOD	= 1 << 13,
	SLI4_GEN_WQE_DBDE	= 1 << 14,
	SLI4_GEN_WQE_WQES	= 1 << 15,

	SLI4_GEN_WQE_PRI	= 0x7,
	SLI4_GEN_WQE_PV		= 1 << 3,
	SLI4_GEN_WQE_EAT	= 1 << 4,
	SLI4_GEN_WQE_XC		= 1 << 5,
	SLI4_GEN_WQE_CCPE	= 1 << 7,

	SLI4_GEN_WQE_CMDTYPE	= 0xf,
	SLI4_GEN_WQE_WQEC	= 1 << 7,
};

struct sli4_generic_wqe {
	__le32		cmd_spec0_5[6];
	__le16		xri_tag;
	__le16		context_tag;
	u8		ct_byte;
	u8		command;
	u8		class_byte;
	u8		timer;
	__le32		abort_tag;
	__le16		request_tag;
	__le16		rsvd34;
	__le16		dw10w0_flags;
	u8		eat_xc_ccpe;
	u8		ccp;
	u8		cmdtype_wqec_byte;
	u8		rsvd41;
	__le16		cq_id;
};

/* WQE used to abort exchanges. */
enum sli4_abort_wqe_flags {
	SLI4_ABRT_WQE_IR	= 0x02,

	SLI4_ABRT_WQE_EBDECNT	= 0xf,
	SLI4_ABRT_WQE_LEN_LOC	= 0x3 << 7,
	SLI4_ABRT_WQE_QOSD	= 1 << 9,
	SLI4_ABRT_WQE_XBL	= 1 << 11,
	SLI4_ABRT_WQE_IOD	= 1 << 13,
	SLI4_ABRT_WQE_DBDE	= 1 << 14,
	SLI4_ABRT_WQE_WQES	= 1 << 15,

	SLI4_ABRT_WQE_PRI	= 0x7,
	SLI4_ABRT_WQE_PV	= 1 << 3,
	SLI4_ABRT_WQE_EAT	= 1 << 4,
	SLI4_ABRT_WQE_XC	= 1 << 5,
	SLI4_ABRT_WQE_CCPE	= 1 << 7,

	SLI4_ABRT_WQE_CMDTYPE	= 0xf,
	SLI4_ABRT_WQE_WQEC	= 1 << 7,
};

struct sli4_abort_wqe {
	__le32		rsvd0;
	__le32		rsvd4;
	__le32		ext_t_tag;
	u8		ia_ir_byte;
	u8		criteria;
	__le16		rsvd10;
	__le32		ext_t_mask;
	__le32		t_mask;
	__le16		xri_tag;
	__le16		context_tag;
	u8		ct_byte;
	u8		command;
	u8		class_byte;
	u8		timer;
	__le32		t_tag;
	__le16		request_tag;
	__le16		rsvd34;
	__le16		dw10w0_flags;
	u8		eat_xc_ccpe;
	u8		ccp;
	u8		cmdtype_wqec_byte;
	u8		rsvd41;
	__le16		cq_id;
};

enum sli4_abort_criteria {
	SLI4_ABORT_CRITERIA_XRI_TAG = 0x01,
	SLI4_ABORT_CRITERIA_ABORT_TAG,
	SLI4_ABORT_CRITERIA_REQUEST_TAG,
	SLI4_ABORT_CRITERIA_EXT_ABORT_TAG,
};

enum sli4_abort_type {
	SLI4_ABORT_XRI,
	SLI4_ABORT_ABORT_ID,
	SLI4_ABORT_REQUEST_ID,
	SLI4_ABORT_MAX,		/* must be last */
};

/* WQE used to create an ELS request. */
enum sli4_els_req_wqe_flags {
	SLI4_REQ_WQE_QOSD		= 0x2,
	SLI4_REQ_WQE_DBDE		= 0x40,
	SLI4_REQ_WQE_XBL		= 0x8,
	SLI4_REQ_WQE_XC			= 0x20,
	SLI4_REQ_WQE_IOD		= 0x20,
	SLI4_REQ_WQE_HLM		= 0x10,
	SLI4_REQ_WQE_CCPE		= 0x80,
	SLI4_REQ_WQE_EAT		= 0x10,
	SLI4_REQ_WQE_WQES		= 0x80,
	SLI4_REQ_WQE_PU_SHFT		= 4,
	SLI4_REQ_WQE_CT_SHFT		= 2,
	SLI4_REQ_WQE_CT			= 0xc,
	SLI4_REQ_WQE_ELSID_SHFT		= 4,
	SLI4_REQ_WQE_SP_SHFT		= 24,
	SLI4_REQ_WQE_LEN_LOC_BIT1	= 0x80,
	SLI4_REQ_WQE_LEN_LOC_BIT2	= 0x1,
};

struct sli4_els_request64_wqe {
	struct sli4_bde	els_request_payload;
	__le32		els_request_payload_length;
	__le32		sid_sp_dword;
	__le32		remote_id_dword;
	__le16		xri_tag;
	__le16		context_tag;
	u8		ct_byte;
	u8		command;
	u8		class_byte;
	u8		timer;
	__le32		abort_tag;
	__le16		request_tag;
	__le16		temporary_rpi;
	u8		len_loc1_byte;
	u8		qosd_xbl_hlm_iod_dbde_wqes;
	u8		eat_xc_ccpe;
	u8		ccp;
	u8		cmdtype_elsid_byte;
	u8		rsvd41;
	__le16		cq_id;
	struct sli4_bde	els_response_payload_bde;
	__le32		max_response_payload_length;
};

/* WQE used to create an FCP initiator no data command. */
enum sli4_icmd_wqe_flags {
	SLI4_ICMD_WQE_DBDE		= 0x40,
	SLI4_ICMD_WQE_XBL		= 0x8,
	SLI4_ICMD_WQE_XC		= 0x20,
	SLI4_ICMD_WQE_IOD		= 0x20,
	SLI4_ICMD_WQE_HLM		= 0x10,
	SLI4_ICMD_WQE_CCPE		= 0x80,
	SLI4_ICMD_WQE_EAT		= 0x10,
	SLI4_ICMD_WQE_APPID		= 0x10,
	SLI4_ICMD_WQE_WQES		= 0x80,
	SLI4_ICMD_WQE_PU_SHFT		= 4,
	SLI4_ICMD_WQE_CT_SHFT		= 2,
	SLI4_ICMD_WQE_BS_SHFT		= 4,
	SLI4_ICMD_WQE_LEN_LOC_BIT1	= 0x80,
	SLI4_ICMD_WQE_LEN_LOC_BIT2	= 0x1,
};

struct sli4_fcp_icmnd64_wqe {
	struct sli4_bde	bde;
	__le16		payload_offset_length;
	__le16		fcp_cmd_buffer_length;
	__le32		rsvd12;
	__le32		remote_n_port_id_dword;
	__le16		xri_tag;
	__le16		context_tag;
	u8		dif_ct_bs_byte;
	u8		command;
	u8		class_pu_byte;
	u8		timer;
	__le32		abort_tag;
	__le16		request_tag;
	__le16		rsvd34;
	u8		len_loc1_byte;
	u8		qosd_xbl_hlm_iod_dbde_wqes;
	u8		eat_xc_ccpe;
	u8		ccp;
	u8		cmd_type_byte;
	u8		rsvd41;
	__le16		cq_id;
	__le32		rsvd44;
	__le32		rsvd48;
	__le32		rsvd52;
	__le32		rsvd56;
};

/* WQE used to create an FCP initiator read. */
enum sli4_ir_wqe_flags {
	SLI4_IR_WQE_DBDE		= 0x40,
	SLI4_IR_WQE_XBL			= 0x8,
	SLI4_IR_WQE_XC			= 0x20,
	SLI4_IR_WQE_IOD			= 0x20,
	SLI4_IR_WQE_HLM			= 0x10,
	SLI4_IR_WQE_CCPE		= 0x80,
	SLI4_IR_WQE_EAT			= 0x10,
	SLI4_IR_WQE_APPID		= 0x10,
	SLI4_IR_WQE_WQES		= 0x80,
	SLI4_IR_WQE_PU_SHFT		= 4,
	SLI4_IR_WQE_CT_SHFT		= 2,
	SLI4_IR_WQE_BS_SHFT		= 4,
	SLI4_IR_WQE_LEN_LOC_BIT1	= 0x80,
	SLI4_IR_WQE_LEN_LOC_BIT2	= 0x1,
};

struct sli4_fcp_iread64_wqe {
	struct sli4_bde	bde;
	__le16		payload_offset_length;
	__le16		fcp_cmd_buffer_length;

	__le32		total_transfer_length;

	__le32		remote_n_port_id_dword;

	__le16		xri_tag;
	__le16		context_tag;

	u8		dif_ct_bs_byte;
	u8		command;
	u8		class_pu_byte;
	u8		timer;

	__le32		abort_tag;

	__le16		request_tag;
	__le16		rsvd34;

	u8		len_loc1_byte;
	u8		qosd_xbl_hlm_iod_dbde_wqes;
	u8		eat_xc_ccpe;
	u8		ccp;

	u8		cmd_type_byte;
	u8		rsvd41;
	__le16		cq_id;

	__le32		rsvd44;
	struct sli4_bde	first_data_bde;
};

/* WQE used to create an FCP initiator write. */
enum sli4_iwr_wqe_flags {
	SLI4_IWR_WQE_DBDE		= 0x40,
	SLI4_IWR_WQE_XBL		= 0x8,
	SLI4_IWR_WQE_XC			= 0x20,
	SLI4_IWR_WQE_IOD		= 0x20,
	SLI4_IWR_WQE_HLM		= 0x10,
	SLI4_IWR_WQE_DNRX		= 0x10,
	SLI4_IWR_WQE_CCPE		= 0x80,
	SLI4_IWR_WQE_EAT		= 0x10,
	SLI4_IWR_WQE_APPID		= 0x10,
	SLI4_IWR_WQE_WQES		= 0x80,
	SLI4_IWR_WQE_PU_SHFT		= 4,
	SLI4_IWR_WQE_CT_SHFT		= 2,
	SLI4_IWR_WQE_BS_SHFT		= 4,
	SLI4_IWR_WQE_LEN_LOC_BIT1	= 0x80,
	SLI4_IWR_WQE_LEN_LOC_BIT2	= 0x1,
};

struct sli4_fcp_iwrite64_wqe {
	struct sli4_bde	bde;
	__le16		payload_offset_length;
	__le16		fcp_cmd_buffer_length;
	__le16		total_transfer_length;
	__le16		initial_transfer_length;
	__le16		xri_tag;
	__le16		context_tag;
	u8		dif_ct_bs_byte;
	u8		command;
	u8		class_pu_byte;
	u8		timer;
	__le32		abort_tag;
	__le16		request_tag;
	__le16		rsvd34;
	u8		len_loc1_byte;
	u8		qosd_xbl_hlm_iod_dbde_wqes;
	u8		eat_xc_ccpe;
	u8		ccp;
	u8		cmd_type_byte;
	u8		rsvd41;
	__le16		cq_id;
	__le32		remote_n_port_id_dword;
	struct sli4_bde	first_data_bde;
};

struct sli4_fcp_128byte_wqe {
	u32 dw[32];
};

/* WQE used to create an FCP target receive */
enum sli4_trcv_wqe_flags {
	SLI4_TRCV_WQE_DBDE		= 0x40,
	SLI4_TRCV_WQE_XBL		= 0x8,
	SLI4_TRCV_WQE_AR		= 0x8,
	SLI4_TRCV_WQE_XC		= 0x20,
	SLI4_TRCV_WQE_IOD		= 0x20,
	SLI4_TRCV_WQE_HLM		= 0x10,
	SLI4_TRCV_WQE_DNRX		= 0x10,
	SLI4_TRCV_WQE_CCPE		= 0x80,
	SLI4_TRCV_WQE_EAT		= 0x10,
	SLI4_TRCV_WQE_APPID		= 0x10,
	SLI4_TRCV_WQE_WQES		= 0x80,
	SLI4_TRCV_WQE_PU_SHFT		= 4,
	SLI4_TRCV_WQE_CT_SHFT		= 2,
	SLI4_TRCV_WQE_BS_SHFT		= 4,
	SLI4_TRCV_WQE_LEN_LOC_BIT2	= 0x1,
};

struct sli4_fcp_treceive64_wqe {
	struct sli4_bde	bde;
	__le32		payload_offset_length;
	__le32		relative_offset;
	union {
		__le16	sec_xri_tag;
		__le16	rsvd;
		__le32	dword;
	} dword5;
	__le16		xri_tag;
	__le16		context_tag;
	u8		dif_ct_bs_byte;
	u8		command;
	u8		class_ar_pu_byte;
	u8		timer;
	__le32		abort_tag;
	__le16		request_tag;
	__le16		remote_xid;
	u8		lloc1_appid;
	u8		qosd_xbl_hlm_iod_dbde_wqes;
	u8		eat_xc_ccpe;
	u8		ccp;
	u8		cmd_type_byte;
	u8		rsvd41;
	__le16		cq_id;
	__le32		fcp_data_receive_length;
	struct sli4_bde	first_data_bde;
};

/* WQE used to create an FCP target response */
enum sli4_trsp_wqe_flags {
	SLI4_TRSP_WQE_AG	= 0x8,
	SLI4_TRSP_WQE_DBDE	= 0x40,
	SLI4_TRSP_WQE_XBL	= 0x8,
	SLI4_TRSP_WQE_XC	= 0x20,
	SLI4_TRSP_WQE_HLM	= 0x10,
	SLI4_TRSP_WQE_DNRX	= 0x10,
	SLI4_TRSP_WQE_CCPE	= 0x80,
	SLI4_TRSP_WQE_EAT	= 0x10,
	SLI4_TRSP_WQE_APPID	= 0x10,
	SLI4_TRSP_WQE_WQES	= 0x80,
};

struct sli4_fcp_trsp64_wqe {
	struct sli4_bde	bde;
	__le32		fcp_response_length;
	__le32		rsvd12;
	__le32		dword5;
	__le16		xri_tag;
	__le16		rpi;
	u8		ct_dnrx_byte;
	u8		command;
	u8		class_ag_byte;
	u8		timer;
	__le32		abort_tag;
	__le16		request_tag;
	__le16		remote_xid;
	u8		lloc1_appid;
	u8		qosd_xbl_hlm_dbde_wqes;
	u8		eat_xc_ccpe;
	u8		ccp;
	u8		cmd_type_byte;
	u8		rsvd41;
	__le16		cq_id;
	__le32		rsvd44;
	__le32		rsvd48;
	__le32		rsvd52;
	__le32		rsvd56;
};

/* WQE used to create an FCP target send (DATA IN). */
enum sli4_tsend_wqe_flags {
	SLI4_TSEND_WQE_XBL	= 0x8,
	SLI4_TSEND_WQE_DBDE	= 0x40,
	SLI4_TSEND_WQE_IOD	= 0x20,
	SLI4_TSEND_WQE_QOSD	= 0x2,
	SLI4_TSEND_WQE_HLM	= 0x10,
	SLI4_TSEND_WQE_PU_SHFT	= 4,
	SLI4_TSEND_WQE_AR	= 0x8,
	SLI4_TSEND_CT_SHFT	= 2,
	SLI4_TSEND_BS_SHFT	= 4,
	SLI4_TSEND_LEN_LOC_BIT2 = 0x1,
	SLI4_TSEND_CCPE		= 0x80,
	SLI4_TSEND_APPID_VALID	= 0x20,
	SLI4_TSEND_WQES		= 0x80,
	SLI4_TSEND_XC		= 0x20,
	SLI4_TSEND_EAT		= 0x10,
};

struct sli4_fcp_tsend64_wqe {
	struct sli4_bde	bde;
	__le32		payload_offset_length;
	__le32		relative_offset;
	__le32		dword5;
	__le16		xri_tag;
	__le16		rpi;
	u8		ct_byte;
	u8		command;
	u8		class_pu_ar_byte;
	u8		timer;
	__le32		abort_tag;
	__le16		request_tag;
	__le16		remote_xid;
	u8		dw10byte0;
	u8		ll_qd_xbl_hlm_iod_dbde;
	u8		dw10byte2;
	u8		ccp;
	u8		cmd_type_byte;
	u8		rsvd45;
	__le16		cq_id;
	__le32		fcp_data_transmit_length;
	struct sli4_bde	first_data_bde;
};

/* WQE used to create a general request. */
enum sli4_gen_req_wqe_flags {
	SLI4_GEN_REQ64_WQE_XBL	= 0x8,
	SLI4_GEN_REQ64_WQE_DBDE	= 0x40,
	SLI4_GEN_REQ64_WQE_IOD	= 0x20,
	SLI4_GEN_REQ64_WQE_QOSD	= 0x2,
	SLI4_GEN_REQ64_WQE_HLM	= 0x10,
	SLI4_GEN_REQ64_CT_SHFT	= 2,
};

struct sli4_gen_request64_wqe {
	struct sli4_bde	bde;
	__le32		request_payload_length;
	__le32		relative_offset;
	u8		rsvd17;
	u8		df_ctl;
	u8		type;
	u8		r_ctl;
	__le16		xri_tag;
	__le16		context_tag;
	u8		ct_byte;
	u8		command;
	u8		class_byte;
	u8		timer;
	__le32		abort_tag;
	__le16		request_tag;
	__le16		rsvd34;
	u8		dw10flags0;
	u8		dw10flags1;
	u8		dw10flags2;
	u8		ccp;
	u8		cmd_type_byte;
	u8		rsvd41;
	__le16		cq_id;
	__le32		remote_n_port_id_dword;
	__le32		rsvd48;
	__le32		rsvd52;
	__le32		max_response_payload_length;
};

/* WQE used to create a send frame request */
enum sli4_sf_wqe_flags {
	SLI4_SF_WQE_DBDE	= 0x40,
	SLI4_SF_PU		= 0x30,
	SLI4_SF_CT		= 0xc,
	SLI4_SF_QOSD		= 0x2,
	SLI4_SF_LEN_LOC_BIT1	= 0x80,
	SLI4_SF_LEN_LOC_BIT2	= 0x1,
	SLI4_SF_XC		= 0x20,
	SLI4_SF_XBL		= 0x8,
};

struct sli4_send_frame_wqe {
	struct sli4_bde	bde;
	__le32		frame_length;
	__le32		fc_header_0_1[2];
	__le16		xri_tag;
	__le16		context_tag;
	u8		ct_byte;
	u8		command;
	u8		dw7flags0;
	u8		timer;
	__le32		abort_tag;
	__le16		request_tag;
	u8		eof;
	u8		sof;
	u8		dw10flags0;
	u8		dw10flags1;
	u8		dw10flags2;
	u8		ccp;
	u8		cmd_type_byte;
	u8		rsvd41;
	__le16		cq_id;
	__le32		fc_header_2_5[4];
};

/* WQE used to create a transmit sequence */
enum sli4_seq_wqe_flags {
	SLI4_SEQ_WQE_DBDE		= 0x4000,
	SLI4_SEQ_WQE_XBL		= 0x800,
	SLI4_SEQ_WQE_SI			= 0x4,
	SLI4_SEQ_WQE_FT			= 0x8,
	SLI4_SEQ_WQE_XO			= 0x40,
	SLI4_SEQ_WQE_LS			= 0x80,
	SLI4_SEQ_WQE_DIF		= 0x3,
	SLI4_SEQ_WQE_BS			= 0x70,
	SLI4_SEQ_WQE_PU			= 0x30,
	SLI4_SEQ_WQE_HLM		= 0x1000,
	SLI4_SEQ_WQE_IOD_SHIFT		= 13,
	SLI4_SEQ_WQE_CT_SHIFT		= 2,
	SLI4_SEQ_WQE_LEN_LOC_SHIFT	= 7,
};

struct sli4_xmit_sequence64_wqe {
	struct sli4_bde	bde;
	__le32		remote_n_port_id_dword;
	__le32		relative_offset;
	u8		dw5flags0;
	u8		df_ctl;
	u8		type;
	u8		r_ctl;
	__le16		xri_tag;
	__le16		context_tag;
	u8		dw7flags0;
	u8		command;
	u8		dw7flags1;
	u8		timer;
	__le32		abort_tag;
	__le16		request_tag;
	__le16		remote_xid;
	__le16		dw10w0;
	u8		dw10flags0;
	u8		ccp;
	u8		cmd_type_wqec_byte;
	u8		rsvd45;
	__le16		cq_id;
	__le32		sequence_payload_len;
	__le32		rsvd48;
	__le32		rsvd52;
	__le32		rsvd56;
};

/*
 * WQE used unblock the specified XRI and to release
 * it to the SLI Port's free pool.
 */
enum sli4_requeue_wqe_flags {
	SLI4_REQU_XRI_WQE_XC	= 0x20,
	SLI4_REQU_XRI_WQE_QOSD	= 0x2,
};

struct sli4_requeue_xri_wqe {
	__le32		rsvd0;
	__le32		rsvd4;
	__le32		rsvd8;
	__le32		rsvd12;
	__le32		rsvd16;
	__le32		rsvd20;
	__le16		xri_tag;
	__le16		context_tag;
	u8		ct_byte;
	u8		command;
	u8		class_byte;
	u8		timer;
	__le32		rsvd32;
	__le16		request_tag;
	__le16		rsvd34;
	__le16		flags0;
	__le16		flags1;
	__le16		flags2;
	u8		ccp;
	u8		cmd_type_wqec_byte;
	u8		rsvd42;
	__le16		cq_id;
	__le32		rsvd44;
	__le32		rsvd48;
	__le32		rsvd52;
	__le32		rsvd56;
};

/* WQE used to create a BLS response */
enum sli4_bls_rsp_wqe_flags {
	SLI4_BLS_RSP_RID		= 0xffffff,
	SLI4_BLS_RSP_WQE_AR		= 0x40000000,
	SLI4_BLS_RSP_WQE_CT_SHFT	= 2,
	SLI4_BLS_RSP_WQE_QOSD		= 0x2,
	SLI4_BLS_RSP_WQE_HLM		= 0x10,
};

struct sli4_xmit_bls_rsp_wqe {
	__le32		payload_word0;
	__le16		rx_id;
	__le16		ox_id;
	__le16		high_seq_cnt;
	__le16		low_seq_cnt;
	__le32		rsvd12;
	__le32		local_n_port_id_dword;
	__le32		remote_id_dword;
	__le16		xri_tag;
	__le16		context_tag;
	u8		dw8flags0;
	u8		command;
	u8		dw8flags1;
	u8		timer;
	__le32		abort_tag;
	__le16		request_tag;
	__le16		rsvd38;
	u8		dw11flags0;
	u8		dw11flags1;
	u8		dw11flags2;
	u8		ccp;
	u8		dw12flags0;
	u8		rsvd45;
	__le16		cq_id;
	__le16		temporary_rpi;
	u8		rsvd50;
	u8		rsvd51;
	__le32		rsvd52;
	__le32		rsvd56;
	__le32		rsvd60;
};

enum sli_bls_type {
	SLI4_SLI_BLS_ACC,
	SLI4_SLI_BLS_RJT,
	SLI4_SLI_BLS_MAX
};

struct sli_bls_payload {
	enum sli_bls_type	type;
	__le16			ox_id;
	__le16			rx_id;
	union {
		struct {
			u8	seq_id_validity;
			u8	seq_id_last;
			u8	rsvd2;
			u8	rsvd3;
			u16	ox_id;
			u16	rx_id;
			__le16	low_seq_cnt;
			__le16	high_seq_cnt;
		} acc;
		struct {
			u8	vendor_unique;
			u8	reason_explanation;
			u8	reason_code;
			u8	rsvd3;
		} rjt;
	} u;
};

/* WQE used to create an ELS response */

enum sli4_els_rsp_flags {
	SLI4_ELS_SID		= 0xffffff,
	SLI4_ELS_RID		= 0xffffff,
	SLI4_ELS_DBDE		= 0x40,
	SLI4_ELS_XBL		= 0x8,
	SLI4_ELS_IOD		= 0x20,
	SLI4_ELS_QOSD		= 0x2,
	SLI4_ELS_XC		= 0x20,
	SLI4_ELS_CT_OFFSET	= 0X2,
	SLI4_ELS_SP		= 0X1000000,
	SLI4_ELS_HLM		= 0X10,
};

struct sli4_xmit_els_rsp64_wqe {
	struct sli4_bde	els_response_payload;
	__le32		els_response_payload_length;
	__le32		sid_dw;
	__le32		rid_dw;
	__le16		xri_tag;
	__le16		context_tag;
	u8		ct_byte;
	u8		command;
	u8		class_byte;
	u8		timer;
	__le32		abort_tag;
	__le16		request_tag;
	__le16		ox_id;
	u8		flags1;
	u8		flags2;
	u8		flags3;
	u8		flags4;
	u8		cmd_type_wqec;
	u8		rsvd34;
	__le16		cq_id;
	__le16		temporary_rpi;
	__le16		rsvd38;
	u32		rsvd40;
	u32		rsvd44;
	u32		rsvd48;
};

/* Local Reject Reason Codes */
enum sli4_fc_local_rej_codes {
	SLI4_FC_LOCAL_REJECT_UNKNOWN,
	SLI4_FC_LOCAL_REJECT_MISSING_CONTINUE,
	SLI4_FC_LOCAL_REJECT_SEQUENCE_TIMEOUT,
	SLI4_FC_LOCAL_REJECT_INTERNAL_ERROR,
	SLI4_FC_LOCAL_REJECT_INVALID_RPI,
	SLI4_FC_LOCAL_REJECT_NO_XRI,
	SLI4_FC_LOCAL_REJECT_ILLEGAL_COMMAND,
	SLI4_FC_LOCAL_REJECT_XCHG_DROPPED,
	SLI4_FC_LOCAL_REJECT_ILLEGAL_FIELD,
	SLI4_FC_LOCAL_REJECT_RPI_SUSPENDED,
	SLI4_FC_LOCAL_REJECT_RSVD,
	SLI4_FC_LOCAL_REJECT_RSVD1,
	SLI4_FC_LOCAL_REJECT_NO_ABORT_MATCH,
	SLI4_FC_LOCAL_REJECT_TX_DMA_FAILED,
	SLI4_FC_LOCAL_REJECT_RX_DMA_FAILED,
	SLI4_FC_LOCAL_REJECT_ILLEGAL_FRAME,
	SLI4_FC_LOCAL_REJECT_RSVD2,
	SLI4_FC_LOCAL_REJECT_NO_RESOURCES, //0x11
	SLI4_FC_LOCAL_REJECT_FCP_CONF_FAILURE,
	SLI4_FC_LOCAL_REJECT_ILLEGAL_LENGTH,
	SLI4_FC_LOCAL_REJECT_UNSUPPORTED_FEATURE,
	SLI4_FC_LOCAL_REJECT_ABORT_IN_PROGRESS,
	SLI4_FC_LOCAL_REJECT_ABORT_REQUESTED,
	SLI4_FC_LOCAL_REJECT_RCV_BUFFER_TIMEOUT,
	SLI4_FC_LOCAL_REJECT_LOOP_OPEN_FAILURE,
	SLI4_FC_LOCAL_REJECT_RSVD3,
	SLI4_FC_LOCAL_REJECT_LINK_DOWN,
	SLI4_FC_LOCAL_REJECT_CORRUPTED_DATA,
	SLI4_FC_LOCAL_REJECT_CORRUPTED_RPI,
	SLI4_FC_LOCAL_REJECT_OUTOFORDER_DATA,
	SLI4_FC_LOCAL_REJECT_OUTOFORDER_ACK,
	SLI4_FC_LOCAL_REJECT_DUP_FRAME,
	SLI4_FC_LOCAL_REJECT_LINK_CONTROL_FRAME, //0x20
	SLI4_FC_LOCAL_REJECT_BAD_HOST_ADDRESS,
	SLI4_FC_LOCAL_REJECT_RSVD4,
	SLI4_FC_LOCAL_REJECT_MISSING_HDR_BUFFER,
	SLI4_FC_LOCAL_REJECT_MSEQ_CHAIN_CORRUPTED,
	SLI4_FC_LOCAL_REJECT_ABORTMULT_REQUESTED,
	SLI4_FC_LOCAL_REJECT_BUFFER_SHORTAGE	= 0x28,
	SLI4_FC_LOCAL_REJECT_RCV_XRIBUF_WAITING,
	SLI4_FC_LOCAL_REJECT_INVALID_VPI	= 0x2e,
	SLI4_FC_LOCAL_REJECT_NO_FPORT_DETECTED,
	SLI4_FC_LOCAL_REJECT_MISSING_XRIBUF,
	SLI4_FC_LOCAL_REJECT_RSVD5,
	SLI4_FC_LOCAL_REJECT_INVALID_XRI,
	SLI4_FC_LOCAL_REJECT_INVALID_RELOFFSET	= 0x40,
	SLI4_FC_LOCAL_REJECT_MISSING_RELOFFSET,
	SLI4_FC_LOCAL_REJECT_INSUFF_BUFFERSPACE,
	SLI4_FC_LOCAL_REJECT_MISSING_SI,
	SLI4_FC_LOCAL_REJECT_MISSING_ES,
	SLI4_FC_LOCAL_REJECT_INCOMPLETE_XFER,
	SLI4_FC_LOCAL_REJECT_SLER_FAILURE,
	SLI4_FC_LOCAL_REJECT_SLER_CMD_RCV_FAILURE,
	SLI4_FC_LOCAL_REJECT_SLER_REC_RJT_ERR,
	SLI4_FC_LOCAL_REJECT_SLER_REC_SRR_RETRY_ERR,
	SLI4_FC_LOCAL_REJECT_SLER_SRR_RJT_ERR,
	SLI4_FC_LOCAL_REJECT_RSVD6,
	SLI4_FC_LOCAL_REJECT_SLER_RRQ_RJT_ERR,
	SLI4_FC_LOCAL_REJECT_SLER_RRQ_RETRY_ERR,
	SLI4_FC_LOCAL_REJECT_SLER_ABTS_ERR,
};

enum sli4_async_rcqe_flags {
	SLI4_RACQE_RQ_EL_INDX	= 0xfff,
	SLI4_RACQE_FCFI		= 0x3f,
	SLI4_RACQE_HDPL		= 0x3f,
	SLI4_RACQE_RQ_ID	= 0xffc0,
};

struct sli4_fc_async_rcqe {
	u8		rsvd0;
	u8		status;
	__le16		rq_elmt_indx_word;
	__le32		rsvd4;
	__le16		fcfi_rq_id_word;
	__le16		data_placement_length;
	u8		sof_byte;
	u8		eof_byte;
	u8		code;
	u8		hdpl_byte;
};

struct sli4_fc_async_rcqe_v1 {
	u8		rsvd0;
	u8		status;
	__le16		rq_elmt_indx_word;
	u8		fcfi_byte;
	u8		rsvd5;
	__le16		rsvd6;
	__le16		rq_id;
	__le16		data_placement_length;
	u8		sof_byte;
	u8		eof_byte;
	u8		code;
	u8		hdpl_byte;
};

enum sli4_fc_async_rq_status {
	SLI4_FC_ASYNC_RQ_SUCCESS = 0x10,
	SLI4_FC_ASYNC_RQ_BUF_LEN_EXCEEDED,
	SLI4_FC_ASYNC_RQ_INSUFF_BUF_NEEDED,
	SLI4_FC_ASYNC_RQ_INSUFF_BUF_FRM_DISC,
	SLI4_FC_ASYNC_RQ_DMA_FAILURE,
};

#define SLI4_RCQE_RQ_EL_INDX	0xfff

struct sli4_fc_coalescing_rcqe {
	u8		rsvd0;
	u8		status;
	__le16		rq_elmt_indx_word;
	__le32		rsvd4;
	__le16		rq_id;
	__le16		seq_placement_length;
	__le16		rsvd14;
	u8		code;
	u8		vld_byte;
};

#define SLI4_FC_COALESCE_RQ_SUCCESS		0x10
#define SLI4_FC_COALESCE_RQ_INSUFF_XRI_NEEDED	0x18

enum sli4_optimized_write_cmd_cqe_flags {
	SLI4_OCQE_RQ_EL_INDX	= 0x7f,		/* DW0 bits 16:30 */
	SLI4_OCQE_FCFI		= 0x3f,		/* DW1 bits 0:6 */
	SLI4_OCQE_OOX		= 1 << 6,	/* DW1 bit 15 */
	SLI4_OCQE_AGXR		= 1 << 7,	/* DW1 bit 16 */
	SLI4_OCQE_HDPL		= 0x3f,		/* DW3 bits 24:29*/
};

struct sli4_fc_optimized_write_cmd_cqe {
	u8		rsvd0;
	u8		status;
	__le16		w1;
	u8		flags0;
	u8		flags1;
	__le16		xri;
	__le16		rq_id;
	__le16		data_placement_length;
	__le16		rpi;
	u8		code;
	u8		hdpl_vld;
};

#define	SLI4_OCQE_XB		0x10

struct sli4_fc_optimized_write_data_cqe {
	u8		hw_status;
	u8		status;
	__le16		xri;
	__le32		total_data_placed;
	__le32		extended_status;
	__le16		rsvd12;
	u8		code;
	u8		flags;
};

struct sli4_fc_xri_aborted_cqe {
	u8		rsvd0;
	u8		status;
	__le16		rsvd2;
	__le32		extended_status;
	__le16		xri;
	__le16		remote_xid;
	__le16		rsvd12;
	u8		code;
	u8		flags;
};

enum sli4_generic_ctx {
	SLI4_GENERIC_CONTEXT_RPI,
	SLI4_GENERIC_CONTEXT_VPI,
	SLI4_GENERIC_CONTEXT_VFI,
	SLI4_GENERIC_CONTEXT_FCFI,
};

#define SLI4_GENERIC_CLASS_CLASS_2		0x1
#define SLI4_GENERIC_CLASS_CLASS_3		0x2

#define SLI4_ELS_REQUEST64_DIR_WRITE		0x0
#define SLI4_ELS_REQUEST64_DIR_READ		0x1

enum sli4_els_request {
	SLI4_ELS_REQUEST64_OTHER,
	SLI4_ELS_REQUEST64_LOGO,
	SLI4_ELS_REQUEST64_FDISC,
	SLI4_ELS_REQUEST64_FLOGIN,
	SLI4_ELS_REQUEST64_PLOGI,
};

enum sli4_els_cmd_type {
	SLI4_ELS_REQUEST64_CMD_GEN		= 0x08,
	SLI4_ELS_REQUEST64_CMD_NON_FABRIC	= 0x0c,
	SLI4_ELS_REQUEST64_CMD_FABRIC		= 0x0d,
};

#define SLI_PAGE_SIZE				SZ_4K

#define SLI4_BMBX_TIMEOUT_MSEC			30000
#define SLI4_FW_READY_TIMEOUT_MSEC		30000

#define SLI4_BMBX_DELAY_US			1000	/* 1 ms */
#define SLI4_INIT_PORT_DELAY_US			10000	/* 10 ms */

static inline u32
sli_page_count(size_t bytes, u32 page_size)
{
	if (!page_size)
		return 0;

	return (bytes + (page_size - 1)) >> __ffs(page_size);
}

/*************************************************************************
 * SLI-4 mailbox command formats and definitions
 */

struct sli4_mbox_command_header {
	u8	resvd0;
	u8	command;
	__le16	status;	/* Port writes to indicate success/fail */
};

enum sli4_mbx_cmd_value {
	SLI4_MBX_CMD_CONFIG_LINK	= 0x07,
	SLI4_MBX_CMD_DUMP		= 0x17,
	SLI4_MBX_CMD_DOWN_LINK		= 0x06,
	SLI4_MBX_CMD_INIT_LINK		= 0x05,
	SLI4_MBX_CMD_INIT_VFI		= 0xa3,
	SLI4_MBX_CMD_INIT_VPI		= 0xa4,
	SLI4_MBX_CMD_POST_XRI		= 0xa7,
	SLI4_MBX_CMD_RELEASE_XRI	= 0xac,
	SLI4_MBX_CMD_READ_CONFIG	= 0x0b,
	SLI4_MBX_CMD_READ_STATUS	= 0x0e,
	SLI4_MBX_CMD_READ_NVPARMS	= 0x02,
	SLI4_MBX_CMD_READ_REV		= 0x11,
	SLI4_MBX_CMD_READ_LNK_STAT	= 0x12,
	SLI4_MBX_CMD_READ_SPARM64	= 0x8d,
	SLI4_MBX_CMD_READ_TOPOLOGY	= 0x95,
	SLI4_MBX_CMD_REG_FCFI		= 0xa0,
	SLI4_MBX_CMD_REG_FCFI_MRQ	= 0xaf,
	SLI4_MBX_CMD_REG_RPI		= 0x93,
	SLI4_MBX_CMD_REG_RX_RQ		= 0xa6,
	SLI4_MBX_CMD_REG_VFI		= 0x9f,
	SLI4_MBX_CMD_REG_VPI		= 0x96,
	SLI4_MBX_CMD_RQST_FEATURES	= 0x9d,
	SLI4_MBX_CMD_SLI_CONFIG		= 0x9b,
	SLI4_MBX_CMD_UNREG_FCFI		= 0xa2,
	SLI4_MBX_CMD_UNREG_RPI		= 0x14,
	SLI4_MBX_CMD_UNREG_VFI		= 0xa1,
	SLI4_MBX_CMD_UNREG_VPI		= 0x97,
	SLI4_MBX_CMD_WRITE_NVPARMS	= 0x03,
	SLI4_MBX_CMD_CFG_AUTO_XFER_RDY	= 0xad,
};

enum sli4_mbx_status {
	SLI4_MBX_STATUS_SUCCESS		= 0x0000,
	SLI4_MBX_STATUS_FAILURE		= 0x0001,
	SLI4_MBX_STATUS_RPI_NOT_REG	= 0x1400,
};

/* CONFIG_LINK - configure link-oriented parameters,
 * such as default N_Port_ID address and various timers
 */
enum sli4_cmd_config_link_flags {
	SLI4_CFG_LINK_BBSCN = 0xf00,
	SLI4_CFG_LINK_CSCN  = 0x1000,
};

struct sli4_cmd_config_link {
	struct sli4_mbox_command_header	hdr;
	u8		maxbbc;
	u8		rsvd5;
	u8		rsvd6;
	u8		rsvd7;
	u8		alpa;
	__le16		n_port_id;
	u8		rsvd11;
	__le32		rsvd12;
	__le32		e_d_tov;
	__le32		lp_tov;
	__le32		r_a_tov;
	__le32		r_t_tov;
	__le32		al_tov;
	__le32		rsvd36;
	__le32		bbscn_dword;
};

#define SLI4_DUMP4_TYPE		0xf

#define SLI4_WKI_TAG_SAT_TEM	0x1040

struct sli4_cmd_dump4 {
	struct sli4_mbox_command_header	hdr;
	__le32		type_dword;
	__le16		wki_selection;
	__le16		rsvd10;
	__le32		rsvd12;
	__le32		returned_byte_cnt;
	__le32		resp_data[59];
};

/* INIT_LINK - initialize the link for a FC port */
enum sli4_init_link_flags {
	SLI4_INIT_LINK_F_LOOPBACK	= 1 << 0,

	SLI4_INIT_LINK_F_P2P_ONLY	= 1 << 1,
	SLI4_INIT_LINK_F_FCAL_ONLY	= 2 << 1,
	SLI4_INIT_LINK_F_FCAL_FAIL_OVER	= 0 << 1,
	SLI4_INIT_LINK_F_P2P_FAIL_OVER	= 1 << 1,

	SLI4_INIT_LINK_F_UNFAIR		= 1 << 6,
	SLI4_INIT_LINK_F_NO_LIRP	= 1 << 7,
	SLI4_INIT_LINK_F_LOOP_VALID_CHK	= 1 << 8,
	SLI4_INIT_LINK_F_NO_LISA	= 1 << 9,
	SLI4_INIT_LINK_F_FAIL_OVER	= 1 << 10,
	SLI4_INIT_LINK_F_FIXED_SPEED	= 1 << 11,
	SLI4_INIT_LINK_F_PICK_HI_ALPA	= 1 << 15,

};

enum sli4_fc_link_speed {
	SLI4_LINK_SPEED_1G = 1,
	SLI4_LINK_SPEED_2G,
	SLI4_LINK_SPEED_AUTO_1_2,
	SLI4_LINK_SPEED_4G,
	SLI4_LINK_SPEED_AUTO_4_1,
	SLI4_LINK_SPEED_AUTO_4_2,
	SLI4_LINK_SPEED_AUTO_4_2_1,
	SLI4_LINK_SPEED_8G,
	SLI4_LINK_SPEED_AUTO_8_1,
	SLI4_LINK_SPEED_AUTO_8_2,
	SLI4_LINK_SPEED_AUTO_8_2_1,
	SLI4_LINK_SPEED_AUTO_8_4,
	SLI4_LINK_SPEED_AUTO_8_4_1,
	SLI4_LINK_SPEED_AUTO_8_4_2,
	SLI4_LINK_SPEED_10G,
	SLI4_LINK_SPEED_16G,
	SLI4_LINK_SPEED_AUTO_16_8_4,
	SLI4_LINK_SPEED_AUTO_16_8,
	SLI4_LINK_SPEED_32G,
	SLI4_LINK_SPEED_AUTO_32_16_8,
	SLI4_LINK_SPEED_AUTO_32_16,
	SLI4_LINK_SPEED_64G,
	SLI4_LINK_SPEED_AUTO_64_32_16,
	SLI4_LINK_SPEED_AUTO_64_32,
	SLI4_LINK_SPEED_128G,
	SLI4_LINK_SPEED_AUTO_128_64_32,
	SLI4_LINK_SPEED_AUTO_128_64,
};

struct sli4_cmd_init_link {
	struct sli4_mbox_command_header       hdr;
	__le32	sel_reset_al_pa_dword;
	__le32	flags0;
	__le32	link_speed_sel_code;
};

/* INIT_VFI - initialize the VFI resource */
enum sli4_init_vfi_flags {
	SLI4_INIT_VFI_FLAG_VP	= 0x1000,
	SLI4_INIT_VFI_FLAG_VF	= 0x2000,
	SLI4_INIT_VFI_FLAG_VT	= 0x4000,
	SLI4_INIT_VFI_FLAG_VR	= 0x8000,

	SLI4_INIT_VFI_VFID	= 0x1fff,
	SLI4_INIT_VFI_PRI	= 0xe000,

	SLI4_INIT_VFI_HOP_COUNT = 0xff000000,
};

struct sli4_cmd_init_vfi {
	struct sli4_mbox_command_header	hdr;
	__le16		vfi;
	__le16		flags0_word;
	__le16		fcfi;
	__le16		vpi;
	__le32		vf_id_pri_dword;
	__le32		hop_cnt_dword;
};

/* INIT_VPI - initialize the VPI resource */
struct sli4_cmd_init_vpi {
	struct sli4_mbox_command_header	hdr;
	__le16		vpi;
	__le16		vfi;
};

/* POST_XRI - post XRI resources to the SLI Port */
enum sli4_post_xri_flags {
	SLI4_POST_XRI_COUNT	= 0xfff,
	SLI4_POST_XRI_FLAG_ENX	= 0x1000,
	SLI4_POST_XRI_FLAG_DL	= 0x2000,
	SLI4_POST_XRI_FLAG_DI	= 0x4000,
	SLI4_POST_XRI_FLAG_VAL	= 0x8000,
};

struct sli4_cmd_post_xri {
	struct sli4_mbox_command_header	hdr;
	__le16		xri_base;
	__le16		xri_count_flags;
};

/* RELEASE_XRI - Release XRI resources from the SLI Port */
enum sli4_release_xri_flags {
	SLI4_RELEASE_XRI_REL_XRI_CNT	= 0x1f,
	SLI4_RELEASE_XRI_COUNT		= 0x1f,
};

struct sli4_cmd_release_xri {
	struct sli4_mbox_command_header	hdr;
	__le16		rel_xri_count_word;
	__le16		xri_count_word;

	struct {
		__le16	xri_tag0;
		__le16	xri_tag1;
	} xri_tbl[62];
};

/* READ_CONFIG - read SLI port configuration parameters */
struct sli4_cmd_read_config {
	struct sli4_mbox_command_header	hdr;
};

enum sli4_read_cfg_resp_flags {
	SLI4_READ_CFG_RESP_RESOURCE_EXT = 0x80000000,	/* DW1 */
	SLI4_READ_CFG_RESP_TOPOLOGY	= 0xff000000,	/* DW2 */
};

enum sli4_read_cfg_topo {
	SLI4_READ_CFG_TOPO_FC		= 0x1,	/* FC topology unknown */
	SLI4_READ_CFG_TOPO_NON_FC_AL	= 0x2,	/* FC point-to-point or fabric */
	SLI4_READ_CFG_TOPO_FC_AL	= 0x3,	/* FC-AL topology */
};

/* Link Module Type */
enum sli4_read_cfg_lmt {
	SLI4_LINK_MODULE_TYPE_1GB	= 0x0004,
	SLI4_LINK_MODULE_TYPE_2GB	= 0x0008,
	SLI4_LINK_MODULE_TYPE_4GB	= 0x0040,
	SLI4_LINK_MODULE_TYPE_8GB	= 0x0080,
	SLI4_LINK_MODULE_TYPE_16GB	= 0x0200,
	SLI4_LINK_MODULE_TYPE_32GB	= 0x0400,
	SLI4_LINK_MODULE_TYPE_64GB	= 0x0800,
	SLI4_LINK_MODULE_TYPE_128GB	= 0x1000,
};

struct sli4_rsp_read_config {
	struct sli4_mbox_command_header	hdr;
	__le32		ext_dword;
	__le32		topology_dword;
	__le32		resvd8;
	__le16		e_d_tov;
	__le16		resvd14;
	__le32		resvd16;
	__le16		r_a_tov;
	__le16		resvd22;
	__le32		resvd24;
	__le32		resvd28;
	__le16		lmt;
	__le16		resvd34;
	__le32		resvd36;
	__le32		resvd40;
	__le16		xri_base;
	__le16		xri_count;
	__le16		rpi_base;
	__le16		rpi_count;
	__le16		vpi_base;
	__le16		vpi_count;
	__le16		vfi_base;
	__le16		vfi_count;
	__le16		resvd60;
	__le16		fcfi_count;
	__le16		rq_count;
	__le16		eq_count;
	__le16		wq_count;
	__le16		cq_count;
	__le32		pad[45];
};

/* READ_NVPARMS - read SLI port configuration parameters */
enum sli4_read_nvparms_flags {
	SLI4_READ_NVPARAMS_HARD_ALPA	  = 0xff,
	SLI4_READ_NVPARAMS_PREFERRED_D_ID = 0xffffff00,
};

struct sli4_cmd_read_nvparms {
	struct sli4_mbox_command_header	hdr;
	__le32		resvd0;
	__le32		resvd4;
	__le32		resvd8;
	__le32		resvd12;
	u8		wwpn[8];
	u8		wwnn[8];
	__le32		hard_alpa_d_id;
};

/* WRITE_NVPARMS - write SLI port configuration parameters */
struct sli4_cmd_write_nvparms {
	struct sli4_mbox_command_header	hdr;
	__le32		resvd0;
	__le32		resvd4;
	__le32		resvd8;
	__le32		resvd12;
	u8		wwpn[8];
	u8		wwnn[8];
	__le32		hard_alpa_d_id;
};

/* READ_REV - read the Port revision levels */
enum {
	SLI4_READ_REV_FLAG_SLI_LEVEL	= 0xf,
	SLI4_READ_REV_FLAG_FCOEM	= 0x10,
	SLI4_READ_REV_FLAG_CEEV		= 0x60,
	SLI4_READ_REV_FLAG_VPD		= 0x2000,

	SLI4_READ_REV_AVAILABLE_LENGTH	= 0xffffff,
};

struct sli4_cmd_read_rev {
	struct sli4_mbox_command_header	hdr;
	__le16			resvd0;
	__le16			flags0_word;
	__le32			first_hw_rev;
	__le32			second_hw_rev;
	__le32			resvd12;
	__le32			third_hw_rev;
	u8			fc_ph_low;
	u8			fc_ph_high;
	u8			feature_level_low;
	u8			feature_level_high;
	__le32			resvd24;
	__le32			first_fw_id;
	u8			first_fw_name[16];
	__le32			second_fw_id;
	u8			second_fw_name[16];
	__le32			rsvd18[30];
	__le32			available_length_dword;
	struct sli4_dmaaddr	hostbuf;
	__le32			returned_vpd_length;
	__le32			actual_vpd_length;
};

/* READ_SPARM64 - read the Port service parameters */
#define SLI4_READ_SPARM64_WWPN_OFFSET	(4 * sizeof(u32))
#define SLI4_READ_SPARM64_WWNN_OFFSET	(6 * sizeof(u32))

struct sli4_cmd_read_sparm64 {
	struct sli4_mbox_command_header hdr;
	__le32			resvd0;
	__le32			resvd4;
	struct sli4_bde		bde_64;
	__le16			vpi;
	__le16			resvd22;
	__le16			port_name_start;
	__le16			port_name_len;
	__le16			node_name_start;
	__le16			node_name_len;
};

/* READ_TOPOLOGY - read the link event information */
enum sli4_read_topo_e {
	SLI4_READTOPO_ATTEN_TYPE	= 0xff,
	SLI4_READTOPO_FLAG_IL		= 0x100,
	SLI4_READTOPO_FLAG_PB_RECVD	= 0x200,

	SLI4_READTOPO_LINKSTATE_RECV	= 0x3,
	SLI4_READTOPO_LINKSTATE_TRANS	= 0xc,
	SLI4_READTOPO_LINKSTATE_MACHINE	= 0xf0,
	SLI4_READTOPO_LINKSTATE_SPEED	= 0xff00,
	SLI4_READTOPO_LINKSTATE_TF	= 0x40000000,
	SLI4_READTOPO_LINKSTATE_LU	= 0x80000000,

	SLI4_READTOPO_SCN_BBSCN		= 0xf,
	SLI4_READTOPO_SCN_CBBSCN	= 0xf0,

	SLI4_READTOPO_R_T_TOV		= 0x1ff,
	SLI4_READTOPO_AL_TOV		= 0xf000,

	SLI4_READTOPO_PB_FLAG		= 0x80,

	SLI4_READTOPO_INIT_N_PORTID	= 0xffffff,
};

#define SLI4_MIN_LOOP_MAP_BYTES	128

struct sli4_cmd_read_topology {
	struct sli4_mbox_command_header	hdr;
	__le32			event_tag;
	__le32			dw2_attentype;
	u8			topology;
	u8			lip_type;
	u8			lip_al_ps;
	u8			al_pa_granted;
	struct sli4_bde		bde_loop_map;
	__le32			linkdown_state;
	__le32			currlink_state;
	u8			max_bbc;
	u8			init_bbc;
	u8			scn_flags;
	u8			rsvd39;
	__le16			dw10w0_al_rt_tov;
	__le16			lp_tov;
	u8			acquired_al_pa;
	u8			pb_flags;
	__le16			specified_al_pa;
	__le32			dw12_init_n_port_id;
};

enum sli4_read_topo_link {
	SLI4_READ_TOPOLOGY_LINK_UP	= 0x1,
	SLI4_READ_TOPOLOGY_LINK_DOWN,
	SLI4_READ_TOPOLOGY_LINK_NO_ALPA,
};

enum sli4_read_topo {
	SLI4_READ_TOPO_UNKNOWN		= 0x0,
	SLI4_READ_TOPO_NON_FC_AL,
	SLI4_READ_TOPO_FC_AL,
};

enum sli4_read_topo_speed {
	SLI4_READ_TOPOLOGY_SPEED_NONE	= 0x00,
	SLI4_READ_TOPOLOGY_SPEED_1G	= 0x04,
	SLI4_READ_TOPOLOGY_SPEED_2G	= 0x08,
	SLI4_READ_TOPOLOGY_SPEED_4G	= 0x10,
	SLI4_READ_TOPOLOGY_SPEED_8G	= 0x20,
	SLI4_READ_TOPOLOGY_SPEED_10G	= 0x40,
	SLI4_READ_TOPOLOGY_SPEED_16G	= 0x80,
	SLI4_READ_TOPOLOGY_SPEED_32G	= 0x90,
	SLI4_READ_TOPOLOGY_SPEED_64G	= 0xa0,
	SLI4_READ_TOPOLOGY_SPEED_128G	= 0xb0,
};

/* REG_FCFI - activate a FC Forwarder */
struct sli4_cmd_reg_fcfi_rq_cfg {
	u8	r_ctl_mask;
	u8	r_ctl_match;
	u8	type_mask;
	u8	type_match;
};

enum sli4_regfcfi_tag {
	SLI4_REGFCFI_VLAN_TAG		= 0xfff,
	SLI4_REGFCFI_VLANTAG_VALID	= 0x1000,
};

#define SLI4_CMD_REG_FCFI_NUM_RQ_CFG	4
struct sli4_cmd_reg_fcfi {
	struct sli4_mbox_command_header	hdr;
	__le16		fcf_index;
	__le16		fcfi;
	__le16		rqid1;
	__le16		rqid0;
	__le16		rqid3;
	__le16		rqid2;
	struct sli4_cmd_reg_fcfi_rq_cfg
			rq_cfg[SLI4_CMD_REG_FCFI_NUM_RQ_CFG];
	__le32		dw8_vlan;
};

#define SLI4_CMD_REG_FCFI_MRQ_NUM_RQ_CFG	4
#define SLI4_CMD_REG_FCFI_MRQ_MAX_NUM_RQ	32
#define SLI4_CMD_REG_FCFI_SET_FCFI_MODE		0
#define SLI4_CMD_REG_FCFI_SET_MRQ_MODE		1

enum sli4_reg_fcfi_mrq {
	SLI4_REGFCFI_MRQ_VLAN_TAG	= 0xfff,
	SLI4_REGFCFI_MRQ_VLANTAG_VALID	= 0x1000,
	SLI4_REGFCFI_MRQ_MODE		= 0x2000,

	SLI4_REGFCFI_MRQ_MASK_NUM_PAIRS	= 0xff,
	SLI4_REGFCFI_MRQ_FILTER_BITMASK = 0xf00,
	SLI4_REGFCFI_MRQ_RQ_SEL_POLICY	= 0xf000,
};

struct sli4_cmd_reg_fcfi_mrq {
	struct sli4_mbox_command_header	hdr;
	__le16		fcf_index;
	__le16		fcfi;
	__le16		rqid1;
	__le16		rqid0;
	__le16		rqid3;
	__le16		rqid2;
	struct sli4_cmd_reg_fcfi_rq_cfg
			rq_cfg[SLI4_CMD_REG_FCFI_MRQ_NUM_RQ_CFG];
	__le32		dw8_vlan;
	__le32		dw9_mrqflags;
};

struct sli4_cmd_rq_cfg {
	__le16	rq_id;
	u8	r_ctl_mask;
	u8	r_ctl_match;
	u8	type_mask;
	u8	type_match;
};

/* REG_RPI - register a Remote Port Indicator */
enum sli4_reg_rpi {
	SLI4_REGRPI_REMOTE_N_PORTID	= 0xffffff,	/* DW2 */
	SLI4_REGRPI_UPD			= 0x1000000,
	SLI4_REGRPI_ETOW		= 0x8000000,
	SLI4_REGRPI_TERP		= 0x20000000,
	SLI4_REGRPI_CI			= 0x80000000,
};

struct sli4_cmd_reg_rpi {
	struct sli4_mbox_command_header	hdr;
	__le16			rpi;
	__le16			rsvd2;
	__le32			dw2_rportid_flags;
	struct sli4_bde		bde_64;
	__le16			vpi;
	__le16			rsvd26;
};

#define SLI4_REG_RPI_BUF_LEN		0x70

/* REG_VFI - register a Virtual Fabric Indicator */
enum sli_reg_vfi {
	SLI4_REGVFI_VP			= 0x1000,	/* DW1 */
	SLI4_REGVFI_UPD			= 0x2000,

	SLI4_REGVFI_LOCAL_N_PORTID	= 0xffffff,	/* DW10 */
};

struct sli4_cmd_reg_vfi {
	struct sli4_mbox_command_header	hdr;
	__le16			vfi;
	__le16			dw0w1_flags;
	__le16			fcfi;
	__le16			vpi;
	u8			wwpn[8];
	struct sli4_bde		sparm;
	__le32			e_d_tov;
	__le32			r_a_tov;
	__le32			dw10_lportid_flags;
};

/* REG_VPI - register a Virtual Port Indicator */
enum sli4_reg_vpi {
	SLI4_REGVPI_LOCAL_N_PORTID	= 0xffffff,
	SLI4_REGVPI_UPD			= 0x1000000,
};

struct sli4_cmd_reg_vpi {
	struct sli4_mbox_command_header	hdr;
	__le32		rsvd0;
	__le32		dw2_lportid_flags;
	u8		wwpn[8];
	__le32		rsvd12;
	__le16		vpi;
	__le16		vfi;
};

/* REQUEST_FEATURES - request / query SLI features */
enum sli4_req_features_flags {
	SLI4_REQFEAT_QRY	= 0x1,		/* Dw1 */

	SLI4_REQFEAT_IAAB	= 1 << 0,	/* DW2 & DW3 */
	SLI4_REQFEAT_NPIV	= 1 << 1,
	SLI4_REQFEAT_DIF	= 1 << 2,
	SLI4_REQFEAT_VF		= 1 << 3,
	SLI4_REQFEAT_FCPI	= 1 << 4,
	SLI4_REQFEAT_FCPT	= 1 << 5,
	SLI4_REQFEAT_FCPC	= 1 << 6,
	SLI4_REQFEAT_RSVD	= 1 << 7,
	SLI4_REQFEAT_RQD	= 1 << 8,
	SLI4_REQFEAT_IAAR	= 1 << 9,
	SLI4_REQFEAT_HLM	= 1 << 10,
	SLI4_REQFEAT_PERFH	= 1 << 11,
	SLI4_REQFEAT_RXSEQ	= 1 << 12,
	SLI4_REQFEAT_RXRI	= 1 << 13,
	SLI4_REQFEAT_DCL2	= 1 << 14,
	SLI4_REQFEAT_RSCO	= 1 << 15,
	SLI4_REQFEAT_MRQP	= 1 << 16,
};

struct sli4_cmd_request_features {
	struct sli4_mbox_command_header	hdr;
	__le32		dw1_qry;
	__le32		cmd;
	__le32		resp;
};

/*
 * SLI_CONFIG - submit a configuration command to Port
 *
 * Command is either embedded as part of the payload (embed) or located
 * in a separate memory buffer (mem)
 */
enum sli4_sli_config {
	SLI4_SLICONF_EMB		= 0x1,		/* DW1 */
	SLI4_SLICONF_PMDCMD_SHIFT	= 3,
	SLI4_SLICONF_PMDCMD_MASK	= 0xf8,
	SLI4_SLICONF_PMDCMD_VAL_1	= 8,
	SLI4_SLICONF_PMDCNT		= 0xf8,

	SLI4_SLICONF_PMD_LEN		= 0x00ffffff,
};

struct sli4_cmd_sli_config {
	struct sli4_mbox_command_header	hdr;
	__le32		dw1_flags;
	__le32		payload_len;
	__le32		rsvd12[3];
	union {
		u8 embed[58 * sizeof(u32)];
		struct sli4_bufptr mem;
	} payload;
};

/* READ_STATUS - read tx/rx status of a particular port */
#define SLI4_READSTATUS_CLEAR_COUNTERS	0x1

struct sli4_cmd_read_status {
	struct sli4_mbox_command_header	hdr;
	__le32		dw1_flags;
	__le32		rsvd4;
	__le32		trans_kbyte_cnt;
	__le32		recv_kbyte_cnt;
	__le32		trans_frame_cnt;
	__le32		recv_frame_cnt;
	__le32		trans_seq_cnt;
	__le32		recv_seq_cnt;
	__le32		tot_exchanges_orig;
	__le32		tot_exchanges_resp;
	__le32		recv_p_bsy_cnt;
	__le32		recv_f_bsy_cnt;
	__le32		no_rq_buf_dropped_frames_cnt;
	__le32		empty_rq_timeout_cnt;
	__le32		no_xri_dropped_frames_cnt;
	__le32		empty_xri_pool_cnt;
};

/* READ_LNK_STAT - read link status of a particular port */
enum sli4_read_link_stats_flags {
	SLI4_READ_LNKSTAT_REC	= 1u << 0,
	SLI4_READ_LNKSTAT_GEC	= 1u << 1,
	SLI4_READ_LNKSTAT_W02OF	= 1u << 2,
	SLI4_READ_LNKSTAT_W03OF	= 1u << 3,
	SLI4_READ_LNKSTAT_W04OF	= 1u << 4,
	SLI4_READ_LNKSTAT_W05OF	= 1u << 5,
	SLI4_READ_LNKSTAT_W06OF	= 1u << 6,
	SLI4_READ_LNKSTAT_W07OF	= 1u << 7,
	SLI4_READ_LNKSTAT_W08OF	= 1u << 8,
	SLI4_READ_LNKSTAT_W09OF	= 1u << 9,
	SLI4_READ_LNKSTAT_W10OF = 1u << 10,
	SLI4_READ_LNKSTAT_W11OF = 1u << 11,
	SLI4_READ_LNKSTAT_W12OF	= 1u << 12,
	SLI4_READ_LNKSTAT_W13OF	= 1u << 13,
	SLI4_READ_LNKSTAT_W14OF	= 1u << 14,
	SLI4_READ_LNKSTAT_W15OF	= 1u << 15,
	SLI4_READ_LNKSTAT_W16OF	= 1u << 16,
	SLI4_READ_LNKSTAT_W17OF	= 1u << 17,
	SLI4_READ_LNKSTAT_W18OF	= 1u << 18,
	SLI4_READ_LNKSTAT_W19OF	= 1u << 19,
	SLI4_READ_LNKSTAT_W20OF	= 1u << 20,
	SLI4_READ_LNKSTAT_W21OF	= 1u << 21,
	SLI4_READ_LNKSTAT_CLRC	= 1u << 30,
	SLI4_READ_LNKSTAT_CLOF	= 1u << 31,
};

struct sli4_cmd_read_link_stats {
	struct sli4_mbox_command_header	hdr;
	__le32	dw1_flags;
	__le32	linkfail_errcnt;
	__le32	losssync_errcnt;
	__le32	losssignal_errcnt;
	__le32	primseq_errcnt;
	__le32	inval_txword_errcnt;
	__le32	crc_errcnt;
	__le32	primseq_eventtimeout_cnt;
	__le32	elastic_bufoverrun_errcnt;
	__le32	arbit_fc_al_timeout_cnt;
	__le32	adv_rx_buftor_to_buf_credit;
	__le32	curr_rx_buf_to_buf_credit;
	__le32	adv_tx_buf_to_buf_credit;
	__le32	curr_tx_buf_to_buf_credit;
	__le32	rx_eofa_cnt;
	__le32	rx_eofdti_cnt;
	__le32	rx_eofni_cnt;
	__le32	rx_soff_cnt;
	__le32	rx_dropped_no_aer_cnt;
	__le32	rx_dropped_no_avail_rpi_rescnt;
	__le32	rx_dropped_no_avail_xri_rescnt;
};

/* Format a WQE with WQ_ID Association performance hint */
static inline void
sli_set_wq_id_association(void *entry, u16 q_id)
{
	u32 *wqe = entry;

	/*
	 * Set Word 10, bit 0 to zero
	 * Set Word 10, bits 15:1 to the WQ ID
	 */
	wqe[10] &= ~0xffff;
	wqe[10] |= q_id << 1;
}

/* UNREG_FCFI - unregister a FCFI */
struct sli4_cmd_unreg_fcfi {
	struct sli4_mbox_command_header	hdr;
	__le32		rsvd0;
	__le16		fcfi;
	__le16		rsvd6;
};

/* UNREG_RPI - unregister one or more RPI */
enum sli4_unreg_rpi {
	SLI4_UNREG_RPI_DP	= 0x2000,
	SLI4_UNREG_RPI_II_SHIFT	= 14,
	SLI4_UNREG_RPI_II_MASK	= 0xc000,
	SLI4_UNREG_RPI_II_RPI	= 0x0000,
	SLI4_UNREG_RPI_II_VPI	= 0x4000,
	SLI4_UNREG_RPI_II_VFI	= 0x8000,
	SLI4_UNREG_RPI_II_FCFI	= 0xc000,

	SLI4_UNREG_RPI_DEST_N_PORTID_MASK = 0x00ffffff,
};

struct sli4_cmd_unreg_rpi {
	struct sli4_mbox_command_header	hdr;
	__le16		index;
	__le16		dw1w1_flags;
	__le32		dw2_dest_n_portid;
};

/* UNREG_VFI - unregister one or more VFI */
enum sli4_unreg_vfi {
	SLI4_UNREG_VFI_II_SHIFT	= 14,
	SLI4_UNREG_VFI_II_MASK	= 0xc000,
	SLI4_UNREG_VFI_II_VFI	= 0x0000,
	SLI4_UNREG_VFI_II_FCFI	= 0xc000,
};

struct sli4_cmd_unreg_vfi {
	struct sli4_mbox_command_header	hdr;
	__le32		rsvd0;
	__le16		index;
	__le16		dw2_flags;
};

enum sli4_unreg_type {
	SLI4_UNREG_TYPE_PORT,
	SLI4_UNREG_TYPE_DOMAIN,
	SLI4_UNREG_TYPE_FCF,
	SLI4_UNREG_TYPE_ALL
};

/* UNREG_VPI - unregister one or more VPI */
enum sli4_unreg_vpi {
	SLI4_UNREG_VPI_II_SHIFT	= 14,
	SLI4_UNREG_VPI_II_MASK	= 0xc000,
	SLI4_UNREG_VPI_II_VPI	= 0x0000,
	SLI4_UNREG_VPI_II_VFI	= 0x8000,
	SLI4_UNREG_VPI_II_FCFI	= 0xc000,
};

struct sli4_cmd_unreg_vpi {
	struct sli4_mbox_command_header	hdr;
	__le32		rsvd0;
	__le16		index;
	__le16		dw2w0_flags;
};

/* AUTO_XFER_RDY - Configure the auto-generate XFER-RDY feature */
struct sli4_cmd_config_auto_xfer_rdy {
	struct sli4_mbox_command_header	hdr;
	__le32		rsvd0;
	__le32		max_burst_len;
};

#define SLI4_CONFIG_AUTO_XFERRDY_BLKSIZE	0xffff

struct sli4_cmd_config_auto_xfer_rdy_hp {
	struct sli4_mbox_command_header	hdr;
	__le32		rsvd0;
	__le32		max_burst_len;
	__le32		dw3_esoc_flags;
	__le16		block_size;
	__le16		rsvd14;
};

/*************************************************************************
 * SLI-4 common configuration command formats and definitions
 */

/*
 * Subsystem values.
 */
enum sli4_subsystem {
	SLI4_SUBSYSTEM_COMMON	= 0x01,
	SLI4_SUBSYSTEM_LOWLEVEL	= 0x0b,
	SLI4_SUBSYSTEM_FC	= 0x0c,
	SLI4_SUBSYSTEM_DMTF	= 0x11,
};

#define	SLI4_OPC_LOWLEVEL_SET_WATCHDOG		0X36

/*
 * Common opcode (OPC) values.
 */
enum sli4_cmn_opcode {
	SLI4_CMN_FUNCTION_RESET		= 0x3d,
	SLI4_CMN_CREATE_CQ		= 0x0c,
	SLI4_CMN_CREATE_CQ_SET		= 0x1d,
	SLI4_CMN_DESTROY_CQ		= 0x36,
	SLI4_CMN_MODIFY_EQ_DELAY	= 0x29,
	SLI4_CMN_CREATE_EQ		= 0x0d,
	SLI4_CMN_DESTROY_EQ		= 0x37,
	SLI4_CMN_CREATE_MQ_EXT		= 0x5a,
	SLI4_CMN_DESTROY_MQ		= 0x35,
	SLI4_CMN_GET_CNTL_ATTRIBUTES	= 0x20,
	SLI4_CMN_NOP			= 0x21,
	SLI4_CMN_GET_RSC_EXTENT_INFO	= 0x9a,
	SLI4_CMN_GET_SLI4_PARAMS	= 0xb5,
	SLI4_CMN_QUERY_FW_CONFIG	= 0x3a,
	SLI4_CMN_GET_PORT_NAME		= 0x4d,

	SLI4_CMN_WRITE_FLASHROM		= 0x07,
	/* TRANSCEIVER Data */
	SLI4_CMN_READ_TRANS_DATA	= 0x49,
	SLI4_CMN_GET_CNTL_ADDL_ATTRS	= 0x79,
	SLI4_CMN_GET_FUNCTION_CFG	= 0xa0,
	SLI4_CMN_GET_PROFILE_CFG	= 0xa4,
	SLI4_CMN_SET_PROFILE_CFG	= 0xa5,
	SLI4_CMN_GET_PROFILE_LIST	= 0xa6,
	SLI4_CMN_GET_ACTIVE_PROFILE	= 0xa7,
	SLI4_CMN_SET_ACTIVE_PROFILE	= 0xa8,
	SLI4_CMN_READ_OBJECT		= 0xab,
	SLI4_CMN_WRITE_OBJECT		= 0xac,
	SLI4_CMN_DELETE_OBJECT		= 0xae,
	SLI4_CMN_READ_OBJECT_LIST	= 0xad,
	SLI4_CMN_SET_DUMP_LOCATION	= 0xb8,
	SLI4_CMN_SET_FEATURES		= 0xbf,
	SLI4_CMN_GET_RECFG_LINK_INFO	= 0xc9,
	SLI4_CMN_SET_RECNG_LINK_ID	= 0xca,
};

/* DMTF opcode (OPC) values */
#define DMTF_EXEC_CLP_CMD 0x01

/*
 * COMMON_FUNCTION_RESET
 *
 * Resets the Port, returning it to a power-on state. This configuration
 * command does not have a payload and should set/expect the lengths to
 * be zero.
 */
struct sli4_rqst_cmn_function_reset {
	struct sli4_rqst_hdr	hdr;
};

struct sli4_rsp_cmn_function_reset {
	struct sli4_rsp_hdr	hdr;
};

/*
 * COMMON_GET_CNTL_ATTRIBUTES
 *
 * Query for information about the SLI Port
 */
enum sli4_cntrl_attr_flags {
	SLI4_CNTL_ATTR_PORTNUM	= 0x3f,
	SLI4_CNTL_ATTR_PORTTYPE	= 0xc0,
};

struct sli4_rsp_cmn_get_cntl_attributes {
	struct sli4_rsp_hdr	hdr;
	u8		version_str[32];
	u8		manufacturer_name[32];
	__le32		supported_modes;
	u8		eprom_version_lo;
	u8		eprom_version_hi;
	__le16		rsvd17;
	__le32		mbx_ds_version;
	__le32		ep_fw_ds_version;
	u8		ncsi_version_str[12];
	__le32		def_extended_timeout;
	u8		model_number[32];
	u8		description[64];
	u8		serial_number[32];
	u8		ip_version_str[32];
	u8		fw_version_str[32];
	u8		bios_version_str[32];
	u8		redboot_version_str[32];
	u8		driver_version_str[32];
	u8		fw_on_flash_version_str[32];
	__le32		functionalities_supported;
	__le16		max_cdb_length;
	u8		asic_revision;
	u8		generational_guid0;
	__le32		generational_guid1_12[3];
	__le16		generational_guid13_14;
	u8		generational_guid15;
	u8		hba_port_count;
	__le16		default_link_down_timeout;
	u8		iscsi_version_min_max;
	u8		multifunctional_device;
	u8		cache_valid;
	u8		hba_status;
	u8		max_domains_supported;
	u8		port_num_type_flags;
	__le32		firmware_post_status;
	__le32		hba_mtu;
	u8		iscsi_features;
	u8		rsvd121[3];
	__le16		pci_vendor_id;
	__le16		pci_device_id;
	__le16		pci_sub_vendor_id;
	__le16		pci_sub_system_id;
	u8		pci_bus_number;
	u8		pci_device_number;
	u8		pci_function_number;
	u8		interface_type;
	__le64		unique_identifier;
	u8		number_of_netfilters;
	u8		rsvd122[3];
};

/*
 * COMMON_GET_CNTL_ATTRIBUTES
 *
 * This command queries the controller information from the Flash ROM.
 */
struct sli4_rqst_cmn_get_cntl_addl_attributes {
	struct sli4_rqst_hdr	hdr;
};

struct sli4_rsp_cmn_get_cntl_addl_attributes {
	struct sli4_rsp_hdr	hdr;
	__le16		ipl_file_number;
	u8		ipl_file_version;
	u8		rsvd4;
	u8		on_die_temperature;
	u8		rsvd5[3];
	__le32		driver_advanced_features_supported;
	__le32		rsvd7[4];
	char		universal_bios_version[32];
	char		x86_bios_version[32];
	char		efi_bios_version[32];
	char		fcode_version[32];
	char		uefi_bios_version[32];
	char		uefi_nic_version[32];
	char		uefi_fcode_version[32];
	char		uefi_iscsi_version[32];
	char		iscsi_x86_bios_version[32];
	char		pxe_x86_bios_version[32];
	u8		default_wwpn[8];
	u8		ext_phy_version[32];
	u8		fc_universal_bios_version[32];
	u8		fc_x86_bios_version[32];
	u8		fc_efi_bios_version[32];
	u8		fc_fcode_version[32];
	u8		ext_phy_crc_label[8];
	u8		ipl_file_name[16];
	u8		rsvd139[72];
};

/*
 * COMMON_NOP
 *
 * This command does not do anything; it only returns
 * the payload in the completion.
 */
struct sli4_rqst_cmn_nop {
	struct sli4_rqst_hdr	hdr;
	__le32			context[2];
};

struct sli4_rsp_cmn_nop {
	struct sli4_rsp_hdr	hdr;
	__le32			context[2];
};

struct sli4_rqst_cmn_get_resource_extent_info {
	struct sli4_rqst_hdr	hdr;
	__le16	resource_type;
	__le16	rsvd16;
};

enum sli4_rsc_type {
	SLI4_RSC_TYPE_VFI	= 0x20,
	SLI4_RSC_TYPE_VPI	= 0x21,
	SLI4_RSC_TYPE_RPI	= 0x22,
	SLI4_RSC_TYPE_XRI	= 0x23,
};

struct sli4_rsp_cmn_get_resource_extent_info {
	struct sli4_rsp_hdr	hdr;
	__le16		resource_extent_count;
	__le16		resource_extent_size;
};

#define SLI4_128BYTE_WQE_SUPPORT	0x02

#define GET_Q_CNT_METHOD(m) \
	(((m) & SLI4_PARAM_Q_CNT_MTHD_MASK) >> SLI4_PARAM_Q_CNT_MTHD_SHFT)
#define GET_Q_CREATE_VERSION(v) \
	(((v) & SLI4_PARAM_QV_MASK) >> SLI4_PARAM_QV_SHIFT)

enum sli4_rsp_get_params_e {
	/*GENERIC*/
	SLI4_PARAM_Q_CNT_MTHD_SHFT	= 24,
	SLI4_PARAM_Q_CNT_MTHD_MASK	= 0xf << 24,
	SLI4_PARAM_QV_SHIFT		= 14,
	SLI4_PARAM_QV_MASK		= 3 << 14,

	/* DW4 */
	SLI4_PARAM_PROTO_TYPE_MASK	= 0xff,
	/* DW5 */
	SLI4_PARAM_FT			= 1 << 0,
	SLI4_PARAM_SLI_REV_MASK		= 0xf << 4,
	SLI4_PARAM_SLI_FAM_MASK		= 0xf << 8,
	SLI4_PARAM_IF_TYPE_MASK		= 0xf << 12,
	SLI4_PARAM_SLI_HINT1_MASK	= 0xff << 16,
	SLI4_PARAM_SLI_HINT2_MASK	= 0x1f << 24,
	/* DW6 */
	SLI4_PARAM_EQ_PAGE_CNT_MASK	= 0xf << 0,
	SLI4_PARAM_EQE_SZS_MASK		= 0xf << 8,
	SLI4_PARAM_EQ_PAGE_SZS_MASK	= 0xff << 16,
	/* DW8 */
	SLI4_PARAM_CQ_PAGE_CNT_MASK	= 0xf << 0,
	SLI4_PARAM_CQE_SZS_MASK		= 0xf << 8,
	SLI4_PARAM_CQ_PAGE_SZS_MASK	= 0xff << 16,
	/* DW10 */
	SLI4_PARAM_MQ_PAGE_CNT_MASK	= 0xf << 0,
	SLI4_PARAM_MQ_PAGE_SZS_MASK	= 0xff << 16,
	/* DW12 */
	SLI4_PARAM_WQ_PAGE_CNT_MASK	= 0xf << 0,
	SLI4_PARAM_WQE_SZS_MASK		= 0xf << 8,
	SLI4_PARAM_WQ_PAGE_SZS_MASK	= 0xff << 16,
	/* DW14 */
	SLI4_PARAM_RQ_PAGE_CNT_MASK	= 0xf << 0,
	SLI4_PARAM_RQE_SZS_MASK		= 0xf << 8,
	SLI4_PARAM_RQ_PAGE_SZS_MASK	= 0xff << 16,
	/* DW15W1*/
	SLI4_PARAM_RQ_DB_WINDOW_MASK	= 0xf000,
	/* DW16 */
	SLI4_PARAM_FC			= 1 << 0,
	SLI4_PARAM_EXT			= 1 << 1,
	SLI4_PARAM_HDRR			= 1 << 2,
	SLI4_PARAM_SGLR			= 1 << 3,
	SLI4_PARAM_FBRR			= 1 << 4,
	SLI4_PARAM_AREG			= 1 << 5,
	SLI4_PARAM_TGT			= 1 << 6,
	SLI4_PARAM_TERP			= 1 << 7,
	SLI4_PARAM_ASSI			= 1 << 8,
	SLI4_PARAM_WCHN			= 1 << 9,
	SLI4_PARAM_TCCA			= 1 << 10,
	SLI4_PARAM_TRTY			= 1 << 11,
	SLI4_PARAM_TRIR			= 1 << 12,
	SLI4_PARAM_PHOFF		= 1 << 13,
	SLI4_PARAM_PHON			= 1 << 14,
	SLI4_PARAM_PHWQ			= 1 << 15,
	SLI4_PARAM_BOUND_4GA		= 1 << 16,
	SLI4_PARAM_RXC			= 1 << 17,
	SLI4_PARAM_HLM			= 1 << 18,
	SLI4_PARAM_IPR			= 1 << 19,
	SLI4_PARAM_RXRI			= 1 << 20,
	SLI4_PARAM_SGLC			= 1 << 21,
	SLI4_PARAM_TIMM			= 1 << 22,
	SLI4_PARAM_TSMM			= 1 << 23,
	SLI4_PARAM_OAS			= 1 << 25,
	SLI4_PARAM_LC			= 1 << 26,
	SLI4_PARAM_AGXF			= 1 << 27,
	SLI4_PARAM_LOOPBACK_MASK	= 0xf << 28,
	/* DW18 */
	SLI4_PARAM_SGL_PAGE_CNT_MASK	= 0xf << 0,
	SLI4_PARAM_SGL_PAGE_SZS_MASK	= 0xff << 8,
	SLI4_PARAM_SGL_PP_ALIGN_MASK	= 0xff << 16,
};

struct sli4_rqst_cmn_get_sli4_params {
	struct sli4_rqst_hdr	hdr;
};

struct sli4_rsp_cmn_get_sli4_params {
	struct sli4_rsp_hdr	hdr;
	__le32		dw4_protocol_type;
	__le32		dw5_sli;
	__le32		dw6_eq_page_cnt;
	__le16		eqe_count_mask;
	__le16		rsvd26;
	__le32		dw8_cq_page_cnt;
	__le16		cqe_count_mask;
	__le16		rsvd34;
	__le32		dw10_mq_page_cnt;
	__le16		mqe_count_mask;
	__le16		rsvd42;
	__le32		dw12_wq_page_cnt;
	__le16		wqe_count_mask;
	__le16		rsvd50;
	__le32		dw14_rq_page_cnt;
	__le16		rqe_count_mask;
	__le16		dw15w1_rq_db_window;
	__le32		dw16_loopback_scope;
	__le32		sge_supported_length;
	__le32		dw18_sgl_page_cnt;
	__le16		min_rq_buffer_size;
	__le16		rsvd75;
	__le32		max_rq_buffer_size;
	__le16		physical_xri_max;
	__le16		physical_rpi_max;
	__le16		physical_vpi_max;
	__le16		physical_vfi_max;
	__le32		rsvd88;
	__le16		frag_num_field_offset;
	__le16		frag_num_field_size;
	__le16		sgl_index_field_offset;
	__le16		sgl_index_field_size;
	__le32		chain_sge_initial_value_lo;
	__le32		chain_sge_initial_value_hi;
};

/*Port Types*/
enum sli4_port_types {
	SLI4_PORT_TYPE_ETH	= 0,
	SLI4_PORT_TYPE_FC	= 1,
};

struct sli4_rqst_cmn_get_port_name {
	struct sli4_rqst_hdr	hdr;
	u8	port_type;
	u8	rsvd4[3];
};

struct sli4_rsp_cmn_get_port_name {
	struct sli4_rsp_hdr	hdr;
	char	port_name[4];
};

struct sli4_rqst_cmn_write_flashrom {
	struct sli4_rqst_hdr	hdr;
	__le32		flash_rom_access_opcode;
	__le32		flash_rom_access_operation_type;
	__le32		data_buffer_size;
	__le32		offset;
	u8		data_buffer[4];
};

/*
 * COMMON_READ_TRANSCEIVER_DATA
 *
 * This command reads SFF transceiver data(Format is defined
 * by the SFF-8472 specification).
 */
struct sli4_rqst_cmn_read_transceiver_data {
	struct sli4_rqst_hdr	hdr;
	__le32			page_number;
	__le32			port;
};

struct sli4_rsp_cmn_read_transceiver_data {
	struct sli4_rsp_hdr	hdr;
	__le32			page_number;
	__le32			port;
	u8			page_data[128];
	u8			page_data_2[128];
};

#define SLI4_REQ_DESIRE_READLEN		0xffffff

struct sli4_rqst_cmn_read_object {
	struct sli4_rqst_hdr	hdr;
	__le32			desired_read_length_dword;
	__le32			read_offset;
	u8			object_name[104];
	__le32			host_buffer_descriptor_count;
	struct sli4_bde		host_buffer_descriptor[0];
};

#define RSP_COM_READ_OBJ_EOF		0x80000000

struct sli4_rsp_cmn_read_object {
	struct sli4_rsp_hdr	hdr;
	__le32			actual_read_length;
	__le32			eof_dword;
};

enum sli4_rqst_write_object_flags {
	SLI4_RQ_DES_WRITE_LEN		= 0xffffff,
	SLI4_RQ_DES_WRITE_LEN_NOC	= 0x40000000,
	SLI4_RQ_DES_WRITE_LEN_EOF	= 0x80000000,
};

struct sli4_rqst_cmn_write_object {
	struct sli4_rqst_hdr	hdr;
	__le32			desired_write_len_dword;
	__le32			write_offset;
	u8			object_name[104];
	__le32			host_buffer_descriptor_count;
	struct sli4_bde		host_buffer_descriptor[0];
};

#define	RSP_CHANGE_STATUS		0xff

struct sli4_rsp_cmn_write_object {
	struct sli4_rsp_hdr	hdr;
	__le32			actual_write_length;
	__le32			change_status_dword;
};

struct sli4_rqst_cmn_delete_object {
	struct sli4_rqst_hdr	hdr;
	__le32			rsvd4;
	__le32			rsvd5;
	u8			object_name[104];
};

#define SLI4_RQ_OBJ_LIST_READ_LEN	0xffffff

struct sli4_rqst_cmn_read_object_list {
	struct sli4_rqst_hdr	hdr;
	__le32			desired_read_length_dword;
	__le32			read_offset;
	u8			object_name[104];
	__le32			host_buffer_descriptor_count;
	struct sli4_bde		host_buffer_descriptor[0];
};

enum sli4_rqst_set_dump_flags {
	SLI4_CMN_SET_DUMP_BUFFER_LEN	= 0xffffff,
	SLI4_CMN_SET_DUMP_FDB		= 0x20000000,
	SLI4_CMN_SET_DUMP_BLP		= 0x40000000,
	SLI4_CMN_SET_DUMP_QRY		= 0x80000000,
};

struct sli4_rqst_cmn_set_dump_location {
	struct sli4_rqst_hdr	hdr;
	__le32			buffer_length_dword;
	__le32			buf_addr_low;
	__le32			buf_addr_high;
};

struct sli4_rsp_cmn_set_dump_location {
	struct sli4_rsp_hdr	hdr;
	__le32			buffer_length_dword;
};

enum sli4_dump_level {
	SLI4_DUMP_LEVEL_NONE,
	SLI4_CHIP_LEVEL_DUMP,
	SLI4_FUNC_DESC_DUMP,
};

enum sli4_dump_state {
	SLI4_DUMP_STATE_NONE,
	SLI4_CHIP_DUMP_STATE_VALID,
	SLI4_FUNC_DUMP_STATE_VALID,
};

enum sli4_dump_status {
	SLI4_DUMP_READY_STATUS_NOT_READY,
	SLI4_DUMP_READY_STATUS_DD_PRESENT,
	SLI4_DUMP_READY_STATUS_FDB_PRESENT,
	SLI4_DUMP_READY_STATUS_SKIP_DUMP,
	SLI4_DUMP_READY_STATUS_FAILED = -1,
};

enum sli4_set_features {
	SLI4_SET_FEATURES_DIF_SEED			= 0x01,
	SLI4_SET_FEATURES_XRI_TIMER			= 0x03,
	SLI4_SET_FEATURES_MAX_PCIE_SPEED		= 0x04,
	SLI4_SET_FEATURES_FCTL_CHECK			= 0x05,
	SLI4_SET_FEATURES_FEC				= 0x06,
	SLI4_SET_FEATURES_PCIE_RECV_DETECT		= 0x07,
	SLI4_SET_FEATURES_DIF_MEMORY_MODE		= 0x08,
	SLI4_SET_FEATURES_DISABLE_SLI_PORT_PAUSE_STATE	= 0x09,
	SLI4_SET_FEATURES_ENABLE_PCIE_OPTIONS		= 0x0a,
	SLI4_SET_FEAT_CFG_AUTO_XFER_RDY_T10PI		= 0x0c,
	SLI4_SET_FEATURES_ENABLE_MULTI_RECEIVE_QUEUE	= 0x0d,
	SLI4_SET_FEATURES_SET_FTD_XFER_HINT		= 0x0f,
	SLI4_SET_FEATURES_SLI_PORT_HEALTH_CHECK		= 0x11,
};

struct sli4_rqst_cmn_set_features {
	struct sli4_rqst_hdr	hdr;
	__le32			feature;
	__le32			param_len;
	__le32			params[8];
};

struct sli4_rqst_cmn_set_features_dif_seed {
	__le16		seed;
	__le16		rsvd16;
};

enum sli4_rqst_set_mrq_features {
	SLI4_RQ_MULTIRQ_ISR		 = 0x1,
	SLI4_RQ_MULTIRQ_AUTOGEN_XFER_RDY = 0x2,

	SLI4_RQ_MULTIRQ_NUM_RQS		 = 0xff,
	SLI4_RQ_MULTIRQ_RQ_SELECT	 = 0xf00,
};

struct sli4_rqst_cmn_set_features_multirq {
	__le32		auto_gen_xfer_dword;
	__le32		num_rqs_dword;
};

enum sli4_rqst_health_check_flags {
	SLI4_RQ_HEALTH_CHECK_ENABLE	= 0x1,
	SLI4_RQ_HEALTH_CHECK_QUERY	= 0x2,
};

struct sli4_rqst_cmn_set_features_health_check {
	__le32		health_check_dword;
};

struct sli4_rqst_cmn_set_features_set_fdt_xfer_hint {
	__le32		fdt_xfer_hint;
};

struct sli4_rqst_dmtf_exec_clp_cmd {
	struct sli4_rqst_hdr	hdr;
	__le32			cmd_buf_length;
	__le32			resp_buf_length;
	__le32			cmd_buf_addr_low;
	__le32			cmd_buf_addr_high;
	__le32			resp_buf_addr_low;
	__le32			resp_buf_addr_high;
};

struct sli4_rsp_dmtf_exec_clp_cmd {
	struct sli4_rsp_hdr	hdr;
	__le32			rsvd4;
	__le32			resp_length;
	__le32			rsvd6;
	__le32			rsvd7;
	__le32			rsvd8;
	__le32			rsvd9;
	__le32			clp_status;
	__le32			clp_detailed_status;
};

#define SLI4_PROTOCOL_FC		0x10
#define SLI4_PROTOCOL_DEFAULT		0xff

struct sli4_rspource_descriptor_v1 {
	u8		descriptor_type;
	u8		descriptor_length;
	__le16		rsvd16;
	__le32		type_specific[0];
};

enum sli4_pcie_desc_flags {
	SLI4_PCIE_DESC_IMM		= 0x4000,
	SLI4_PCIE_DESC_NOSV		= 0x8000,

	SLI4_PCIE_DESC_PF_NO		= 0x3ff0000,

	SLI4_PCIE_DESC_MISSN_ROLE	= 0xff,
	SLI4_PCIE_DESC_PCHG		= 0x8000000,
	SLI4_PCIE_DESC_SCHG		= 0x10000000,
	SLI4_PCIE_DESC_XCHG		= 0x20000000,
	SLI4_PCIE_DESC_XROM		= 0xc0000000
};

struct sli4_pcie_resource_descriptor_v1 {
	u8		descriptor_type;
	u8		descriptor_length;
	__le16		imm_nosv_dword;
	__le32		pf_number_dword;
	__le32		rsvd3;
	u8		sriov_state;
	u8		pf_state;
	u8		pf_type;
	u8		rsvd4;
	__le16		number_of_vfs;
	__le16		rsvd5;
	__le32		mission_roles_dword;
	__le32		rsvd7[16];
};

struct sli4_rqst_cmn_get_function_config {
	struct sli4_rqst_hdr  hdr;
};

struct sli4_rsp_cmn_get_function_config {
	struct sli4_rsp_hdr	hdr;
	__le32			desc_count;
	__le32			desc[54];
};

/* Link Config Descriptor for link config functions */
struct sli4_link_config_descriptor {
	u8		link_config_id;
	u8		rsvd1[3];
	__le32		config_description[8];
};

#define MAX_LINK_DES	10

struct sli4_rqst_cmn_get_reconfig_link_info {
	struct sli4_rqst_hdr  hdr;
};

struct sli4_rsp_cmn_get_reconfig_link_info {
	struct sli4_rsp_hdr	hdr;
	u8			active_link_config_id;
	u8			rsvd17;
	u8			next_link_config_id;
	u8			rsvd19;
	__le32			link_configuration_descriptor_count;
	struct sli4_link_config_descriptor
				desc[MAX_LINK_DES];
};

enum sli4_set_reconfig_link_flags {
	SLI4_SET_RECONFIG_LINKID_NEXT	= 0xff,
	SLI4_SET_RECONFIG_LINKID_FD	= 1u << 31,
};

struct sli4_rqst_cmn_set_reconfig_link_id {
	struct sli4_rqst_hdr  hdr;
	__le32			dw4_flags;
};

struct sli4_rsp_cmn_set_reconfig_link_id {
	struct sli4_rsp_hdr	hdr;
};

struct sli4_rqst_lowlevel_set_watchdog {
	struct sli4_rqst_hdr	hdr;
	__le16			watchdog_timeout;
	__le16			rsvd18;
};

struct sli4_rsp_lowlevel_set_watchdog {
	struct sli4_rsp_hdr	hdr;
	__le32			rsvd;
};

/* FC opcode (OPC) values */
enum sli4_fc_opcodes {
	SLI4_OPC_WQ_CREATE		= 0x1,
	SLI4_OPC_WQ_DESTROY		= 0x2,
	SLI4_OPC_POST_SGL_PAGES		= 0x3,
	SLI4_OPC_RQ_CREATE		= 0x5,
	SLI4_OPC_RQ_DESTROY		= 0x6,
	SLI4_OPC_READ_FCF_TABLE		= 0x8,
	SLI4_OPC_POST_HDR_TEMPLATES	= 0xb,
	SLI4_OPC_REDISCOVER_FCF		= 0x10,
};

/* Use the default CQ associated with the WQ */
#define SLI4_CQ_DEFAULT 0xffff

/*
 * POST_SGL_PAGES
 *
 * Register the scatter gather list (SGL) memory and
 * associate it with an XRI.
 */
struct sli4_rqst_post_sgl_pages {
	struct sli4_rqst_hdr	hdr;
	__le16			xri_start;
	__le16			xri_count;
	struct {
		__le32		page0_low;
		__le32		page0_high;
		__le32		page1_low;
		__le32		page1_high;
	} page_set[10];
};

struct sli4_rsp_post_sgl_pages {
	struct sli4_rsp_hdr	hdr;
};

struct sli4_rqst_post_hdr_templates {
	struct sli4_rqst_hdr	hdr;
	__le16			rpi_offset;
	__le16			page_count;
	struct sli4_dmaaddr	page_descriptor[0];
};

#define SLI4_HDR_TEMPLATE_SIZE		64

enum sli4_io_flags {
/* The XRI associated with this IO is already active */
	SLI4_IO_CONTINUATION		= 1 << 0,
/* Automatically generate a good RSP frame */
	SLI4_IO_AUTO_GOOD_RESPONSE	= 1 << 1,
	SLI4_IO_NO_ABORT		= 1 << 2,
/* Set the DNRX bit because no auto xref rdy buffer is posted */
	SLI4_IO_DNRX			= 1 << 3,
};

enum sli4_callback {
	SLI4_CB_LINK,
	SLI4_CB_MAX,
};

enum sli4_link_status {
	SLI4_LINK_STATUS_UP,
	SLI4_LINK_STATUS_DOWN,
	SLI4_LINK_STATUS_NO_ALPA,
	SLI4_LINK_STATUS_MAX,
};

enum sli4_link_topology {
	SLI4_LINK_TOPO_NON_FC_AL = 1,
	SLI4_LINK_TOPO_FC_AL,
	SLI4_LINK_TOPO_LOOPBACK_INTERNAL,
	SLI4_LINK_TOPO_LOOPBACK_EXTERNAL,
	SLI4_LINK_TOPO_NONE,
	SLI4_LINK_TOPO_MAX,
};

enum sli4_link_medium {
	SLI4_LINK_MEDIUM_ETHERNET,
	SLI4_LINK_MEDIUM_FC,
	SLI4_LINK_MEDIUM_MAX,
};
/******Driver specific structures******/

struct sli4_queue {
	/* Common to all queue types */
	struct efc_dma	dma;
	spinlock_t	lock;		/* Lock to protect the doorbell register
					 * writes and queue reads
					 */
	u32		index;		/* current host entry index */
	u16		size;		/* entry size */
	u16		length;		/* number of entries */
	u16		n_posted;	/* number entries posted for CQ, EQ */
	u16		id;		/* Port assigned xQ_ID */
	u8		type;		/* queue type ie EQ, CQ, ... */
	void __iomem    *db_regaddr;	/* register address for the doorbell */
	u16		phase;		/* For if_type = 6, this value toggle
					 * for each iteration of the queue,
					 * a queue entry is valid when a cqe
					 * valid bit matches this value
					 */
	u32		proc_limit;	/* limit CQE processed per iteration */
	u32		posted_limit;	/* CQE/EQE process before ring db */
	u32		max_num_processed;
	u64		max_process_time;
	union {
		u32	r_idx;		/* "read" index (MQ only) */
		u32	flag;
	} u;
};

/* Parameters used to populate WQE*/
struct sli_bls_params {
	u32		s_id;
	u32		d_id;
	u16		ox_id;
	u16		rx_id;
	u32		rpi;
	u32		vpi;
	bool		rpi_registered;
	u8		payload[12];
	u16		xri;
	u16		tag;
};

struct sli_els_params {
	u32		s_id;
	u32		d_id;
	u16		ox_id;
	u32		rpi;
	u32		vpi;
	bool		rpi_registered;
	u32		xmit_len;
	u32		rsp_len;
	u8		timeout;
	u8		cmd;
	u16		xri;
	u16		tag;
};

struct sli_ct_params {
	u8		r_ctl;
	u8		type;
	u8		df_ctl;
	u8		timeout;
	u16		ox_id;
	u32		d_id;
	u32		rpi;
	u32		vpi;
	bool		rpi_registered;
	u32		xmit_len;
	u32		rsp_len;
	u16		xri;
	u16		tag;
};

struct sli_fcp_tgt_params {
	u32		s_id;
	u32		d_id;
	u32		rpi;
	u32		vpi;
	u32		offset;
	u16		ox_id;
	u16		flags;
	u8		cs_ctl;
	u8		timeout;
	u32		app_id;
	u32		xmit_len;
	u16		xri;
	u16		tag;
};

struct sli4_link_event {
	enum sli4_link_status	status;
	enum sli4_link_topology	topology;
	enum sli4_link_medium	medium;
	u32			speed;
	u8			*loop_map;
	u32			fc_id;
};

enum sli4_resource {
	SLI4_RSRC_VFI,
	SLI4_RSRC_VPI,
	SLI4_RSRC_RPI,
	SLI4_RSRC_XRI,
	SLI4_RSRC_FCFI,
	SLI4_RSRC_MAX,
};

struct sli4_extent {
	u32		number;
	u32		size;
	u32		n_alloc;
	u32		*base;
	unsigned long	*use_map;
	u32		map_size;
};

struct sli4_queue_info {
	u16	max_qcount[SLI4_QTYPE_MAX];
	u32	max_qentries[SLI4_QTYPE_MAX];
	u16	count_mask[SLI4_QTYPE_MAX];
	u16	count_method[SLI4_QTYPE_MAX];
	u32	qpage_count[SLI4_QTYPE_MAX];
};

struct sli4_params {
	u8	has_extents;
	u8	auto_reg;
	u8	auto_xfer_rdy;
	u8	hdr_template_req;
	u8	perf_hint;
	u8	perf_wq_id_association;
	u8	cq_create_version;
	u8	mq_create_version;
	u8	high_login_mode;
	u8	sgl_pre_registered;
	u8	sgl_pre_reg_required;
	u8	t10_dif_inline_capable;
	u8	t10_dif_separate_capable;
};

struct sli4 {
	void			*os;
	struct pci_dev		*pci;
	void __iomem		*reg[PCI_STD_NUM_BARS];

	u32			sli_rev;
	u32			sli_family;
	u32			if_type;

	u16			asic_type;
	u16			asic_rev;

	u16			e_d_tov;
	u16			r_a_tov;
	struct sli4_queue_info	qinfo;
	u16			link_module_type;
	u8			rq_batch;
	u8			port_number;
	char			port_name[2];
	u16			rq_min_buf_size;
	u32			rq_max_buf_size;
	u8			topology;
	u8			wwpn[8];
	u8			wwnn[8];
	u32			fw_rev[2];
	u8			fw_name[2][16];
	char			ipl_name[16];
	u32			hw_rev[3];
	char			modeldesc[64];
	char			bios_version_string[32];
	u32			wqe_size;
	u32			vpd_length;
	/*
	 * Tracks the port resources using extents metaphor. For
	 * devices that don't implement extents (i.e.
	 * has_extents == FALSE), the code models each resource as
	 * a single large extent.
	 */
	struct sli4_extent	ext[SLI4_RSRC_MAX];
	u32			features;
	struct sli4_params	params;
	u32			sge_supported_length;
	u32			sgl_page_sizes;
	u32			max_sgl_pages;

	/*
	 * Callback functions
	 */
	int			(*link)(void *ctx, void *event);
	void			*link_arg;

	struct efc_dma		bmbx;

	/* Save pointer to physical memory descriptor for non-embedded
	 * SLI_CONFIG commands for BMBX dumping purposes
	 */
	struct efc_dma		*bmbx_non_emb_pmd;

	struct efc_dma		vpd_data;
};

static inline void
sli_cmd_fill_hdr(struct sli4_rqst_hdr *hdr, u8 opc, u8 sub, u32 ver, __le32 len)
{
	hdr->opcode = opc;
	hdr->subsystem = sub;
	hdr->dw3_version = cpu_to_le32(ver);
	hdr->request_length = len;
}

/**
 * Get / set parameter functions
 */

static inline u32
sli_get_max_sge(struct sli4 *sli4)
{
	return sli4->sge_supported_length;
}

static inline u32
sli_get_max_sgl(struct sli4 *sli4)
{
	if (sli4->sgl_page_sizes != 1) {
		efc_log_err(sli4, "unsupported SGL page sizes %#x\n",
			    sli4->sgl_page_sizes);
		return 0;
	}

	return (sli4->max_sgl_pages * SLI_PAGE_SIZE) / sizeof(struct sli4_sge);
}

static inline enum sli4_link_medium
sli_get_medium(struct sli4 *sli4)
{
	switch (sli4->topology) {
	case SLI4_READ_CFG_TOPO_FC:
	case SLI4_READ_CFG_TOPO_FC_AL:
	case SLI4_READ_CFG_TOPO_NON_FC_AL:
		return SLI4_LINK_MEDIUM_FC;
	default:
		return SLI4_LINK_MEDIUM_MAX;
	}
}

static inline u32
sli_get_lmt(struct sli4 *sli4)
{
	return sli4->link_module_type;
}

static inline int
sli_set_topology(struct sli4 *sli4, u32 value)
{
	int	rc = 0;

	switch (value) {
	case SLI4_READ_CFG_TOPO_FC:
	case SLI4_READ_CFG_TOPO_FC_AL:
	case SLI4_READ_CFG_TOPO_NON_FC_AL:
		sli4->topology = value;
		break;
	default:
		efc_log_err(sli4, "unsupported topology %#x\n", value);
		rc = -1;
	}

	return rc;
}

static inline u32
sli_convert_mask_to_count(u32 method, u32 mask)
{
	u32 count = 0;

	if (method) {
		count = 1 << (31 - __builtin_clz(mask));
		count *= 16;
	} else {
		count = mask;
	}

	return count;
}

static inline u32
sli_reg_read_status(struct sli4 *sli)
{
	return readl(sli->reg[0] + SLI4_PORT_STATUS_REGOFF);
}

static inline int
sli_fw_error_status(struct sli4 *sli4)
{
	return (sli_reg_read_status(sli4) & SLI4_PORT_STATUS_ERR) ? 1 : 0;
}

static inline u32
sli_reg_read_err1(struct sli4 *sli)
{
	return readl(sli->reg[0] + SLI4_PORT_ERROR1);
}

static inline u32
sli_reg_read_err2(struct sli4 *sli)
{
	return readl(sli->reg[0] + SLI4_PORT_ERROR2);
}

static inline int
sli_fc_rqe_length(struct sli4 *sli4, void *cqe, u32 *len_hdr,
		  u32 *len_data)
{
	struct sli4_fc_async_rcqe	*rcqe = cqe;

	*len_hdr = *len_data = 0;

	if (rcqe->status == SLI4_FC_ASYNC_RQ_SUCCESS) {
		*len_hdr  = rcqe->hdpl_byte & SLI4_RACQE_HDPL;
		*len_data = le16_to_cpu(rcqe->data_placement_length);
		return 0;
	} else {
		return -1;
	}
}

static inline u8
sli_fc_rqe_fcfi(struct sli4 *sli4, void *cqe)
{
	u8 code = ((u8 *)cqe)[SLI4_CQE_CODE_OFFSET];
	u8 fcfi = U8_MAX;

	switch (code) {
	case SLI4_CQE_CODE_RQ_ASYNC: {
		struct sli4_fc_async_rcqe *rcqe = cqe;

		fcfi = le16_to_cpu(rcqe->fcfi_rq_id_word) & SLI4_RACQE_FCFI;
		break;
	}
	case SLI4_CQE_CODE_RQ_ASYNC_V1: {
		struct sli4_fc_async_rcqe_v1 *rcqev1 = cqe;

		fcfi = rcqev1->fcfi_byte & SLI4_RACQE_FCFI;
		break;
	}
	case SLI4_CQE_CODE_OPTIMIZED_WRITE_CMD: {
		struct sli4_fc_optimized_write_cmd_cqe *opt_wr = cqe;

		fcfi = opt_wr->flags0 & SLI4_OCQE_FCFI;
		break;
	}
	}

	return fcfi;
}

/****************************************************************************
 * Function prototypes
 */
int
sli_cmd_config_link(struct sli4 *sli4, void *buf);
int
sli_cmd_down_link(struct sli4 *sli4, void *buf);
int
sli_cmd_dump_type4(struct sli4 *sli4, void *buf, u16 wki);
int
sli_cmd_common_read_transceiver_data(struct sli4 *sli4, void *buf,
				     u32 page_num, struct efc_dma *dma);
int
sli_cmd_read_link_stats(struct sli4 *sli4, void *buf, u8 req_stats,
			u8 clear_overflow_flags, u8 clear_all_counters);
int
sli_cmd_read_status(struct sli4 *sli4, void *buf, u8 clear);
int
sli_cmd_init_link(struct sli4 *sli4, void *buf, u32 speed,
		  u8 reset_alpa);
int
sli_cmd_init_vfi(struct sli4 *sli4, void *buf, u16 vfi, u16 fcfi,
		 u16 vpi);
int
sli_cmd_init_vpi(struct sli4 *sli4, void *buf, u16 vpi, u16 vfi);
int
sli_cmd_post_xri(struct sli4 *sli4, void *buf, u16 base, u16 cnt);
int
sli_cmd_release_xri(struct sli4 *sli4, void *buf, u8 num_xri);
int
sli_cmd_read_sparm64(struct sli4 *sli4, void *buf,
		     struct efc_dma *dma, u16 vpi);
int
sli_cmd_read_topology(struct sli4 *sli4, void *buf, struct efc_dma *dma);
int
sli_cmd_read_nvparms(struct sli4 *sli4, void *buf);
int
sli_cmd_write_nvparms(struct sli4 *sli4, void *buf, u8 *wwpn,
		      u8 *wwnn, u8 hard_alpa, u32 preferred_d_id);
int
sli_cmd_reg_fcfi(struct sli4 *sli4, void *buf, u16 index,
		 struct sli4_cmd_rq_cfg *rq_cfg);
int
sli_cmd_reg_fcfi_mrq(struct sli4 *sli4, void *buf, u8 mode, u16 index,
		     u8 rq_selection_policy, u8 mrq_bit_mask, u16 num_mrqs,
		     struct sli4_cmd_rq_cfg *rq_cfg);
int
sli_cmd_reg_rpi(struct sli4 *sli4, void *buf, u32 rpi, u32 vpi, u32 fc_id,
		struct efc_dma *dma, u8 update, u8 enable_t10_pi);
int
sli_cmd_unreg_fcfi(struct sli4 *sli4, void *buf, u16 indicator);
int
sli_cmd_unreg_rpi(struct sli4 *sli4, void *buf, u16 indicator,
		  enum sli4_resource which, u32 fc_id);
int
sli_cmd_reg_vpi(struct sli4 *sli4, void *buf, u32 fc_id,
		__be64 sli_wwpn, u16 vpi, u16 vfi, bool update);
int
sli_cmd_reg_vfi(struct sli4 *sli4, void *buf, size_t size,
		u16 vfi, u16 fcfi, struct efc_dma dma,
		u16 vpi, __be64 sli_wwpn, u32 fc_id);
int
sli_cmd_unreg_vpi(struct sli4 *sli4, void *buf, u16 id, u32 type);
int
sli_cmd_unreg_vfi(struct sli4 *sli4, void *buf, u16 idx, u32 type);
int
sli_cmd_common_nop(struct sli4 *sli4, void *buf, uint64_t context);
int
sli_cmd_common_get_resource_extent_info(struct sli4 *sli4, void *buf,
					u16 rtype);
int
sli_cmd_common_get_sli4_parameters(struct sli4 *sli4, void *buf);
int
sli_cmd_common_write_object(struct sli4 *sli4, void *buf, u16 noc,
		u16 eof, u32 len, u32 offset, char *name, struct efc_dma *dma);
int
sli_cmd_common_delete_object(struct sli4 *sli4, void *buf, char *object_name);
int
sli_cmd_common_read_object(struct sli4 *sli4, void *buf,
		u32 length, u32 offset, char *name, struct efc_dma *dma);
int
sli_cmd_dmtf_exec_clp_cmd(struct sli4 *sli4, void *buf,
		struct efc_dma *cmd, struct efc_dma *resp);
int
sli_cmd_common_set_dump_location(struct sli4 *sli4, void *buf,
		bool query, bool is_buffer_list, struct efc_dma *dma, u8 fdb);
int
sli_cmd_common_set_features(struct sli4 *sli4, void *buf,
			    u32 feature, u32 param_len, void *parameter);

int sli_cqe_mq(struct sli4 *sli4, void *buf);
int sli_cqe_async(struct sli4 *sli4, void *buf);

int
sli_setup(struct sli4 *sli4, void *os, struct pci_dev *pdev, void __iomem *r[]);
void sli_calc_max_qentries(struct sli4 *sli4);
int sli_init(struct sli4 *sli4);
int sli_reset(struct sli4 *sli4);
int sli_fw_reset(struct sli4 *sli4);
void sli_teardown(struct sli4 *sli4);
int
sli_callback(struct sli4 *sli4, enum sli4_callback cb, void *func, void *arg);
int
sli_bmbx_command(struct sli4 *sli4);
int
__sli_queue_init(struct sli4 *sli4, struct sli4_queue *q, u32 qtype,
		 size_t size, u32 n_entries, u32 align);
int
__sli_create_queue(struct sli4 *sli4, struct sli4_queue *q);
int
sli_eq_modify_delay(struct sli4 *sli4, struct sli4_queue *eq, u32 num_eq,
		    u32 shift, u32 delay_mult);
int
sli_queue_alloc(struct sli4 *sli4, u32 qtype, struct sli4_queue *q,
		u32 n_entries, struct sli4_queue *assoc);
int
sli_cq_alloc_set(struct sli4 *sli4, struct sli4_queue *qs[], u32 num_cqs,
		 u32 n_entries, struct sli4_queue *eqs[]);
int
sli_get_queue_entry_size(struct sli4 *sli4, u32 qtype);
int
sli_queue_free(struct sli4 *sli4, struct sli4_queue *q, u32 destroy_queues,
	       u32 free_memory);
int
sli_queue_eq_arm(struct sli4 *sli4, struct sli4_queue *q, bool arm);
int
sli_queue_arm(struct sli4 *sli4, struct sli4_queue *q, bool arm);

int
sli_wq_write(struct sli4 *sli4, struct sli4_queue *q, u8 *entry);
int
sli_mq_write(struct sli4 *sli4, struct sli4_queue *q, u8 *entry);
int
sli_rq_write(struct sli4 *sli4, struct sli4_queue *q, u8 *entry);
int
sli_eq_read(struct sli4 *sli4, struct sli4_queue *q, u8 *entry);
int
sli_cq_read(struct sli4 *sli4, struct sli4_queue *q, u8 *entry);
int
sli_mq_read(struct sli4 *sli4, struct sli4_queue *q, u8 *entry);
int
sli_resource_alloc(struct sli4 *sli4, enum sli4_resource rtype, u32 *rid,
		   u32 *index);
int
sli_resource_free(struct sli4 *sli4, enum sli4_resource rtype, u32 rid);
int
sli_resource_reset(struct sli4 *sli4, enum sli4_resource rtype);
int
sli_eq_parse(struct sli4 *sli4, u8 *buf, u16 *cq_id);
int
sli_cq_parse(struct sli4 *sli4, struct sli4_queue *cq, u8 *cqe,
	     enum sli4_qentry *etype, u16 *q_id);

int sli_raise_ue(struct sli4 *sli4, u8 dump);
int sli_dump_is_ready(struct sli4 *sli4);
bool sli_reset_required(struct sli4 *sli4);
bool sli_fw_ready(struct sli4 *sli4);

int
sli_fc_process_link_attention(struct sli4 *sli4, void *acqe);
int
sli_fc_cqe_parse(struct sli4 *sli4, struct sli4_queue *cq,
		 u8 *cqe, enum sli4_qentry *etype,
		 u16 *rid);
u32 sli_fc_response_length(struct sli4 *sli4, u8 *cqe);
u32 sli_fc_io_length(struct sli4 *sli4, u8 *cqe);
int sli_fc_els_did(struct sli4 *sli4, u8 *cqe, u32 *d_id);
u32 sli_fc_ext_status(struct sli4 *sli4, u8 *cqe);
int
sli_fc_rqe_rqid_and_index(struct sli4 *sli4, u8 *cqe, u16 *rq_id, u32 *index);
int
sli_cmd_wq_create(struct sli4 *sli4, void *buf,
		  struct efc_dma *qmem, u16 cq_id);
int sli_cmd_post_sgl_pages(struct sli4 *sli4, void *buf, u16 xri,
		u32 xri_count, struct efc_dma *page0[], struct efc_dma *page1[],
		struct efc_dma *dma);
int
sli_cmd_post_hdr_templates(struct sli4 *sli4, void *buf,
		struct efc_dma *dma, u16 rpi, struct efc_dma *payload_dma);
int
sli_fc_rq_alloc(struct sli4 *sli4, struct sli4_queue *q, u32 n_entries,
		u32 buffer_size, struct sli4_queue *cq, bool is_hdr);
int
sli_fc_rq_set_alloc(struct sli4 *sli4, u32 num_rq_pairs, struct sli4_queue *q[],
		u32 base_cq_id, u32 num, u32 hdr_buf_size, u32 data_buf_size);
u32 sli_fc_get_rpi_requirements(struct sli4 *sli4, u32 n_rpi);
int
sli_abort_wqe(struct sli4 *sli4, void *buf, enum sli4_abort_type type,
	      bool send_abts, u32 ids, u32 mask, u16 tag, u16 cq_id);

int
sli_send_frame_wqe(struct sli4 *sli4, void *buf, u8 sof, u8 eof,
		   u32 *hdr, struct efc_dma *payload, u32 req_len, u8 timeout,
		   u16 xri, u16 req_tag);

int
sli_xmit_els_rsp64_wqe(struct sli4 *sli4, void *buf, struct efc_dma *rsp,
		       struct sli_els_params *params);

int
sli_els_request64_wqe(struct sli4 *sli4, void *buf, struct efc_dma *sgl,
		      struct sli_els_params *params);

int
sli_fcp_icmnd64_wqe(struct sli4 *sli4, void *buf, struct efc_dma *sgl, u16 xri,
		    u16 tag, u16 cq_id, u32 rpi, u32 rnode_fcid, u8 timeout);

int
sli_fcp_iread64_wqe(struct sli4 *sli4, void *buf, struct efc_dma *sgl,
		    u32 first_data_sge, u32 xfer_len, u16 xri,
		    u16 tag, u16 cq_id, u32 rpi, u32 rnode_fcid, u8 dif, u8 bs,
		    u8 timeout);

int
sli_fcp_iwrite64_wqe(struct sli4 *sli4, void *buf, struct efc_dma *sgl,
		     u32 first_data_sge, u32 xfer_len,
		     u32 first_burst, u16 xri, u16 tag, u16 cq_id, u32 rpi,
		     u32 rnode_fcid, u8 dif, u8 bs, u8 timeout);

int
sli_fcp_treceive64_wqe(struct sli4 *sli, void *buf, struct efc_dma *sgl,
		       u32 first_data_sge, u16 cq_id, u8 dif, u8 bs,
		       struct sli_fcp_tgt_params *params);
int
sli_fcp_cont_treceive64_wqe(struct sli4 *sli, void *buf, struct efc_dma *sgl,
			    u32 first_data_sge, u16 sec_xri, u16 cq_id, u8 dif,
			    u8 bs, struct sli_fcp_tgt_params *params);

int
sli_fcp_trsp64_wqe(struct sli4 *sli4, void *buf, struct efc_dma *sgl,
		   u16 cq_id, u8 port_owned, struct sli_fcp_tgt_params *params);

int
sli_fcp_tsend64_wqe(struct sli4 *sli4, void *buf, struct efc_dma *sgl,
		    u32 first_data_sge, u16 cq_id, u8 dif, u8 bs,
		    struct sli_fcp_tgt_params *params);
int
sli_gen_request64_wqe(struct sli4 *sli4, void *buf, struct efc_dma *sgl,
		      struct sli_ct_params *params);

int
sli_xmit_bls_rsp64_wqe(struct sli4 *sli4, void *buf,
		struct sli_bls_payload *payload, struct sli_bls_params *params);

int
sli_xmit_sequence64_wqe(struct sli4 *sli4, void *buf, struct efc_dma *payload,
			struct sli_ct_params *params);

int
sli_requeue_xri_wqe(struct sli4 *sli4, void *buf, u16 xri, u16 tag, u16 cq_id);
void
sli4_cmd_lowlevel_set_watchdog(struct sli4 *sli4, void *buf, size_t size,
			       u16 timeout);

const char *sli_fc_get_status_string(u32 status);

#endif /* !_SLI4_H */
