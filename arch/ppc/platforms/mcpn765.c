/*
 * arch/ppc/platforms/mcpn765.c
 *
 * Board setup routines for the Motorola MCG MCPN765 cPCI Board.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * Modified by Randy Vinson (rvinson@mvista.com)
 *
 * 2001-2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * This file adds support for the Motorola MCG MCPN765.
 */
#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/serial.h>
#include <linux/tty.h>	/* for linux/serial_core.h */
#include <linux/serial_core.h>
#include <linux/slab.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/time.h>
#include <asm/dma.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/open_pic.h>
#include <asm/i8259.h>
#include <asm/todc.h>
#include <asm/pci-bridge.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/bootinfo.h>
#include <asm/hawk.h>
#include <asm/kgdb.h>

#include "mcpn765.h"

static u_char mcpn765_openpic_initsenses[] __initdata = {
	(IRQ_SENSE_EDGE  | IRQ_POLARITY_POSITIVE),/* 16: i8259 cascade */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 17: COM1,2,3,4 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 18: Enet 1 (front) */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 19: HAWK WDT XXXX */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 20: 21554 bridge */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 21: cPCI INTA# */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 22: cPCI INTB# */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 23: cPCI INTC# */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 24: cPCI INTD# */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 25: PMC1 INTA#,PMC2 INTB#*/
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 26: PMC1 INTB#,PMC2 INTC#*/
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 27: PMC1 INTC#,PMC2 INTD#*/
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 28: PMC1 INTD#,PMC2 INTA#*/
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 29: Enet 2 (J3) */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 30: Abort Switch */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/* 31: RTC Alarm */
};

extern void mcpn765_set_VIA_IDE_native(void);

extern u_int openpic_irq(void);
extern char cmd_line[];

extern void gen550_progress(char *, unsigned short);
extern void gen550_init(int, struct uart_port *);

int use_of_interrupt_tree = 0;

static void mcpn765_halt(void);

TODC_ALLOC();

/*
 * Motorola MCG MCPN765 interrupt routing.
 */
static inline int
mcpn765_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *	PCI IDSEL/INTPIN->INTLINE
	 * 	   A   B   C   D
	 */
	{
		{ 14,  0,  0,  0 },	/* IDSEL 11 - have to manually set */
		{  0,  0,  0,  0 },	/* IDSEL 12 - unused */
		{  0,  0,  0,  0 },	/* IDSEL 13 - unused */
		{ 18,  0,  0,  0 },	/* IDSEL 14 - Enet 0 */
		{  0,  0,  0,  0 },	/* IDSEL 15 - unused */
		{ 25, 26, 27, 28 },	/* IDSEL 16 - PMC Slot 1 */
		{ 28, 25, 26, 27 },	/* IDSEL 17 - PMC Slot 2 */
		{  0,  0,  0,  0 },	/* IDSEL 18 - PMC 2B Connector XXXX */
		{ 29,  0,  0,  0 },	/* IDSEL 19 - Enet 1 */
		{ 20,  0,  0,  0 },	/* IDSEL 20 - 21554 cPCI bridge */
	};

	const long min_idsel = 11, max_idsel = 20, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

void __init
mcpn765_set_VIA_IDE_legacy(void)
{
	unsigned short vend, dev;

	early_read_config_word(0, 0, PCI_DEVFN(0xb, 1), PCI_VENDOR_ID, &vend);
	early_read_config_word(0, 0, PCI_DEVFN(0xb, 1), PCI_DEVICE_ID, &dev);

	if ((vend == PCI_VENDOR_ID_VIA) &&
	    (dev == PCI_DEVICE_ID_VIA_82C586_1)) {

		unsigned char temp;

		/* put back original "standard" port base addresses */
		early_write_config_dword(0, 0, PCI_DEVFN(0xb, 1),
				         PCI_BASE_ADDRESS_0, 0x1f1);
		early_write_config_dword(0, 0, PCI_DEVFN(0xb, 1),
				         PCI_BASE_ADDRESS_1, 0x3f5);
		early_write_config_dword(0, 0, PCI_DEVFN(0xb, 1),
				         PCI_BASE_ADDRESS_2, 0x171);
		early_write_config_dword(0, 0, PCI_DEVFN(0xb, 1),
				         PCI_BASE_ADDRESS_3, 0x375);
		early_write_config_dword(0, 0, PCI_DEVFN(0xb, 1),
				         PCI_BASE_ADDRESS_4, 0xcc01);

		/* put into legacy mode */
		early_read_config_byte(0, 0, PCI_DEVFN(0xb, 1), PCI_CLASS_PROG,
				       &temp);
		temp &= ~0x05;
		early_write_config_byte(0, 0, PCI_DEVFN(0xb, 1), PCI_CLASS_PROG,
					temp);
	}
}

void
mcpn765_set_VIA_IDE_native(void)
{
	unsigned short vend, dev;

	early_read_config_word(0, 0, PCI_DEVFN(0xb, 1), PCI_VENDOR_ID, &vend);
	early_read_config_word(0, 0, PCI_DEVFN(0xb, 1), PCI_DEVICE_ID, &dev);

	if ((vend == PCI_VENDOR_ID_VIA) &&
	    (dev == PCI_DEVICE_ID_VIA_82C586_1)) {

		unsigned char temp;

		/* put into native mode */
		early_read_config_byte(0, 0, PCI_DEVFN(0xb, 1), PCI_CLASS_PROG,
				       &temp);
		temp |= 0x05;
		early_write_config_byte(0, 0, PCI_DEVFN(0xb, 1), PCI_CLASS_PROG,
					temp);
	}
}

/*
 * Initialize the VIA 82c586b.
 */
static void __init
mcpn765_setup_via_82c586b(void)
{
	struct pci_dev	*dev;
	u_char		c;

	if ((dev = pci_get_device(PCI_VENDOR_ID_VIA,
				   PCI_DEVICE_ID_VIA_82C586_0,
				   NULL)) == NULL) {
		printk("No VIA ISA bridge found\n");
		mcpn765_halt();
		/* NOTREACHED */
	}

	/*
	 * If the firmware left the EISA 4d0/4d1 ports enabled, make sure
	 * IRQ 14 is set for edge.
	 */
	pci_read_config_byte(dev, 0x47, &c);

	if (c & (1<<5)) {
		c = inb(0x4d1);
		c &= ~(1<<6);
		outb(c, 0x4d1);
	}

	/* Disable PNP IRQ routing since we use the Hawk's MPIC */
	pci_write_config_dword(dev, 0x54, 0);
	pci_write_config_byte(dev, 0x58, 0);

	pci_dev_put(dev);
	if ((dev = pci_get_device(PCI_VENDOR_ID_VIA,
				   PCI_DEVICE_ID_VIA_82C586_1,
				   NULL)) == NULL) {
		printk("No VIA ISA bridge found\n");
		mcpn765_halt();
		/* NOTREACHED */
	}

	/*
	 * PPCBug doesn't set the enable bits for the IDE device.
	 * Turn them on now.
	 */
	pci_read_config_byte(dev, 0x40, &c);
	c |= 0x03;
	pci_write_config_byte(dev, 0x40, c);
	pci_dev_put(dev);

	return;
}

void __init
mcpn765_pcibios_fixup(void)
{
	/* Do MCPN765 board specific initialization.  */
	mcpn765_setup_via_82c586b();
}

void __init
mcpn765_find_bridges(void)
{
	struct pci_controller	*hose;

	hose = pcibios_alloc_controller();

	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;
	hose->pci_mem_offset = MCPN765_PCI_PHY_MEM_OFFSET;

	pci_init_resource(&hose->io_resource,
			MCPN765_PCI_IO_START,
			MCPN765_PCI_IO_END,
			IORESOURCE_IO,
			"PCI host bridge");

	pci_init_resource(&hose->mem_resources[0],
			MCPN765_PCI_MEM_START,
			MCPN765_PCI_MEM_END,
			IORESOURCE_MEM,
			"PCI host bridge");

	hose->io_space.start = MCPN765_PCI_IO_START;
	hose->io_space.end = MCPN765_PCI_IO_END;
	hose->mem_space.start = MCPN765_PCI_MEM_START;
	hose->mem_space.end = MCPN765_PCI_MEM_END - HAWK_MPIC_SIZE;

	if (hawk_init(hose,
		       MCPN765_HAWK_PPC_REG_BASE,
		       MCPN765_PROC_PCI_MEM_START,
		       MCPN765_PROC_PCI_MEM_END - HAWK_MPIC_SIZE,
		       MCPN765_PROC_PCI_IO_START,
		       MCPN765_PROC_PCI_IO_END,
		       MCPN765_PCI_MEM_END - HAWK_MPIC_SIZE + 1) != 0) {
		printk("Could not initialize HAWK bridge\n");
	}

	/* VIA IDE BAR decoders are only 16-bits wide. PCI Auto Config
	 * will reassign the bars outside of 16-bit I/O space, which will 
	 * "break" things. To prevent this, we'll set the IDE chip into
	 * legacy mode and seed the bars with their legacy addresses (in 16-bit
	 * I/O space). The Auto Config code will skip the IDE contoller in 
	 * legacy mode, so our bar values will stick.
	 */
	mcpn765_set_VIA_IDE_legacy();

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	/* Now that we've got 16-bit addresses in the bars, we can switch the
	 * IDE controller back into native mode so we can do "modern" resource
	 * and interrupt management.
	 */
	mcpn765_set_VIA_IDE_native();

	ppc_md.pcibios_fixup = mcpn765_pcibios_fixup;
	ppc_md.pcibios_fixup_bus = NULL;
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = mcpn765_map_irq;

	return;
}
static void __init
mcpn765_setup_arch(void)
{
	struct pci_controller *hose;

	if ( ppc_md.progress )
		ppc_md.progress("mcpn765_setup_arch: enter", 0);

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
		ppc_md.progress("mcpn765_setup_arch: find_bridges", 0);

	/* Lookup PCI host bridges */
	mcpn765_find_bridges();

	hose = pci_bus_to_hose(0);
	isa_io_base = (ulong)hose->io_base_virt;

	TODC_INIT(TODC_TYPE_MK48T37,
		  (MCPN765_PHYS_NVRAM_AS0 - isa_io_base),
		  (MCPN765_PHYS_NVRAM_AS1 - isa_io_base),
		  (MCPN765_PHYS_NVRAM_DATA - isa_io_base),
		  8);

	OpenPIC_InitSenses = mcpn765_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(mcpn765_openpic_initsenses);

	printk("Motorola MCG MCPN765 cPCI Non-System Board\n");
	printk("MCPN765 port (MontaVista Software, Inc. (source@mvista.com))\n");

	if ( ppc_md.progress )
		ppc_md.progress("mcpn765_setup_arch: exit", 0);

	return;
}

static void __init
mcpn765_init2(void)
{

	request_region(0x00,0x20,"dma1");
	request_region(0x20,0x20,"pic1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xa0,0x20,"pic2");
	request_region(0xc0,0x20,"dma2");

	return;
}

/*
 * Interrupt setup and service.
 * Have MPIC on HAWK and cascaded 8259s on VIA 82586 cascaded to MPIC.
 */
static void __init
mcpn765_init_IRQ(void)
{
	int i;

	if ( ppc_md.progress )
		ppc_md.progress("init_irq: enter", 0);

	openpic_init(NUM_8259_INTERRUPTS);
	openpic_hookup_cascade(NUM_8259_INTERRUPTS, "82c59 cascade",
			i8259_irq);

	for(i=0; i < NUM_8259_INTERRUPTS; i++)
		irq_desc[i].handler = &i8259_pic;

	i8259_init(0);

	if ( ppc_md.progress )
		ppc_md.progress("init_irq: exit", 0);

	return;
}

static u32
mcpn765_irq_canonicalize(u32 irq)
{
	if (irq == 2)
		return 9;
	else
		return irq;
}

static unsigned long __init
mcpn765_find_end_of_memory(void)
{
	return hawk_get_mem_size(MCPN765_HAWK_SMC_BASE);
}

static void __init
mcpn765_map_io(void)
{
	io_block_mapping(0xfe800000, 0xfe800000, 0x00800000, _PAGE_IO);
}

static void
mcpn765_reset_board(void)
{
	local_irq_disable();

	/* set VIA IDE controller into native mode */
	mcpn765_set_VIA_IDE_native();

	/* Set exception prefix high - to the firmware */
	_nmask_and_or_msr(0, MSR_IP);

	out_8((u_char *)MCPN765_BOARD_MODRST_REG, 0x01);

	return;
}

static void
mcpn765_restart(char *cmd)
{
	volatile ulong	i = 10000000;

	mcpn765_reset_board();

	while (i-- > 0);
	panic("restart failed\n");
}

static void
mcpn765_power_off(void)
{
	mcpn765_halt();
	/* NOTREACHED */
}

static void
mcpn765_halt(void)
{
	local_irq_disable();
	while (1);
	/* NOTREACHED */
}

static int
mcpn765_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: Motorola MCG\n");
	seq_printf(m, "machine\t\t: MCPN765\n");

	return 0;
}

/*
 * Set BAT 3 to map 0xf0000000 to end of physical memory space.
 */
static __inline__ void
mcpn765_set_bat(void)
{
	mb();
	mtspr(SPRN_DBAT1U, 0xfe8000fe);
	mtspr(SPRN_DBAT1L, 0xfe80002a);
	mb();
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	/* Map in board regs, etc. */
	mcpn765_set_bat();

	isa_mem_base = MCPN765_ISA_MEM_BASE;
	pci_dram_offset = MCPN765_PCI_DRAM_OFFSET;
	ISA_DMA_THRESHOLD = 0x00ffffff;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;

	ppc_md.setup_arch = mcpn765_setup_arch;
	ppc_md.show_cpuinfo = mcpn765_show_cpuinfo;
	ppc_md.irq_canonicalize = mcpn765_irq_canonicalize;
	ppc_md.init_IRQ = mcpn765_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;
	ppc_md.init = mcpn765_init2;

	ppc_md.restart = mcpn765_restart;
	ppc_md.power_off = mcpn765_power_off;
	ppc_md.halt = mcpn765_halt;

	ppc_md.find_end_of_memory = mcpn765_find_end_of_memory;
	ppc_md.setup_io_mappings = mcpn765_map_io;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.calibrate_decr = todc_calibrate_decr;

	ppc_md.nvram_read_val = todc_m48txx_read_val;
	ppc_md.nvram_write_val = todc_m48txx_write_val;

	ppc_md.heartbeat = NULL;
	ppc_md.heartbeat_reset = 0;
	ppc_md.heartbeat_count = 0;

#ifdef CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = gen550_progress;
#endif
#ifdef CONFIG_KGDB
	ppc_md.kgdb_map_scc = gen550_kgdb_map_scc;
#endif

	return;
}
