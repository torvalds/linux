/*
 * Copyright (C) 2008 Lemote Technology
 * Copyright (C) 2004 ICT CAS
 * Author: Li xiaoyu, lixy@ict.ac.cn
 *
 * Copyright (C) 2007 Lemote, Inc.
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/init.h>
#include <linux/pci.h>

#include <loongson.h>
#include <cs5536/cs5536.h>
#include <cs5536/cs5536_pci.h>

/* PCI interrupt pins
 *
 * These should not be changed, or you should consider loongson2f interrupt
 * register and your pci card dispatch
 */

#define PCIA		4
#define PCIB		5
#define PCIC		6
#define PCID		7

/* all the pci device has the PCIA pin, check the datasheet. */
static char irq_tab[][5] __initdata = {
	/*	INTA	INTB	INTC	INTD */
	{0, 0, 0, 0, 0},	/*  11: Unused */
	{0, 0, 0, 0, 0},	/*  12: Unused */
	{0, 0, 0, 0, 0},	/*  13: Unused */
	{0, 0, 0, 0, 0},	/*  14: Unused */
	{0, 0, 0, 0, 0},	/*  15: Unused */
	{0, 0, 0, 0, 0},	/*  16: Unused */
	{0, PCIA, 0, 0, 0},	/*  17: RTL8110-0 */
	{0, PCIB, 0, 0, 0},	/*  18: RTL8110-1 */
	{0, PCIC, 0, 0, 0},	/*  19: SiI3114 */
	{0, PCID, 0, 0, 0},	/*  20: 3-ports nec usb */
	{0, PCIA, PCIB, PCIC, PCID},	/*  21: PCI-SLOT */
	{0, 0, 0, 0, 0},	/*  22: Unused */
	{0, 0, 0, 0, 0},	/*  23: Unused */
	{0, 0, 0, 0, 0},	/*  24: Unused */
	{0, 0, 0, 0, 0},	/*  25: Unused */
	{0, 0, 0, 0, 0},	/*  26: Unused */
	{0, 0, 0, 0, 0},	/*  27: Unused */
};

int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int virq;

	if ((PCI_SLOT(dev->devfn) != PCI_IDSEL_CS5536)
	    && (PCI_SLOT(dev->devfn) < 32)) {
		virq = irq_tab[slot][pin];
		printk(KERN_INFO "slot: %d, pin: %d, irq: %d\n", slot, pin,
		       virq + LOONGSON_IRQ_BASE);
		if (virq != 0)
			return LOONGSON_IRQ_BASE + virq;
		else
			return 0;
	} else if (PCI_SLOT(dev->devfn) == PCI_IDSEL_CS5536) {	/*  cs5536 */
		switch (PCI_FUNC(dev->devfn)) {
		case 2:
			pci_write_config_byte(dev, PCI_INTERRUPT_LINE,
					      CS5536_IDE_INTR);
			return CS5536_IDE_INTR; /*  for IDE */
		case 3:
			pci_write_config_byte(dev, PCI_INTERRUPT_LINE,
					      CS5536_ACC_INTR);
			return CS5536_ACC_INTR; /*  for AUDIO */
		case 4: /*  for OHCI */
		case 5: /*  for EHCI */
		case 6: /*  for UDC */
		case 7: /*  for OTG */
			pci_write_config_byte(dev, PCI_INTERRUPT_LINE,
					      CS5536_USB_INTR);
			return CS5536_USB_INTR;
		}
		return dev->irq;
	} else {
		printk(KERN_INFO " strange pci slot number.\n");
		return 0;
	}
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

/* CS5536 SPEC. fixup */
static void loongson_cs5536_isa_fixup(struct pci_dev *pdev)
{
	/* the uart1 and uart2 interrupt in PIC is enabled as default */
	pci_write_config_dword(pdev, PCI_UART1_INT_REG, 1);
	pci_write_config_dword(pdev, PCI_UART2_INT_REG, 1);
}

static void loongson_cs5536_ide_fixup(struct pci_dev *pdev)
{
	/* setting the mutex pin as IDE function */
	pci_write_config_dword(pdev, PCI_IDE_CFG_REG,
			       CS5536_IDE_FLASH_SIGNATURE);
}

static void loongson_cs5536_acc_fixup(struct pci_dev *pdev)
{
	/* enable the AUDIO interrupt in PIC  */
	pci_write_config_dword(pdev, PCI_ACC_INT_REG, 1);

	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0xc0);
}

static void loongson_cs5536_ohci_fixup(struct pci_dev *pdev)
{
	/* enable the OHCI interrupt in PIC */
	/* THE OHCI, EHCI, UDC, OTG are shared with interrupt in PIC */
	pci_write_config_dword(pdev, PCI_OHCI_INT_REG, 1);
}

static void loongson_cs5536_ehci_fixup(struct pci_dev *pdev)
{
	u32 hi, lo;

	/* Serial short detect enable */
	_rdmsr(USB_MSR_REG(USB_CONFIG), &hi, &lo);
	_wrmsr(USB_MSR_REG(USB_CONFIG), (1 << 1) | (1 << 3), lo);

	/* setting the USB2.0 micro frame length */
	pci_write_config_dword(pdev, PCI_EHCI_FLADJ_REG, 0x2000);
}

static void loongson_nec_fixup(struct pci_dev *pdev)
{
	unsigned int val;

	pci_read_config_dword(pdev, 0xe0, &val);
	/* Only 2 port be used */
	pci_write_config_dword(pdev, 0xe0, (val & ~3) | 0x2);
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_CS5536_ISA,
			 loongson_cs5536_isa_fixup);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_CS5536_OHC,
			 loongson_cs5536_ohci_fixup);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_CS5536_EHC,
			 loongson_cs5536_ehci_fixup);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_CS5536_AUDIO,
			 loongson_cs5536_acc_fixup);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_CS5536_IDE,
			 loongson_cs5536_ide_fixup);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NEC, PCI_DEVICE_ID_NEC_USB,
			 loongson_nec_fixup);
