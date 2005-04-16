/*
 * arch/ppc/platforms/powerpmc250.c
 *
 * Board setup routines for Force PowerPMC-250 Processor PMC
 *
 * Author: Troy Benjegerdes <tbenjegerdes@mvista.com>
 * Borrowed heavily from prpmc750_*.c by
 * 	Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
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
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/ide.h>
#include <linux/root_dev.h>

#include <asm/byteorder.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <platforms/powerpmc250.h>
#include <asm/open_pic.h>
#include <asm/pci-bridge.h>
#include <asm/mpc10x.h>
#include <asm/uaccess.h>
#include <asm/bootinfo.h>

extern void powerpmc250_find_bridges(void);
extern unsigned long loops_per_jiffy;

static u_char powerpmc250_openpic_initsenses[] __initdata =
{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1,	/* PMC INTA (also MPC107 output interrupt INTA) */
    1,	/* PMC INTB (also I82559 Ethernet controller) */
    1,	/* PMC INTC */
    1,	/* PMC INTD */
    0,	/* DUART interrupt (active high) */
};

static int
powerpmc250_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m,"machine\t\t: Force PowerPMC250\n");

	return 0;
}

static void __init
powerpmc250_setup_arch(void)
{
	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000/HZ;

	/* Lookup PCI host bridges */
	powerpmc250_find_bridges();

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

	printk("Force PowerPMC250 port (C) 2001 MontaVista Software, Inc. (source@mvista.com)\n");
}

#if 0
/*
 * Compute the PrPMC750's bus speed using the baud clock as a
 * reference.
 */
unsigned long __init powerpmc250_get_bus_speed(void)
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
	} while(old_state == current_state);

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
		} while(old_state == current_state);
		old_state = current_state;
	} while (--count);

	/* Get the ending time base value */
	tbl_end = get_tbl();

	/* Compute bus speed */
	bus_speed = (tbl_end-tbl_start)*128;

	return bus_speed;
}
#endif

static void __init
powerpmc250_calibrate_decr(void)
{
	unsigned long freq;
	int divisor = 4;

	//freq = powerpmc250_get_bus_speed();
#warning hardcoded bus freq
	freq = 100000000;

	tb_ticks_per_jiffy = freq / (HZ * divisor);
	tb_to_us = mulhwu_scale_factor(freq/divisor, 1000000);
}

static void
powerpmc250_restart(char *cmd)
{
	local_irq_disable();
	/* Hard reset */
	writeb(0x11, 0xfe000332);
	while(1);
}

static void
powerpmc250_halt(void)
{
	local_irq_disable();
	while (1);
}

static void
powerpmc250_power_off(void)
{
	powerpmc250_halt();
}

static void __init
powerpmc250_init_IRQ(void)
{

	OpenPIC_InitSenses = powerpmc250_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(powerpmc250_openpic_initsenses);
	mpc10x_set_openpic();
}

/*
 * Set BAT 3 to map 0xf0000000 to end of physical memory space.
 */
static __inline__ void
powerpmc250_set_bat(void)
{
	unsigned long   bat3u, bat3l;
	static int	mapping_set = 0;

	if (!mapping_set)
	{
		__asm__ __volatile__(
				" lis %0,0xf000\n \
				ori %1,%0,0x002a\n \
				ori %0,%0,0x1ffe\n \
				mtspr 0x21e,%0\n \
				mtspr 0x21f,%1\n \
				isync\n \
				sync "
				: "=r" (bat3u), "=r" (bat3l));

		mapping_set = 1;
	}
	return;
}

static unsigned long __init
powerpmc250_find_end_of_memory(void)
{
	/* Cover I/O space with a BAT */
	/* yuck, better hope your ram size is a power of 2  -- paulus */
	powerpmc250_set_bat();

	return mpc10x_get_mem_size(MPC10X_MEM_MAP_B);
}

static void __init
powerpmc250_map_io(void)
{
	io_block_mapping(0xfe000000, 0xfe000000, 0x02000000, _PAGE_IO);
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

#ifdef CONFIG_BLK_DEV_INITRD
	if ( r4 )
	{
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif

	/* Copy cmd_line parameters */
	if ( r6)
	{
		*(char *)(r7 + KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6 + KERNELBASE));
	}

	isa_io_base = MPC10X_MAPB_ISA_IO_BASE;
	isa_mem_base = MPC10X_MAPB_ISA_MEM_BASE;
	pci_dram_offset = MPC10X_MAPB_DRAM_OFFSET;

	ppc_md.setup_arch	= powerpmc250_setup_arch;
	ppc_md.show_cpuinfo	= powerpmc250_show_cpuinfo;
	ppc_md.init_IRQ		= powerpmc250_init_IRQ;
	ppc_md.get_irq		= openpic_get_irq;

	ppc_md.find_end_of_memory = powerpmc250_find_end_of_memory;
	ppc_md.setup_io_mappings = powerpmc250_map_io;

	ppc_md.restart		= powerpmc250_restart;
	ppc_md.power_off	= powerpmc250_power_off;
	ppc_md.halt		= powerpmc250_halt;

	/* PowerPMC250 has no timekeeper part */
	ppc_md.time_init	= NULL;
	ppc_md.get_rtc_time	= NULL;
	ppc_md.set_rtc_time	= NULL;
	ppc_md.calibrate_decr	= powerpmc250_calibrate_decr;
}


/*
 * (This used to be arch/ppc/platforms/powerpmc250_pci.c)
 *
 * PCI support for Force PowerPMC250
 *
 */

#undef DEBUG
#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif /* DEBUG */

static inline int __init
powerpmc250_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *      PCI IDSEL/INTPIN->INTLINE
	 *      A       B       C       D
	 */
	{
		{17,	0,	0,	0},	/* Device 11 - 82559 */
		{0,	0,	0,	0},	/* 12 */
		{0,	0,	0,	0},	/* 13 */
		{0,	0,	0,	0},	/* 14 */
		{0,	0,	0,	0},	/* 15 */
		{16,	17,	18,	19},	/* Device 16 - PMC A1?? */
		};
	const long min_idsel = 11, max_idsel = 16, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
};

static int
powerpmc250_exclude_device(u_char bus, u_char devfn)
{
	/*
	 * While doing PCI Scan  the MPC107 will 'detect' itself as
	 * device on the PCI Bus, will create an incorrect response and
	 * later will respond incorrectly to Configuration read coming
	 * from another device.
	 *
	 * The work around is that when doing a PCI Scan one
	 * should skip its own device number in the scan.
	 *
	 * The top IDsel is AD13 and the middle is AD14.
	 *
	 * -- Note from force
	 */

	if ((bus == 0) && (PCI_SLOT(devfn) == 13 || PCI_SLOT(devfn) == 14)) {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	else {
		return PCIBIOS_SUCCESSFUL;
	}
}

void __init
powerpmc250_find_bridges(void)
{
	struct pci_controller* hose;

	hose = pcibios_alloc_controller();
	if (!hose){
		printk("Can't allocate PCI 'hose' structure!!!\n");
		return;
	}

	hose->first_busno = 0;
	hose->last_busno = 0xff;

	if (mpc10x_bridge_init(hose,
			MPC10X_MEM_MAP_B,
			MPC10X_MEM_MAP_B,
			MPC10X_MAPB_EUMB_BASE) == 0) {

		hose->mem_resources[0].end = 0xffffffff;

		hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

		/* ppc_md.pcibios_fixup = pcore_pcibios_fixup; */
		ppc_md.pci_swizzle = common_swizzle;

		ppc_md.pci_exclude_device = powerpmc250_exclude_device;
		ppc_md.pci_map_irq = powerpmc250_map_irq;
	} else {
		if (ppc_md.progress)
			ppc_md.progress("Bridge init failed", 0x100);
		printk("Host bridge init failed\n");
	}

}
