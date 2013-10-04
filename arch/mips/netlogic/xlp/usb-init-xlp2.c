/*
 * Copyright (c) 2003-2013 Broadcom Corporation
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
#include <linux/platform_device.h>
#include <linux/irq.h>

#include <asm/netlogic/common.h>
#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/xlp-hal/xlp.h>

#define XLPII_USB3_CTL_0		0xc0
#define XLPII_VAUXRST			BIT(0)
#define XLPII_VCCRST			BIT(1)
#define XLPII_NUM2PORT			9
#define XLPII_NUM3PORT			13
#define XLPII_RTUNEREQ			BIT(20)
#define XLPII_MS_CSYSREQ		BIT(21)
#define XLPII_XS_CSYSREQ		BIT(22)
#define XLPII_RETENABLEN		BIT(23)
#define XLPII_TX2RX			BIT(24)
#define XLPII_XHCIREV			BIT(25)
#define XLPII_ECCDIS			BIT(26)

#define XLPII_USB3_INT_REG		0xc2
#define XLPII_USB3_INT_MASK		0xc3

#define XLPII_USB_PHY_TEST		0xc6
#define XLPII_PRESET			BIT(0)
#define XLPII_ATERESET			BIT(1)
#define XLPII_LOOPEN			BIT(2)
#define XLPII_TESTPDHSP			BIT(3)
#define XLPII_TESTPDSSP			BIT(4)
#define XLPII_TESTBURNIN		BIT(5)

#define XLPII_USB_PHY_LOS_LV		0xc9
#define XLPII_LOSLEV			0
#define XLPII_LOSBIAS			5
#define XLPII_SQRXTX			8
#define XLPII_TXBOOST			11
#define XLPII_RSLKSEL			16
#define XLPII_FSEL			20

#define XLPII_USB_RFCLK_REG		0xcc
#define XLPII_VVLD			30

#define nlm_read_usb_reg(b, r)		nlm_read_reg(b, r)
#define nlm_write_usb_reg(b, r, v)	nlm_write_reg(b, r, v)

#define nlm_xlpii_get_usb_pcibase(node, inst)		\
	nlm_pcicfg_base(XLP2XX_IO_USB_OFFSET(node, inst))
#define nlm_xlpii_get_usb_regbase(node, inst)		\
	(nlm_xlpii_get_usb_pcibase(node, inst) + XLP_IO_PCI_HDRSZ)

static void xlpii_usb_ack(struct irq_data *data)
{
	u64 port_addr;

	switch (data->irq) {
	case PIC_2XX_XHCI_0_IRQ:
		port_addr = nlm_xlpii_get_usb_regbase(0, 1);
		break;
	case PIC_2XX_XHCI_1_IRQ:
		port_addr = nlm_xlpii_get_usb_regbase(0, 2);
		break;
	case PIC_2XX_XHCI_2_IRQ:
		port_addr = nlm_xlpii_get_usb_regbase(0, 3);
		break;
	default:
		pr_err("No matching USB irq!\n");
		return;
	}
	nlm_write_usb_reg(port_addr, XLPII_USB3_INT_REG, 0xffffffff);
}

static void nlm_xlpii_usb_hw_reset(int node, int port)
{
	u64 port_addr, xhci_base, pci_base;
	void __iomem *corebase;
	u32 val;

	port_addr = nlm_xlpii_get_usb_regbase(node, port);

	/* Set frequency */
	val = nlm_read_usb_reg(port_addr, XLPII_USB_PHY_LOS_LV);
	val &= ~(0x3f << XLPII_FSEL);
	val |= (0x27 << XLPII_FSEL);
	nlm_write_usb_reg(port_addr, XLPII_USB_PHY_LOS_LV, val);

	val = nlm_read_usb_reg(port_addr, XLPII_USB_RFCLK_REG);
	val |= (1 << XLPII_VVLD);
	nlm_write_usb_reg(port_addr, XLPII_USB_RFCLK_REG, val);

	/* PHY reset */
	val = nlm_read_usb_reg(port_addr, XLPII_USB_PHY_TEST);
	val &= (XLPII_ATERESET | XLPII_LOOPEN | XLPII_TESTPDHSP
		| XLPII_TESTPDSSP | XLPII_TESTBURNIN);
	nlm_write_usb_reg(port_addr, XLPII_USB_PHY_TEST, val);

	/* Setup control register */
	val =  XLPII_VAUXRST | XLPII_VCCRST | (1 << XLPII_NUM2PORT)
		| (1 << XLPII_NUM3PORT) | XLPII_MS_CSYSREQ | XLPII_XS_CSYSREQ
		| XLPII_RETENABLEN | XLPII_XHCIREV;
	nlm_write_usb_reg(port_addr, XLPII_USB3_CTL_0, val);

	/* Enable interrupts */
	nlm_write_usb_reg(port_addr, XLPII_USB3_INT_MASK, 0x00000001);

	/* Clear all interrupts */
	nlm_write_usb_reg(port_addr, XLPII_USB3_INT_REG, 0xffffffff);

	udelay(2000);

	/* XHCI configuration at PCI mem */
	pci_base = nlm_xlpii_get_usb_pcibase(node, port);
	xhci_base = nlm_read_usb_reg(pci_base, 0x4) & ~0xf;
	corebase = ioremap(xhci_base, 0x10000);
	if (!corebase)
		return;

	writel(0x240002, corebase + 0xc2c0);
	/* GCTL 0xc110 */
	val = readl(corebase + 0xc110);
	val &= ~(0x3 << 12);
	val |= (1 << 12);
	writel(val, corebase + 0xc110);
	udelay(100);

	/* PHYCFG 0xc200 */
	val = readl(corebase + 0xc200);
	val &= ~(1 << 6);
	writel(val, corebase + 0xc200);
	udelay(100);

	/* PIPECTL 0xc2c0 */
	val = readl(corebase + 0xc2c0);
	val &= ~(1 << 17);
	writel(val, corebase + 0xc2c0);

	iounmap(corebase);
}

static int __init nlm_platform_xlpii_usb_init(void)
{
	if (!cpu_is_xlpii())
		return 0;

	pr_info("Initializing 2XX USB Interface\n");
	nlm_xlpii_usb_hw_reset(0, 1);
	nlm_xlpii_usb_hw_reset(0, 2);
	nlm_xlpii_usb_hw_reset(0, 3);
	nlm_set_pic_extra_ack(0, PIC_2XX_XHCI_0_IRQ, xlpii_usb_ack);
	nlm_set_pic_extra_ack(0, PIC_2XX_XHCI_1_IRQ, xlpii_usb_ack);
	nlm_set_pic_extra_ack(0, PIC_2XX_XHCI_2_IRQ, xlpii_usb_ack);

	return 0;
}

arch_initcall(nlm_platform_xlpii_usb_init);

static u64 xlp_usb_dmamask = ~(u32)0;

/* Fixup IRQ for USB devices on XLP the SoC PCIe bus */
static void nlm_usb_fixup_final(struct pci_dev *dev)
{
	dev->dev.dma_mask		= &xlp_usb_dmamask;
	dev->dev.coherent_dma_mask	= DMA_BIT_MASK(32);
	switch (dev->devfn) {
	case 0x21:
		dev->irq = PIC_2XX_XHCI_0_IRQ;
		break;
	case 0x22:
		dev->irq = PIC_2XX_XHCI_1_IRQ;
		break;
	case 0x23:
		dev->irq = PIC_2XX_XHCI_2_IRQ;
		break;
	}
}

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_NETLOGIC, PCI_DEVICE_ID_NLM_XHCI,
		nlm_usb_fixup_final);
