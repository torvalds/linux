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
#include <linux/pci_ids.h>
#include <linux/nodemask.h>

#include <asm/cpu.h>
#include <asm/mipsregs.h>

#include <asm/netlogic/common.h>
#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/mips-extns.h>
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/xlp-hal/iomap.h>

#define SATA_CTL		0x0
#define SATA_STATUS		0x1 /* Status Reg */
#define SATA_INT		0x2 /* Interrupt Reg */
#define SATA_INT_MASK		0x3 /* Interrupt Mask Reg */
#define SATA_BIU_TIMEOUT	0x4
#define AXIWRSPERRLOG		0x5
#define AXIRDSPERRLOG		0x6
#define BiuTimeoutLow		0x7
#define BiuTimeoutHi		0x8
#define BiuSlvErLow		0x9
#define BiuSlvErHi		0xa
#define IO_CONFIG_SWAP_DIS	0xb
#define CR_REG_TIMER		0xc
#define CORE_ID			0xd
#define AXI_SLAVE_OPT1		0xe
#define PHY_MEM_ACCESS		0xf
#define PHY0_CNTRL		0x10
#define PHY0_STAT		0x11
#define PHY0_RX_ALIGN		0x12
#define PHY0_RX_EQ_LO		0x13
#define PHY0_RX_EQ_HI		0x14
#define PHY0_BIST_LOOP		0x15
#define PHY1_CNTRL		0x16
#define PHY1_STAT		0x17
#define PHY1_RX_ALIGN		0x18
#define PHY1_RX_EQ_LO		0x19
#define PHY1_RX_EQ_HI		0x1a
#define PHY1_BIST_LOOP		0x1b
#define RdExBase		0x1c
#define RdExLimit		0x1d
#define CacheAllocBase		0x1e
#define CacheAllocLimit		0x1f
#define BiuSlaveCmdGstNum	0x20

/*SATA_CTL Bits */
#define SATA_RST_N		BIT(0)  /* Active low reset sata_core phy */
#define SataCtlReserve0		BIT(1)
#define M_CSYSREQ		BIT(2)  /* AXI master low power, not used */
#define S_CSYSREQ		BIT(3)  /* AXI slave low power, not used */
#define P0_CP_DET		BIT(8)  /* Reserved, bring in from pad */
#define P0_MP_SW		BIT(9)  /* Mech Switch */
#define P0_DISABLE		BIT(10) /* disable p0 */
#define P0_ACT_LED_EN		BIT(11) /* Active LED enable */
#define P0_IRST_HARD_SYNTH	BIT(12) /* PHY hard synth reset */
#define P0_IRST_HARD_TXRX	BIT(13) /* PHY lane hard reset */
#define P0_IRST_POR		BIT(14) /* PHY power on reset*/
#define P0_IPDTXL		BIT(15) /* PHY Tx lane dis/power down */
#define P0_IPDRXL		BIT(16) /* PHY Rx lane dis/power down */
#define P0_IPDIPDMSYNTH		BIT(17) /* PHY synthesizer dis/porwer down */
#define P0_CP_POD_EN		BIT(18) /* CP_POD enable */
#define P0_AT_BYPASS		BIT(19) /* P0 address translation by pass */
#define P1_CP_DET		BIT(20) /* Reserved,Cold Detect */
#define P1_MP_SW		BIT(21) /* Mech Switch */
#define P1_DISABLE		BIT(22) /* disable p1 */
#define P1_ACT_LED_EN		BIT(23) /* Active LED enable */
#define P1_IRST_HARD_SYNTH	BIT(24) /* PHY hard synth reset */
#define P1_IRST_HARD_TXRX	BIT(25) /* PHY lane hard reset */
#define P1_IRST_POR		BIT(26) /* PHY power on reset*/
#define P1_IPDTXL		BIT(27) /* PHY Tx lane dis/porwer down */
#define P1_IPDRXL		BIT(28) /* PHY Rx lane dis/porwer down */
#define P1_IPDIPDMSYNTH		BIT(29) /* PHY synthesizer dis/porwer down */
#define P1_CP_POD_EN		BIT(30)
#define P1_AT_BYPASS		BIT(31) /* P1 address translation by pass */

/* Status register */
#define M_CACTIVE		BIT(0)  /* m_cactive, not used */
#define S_CACTIVE		BIT(1)  /* s_cactive, not used */
#define P0_PHY_READY		BIT(8)  /* phy is ready */
#define P0_CP_POD		BIT(9)  /* Cold PowerOn */
#define P0_SLUMBER		BIT(10) /* power mode slumber */
#define P0_PATIAL		BIT(11) /* power mode patial */
#define P0_PHY_SIG_DET		BIT(12) /* phy dignal detect */
#define P0_PHY_CALI		BIT(13) /* phy calibration done */
#define P1_PHY_READY		BIT(16) /* phy is ready */
#define P1_CP_POD		BIT(17) /* Cold PowerOn */
#define P1_SLUMBER		BIT(18) /* power mode slumber */
#define P1_PATIAL		BIT(19) /* power mode patial */
#define P1_PHY_SIG_DET		BIT(20) /* phy dignal detect */
#define P1_PHY_CALI		BIT(21) /* phy calibration done */

/* SATA CR_REG_TIMER bits */
#define CR_TIME_SCALE		(0x1000 << 0)

/* SATA PHY specific registers start and end address */
#define RXCDRCALFOSC0		0x0065
#define CALDUTY			0x006e
#define RXDPIF			0x8065
#define PPMDRIFTMAX_HI		0x80A4

#define nlm_read_sata_reg(b, r)		nlm_read_reg(b, r)
#define nlm_write_sata_reg(b, r, v)	nlm_write_reg(b, r, v)
#define nlm_get_sata_pcibase(node)	\
		nlm_pcicfg_base(XLP9XX_IO_SATA_OFFSET(node))
#define nlm_get_sata_regbase(node)	\
		(nlm_get_sata_pcibase(node) + 0x100)

/* SATA PHY config for register block 1 0x0065 .. 0x006e */
static const u8 sata_phy_config1[]  = {
	0xC9, 0xC9, 0x07, 0x07, 0x18, 0x18, 0x01, 0x01, 0x22, 0x00
};

/* SATA PHY config for register block 2 0x0x8065 .. 0x0x80A4 */
static const u8 sata_phy_config2[]  = {
	0xAA, 0x00, 0x4C, 0xC9, 0xC9, 0x07, 0x07, 0x18,
	0x18, 0x05, 0x0C, 0x10, 0x00, 0x10, 0x00, 0xFF,
	0xCF, 0xF7, 0xE1, 0xF5, 0xFD, 0xFD, 0xFF, 0xFF,
	0xFF, 0xFF, 0xE3, 0xE7, 0xDB, 0xF5, 0xFD, 0xFD,
	0xF5, 0xF5, 0xFF, 0xFF, 0xE3, 0xE7, 0xDB, 0xF5,
	0xFD, 0xFD, 0xF5, 0xF5, 0xFF, 0xFF, 0xFF, 0xF5,
	0x3F, 0x00, 0x32, 0x00, 0x03, 0x01, 0x05, 0x05,
	0x04, 0x00, 0x00, 0x08, 0x04, 0x00, 0x00, 0x04,
};

const int sata_phy_debug = 0;	/* set to verify PHY writes */

static void sata_clear_glue_reg(u64 regbase, u32 off, u32 bit)
{
	u32 reg_val;

	reg_val = nlm_read_sata_reg(regbase, off);
	nlm_write_sata_reg(regbase, off, (reg_val & ~bit));
}

static void sata_set_glue_reg(u64 regbase, u32 off, u32 bit)
{
	u32 reg_val;

	reg_val = nlm_read_sata_reg(regbase, off);
	nlm_write_sata_reg(regbase, off, (reg_val | bit));
}

static void write_phy_reg(u64 regbase, u32 addr, u32 physel, u8 data)
{
	nlm_write_sata_reg(regbase, PHY_MEM_ACCESS,
		(1u << 31) | (physel << 24) | (data << 16) | addr);
	udelay(850);
}

static u8 read_phy_reg(u64 regbase, u32 addr, u32 physel)
{
	u32 val;

	nlm_write_sata_reg(regbase, PHY_MEM_ACCESS,
		(0 << 31) | (physel << 24) | (0 << 16) | addr);
	udelay(850);
	val = nlm_read_sata_reg(regbase, PHY_MEM_ACCESS);
	return (val >> 16) & 0xff;
}

static void config_sata_phy(u64 regbase)
{
	u32 port, i, reg;
	u8 val;

	for (port = 0; port < 2; port++) {
		for (i = 0, reg = RXCDRCALFOSC0; reg <= CALDUTY; reg++, i++)
			write_phy_reg(regbase, reg, port, sata_phy_config1[i]);

		for (i = 0, reg = RXDPIF; reg <= PPMDRIFTMAX_HI; reg++, i++)
			write_phy_reg(regbase, reg, port, sata_phy_config2[i]);

		/* Fix for PHY link up failures at lower temperatures */
		write_phy_reg(regbase, 0x800F, port, 0x1f);

		val = read_phy_reg(regbase, 0x0029, port);
		write_phy_reg(regbase, 0x0029, port, val | (0x7 << 1));

		val = read_phy_reg(regbase, 0x0056, port);
		write_phy_reg(regbase, 0x0056, port, val & ~(1 << 3));

		val = read_phy_reg(regbase, 0x0018, port);
		write_phy_reg(regbase, 0x0018, port, val & ~(0x7 << 0));
	}
}

static void check_phy_register(u64 regbase, u32 addr, u32 physel, u8 xdata)
{
	u8 data;

	data = read_phy_reg(regbase, addr, physel);
	pr_info("PHY read addr = 0x%x physel = %d data = 0x%x %s\n",
		addr, physel, data, data == xdata ? "TRUE" : "FALSE");
}

static void verify_sata_phy_config(u64 regbase)
{
	u32 port, i, reg;

	for (port = 0; port < 2; port++) {
		for (i = 0, reg = RXCDRCALFOSC0; reg <= CALDUTY; reg++, i++)
			check_phy_register(regbase, reg, port,
						sata_phy_config1[i]);

		for (i = 0, reg = RXDPIF; reg <= PPMDRIFTMAX_HI; reg++, i++)
			check_phy_register(regbase, reg, port,
						sata_phy_config2[i]);
	}
}

static void nlm_sata_firmware_init(int node)
{
	u32 reg_val;
	u64 regbase;
	int n;

	pr_info("Initializing XLP9XX On-chip AHCI...\n");
	regbase = nlm_get_sata_regbase(node);

	/* Reset port0 */
	sata_clear_glue_reg(regbase, SATA_CTL, P0_IRST_POR);
	sata_clear_glue_reg(regbase, SATA_CTL, P0_IRST_HARD_TXRX);
	sata_clear_glue_reg(regbase, SATA_CTL, P0_IRST_HARD_SYNTH);
	sata_clear_glue_reg(regbase, SATA_CTL, P0_IPDTXL);
	sata_clear_glue_reg(regbase, SATA_CTL, P0_IPDRXL);
	sata_clear_glue_reg(regbase, SATA_CTL, P0_IPDIPDMSYNTH);

	/* port1 */
	sata_clear_glue_reg(regbase, SATA_CTL, P1_IRST_POR);
	sata_clear_glue_reg(regbase, SATA_CTL, P1_IRST_HARD_TXRX);
	sata_clear_glue_reg(regbase, SATA_CTL, P1_IRST_HARD_SYNTH);
	sata_clear_glue_reg(regbase, SATA_CTL, P1_IPDTXL);
	sata_clear_glue_reg(regbase, SATA_CTL, P1_IPDRXL);
	sata_clear_glue_reg(regbase, SATA_CTL, P1_IPDIPDMSYNTH);
	udelay(300);

	/* Set PHY */
	sata_set_glue_reg(regbase, SATA_CTL, P0_IPDTXL);
	sata_set_glue_reg(regbase, SATA_CTL, P0_IPDRXL);
	sata_set_glue_reg(regbase, SATA_CTL, P0_IPDIPDMSYNTH);
	sata_set_glue_reg(regbase, SATA_CTL, P1_IPDTXL);
	sata_set_glue_reg(regbase, SATA_CTL, P1_IPDRXL);
	sata_set_glue_reg(regbase, SATA_CTL, P1_IPDIPDMSYNTH);

	udelay(1000);
	sata_set_glue_reg(regbase, SATA_CTL, P0_IRST_POR);
	udelay(1000);
	sata_set_glue_reg(regbase, SATA_CTL, P1_IRST_POR);
	udelay(1000);

	/* setup PHY */
	config_sata_phy(regbase);
	if (sata_phy_debug)
		verify_sata_phy_config(regbase);

	udelay(1000);
	sata_set_glue_reg(regbase, SATA_CTL, P0_IRST_HARD_TXRX);
	sata_set_glue_reg(regbase, SATA_CTL, P0_IRST_HARD_SYNTH);
	sata_set_glue_reg(regbase, SATA_CTL, P1_IRST_HARD_TXRX);
	sata_set_glue_reg(regbase, SATA_CTL, P1_IRST_HARD_SYNTH);
	udelay(300);

	/* Override reset in serial PHY mode */
	sata_set_glue_reg(regbase, CR_REG_TIMER, CR_TIME_SCALE);
	/* Set reset SATA */
	sata_set_glue_reg(regbase, SATA_CTL, SATA_RST_N);
	sata_set_glue_reg(regbase, SATA_CTL, M_CSYSREQ);
	sata_set_glue_reg(regbase, SATA_CTL, S_CSYSREQ);

	pr_debug("Waiting for PHYs to come up.\n");
	n = 10000;
	do {
		reg_val = nlm_read_sata_reg(regbase, SATA_STATUS);
		if ((reg_val & P1_PHY_READY) && (reg_val & P0_PHY_READY))
			break;
		udelay(10);
	} while (--n > 0);

	if (reg_val  & P0_PHY_READY)
		pr_info("PHY0 is up.\n");
	else
		pr_info("PHY0 is down.\n");
	if (reg_val  & P1_PHY_READY)
		pr_info("PHY1 is up.\n");
	else
		pr_info("PHY1 is down.\n");

	pr_info("XLP AHCI Init Done.\n");
}

static int __init nlm_ahci_init(void)
{
	int node;

	if (!cpu_is_xlp9xx())
		return 0;
	for (node = 0; node < NLM_NR_NODES; node++)
		if (nlm_node_present(node))
			nlm_sata_firmware_init(node);
	return 0;
}

static void nlm_sata_intr_ack(struct irq_data *data)
{
	u64 regbase;
	u32 val;
	int node;

	node = data->irq / NLM_IRQS_PER_NODE;
	regbase = nlm_get_sata_regbase(node);
	val = nlm_read_sata_reg(regbase, SATA_INT);
	sata_set_glue_reg(regbase, SATA_INT, val);
}

static void nlm_sata_fixup_bar(struct pci_dev *dev)
{
	dev->resource[5] = dev->resource[0];
	memset(&dev->resource[0], 0, sizeof(dev->resource[0]));
}

static void nlm_sata_fixup_final(struct pci_dev *dev)
{
	u32 val;
	u64 regbase;
	int node;

	/* Find end bridge function to find node */
	node = xlp_socdev_to_node(dev);
	regbase = nlm_get_sata_regbase(node);

	/* clear pending interrupts and then enable them */
	val = nlm_read_sata_reg(regbase, SATA_INT);
	sata_set_glue_reg(regbase, SATA_INT, val);

	/* Enable only the core interrupt */
	sata_set_glue_reg(regbase, SATA_INT_MASK, 0x1);

	dev->irq = nlm_irq_to_xirq(node, PIC_SATA_IRQ);
	nlm_set_pic_extra_ack(node, PIC_SATA_IRQ, nlm_sata_intr_ack);
}

arch_initcall(nlm_ahci_init);

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_XLP9XX_SATA,
		nlm_sata_fixup_bar);

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_XLP9XX_SATA,
		nlm_sata_fixup_final);
