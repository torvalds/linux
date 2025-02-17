// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/arch/alpha/kernel/console.c
 *
 * Architecture-specific specific support for VGA device on 
 * non-0 I/O hose
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/vt.h>
#include <asm/vga.h>
#include <asm/machvec.h>

#include "pci_impl.h"
#include "proto.h"

#ifdef CONFIG_VGA_HOSE

#define VGA_IO_START 0x3C0
#define VGA_IO_END 0x3DF

struct pci_controller *pci_vga_hose;
static struct resource alpha_vga = {
	.name	= "alpha-vga+",
	.flags	= IORESOURCE_IO,
	.start	= VGA_IO_START,
	.end	= VGA_IO_END
};

static struct pci_controller * __init 
default_vga_hose_select(struct pci_controller *h1, struct pci_controller *h2)
{
    return (h2->index < h1->index) ? h2 : h1;
}

void __init 
locate_and_init_vga(void *(* const sel_func)(void *, void *))
{
	struct pci_controller *hose = NULL;
	struct pci_dev *dev = NULL;

	/* Default the select function */
	if (!sel_func) sel_func = (void *)default_vga_hose_select;

	/* Find the console VGA device */
	for(dev=NULL; (dev=pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, dev));) {
		hose = hose ? sel_func(hose, dev->sysdata) : dev->sysdata;
	}

	/* Did we already initialize the correct one? Is there one? */
	if (!hose || (conswitchp == &vga_con && pci_vga_hose == hose))
		return;

	/* Create a new VGA ioport resource WRT the hose it is on. */
	alpha_vga.start += hose->io_space->start;
	alpha_vga.end += hose->io_space->start;
	
	if (request_resource(hose->io_space, &alpha_vga)) {
	    printk(KERN_ERR "Failed to request VGA resource\n")
            return;		    
	}

	/* Set the VGA hose and init the new console. */
	pci_vga_hose = hose;
	console_lock();
	do_take_over_console(&vga_con, 0, MAX_NR_CONSOLES-1, 1);
	console_unlock();
}

void __init
find_console_vga_hose(void)
{
	u64 *pu64 = (u64 *)((u64)hwrpb + hwrpb->ctbt_offset);

	if (!hwrpb || !pu64) {
	    printk(KERN_ERR "hwrpb or pu64 is NULL\n");
	    return;
	}

	if (pu64[7] == 3) {	/* TERM_TYPE == graphics */
		struct pci_controller *hose;
		int h = (pu64[30] >> 24) & 0xff;	/* console hose # */

		/*
		 * Our hose numbering DOES match the console's, so find
		 * the right one...
		 */
		for (hose = hose_head; hose; hose = hose->next) {
			if (hose->index == h) break;
		}

		if (hose) {
			printk(KERN_INFO "Console graphics on hose %d\n", h);
			pci_vga_hose = hose;
		}
	}
}

#endif
