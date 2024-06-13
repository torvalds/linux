// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/mips-boards/piix4.h>

/* PCI interrupt pins */
#define PCIA		1
#define PCIB		2
#define PCIC		3
#define PCID		4

/* This table is filled in by interrogating the PIIX4 chip */
static char pci_irq[5] = {
};

static char irq_tab[][5] = {
	/*	INTA	INTB	INTC	INTD */
	{0,	0,	0,	0,	0 },	/*  0: GT64120 PCI bridge */
	{0,	0,	0,	0,	0 },	/*  1: Unused */
	{0,	0,	0,	0,	0 },	/*  2: Unused */
	{0,	0,	0,	0,	0 },	/*  3: Unused */
	{0,	0,	0,	0,	0 },	/*  4: Unused */
	{0,	0,	0,	0,	0 },	/*  5: Unused */
	{0,	0,	0,	0,	0 },	/*  6: Unused */
	{0,	0,	0,	0,	0 },	/*  7: Unused */
	{0,	0,	0,	0,	0 },	/*  8: Unused */
	{0,	0,	0,	0,	0 },	/*  9: Unused */
	{0,	0,	0,	0,	PCID }, /* 10: PIIX4 USB */
	{0,	PCIB,	0,	0,	0 },	/* 11: AMD 79C973 Ethernet */
	{0,	PCIC,	0,	0,	0 },	/* 12: Crystal 4281 Sound */
	{0,	0,	0,	0,	0 },	/* 13: Unused */
	{0,	0,	0,	0,	0 },	/* 14: Unused */
	{0,	0,	0,	0,	0 },	/* 15: Unused */
	{0,	0,	0,	0,	0 },	/* 16: Unused */
	{0,	0,	0,	0,	0 },	/* 17: Bonito/SOC-it PCI Bridge*/
	{0,	PCIA,	PCIB,	PCIC,	PCID }, /* 18: PCI Slot 1 */
	{0,	PCIB,	PCIC,	PCID,	PCIA }, /* 19: PCI Slot 2 */
	{0,	PCIC,	PCID,	PCIA,	PCIB }, /* 20: PCI Slot 3 */
	{0,	PCID,	PCIA,	PCIB,	PCIC }	/* 21: PCI Slot 4 */
};

int pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int virq;
	virq = irq_tab[slot][pin];
	return pci_irq[virq];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

static void malta_piix_func3_base_fixup(struct pci_dev *dev)
{
	/* Set a sane PM I/O base address */
	pci_write_config_word(dev, PIIX4_FUNC3_PMBA, 0x1000);

	/* Enable access to the PM I/O region */
	pci_write_config_byte(dev, PIIX4_FUNC3_PMREGMISC,
			      PIIX4_FUNC3_PMREGMISC_EN);
}

DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_3,
			malta_piix_func3_base_fixup);

static void malta_piix_func0_fixup(struct pci_dev *pdev)
{
	unsigned char reg_val;
	u32 reg_val32;
	u16 reg_val16;
	/* PIIX PIRQC[A:D] irq mappings */
	static int piixirqmap[PIIX4_FUNC0_PIRQRC_IRQ_ROUTING_MAX] = {
		0,  0,	0,  3,
		4,  5,	6,  7,
		0,  9, 10, 11,
		12, 0, 14, 15
	};
	int i;

	/* Interrogate PIIX4 to get PCI IRQ mapping */
	for (i = 0; i <= 3; i++) {
		pci_read_config_byte(pdev, PIIX4_FUNC0_PIRQRC+i, &reg_val);
		if (reg_val & PIIX4_FUNC0_PIRQRC_IRQ_ROUTING_DISABLE)
			pci_irq[PCIA+i] = 0;	/* Disabled */
		else
			pci_irq[PCIA+i] = piixirqmap[reg_val &
				PIIX4_FUNC0_PIRQRC_IRQ_ROUTING_MASK];
	}

	/* Done by YAMON 2.00 onwards */
	if (PCI_SLOT(pdev->devfn) == 10) {
		/*
		 * Set top of main memory accessible by ISA or DMA
		 * devices to 16 Mb.
		 */
		pci_read_config_byte(pdev, PIIX4_FUNC0_TOM, &reg_val);
		pci_write_config_byte(pdev, PIIX4_FUNC0_TOM, reg_val |
				PIIX4_FUNC0_TOM_TOP_OF_MEMORY_MASK);
	}

	/* Mux SERIRQ to its pin */
	pci_read_config_dword(pdev, PIIX4_FUNC0_GENCFG, &reg_val32);
	pci_write_config_dword(pdev, PIIX4_FUNC0_GENCFG,
			       reg_val32 | PIIX4_FUNC0_GENCFG_SERIRQ);

	/* Enable SERIRQ */
	pci_read_config_byte(pdev, PIIX4_FUNC0_SERIRQC, &reg_val);
	reg_val |= PIIX4_FUNC0_SERIRQC_EN | PIIX4_FUNC0_SERIRQC_CONT;
	pci_write_config_byte(pdev, PIIX4_FUNC0_SERIRQC, reg_val);

	/* Enable response to special cycles */
	pci_read_config_word(pdev, PCI_COMMAND, &reg_val16);
	pci_write_config_word(pdev, PCI_COMMAND,
			      reg_val16 | PCI_COMMAND_SPECIAL);
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_0,
	 malta_piix_func0_fixup);

static void malta_piix_func1_fixup(struct pci_dev *pdev)
{
	unsigned char reg_val;

	/* Done by YAMON 2.02 onwards */
	if (PCI_SLOT(pdev->devfn) == 10) {
		/*
		 * IDE Decode enable.
		 */
		pci_read_config_byte(pdev, PIIX4_FUNC1_IDETIM_PRIMARY_HI,
			&reg_val);
		pci_write_config_byte(pdev, PIIX4_FUNC1_IDETIM_PRIMARY_HI,
			reg_val|PIIX4_FUNC1_IDETIM_PRIMARY_HI_IDE_DECODE_EN);
		pci_read_config_byte(pdev, PIIX4_FUNC1_IDETIM_SECONDARY_HI,
			&reg_val);
		pci_write_config_byte(pdev, PIIX4_FUNC1_IDETIM_SECONDARY_HI,
			reg_val|PIIX4_FUNC1_IDETIM_SECONDARY_HI_IDE_DECODE_EN);
	}
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB,
	 malta_piix_func1_fixup);

/* Enable PCI 2.1 compatibility in PIIX4 */
static void quirk_dlcsetup(struct pci_dev *dev)
{
	u8 odlc, ndlc;

	(void) pci_read_config_byte(dev, PIIX4_FUNC0_DLC, &odlc);
	/* Enable passive releases and delayed transaction */
	ndlc = odlc | PIIX4_FUNC0_DLC_USBPR_EN |
		      PIIX4_FUNC0_DLC_PASSIVE_RELEASE_EN |
		      PIIX4_FUNC0_DLC_DELAYED_TRANSACTION_EN;
	(void) pci_write_config_byte(dev, PIIX4_FUNC0_DLC, ndlc);
}

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_0,
	quirk_dlcsetup);
