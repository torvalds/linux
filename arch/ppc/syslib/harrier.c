/*
 * Motorola MCG Harrier northbridge/memory controller support
 *
 * Author: Dale Farnsworth
 *         dale.farnsworth@mvista.com
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/harrier_defs.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pci.h>
#include <asm/pci-bridge.h>
#include <asm/open_pic.h>
#include <asm/harrier.h>

/* define defaults for inbound windows */
#define HARRIER_ITAT_DEFAULT		(HARRIER_ITAT_ENA | \
					 HARRIER_ITAT_MEM | \
					 HARRIER_ITAT_WPE | \
					 HARRIER_ITAT_GBL)

#define HARRIER_MPAT_DEFAULT		(HARRIER_ITAT_ENA | \
					 HARRIER_ITAT_MEM | \
					 HARRIER_ITAT_WPE | \
					 HARRIER_ITAT_GBL)

/*
 * Initialize the inbound window size on a non-monarch harrier.
 */
void __init harrier_setup_nonmonarch(uint ppc_reg_base, uint in0_size)
{
	u16 temps;
	u32 temp;

	if (in0_size > HARRIER_ITSZ_2GB) {
		printk
		    ("harrier_setup_nonmonarch: Invalid window size code %d\n",
		     in0_size);
		return;
	}

	/* Clear the PCI memory enable bit. If we don't, then when the
	 * inbound windows are enabled below, the corresponding BARs will be
	 * "live" and start answering to PCI memory reads from their default
	 * addresses (0x0), which overlap with system RAM.
	 */
	temps = in_le16((u16 *) (ppc_reg_base +
				 HARRIER_XCSR_CONFIG(PCI_COMMAND)));
	temps &= ~(PCI_COMMAND_MEMORY);
	out_le16((u16 *) (ppc_reg_base + HARRIER_XCSR_CONFIG(PCI_COMMAND)),
		 temps);

	/* Setup a non-prefetchable inbound window */
	out_le32((u32 *) (ppc_reg_base +
			  HARRIER_XCSR_CONFIG(HARRIER_ITSZ0_OFF)), in0_size);

	temp = in_le32((u32 *) (ppc_reg_base +
				HARRIER_XCSR_CONFIG(HARRIER_ITAT0_OFF)));
	temp &= ~HARRIER_ITAT_PRE;
	temp |= HARRIER_ITAT_DEFAULT;
	out_le32((u32 *) (ppc_reg_base +
			  HARRIER_XCSR_CONFIG(HARRIER_ITAT0_OFF)), temp);

	/* Enable the message passing block */
	temp = in_le32((u32 *) (ppc_reg_base +
				HARRIER_XCSR_CONFIG(HARRIER_MPAT_OFF)));
	temp |= HARRIER_MPAT_DEFAULT;
	out_le32((u32 *) (ppc_reg_base +
			  HARRIER_XCSR_CONFIG(HARRIER_MPAT_OFF)), temp);
}

void __init harrier_release_eready(uint ppc_reg_base)
{
	ulong temp;

	/*
	 * Set EREADY to allow the line to be pulled up after everyone is
	 * ready.
	 */
	temp = in_be32((uint *) (ppc_reg_base + HARRIER_MISC_CSR_OFF));
	temp |= HARRIER_EREADY;
	out_be32((uint *) (ppc_reg_base + HARRIER_MISC_CSR_OFF), temp);
}

void __init harrier_wait_eready(uint ppc_reg_base)
{
	ulong temp;

	/*
	 * Poll the ERDYS line until it goes high to indicate that all
	 * non-monarch PrPMCs are ready for bus enumeration (or that there are
	 * no PrPMCs present).
	 */

	/* FIXME: Add a timeout of some kind to prevent endless waits. */
	do {

		temp = in_be32((uint *) (ppc_reg_base + HARRIER_MISC_CSR_OFF));

	} while (!(temp & HARRIER_ERDYS));
}

/*
 * Initialize the Motorola MCG Harrier host bridge.
 *
 * This means setting up the PPC bus to PCI memory and I/O space mappings,
 * setting the PCI memory space address of the MPIC (mapped straight
 * through), and ioremap'ing the mpic registers.
 * 'OpenPIC_Addr' will be set correctly by this routine.
 * This routine will not change the PCI_CONFIG_ADDR or PCI_CONFIG_DATA
 * addresses and assumes that the mapping of PCI memory space back to system
 * memory is set up correctly by PPCBug.
 */
int __init
harrier_init(struct pci_controller *hose,
	     uint ppc_reg_base,
	     ulong processor_pci_mem_start,
	     ulong processor_pci_mem_end,
	     ulong processor_pci_io_start,
	     ulong processor_pci_io_end, ulong processor_mpic_base)
{
	uint addr, offset;

	/*
	 * Some sanity checks...
	 */
	if (((processor_pci_mem_start & 0xffff0000) != processor_pci_mem_start)
	    || ((processor_pci_io_start & 0xffff0000) !=
		processor_pci_io_start)) {
		printk("harrier_init: %s\n",
		       "PPC to PCI mappings must start on 64 KB boundaries");
		return -1;
	}

	if (((processor_pci_mem_end & 0x0000ffff) != 0x0000ffff) ||
	    ((processor_pci_io_end & 0x0000ffff) != 0x0000ffff)) {
		printk("harrier_init: PPC to PCI mappings %s\n",
		       "must end just before a 64 KB boundaries");
		return -1;
	}

	if (((processor_pci_mem_end - processor_pci_mem_start) !=
	     (hose->mem_space.end - hose->mem_space.start)) ||
	    ((processor_pci_io_end - processor_pci_io_start) !=
	     (hose->io_space.end - hose->io_space.start))) {
		printk("harrier_init: %s\n",
		       "PPC and PCI memory or I/O space sizes don't match");
		return -1;
	}

	if ((processor_mpic_base & 0xfffc0000) != processor_mpic_base) {
		printk("harrier_init: %s\n",
		       "MPIC address must start on 256 KB boundary");
		return -1;
	}

	if ((pci_dram_offset & 0xffff0000) != pci_dram_offset) {
		printk("harrier_init: %s\n",
		       "pci_dram_offset must be multiple of 64 KB");
		return -1;
	}

	/*
	 * Program the OTAD/OTOF registers to set up the PCI Mem & I/O
	 * space mappings.  These are the mappings going from the processor to
	 * the PCI bus.
	 *
	 * Note: Don't need to 'AND' start/end addresses with 0xffff0000
	 *       because sanity check above ensures that they are properly
	 *       aligned.
	 */

	/* Set up PPC->PCI Mem mapping */
	addr = processor_pci_mem_start | (processor_pci_mem_end >> 16);
#ifdef CONFIG_HARRIER_STORE_GATHERING
	offset = (hose->mem_space.start - processor_pci_mem_start) | 0x9a;
#else
	offset = (hose->mem_space.start - processor_pci_mem_start) | 0x92;
#endif
	out_be32((uint *) (ppc_reg_base + HARRIER_OTAD0_OFF), addr);
	out_be32((uint *) (ppc_reg_base + HARRIER_OTOF0_OFF), offset);

	/* Set up PPC->PCI I/O mapping -- Contiguous I/O space */
	addr = processor_pci_io_start | (processor_pci_io_end >> 16);
	offset = (hose->io_space.start - processor_pci_io_start) | 0x80;
	out_be32((uint *) (ppc_reg_base + HARRIER_OTAD1_OFF), addr);
	out_be32((uint *) (ppc_reg_base + HARRIER_OTOF1_OFF), offset);

	/* Enable MPIC */
	OpenPIC_Addr = (void *)processor_mpic_base;
	addr = (processor_mpic_base >> 16) | 1;
	out_be16((ushort *) (ppc_reg_base + HARRIER_MBAR_OFF), addr);
	out_8((u_char *) (ppc_reg_base + HARRIER_MPIC_CSR_OFF),
	      HARRIER_MPIC_OPI_ENABLE);

	return 0;
}

/*
 * Find the amount of RAM present.
 * This assumes that PPCBug has initialized the memory controller (SMC)
 * on the Harrier correctly (i.e., it does no sanity checking).
 * It also assumes that the memory base registers are set to configure the
 * memory as contigous starting with "RAM A BASE", "RAM B BASE", etc.
 * however, RAM base registers can be skipped (e.g. A, B, C are set,
 * D is skipped but E is set is okay).
 */
#define	MB	(1024*1024UL)

static uint harrier_size_table[] __initdata = {
	0 * MB,			/* 0 ==>    0 MB */
	32 * MB,		/* 1 ==>   32 MB */
	64 * MB,		/* 2 ==>   64 MB */
	64 * MB,		/* 3 ==>   64 MB */
	128 * MB,		/* 4 ==>  128 MB */
	128 * MB,		/* 5 ==>  128 MB */
	128 * MB,		/* 6 ==>  128 MB */
	256 * MB,		/* 7 ==>  256 MB */
	256 * MB,		/* 8 ==>  256 MB */
	256 * MB,		/* 9 ==>  256 MB */
	512 * MB,		/* a ==>  512 MB */
	512 * MB,		/* b ==>  512 MB */
	512 * MB,		/* c ==>  512 MB */
	1024 * MB,		/* d ==> 1024 MB */
	1024 * MB,		/* e ==> 1024 MB */
	2048 * MB,		/* f ==> 2048 MB */
};

/*
 * *** WARNING: You MUST have a BAT set up to map in the XCSR regs ***
 *
 * Read the memory controller's registers to determine the amount of system
 * memory.  Assumes that the memory controller registers are already mapped
 * into virtual memory--too early to use ioremap().
 */
unsigned long __init harrier_get_mem_size(uint xcsr_base)
{
	ulong last_addr;
	int i;
	uint vend_dev_id;
	uint *size_table;
	uint val;
	uint *csrp;
	uint size;
	int size_table_entries;

	vend_dev_id = in_be32((uint *) xcsr_base + PCI_VENDOR_ID);

	if (((vend_dev_id & 0xffff0000) >> 16) != PCI_VENDOR_ID_MOTOROLA) {
		printk("harrier_get_mem_size: %s (0x%x)\n",
		       "Not a Motorola Memory Controller", vend_dev_id);
		return 0;
	}

	vend_dev_id &= 0x0000ffff;

	if (vend_dev_id == PCI_DEVICE_ID_MOTOROLA_HARRIER) {
		size_table = harrier_size_table;
		size_table_entries = sizeof(harrier_size_table) /
		    sizeof(harrier_size_table[0]);
	} else {
		printk("harrier_get_mem_size: %s (0x%x)\n",
		       "Not a Harrier", vend_dev_id);
		return 0;
	}

	last_addr = 0;

	csrp = (uint *) (xcsr_base + HARRIER_SDBA_OFF);
	for (i = 0; i < 8; i++) {
		val = in_be32(csrp++);

		if (val & 0x100) {	/* If enabled */
			size = val >> HARRIER_SDB_SIZE_SHIFT;
			size &= HARRIER_SDB_SIZE_MASK;
			if (size >= size_table_entries) {
				break;	/* Register not set correctly */
			}
			size = size_table[size];

			val &= ~(size - 1);
			val += size;

			if (val > last_addr) {
				last_addr = val;
			}
		}
	}

	return last_addr;
}
