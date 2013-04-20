/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SNI specific PCI support for RM200/RM300.
 *
 * Copyright (C) 1997 - 2000, 2003, 04 Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/mipsregs.h>
#include <asm/sni.h>

#include <irq.h>

/*
 * PCIMT Shortcuts ...
 */
#define SCSI	PCIMT_IRQ_SCSI
#define ETH	PCIMT_IRQ_ETHERNET
#define INTA	PCIMT_IRQ_INTA
#define INTB	PCIMT_IRQ_INTB
#define INTC	PCIMT_IRQ_INTC
#define INTD	PCIMT_IRQ_INTD

/*
 * Device 0: PCI EISA Bridge	(directly routed)
 * Device 1: NCR53c810 SCSI	(directly routed)
 * Device 2: PCnet32 Ethernet	(directly routed)
 * Device 3: VGA		(routed to INTB)
 * Device 4: Unused
 * Device 5: Slot 2
 * Device 6: Slot 3
 * Device 7: Slot 4
 *
 * Documentation says the VGA is device 5 and device 3 is unused but that
 * seem to be a documentation error.  At least on my RM200C the Cirrus
 * Logic CL-GD5434 VGA is device 3.
 */
static char irq_tab_rm200[8][5] __initdata = {
	/*	 INTA  INTB  INTC  INTD */
	{     0,    0,	  0,	0,    0 },	/* EISA bridge */
	{  SCSI, SCSI, SCSI, SCSI, SCSI },	/* SCSI */
	{   ETH,  ETH,	ETH,  ETH,  ETH },	/* Ethernet */
	{  INTB, INTB, INTB, INTB, INTB },	/* VGA */
	{     0,    0,	  0,	0,    0 },	/* Unused */
	{     0, INTB, INTC, INTD, INTA },	/* Slot 2 */
	{     0, INTC, INTD, INTA, INTB },	/* Slot 3 */
	{     0, INTD, INTA, INTB, INTC },	/* Slot 4 */
};

/*
 * In Revision D of the RM300 Device 2 has become a normal purpose Slot 1
 *
 * The VGA card is optional for RM300 systems.
 */
static char irq_tab_rm300d[8][5] __initdata = {
	/*	 INTA  INTB  INTC  INTD */
	{     0,    0,	  0,	0,    0 },	/* EISA bridge */
	{  SCSI, SCSI, SCSI, SCSI, SCSI },	/* SCSI */
	{     0, INTC, INTD, INTA, INTB },	/* Slot 1 */
	{  INTB, INTB, INTB, INTB, INTB },	/* VGA */
	{     0,    0,	  0,	0,    0 },	/* Unused */
	{     0, INTB, INTC, INTD, INTA },	/* Slot 2 */
	{     0, INTC, INTD, INTA, INTB },	/* Slot 3 */
	{     0, INTD, INTA, INTB, INTC },	/* Slot 4 */
};

static char irq_tab_rm300e[5][5] __initdata = {
	/*	 INTA  INTB  INTC  INTD */
	{     0,    0,	  0,	0,    0 },	/* HOST bridge */
	{  SCSI, SCSI, SCSI, SCSI, SCSI },	/* SCSI */
	{     0, INTC, INTD, INTA, INTB },	/* Bridge/i960 */
	{     0, INTD, INTA, INTB, INTC },	/* Slot 1 */
	{     0, INTA, INTB, INTC, INTD },	/* Slot 2 */
};
#undef SCSI
#undef ETH
#undef INTA
#undef INTB
#undef INTC
#undef INTD


/*
 * PCIT Shortcuts ...
 */
#define SCSI0	PCIT_IRQ_SCSI0
#define SCSI1	PCIT_IRQ_SCSI1
#define ETH	PCIT_IRQ_ETHERNET
#define INTA	PCIT_IRQ_INTA
#define INTB	PCIT_IRQ_INTB
#define INTC	PCIT_IRQ_INTC
#define INTD	PCIT_IRQ_INTD

static char irq_tab_pcit[13][5] __initdata = {
	/*	 INTA  INTB  INTC  INTD */
	{     0,     0,	    0,	   0,	  0 },	/* HOST bridge */
	{ SCSI0, SCSI0, SCSI0, SCSI0, SCSI0 },	/* SCSI */
	{ SCSI1, SCSI1, SCSI1, SCSI1, SCSI1 },	/* SCSI */
	{   ETH,   ETH,	  ETH,	 ETH,	ETH },	/* Ethernet */
	{     0,  INTA,	 INTB,	INTC,  INTD },	/* PCI-PCI bridge */
	{     0,     0,	    0,	   0,	  0 },	/* Unused */
	{     0,     0,	    0,	   0,	  0 },	/* Unused */
	{     0,     0,	    0,	   0,	  0 },	/* Unused */
	{     0,  INTA,	 INTB,	INTC,  INTD },	/* Slot 1 */
	{     0,  INTB,	 INTC,	INTD,  INTA },	/* Slot 2 */
	{     0,  INTC,	 INTD,	INTA,  INTB },	/* Slot 3 */
	{     0,  INTD,	 INTA,	INTB,  INTC },	/* Slot 4 */
	{     0,  INTA,	 INTB,	INTC,  INTD },	/* Slot 5 */
};

static char irq_tab_pcit_cplus[13][5] __initdata = {
	/*	 INTA  INTB  INTC  INTD */
	{     0,     0,	    0,	   0,	  0 },	/* HOST bridge */
	{     0,  INTB,	 INTC,	INTD,  INTA },	/* PCI Slot 9 */
	{     0,     0,	    0,	   0,	  0 },	/* PCI-EISA */
	{     0,     0,	    0,	   0,	  0 },	/* Unused */
	{     0,  INTA,	 INTB,	INTC,  INTD },	/* PCI-PCI bridge */
	{     0,  INTB,	 INTC,	INTD,  INTA },	/* fixup */
};

static inline int is_rm300_revd(void)
{
	unsigned char csmsr = *(volatile unsigned char *)PCIMT_CSMSR;

	return (csmsr & 0xa0) == 0x20;
}

int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	switch (sni_brd_type) {
	case SNI_BRD_PCI_TOWER_CPLUS:
		if (slot == 4) {
			/*
			 * SNI messed up interrupt wiring for onboard
			 * PCI bus 1; we need to fix this up here
			 */
			while (dev && dev->bus->number != 1)
				dev = dev->bus->self;
			if (dev && dev->devfn >= PCI_DEVFN(4, 0))
				slot = 5;
		}
		return irq_tab_pcit_cplus[slot][pin];
	case SNI_BRD_PCI_TOWER:
		return irq_tab_pcit[slot][pin];

	case SNI_BRD_PCI_MTOWER:
		if (is_rm300_revd())
			return irq_tab_rm300d[slot][pin];
		/* fall through */

	case SNI_BRD_PCI_DESKTOP:
		return irq_tab_rm200[slot][pin];

	case SNI_BRD_PCI_MTOWER_CPLUS:
		return irq_tab_rm300e[slot][pin];
	}

	return 0;
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}
