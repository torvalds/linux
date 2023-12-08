// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2004 ICT CAS
 * Author: Li xiaoyu, ICT CAS
 *   lixy@ict.ac.cn
 *
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 */
#include <linux/init.h>
#include <linux/pci.h>

#include <loongson.h>

/* South bridge slot number is set by the pci probe process */
static u8 sb_slot = 5;

int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq = 0;

	if (slot == sb_slot) {
		switch (PCI_FUNC(dev->devfn)) {
		case 2:
			irq = 10;
			break;
		case 3:
			irq = 11;
			break;
		case 5:
			irq = 9;
			break;
		}
	} else {
		irq = LOONGSON_IRQ_BASE + 25 + pin;
	}
	return irq;

}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

static void loongson2e_nec_fixup(struct pci_dev *pdev)
{
	unsigned int val;

	/* Configures port 1, 2, 3, 4 to be validate*/
	pci_read_config_dword(pdev, 0xe0, &val);
	pci_write_config_dword(pdev, 0xe0, (val & ~7) | 0x4);

	/* System clock is 48-MHz Oscillator. */
	pci_write_config_dword(pdev, 0xe4, 1 << 5);
}

static void loongson2e_686b_func0_fixup(struct pci_dev *pdev)
{
	unsigned char c;

	sb_slot = PCI_SLOT(pdev->devfn);

	printk(KERN_INFO "via686b fix: ISA bridge\n");

	/*  Enable I/O Recovery time */
	pci_write_config_byte(pdev, 0x40, 0x08);

	/*  Enable ISA refresh */
	pci_write_config_byte(pdev, 0x41, 0x01);

	/*  disable ISA line buffer */
	pci_write_config_byte(pdev, 0x45, 0x00);

	/*  Gate INTR, and flush line buffer */
	pci_write_config_byte(pdev, 0x46, 0xe0);

	/*  Disable PCI Delay Transaction, Enable EISA ports 4D0/4D1. */
	/* pci_write_config_byte(pdev, 0x47, 0x20); */

	/*
	 *  enable PCI Delay Transaction, Enable EISA ports 4D0/4D1.
	 *  enable time-out timer
	 */
	pci_write_config_byte(pdev, 0x47, 0xe6);

	/*
	 * enable level trigger on pci irqs: 9,10,11,13
	 * important! without this PCI interrupts won't work
	 */
	outb(0x2e, 0x4d1);

	/*  512 K PCI Decode */
	pci_write_config_byte(pdev, 0x48, 0x01);

	/*  Wait for PGNT before grant to ISA Master/DMA */
	pci_write_config_byte(pdev, 0x4a, 0x84);

	/*
	 * Plug'n'Play
	 *
	 *  Parallel DRQ 3, Floppy DRQ 2 (default)
	 */
	pci_write_config_byte(pdev, 0x50, 0x0e);

	/*
	 * IRQ Routing for Floppy and Parallel port
	 *
	 *  IRQ 6 for floppy, IRQ 7 for parallel port
	 */
	pci_write_config_byte(pdev, 0x51, 0x76);

	/* IRQ Routing for serial ports (take IRQ 3 and 4) */
	pci_write_config_byte(pdev, 0x52, 0x34);

	/*  All IRQ's level triggered. */
	pci_write_config_byte(pdev, 0x54, 0x00);

	/* route PIRQA-D irq */
	pci_write_config_byte(pdev, 0x55, 0x90);	/* bit 7-4, PIRQA */
	pci_write_config_byte(pdev, 0x56, 0xba);	/* bit 7-4, PIRQC; */
							/* 3-0, PIRQB */
	pci_write_config_byte(pdev, 0x57, 0xd0);	/* bit 7-4, PIRQD */

	/* enable function 5/6, audio/modem */
	pci_read_config_byte(pdev, 0x85, &c);
	c &= ~(0x3 << 2);
	pci_write_config_byte(pdev, 0x85, c);

	printk(KERN_INFO"via686b fix: ISA bridge done\n");
}

static void loongson2e_686b_func1_fixup(struct pci_dev *pdev)
{
	printk(KERN_INFO"via686b fix: IDE\n");

	/* Modify IDE controller setup */
	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 48);
	pci_write_config_byte(pdev, PCI_COMMAND,
			      PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
			      PCI_COMMAND_MASTER);
	pci_write_config_byte(pdev, 0x40, 0x0b);
	/* legacy mode */
	pci_write_config_byte(pdev, 0x42, 0x09);

#if 1/* play safe, otherwise we may see notebook's usb keyboard lockup */
	/* disable read prefetch/write post buffers */
	pci_write_config_byte(pdev, 0x41, 0x02);

	/* use 3/4 as fifo thresh hold	*/
	pci_write_config_byte(pdev, 0x43, 0x0a);
	pci_write_config_byte(pdev, 0x44, 0x00);

	pci_write_config_byte(pdev, 0x45, 0x00);
#else
	pci_write_config_byte(pdev, 0x41, 0xc2);
	pci_write_config_byte(pdev, 0x43, 0x35);
	pci_write_config_byte(pdev, 0x44, 0x1c);

	pci_write_config_byte(pdev, 0x45, 0x10);
#endif

	printk(KERN_INFO"via686b fix: IDE done\n");
}

static void loongson2e_686b_func2_fixup(struct pci_dev *pdev)
{
	/* irq routing */
	pci_write_config_byte(pdev, PCI_INTERRUPT_LINE, 10);
}

static void loongson2e_686b_func3_fixup(struct pci_dev *pdev)
{
	/* irq routing */
	pci_write_config_byte(pdev, PCI_INTERRUPT_LINE, 11);
}

static void loongson2e_686b_func5_fixup(struct pci_dev *pdev)
{
	unsigned int val;
	unsigned char c;

	/* enable IO */
	pci_write_config_byte(pdev, PCI_COMMAND,
			      PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
			      PCI_COMMAND_MASTER);
	pci_read_config_dword(pdev, 0x4, &val);
	pci_write_config_dword(pdev, 0x4, val | 1);

	/* route ac97 IRQ */
	pci_write_config_byte(pdev, 0x3c, 9);

	pci_read_config_byte(pdev, 0x8, &c);

	/* link control: enable link & SGD PCM output */
	pci_write_config_byte(pdev, 0x41, 0xcc);

	/* disable game port, FM, midi, sb, enable write to reg2c-2f */
	pci_write_config_byte(pdev, 0x42, 0x20);

	/* we are using Avance logic codec */
	pci_write_config_word(pdev, 0x2c, 0x1005);
	pci_write_config_word(pdev, 0x2e, 0x4710);
	pci_read_config_dword(pdev, 0x2c, &val);

	pci_write_config_byte(pdev, 0x42, 0x0);
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C686,
			 loongson2e_686b_func0_fixup);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C586_1,
			 loongson2e_686b_func1_fixup);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C586_2,
			 loongson2e_686b_func2_fixup);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C586_3,
			 loongson2e_686b_func3_fixup);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C686_5,
			 loongson2e_686b_func5_fixup);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NEC, PCI_DEVICE_ID_NEC_USB,
			 loongson2e_nec_fixup);
