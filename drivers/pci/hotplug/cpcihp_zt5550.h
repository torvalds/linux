/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * cpcihp_zt5550.h
 *
 * Intel/Ziatech ZT5550 CompactPCI Host Controller driver definitions
 *
 * Copyright 2002 SOMA Networks, Inc.
 * Copyright 2001 Intel San Luis Obispo
 * Copyright 2000,2001 MontaVista Software Inc.
 *
 * Send feedback to <scottm@somanetworks.com>
 */

#ifndef _CPCIHP_ZT5550_H
#define _CPCIHP_ZT5550_H

/* Direct registers */
#define CSR_HCINDEX		0x00
#define CSR_HCDATA		0x04
#define CSR_INTSTAT		0x08
#define CSR_INTMASK		0x09
#define CSR_CNT0CMD		0x0C
#define CSR_CNT1CMD		0x0E
#define CSR_CNT0		0x10
#define CSR_CNT1		0x14

/* Masks for interrupt bits in CSR_INTMASK direct register */
#define CNT0_INT_MASK		0x01
#define CNT1_INT_MASK		0x02
#define ENUM_INT_MASK		0x04
#define ALL_DIRECT_INTS_MASK	0x07

/* Indexed registers (through CSR_INDEX, CSR_DATA) */
#define HC_INT_MASK_REG		0x04
#define HC_STATUS_REG		0x08
#define HC_CMD_REG		0x0C
#define ARB_CONFIG_GNT_REG	0x10
#define ARB_CONFIG_CFG_REG	0x12
#define ARB_CONFIG_REG		0x10
#define ISOL_CONFIG_REG		0x18
#define FAULT_STATUS_REG	0x20
#define FAULT_CONFIG_REG	0x24
#define WD_CONFIG_REG		0x2C
#define HC_DIAG_REG		0x30
#define SERIAL_COMM_REG		0x34
#define SERIAL_OUT_REG		0x38
#define SERIAL_IN_REG		0x3C

/* Masks for interrupt bits in HC_INT_MASK_REG indexed register */
#define SERIAL_INT_MASK		0x01
#define FAULT_INT_MASK		0x02
#define HCF_INT_MASK		0x04
#define ALL_INDEXED_INTS_MASK	0x07

/* Digital I/O port storing ENUM# */
#define ENUM_PORT	0xE1
/* Mask to get to the ENUM# bit on the bus */
#define ENUM_MASK	0x40

#endif				/* _CPCIHP_ZT5550_H */
