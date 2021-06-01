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

#endif /* !_SLI4_H */
