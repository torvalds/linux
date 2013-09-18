/*
 * Copyright (c) 2003-2012 Broadcom Corporation
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

#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/xlp-hal/xlp.h>

/*
 * USB glue logic registers, used only during initialization
 */
#define USB_CTL_0			0x01
#define USB_PHY_0			0x0A
#define USB_PHY_RESET			0x01
#define USB_PHY_PORT_RESET_0		0x10
#define USB_PHY_PORT_RESET_1		0x20
#define USB_CONTROLLER_RESET		0x01
#define USB_INT_STATUS			0x0E
#define USB_INT_EN			0x0F
#define USB_PHY_INTERRUPT_EN		0x01
#define USB_OHCI_INTERRUPT_EN		0x02
#define USB_OHCI_INTERRUPT1_EN		0x04
#define USB_OHCI_INTERRUPT2_EN		0x08
#define USB_CTRL_INTERRUPT_EN		0x10

#define nlm_read_usb_reg(b, r)			nlm_read_reg(b, r)
#define nlm_write_usb_reg(b, r, v)		nlm_write_reg(b, r, v)
#define nlm_get_usb_pcibase(node, inst)		\
	nlm_pcicfg_base(XLP_IO_USB_OFFSET(node, inst))
#define nlm_get_usb_regbase(node, inst)		\
	(nlm_get_usb_pcibase(node, inst) + XLP_IO_PCI_HDRSZ)

static void nlm_usb_intr_en(int node, int port)
{
	uint32_t val;
	uint64_t port_addr;

	port_addr = nlm_get_usb_regbase(node, port);
	val = nlm_read_usb_reg(port_addr, USB_INT_EN);
	val = USB_CTRL_INTERRUPT_EN  | USB_OHCI_INTERRUPT_EN |
		USB_OHCI_INTERRUPT1_EN | USB_CTRL_INTERRUPT_EN	|
		USB_OHCI_INTERRUPT_EN | USB_OHCI_INTERRUPT2_EN;
	nlm_write_usb_reg(port_addr, USB_INT_EN, val);
}

static void nlm_usb_hw_reset(int node, int port)
{
	uint64_t port_addr;
	uint32_t val;

	/* reset USB phy */
	port_addr = nlm_get_usb_regbase(node, port);
	val = nlm_read_usb_reg(port_addr, USB_PHY_0);
	val &= ~(USB_PHY_RESET | USB_PHY_PORT_RESET_0 | USB_PHY_PORT_RESET_1);
	nlm_write_usb_reg(port_addr, USB_PHY_0, val);

	mdelay(100);
	val = nlm_read_usb_reg(port_addr, USB_CTL_0);
	val &= ~(USB_CONTROLLER_RESET);
	val |= 0x4;
	nlm_write_usb_reg(port_addr, USB_CTL_0, val);
}

static int __init nlm_platform_usb_init(void)
{
	pr_info("Initializing USB Interface\n");
	nlm_usb_hw_reset(0, 0);
	nlm_usb_hw_reset(0, 3);

	/* Enable PHY interrupts */
	nlm_usb_intr_en(0, 0);
	nlm_usb_intr_en(0, 3);

	return 0;
}

arch_initcall(nlm_platform_usb_init);

static u64 xlp_usb_dmamask = ~(u32)0;

/* Fixup the IRQ for USB devices which is exist on XLP SOC PCIE bus */
static void nlm_usb_fixup_final(struct pci_dev *dev)
{
	dev->dev.dma_mask		= &xlp_usb_dmamask;
	dev->dev.coherent_dma_mask	= DMA_BIT_MASK(32);
	switch (dev->devfn) {
	case 0x10:
		dev->irq = PIC_EHCI_0_IRQ;
		break;
	case 0x11:
		dev->irq = PIC_OHCI_0_IRQ;
		break;
	case 0x12:
		dev->irq = PIC_OHCI_1_IRQ;
		break;
	case 0x13:
		dev->irq = PIC_EHCI_1_IRQ;
		break;
	case 0x14:
		dev->irq = PIC_OHCI_2_IRQ;
		break;
	case 0x15:
		dev->irq = PIC_OHCI_3_IRQ;
		break;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_NETLOGIC, PCI_DEVICE_ID_NLM_EHCI,
		nlm_usb_fixup_final);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_NETLOGIC, PCI_DEVICE_ID_NLM_OHCI,
		nlm_usb_fixup_final);
