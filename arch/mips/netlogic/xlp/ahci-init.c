/*
 * Copyright (c) 2003-2014 Broadcom Corporation
 * All Rights Reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/bitops.h>

#include <asm/cpu.h>
#include <asm/mipsregs.h>

#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/common.h>
#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/mips-extns.h>

#define SATA_CTL		0x0
#define SATA_STATUS		0x1	/* Status Reg */
#define SATA_INT		0x2	/* Interrupt Reg */
#define SATA_INT_MASK		0x3	/* Interrupt Mask Reg */
#define SATA_CR_REG_TIMER	0x4	/* PHY Conrol Timer Reg */
#define SATA_CORE_ID		0x5	/* Core ID Reg */
#define SATA_AXI_SLAVE_OPT1	0x6	/* AXI Slave Options Reg */
#define SATA_PHY_LOS_LEV	0x7	/* PHY LOS Level Reg */
#define SATA_PHY_MULTI		0x8	/* PHY Multiplier Reg */
#define SATA_PHY_CLK_SEL	0x9	/* Clock Select Reg */
#define SATA_PHY_AMP1_GEN1	0xa	/* PHY Transmit Amplitude Reg 1 */
#define SATA_PHY_AMP1_GEN2	0xb	/* PHY Transmit Amplitude Reg 2 */
#define SATA_PHY_AMP1_GEN3	0xc	/* PHY Transmit Amplitude Reg 3 */
#define SATA_PHY_PRE1		0xd	/* PHY Transmit Preemphasis Reg 1 */
#define SATA_PHY_PRE2		0xe	/* PHY Transmit Preemphasis Reg 2 */
#define SATA_PHY_PRE3		0xf	/* PHY Transmit Preemphasis Reg 3 */
#define SATA_SPDMODE		0x10	/* Speed Mode Reg */
#define SATA_REFCLK		0x11	/* Reference Clock Control Reg */
#define SATA_BYTE_SWAP_DIS	0x12	/* byte swap disable */

/*SATA_CTL Bits */
#define SATA_RST_N		BIT(0)
#define PHY0_RESET_N		BIT(16)
#define PHY1_RESET_N		BIT(17)
#define PHY2_RESET_N		BIT(18)
#define PHY3_RESET_N		BIT(19)
#define M_CSYSREQ		BIT(2)
#define S_CSYSREQ		BIT(3)

/*SATA_STATUS Bits */
#define P0_PHY_READY		BIT(4)
#define P1_PHY_READY		BIT(5)
#define P2_PHY_READY		BIT(6)
#define P3_PHY_READY		BIT(7)

#define nlm_read_sata_reg(b, r)		nlm_read_reg(b, r)
#define nlm_write_sata_reg(b, r, v)	nlm_write_reg(b, r, v)
#define nlm_get_sata_pcibase(node)	\
		nlm_pcicfg_base(XLP_IO_SATA_OFFSET(node))
/* SATA device specific configuration registers are starts at 0x900 offset */
#define nlm_get_sata_regbase(node)	\
		(nlm_get_sata_pcibase(node) + 0x900)

static void sata_clear_glue_reg(uint64_t regbase, uint32_t off, uint32_t bit)
{
	uint32_t reg_val;

	reg_val = nlm_read_sata_reg(regbase, off);
	nlm_write_sata_reg(regbase, off, (reg_val & ~bit));
}

static void sata_set_glue_reg(uint64_t regbase, uint32_t off, uint32_t bit)
{
	uint32_t reg_val;

	reg_val = nlm_read_sata_reg(regbase, off);
	nlm_write_sata_reg(regbase, off, (reg_val | bit));
}

static void nlm_sata_firmware_init(int node)
{
	uint32_t reg_val;
	uint64_t regbase;
	int i;

	pr_info("XLP AHCI Initialization started.\n");
	regbase = nlm_get_sata_regbase(node);

	/* Reset SATA */
	sata_clear_glue_reg(regbase, SATA_CTL, SATA_RST_N);
	/* Reset PHY */
	sata_clear_glue_reg(regbase, SATA_CTL,
			(PHY3_RESET_N | PHY2_RESET_N
			 | PHY1_RESET_N | PHY0_RESET_N));

	/* Set SATA */
	sata_set_glue_reg(regbase, SATA_CTL, SATA_RST_N);
	/* Set PHY */
	sata_set_glue_reg(regbase, SATA_CTL,
			(PHY3_RESET_N | PHY2_RESET_N
			 | PHY1_RESET_N | PHY0_RESET_N));

	pr_debug("Waiting for PHYs to come up.\n");
	i = 0;
	do {
		reg_val = nlm_read_sata_reg(regbase, SATA_STATUS);
		i++;
	} while (((reg_val & 0xF0) != 0xF0) && (i < 10000));

	for (i = 0; i < 4; i++) {
		if (reg_val  & (P0_PHY_READY << i))
			pr_info("PHY%d is up.\n", i);
		else
			pr_info("PHY%d is down.\n", i);
	}

	pr_info("XLP AHCI init done.\n");
}

static int __init nlm_ahci_init(void)
{
	int node = 0;
	int chip = read_c0_prid() & PRID_IMP_MASK;

	if (chip == PRID_IMP_NETLOGIC_XLP3XX)
		nlm_sata_firmware_init(node);
	return 0;
}

static void nlm_sata_intr_ack(struct irq_data *data)
{
	uint32_t val = 0;
	uint64_t regbase;

	regbase = nlm_get_sata_regbase(nlm_nodeid());
	val = nlm_read_sata_reg(regbase, SATA_INT);
	sata_set_glue_reg(regbase, SATA_INT, val);
}

static void nlm_sata_fixup_bar(struct pci_dev *dev)
{
	/*
	 * The AHCI resource is in BAR 0, move it to
	 * BAR 5, where it is expected
	 */
	dev->resource[5] = dev->resource[0];
	memset(&dev->resource[0], 0, sizeof(dev->resource[0]));
}

static void nlm_sata_fixup_final(struct pci_dev *dev)
{
	uint32_t val;
	uint64_t regbase;
	int node = 0; /* XLP3XX does not support multi-node */

	regbase = nlm_get_sata_regbase(node);

	/* clear pending interrupts and then enable them */
	val = nlm_read_sata_reg(regbase, SATA_INT);
	sata_set_glue_reg(regbase, SATA_INT, val);

	/* Mask the core interrupt. If all the interrupts
	 * are enabled there are spurious interrupt flow
	 * happening, to avoid only enable core interrupt
	 * mask.
	 */
	sata_set_glue_reg(regbase, SATA_INT_MASK, 0x1);

	dev->irq = PIC_SATA_IRQ;
	nlm_set_pic_extra_ack(node, PIC_SATA_IRQ, nlm_sata_intr_ack);
}

arch_initcall(nlm_ahci_init);

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_NETLOGIC, PCI_DEVICE_ID_NLM_SATA,
		nlm_sata_fixup_bar);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_NETLOGIC, PCI_DEVICE_ID_NLM_SATA,
		nlm_sata_fixup_final);
