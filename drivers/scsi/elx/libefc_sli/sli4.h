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

#endif /* !_SLI4_H */
