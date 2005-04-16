/*
 *	linux/arch/alpha/kernel/console.c
 *
 * Architecture-specific specific support for VGA device on 
 * non-0 I/O hose
 */

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <asm/vga.h>
#include <asm/machvec.h>

#ifdef CONFIG_VGA_HOSE

/*
 * Externally-visible vga hose bases
 */
unsigned long __vga_hose_io_base = 0;	/* base for default hose */
unsigned long __vga_hose_mem_base = 0;	/* base for default hose */

static struct pci_controller * __init 
default_vga_hose_select(struct pci_controller *h1, struct pci_controller *h2)
{
	if (h2->index < h1->index)
		return h2;

	return h1;
}

void __init 
set_vga_hose(struct pci_controller *hose)
{
	if (hose) {
		__vga_hose_io_base = hose->io_space->start;
		__vga_hose_mem_base = hose->mem_space->start;
	}
}

void __init 
locate_and_init_vga(void *(*sel_func)(void *, void *))
{
	struct pci_controller *hose = NULL;
	struct pci_dev *dev = NULL;

	if (!sel_func) sel_func = (void *)default_vga_hose_select;

	for(dev=NULL; (dev=pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, dev));) {
		if (!hose) hose = dev->sysdata;
		else hose = sel_func(hose, dev->sysdata);
	}

	/* Did we already inititialize the correct one? */
	if (conswitchp == &vga_con &&
	    __vga_hose_io_base == hose->io_space->start &&
	    __vga_hose_mem_base == hose->mem_space->start)
		return;

	/* Set the VGA hose and init the new console */
	set_vga_hose(hose);
	take_over_console(&vga_con, 0, MAX_NR_CONSOLES-1, 1);
}

#endif
