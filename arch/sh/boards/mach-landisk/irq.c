/*
 * arch/sh/boards/mach-landisk/irq.c
 *
 * I-O DATA Device, Inc. LANDISK Support
 *
 * Copyright (C) 2005-2007 kogiidena
 * Copyright (C) 2011 Nobuhiro Iwamatsu
 *
 * Copyright (C) 2001  Ian da Silva, Jeremy Siegel
 * Based largely on io_se.c.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <mach-landisk/mach/iodata_landisk.h>

enum {
	UNUSED = 0,

	PCI_INTA, /* PCI int A */
	PCI_INTB, /* PCI int B */
	PCI_INTC, /* PCI int C */
	PCI_INTD, /* PCI int D */
	ATA,	  /* ATA */
	FATA,	  /* CF */
	POWER,	  /* Power swtich */
	BUTTON,	  /* Button swtich */
};

/* Vectors for LANDISK */
static struct intc_vect vectors_landisk[] __initdata = {
	INTC_IRQ(PCI_INTA, IRQ_PCIINTA),
	INTC_IRQ(PCI_INTB, IRQ_PCIINTB),
	INTC_IRQ(PCI_INTC, IRQ_PCIINTC),
	INTC_IRQ(PCI_INTD, IRQ_PCIINTD),
	INTC_IRQ(ATA, IRQ_ATA),
	INTC_IRQ(FATA, IRQ_FATA),
	INTC_IRQ(POWER, IRQ_POWER),
	INTC_IRQ(BUTTON, IRQ_BUTTON),
};

/* IRLMSK mask register layout for LANDISK */
static struct intc_mask_reg mask_registers_landisk[] __initdata = {
	{ PA_IMASK, 0, 8, /* IRLMSK */
	  {  BUTTON, POWER, FATA, ATA,
	     PCI_INTD, PCI_INTC, PCI_INTB, PCI_INTA,
	  }
	},
};

static DECLARE_INTC_DESC(intc_desc_landisk, "landisk", vectors_landisk, NULL,
			mask_registers_landisk, NULL, NULL);
/*
 * Initialize IRQ setting
 */
void __init init_landisk_IRQ(void)
{
	register_intc_controller(&intc_desc_landisk);
	__raw_writeb(0x00, PA_PWRINT_CLR);
}
