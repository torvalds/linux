#include <linux/config.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/mips-boards/atlasint.h>

#define INTD		ATLASINT_INTD
#define INTC		ATLASINT_INTC
#define INTB		ATLASINT_INTB
#define INTA		ATLASINT_INTA
#define SCSI		ATLASINT_SCSI
#define ETH		ATLASINT_ETH

static char irq_tab[][5] __initdata = {
	/*      INTA    INTB    INTC    INTD */
	{0,	0,	0,	0,	0 },	/*  0: Unused */
	{0,	0,	0,	0,	0 },	/*  1: Unused */
	{0,	0,	0,	0,	0 },	/*  2: Unused */
	{0,	0,	0,	0,	0 },	/*  3: Unused */
	{0,	0,	0,	0,	0 },	/*  4: Unused */
	{0,	0,	0,	0,	0 },	/*  5: Unused */
	{0,	0,	0,	0,	0 },	/*  6: Unused */
	{0,	0,	0,	0,	0 },	/*  7: Unused */
	{0,	0,	0,	0,	0 },	/*  8: Unused */
	{0,	0,	0,	0,	0 },	/*  9: Unused */
	{0,	0,	0,	0,	0 },	/* 10: Unused */
	{0,	0,	0,	0,	0 },	/* 11: Unused */
	{0,	0,	0,	0,	0 },	/* 12: Unused */
	{0,	0,	0,	0,	0 },	/* 13: Unused */
	{0,	0,	0,	0,	0 },	/* 14: Unused */
	{0,	0,	0,	0,	0 },	/* 15: Unused */
	{0,	SCSI,	0,	0,	0 },	/* 16: SYM53C810A SCSI */
	{0,	0,	0,	0,	0 },	/* 17: Core */
	{0,	INTA,	INTB,	INTC,	INTD },	/* 18: PCI Slot 1 */
	{0,	ETH,	0,	0,	0 },	/* 19: SAA9730 Ethernet */
	{0,	0,	0,	0,	0 },	/* 20: PCI Slot 3 */
	{0,	0,	0,	0,	0 }	/* 21: PCI Slot 4 */
};

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return irq_tab[slot][pin];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

#ifdef CONFIG_KGDB
/*
 * The PCI scan may have moved the saa9730 I/O address, so reread
 * the address here.
 * This does mean that it's not possible to debug the PCI bus configuration
 * code, but it is better than nothing...
 */

static void atlas_saa9730_base_fixup (struct pci_dev *pdev)
{
	extern void *saa9730_base;
	if (pdev->bus == 0 && PCI_SLOT(pdev->devfn) == 19)
		(void) pci_read_config_dword (pdev, 0x14, (u32 *)&saa9730_base);
	printk ("saa9730_base = %x\n", saa9730_base);
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_PHILIPS, PCI_DEVICE_ID_PHILIPS_SAA9730,
	 atlas_saa9730_base_fixup);

#endif
