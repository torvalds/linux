/*
 * arch/ppc/platforms/k2.c
 *
 * Board setup routines for SBS K2
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * Updated by: Randy Vinson <rvinson@mvista.com.
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
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/i8259.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>

#include <syslib/cpc710.h>
#include "k2.h"

extern unsigned long loops_per_jiffy;
extern void gen550_progress(char *, unsigned short);

static unsigned int cpu_7xx[16] = {
	0, 15, 14, 0, 0, 13, 5, 9, 6, 11, 8, 10, 16, 12, 7, 0
};
static unsigned int cpu_6xx[16] = {
	0, 0, 14, 0, 0, 13, 5, 9, 6, 11, 8, 10, 0, 12, 7, 0
};

static inline int __init
k2_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	struct pci_controller *hose = pci_bus_to_hose(dev->bus->number);
	/*
	 * Check our hose index.  If we are zero then we are on the
	 * local PCI hose, otherwise we are on the cPCI hose.
	 */
	if (!hose->index) {
		static char pci_irq_table[][4] =
			/*
			 * 	PCI IDSEL/INTPIN->INTLINE
			 * 	A	B	C	D
			 */
		{
			{1, 	0,	0,	0},	/* Ethernet */
			{5,	5,	5,	5},	/* PMC Site 1 */
			{6,	6,	6,	6},	/* PMC Site 2 */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* PCI-ISA Bridge */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{0,     0,      0,      0},     /* unused */
			{15,	0,	0,	0},	/* M5229 IDE */
		};
		const long min_idsel = 3, max_idsel = 17, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	} else {
		static char pci_irq_table[][4] =
		/*
		 * 	PCI IDSEL/INTPIN->INTLINE
		 * 	A	B	C	D
		 */
		{
			{10, 	11,	12,	9},	/* cPCI slot 8 */
			{11, 	12,	9,	10},	/* cPCI slot 7 */
			{12, 	9,	10,	11},	/* cPCI slot 6 */
			{9, 	10,	11,	12},	/* cPCI slot 5 */
			{10, 	11,	12,	9},	/* cPCI slot 4 */
			{11, 	12,	9,	10},	/* cPCI slot 3 */
			{12, 	9,	10,	11},	/* cPCI slot 2 */
		};
		const long min_idsel = 15, max_idsel = 21, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	}
}

void k2_pcibios_fixup(void)
{
#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
	struct pci_dev *ide_dev;

	/*
	 * Enable DMA support on hdc
	 */
	ide_dev = pci_get_device(PCI_VENDOR_ID_AL,
				  PCI_DEVICE_ID_AL_M5229, NULL);

	if (ide_dev) {

		unsigned long ide_dma_base;

		ide_dma_base = pci_resource_start(ide_dev, 4);
		outb(0x00, ide_dma_base + 0x2);
		outb(0x20, ide_dma_base + 0xa);
		pci_dev_put(ide_dev);
	}
#endif
}

void k2_pcibios_fixup_resources(struct pci_dev *dev)
{
	int i;

	if ((dev->vendor == PCI_VENDOR_ID_IBM) &&
	    (dev->device == PCI_DEVICE_ID_IBM_CPC710_PCI64)) {
		pr_debug("Fixup CPC710 resources\n");
		for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
			dev->resource[i].start = 0;
			dev->resource[i].end = 0;
		}
	}
}

void k2_setup_hoses(void)
{
	struct pci_controller *hose_a, *hose_b;

	/*
	 * Reconfigure CPC710 memory map so
	 * we have some more PCI memory space.
	 */

	/* Set FPHB mode */
	__raw_writel(0x808000e0, PGCHP);	/* Set FPHB mode */

	/* PCI32 mappings */
	__raw_writel(0x00000000, K2_PCI32_BAR + PIBAR);	/* PCI I/O base */
	__raw_writel(0x00000000, K2_PCI32_BAR + PMBAR);	/* PCI Mem base */
	__raw_writel(0xf0000000, K2_PCI32_BAR + MSIZE);	/* 256MB */
	__raw_writel(0xfff00000, K2_PCI32_BAR + IOSIZE); /* 1MB */
	__raw_writel(0xc0000000, K2_PCI32_BAR + SMBAR);	/* Base@0xc0000000 */
	__raw_writel(0x80000000, K2_PCI32_BAR + SIBAR);	/* Base@0x80000000 */
	__raw_writel(0x000000c0, K2_PCI32_BAR + PSSIZE); /* 1GB space */
	__raw_writel(0x000000c0, K2_PCI32_BAR + PPSIZE); /* 1GB space */
	__raw_writel(0x00000000, K2_PCI32_BAR + BARPS);	/* Base@0x00000000 */
	__raw_writel(0x00000000, K2_PCI32_BAR + BARPP);	/* Base@0x00000000 */
	__raw_writel(0x00000080, K2_PCI32_BAR + PSBAR);	/* Base@0x80 */
	__raw_writel(0x00000000, K2_PCI32_BAR + PPBAR);

	__raw_writel(0xc0000000, K2_PCI32_BAR + BPMDLK);
	__raw_writel(0xd0000000, K2_PCI32_BAR + TPMDLK);
	__raw_writel(0x80000000, K2_PCI32_BAR + BIODLK);
	__raw_writel(0x80100000, K2_PCI32_BAR + TIODLK);
	__raw_writel(0xe0008000, K2_PCI32_BAR + DLKCTRL);
	__raw_writel(0xffffffff, K2_PCI32_BAR + DLKDEV);

	/* PCI64 mappings */
	__raw_writel(0x00100000, K2_PCI64_BAR + PIBAR);	/* PCI I/O base */
	__raw_writel(0x10000000, K2_PCI64_BAR + PMBAR);	/* PCI Mem base */
	__raw_writel(0xf0000000, K2_PCI64_BAR + MSIZE);	/* 256MB */
	__raw_writel(0xfff00000, K2_PCI64_BAR + IOSIZE); /* 1MB */
	__raw_writel(0xd0000000, K2_PCI64_BAR + SMBAR);	/* Base@0xd0000000 */
	__raw_writel(0x80100000, K2_PCI64_BAR + SIBAR);	/* Base@0x80100000 */
	__raw_writel(0x000000c0, K2_PCI64_BAR + PSSIZE); /* 1GB space */
	__raw_writel(0x000000c0, K2_PCI64_BAR + PPSIZE); /* 1GB space */
	__raw_writel(0x00000000, K2_PCI64_BAR + BARPS);	/* Base@0x00000000 */
	__raw_writel(0x00000000, K2_PCI64_BAR + BARPP);	/* Base@0x00000000 */

	/* Setup PCI32 hose */
	hose_a = pcibios_alloc_controller();
	if (!hose_a)
		return;

	hose_a->first_busno = 0;
	hose_a->last_busno = 0xff;
	hose_a->pci_mem_offset = K2_PCI32_MEM_BASE;

	pci_init_resource(&hose_a->io_resource,
			  K2_PCI32_LOWER_IO,
			  K2_PCI32_UPPER_IO,
			  IORESOURCE_IO, "PCI32 host bridge");

	pci_init_resource(&hose_a->mem_resources[0],
			  K2_PCI32_LOWER_MEM + K2_PCI32_MEM_BASE,
			  K2_PCI32_UPPER_MEM + K2_PCI32_MEM_BASE,
			  IORESOURCE_MEM, "PCI32 host bridge");

	hose_a->io_space.start = K2_PCI32_LOWER_IO;
	hose_a->io_space.end = K2_PCI32_UPPER_IO;
	hose_a->mem_space.start = K2_PCI32_LOWER_MEM;
	hose_a->mem_space.end = K2_PCI32_UPPER_MEM;
	hose_a->io_base_virt = (void *)K2_ISA_IO_BASE;

	setup_indirect_pci(hose_a, K2_PCI32_CONFIG_ADDR, K2_PCI32_CONFIG_DATA);

	/* Initialize PCI32 bus registers */
	early_write_config_byte(hose_a,
				hose_a->first_busno,
				PCI_DEVFN(0, 0),
				CPC710_BUS_NUMBER, hose_a->first_busno);

	early_write_config_byte(hose_a,
				hose_a->first_busno,
				PCI_DEVFN(0, 0),
				CPC710_SUB_BUS_NUMBER, hose_a->last_busno);

	/* Enable PCI interrupt polling */
	early_write_config_byte(hose_a,
				hose_a->first_busno,
				PCI_DEVFN(8, 0), 0x45, 0x80);

	/* Route polled PCI interrupts */
	early_write_config_byte(hose_a,
				hose_a->first_busno,
				PCI_DEVFN(8, 0), 0x48, 0x58);

	early_write_config_byte(hose_a,
				hose_a->first_busno,
				PCI_DEVFN(8, 0), 0x49, 0x07);

	early_write_config_byte(hose_a,
				hose_a->first_busno,
				PCI_DEVFN(8, 0), 0x4a, 0x31);

	early_write_config_byte(hose_a,
				hose_a->first_busno,
				PCI_DEVFN(8, 0), 0x4b, 0xb9);

	/* route secondary IDE channel interrupt to IRQ 15 */
	early_write_config_byte(hose_a,
				hose_a->first_busno,
				PCI_DEVFN(8, 0), 0x75, 0x0f);

	/* enable IDE controller IDSEL */
	early_write_config_byte(hose_a,
				hose_a->first_busno,
				PCI_DEVFN(8, 0), 0x58, 0x48);

	/* Enable IDE function */
	early_write_config_byte(hose_a,
				hose_a->first_busno,
				PCI_DEVFN(17, 0), 0x50, 0x03);

	/* Set M5229 IDE controller to native mode */
	early_write_config_byte(hose_a,
				hose_a->first_busno,
				PCI_DEVFN(17, 0), PCI_CLASS_PROG, 0xdf);

	hose_a->last_busno = pciauto_bus_scan(hose_a, hose_a->first_busno);

	/* Write out correct max subordinate bus number for hose A */
	early_write_config_byte(hose_a,
				hose_a->first_busno,
				PCI_DEVFN(0, 0),
				CPC710_SUB_BUS_NUMBER, hose_a->last_busno);

	/* Only setup PCI64 hose if we are in the system slot */
	if (!(readb(K2_MISC_REG) & K2_SYS_SLOT_MASK)) {
		/* Setup PCI64 hose */
		hose_b = pcibios_alloc_controller();
		if (!hose_b)
			return;

		hose_b->first_busno = hose_a->last_busno + 1;
		hose_b->last_busno = 0xff;

		/* Reminder: quit changing the following, it is correct. */
		hose_b->pci_mem_offset = K2_PCI32_MEM_BASE;

		pci_init_resource(&hose_b->io_resource,
				  K2_PCI64_LOWER_IO,
				  K2_PCI64_UPPER_IO,
				  IORESOURCE_IO, "PCI64 host bridge");

		pci_init_resource(&hose_b->mem_resources[0],
				  K2_PCI64_LOWER_MEM + K2_PCI32_MEM_BASE,
				  K2_PCI64_UPPER_MEM + K2_PCI32_MEM_BASE,
				  IORESOURCE_MEM, "PCI64 host bridge");

		hose_b->io_space.start = K2_PCI64_LOWER_IO;
		hose_b->io_space.end = K2_PCI64_UPPER_IO;
		hose_b->mem_space.start = K2_PCI64_LOWER_MEM;
		hose_b->mem_space.end = K2_PCI64_UPPER_MEM;
		hose_b->io_base_virt = (void *)K2_ISA_IO_BASE;

		setup_indirect_pci(hose_b,
				   K2_PCI64_CONFIG_ADDR, K2_PCI64_CONFIG_DATA);

		/* Initialize PCI64 bus registers */
		early_write_config_byte(hose_b,
					0,
					PCI_DEVFN(0, 0),
					CPC710_SUB_BUS_NUMBER, 0xff);

		early_write_config_byte(hose_b,
					0,
					PCI_DEVFN(0, 0),
					CPC710_BUS_NUMBER, hose_b->first_busno);

		hose_b->last_busno = pciauto_bus_scan(hose_b,
						      hose_b->first_busno);

		/* Write out correct max subordinate bus number for hose B */
		early_write_config_byte(hose_b,
					hose_b->first_busno,
					PCI_DEVFN(0, 0),
					CPC710_SUB_BUS_NUMBER,
					hose_b->last_busno);

		/* Configure PCI64 PSBAR */
		early_write_config_dword(hose_b,
					 hose_b->first_busno,
					 PCI_DEVFN(0, 0),
					 PCI_BASE_ADDRESS_0,
					 K2_PCI64_SYS_MEM_BASE);
	}

	/* Configure i8259 level/edge settings */
	outb(0x62, 0x4d0);
	outb(0xde, 0x4d1);

#ifdef CONFIG_CPC710_DATA_GATHERING
	{
		unsigned int tmp;
		tmp = __raw_readl(ABCNTL);
		/* Enable data gathering on both PCI interfaces */
		__raw_writel(tmp | 0x05000000, ABCNTL);
	}
#endif

	ppc_md.pcibios_fixup = k2_pcibios_fixup;
	ppc_md.pcibios_fixup_resources = k2_pcibios_fixup_resources;
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = k2_map_irq;
}

static int k2_get_bus_speed(void)
{
	int bus_speed;
	unsigned char board_id;

	board_id = *(unsigned char *)K2_BOARD_ID_REG;

	switch (K2_BUS_SPD(board_id)) {

	case 0:
	default:
		bus_speed = 100000000;
		break;

	case 1:
		bus_speed = 83333333;
		break;

	case 2:
		bus_speed = 75000000;
		break;

	case 3:
		bus_speed = 66666666;
		break;
	}
	return bus_speed;
}

static int k2_get_cpu_speed(void)
{
	unsigned long hid1;
	int cpu_speed;

	hid1 = mfspr(SPRN_HID1) >> 28;

	if ((mfspr(SPRN_PVR) >> 16) == 8)
		hid1 = cpu_7xx[hid1];
	else
		hid1 = cpu_6xx[hid1];

	cpu_speed = k2_get_bus_speed() * hid1 / 2;
	return cpu_speed;
}

static void __init k2_calibrate_decr(void)
{
	int freq, divisor = 4;

	/* determine processor bus speed */
	freq = k2_get_bus_speed();
	tb_ticks_per_jiffy = freq / HZ / divisor;
	tb_to_us = mulhwu_scale_factor(freq / divisor, 1000000);
}

static int k2_show_cpuinfo(struct seq_file *m)
{
	unsigned char k2_geo_bits, k2_system_slot;

	seq_printf(m, "vendor\t\t: SBS\n");
	seq_printf(m, "machine\t\t: K2\n");
	seq_printf(m, "cpu speed\t: %dMhz\n", k2_get_cpu_speed() / 1000000);
	seq_printf(m, "bus speed\t: %dMhz\n", k2_get_bus_speed() / 1000000);
	seq_printf(m, "memory type\t: SDRAM\n");

	k2_geo_bits = readb(K2_MSIZ_GEO_REG) & K2_GEO_ADR_MASK;
	k2_system_slot = !(readb(K2_MISC_REG) & K2_SYS_SLOT_MASK);
	seq_printf(m, "backplane\t: %s slot board",
		   k2_system_slot ? "System" : "Non system");
	seq_printf(m, "with geographical address %x\n", k2_geo_bits);

	return 0;
}

TODC_ALLOC();

static void __init k2_setup_arch(void)
{
	unsigned int cpu;

	/* Setup TODC access */
	TODC_INIT(TODC_TYPE_MK48T37, 0, 0,
		  ioremap(K2_RTC_BASE_ADDRESS, K2_RTC_SIZE), 8);

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000 / HZ;

	/* make FLASH transactions higher priority than PCI to avoid deadlock */
	__raw_writel(__raw_readl(SIOC1) | 0x80000000, SIOC1);

	/* Set hardware to access FLASH page 2 */
	__raw_writel(1 << 29, GPOUT);

	/* Setup PCI host bridges */
	k2_setup_hoses();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_HDC1;
#endif

	/* Identify the system */
	printk(KERN_INFO "System Identification: SBS K2 - PowerPC 750 @ "
			"%d Mhz\n", k2_get_cpu_speed() / 1000000);
	printk(KERN_INFO "Port by MontaVista Software, Inc. "
			"(source@mvista.com)\n");

	/* Identify the CPU manufacturer */
	cpu = PVR_REV(mfspr(SPRN_PVR));
	printk(KERN_INFO "CPU manufacturer: %s [rev=%04x]\n",
			(cpu & (1 << 15)) ? "IBM" : "Motorola", cpu);
}

static void k2_restart(char *cmd)
{
	local_irq_disable();

	/* Flip FLASH back to page 1 to access firmware image */
	__raw_writel(0, GPOUT);

	/* SRR0 has system reset vector, SRR1 has default MSR value */
	/* rfi restores MSR from SRR1 and sets the PC to the SRR0 value */
	mtspr(SPRN_SRR0, 0xfff00100);
	mtspr(SPRN_SRR1, 0);
	__asm__ __volatile__("rfi\n\t");

	/* not reached */
	for (;;) ;
}

static void k2_power_off(void)
{
	for (;;) ;
}

static void k2_halt(void)
{
	k2_restart(NULL);
}

/*
 * Set BAT 3 to map PCI32 I/O space.
 */
static __inline__ void k2_set_bat(void)
{
	/* wait for all outstanding memory accesses to complete */
	mb();

	/* setup DBATs */
	mtspr(SPRN_DBAT2U, 0x80001ffe);
	mtspr(SPRN_DBAT2L, 0x8000002a);
	mtspr(SPRN_DBAT3U, 0xf0001ffe);
	mtspr(SPRN_DBAT3L, 0xf000002a);

	/* wait for updates */
	mb();
}

static unsigned long __init k2_find_end_of_memory(void)
{
	unsigned long total;
	unsigned char msize = 7;	/* Default to 128MB */

	msize = K2_MEM_SIZE(readb(K2_MSIZ_GEO_REG));

	switch (msize) {
	case 2:
		/*
		 * This will break without a lowered
		 * KERNELBASE or CONFIG_HIGHMEM on.
		 * It seems non 1GB builds exist yet,
		 * though.
		 */
		total = K2_MEM_SIZE_1GB;
		break;
	case 3:
	case 4:
		total = K2_MEM_SIZE_512MB;
		break;
	case 5:
	case 6:
		total = K2_MEM_SIZE_256MB;
		break;
	case 7:
		total = K2_MEM_SIZE_128MB;
		break;
	default:
		printk
		    ("K2: Invalid memory size detected, defaulting to 128MB\n");
		total = K2_MEM_SIZE_128MB;
		break;
	}
	return total;
}

static void __init k2_map_io(void)
{
	io_block_mapping(K2_PCI32_IO_BASE,
			 K2_PCI32_IO_BASE, 0x00200000, _PAGE_IO);
	io_block_mapping(0xff000000, 0xff000000, 0x01000000, _PAGE_IO);
}

static void __init k2_init_irq(void)
{
	int i;

	for (i = 0; i < 16; i++)
		irq_desc[i].handler = &i8259_pic;

	i8259_init(0);
}

void __init platform_init(unsigned long r3, unsigned long r4,
			  unsigned long r5, unsigned long r6, unsigned long r7)
{
	parse_bootinfo((struct bi_record *)(r3 + KERNELBASE));

	k2_set_bat();

	isa_io_base = K2_ISA_IO_BASE;
	isa_mem_base = K2_ISA_MEM_BASE;
	pci_dram_offset = K2_PCI32_SYS_MEM_BASE;

	ppc_md.setup_arch = k2_setup_arch;
	ppc_md.show_cpuinfo = k2_show_cpuinfo;
	ppc_md.init_IRQ = k2_init_irq;
	ppc_md.get_irq = i8259_irq;

	ppc_md.find_end_of_memory = k2_find_end_of_memory;
	ppc_md.setup_io_mappings = k2_map_io;

	ppc_md.restart = k2_restart;
	ppc_md.power_off = k2_power_off;
	ppc_md.halt = k2_halt;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.calibrate_decr = k2_calibrate_decr;

	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;

#ifdef CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = gen550_progress;
#endif
}
