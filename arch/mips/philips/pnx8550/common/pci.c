/*
 *
 * BRIEF MODULE DESCRIPTION
 *
 * Author: source@mvista.com
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <pci.h>
#include <glb.h>
#include <nand.h>

static struct resource pci_io_resource = {
	.start	= PNX8550_PCIIO + 0x1000,	/* reserve regacy I/O space */
	.end	= PNX8550_PCIIO + PNX8550_PCIIO_SIZE,
	.name	= "pci IO space",
	.flags	= IORESOURCE_IO
};

static struct resource pci_mem_resource = {
	.start	= PNX8550_PCIMEM,
	.end	= PNX8550_PCIMEM + PNX8550_PCIMEM_SIZE - 1,
	.name	= "pci memory space",
	.flags	= IORESOURCE_MEM
};

extern struct pci_ops pnx8550_pci_ops;

static struct pci_controller pnx8550_controller = {
	.pci_ops	= &pnx8550_pci_ops,
	.io_resource	= &pci_io_resource,
	.mem_resource	= &pci_mem_resource,
};

/* Return the total size of DRAM-memory, (RANK0 + RANK1) */
static inline unsigned long get_system_mem_size(void)
{
	/* Read IP2031_RANK0_ADDR_LO */
	unsigned long dram_r0_lo = inl(PCI_BASE | 0x65010);
	/* Read IP2031_RANK1_ADDR_HI */
	unsigned long dram_r1_hi = inl(PCI_BASE | 0x65018);

	return dram_r1_hi - dram_r0_lo + 1;
}

static int __init pnx8550_pci_setup(void)
{
	int pci_mem_code;
	int mem_size = get_system_mem_size() >> 20;

	/* Clear the Global 2 Register, PCI Inta Output Enable Registers
	   Bit 1:Enable DAC Powerdown
	  -> 0:DACs are enabled and are working normally
	     1:DACs are powerdown
	   Bit 0:Enable of PCI inta output
	  -> 0 = Disable PCI inta output
	     1 = Enable PCI inta output
	*/
	PNX8550_GLB2_ENAB_INTA_O = 0;

	/* Calc the PCI mem size code */
	if (mem_size >= 128)
		pci_mem_code = SIZE_128M;
	else if (mem_size >= 64)
		pci_mem_code = SIZE_64M;
	else if (mem_size >= 32)
		pci_mem_code = SIZE_32M;
	else
		pci_mem_code = SIZE_16M;

	/* Set PCI_XIO registers */
	outl(pci_mem_resource.start, PCI_BASE | PCI_BASE1_LO);
	outl(pci_mem_resource.end + 1, PCI_BASE | PCI_BASE1_HI);
	outl(pci_io_resource.start, PCI_BASE | PCI_BASE2_LO);
	outl(pci_io_resource.end, PCI_BASE | PCI_BASE2_HI);

	/* Send memory transaction via PCI_BASE2 */
	outl(0x00000001, PCI_BASE | PCI_IO);

	/* Unlock the setup register */
	outl(0xca, PCI_BASE | PCI_UNLOCKREG);

	/*
	 * BAR0 of PNX8550 (pci base 10) must be zero in order for ide
	 * to work, and in order for bus_to_baddr to work without any
	 * hacks.
	 */
	outl(0x00000000, PCI_BASE | PCI_BASE10);

	/*
	 *These two bars are set by default or the boot code.
	 * However, it's safer to set them here so we're not boot
	 * code dependent.
	 */
	outl(0x1be00000, PCI_BASE | PCI_BASE14);  /* PNX MMIO */
	outl(PNX8550_NAND_BASE_ADDR, PCI_BASE | PCI_BASE18);  /* XIO      */

	outl(PCI_EN_TA |
	     PCI_EN_PCI2MMI |
	     PCI_EN_XIO |
	     PCI_SETUP_BASE18_SIZE(SIZE_32M) |
	     PCI_SETUP_BASE18_EN |
	     PCI_SETUP_BASE14_EN |
	     PCI_SETUP_BASE10_PREF |
	     PCI_SETUP_BASE10_SIZE(pci_mem_code) |
	     PCI_SETUP_CFGMANAGE_EN |
	     PCI_SETUP_PCIARB_EN,
	     PCI_BASE |
	     PCI_SETUP);	/* PCI_SETUP */
	outl(0x00000000, PCI_BASE | PCI_CTRL);	/* PCI_CONTROL */

	register_pci_controller(&pnx8550_controller);

	return 0;
}

arch_initcall(pnx8550_pci_setup);
