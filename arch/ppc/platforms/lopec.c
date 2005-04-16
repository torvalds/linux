/*
 * arch/ppc/platforms/lopec.c
 *
 * Setup routines for the Motorola LoPEC.
 *
 * Author: Dan Cox
 * Maintainer: Tom Rini <trini@kernel.crashing.org>
 *
 * 2001-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pci_ids.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <linux/seq_file.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/root_dev.h>
#include <linux/pci.h>

#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/io.h>
#include <asm/open_pic.h>
#include <asm/i8259.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/mpc10x.h>
#include <asm/hw_irq.h>
#include <asm/prep_nvram.h>
#include <asm/kgdb.h>

/*
 * Define all of the IRQ senses and polarities.  Taken from the
 * LoPEC Programmer's Reference Guide.
 */
static u_char lopec_openpic_initsenses[16] __initdata = {
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* IRQ 0 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 1 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* IRQ 2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 3 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* IRQ 4 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* IRQ 5 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 6 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 7 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 8 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 9 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 10 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 11 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* IRQ 12 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE),	/* IRQ 13 */
	(IRQ_SENSE_EDGE | IRQ_POLARITY_NEGATIVE),	/* IRQ 14 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE)	/* IRQ 15 */
};

static inline int __init
lopec_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	int irq;
	static char pci_irq_table[][4] = {
		{16, 0, 0, 0}, /* ID 11 - Winbond */
		{22, 0, 0, 0}, /* ID 12 - SCSI */
		{0, 0, 0, 0}, /* ID 13 - nothing */
		{17, 0, 0, 0}, /* ID 14 - 82559 Ethernet */
		{27, 0, 0, 0}, /* ID 15 - USB */
		{23, 0, 0, 0}, /* ID 16 - PMC slot 1 */
		{24, 0, 0, 0}, /* ID 17 - PMC slot 2 */
		{25, 0, 0, 0}, /* ID 18 - PCI slot */
		{0, 0, 0, 0}, /* ID 19 - nothing */
		{0, 0, 0, 0}, /* ID 20 - nothing */
		{0, 0, 0, 0}, /* ID 21 - nothing */
		{0, 0, 0, 0}, /* ID 22 - nothing */
		{0, 0, 0, 0}, /* ID 23 - nothing */
		{0, 0, 0, 0}, /* ID 24 - PMC slot 1b */
		{0, 0, 0, 0}, /* ID 25 - nothing */
		{0, 0, 0, 0}  /* ID 26 - PMC Slot 2b */
	};
	const long min_idsel = 11, max_idsel = 26, irqs_per_slot = 4;

	irq = PCI_IRQ_TABLE_LOOKUP;
	if (!irq)
		return 0;

	return irq;
}

static void __init
lopec_setup_winbond_83553(struct pci_controller *hose)
{
	int devfn;

	devfn = PCI_DEVFN(11,0);

	/* IDE interrupt routing (primary 14, secondary 15) */
	early_write_config_byte(hose, 0, devfn, 0x43, 0xef);
	/* PCI interrupt routing */
	early_write_config_word(hose, 0, devfn, 0x44, 0x0000);

	/* ISA-PCI address decoder */
	early_write_config_byte(hose, 0, devfn, 0x48, 0xf0);

	/* RTC, kb, not used in PPC */
	early_write_config_byte(hose, 0, devfn, 0x4d, 0x00);
	early_write_config_byte(hose, 0, devfn, 0x4e, 0x04);
	devfn = PCI_DEVFN(11, 1);
	early_write_config_byte(hose, 0, devfn, 0x09, 0x8f);
	early_write_config_dword(hose, 0, devfn, 0x40, 0x00ff0011);
}

static void __init
lopec_find_bridges(void)
{
	struct pci_controller *hose;

	hose = pcibios_alloc_controller();
	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;

	if (mpc10x_bridge_init(hose, MPC10X_MEM_MAP_B, MPC10X_MEM_MAP_B,
				MPC10X_MAPB_EUMB_BASE) == 0) {

		hose->mem_resources[0].end = 0xffffffff;
		lopec_setup_winbond_83553(hose);
		hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);
		ppc_md.pci_swizzle = common_swizzle;
		ppc_md.pci_map_irq = lopec_map_irq;
	}
}

static int
lopec_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "machine\t\t: Motorola LoPEC\n");
	return 0;
}

static u32
lopec_irq_canonicalize(u32 irq)
{
	if (irq == 2)
		return 9;
	else
		return irq;
}

static void
lopec_restart(char *cmd)
{
#define LOPEC_SYSSTAT1 0xffe00000
	/* force a hard reset, if possible */
	unsigned char reg = *((unsigned char *) LOPEC_SYSSTAT1);
	reg |= 0x80;
	*((unsigned char *) LOPEC_SYSSTAT1) = reg;

	local_irq_disable();
	while(1);
#undef LOPEC_SYSSTAT1
}

static void
lopec_halt(void)
{
	local_irq_disable();
	while(1);
}

static void
lopec_power_off(void)
{
	lopec_halt();
}

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
int lopec_ide_ports_known = 0;
static unsigned long lopec_ide_regbase[MAX_HWIFS];
static unsigned long lopec_ide_ctl_regbase[MAX_HWIFS];
static unsigned long lopec_idedma_regbase;

static void
lopec_ide_probe(void)
{
	struct pci_dev *dev = pci_get_device(PCI_VENDOR_ID_WINBOND,
					      PCI_DEVICE_ID_WINBOND_82C105,
					      NULL);
	lopec_ide_ports_known = 1;

	if (dev) {
		lopec_ide_regbase[0] = dev->resource[0].start;
		lopec_ide_regbase[1] = dev->resource[2].start;
		lopec_ide_ctl_regbase[0] = dev->resource[1].start;
		lopec_ide_ctl_regbase[1] = dev->resource[3].start;
		lopec_idedma_regbase = dev->resource[4].start;
		pci_dev_put(dev);
	}
}

static int
lopec_ide_default_irq(unsigned long base)
{
	if (lopec_ide_ports_known == 0)
		lopec_ide_probe();

	if (base == lopec_ide_regbase[0])
		return 14;
	else if (base == lopec_ide_regbase[1])
		return 15;
	else
		return 0;
}

static unsigned long
lopec_ide_default_io_base(int index)
{
	if (lopec_ide_ports_known == 0)
		lopec_ide_probe();
	return lopec_ide_regbase[index];
}

static void __init
lopec_ide_init_hwif_ports(hw_regs_t *hw, unsigned long data,
			  unsigned long ctl, int *irq)
{
	unsigned long reg = data;
	uint alt_status_base;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++)
		hw->io_ports[i] = reg++;

	if (data == lopec_ide_regbase[0]) {
		alt_status_base = lopec_ide_ctl_regbase[0] + 2;
		hw->irq = 14;
	} else if (data == lopec_ide_regbase[1]) {
		alt_status_base = lopec_ide_ctl_regbase[1] + 2;
		hw->irq = 15;
	} else {
		alt_status_base = 0;
		hw->irq = 0;
	}

	if (ctl)
		hw->io_ports[IDE_CONTROL_OFFSET] = ctl;
	else
		hw->io_ports[IDE_CONTROL_OFFSET] = alt_status_base;

	if (irq != NULL)
		*irq = hw->irq;

}
#endif /* BLK_DEV_IDE */

static void __init
lopec_init_IRQ(void)
{
	int i;

	/*
	 * Provide the open_pic code with the correct table of interrupts.
	 */
	OpenPIC_InitSenses = lopec_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(lopec_openpic_initsenses);

	mpc10x_set_openpic();

	/* We have a cascade on OpenPIC IRQ 0, Linux IRQ 16 */
	openpic_hookup_cascade(NUM_8259_INTERRUPTS, "82c59 cascade",
			&i8259_irq);

	/* Map i8259 interrupts */
	for(i = 0; i < NUM_8259_INTERRUPTS; i++)
		irq_desc[i].handler = &i8259_pic;

	/*
	 * The EPIC allows for a read in the range of 0xFEF00000 ->
	 * 0xFEFFFFFF to generate a PCI interrupt-acknowledge transaction.
	 */
	i8259_init(0xfef00000);
}

static int __init
lopec_request_io(void)
{
	outb(0x00, 0x4d0);
	outb(0xc0, 0x4d1);

	request_region(0x00, 0x20, "dma1");
	request_region(0x20, 0x20, "pic1");
	request_region(0x40, 0x20, "timer");
	request_region(0x80, 0x10, "dma page reg");
	request_region(0xa0, 0x20, "pic2");
	request_region(0xc0, 0x20, "dma2");

	return 0;
}

device_initcall(lopec_request_io);

static void __init
lopec_map_io(void)
{
	io_block_mapping(0xf0000000, 0xf0000000, 0x10000000, _PAGE_IO);
	io_block_mapping(0xb0000000, 0xb0000000, 0x10000000, _PAGE_IO);
}

/*
 * Set BAT 3 to map 0xf8000000 to end of physical memory space 1-to-1.
 */
static __inline__ void
lopec_set_bat(void)
{
	mb();
	mtspr(SPRN_DBAT1U, 0xf8000ffe);
	mtspr(SPRN_DBAT1L, 0xf800002a);
	mb();
}

TODC_ALLOC();

static void __init
lopec_setup_arch(void)
{

	TODC_INIT(TODC_TYPE_MK48T37, 0, 0,
		  ioremap(0xffe80000, 0x8000), 8);

	loops_per_jiffy = 100000000/HZ;

	lopec_find_bridges();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#elif defined(CONFIG_ROOT_NFS)
        	ROOT_DEV = Root_NFS;
#elif defined(CONFIG_BLK_DEV_IDEDISK)
	        ROOT_DEV = Root_HDA1;
#else
        	ROOT_DEV = Root_SDA1;
#endif

#ifdef CONFIG_PPCBUG_NVRAM
	/* Read in NVRAM data */
	init_prep_nvram();

	/* if no bootargs, look in NVRAM */
	if ( cmd_line[0] == '\0' ) {
		char *bootargs;
		 bootargs = prep_nvram_get_var("bootargs");
		 if (bootargs != NULL) {
			 strcpy(cmd_line, bootargs);
			 /* again.. */
			 strcpy(saved_command_line, cmd_line);
		}
	}
#endif
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());
	lopec_set_bat();

	isa_io_base = MPC10X_MAPB_ISA_IO_BASE;
	isa_mem_base = MPC10X_MAPB_ISA_MEM_BASE;
	pci_dram_offset = MPC10X_MAPB_DRAM_OFFSET;
	ISA_DMA_THRESHOLD = 0x00ffffff;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;

	ppc_md.setup_arch = lopec_setup_arch;
	ppc_md.show_cpuinfo = lopec_show_cpuinfo;
	ppc_md.irq_canonicalize = lopec_irq_canonicalize;
	ppc_md.init_IRQ = lopec_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;

	ppc_md.restart = lopec_restart;
	ppc_md.power_off = lopec_power_off;
	ppc_md.halt = lopec_halt;

	ppc_md.setup_io_mappings = lopec_map_io;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.calibrate_decr = todc_calibrate_decr;

	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
	ppc_ide_md.default_irq = lopec_ide_default_irq;
	ppc_ide_md.default_io_base = lopec_ide_default_io_base;
	ppc_ide_md.ide_init_hwif = lopec_ide_init_hwif_ports;
#endif
#ifdef CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = gen550_progress;
#endif
}
