/*
 * Low-level PCI config space access for OLPC systems who lack the VSA
 * PCI virtualization software.
 *
 * Copyright © 2006  Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The AMD Geode chipset (ie: GX2 processor, cs5536 I/O companion device)
 * has some I/O functions (display, southbridge, sound, USB HCIs, etc)
 * that more or less behave like PCI devices, but the hardware doesn't
 * directly implement the PCI configuration space headers.  AMD provides
 * "VSA" (Virtual System Architecture) software that emulates PCI config
 * space for these devices, by trapping I/O accesses to PCI config register
 * (CF8/CFC) and running some code in System Management Mode interrupt state.
 * On the OLPC platform, we don't want to use that VSA code because
 * (a) it slows down suspend/resume, and (b) recompiling it requires special
 * compilers that are hard to get.  So instead of letting the complex VSA
 * code simulate the PCI config registers for the on-chip devices, we
 * just simulate them the easy way, by inserting the code into the
 * pci_write_config and pci_read_config path.  Most of the config registers
 * are read-only anyway, so the bulk of the simulation is just table lookup.
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <asm/olpc.h>
#include <asm/geode.h>
#include <asm/pci_x86.h>

/*
 * In the tables below, the first two line (8 longwords) are the
 * size masks that are used when the higher level PCI code determines
 * the size of the region by writing ~0 to a base address register
 * and reading back the result.
 *
 * The following lines are the values that are read during normal
 * PCI config access cycles, i.e. not after just having written
 * ~0 to a base address register.
 */

static const uint32_t lxnb_hdr[] = {  /* dev 1 function 0 - devfn = 8 */
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,

	0x281022, 0x2200005, 0x6000021, 0x80f808,	/* AMD Vendor ID */
	0x0,	0x0,	0x0,	0x0,   /* No virtual registers, hence no BAR */
	0x0,	0x0,	0x0,	0x28100b,
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
};

static const uint32_t gxnb_hdr[] = {  /* dev 1 function 0 - devfn = 8 */
	0xfffffffd, 0x0, 0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,

	0x28100b, 0x2200005, 0x6000021, 0x80f808,	/* NSC Vendor ID */
	0xac1d,	0x0,	0x0,	0x0,  /* I/O BAR - base of virtual registers */
	0x0,	0x0,	0x0,	0x28100b,
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
};

static const uint32_t lxfb_hdr[] = {  /* dev 1 function 1 - devfn = 9 */
	0xff000008, 0xffffc000, 0xffffc000, 0xffffc000,
	0xffffc000,	0x0,	0x0,	0x0,

	0x20811022, 0x2200003, 0x3000000, 0x0,		/* AMD Vendor ID */
	0xfd000000, 0xfe000000, 0xfe004000, 0xfe008000, /* FB, GP, VG, DF */
	0xfe00c000, 0x0, 0x0,	0x30100b,		/* VIP */
	0x0,	0x0,	0x0,	0x10e,	   /* INTA, IRQ14 for graphics accel */
	0x0,	0x0,	0x0,	0x0,
	0x3d0,	0x3c0,	0xa0000, 0x0,	    /* VG IO, VG IO, EGA FB, MONO FB */
	0x0,	0x0,	0x0,	0x0,
};

static const uint32_t gxfb_hdr[] = {  /* dev 1 function 1 - devfn = 9 */
	0xff800008, 0xffffc000, 0xffffc000, 0xffffc000,
	0x0,	0x0,	0x0,	0x0,

	0x30100b, 0x2200003, 0x3000000, 0x0,		/* NSC Vendor ID */
	0xfd000000, 0xfe000000, 0xfe004000, 0xfe008000,	/* FB, GP, VG, DF */
	0x0,	0x0,	0x0,	0x30100b,
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
	0x3d0,	0x3c0,	0xa0000, 0x0,  	    /* VG IO, VG IO, EGA FB, MONO FB */
	0x0,	0x0,	0x0,	0x0,
};

static const uint32_t aes_hdr[] = {	/* dev 1 function 2 - devfn = 0xa */
	0xffffc000, 0x0, 0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,

	0x20821022, 0x2a00006, 0x10100000, 0x8,		/* NSC Vendor ID */
	0xfe010000, 0x0, 0x0,	0x0,			/* AES registers */
	0x0,	0x0,	0x0,	0x20821022,
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
};


static const uint32_t isa_hdr[] = {  /* dev f function 0 - devfn = 78 */
	0xfffffff9, 0xffffff01, 0xffffffc1, 0xffffffe1,
	0xffffff81, 0xffffffc1, 0x0, 0x0,

	0x20901022, 0x2a00049, 0x6010003, 0x802000,
	0x18b1,	0x1001,	0x1801,	0x1881,	/* SMB-8   GPIO-256 MFGPT-64  IRQ-32 */
	0x1401,	0x1841,	0x0,	0x20901022,		/* PMS-128 ACPI-64 */
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0xaa5b,			/* IRQ steering */
	0x0,	0x0,	0x0,	0x0,
};

static const uint32_t ac97_hdr[] = {  /* dev f function 3 - devfn = 7b */
	0xffffff81, 0x0, 0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,

	0x20931022, 0x2a00041, 0x4010001, 0x0,
	0x1481,	0x0,	0x0,	0x0,			/* I/O BAR-128 */
	0x0,	0x0,	0x0,	0x20931022,
	0x0,	0x0,	0x0,	0x205,			/* IntB, IRQ5 */
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
};

static const uint32_t ohci_hdr[] = {  /* dev f function 4 - devfn = 7c */
	0xfffff000, 0x0, 0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,

	0x20941022, 0x2300006, 0xc031002, 0x0,
	0xfe01a000, 0x0, 0x0,	0x0,			/* MEMBAR-1000 */
	0x0,	0x0,	0x0,	0x20941022,
	0x0,	0x40,	0x0,	0x40a,			/* CapPtr INT-D, IRQA */
	0xc8020001, 0x0, 0x0,	0x0,	/* Capabilities - 40 is R/O,
					   44 is mask 8103 (power control) */
	0x0,	0x0,	0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,
};

static const uint32_t ehci_hdr[] = {  /* dev f function 4 - devfn = 7d */
	0xfffff000, 0x0, 0x0,	0x0,
	0x0,	0x0,	0x0,	0x0,

	0x20951022, 0x2300006, 0xc032002, 0x0,
	0xfe01b000, 0x0, 0x0,	0x0,			/* MEMBAR-1000 */
	0x0,	0x0,	0x0,	0x20951022,
	0x0,	0x40,	0x0,	0x40a,			/* CapPtr INT-D, IRQA */
	0xc8020001, 0x0, 0x0,	0x0,	/* Capabilities - 40 is R/O, 44 is
					   mask 8103 (power control) */
#if 0
	0x1,	0x40080000, 0x0, 0x0,	/* EECP - see EHCI spec section 2.1.7 */
#endif
	0x01000001, 0x0, 0x0,	0x0,	/* EECP - see EHCI spec section 2.1.7 */
	0x2020,	0x0,	0x0,	0x0,	/* (EHCI page 8) 60 SBRN (R/O),
					   61 FLADJ (R/W), PORTWAKECAP  */
};

static uint32_t ff_loc = ~0;
static uint32_t zero_loc;
static int bar_probing;		/* Set after a write of ~0 to a BAR */
static int is_lx;

#define NB_SLOT 0x1	/* Northbridge - GX chip - Device 1 */
#define SB_SLOT 0xf	/* Southbridge - CS5536 chip - Device F */

static int is_simulated(unsigned int bus, unsigned int devfn)
{
	return (!bus && ((PCI_SLOT(devfn) == NB_SLOT) ||
			(PCI_SLOT(devfn) == SB_SLOT)));
}

static uint32_t *hdr_addr(const uint32_t *hdr, int reg)
{
	uint32_t addr;

	/*
	 * This is a little bit tricky.  The header maps consist of
	 * 0x20 bytes of size masks, followed by 0x70 bytes of header data.
	 * In the normal case, when not probing a BAR's size, we want
	 * to access the header data, so we add 0x20 to the reg offset,
	 * thus skipping the size mask area.
	 * In the BAR probing case, we want to access the size mask for
	 * the BAR, so we subtract 0x10 (the config header offset for
	 * BAR0), and don't skip the size mask area.
	 */

	addr = (uint32_t)hdr + reg + (bar_probing ? -0x10 : 0x20);

	bar_probing = 0;
	return (uint32_t *)addr;
}

static int pci_olpc_read(unsigned int seg, unsigned int bus,
		unsigned int devfn, int reg, int len, uint32_t *value)
{
	uint32_t *addr;

	WARN_ON(seg);

	/* Use the hardware mechanism for non-simulated devices */
	if (!is_simulated(bus, devfn))
		return pci_direct_conf1.read(seg, bus, devfn, reg, len, value);

	/*
	 * No device has config registers past 0x70, so we save table space
	 * by not storing entries for the nonexistent registers
	 */
	if (reg >= 0x70)
		addr = &zero_loc;
	else {
		switch (devfn) {
		case  0x8:
			addr = hdr_addr(is_lx ? lxnb_hdr : gxnb_hdr, reg);
			break;
		case  0x9:
			addr = hdr_addr(is_lx ? lxfb_hdr : gxfb_hdr, reg);
			break;
		case  0xa:
			addr = is_lx ? hdr_addr(aes_hdr, reg) : &ff_loc;
			break;
		case 0x78:
			addr = hdr_addr(isa_hdr, reg);
			break;
		case 0x7b:
			addr = hdr_addr(ac97_hdr, reg);
			break;
		case 0x7c:
			addr = hdr_addr(ohci_hdr, reg);
			break;
		case 0x7d:
			addr = hdr_addr(ehci_hdr, reg);
			break;
		default:
			addr = &ff_loc;
			break;
		}
	}
	switch (len) {
	case 1:
		*value = *(uint8_t *)addr;
		break;
	case 2:
		*value = *(uint16_t *)addr;
		break;
	case 4:
		*value = *addr;
		break;
	default:
		BUG();
	}

	return 0;
}

static int pci_olpc_write(unsigned int seg, unsigned int bus,
		unsigned int devfn, int reg, int len, uint32_t value)
{
	WARN_ON(seg);

	/* Use the hardware mechanism for non-simulated devices */
	if (!is_simulated(bus, devfn))
		return pci_direct_conf1.write(seg, bus, devfn, reg, len, value);

	/* XXX we may want to extend this to simulate EHCI power management */

	/*
	 * Mostly we just discard writes, but if the write is a size probe
	 * (i.e. writing ~0 to a BAR), we remember it and arrange to return
	 * the appropriate size mask on the next read.  This is cheating
	 * to some extent, because it depends on the fact that the next
	 * access after such a write will always be a read to the same BAR.
	 */

	if ((reg >= 0x10) && (reg < 0x2c)) {
		/* write is to a BAR */
		if (value == ~0)
			bar_probing = 1;
	} else {
		/*
		 * No warning on writes to ROM BAR, CMD, LATENCY_TIMER,
		 * CACHE_LINE_SIZE, or PM registers.
		 */
		if ((reg != PCI_ROM_ADDRESS) && (reg != PCI_COMMAND_MASTER) &&
				(reg != PCI_LATENCY_TIMER) &&
				(reg != PCI_CACHE_LINE_SIZE) && (reg != 0x44))
			printk(KERN_WARNING "OLPC PCI: Config write to devfn"
				" %x reg %x value %x\n", devfn, reg, value);
	}

	return 0;
}

static const struct pci_raw_ops pci_olpc_conf = {
	.read =	pci_olpc_read,
	.write = pci_olpc_write,
};

int __init pci_olpc_init(void)
{
	printk(KERN_INFO "PCI: Using configuration type OLPC XO-1\n");
	raw_pci_ops = &pci_olpc_conf;
	is_lx = is_geode_lx();
	return 0;
}
