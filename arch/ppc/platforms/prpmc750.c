/*
 * Board setup routines for Motorola PrPMC750
 *
 * Author: Matt Porter <mporter@mvista.com>
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
#include <linux/slab.h>
#include <linux/serial_reg.h>

#include <asm/byteorder.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/uaccess.h>
#include <asm/time.h>
#include <asm/open_pic.h>
#include <asm/bootinfo.h>
#include <asm/hawk.h>

#include "prpmc750.h"

extern unsigned long loops_per_jiffy;

extern void gen550_progress(char *, unsigned short);

static u_char prpmc750_openpic_initsenses[] __initdata =
{
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_HOSTINT0 */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_UART */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_DEBUGINT */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_HAWK_WDT */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_UNUSED */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_ABORT */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_HOSTINT1 */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_HOSTINT2 */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_HOSTINT3 */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_PMC_INTA */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_PMC_INTB */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_PMC_INTC */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_PMC_INTD */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_UNUSED */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_UNUSED */
    (IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* PRPMC750_INT_UNUSED */
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

static void __init prpmc750_pcibios_fixup(void)
{
	struct pci_dev *dev;
	unsigned short wtmp;

	/*
	 * Kludge to clean up after PPC6BUG which doesn't
	 * configure the CL5446 VGA card.  Also the
	 * resource subsystem doesn't fixup the
	 * PCI mem resources on the CL5446.
	 */
	if ((dev = pci_get_device(PCI_VENDOR_ID_CIRRUS,
				   PCI_DEVICE_ID_CIRRUS_5446, 0))) {
		dev->resource[0].start += PRPMC750_PCI_PHY_MEM_OFFSET;
		dev->resource[0].end += PRPMC750_PCI_PHY_MEM_OFFSET;
		pci_read_config_word(dev, PCI_COMMAND, &wtmp);
		pci_write_config_word(dev, PCI_COMMAND, wtmp | 3);
		/* Enable Color mode in MISC reg */
		outb(0x03, 0x3c2);
		/* Select DRAM config reg */
		outb(0x0f, 0x3c4);
		/* Set proper DRAM config */
		outb(0xdf, 0x3c5);
		pci_dev_put(dev);
	}
}

void __init prpmc750_find_bridges(void)
{
	struct pci_controller *hose;

	hose = pcibios_alloc_controller();
	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;
	hose->io_base_virt = (void *)PRPMC750_ISA_IO_BASE;
	hose->pci_mem_offset = PRPMC750_PCI_PHY_MEM_OFFSET;

	pci_init_resource(&hose->io_resource,
			  PRPMC750_PCI_IO_START,
			  PRPMC750_PCI_IO_END,
			  IORESOURCE_IO, "PCI host bridge");

	pci_init_resource(&hose->mem_resources[0],
			  PRPMC750_PROC_PCI_MEM_START,
			  PRPMC750_PROC_PCI_MEM_END,
			  IORESOURCE_MEM, "PCI host bridge");

	hose->io_space.start = PRPMC750_PCI_IO_START;
	hose->io_space.end = PRPMC750_PCI_IO_END;
	hose->mem_space.start = PRPMC750_PCI_MEM_START;
	hose->mem_space.end = PRPMC750_PCI_MEM_END - HAWK_MPIC_SIZE;

	if (hawk_init(hose, PRPMC750_HAWK_PPC_REG_BASE,
		      PRPMC750_PROC_PCI_MEM_START,
		      PRPMC750_PROC_PCI_MEM_END - HAWK_MPIC_SIZE,
		      PRPMC750_PROC_PCI_IO_START, PRPMC750_PROC_PCI_IO_END,
		      PRPMC750_PROC_PCI_MEM_END - HAWK_MPIC_SIZE + 1)
	    != 0) {
		printk(KERN_CRIT "Could not initialize host bridge\n");
	}

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pcibios_fixup = prpmc750_pcibios_fixup;
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = prpmc_map_irq;
}
static int prpmc750_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "machine\t\t: PrPMC750\n");

	return 0;
}

static void __init prpmc750_setup_arch(void)
{
	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000 / HZ;

	/* Lookup PCI host bridges */
	prpmc750_find_bridges();

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

	OpenPIC_InitSenses = prpmc750_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(prpmc750_openpic_initsenses);

	printk(KERN_INFO "Port by MontaVista Software, Inc. "
			"(source@mvista.com)\n");
}

/*
 * Compute the PrPMC750's bus speed using the baud clock as a
 * reference.
 */
static unsigned long __init prpmc750_get_bus_speed(void)
{
	unsigned long tbl_start, tbl_end;
	unsigned long current_state, old_state, bus_speed;
	unsigned char lcr, dll, dlm;
	int baud_divisor, count;

	/* Read the UART's baud clock divisor */
	lcr = readb(PRPMC750_SERIAL_0_LCR);
	writeb(lcr | UART_LCR_DLAB, PRPMC750_SERIAL_0_LCR);
	dll = readb(PRPMC750_SERIAL_0_DLL);
	dlm = readb(PRPMC750_SERIAL_0_DLM);
	writeb(lcr & ~UART_LCR_DLAB, PRPMC750_SERIAL_0_LCR);
	baud_divisor = (dlm << 8) | dll;

	/*
	 * Use the baud clock divisor and base baud clock
	 * to determine the baud rate and use that as
	 * the number of baud clock edges we use for
	 * the time base sample.  Make it half the baud
	 * rate.
	 */
	count = PRPMC750_BASE_BAUD / (baud_divisor * 16);

	/* Find the first edge of the baud clock */
	old_state = readb(PRPMC750_STATUS_REG) & PRPMC750_BAUDOUT_MASK;
	do {
		current_state = readb(PRPMC750_STATUS_REG) &
		    PRPMC750_BAUDOUT_MASK;
	} while (old_state == current_state);

	old_state = current_state;

	/* Get the starting time base value */
	tbl_start = get_tbl();

	/*
	 * Loop until we have found a number of edges equal
	 * to half the count (half the baud rate)
	 */
	do {
		do {
			current_state = readb(PRPMC750_STATUS_REG) &
			    PRPMC750_BAUDOUT_MASK;
		} while (old_state == current_state);
		old_state = current_state;
	} while (--count);

	/* Get the ending time base value */
	tbl_end = get_tbl();

	/* Compute bus speed */
	bus_speed = (tbl_end - tbl_start) * 128;

	return bus_speed;
}

static void __init prpmc750_calibrate_decr(void)
{
	unsigned long freq;
	int divisor = 4;

	freq = prpmc750_get_bus_speed();

	tb_ticks_per_jiffy = freq / (HZ * divisor);
	tb_to_us = mulhwu_scale_factor(freq / divisor, 1000000);
}

static void prpmc750_restart(char *cmd)
{
	local_irq_disable();
	writeb(PRPMC750_MODRST_MASK, PRPMC750_MODRST_REG);
	while (1) ;
}

static void prpmc750_halt(void)
{
	local_irq_disable();
	while (1) ;
}

static void prpmc750_power_off(void)
{
	prpmc750_halt();
}

static void __init prpmc750_init_IRQ(void)
{
	openpic_init(0);
}

/*
 * Set BAT 3 to map 0xf0000000 to end of physical memory space.
 */
static __inline__ void prpmc750_set_bat(void)
{
	mb();
	mtspr(SPRN_DBAT1U, 0xf0001ffe);
	mtspr(SPRN_DBAT1L, 0xf000002a);
	mb();
}

/*
 * We need to read the Falcon/Hawk memory controller
 * to properly determine this value
 */
static unsigned long __init prpmc750_find_end_of_memory(void)
{
	/* Read the memory size from the Hawk SMC */
	return hawk_get_mem_size(PRPMC750_HAWK_SMC_BASE);
}

static void __init prpmc750_map_io(void)
{
	io_block_mapping(PRPMC750_ISA_IO_BASE, PRPMC750_ISA_IO_BASE,
			 0x10000000, _PAGE_IO);
#if 0
	io_block_mapping(0xf0000000, 0xc0000000, 0x08000000, _PAGE_IO);
#endif
	io_block_mapping(0xf8000000, 0xf8000000, 0x08000000, _PAGE_IO);
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	/* Cover the Hawk registers with a BAT */
	prpmc750_set_bat();

	isa_io_base = PRPMC750_ISA_IO_BASE;
	isa_mem_base = PRPMC750_ISA_MEM_BASE;
	pci_dram_offset = PRPMC750_PCI_DRAM_OFFSET;

	ppc_md.setup_arch = prpmc750_setup_arch;
	ppc_md.show_cpuinfo = prpmc750_show_cpuinfo;
	ppc_md.init_IRQ = prpmc750_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;

	ppc_md.find_end_of_memory = prpmc750_find_end_of_memory;
	ppc_md.setup_io_mappings = prpmc750_map_io;

	ppc_md.restart = prpmc750_restart;
	ppc_md.power_off = prpmc750_power_off;
	ppc_md.halt = prpmc750_halt;

	/* PrPMC750 has no timekeeper part */
	ppc_md.time_init = NULL;
	ppc_md.get_rtc_time = NULL;
	ppc_md.set_rtc_time = NULL;
	ppc_md.calibrate_decr = prpmc750_calibrate_decr;

#ifdef  CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = gen550_progress;
#endif				/* CONFIG_SERIAL_TEXT_DEBUG */
}
