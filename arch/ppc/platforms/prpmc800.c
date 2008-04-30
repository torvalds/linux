/*
 * Author: Dale Farnsworth <dale.farnsworth@mvista.com>
 *
 * 2001-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/harrier_defs.h>

#include <asm/byteorder.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/pci-bridge.h>
#include <asm/open_pic.h>
#include <asm/bootinfo.h>
#include <asm/harrier.h>

#include "prpmc800.h"

#define HARRIER_REVI_REG	(PRPMC800_HARRIER_XCSR_BASE+HARRIER_REVI_OFF)
#define HARRIER_UCTL_REG	(PRPMC800_HARRIER_XCSR_BASE+HARRIER_UCTL_OFF)
#define HARRIER_MISC_CSR_REG   (PRPMC800_HARRIER_XCSR_BASE+HARRIER_MISC_CSR_OFF)
#define HARRIER_IFEVP_REG    (PRPMC800_HARRIER_MPIC_BASE+HARRIER_MPIC_IFEVP_OFF)
#define HARRIER_IFEDE_REG    (PRPMC800_HARRIER_MPIC_BASE+HARRIER_MPIC_IFEDE_OFF)
#define HARRIER_FEEN_REG	(PRPMC800_HARRIER_XCSR_BASE+HARRIER_FEEN_OFF)
#define HARRIER_FEMA_REG	(PRPMC800_HARRIER_XCSR_BASE+HARRIER_FEMA_OFF)

#define HARRIER_VENI_REG	(PRPMC800_HARRIER_XCSR_BASE + HARRIER_VENI_OFF)
#define HARRIER_MISC_CSR	(PRPMC800_HARRIER_XCSR_BASE + \
				 HARRIER_MISC_CSR_OFF)

#define MONARCH	(monarch != 0)
#define NON_MONARCH (monarch == 0)

extern int mpic_init(void);
extern unsigned long loops_per_jiffy;
extern void gen550_progress(char *, unsigned short);

static int monarch = 0;
static int found_self = 0;
static int self = 0;

static u_char prpmc800_openpic_initsenses[] __initdata =
{
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_HOSTINT0 */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_UNUSED */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_DEBUGINT */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_HARRIER_WDT */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_UNUSED */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_UNUSED */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_HOSTINT1 */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_HOSTINT2 */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_HOSTINT3 */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_PMC_INTA */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_PMC_INTB */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_PMC_INTC */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_PMC_INTD */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_UNUSED */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_UNUSED */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_UNUSED */
   (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC800_INT_HARRIER_INT (UARTS, ABORT, DMA) */
};

/*
 * Motorola PrPMC750/PrPMC800 in PrPMCBASE or PrPMC-Carrier
 * Combined irq tables.  Only Base has IDSEL 14, only Carrier has 21 and 22.
 */
static inline int
prpmc_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *      PCI IDSEL/INTPIN->INTLINE
	 *      A       B       C       D
	 */
	{
		{12,	0,	0,	0},  /* IDSEL 14 - Ethernet, base */
		{0,	0,	0,	0},  /* IDSEL 15 - unused */
		{10,	11,	12,	9},  /* IDSEL 16 - PMC A1, PMC1 */
		{10,	11,	12,	9},  /* IDSEL 17 - PrPMC-A-B, PMC2-B */
		{11,	12,	9,	10}, /* IDSEL 18 - PMC A1-B, PMC1-B */
		{0,	0,	0,	0},  /* IDSEL 19 - unused */
		{9,	10,	11,	12}, /* IDSEL 20 - P2P Bridge */
		{11,	12,	9,	10}, /* IDSEL 21 - PMC A2, carrier */
		{12,	9,	10,	11}, /* IDSEL 22 - PMC A2-B, carrier */
	};
	const long min_idsel = 14, max_idsel = 22, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
};

static int
prpmc_read_config_dword(struct pci_controller *hose, u8 bus, u8 devfn,
			int offset, u32 * val)
{
	/* paranoia */
	if ((hose == NULL) ||
	    (hose->cfg_addr == NULL) || (hose->cfg_data == NULL))
		return PCIBIOS_DEVICE_NOT_FOUND;

	out_be32(hose->cfg_addr, ((offset & 0xfc) << 24) | (devfn << 16)
		 | ((bus - hose->bus_offset) << 8) | 0x80);
	*val = in_le32((u32 *) (hose->cfg_data + (offset & 3)));

	return PCIBIOS_SUCCESSFUL;
}

#define HARRIER_PCI_VEND_DEV_ID	(PCI_VENDOR_ID_MOTOROLA | \
				 (PCI_DEVICE_ID_MOTOROLA_HARRIER << 16))
static int prpmc_self(u8 bus, u8 devfn)
{
	/*
	 * Harriers always view themselves as being on bus 0. If we're not
	 * looking at bus 0, we're not going to find ourselves.
	 */
	if (bus != 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	else {
		int result;
		int val;
		struct pci_controller *hose;

		hose = pci_bus_to_hose(bus);

		/* See if target device is a Harrier */
		result = prpmc_read_config_dword(hose, bus, devfn,
						 PCI_VENDOR_ID, &val);
		if ((result != PCIBIOS_SUCCESSFUL) ||
		    (val != HARRIER_PCI_VEND_DEV_ID))
			return PCIBIOS_DEVICE_NOT_FOUND;

		/*
		 * LBA bit is set if target Harrier == initiating Harrier
		 * (i.e. if we are reading our own PCI header).
		 */
		result = prpmc_read_config_dword(hose, bus, devfn,
						 HARRIER_LBA_OFF, &val);
		if ((result != PCIBIOS_SUCCESSFUL) ||
		    ((val & HARRIER_LBA_MSK) != HARRIER_LBA_MSK))
			return PCIBIOS_DEVICE_NOT_FOUND;

		/* It's us, save our location for later */
		self = devfn;
		found_self = 1;
		return PCIBIOS_SUCCESSFUL;
	}
}

static int prpmc_exclude_device(u8 bus, u8 devfn)
{
	/*
	 * Monarch is allowed to access all PCI devices. Non-monarch is
	 * only allowed to access its own Harrier.
	 */

	if (MONARCH)
		return PCIBIOS_SUCCESSFUL;
	if (found_self)
		if ((bus == 0) && (devfn == self))
			return PCIBIOS_SUCCESSFUL;
		else
			return PCIBIOS_DEVICE_NOT_FOUND;
	else
		return prpmc_self(bus, devfn);
}

void __init prpmc800_find_bridges(void)
{
	struct pci_controller *hose;
	int host_bridge;

	hose = pcibios_alloc_controller();
	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;

	ppc_md.pci_exclude_device = prpmc_exclude_device;
	ppc_md.pcibios_fixup = NULL;
	ppc_md.pcibios_fixup_bus = NULL;
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = prpmc_map_irq;

	setup_indirect_pci(hose,
			   PRPMC800_PCI_CONFIG_ADDR, PRPMC800_PCI_CONFIG_DATA);

	/* Get host bridge vendor/dev id */

	host_bridge = in_be32((uint *) (HARRIER_VENI_REG));

	if (host_bridge != HARRIER_VEND_DEV_ID) {
		printk(KERN_CRIT "Host bridge 0x%x not supported\n",
				host_bridge);
		return;
	}

	monarch = in_be32((uint *) HARRIER_MISC_CSR) & HARRIER_SYSCON;

	printk(KERN_INFO "Running as %s.\n",
			MONARCH ? "Monarch" : "Non-Monarch");

	hose->io_space.start = PRPMC800_PCI_IO_START;
	hose->io_space.end = PRPMC800_PCI_IO_END;
	hose->io_base_virt = (void *)PRPMC800_ISA_IO_BASE;
	hose->pci_mem_offset = PRPMC800_PCI_PHY_MEM_OFFSET;

	pci_init_resource(&hose->io_resource,
			  PRPMC800_PCI_IO_START, PRPMC800_PCI_IO_END,
			  IORESOURCE_IO, "PCI host bridge");

	if (MONARCH) {
		hose->mem_space.start = PRPMC800_PCI_MEM_START;
		hose->mem_space.end = PRPMC800_PCI_MEM_END;

		pci_init_resource(&hose->mem_resources[0],
				  PRPMC800_PCI_MEM_START,
				  PRPMC800_PCI_MEM_END,
				  IORESOURCE_MEM, "PCI host bridge");

		if (harrier_init(hose,
				 PRPMC800_HARRIER_XCSR_BASE,
				 PRPMC800_PROC_PCI_MEM_START,
				 PRPMC800_PROC_PCI_MEM_END,
				 PRPMC800_PROC_PCI_IO_START,
				 PRPMC800_PROC_PCI_IO_END,
				 PRPMC800_HARRIER_MPIC_BASE) != 0)
			printk(KERN_CRIT "Could not initialize HARRIER "
					 "bridge\n");

		harrier_release_eready(PRPMC800_HARRIER_XCSR_BASE);
		harrier_wait_eready(PRPMC800_HARRIER_XCSR_BASE);
		hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	} else {
		pci_init_resource(&hose->mem_resources[0],
				  PRPMC800_NM_PCI_MEM_START,
				  PRPMC800_NM_PCI_MEM_END,
				  IORESOURCE_MEM, "PCI host bridge");

		hose->mem_space.start = PRPMC800_NM_PCI_MEM_START;
		hose->mem_space.end = PRPMC800_NM_PCI_MEM_END;

		if (harrier_init(hose,
				 PRPMC800_HARRIER_XCSR_BASE,
				 PRPMC800_NM_PROC_PCI_MEM_START,
				 PRPMC800_NM_PROC_PCI_MEM_END,
				 PRPMC800_PROC_PCI_IO_START,
				 PRPMC800_PROC_PCI_IO_END,
				 PRPMC800_HARRIER_MPIC_BASE) != 0)
			printk(KERN_CRIT "Could not initialize HARRIER "
					 "bridge\n");

		harrier_setup_nonmonarch(PRPMC800_HARRIER_XCSR_BASE,
					 HARRIER_ITSZ_1MB);
		harrier_release_eready(PRPMC800_HARRIER_XCSR_BASE);
	}
}

static int prpmc800_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "machine\t\t: PrPMC800\n");

	return 0;
}

static void __init prpmc800_setup_arch(void)
{
	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000 / HZ;

	/* Lookup PCI host bridges */
	prpmc800_find_bridges();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA2;
#endif

	printk(KERN_INFO "Port by MontaVista Software, Inc. "
			 "(source@mvista.com)\n");
}

/*
 * Compute the PrPMC800's tbl frequency using the baud clock as a reference.
 */
static void __init prpmc800_calibrate_decr(void)
{
	unsigned long tbl_start, tbl_end;
	unsigned long current_state, old_state, tb_ticks_per_second;
	unsigned int count;
	unsigned int harrier_revision;

	harrier_revision = readb(HARRIER_REVI_REG);
	if (harrier_revision < 2) {
		/* XTAL64 was broken in harrier revision 1 */
		printk(KERN_INFO "time_init: Harrier revision %d, assuming "
				 "100 Mhz bus\n", harrier_revision);
		tb_ticks_per_second = 100000000 / 4;
		tb_ticks_per_jiffy = tb_ticks_per_second / HZ;
		tb_to_us = mulhwu_scale_factor(tb_ticks_per_second, 1000000);
		return;
	}

	/*
	 * The XTAL64 bit oscillates at the 1/64 the base baud clock
	 * Set count to XTAL64 cycles per second.  Since we'll count
	 * half-cycles, we'll reach the count in half a second.
	 */
	count = PRPMC800_BASE_BAUD / 64;

	/* Find the first edge of the baud clock */
	old_state = readb(HARRIER_UCTL_REG) & HARRIER_XTAL64_MASK;
	do {
		current_state = readb(HARRIER_UCTL_REG) & HARRIER_XTAL64_MASK;
	} while (old_state == current_state);

	old_state = current_state;

	/* Get the starting time base value */
	tbl_start = get_tbl();

	/*
	 * Loop until we have found a number of edges (half-cycles)
	 * equal to the count (half a second)
	 */
	do {
		do {
			current_state = readb(HARRIER_UCTL_REG) &
			    HARRIER_XTAL64_MASK;
		} while (old_state == current_state);
		old_state = current_state;
	} while (--count);

	/* Get the ending time base value */
	tbl_end = get_tbl();

	/* We only counted for half a second, so double to get ticks/second */
	tb_ticks_per_second = (tbl_end - tbl_start) * 2;
	tb_ticks_per_jiffy = tb_ticks_per_second / HZ;
	tb_to_us = mulhwu_scale_factor(tb_ticks_per_second, 1000000);
}

static void prpmc800_restart(char *cmd)
{
	ulong temp;

	local_irq_disable();
	temp = in_be32((uint *) HARRIER_MISC_CSR_REG);
	temp |= HARRIER_RSTOUT;
	out_be32((uint *) HARRIER_MISC_CSR_REG, temp);
	while (1) ;
}

static void prpmc800_halt(void)
{
	local_irq_disable();
	while (1) ;
}

static void prpmc800_power_off(void)
{
	prpmc800_halt();
}

static void __init prpmc800_init_IRQ(void)
{
	OpenPIC_InitSenses = prpmc800_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(prpmc800_openpic_initsenses);

	/* Setup external interrupt sources. */
	openpic_set_sources(0, 16, OpenPIC_Addr + 0x10000);
	/* Setup internal UART interrupt source. */
	openpic_set_sources(16, 1, OpenPIC_Addr + 0x10200);

	/* Do the MPIC initialization based on the above settings. */
	openpic_init(0);

	/* enable functional exceptions for uarts and abort */
	out_8((u8 *) HARRIER_FEEN_REG, (HARRIER_FE_UA0 | HARRIER_FE_UA1));
	out_8((u8 *) HARRIER_FEMA_REG, ~(HARRIER_FE_UA0 | HARRIER_FE_UA1));
}

/*
 * Set BAT 3 to map 0xf0000000 to end of physical memory space.
 */
static __inline__ void prpmc800_set_bat(void)
{
	mb();
	mtspr(SPRN_DBAT1U, 0xf0001ffe);
	mtspr(SPRN_DBAT1L, 0xf000002a);
	mb();
}

/*
 * We need to read the Harrier memory controller
 * to properly determine this value
 */
static unsigned long __init prpmc800_find_end_of_memory(void)
{
	/* Read the memory size from the Harrier XCSR */
	return harrier_get_mem_size(PRPMC800_HARRIER_XCSR_BASE);
}

static void __init prpmc800_map_io(void)
{
	io_block_mapping(0x80000000, 0x80000000, 0x10000000, _PAGE_IO);
	io_block_mapping(0xf0000000, 0xf0000000, 0x10000000, _PAGE_IO);
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	prpmc800_set_bat();

	isa_io_base = PRPMC800_ISA_IO_BASE;
	isa_mem_base = PRPMC800_ISA_MEM_BASE;
	pci_dram_offset = PRPMC800_PCI_DRAM_OFFSET;

	ppc_md.setup_arch = prpmc800_setup_arch;
	ppc_md.show_cpuinfo = prpmc800_show_cpuinfo;
	ppc_md.init_IRQ = prpmc800_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;

	ppc_md.find_end_of_memory = prpmc800_find_end_of_memory;
	ppc_md.setup_io_mappings = prpmc800_map_io;

	ppc_md.restart = prpmc800_restart;
	ppc_md.power_off = prpmc800_power_off;
	ppc_md.halt = prpmc800_halt;

	/* PrPMC800 has no timekeeper part */
	ppc_md.time_init = NULL;
	ppc_md.get_rtc_time = NULL;
	ppc_md.set_rtc_time = NULL;
	ppc_md.calibrate_decr = prpmc800_calibrate_decr;
#ifdef  CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = gen550_progress;
#else				/* !CONFIG_SERIAL_TEXT_DEBUG */
	ppc_md.progress = NULL;
#endif				/* CONFIG_SERIAL_TEXT_DEBUG */
}
