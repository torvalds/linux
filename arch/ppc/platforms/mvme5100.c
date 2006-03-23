/*
 * Board setup routines for the Motorola MVME5100.
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * 2001-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/seq_file.h>
#include <linux/kdev_t.h>
#include <linux/root_dev.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/open_pic.h>
#include <asm/i8259.h>
#include <asm/todc.h>
#include <asm/pci-bridge.h>
#include <asm/bootinfo.h>
#include <asm/hawk.h>

#include <platforms/pplus.h>
#include <platforms/mvme5100.h>

static u_char mvme5100_openpic_initsenses[16] __initdata = {
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE), /* i8259 cascade */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* TL16C550 UART 1,2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* Enet1 front panel or P2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* Hawk Watchdog 1,2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* DS1621 thermal alarm */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* Universe II LINT0# */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* Universe II LINT1# */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* Universe II LINT2# */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* Universe II LINT3# */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* PMC1 INTA#, PMC2 INTB# */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* PMC1 INTB#, PMC2 INTC# */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* PMC1 INTC#, PMC2 INTD# */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* PMC1 INTD#, PMC2 INTA# */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* Enet 2 (front panel) */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* Abort Switch */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* RTC Alarm */
};

static inline int
mvme5100_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	int irq;

	static char pci_irq_table[][4] =
	/*
	 *	PCI IDSEL/INTPIN->INTLINE
	 * 	   A   B   C   D
	 */
	{
		{  0,  0,  0,  0 },	/* IDSEL 11 - Winbond */
		{  0,  0,  0,  0 },	/* IDSEL 12 - unused */
		{ 21, 22, 23, 24 },	/* IDSEL 13 - Universe II */
		{ 18,  0,  0,  0 },	/* IDSEL 14 - Enet 1 */
		{  0,  0,  0,  0 },	/* IDSEL 15 - unused */
		{ 25, 26, 27, 28 },	/* IDSEL 16 - PMC Slot 1 */
		{ 28, 25, 26, 27 },	/* IDSEL 17 - PMC Slot 2 */
		{  0,  0,  0,  0 },	/* IDSEL 18 - unused */
		{ 29,  0,  0,  0 },	/* IDSEL 19 - Enet 2 */
		{  0,  0,  0,  0 },	/* IDSEL 20 - PMCSPAN */
	};

	const long min_idsel = 11, max_idsel = 20, irqs_per_slot = 4;
	irq = PCI_IRQ_TABLE_LOOKUP;
	/* If lookup is zero, always return 0 */
	if (!irq)
		return 0;
	else
#ifdef CONFIG_MVME5100_IPMC761_PRESENT
	/* If IPMC761 present, return table value */
	return irq;
#else
	/* If IPMC761 not present, we don't have an i8259 so adjust */
	return (irq - NUM_8259_INTERRUPTS);
#endif
}

static void
mvme5100_pcibios_fixup_resources(struct pci_dev *dev)
{
	int i;

	if ((dev->vendor == PCI_VENDOR_ID_MOTOROLA) &&
			(dev->device == PCI_DEVICE_ID_MOTOROLA_HAWK))
		for (i=0; i<DEVICE_COUNT_RESOURCE; i++)
		{
			dev->resource[i].start = 0;
			dev->resource[i].end = 0;
		}
}

static void __init
mvme5100_setup_bridge(void)
{
	struct pci_controller*	hose;

	hose = pcibios_alloc_controller();

	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;
	hose->pci_mem_offset = MVME5100_PCI_MEM_OFFSET;

	pci_init_resource(&hose->io_resource, MVME5100_PCI_LOWER_IO,
			MVME5100_PCI_UPPER_IO, IORESOURCE_IO,
			"PCI host bridge");

	pci_init_resource(&hose->mem_resources[0], MVME5100_PCI_LOWER_MEM,
			MVME5100_PCI_UPPER_MEM, IORESOURCE_MEM,
			"PCI host bridge");

	hose->io_space.start = MVME5100_PCI_LOWER_IO;
	hose->io_space.end = MVME5100_PCI_UPPER_IO;
	hose->mem_space.start = MVME5100_PCI_LOWER_MEM;
	hose->mem_space.end = MVME5100_PCI_UPPER_MEM;
	hose->io_base_virt = (void *)MVME5100_ISA_IO_BASE;

	/* Use indirect method of Hawk */
	setup_indirect_pci(hose, MVME5100_PCI_CONFIG_ADDR,
			MVME5100_PCI_CONFIG_DATA);

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pcibios_fixup_resources = mvme5100_pcibios_fixup_resources;
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = mvme5100_map_irq;
}

static void __init
mvme5100_setup_arch(void)
{
	if ( ppc_md.progress )
		ppc_md.progress("mvme5100_setup_arch: enter", 0);

	loops_per_jiffy = 50000000 / HZ;

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef	CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA2;
#endif

	if ( ppc_md.progress )
		ppc_md.progress("mvme5100_setup_arch: find_bridges", 0);

	/* Setup PCI host bridge */
	mvme5100_setup_bridge();

	/* Find and map our OpenPIC */
	hawk_mpic_init(MVME5100_PCI_MEM_OFFSET);
	OpenPIC_InitSenses = mvme5100_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(mvme5100_openpic_initsenses);

	printk("MVME5100 port (C) 2001 MontaVista Software, Inc. (source@mvista.com)\n");

	if ( ppc_md.progress )
		ppc_md.progress("mvme5100_setup_arch: exit", 0);

	return;
}

static void __init
mvme5100_init2(void)
{
#ifdef CONFIG_MVME5100_IPMC761_PRESENT
		request_region(0x00,0x20,"dma1");
		request_region(0x20,0x20,"pic1");
		request_region(0x40,0x20,"timer");
		request_region(0x80,0x10,"dma page reg");
		request_region(0xa0,0x20,"pic2");
		request_region(0xc0,0x20,"dma2");
#endif
	return;
}

/*
 * Interrupt setup and service.
 * Have MPIC on HAWK and cascaded 8259s on Winbond cascaded to MPIC.
 */
static void __init
mvme5100_init_IRQ(void)
{
#ifdef CONFIG_MVME5100_IPMC761_PRESENT
	int i;
#endif

	if ( ppc_md.progress )
		ppc_md.progress("init_irq: enter", 0);

	openpic_set_sources(0, 16, OpenPIC_Addr + 0x10000);
#ifdef CONFIG_MVME5100_IPMC761_PRESENT
	openpic_init(NUM_8259_INTERRUPTS);
	openpic_hookup_cascade(NUM_8259_INTERRUPTS, "82c59 cascade",
			&i8259_irq);

	i8259_init(0, 0);
#else
	openpic_init(0);
#endif

	if ( ppc_md.progress )
		ppc_md.progress("init_irq: exit", 0);

	return;
}

/*
 * Set BAT 3 to map 0xf0000000 to end of physical memory space.
 */
static __inline__ void
mvme5100_set_bat(void)
{
	mb();
	mtspr(SPRN_DBAT1U, 0xf0001ffe);
	mtspr(SPRN_DBAT1L, 0xf000002a);
	mb();
}

static unsigned long __init
mvme5100_find_end_of_memory(void)
{
	return hawk_get_mem_size(MVME5100_HAWK_SMC_BASE);
}

static void __init
mvme5100_map_io(void)
{
	io_block_mapping(0xfe000000, 0xfe000000, 0x02000000, _PAGE_IO);
	ioremap_base = 0xfe000000;
}

static void
mvme5100_reset_board(void)
{
	local_irq_disable();

	/* Set exception prefix high - to the firmware */
	_nmask_and_or_msr(0, MSR_IP);

	out_8((u_char *)MVME5100_BOARD_MODRST_REG, 0x01);

	return;
}

static void
mvme5100_restart(char *cmd)
{
	volatile ulong i = 10000000;

	mvme5100_reset_board();

	while (i-- > 0);
	panic("restart failed\n");
}

static void
mvme5100_halt(void)
{
	local_irq_disable();
	while (1);
}

static void
mvme5100_power_off(void)
{
	mvme5100_halt();
}

static int
mvme5100_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: Motorola\n");
	seq_printf(m, "machine\t\t: MVME5100\n");

	return 0;
}

TODC_ALLOC();

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());
	mvme5100_set_bat();

	isa_io_base = MVME5100_ISA_IO_BASE;
	isa_mem_base = MVME5100_ISA_MEM_BASE;
	pci_dram_offset = MVME5100_PCI_DRAM_OFFSET;

	ppc_md.setup_arch = mvme5100_setup_arch;
	ppc_md.show_cpuinfo = mvme5100_show_cpuinfo;
	ppc_md.init_IRQ = mvme5100_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;
	ppc_md.init = mvme5100_init2;

	ppc_md.restart = mvme5100_restart;
	ppc_md.power_off = mvme5100_power_off;
	ppc_md.halt = mvme5100_halt;

	ppc_md.find_end_of_memory = mvme5100_find_end_of_memory;
	ppc_md.setup_io_mappings = mvme5100_map_io;

	TODC_INIT(TODC_TYPE_MK48T37, MVME5100_NVRAM_AS0, MVME5100_NVRAM_AS1,
			MVME5100_NVRAM_DATA, 8);

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.calibrate_decr = todc_calibrate_decr;

	ppc_md.nvram_read_val = todc_m48txx_read_val;
	ppc_md.nvram_write_val = todc_m48txx_write_val;
}
