/*
 *  This file contains work-arounds for many known PCI hardware
 *  bugs.  Devices present only on certain architectures (host
 *  bridges et cetera) should be handled in arch-specific code.
 *
 *  Note: any quirks for hotpluggable devices must _NOT_ be declared __init.
 *
 *  Copyright (c) 1999 Martin Mares <mj@ucw.cz>
 *
 *  Init/reset quirks for USB host controllers should be in the
 *  USB quirks file, where their drivers can access reuse it.
 *
 *  The bridge optimization stuff has been removed. If you really
 *  have a silly BIOS which is unable to set your host bridge right,
 *  use the PowerTweak utility (see http://powertweak.sourceforge.net).
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/acpi.h>
#include "pci.h"

/* The Mellanox Tavor device gives false positive parity errors
 * Mark this device with a broken_parity_status, to allow
 * PCI scanning code to "skip" this now blacklisted device.
 */
static void __devinit quirk_mellanox_tavor(struct pci_dev *dev)
{
	dev->broken_parity_status = 1;	/* This device gives false positives */
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_MELLANOX,PCI_DEVICE_ID_MELLANOX_TAVOR,quirk_mellanox_tavor);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_MELLANOX,PCI_DEVICE_ID_MELLANOX_TAVOR_BRIDGE,quirk_mellanox_tavor);

/* Deal with broken BIOS'es that neglect to enable passive release,
   which can cause problems in combination with the 82441FX/PPro MTRRs */
static void quirk_passive_release(struct pci_dev *dev)
{
	struct pci_dev *d = NULL;
	unsigned char dlc;

	/* We have to make sure a particular bit is set in the PIIX3
	   ISA bridge, so we have to go out and find it. */
	while ((d = pci_get_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_0, d))) {
		pci_read_config_byte(d, 0x82, &dlc);
		if (!(dlc & 1<<1)) {
			printk(KERN_ERR "PCI: PIIX3: Enabling Passive Release on %s\n", pci_name(d));
			dlc |= 1<<1;
			pci_write_config_byte(d, 0x82, dlc);
		}
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_passive_release );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_passive_release );

/*  The VIA VP2/VP3/MVP3 seem to have some 'features'. There may be a workaround
    but VIA don't answer queries. If you happen to have good contacts at VIA
    ask them for me please -- Alan 
    
    This appears to be BIOS not version dependent. So presumably there is a 
    chipset level fix */
int isa_dma_bridge_buggy;
EXPORT_SYMBOL(isa_dma_bridge_buggy);
    
static void __devinit quirk_isa_dma_hangs(struct pci_dev *dev)
{
	if (!isa_dma_bridge_buggy) {
		isa_dma_bridge_buggy=1;
		printk(KERN_INFO "Activating ISA DMA hang workarounds.\n");
	}
}
	/*
	 * Its not totally clear which chipsets are the problematic ones
	 * We know 82C586 and 82C596 variants are affected.
	 */
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_0,	quirk_isa_dma_hangs );
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C596,	quirk_isa_dma_hangs );
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82371SB_0,  quirk_isa_dma_hangs );
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AL,	PCI_DEVICE_ID_AL_M1533, 	quirk_isa_dma_hangs );
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NEC,	PCI_DEVICE_ID_NEC_CBUS_1,	quirk_isa_dma_hangs );
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NEC,	PCI_DEVICE_ID_NEC_CBUS_2,	quirk_isa_dma_hangs );
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NEC,	PCI_DEVICE_ID_NEC_CBUS_3,	quirk_isa_dma_hangs );

int pci_pci_problems;
EXPORT_SYMBOL(pci_pci_problems);

/*
 *	Chipsets where PCI->PCI transfers vanish or hang
 */
static void __devinit quirk_nopcipci(struct pci_dev *dev)
{
	if ((pci_pci_problems & PCIPCI_FAIL)==0) {
		printk(KERN_INFO "Disabling direct PCI/PCI transfers.\n");
		pci_pci_problems |= PCIPCI_FAIL;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_5597,		quirk_nopcipci );
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_496,		quirk_nopcipci );

static void __devinit quirk_nopciamd(struct pci_dev *dev)
{
	u8 rev;
	pci_read_config_byte(dev, 0x08, &rev);
	if (rev == 0x13) {
		/* Erratum 24 */
		printk(KERN_INFO "Chipset erratum: Disabling direct PCI/AGP transfers.\n");
		pci_pci_problems |= PCIAGP_FAIL;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_8151_0,	quirk_nopciamd );

/*
 *	Triton requires workarounds to be used by the drivers
 */
static void __devinit quirk_triton(struct pci_dev *dev)
{
	if ((pci_pci_problems&PCIPCI_TRITON)==0) {
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems |= PCIPCI_TRITON;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82437, 	quirk_triton ); 
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82437VX, 	quirk_triton ); 
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82439, 	quirk_triton ); 
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82439TX, 	quirk_triton ); 

/*
 *	VIA Apollo KT133 needs PCI latency patch
 *	Made according to a windows driver based patch by George E. Breese
 *	see PCI Latency Adjust on http://www.viahardware.com/download/viatweak.shtm
 *      Also see http://www.au-ja.org/review-kt133a-1-en.phtml for
 *      the info on which Mr Breese based his work.
 *
 *	Updated based on further information from the site and also on
 *	information provided by VIA 
 */
static void quirk_vialatency(struct pci_dev *dev)
{
	struct pci_dev *p;
	u8 rev;
	u8 busarb;
	/* Ok we have a potential problem chipset here. Now see if we have
	   a buggy southbridge */
	   
	p = pci_get_device(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_82C686, NULL);
	if (p!=NULL) {
		pci_read_config_byte(p, PCI_CLASS_REVISION, &rev);
		/* 0x40 - 0x4f == 686B, 0x10 - 0x2f == 686A; thanks Dan Hollis */
		/* Check for buggy part revisions */
		if (rev < 0x40 || rev > 0x42)
			goto exit;
	} else {
		p = pci_get_device(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8231, NULL);
		if (p==NULL)	/* No problem parts */
			goto exit;
		pci_read_config_byte(p, PCI_CLASS_REVISION, &rev);
		/* Check for buggy part revisions */
		if (rev < 0x10 || rev > 0x12) 
			goto exit;
	}
	
	/*
	 *	Ok we have the problem. Now set the PCI master grant to 
	 *	occur every master grant. The apparent bug is that under high
	 *	PCI load (quite common in Linux of course) you can get data
	 *	loss when the CPU is held off the bus for 3 bus master requests
	 *	This happens to include the IDE controllers....
	 *
	 *	VIA only apply this fix when an SB Live! is present but under
	 *	both Linux and Windows this isnt enough, and we have seen
	 *	corruption without SB Live! but with things like 3 UDMA IDE
	 *	controllers. So we ignore that bit of the VIA recommendation..
	 */

	pci_read_config_byte(dev, 0x76, &busarb);
	/* Set bit 4 and bi 5 of byte 76 to 0x01 
	   "Master priority rotation on every PCI master grant */
	busarb &= ~(1<<5);
	busarb |= (1<<4);
	pci_write_config_byte(dev, 0x76, busarb);
	printk(KERN_INFO "Applying VIA southbridge workaround.\n");
exit:
	pci_dev_put(p);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8363_0,	quirk_vialatency );
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8371_1,	quirk_vialatency );
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8361,		quirk_vialatency );
/* Must restore this on a resume from RAM */
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8363_0,	quirk_vialatency );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8371_1,	quirk_vialatency );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8361,		quirk_vialatency );

/*
 *	VIA Apollo VP3 needs ETBF on BT848/878
 */
static void __devinit quirk_viaetbf(struct pci_dev *dev)
{
	if ((pci_pci_problems&PCIPCI_VIAETBF)==0) {
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems |= PCIPCI_VIAETBF;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C597_0,	quirk_viaetbf );

static void __devinit quirk_vsfx(struct pci_dev *dev)
{
	if ((pci_pci_problems&PCIPCI_VSFX)==0) {
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems |= PCIPCI_VSFX;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C576,	quirk_vsfx );

/*
 *	Ali Magik requires workarounds to be used by the drivers
 *	that DMA to AGP space. Latency must be set to 0xA and triton
 *	workaround applied too
 *	[Info kindly provided by ALi]
 */	
static void __init quirk_alimagik(struct pci_dev *dev)
{
	if ((pci_pci_problems&PCIPCI_ALIMAGIK)==0) {
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems |= PCIPCI_ALIMAGIK|PCIPCI_TRITON;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AL, 	PCI_DEVICE_ID_AL_M1647, 	quirk_alimagik );
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AL, 	PCI_DEVICE_ID_AL_M1651, 	quirk_alimagik );

/*
 *	Natoma has some interesting boundary conditions with Zoran stuff
 *	at least
 */
static void __devinit quirk_natoma(struct pci_dev *dev)
{
	if ((pci_pci_problems&PCIPCI_NATOMA)==0) {
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems |= PCIPCI_NATOMA;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82441, 	quirk_natoma ); 
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443LX_0, 	quirk_natoma ); 
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443LX_1, 	quirk_natoma ); 
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443BX_0, 	quirk_natoma ); 
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443BX_1, 	quirk_natoma ); 
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443BX_2, 	quirk_natoma );

/*
 *  This chip can cause PCI parity errors if config register 0xA0 is read
 *  while DMAs are occurring.
 */
static void __devinit quirk_citrine(struct pci_dev *dev)
{
	dev->cfg_size = 0xA0;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_IBM,	PCI_DEVICE_ID_IBM_CITRINE,	quirk_citrine );

/*
 *  S3 868 and 968 chips report region size equal to 32M, but they decode 64M.
 *  If it's needed, re-allocate the region.
 */
static void __devinit quirk_s3_64M(struct pci_dev *dev)
{
	struct resource *r = &dev->resource[0];

	if ((r->start & 0x3ffffff) || r->end != r->start + 0x3ffffff) {
		r->start = 0;
		r->end = 0x3ffffff;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_868,		quirk_s3_64M );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_968,		quirk_s3_64M );

static void __devinit quirk_io_region(struct pci_dev *dev, unsigned region,
	unsigned size, int nr, const char *name)
{
	region &= ~(size-1);
	if (region) {
		struct pci_bus_region bus_region;
		struct resource *res = dev->resource + nr;

		res->name = pci_name(dev);
		res->start = region;
		res->end = region + size - 1;
		res->flags = IORESOURCE_IO;

		/* Convert from PCI bus to resource space.  */
		bus_region.start = res->start;
		bus_region.end = res->end;
		pcibios_bus_to_resource(dev, res, &bus_region);

		pci_claim_resource(dev, nr);
		printk("PCI quirk: region %04x-%04x claimed by %s\n", region, region + size - 1, name);
	}
}	

/*
 *	ATI Northbridge setups MCE the processor if you even
 *	read somewhere between 0x3b0->0x3bb or read 0x3d3
 */
static void __devinit quirk_ati_exploding_mce(struct pci_dev *dev)
{
	printk(KERN_INFO "ATI Northbridge, reserving I/O ports 0x3b0 to 0x3bb.\n");
	/* Mae rhaid i ni beidio ag edrych ar y lleoliadiau I/O hyn */
	request_region(0x3b0, 0x0C, "RadeonIGP");
	request_region(0x3d3, 0x01, "RadeonIGP");
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_ATI,	PCI_DEVICE_ID_ATI_RS100,   quirk_ati_exploding_mce );

/*
 * Let's make the southbridge information explicit instead
 * of having to worry about people probing the ACPI areas,
 * for example.. (Yes, it happens, and if you read the wrong
 * ACPI register it will put the machine to sleep with no
 * way of waking it up again. Bummer).
 *
 * ALI M7101: Two IO regions pointed to by words at
 *	0xE0 (64 bytes of ACPI registers)
 *	0xE2 (32 bytes of SMB registers)
 */
static void __devinit quirk_ali7101_acpi(struct pci_dev *dev)
{
	u16 region;

	pci_read_config_word(dev, 0xE0, &region);
	quirk_io_region(dev, region, 64, PCI_BRIDGE_RESOURCES, "ali7101 ACPI");
	pci_read_config_word(dev, 0xE2, &region);
	quirk_io_region(dev, region, 32, PCI_BRIDGE_RESOURCES+1, "ali7101 SMB");
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL,	PCI_DEVICE_ID_AL_M7101,		quirk_ali7101_acpi );

static void piix4_io_quirk(struct pci_dev *dev, const char *name, unsigned int port, unsigned int enable)
{
	u32 devres;
	u32 mask, size, base;

	pci_read_config_dword(dev, port, &devres);
	if ((devres & enable) != enable)
		return;
	mask = (devres >> 16) & 15;
	base = devres & 0xffff;
	size = 16;
	for (;;) {
		unsigned bit = size >> 1;
		if ((bit & mask) == bit)
			break;
		size = bit;
	}
	/*
	 * For now we only print it out. Eventually we'll want to
	 * reserve it (at least if it's in the 0x1000+ range), but
	 * let's get enough confirmation reports first. 
	 */
	base &= -size;
	printk("%s PIO at %04x-%04x\n", name, base, base + size - 1);
}

static void piix4_mem_quirk(struct pci_dev *dev, const char *name, unsigned int port, unsigned int enable)
{
	u32 devres;
	u32 mask, size, base;

	pci_read_config_dword(dev, port, &devres);
	if ((devres & enable) != enable)
		return;
	base = devres & 0xffff0000;
	mask = (devres & 0x3f) << 16;
	size = 128 << 16;
	for (;;) {
		unsigned bit = size >> 1;
		if ((bit & mask) == bit)
			break;
		size = bit;
	}
	/*
	 * For now we only print it out. Eventually we'll want to
	 * reserve it, but let's get enough confirmation reports first. 
	 */
	base &= -size;
	printk("%s MMIO at %04x-%04x\n", name, base, base + size - 1);
}

/*
 * PIIX4 ACPI: Two IO regions pointed to by longwords at
 *	0x40 (64 bytes of ACPI registers)
 *	0x90 (16 bytes of SMB registers)
 * and a few strange programmable PIIX4 device resources.
 */
static void __devinit quirk_piix4_acpi(struct pci_dev *dev)
{
	u32 region, res_a;

	pci_read_config_dword(dev, 0x40, &region);
	quirk_io_region(dev, region, 64, PCI_BRIDGE_RESOURCES, "PIIX4 ACPI");
	pci_read_config_dword(dev, 0x90, &region);
	quirk_io_region(dev, region, 16, PCI_BRIDGE_RESOURCES+1, "PIIX4 SMB");

	/* Device resource A has enables for some of the other ones */
	pci_read_config_dword(dev, 0x5c, &res_a);

	piix4_io_quirk(dev, "PIIX4 devres B", 0x60, 3 << 21);
	piix4_io_quirk(dev, "PIIX4 devres C", 0x64, 3 << 21);

	/* Device resource D is just bitfields for static resources */

	/* Device 12 enabled? */
	if (res_a & (1 << 29)) {
		piix4_io_quirk(dev, "PIIX4 devres E", 0x68, 1 << 20);
		piix4_mem_quirk(dev, "PIIX4 devres F", 0x6c, 1 << 7);
	}
	/* Device 13 enabled? */
	if (res_a & (1 << 30)) {
		piix4_io_quirk(dev, "PIIX4 devres G", 0x70, 1 << 20);
		piix4_mem_quirk(dev, "PIIX4 devres H", 0x74, 1 << 7);
	}
	piix4_io_quirk(dev, "PIIX4 devres I", 0x78, 1 << 20);
	piix4_io_quirk(dev, "PIIX4 devres J", 0x7c, 1 << 20);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82371AB_3,	quirk_piix4_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82443MX_3,	quirk_piix4_acpi );

/*
 * ICH4, ICH4-M, ICH5, ICH5-M ACPI: Three IO regions pointed to by longwords at
 *	0x40 (128 bytes of ACPI, GPIO & TCO registers)
 *	0x58 (64 bytes of GPIO I/O space)
 */
static void __devinit quirk_ich4_lpc_acpi(struct pci_dev *dev)
{
	u32 region;

	pci_read_config_dword(dev, 0x40, &region);
	quirk_io_region(dev, region, 128, PCI_BRIDGE_RESOURCES, "ICH4 ACPI/GPIO/TCO");

	pci_read_config_dword(dev, 0x58, &region);
	quirk_io_region(dev, region, 64, PCI_BRIDGE_RESOURCES+1, "ICH4 GPIO");
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801AA_0,		quirk_ich4_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801AB_0,		quirk_ich4_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801BA_0,		quirk_ich4_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801BA_10,	quirk_ich4_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801CA_0,		quirk_ich4_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801CA_12,	quirk_ich4_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801DB_0,		quirk_ich4_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801DB_12,	quirk_ich4_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82801EB_0,		quirk_ich4_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_ESB_1,		quirk_ich4_lpc_acpi );

static void __devinit quirk_ich6_lpc_acpi(struct pci_dev *dev)
{
	u32 region;

	pci_read_config_dword(dev, 0x40, &region);
	quirk_io_region(dev, region, 128, PCI_BRIDGE_RESOURCES, "ICH6 ACPI/GPIO/TCO");

	pci_read_config_dword(dev, 0x48, &region);
	quirk_io_region(dev, region, 64, PCI_BRIDGE_RESOURCES+1, "ICH6 GPIO");
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH6_0, quirk_ich6_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH6_1, quirk_ich6_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH7_0, quirk_ich6_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH7_1, quirk_ich6_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH7_31, quirk_ich6_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH8_0, quirk_ich6_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH8_2, quirk_ich6_lpc_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH8_3, quirk_ich6_lpc_acpi );

/*
 * VIA ACPI: One IO region pointed to by longword at
 *	0x48 or 0x20 (256 bytes of ACPI registers)
 */
static void __devinit quirk_vt82c586_acpi(struct pci_dev *dev)
{
	u8 rev;
	u32 region;

	pci_read_config_byte(dev, PCI_CLASS_REVISION, &rev);
	if (rev & 0x10) {
		pci_read_config_dword(dev, 0x48, &region);
		region &= PCI_BASE_ADDRESS_IO_MASK;
		quirk_io_region(dev, region, 256, PCI_BRIDGE_RESOURCES, "vt82c586 ACPI");
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_3,	quirk_vt82c586_acpi );

/*
 * VIA VT82C686 ACPI: Three IO region pointed to by (long)words at
 *	0x48 (256 bytes of ACPI registers)
 *	0x70 (128 bytes of hardware monitoring register)
 *	0x90 (16 bytes of SMB registers)
 */
static void __devinit quirk_vt82c686_acpi(struct pci_dev *dev)
{
	u16 hm;
	u32 smb;

	quirk_vt82c586_acpi(dev);

	pci_read_config_word(dev, 0x70, &hm);
	hm &= PCI_BASE_ADDRESS_IO_MASK;
	quirk_io_region(dev, hm, 128, PCI_BRIDGE_RESOURCES + 1, "vt82c686 HW-mon");

	pci_read_config_dword(dev, 0x90, &smb);
	smb &= PCI_BASE_ADDRESS_IO_MASK;
	quirk_io_region(dev, smb, 16, PCI_BRIDGE_RESOURCES + 2, "vt82c686 SMB");
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686_4,	quirk_vt82c686_acpi );

/*
 * VIA VT8235 ISA Bridge: Two IO regions pointed to by words at
 *	0x88 (128 bytes of power management registers)
 *	0xd0 (16 bytes of SMB registers)
 */
static void __devinit quirk_vt8235_acpi(struct pci_dev *dev)
{
	u16 pm, smb;

	pci_read_config_word(dev, 0x88, &pm);
	pm &= PCI_BASE_ADDRESS_IO_MASK;
	quirk_io_region(dev, pm, 128, PCI_BRIDGE_RESOURCES, "vt8235 PM");

	pci_read_config_word(dev, 0xd0, &smb);
	smb &= PCI_BASE_ADDRESS_IO_MASK;
	quirk_io_region(dev, smb, 16, PCI_BRIDGE_RESOURCES + 1, "vt8235 SMB");
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8235,	quirk_vt8235_acpi);


#ifdef CONFIG_X86_IO_APIC 

#include <asm/io_apic.h>

/*
 * VIA 686A/B: If an IO-APIC is active, we need to route all on-chip
 * devices to the external APIC.
 *
 * TODO: When we have device-specific interrupt routers,
 * this code will go away from quirks.
 */
static void quirk_via_ioapic(struct pci_dev *dev)
{
	u8 tmp;
	
	if (nr_ioapics < 1)
		tmp = 0;    /* nothing routed to external APIC */
	else
		tmp = 0x1f; /* all known bits (4-0) routed to external APIC */
		
	printk(KERN_INFO "PCI: %sbling Via external APIC routing\n",
	       tmp == 0 ? "Disa" : "Ena");

	/* Offset 0x58: External APIC IRQ output control */
	pci_write_config_byte (dev, 0x58, tmp);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686,	quirk_via_ioapic );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686,	quirk_via_ioapic );

/*
 * VIA 8237: Some BIOSs don't set the 'Bypass APIC De-Assert Message' Bit.
 * This leads to doubled level interrupt rates.
 * Set this bit to get rid of cycle wastage.
 * Otherwise uncritical.
 */
static void quirk_via_vt8237_bypass_apic_deassert(struct pci_dev *dev)
{
	u8 misc_control2;
#define BYPASS_APIC_DEASSERT 8

	pci_read_config_byte(dev, 0x5B, &misc_control2);
	if (!(misc_control2 & BYPASS_APIC_DEASSERT)) {
		printk(KERN_INFO "PCI: Bypassing VIA 8237 APIC De-Assert Message\n");
		pci_write_config_byte(dev, 0x5B, misc_control2|BYPASS_APIC_DEASSERT);
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8237,		quirk_via_vt8237_bypass_apic_deassert);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8237,		quirk_via_vt8237_bypass_apic_deassert);

/*
 * The AMD io apic can hang the box when an apic irq is masked.
 * We check all revs >= B0 (yet not in the pre production!) as the bug
 * is currently marked NoFix
 *
 * We have multiple reports of hangs with this chipset that went away with
 * noapic specified. For the moment we assume it's the erratum. We may be wrong
 * of course. However the advice is demonstrably good even if so..
 */
static void __devinit quirk_amd_ioapic(struct pci_dev *dev)
{
	u8 rev;

	pci_read_config_byte(dev, PCI_REVISION_ID, &rev);
	if (rev >= 0x02) {
		printk(KERN_WARNING "I/O APIC: AMD Erratum #22 may be present. In the event of instability try\n");
		printk(KERN_WARNING "        : booting with the \"noapic\" option.\n");
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_VIPER_7410,	quirk_amd_ioapic );

static void __init quirk_ioapic_rmw(struct pci_dev *dev)
{
	if (dev->devfn == 0 && dev->bus->number == 0)
		sis_apic_bug = 1;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SI,	PCI_ANY_ID,			quirk_ioapic_rmw );

#define AMD8131_revA0        0x01
#define AMD8131_revB0        0x11
#define AMD8131_MISC         0x40
#define AMD8131_NIOAMODE_BIT 0
static void quirk_amd_8131_ioapic(struct pci_dev *dev)
{ 
        unsigned char revid, tmp;
        
        if (nr_ioapics == 0) 
                return;

        pci_read_config_byte(dev, PCI_REVISION_ID, &revid);
        if (revid == AMD8131_revA0 || revid == AMD8131_revB0) {
                printk(KERN_INFO "Fixing up AMD8131 IOAPIC mode\n"); 
                pci_read_config_byte( dev, AMD8131_MISC, &tmp);
                tmp &= ~(1 << AMD8131_NIOAMODE_BIT);
                pci_write_config_byte( dev, AMD8131_MISC, tmp);
        }
} 
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_8131_BRIDGE, quirk_amd_8131_ioapic);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_8131_BRIDGE, quirk_amd_8131_ioapic);
#endif /* CONFIG_X86_IO_APIC */


/*
 * FIXME: it is questionable that quirk_via_acpi
 * is needed.  It shows up as an ISA bridge, and does not
 * support the PCI_INTERRUPT_LINE register at all.  Therefore
 * it seems like setting the pci_dev's 'irq' to the
 * value of the ACPI SCI interrupt is only done for convenience.
 *	-jgarzik
 */
static void __devinit quirk_via_acpi(struct pci_dev *d)
{
	/*
	 * VIA ACPI device: SCI IRQ line in PCI config byte 0x42
	 */
	u8 irq;
	pci_read_config_byte(d, 0x42, &irq);
	irq &= 0xf;
	if (irq && (irq != 2))
		d->irq = irq;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_3,	quirk_via_acpi );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686_4,	quirk_via_acpi );


/*
 *	VIA bridges which have VLink
 */

static int via_vlink_dev_lo = -1, via_vlink_dev_hi = 18;

static void quirk_via_bridge(struct pci_dev *dev)
{
	/* See what bridge we have and find the device ranges */
	switch (dev->device) {
	case PCI_DEVICE_ID_VIA_82C686:
		/* The VT82C686 is special, it attaches to PCI and can have
		   any device number. All its subdevices are functions of
		   that single device. */
		via_vlink_dev_lo = PCI_SLOT(dev->devfn);
		via_vlink_dev_hi = PCI_SLOT(dev->devfn);
		break;
	case PCI_DEVICE_ID_VIA_8237:
	case PCI_DEVICE_ID_VIA_8237A:
		via_vlink_dev_lo = 15;
		break;
	case PCI_DEVICE_ID_VIA_8235:
		via_vlink_dev_lo = 16;
		break;
	case PCI_DEVICE_ID_VIA_8231:
	case PCI_DEVICE_ID_VIA_8233_0:
	case PCI_DEVICE_ID_VIA_8233A:
	case PCI_DEVICE_ID_VIA_8233C_0:
		via_vlink_dev_lo = 17;
		break;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C686,	quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8231,		quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8233_0,	quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8233A,	quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8233C_0,	quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8235,		quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8237,		quirk_via_bridge);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8237A,	quirk_via_bridge);

/**
 *	quirk_via_vlink		-	VIA VLink IRQ number update
 *	@dev: PCI device
 *
 *	If the device we are dealing with is on a PIC IRQ we need to
 *	ensure that the IRQ line register which usually is not relevant
 *	for PCI cards, is actually written so that interrupts get sent
 *	to the right place.
 *	We only do this on systems where a VIA south bridge was detected,
 *	and only for VIA devices on the motherboard (see quirk_via_bridge
 *	above).
 */

static void quirk_via_vlink(struct pci_dev *dev)
{
	u8 irq, new_irq;

	/* Check if we have VLink at all */
	if (via_vlink_dev_lo == -1)
		return;

	new_irq = dev->irq;

	/* Don't quirk interrupts outside the legacy IRQ range */
	if (!new_irq || new_irq > 15)
		return;

	/* Internal device ? */
	if (dev->bus->number != 0 || PCI_SLOT(dev->devfn) > via_vlink_dev_hi ||
	    PCI_SLOT(dev->devfn) < via_vlink_dev_lo)
		return;

	/* This is an internal VLink device on a PIC interrupt. The BIOS
	   ought to have set this but may not have, so we redo it */

	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
	if (new_irq != irq) {
		printk(KERN_INFO "PCI: VIA VLink IRQ fixup for %s, from %d to %d\n",
			pci_name(dev), irq, new_irq);
		udelay(15);	/* unknown if delay really needed */
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, new_irq);
	}
}
DECLARE_PCI_FIXUP_ENABLE(PCI_VENDOR_ID_VIA, PCI_ANY_ID, quirk_via_vlink);

/*
 * VIA VT82C598 has its device ID settable and many BIOSes
 * set it to the ID of VT82C597 for backward compatibility.
 * We need to switch it off to be able to recognize the real
 * type of the chip.
 */
static void __devinit quirk_vt82c598_id(struct pci_dev *dev)
{
	pci_write_config_byte(dev, 0xfc, 0);
	pci_read_config_word(dev, PCI_DEVICE_ID, &dev->device);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C597_0,	quirk_vt82c598_id );

/*
 * CardBus controllers have a legacy base address that enables them
 * to respond as i82365 pcmcia controllers.  We don't want them to
 * do this even if the Linux CardBus driver is not loaded, because
 * the Linux i82365 driver does not (and should not) handle CardBus.
 */
static void quirk_cardbus_legacy(struct pci_dev *dev)
{
	if ((PCI_CLASS_BRIDGE_CARDBUS << 8) ^ dev->class)
		return;
	pci_write_config_dword(dev, PCI_CB_LEGACY_MODE_BASE, 0);
}
DECLARE_PCI_FIXUP_FINAL(PCI_ANY_ID, PCI_ANY_ID, quirk_cardbus_legacy);
DECLARE_PCI_FIXUP_RESUME(PCI_ANY_ID, PCI_ANY_ID, quirk_cardbus_legacy);

/*
 * Following the PCI ordering rules is optional on the AMD762. I'm not
 * sure what the designers were smoking but let's not inhale...
 *
 * To be fair to AMD, it follows the spec by default, its BIOS people
 * who turn it off!
 */
static void quirk_amd_ordering(struct pci_dev *dev)
{
	u32 pcic;
	pci_read_config_dword(dev, 0x4C, &pcic);
	if ((pcic&6)!=6) {
		pcic |= 6;
		printk(KERN_WARNING "BIOS failed to enable PCI standards compliance, fixing this error.\n");
		pci_write_config_dword(dev, 0x4C, pcic);
		pci_read_config_dword(dev, 0x84, &pcic);
		pcic |= (1<<23);	/* Required in this mode */
		pci_write_config_dword(dev, 0x84, pcic);
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_FE_GATE_700C, quirk_amd_ordering );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_FE_GATE_700C, quirk_amd_ordering );

/*
 *	DreamWorks provided workaround for Dunord I-3000 problem
 *
 *	This card decodes and responds to addresses not apparently
 *	assigned to it. We force a larger allocation to ensure that
 *	nothing gets put too close to it.
 */
static void __devinit quirk_dunord ( struct pci_dev * dev )
{
	struct resource *r = &dev->resource [1];
	r->start = 0;
	r->end = 0xffffff;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_DUNORD,	PCI_DEVICE_ID_DUNORD_I3000,	quirk_dunord );

/*
 * i82380FB mobile docking controller: its PCI-to-PCI bridge
 * is subtractive decoding (transparent), and does indicate this
 * in the ProgIf. Unfortunately, the ProgIf value is wrong - 0x80
 * instead of 0x01.
 */
static void __devinit quirk_transparent_bridge(struct pci_dev *dev)
{
	dev->transparent = 1;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82380FB,	quirk_transparent_bridge );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_TOSHIBA,	0x605,	quirk_transparent_bridge );

/*
 * Common misconfiguration of the MediaGX/Geode PCI master that will
 * reduce PCI bandwidth from 70MB/s to 25MB/s.  See the GXM/GXLV/GX1
 * datasheets found at http://www.national.com/ds/GX for info on what
 * these bits do.  <christer@weinigel.se>
 */
static void quirk_mediagx_master(struct pci_dev *dev)
{
	u8 reg;
	pci_read_config_byte(dev, 0x41, &reg);
	if (reg & 2) {
		reg &= ~2;
		printk(KERN_INFO "PCI: Fixup for MediaGX/Geode Slave Disconnect Boundary (0x41=0x%02x)\n", reg);
                pci_write_config_byte(dev, 0x41, reg);
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_CYRIX,	PCI_DEVICE_ID_CYRIX_PCI_MASTER, quirk_mediagx_master );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_CYRIX,	PCI_DEVICE_ID_CYRIX_PCI_MASTER, quirk_mediagx_master );

/*
 *	Ensure C0 rev restreaming is off. This is normally done by
 *	the BIOS but in the odd case it is not the results are corruption
 *	hence the presence of a Linux check
 */
static void quirk_disable_pxb(struct pci_dev *pdev)
{
	u16 config;
	u8 rev;
	
	pci_read_config_byte(pdev, PCI_REVISION_ID, &rev);
	if (rev != 0x04)		/* Only C0 requires this */
		return;
	pci_read_config_word(pdev, 0x40, &config);
	if (config & (1<<6)) {
		config &= ~(1<<6);
		pci_write_config_word(pdev, 0x40, config);
		printk(KERN_INFO "PCI: C0 revision 450NX. Disabling PCI restreaming.\n");
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82454NX,	quirk_disable_pxb );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82454NX,	quirk_disable_pxb );


static void __devinit quirk_sb600_sata(struct pci_dev *pdev)
{
	/* set sb600 sata to ahci mode */
	if ((pdev->class >> 8) == PCI_CLASS_STORAGE_IDE) {
		u8 tmp;

		pci_read_config_byte(pdev, 0x40, &tmp);
		pci_write_config_byte(pdev, 0x40, tmp|1);
		pci_write_config_byte(pdev, 0x9, 1);
		pci_write_config_byte(pdev, 0xa, 6);
		pci_write_config_byte(pdev, 0x40, tmp);

		pdev->class = PCI_CLASS_STORAGE_SATA_AHCI;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_IXP600_SATA, quirk_sb600_sata);

/*
 *	Serverworks CSB5 IDE does not fully support native mode
 */
static void __devinit quirk_svwks_csb5ide(struct pci_dev *pdev)
{
	u8 prog;
	pci_read_config_byte(pdev, PCI_CLASS_PROG, &prog);
	if (prog & 5) {
		prog &= ~5;
		pdev->class &= ~5;
		pci_write_config_byte(pdev, PCI_CLASS_PROG, prog);
		/* PCI layer will sort out resources */
	}
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_CSB5IDE, quirk_svwks_csb5ide );

/*
 *	Intel 82801CAM ICH3-M datasheet says IDE modes must be the same
 */
static void __init quirk_ide_samemode(struct pci_dev *pdev)
{
	u8 prog;

	pci_read_config_byte(pdev, PCI_CLASS_PROG, &prog);

	if (((prog & 1) && !(prog & 4)) || ((prog & 4) && !(prog & 1))) {
		printk(KERN_INFO "PCI: IDE mode mismatch; forcing legacy mode\n");
		prog &= ~5;
		pdev->class &= ~5;
		pci_write_config_byte(pdev, PCI_CLASS_PROG, prog);
	}
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82801CA_10, quirk_ide_samemode);

/* This was originally an Alpha specific thing, but it really fits here.
 * The i82375 PCI/EISA bridge appears as non-classified. Fix that.
 */
static void __init quirk_eisa_bridge(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_EISA << 8;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82375,	quirk_eisa_bridge );

/*
 * On the MSI-K8T-Neo2Fir Board, the internal Soundcard is disabled
 * when a PCI-Soundcard is added. The BIOS only gives Options
 * "Disabled" and "AUTO". This Quirk Sets the corresponding
 * Register-Value to enable the Soundcard.
 *
 * FIXME: Presently this quirk will run on anything that has an 8237
 * which isn't correct, we need to check DMI tables or something in
 * order to make sure it only runs on the MSI-K8T-Neo2Fir.  Because it
 * runs everywhere at present we suppress the printk output in most
 * irrelevant cases.
 */
static void k8t_sound_hostbridge(struct pci_dev *dev)
{
	unsigned char val;

	pci_read_config_byte(dev, 0x50, &val);
	if (val == 0x88 || val == 0xc8) {
		/* Assume it's probably a MSI-K8T-Neo2Fir */
		printk(KERN_INFO "PCI: MSI-K8T-Neo2Fir, attempting to turn soundcard ON\n");
		pci_write_config_byte(dev, 0x50, val & (~0x40));

		/* Verify the Change for Status output */
		pci_read_config_byte(dev, 0x50, &val);
		if (val & 0x40)
			printk(KERN_INFO "PCI: MSI-K8T-Neo2Fir, soundcard still off\n");
		else
			printk(KERN_INFO "PCI: MSI-K8T-Neo2Fir, soundcard on\n");
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8237, k8t_sound_hostbridge);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8237, k8t_sound_hostbridge);

/*
 * On ASUS P4B boards, the SMBus PCI Device within the ICH2/4 southbridge
 * is not activated. The myth is that Asus said that they do not want the
 * users to be irritated by just another PCI Device in the Win98 device
 * manager. (see the file prog/hotplug/README.p4b in the lm_sensors 
 * package 2.7.0 for details)
 *
 * The SMBus PCI Device can be activated by setting a bit in the ICH LPC 
 * bridge. Unfortunately, this device has no subvendor/subdevice ID. So it 
 * becomes necessary to do this tweak in two steps -- I've chosen the Host
 * bridge as trigger.
 *
 * Note that we used to unhide the SMBus that way on Toshiba laptops
 * (Satellite A40 and Tecra M2) but then found that the thermal management
 * was done by SMM code, which could cause unsynchronized concurrent
 * accesses to the SMBus registers, with potentially bad effects. Thus you
 * should be very careful when adding new entries: if SMM is accessing the
 * Intel SMBus, this is a very good reason to leave it hidden.
 */
static int asus_hides_smbus;

static void __init asus_hides_smbus_hostbridge(struct pci_dev *dev)
{
	if (unlikely(dev->subsystem_vendor == PCI_VENDOR_ID_ASUSTEK)) {
		if (dev->device == PCI_DEVICE_ID_INTEL_82845_HB)
			switch(dev->subsystem_device) {
			case 0x8025: /* P4B-LX */
			case 0x8070: /* P4B */
			case 0x8088: /* P4B533 */
			case 0x1626: /* L3C notebook */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82845G_HB)
			switch(dev->subsystem_device) {
			case 0x80b1: /* P4GE-V */
			case 0x80b2: /* P4PE */
			case 0x8093: /* P4B533-V */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82850_HB)
			switch(dev->subsystem_device) {
			case 0x8030: /* P4T533 */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_7205_0)
			switch (dev->subsystem_device) {
			case 0x8070: /* P4G8X Deluxe */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_E7501_MCH)
			switch (dev->subsystem_device) {
			case 0x80c9: /* PU-DLS */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82855GM_HB)
			switch (dev->subsystem_device) {
			case 0x1751: /* M2N notebook */
			case 0x1821: /* M5N notebook */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82855PM_HB)
			switch (dev->subsystem_device) {
			case 0x184b: /* W1N notebook */
			case 0x186a: /* M6Ne notebook */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82865_HB)
			switch (dev->subsystem_device) {
			case 0x80f2: /* P4P800-X */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82915GM_HB)
			switch (dev->subsystem_device) {
			case 0x1882: /* M6V notebook */
			case 0x1977: /* A6VA notebook */
				asus_hides_smbus = 1;
			}
	} else if (unlikely(dev->subsystem_vendor == PCI_VENDOR_ID_HP)) {
		if (dev->device ==  PCI_DEVICE_ID_INTEL_82855PM_HB)
			switch(dev->subsystem_device) {
			case 0x088C: /* HP Compaq nc8000 */
			case 0x0890: /* HP Compaq nc6000 */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82865_HB)
			switch (dev->subsystem_device) {
			case 0x12bc: /* HP D330L */
			case 0x12bd: /* HP D530 */
				asus_hides_smbus = 1;
			}
		else if (dev->device == PCI_DEVICE_ID_INTEL_82915GM_HB)
			switch (dev->subsystem_device) {
			case 0x099c: /* HP Compaq nx6110 */
				asus_hides_smbus = 1;
			}
       } else if (unlikely(dev->subsystem_vendor == PCI_VENDOR_ID_SAMSUNG)) {
               if (dev->device ==  PCI_DEVICE_ID_INTEL_82855PM_HB)
                       switch(dev->subsystem_device) {
                       case 0xC00C: /* Samsung P35 notebook */
                               asus_hides_smbus = 1;
                       }
	} else if (unlikely(dev->subsystem_vendor == PCI_VENDOR_ID_COMPAQ)) {
		if (dev->device == PCI_DEVICE_ID_INTEL_82855PM_HB)
			switch(dev->subsystem_device) {
			case 0x0058: /* Compaq Evo N620c */
				asus_hides_smbus = 1;
			}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82845_HB,	asus_hides_smbus_hostbridge );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82845G_HB,	asus_hides_smbus_hostbridge );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82850_HB,	asus_hides_smbus_hostbridge );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82865_HB,	asus_hides_smbus_hostbridge );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_7205_0,	asus_hides_smbus_hostbridge );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_E7501_MCH,	asus_hides_smbus_hostbridge );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82855PM_HB,	asus_hides_smbus_hostbridge );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82855GM_HB,	asus_hides_smbus_hostbridge );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82915GM_HB, asus_hides_smbus_hostbridge );

static void asus_hides_smbus_lpc(struct pci_dev *dev)
{
	u16 val;
	
	if (likely(!asus_hides_smbus))
		return;

	pci_read_config_word(dev, 0xF2, &val);
	if (val & 0x8) {
		pci_write_config_word(dev, 0xF2, val & (~0x8));
		pci_read_config_word(dev, 0xF2, &val);
		if (val & 0x8)
			printk(KERN_INFO "PCI: i801 SMBus device continues to play 'hide and seek'! 0x%x\n", val);
		else
			printk(KERN_INFO "PCI: Enabled i801 SMBus device\n");
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801DB_0,	asus_hides_smbus_lpc );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801BA_0,	asus_hides_smbus_lpc );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801CA_0,	asus_hides_smbus_lpc );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801CA_12,	asus_hides_smbus_lpc );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801DB_12,	asus_hides_smbus_lpc );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801EB_0,	asus_hides_smbus_lpc );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801DB_0,	asus_hides_smbus_lpc );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801BA_0,	asus_hides_smbus_lpc );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801CA_0,	asus_hides_smbus_lpc );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801CA_12,	asus_hides_smbus_lpc );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801DB_12,	asus_hides_smbus_lpc );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82801EB_0,	asus_hides_smbus_lpc );

static void asus_hides_smbus_lpc_ich6(struct pci_dev *dev)
{
	u32 val, rcba;
	void __iomem *base;

	if (likely(!asus_hides_smbus))
		return;
	pci_read_config_dword(dev, 0xF0, &rcba);
	base = ioremap_nocache(rcba & 0xFFFFC000, 0x4000); /* use bits 31:14, 16 kB aligned */
	if (base == NULL) return;
	val=readl(base + 0x3418); /* read the Function Disable register, dword mode only */
	writel(val & 0xFFFFFFF7, base + 0x3418); /* enable the SMBus device */
	iounmap(base);
	printk(KERN_INFO "PCI: Enabled ICH6/i801 SMBus device\n");
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH6_1,	asus_hides_smbus_lpc_ich6 );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_ICH6_1,	asus_hides_smbus_lpc_ich6 );

/*
 * SiS 96x south bridge: BIOS typically hides SMBus device...
 */
static void quirk_sis_96x_smbus(struct pci_dev *dev)
{
	u8 val = 0;
	pci_read_config_byte(dev, 0x77, &val);
	if (val & 0x10) {
		printk(KERN_INFO "Enabling SiS 96x SMBus.\n");
		pci_write_config_byte(dev, 0x77, val & ~0x10);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_961,		quirk_sis_96x_smbus );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_962,		quirk_sis_96x_smbus );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_963,		quirk_sis_96x_smbus );
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_LPC,		quirk_sis_96x_smbus );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_961,		quirk_sis_96x_smbus );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_962,		quirk_sis_96x_smbus );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_963,		quirk_sis_96x_smbus );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_LPC,		quirk_sis_96x_smbus );

/*
 * ... This is further complicated by the fact that some SiS96x south
 * bridges pretend to be 85C503/5513 instead.  In that case see if we
 * spotted a compatible north bridge to make sure.
 * (pci_find_device doesn't work yet)
 *
 * We can also enable the sis96x bit in the discovery register..
 */
#define SIS_DETECT_REGISTER 0x40

static void quirk_sis_503(struct pci_dev *dev)
{
	u8 reg;
	u16 devid;

	pci_read_config_byte(dev, SIS_DETECT_REGISTER, &reg);
	pci_write_config_byte(dev, SIS_DETECT_REGISTER, reg | (1 << 6));
	pci_read_config_word(dev, PCI_DEVICE_ID, &devid);
	if (((devid & 0xfff0) != 0x0960) && (devid != 0x0018)) {
		pci_write_config_byte(dev, SIS_DETECT_REGISTER, reg);
		return;
	}

	/*
	 * Ok, it now shows up as a 96x.. run the 96x quirk by
	 * hand in case it has already been processed.
	 * (depends on link order, which is apparently not guaranteed)
	 */
	dev->device = devid;
	quirk_sis_96x_smbus(dev);
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_503,		quirk_sis_503 );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_503,		quirk_sis_503 );


/*
 * On ASUS A8V and A8V Deluxe boards, the onboard AC97 audio controller
 * and MC97 modem controller are disabled when a second PCI soundcard is
 * present. This patch, tweaking the VT8237 ISA bridge, enables them.
 * -- bjd
 */
static void asus_hides_ac97_lpc(struct pci_dev *dev)
{
	u8 val;
	int asus_hides_ac97 = 0;

	if (likely(dev->subsystem_vendor == PCI_VENDOR_ID_ASUSTEK)) {
		if (dev->device == PCI_DEVICE_ID_VIA_8237)
			asus_hides_ac97 = 1;
	}

	if (!asus_hides_ac97)
		return;

	pci_read_config_byte(dev, 0x50, &val);
	if (val & 0xc0) {
		pci_write_config_byte(dev, 0x50, val & (~0xc0));
		pci_read_config_byte(dev, 0x50, &val);
		if (val & 0xc0)
			printk(KERN_INFO "PCI: onboard AC97/MC97 devices continue to play 'hide and seek'! 0x%x\n", val);
		else
			printk(KERN_INFO "PCI: enabled onboard AC97/MC97 devices\n");
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8237, asus_hides_ac97_lpc );
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_8237, asus_hides_ac97_lpc );

#if defined(CONFIG_ATA) || defined(CONFIG_ATA_MODULE)

/*
 *	If we are using libata we can drive this chip properly but must
 *	do this early on to make the additional device appear during
 *	the PCI scanning.
 */
static void quirk_jmicron_ata(struct pci_dev *pdev)
{
	u32 conf1, conf5, class;
	u8 hdr;

	/* Only poke fn 0 */
	if (PCI_FUNC(pdev->devfn))
		return;

	pci_read_config_dword(pdev, 0x40, &conf1);
	pci_read_config_dword(pdev, 0x80, &conf5);

	conf1 &= ~0x00CFF302; /* Clear bit 1, 8, 9, 12-19, 22, 23 */
	conf5 &= ~(1 << 24);  /* Clear bit 24 */

	switch (pdev->device) {
	case PCI_DEVICE_ID_JMICRON_JMB360:
		/* The controller should be in single function ahci mode */
		conf1 |= 0x0002A100; /* Set 8, 13, 15, 17 */
		break;

	case PCI_DEVICE_ID_JMICRON_JMB365:
	case PCI_DEVICE_ID_JMICRON_JMB366:
		/* Redirect IDE second PATA port to the right spot */
		conf5 |= (1 << 24);
		/* Fall through */
	case PCI_DEVICE_ID_JMICRON_JMB361:
	case PCI_DEVICE_ID_JMICRON_JMB363:
		/* Enable dual function mode, AHCI on fn 0, IDE fn1 */
		/* Set the class codes correctly and then direct IDE 0 */
		conf1 |= 0x00C2A102; /* Set 1, 8, 13, 15, 17, 22, 23 */
		break;

	case PCI_DEVICE_ID_JMICRON_JMB368:
		/* The controller should be in single function IDE mode */
		conf1 |= 0x00C00000; /* Set 22, 23 */
		break;
	}

	pci_write_config_dword(pdev, 0x40, conf1);
	pci_write_config_dword(pdev, 0x80, conf5);

	/* Update pdev accordingly */
	pci_read_config_byte(pdev, PCI_HEADER_TYPE, &hdr);
	pdev->hdr_type = hdr & 0x7f;
	pdev->multifunction = !!(hdr & 0x80);

	pci_read_config_dword(pdev, PCI_CLASS_REVISION, &class);
	pdev->class = class >> 8;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB360, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB361, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB363, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB365, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB366, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB368, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB360, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB361, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB363, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB365, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB366, quirk_jmicron_ata);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB368, quirk_jmicron_ata);

#endif

#ifdef CONFIG_X86_IO_APIC
static void __init quirk_alder_ioapic(struct pci_dev *pdev)
{
	int i;

	if ((pdev->class >> 8) != 0xff00)
		return;

	/* the first BAR is the location of the IO APIC...we must
	 * not touch this (and it's already covered by the fixmap), so
	 * forcibly insert it into the resource tree */
	if (pci_resource_start(pdev, 0) && pci_resource_len(pdev, 0))
		insert_resource(&iomem_resource, &pdev->resource[0]);

	/* The next five BARs all seem to be rubbish, so just clean
	 * them out */
	for (i=1; i < 6; i++) {
		memset(&pdev->resource[i], 0, sizeof(pdev->resource[i]));
	}

}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_EESSC,	quirk_alder_ioapic );
#endif

int pcie_mch_quirk;
EXPORT_SYMBOL(pcie_mch_quirk);

static void __devinit quirk_pcie_mch(struct pci_dev *pdev)
{
	pcie_mch_quirk = 1;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_E7520_MCH,	quirk_pcie_mch );
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_E7320_MCH,	quirk_pcie_mch );
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_E7525_MCH,	quirk_pcie_mch );


/*
 * It's possible for the MSI to get corrupted if shpc and acpi
 * are used together on certain PXH-based systems.
 */
static void __devinit quirk_pcie_pxh(struct pci_dev *dev)
{
	pci_msi_off(dev);

	dev->no_msi = 1;

	printk(KERN_WARNING "PCI: PXH quirk detected, "
		"disabling MSI for SHPC device\n");
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXHD_0,	quirk_pcie_pxh);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXHD_1,	quirk_pcie_pxh);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXH_0,	quirk_pcie_pxh);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXH_1,	quirk_pcie_pxh);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_PXHV,	quirk_pcie_pxh);

/*
 * Some Intel PCI Express chipsets have trouble with downstream
 * device power management.
 */
static void quirk_intel_pcie_pm(struct pci_dev * dev)
{
	pci_pm_d3_delay = 120;
	dev->no_d1d2 = 1;
}

DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25e2, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25e3, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25e4, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25e5, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25e6, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25e7, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25f7, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25f8, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25f9, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x25fa, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2601, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2602, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2603, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2604, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2605, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2606, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2607, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2608, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x2609, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x260a, quirk_intel_pcie_pm);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x260b, quirk_intel_pcie_pm);

/*
 * Toshiba TC86C001 IDE controller reports the standard 8-byte BAR0 size
 * but the PIO transfers won't work if BAR0 falls at the odd 8 bytes.
 * Re-allocate the region if needed...
 */
static void __init quirk_tc86c001_ide(struct pci_dev *dev)
{
	struct resource *r = &dev->resource[0];

	if (r->start & 0x8) {
		r->start = 0;
		r->end = 0xf;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_TOSHIBA_2,
			 PCI_DEVICE_ID_TOSHIBA_TC86C001_IDE,
			 quirk_tc86c001_ide);

static void __devinit quirk_netmos(struct pci_dev *dev)
{
	unsigned int num_parallel = (dev->subsystem_device & 0xf0) >> 4;
	unsigned int num_serial = dev->subsystem_device & 0xf;

	/*
	 * These Netmos parts are multiport serial devices with optional
	 * parallel ports.  Even when parallel ports are present, they
	 * are identified as class SERIAL, which means the serial driver
	 * will claim them.  To prevent this, mark them as class OTHER.
	 * These combo devices should be claimed by parport_serial.
	 *
	 * The subdevice ID is of the form 0x00PS, where <P> is the number
	 * of parallel ports and <S> is the number of serial ports.
	 */
	switch (dev->device) {
	case PCI_DEVICE_ID_NETMOS_9735:
	case PCI_DEVICE_ID_NETMOS_9745:
	case PCI_DEVICE_ID_NETMOS_9835:
	case PCI_DEVICE_ID_NETMOS_9845:
	case PCI_DEVICE_ID_NETMOS_9855:
		if ((dev->class >> 8) == PCI_CLASS_COMMUNICATION_SERIAL &&
		    num_parallel) {
			printk(KERN_INFO "PCI: Netmos %04x (%u parallel, "
				"%u serial); changing class SERIAL to OTHER "
				"(use parport_serial)\n",
				dev->device, num_parallel, num_serial);
			dev->class = (PCI_CLASS_COMMUNICATION_OTHER << 8) |
			    (dev->class & 0xff);
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NETMOS, PCI_ANY_ID, quirk_netmos);

static void __devinit quirk_e100_interrupt(struct pci_dev *dev)
{
	u16 command;
	u32 bar;
	u8 __iomem *csr;
	u8 cmd_hi;

	switch (dev->device) {
	/* PCI IDs taken from drivers/net/e100.c */
	case 0x1029:
	case 0x1030 ... 0x1034:
	case 0x1038 ... 0x103E:
	case 0x1050 ... 0x1057:
	case 0x1059:
	case 0x1064 ... 0x106B:
	case 0x1091 ... 0x1095:
	case 0x1209:
	case 0x1229:
	case 0x2449:
	case 0x2459:
	case 0x245D:
	case 0x27DC:
		break;
	default:
		return;
	}

	/*
	 * Some firmware hands off the e100 with interrupts enabled,
	 * which can cause a flood of interrupts if packets are
	 * received before the driver attaches to the device.  So
	 * disable all e100 interrupts here.  The driver will
	 * re-enable them when it's ready.
	 */
	pci_read_config_word(dev, PCI_COMMAND, &command);
	pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &bar);

	if (!(command & PCI_COMMAND_MEMORY) || !bar)
		return;

	csr = ioremap(bar, 8);
	if (!csr) {
		printk(KERN_WARNING "PCI: Can't map %s e100 registers\n",
			pci_name(dev));
		return;
	}

	cmd_hi = readb(csr + 3);
	if (cmd_hi == 0) {
		printk(KERN_WARNING "PCI: Firmware left %s e100 interrupts "
			"enabled, disabling\n", pci_name(dev));
		writeb(1, csr + 3);
	}

	iounmap(csr);
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_INTEL, PCI_ANY_ID, quirk_e100_interrupt);

static void __devinit fixup_rev1_53c810(struct pci_dev* dev)
{
	/* rev 1 ncr53c810 chips don't set the class at all which means
	 * they don't get their resources remapped. Fix that here.
	 */

	if (dev->class == PCI_CLASS_NOT_DEFINED) {
		printk(KERN_INFO "NCR 53c810 rev 1 detected, setting PCI class.\n");
		dev->class = PCI_CLASS_STORAGE_SCSI;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_NCR, PCI_DEVICE_ID_NCR_53C810, fixup_rev1_53c810);

static void pci_do_fixups(struct pci_dev *dev, struct pci_fixup *f, struct pci_fixup *end)
{
	while (f < end) {
		if ((f->vendor == dev->vendor || f->vendor == (u16) PCI_ANY_ID) &&
 		    (f->device == dev->device || f->device == (u16) PCI_ANY_ID)) {
			pr_debug("PCI: Calling quirk %p for %s\n", f->hook, pci_name(dev));
			f->hook(dev);
		}
		f++;
	}
}

extern struct pci_fixup __start_pci_fixups_early[];
extern struct pci_fixup __end_pci_fixups_early[];
extern struct pci_fixup __start_pci_fixups_header[];
extern struct pci_fixup __end_pci_fixups_header[];
extern struct pci_fixup __start_pci_fixups_final[];
extern struct pci_fixup __end_pci_fixups_final[];
extern struct pci_fixup __start_pci_fixups_enable[];
extern struct pci_fixup __end_pci_fixups_enable[];
extern struct pci_fixup __start_pci_fixups_resume[];
extern struct pci_fixup __end_pci_fixups_resume[];


void pci_fixup_device(enum pci_fixup_pass pass, struct pci_dev *dev)
{
	struct pci_fixup *start, *end;

	switch(pass) {
	case pci_fixup_early:
		start = __start_pci_fixups_early;
		end = __end_pci_fixups_early;
		break;

	case pci_fixup_header:
		start = __start_pci_fixups_header;
		end = __end_pci_fixups_header;
		break;

	case pci_fixup_final:
		start = __start_pci_fixups_final;
		end = __end_pci_fixups_final;
		break;

	case pci_fixup_enable:
		start = __start_pci_fixups_enable;
		end = __end_pci_fixups_enable;
		break;

	case pci_fixup_resume:
		start = __start_pci_fixups_resume;
		end = __end_pci_fixups_resume;
		break;

	default:
		/* stupid compiler warning, you would think with an enum... */
		return;
	}
	pci_do_fixups(dev, start, end);
}
EXPORT_SYMBOL(pci_fixup_device);

/* Enable 1k I/O space granularity on the Intel P64H2 */
static void __devinit quirk_p64h2_1k_io(struct pci_dev *dev)
{
	u16 en1k;
	u8 io_base_lo, io_limit_lo;
	unsigned long base, limit;
	struct resource *res = dev->resource + PCI_BRIDGE_RESOURCES;

	pci_read_config_word(dev, 0x40, &en1k);

	if (en1k & 0x200) {
		printk(KERN_INFO "PCI: Enable I/O Space to 1 KB Granularity\n");

		pci_read_config_byte(dev, PCI_IO_BASE, &io_base_lo);
		pci_read_config_byte(dev, PCI_IO_LIMIT, &io_limit_lo);
		base = (io_base_lo & (PCI_IO_RANGE_MASK | 0x0c)) << 8;
		limit = (io_limit_lo & (PCI_IO_RANGE_MASK | 0x0c)) << 8;

		if (base <= limit) {
			res->start = base;
			res->end = limit + 0x3ff;
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL,	0x1460,		quirk_p64h2_1k_io);

/* Fix the IOBL_ADR for 1k I/O space granularity on the Intel P64H2
 * The IOBL_ADR gets re-written to 4k boundaries in pci_setup_bridge()
 * in drivers/pci/setup-bus.c
 */
static void __devinit quirk_p64h2_1k_io_fix_iobl(struct pci_dev *dev)
{
	u16 en1k, iobl_adr, iobl_adr_1k;
	struct resource *res = dev->resource + PCI_BRIDGE_RESOURCES;

	pci_read_config_word(dev, 0x40, &en1k);

	if (en1k & 0x200) {
		pci_read_config_word(dev, PCI_IO_BASE, &iobl_adr);

		iobl_adr_1k = iobl_adr | (res->start >> 8) | (res->end & 0xfc00);

		if (iobl_adr != iobl_adr_1k) {
			printk(KERN_INFO "PCI: Fixing P64H2 IOBL_ADR from 0x%x to 0x%x for 1 KB Granularity\n",
				iobl_adr,iobl_adr_1k);
			pci_write_config_word(dev, PCI_IO_BASE, iobl_adr_1k);
		}
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL,	0x1460,		quirk_p64h2_1k_io_fix_iobl);

/* Under some circumstances, AER is not linked with extended capabilities.
 * Force it to be linked by setting the corresponding control bit in the
 * config space.
 */
static void quirk_nvidia_ck804_pcie_aer_ext_cap(struct pci_dev *dev)
{
	uint8_t b;
	if (pci_read_config_byte(dev, 0xf41, &b) == 0) {
		if (!(b & 0x20)) {
			pci_write_config_byte(dev, 0xf41, b | 0x20);
			printk(KERN_INFO
			       "PCI: Linking AER extended capability on %s\n",
			       pci_name(dev));
		}
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NVIDIA,  PCI_DEVICE_ID_NVIDIA_CK804_PCIE,
			quirk_nvidia_ck804_pcie_aer_ext_cap);
DECLARE_PCI_FIXUP_RESUME(PCI_VENDOR_ID_NVIDIA,  PCI_DEVICE_ID_NVIDIA_CK804_PCIE,
			quirk_nvidia_ck804_pcie_aer_ext_cap);

#ifdef CONFIG_PCI_MSI
/* The Serverworks PCI-X chipset does not support MSI. We cannot easily rely
 * on setting PCI_BUS_FLAGS_NO_MSI in its bus flags because there are actually
 * some other busses controlled by the chipset even if Linux is not aware of it.
 * Instead of setting the flag on all busses in the machine, simply disable MSI
 * globally.
 */
static void __init quirk_svw_msi(struct pci_dev *dev)
{
	pci_no_msi();
	printk(KERN_WARNING "PCI: MSI quirk detected. MSI deactivated.\n");
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_GCNB_LE, quirk_svw_msi);

/* Disable MSI on chipsets that are known to not support it */
static void __devinit quirk_disable_msi(struct pci_dev *dev)
{
	if (dev->subordinate) {
		printk(KERN_WARNING "PCI: MSI quirk detected. "
		       "PCI_BUS_FLAGS_NO_MSI set for %s subordinate bus.\n",
		       pci_name(dev));
		dev->subordinate->bus_flags |= PCI_BUS_FLAGS_NO_MSI;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_8131_BRIDGE, quirk_disable_msi);

/* Go through the list of Hypertransport capabilities and
 * return 1 if a HT MSI capability is found and enabled */
static int __devinit msi_ht_cap_enabled(struct pci_dev *dev)
{
	int pos, ttl = 48;

	pos = pci_find_ht_capability(dev, HT_CAPTYPE_MSI_MAPPING);
	while (pos && ttl--) {
		u8 flags;

		if (pci_read_config_byte(dev, pos + HT_MSI_FLAGS,
					 &flags) == 0)
		{
			printk(KERN_INFO "PCI: Found %s HT MSI Mapping on %s\n",
				flags & HT_MSI_FLAGS_ENABLE ?
				"enabled" : "disabled", pci_name(dev));
			return (flags & HT_MSI_FLAGS_ENABLE) != 0;
		}

		pos = pci_find_next_ht_capability(dev, pos,
						  HT_CAPTYPE_MSI_MAPPING);
	}
	return 0;
}

/* Check the hypertransport MSI mapping to know whether MSI is enabled or not */
static void __devinit quirk_msi_ht_cap(struct pci_dev *dev)
{
	if (dev->subordinate && !msi_ht_cap_enabled(dev)) {
		printk(KERN_WARNING "PCI: MSI quirk detected. "
		       "MSI disabled on chipset %s.\n",
		       pci_name(dev));
		dev->subordinate->bus_flags |= PCI_BUS_FLAGS_NO_MSI;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_SERVERWORKS, PCI_DEVICE_ID_SERVERWORKS_HT2000_PCIE,
			quirk_msi_ht_cap);

/* The nVidia CK804 chipset may have 2 HT MSI mappings.
 * MSI are supported if the MSI capability set in any of these mappings.
 */
static void __devinit quirk_nvidia_ck804_msi_ht_cap(struct pci_dev *dev)
{
	struct pci_dev *pdev;

	if (!dev->subordinate)
		return;

	/* check HT MSI cap on this chipset and the root one.
	 * a single one having MSI is enough to be sure that MSI are supported.
	 */
	pdev = pci_get_slot(dev->bus, 0);
	if (!pdev)
		return;
	if (!msi_ht_cap_enabled(dev) && !msi_ht_cap_enabled(pdev)) {
		printk(KERN_WARNING "PCI: MSI quirk detected. "
		       "MSI disabled on chipset %s.\n",
		       pci_name(dev));
		dev->subordinate->bus_flags |= PCI_BUS_FLAGS_NO_MSI;
	}
	pci_dev_put(pdev);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_CK804_PCIE,
			quirk_nvidia_ck804_msi_ht_cap);
#endif /* CONFIG_PCI_MSI */
