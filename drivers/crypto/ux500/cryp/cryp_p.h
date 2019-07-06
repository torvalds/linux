/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Shujuan Chen <shujuan.chen@stericsson.com> for ST-Ericsson.
 * Author: Jonas Linde <jonas.linde@stericsson.com> for ST-Ericsson.
 * Author: Joakim Bech <joakim.xx.bech@stericsson.com> for ST-Ericsson.
 * Author: Berne Hebark <berne.herbark@stericsson.com> for ST-Ericsson.
 * Author: Niklas Hernaeus <niklas.hernaeus@stericsson.com> for ST-Ericsson.
 */

#ifndef _CRYP_P_H_
#define _CRYP_P_H_

#include <linux/io.h>
#include <linux/bitops.h>

#include "cryp.h"
#include "cryp_irqp.h"

/**
 * Generic Macros
 */
#define CRYP_SET_BITS(reg_name, mask) \
	writel_relaxed((readl_relaxed(reg_name) | mask), reg_name)

#define CRYP_WRITE_BIT(reg_name, val, mask) \
	writel_relaxed(((readl_relaxed(reg_name) & ~(mask)) |\
			((val) & (mask))), reg_name)

#define CRYP_TEST_BITS(reg_name, val) \
	(readl_relaxed(reg_name) & (val))

#define CRYP_PUT_BITS(reg, val, shift, mask) \
	writel_relaxed(((readl_relaxed(reg) & ~(mask)) | \
		(((u32)val << shift) & (mask))), reg)

/**
 * CRYP specific Macros
 */
#define CRYP_PERIPHERAL_ID0		0xE3
#define CRYP_PERIPHERAL_ID1		0x05

#define CRYP_PERIPHERAL_ID2_DB8500	0x28
#define CRYP_PERIPHERAL_ID3		0x00

#define CRYP_PCELL_ID0			0x0D
#define CRYP_PCELL_ID1			0xF0
#define CRYP_PCELL_ID2			0x05
#define CRYP_PCELL_ID3			0xB1

/**
 * CRYP register default values
 */
#define MAX_DEVICE_SUPPORT		2

/* Priv set, keyrden set and datatype 8bits swapped set as default. */
#define CRYP_CR_DEFAULT			0x0482
#define CRYP_DMACR_DEFAULT		0x0
#define CRYP_IMSC_DEFAULT		0x0
#define CRYP_DIN_DEFAULT		0x0
#define CRYP_DOUT_DEFAULT		0x0
#define CRYP_KEY_DEFAULT		0x0
#define CRYP_INIT_VECT_DEFAULT		0x0

/**
 * CRYP Control register specific mask
 */
#define CRYP_CR_SECURE_MASK		BIT(0)
#define CRYP_CR_PRLG_MASK		BIT(1)
#define CRYP_CR_ALGODIR_MASK		BIT(2)
#define CRYP_CR_ALGOMODE_MASK		(BIT(5) | BIT(4) | BIT(3))
#define CRYP_CR_DATATYPE_MASK		(BIT(7) | BIT(6))
#define CRYP_CR_KEYSIZE_MASK		(BIT(9) | BIT(8))
#define CRYP_CR_KEYRDEN_MASK		BIT(10)
#define CRYP_CR_KSE_MASK		BIT(11)
#define CRYP_CR_START_MASK		BIT(12)
#define CRYP_CR_INIT_MASK		BIT(13)
#define CRYP_CR_FFLUSH_MASK		BIT(14)
#define CRYP_CR_CRYPEN_MASK		BIT(15)
#define CRYP_CR_CONTEXT_SAVE_MASK	(CRYP_CR_SECURE_MASK |\
					 CRYP_CR_PRLG_MASK |\
					 CRYP_CR_ALGODIR_MASK |\
					 CRYP_CR_ALGOMODE_MASK |\
					 CRYP_CR_DATATYPE_MASK |\
					 CRYP_CR_KEYSIZE_MASK |\
					 CRYP_CR_KEYRDEN_MASK |\
					 CRYP_CR_DATATYPE_MASK)


#define CRYP_SR_INFIFO_READY_MASK	(BIT(0) | BIT(1))
#define CRYP_SR_IFEM_MASK		BIT(0)
#define CRYP_SR_BUSY_MASK		BIT(4)

/**
 * Bit position used while setting bits in register
 */
#define CRYP_CR_PRLG_POS		1
#define CRYP_CR_ALGODIR_POS		2
#define CRYP_CR_ALGOMODE_POS		3
#define CRYP_CR_DATATYPE_POS		6
#define CRYP_CR_KEYSIZE_POS		8
#define CRYP_CR_KEYRDEN_POS		10
#define CRYP_CR_KSE_POS			11
#define CRYP_CR_START_POS		12
#define CRYP_CR_INIT_POS		13
#define CRYP_CR_CRYPEN_POS		15

#define CRYP_SR_BUSY_POS		4

/**
 * CRYP PCRs------PC_NAND control register
 * BIT_MASK
 */
#define CRYP_DMA_REQ_MASK		(BIT(1) | BIT(0))
#define CRYP_DMA_REQ_MASK_POS		0


struct cryp_system_context {
	/* CRYP Register structure */
	struct cryp_register *p_cryp_reg[MAX_DEVICE_SUPPORT];
};

#endif
