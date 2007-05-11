/*
 * Common Motorola PowerPlus Platform--really Falcon/Raven or HAWK.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pci.h>
#include <asm/pci-bridge.h>
#include <asm/open_pic.h>
#include <asm/hawk.h>

/*
 * The Falcon/Raven and HAWK has 4 sets of registers:
 *   1) PPC Registers which define the mappings from PPC bus to PCI bus,
 *      etc.
 *   2) PCI Registers which define the mappings from PCI bus to PPC bus and the
 *      MPIC base address.
 *   3) MPIC registers.
 *   4) System Memory Controller (SMC) registers.
 */

/*
 * Initialize the Motorola MCG Raven or HAWK host bridge.
 *
 * This means setting up the PPC bus to PCI memory and I/O space mappings,
 * setting the PCI memory space address of the MPIC (mapped straight
 * through), and ioremap'ing the mpic registers.
 * This routine will set the PCI_CONFIG_ADDR or PCI_CONFIG_DATA
 * addresses based on the PCI I/O address that is passed in.
 * 'OpenPIC_Addr' will be set correctly by this routine.
 */
int __init
hawk_init(struct pci_controller *hose,
	     uint ppc_reg_base,
	     ulong processor_pci_mem_start,
	     ulong processor_pci_mem_end,
	     ulong processor_pci_io_start,
	     ulong processor_pci_io_end,
	     ulong processor_mpic_base)
{
	uint		addr, offset;

	/*
	 * Some sanity checks...
	 */
	if (((processor_pci_mem_start&0xffff0000) != processor_pci_mem_start) ||
	    ((processor_pci_io_start &0xffff0000) != processor_pci_io_start)) {
		printk("hawk_init: %s\n",
			"PPC to PCI mappings must start on 64 KB boundaries");
		return -1;
	}

	if (((processor_pci_mem_end  &0x0000ffff) != 0x0000ffff) ||
	    ((processor_pci_io_end   &0x0000ffff) != 0x0000ffff)) {
		printk("hawk_init: PPC to PCI mappings %s\n",
			"must end just before a 64 KB boundaries");
		return -1;
	}

	if (((processor_pci_mem_end - processor_pci_mem_start) !=
	     (hose->mem_space.end - hose->mem_space.start)) ||
	    ((processor_pci_io_end - processor_pci_io_start) !=
	     (hose->io_space.end - hose->io_space.start))) {
		printk("hawk_init: %s\n",
			"PPC and PCI memory or I/O space sizes don't match");
		return -1;
	}

	if ((processor_mpic_base & 0xfffc0000) != processor_mpic_base) {
		printk("hawk_init: %s\n",
			"MPIC address must start on 256 MB boundary");
		return -1;
	}

	if ((pci_dram_offset & 0xffff0000) != pci_dram_offset) {
		printk("hawk_init: %s\n",
			"pci_dram_offset must be multiple of 64 KB");
		return -1;
	}

	/*
	 * Disable previous PPC->PCI mappings.
	 */
	out_be32((uint *)(ppc_reg_base + HAWK_PPC_XSOFF0_OFF), 0x00000000);
	out_be32((uint *)(ppc_reg_base + HAWK_PPC_XSOFF1_OFF), 0x00000000);
	out_be32((uint *)(ppc_reg_base + HAWK_PPC_XSOFF2_OFF), 0x00000000);
	out_be32((uint *)(ppc_reg_base + HAWK_PPC_XSOFF3_OFF), 0x00000000);

	/*
	 * Program the XSADD/XSOFF registers to set up the PCI Mem & I/O
	 * space mappings.  These are the mappings going from the processor to
	 * the PCI bus.
	 *
	 * Note: Don't need to 'AND' start/end addresses with 0xffff0000
	 *	 because sanity check above ensures that they are properly
	 *	 aligned.
	 */

	/* Set up PPC->PCI Mem mapping */
	addr = processor_pci_mem_start | (processor_pci_mem_end >> 16);
	offset = (hose->mem_space.start - processor_pci_mem_start) | 0xd2;
	out_be32((uint *)(ppc_reg_base + HAWK_PPC_XSADD0_OFF), addr);
	out_be32((uint *)(ppc_reg_base + HAWK_PPC_XSOFF0_OFF), offset);

	/* Set up PPC->MPIC mapping on the bridge */
	addr = processor_mpic_base |
	        (((processor_mpic_base + HAWK_MPIC_SIZE) >> 16) - 1);
	/* No write posting for this PCI Mem space */
	offset = (hose->mem_space.start - processor_pci_mem_start) | 0xc2;

	out_be32((uint *)(ppc_reg_base + HAWK_PPC_XSADD1_OFF), addr);
	out_be32((uint *)(ppc_reg_base + HAWK_PPC_XSOFF1_OFF), offset);

	/* Set up PPC->PCI I/O mapping -- Contiguous I/O space */
	addr = processor_pci_io_start | (processor_pci_io_end >> 16);
	offset = (hose->io_space.start - processor_pci_io_start) | 0xc0;
	out_be32((uint *)(ppc_reg_base + HAWK_PPC_XSADD3_OFF), addr);
	out_be32((uint *)(ppc_reg_base + HAWK_PPC_XSOFF3_OFF), offset);

	hose->io_base_virt = (void *)ioremap(processor_pci_io_start,
			(processor_pci_io_end - processor_pci_io_start + 1));

	/*
	 * Set up the indirect method of accessing PCI config space.
	 * The PCI config addr/data pair based on start addr of PCI I/O space.
	 */
	setup_indirect_pci(hose,
			   processor_pci_io_start + HAWK_PCI_CONFIG_ADDR_OFF,
			   processor_pci_io_start + HAWK_PCI_CONFIG_DATA_OFF);

	/*
	 * Disable previous PCI->PPC mappings.
	 */

	/* XXXX Put in mappings from PCI bus to processor bus XXXX */

	/*
	 * Disable MPIC response to PCI I/O space (BAR 0).
	 * Make MPIC respond to PCI Mem space at specified address.
	 * (BAR 1).
	 */
	early_write_config_dword(hose,
			         0,
			         PCI_DEVFN(0,0),
			         PCI_BASE_ADDRESS_0,
			         0x00000000 | 0x1);

	early_write_config_dword(hose,
			         0,
			         PCI_DEVFN(0,0),
			         PCI_BASE_ADDRESS_1,
			         (processor_mpic_base -
				 processor_pci_mem_start + 
				 hose->mem_space.start) | 0x0);

	/* Map MPIC into virtual memory */
	OpenPIC_Addr = ioremap(processor_mpic_base, HAWK_MPIC_SIZE);

	return 0;
}

/*
 * Find the amount of RAM present.
 * This assumes that PPCBug has initialized the memory controller (SMC)
 * on the Falcon/HAWK correctly (i.e., it does no sanity checking).
 * It also assumes that the memory base registers are set to configure the
 * memory as contiguous starting with "RAM A BASE", "RAM B BASE", etc.
 * however, RAM base registers can be skipped (e.g. A, B, C are set,
 * D is skipped but E is set is okay).
 */
#define	MB	(1024*1024)

static uint reg_offset_table[] __initdata = {
	HAWK_SMC_RAM_A_SIZE_REG_OFF,
	HAWK_SMC_RAM_B_SIZE_REG_OFF,
	HAWK_SMC_RAM_C_SIZE_REG_OFF,
	HAWK_SMC_RAM_D_SIZE_REG_OFF,
	HAWK_SMC_RAM_E_SIZE_REG_OFF,
	HAWK_SMC_RAM_F_SIZE_REG_OFF,
	HAWK_SMC_RAM_G_SIZE_REG_OFF,
	HAWK_SMC_RAM_H_SIZE_REG_OFF
};

static uint falcon_size_table[] __initdata = {
	   0 * MB, /* 0 ==>    0 MB */
	  16 * MB, /* 1 ==>   16 MB */
	  32 * MB, /* 2 ==>   32 MB */
	  64 * MB, /* 3 ==>   64 MB */
	 128 * MB, /* 4 ==>  128 MB */
	 256 * MB, /* 5 ==>  256 MB */
        1024 * MB, /* 6 ==> 1024 MB (1 GB) */
};

static uint hawk_size_table[] __initdata = {
	  0 * MB, /* 0 ==>    0 MB */
	 32 * MB, /* 1 ==>   32 MB */
	 64 * MB, /* 2 ==>   64 MB */
	 64 * MB, /* 3 ==>   64 MB */
	128 * MB, /* 4 ==>  128 MB */
	128 * MB, /* 5 ==>  128 MB */
	128 * MB, /* 6 ==>  128 MB */
	256 * MB, /* 7 ==>  256 MB */
	256 * MB, /* 8 ==>  256 MB */
	512 * MB, /* 9 ==>  512 MB */
};

/*
 * *** WARNING: You MUST have a BAT set up to map in the SMC regs ***
 *
 * Read the memory controller's registers to determine the amount of system
 * memory.  Assumes that the memory controller registers are already mapped
 * into virtual memory--too early to use ioremap().
 */
unsigned long __init
hawk_get_mem_size(uint smc_base)
{
	unsigned long	total;
	int		i, size_table_entries, reg_limit;
	uint		vend_dev_id;
	uint		*size_table;
	u_char		val;


	vend_dev_id = in_be32((uint *)smc_base + PCI_VENDOR_ID);

	if (((vend_dev_id & 0xffff0000) >> 16) != PCI_VENDOR_ID_MOTOROLA) {
		printk("hawk_get_mem_size: %s (0x%x)\n",
			"Not a Motorola Memory Controller", vend_dev_id);
		return 0;
	}

	vend_dev_id &= 0x0000ffff;

	if (vend_dev_id == PCI_DEVICE_ID_MOTOROLA_FALCON) {
		size_table = falcon_size_table;
		size_table_entries = sizeof(falcon_size_table) /
				     sizeof(falcon_size_table[0]);

		reg_limit = FALCON_SMC_REG_COUNT;
	}
	else if (vend_dev_id == PCI_DEVICE_ID_MOTOROLA_HAWK) {
		size_table = hawk_size_table;
		size_table_entries = sizeof(hawk_size_table) /
				     sizeof(hawk_size_table[0]);
		reg_limit = HAWK_SMC_REG_COUNT;
	}
	else {
		printk("hawk_get_mem_size: %s (0x%x)\n",
			"Not a Falcon or HAWK", vend_dev_id);
		return 0;
	}

	total = 0;

	/* Check every reg because PPCBug may skip some */
	for (i=0; i<reg_limit; i++) {
		val = in_8((u_char *)(smc_base + reg_offset_table[i]));

		if (val & 0x80) {	/* If enabled */
			val &= 0x0f;

			/* Don't go past end of size_table */
			if (val < size_table_entries) {
				total += size_table[val];
			}
			else {	/* Register not set correctly */
				break;
			}
		}
	}

	return total;
}

int __init
hawk_mpic_init(unsigned int pci_mem_offset)
{
	unsigned short	devid;
	unsigned int	pci_membase;

	/* Check the first PCI device to see if it is a Raven or Hawk. */
	early_read_config_word(0, 0, 0, PCI_DEVICE_ID, &devid);

	switch (devid) {
	case PCI_DEVICE_ID_MOTOROLA_RAVEN:
	case PCI_DEVICE_ID_MOTOROLA_HAWK:
		break;
	default:
		OpenPIC_Addr = NULL;
		return 1;
	}

	/* Read the memory base register. */
	early_read_config_dword(0, 0, 0, PCI_BASE_ADDRESS_1, &pci_membase);

	if (pci_membase == 0) {
		OpenPIC_Addr = NULL;
		return 1;
	}

	/* Map the MPIC registers to virtual memory. */
	OpenPIC_Addr = ioremap(pci_membase + pci_mem_offset, 0x22000);

	return 0;
}
