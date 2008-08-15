/*
 * Renesas Solutions Highlander R7780RP-1 Support.
 *
 * Copyright (C) 2002  Atom Create Engineering Co., Ltd.
 * Copyright (C) 2006  Paul Mundt
 * Copyright (C) 2008  Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/r7780rp.h>

enum {
	UNUSED = 0,

	/* board specific interrupt sources */

	AX88796,          /* Ethernet controller */
	PSW,              /* Push Switch */
	CF,               /* Compact Flash */

	PCI_A,
	PCI_B,
	PCI_C,
	PCI_D,
};

static struct intc_vect vectors[] __initdata = {
	INTC_IRQ(PCI_A, 65), /* dirty: overwrite cpu vectors for pci */
	INTC_IRQ(PCI_B, 66),
	INTC_IRQ(PCI_C, 67),
	INTC_IRQ(PCI_D, 68),
	INTC_IRQ(CF, IRQ_CF),
	INTC_IRQ(PSW, IRQ_PSW),
	INTC_IRQ(AX88796, IRQ_AX88796),
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xa5000000, 0, 16, /* IRLMSK */
	  { PCI_A, PCI_B, PCI_C, PCI_D, CF, 0, 0, 0,
	    0, 0, 0, 0, 0, 0, PSW, AX88796 } },
};

static unsigned char irl2irq[HL_NR_IRL] __initdata = {
	65, 66, 67, 68,
	IRQ_CF, 0, 0, 0,
	0, 0, 0, 0,
	IRQ_AX88796, IRQ_PSW
};

static DECLARE_INTC_DESC(intc_desc, "r7780rp", vectors,
			 NULL, mask_registers, NULL, NULL);

unsigned char * __init highlander_plat_irq_setup(void)
{
	if (ctrl_inw(0xa5000600)) {
		printk(KERN_INFO "Using r7780rp interrupt controller.\n");
		register_intc_controller(&intc_desc);
		return irl2irq;
	}

	return NULL;
}
