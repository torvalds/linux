/*
 * arch/ppc/platforms/4xx/taishan.c
 *
 * AMCC Taishan board specific routines
 *
 * Copyright 2007 DENX Software Engineering, Stefan Roese <sr@denx.de>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
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
#include <linux/blkdev.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/ndfc.h>
#include <linux/mtd/physmap.h>

#include <asm/machdep.h>
#include <asm/ocp.h>
#include <asm/bootinfo.h>
#include <asm/ppcboot.h>

#include <syslib/gen550.h>
#include <syslib/ibm440gx_common.h>

extern bd_t __res;

static struct ibm44x_clocks clocks __initdata;

/*
 * NOR FLASH configuration (using mtd physmap driver)
 */

/* start will be added dynamically, end is always fixed */
static struct resource taishan_nor_resource = {
	.start = TAISHAN_FLASH_ADDR,
	.end   = 0x1ffffffffULL,
	.flags = IORESOURCE_MEM,
};

#define RW_PART0_OF	0
#define RW_PART0_SZ	0x180000
#define RW_PART1_SZ	0x200000
/* Partition 2 will be autosized dynamically... */
#define RW_PART3_SZ	0x80000
#define RW_PART4_SZ	0x40000

static struct mtd_partition taishan_nor_parts[] = {
	{
		.name = "kernel",
		.offset = 0,
		.size = RW_PART0_SZ
	},
	{
		.name = "root",
		.offset = MTDPART_OFS_APPEND,
		.size = RW_PART1_SZ,
	},
	{
		.name = "user",
		.offset = MTDPART_OFS_APPEND,
/*		.size = RW_PART2_SZ */ /* will be adjusted dynamically */
	},
	{
		.name = "env",
		.offset = MTDPART_OFS_APPEND,
		.size = RW_PART3_SZ,
	},
	{
		.name = "u-boot",
		.offset = MTDPART_OFS_APPEND,
		.size = RW_PART4_SZ,
	}
};

static struct physmap_flash_data taishan_nor_data = {
	.width		= 4,
	.parts		= taishan_nor_parts,
	.nr_parts	= ARRAY_SIZE(taishan_nor_parts),
};

static struct platform_device taishan_nor_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev = {
			.platform_data = &taishan_nor_data,
		},
	.num_resources	= 1,
	.resource	= &taishan_nor_resource,
};

static int taishan_setup_flash(void)
{
	/*
	 * Adjust partition 2 to flash size
	 */
	taishan_nor_parts[2].size = __res.bi_flashsize -
		RW_PART0_SZ - RW_PART1_SZ - RW_PART3_SZ - RW_PART4_SZ;

	platform_device_register(&taishan_nor_device);

	return 0;
}
arch_initcall(taishan_setup_flash);

static void __init
taishan_calibrate_decr(void)
{
	unsigned int freq;

	if (mfspr(SPRN_CCR1) & CCR1_TCS)
		freq = TAISHAN_TMR_CLK;
	else
		freq = clocks.cpu;

	ibm44x_calibrate_decr(freq);
}

static int
taishan_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: AMCC\n");
	seq_printf(m, "machine\t\t: PPC440GX EVB (Taishan)\n");
	ibm440gx_show_cpuinfo(m);
	return 0;
}

static inline int
taishan_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *	PCI IDSEL/INTPIN->INTLINE
	 * 	   A   B   C   D
	 */
	{
		{ 23, 24, 25, 26 },	/* IDSEL 1 - PCI Slot 0 */
		{ 24, 25, 26, 23 },	/* IDSEL 2 - PCI Slot 1 */
	};

	const long min_idsel = 1, max_idsel = 2, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

static void __init taishan_set_emacdata(void)
{
	struct ocp_def *def;
	struct ocp_func_emac_data *emacdata;
	int i;

	/* Set phy_map, phy_mode, and mac_addr for each EMAC */
	for (i=2; i<4; i++) {
		def = ocp_get_one_device(OCP_VENDOR_IBM, OCP_FUNC_EMAC, i);
		emacdata = def->additions;
		if (i < 2) {
			emacdata->phy_map = 0x00000001;	/* Skip 0x00 */
			emacdata->phy_mode = PHY_MODE_SMII;
		} else {
			emacdata->phy_map = 0x00000001; /* Skip 0x00 */
			emacdata->phy_mode = PHY_MODE_RGMII;
		}
		if (i == 0)
			memcpy(emacdata->mac_addr, "\0\0\0\0\0\0", 6);
		else if (i == 1)
			memcpy(emacdata->mac_addr, "\0\0\0\0\0\0", 6);
		else if (i == 2)
			memcpy(emacdata->mac_addr, __res.bi_enetaddr, 6);
		else if (i == 3)
			memcpy(emacdata->mac_addr, __res.bi_enet1addr, 6);
	}
}

#define PCIX_READW(offset) \
	(readw(pcix_reg_base+offset))

#define PCIX_WRITEW(value, offset) \
	(writew(value, pcix_reg_base+offset))

#define PCIX_WRITEL(value, offset) \
	(writel(value, pcix_reg_base+offset))

/*
 * FIXME: This is only here to "make it work".  This will move
 * to a ibm_pcix.c which will contain a generic IBM PCIX bridge
 * configuration library. -Matt
 */
static void __init
taishan_setup_pcix(void)
{
	void *pcix_reg_base;

	pcix_reg_base = ioremap64(PCIX0_REG_BASE, PCIX_REG_SIZE);

	/* Enable PCIX0 I/O, Mem, and Busmaster cycles */
	PCIX_WRITEW(PCIX_READW(PCIX0_COMMAND) | PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER, PCIX0_COMMAND);

	/* Disable all windows */
	PCIX_WRITEL(0, PCIX0_POM0SA);
	PCIX_WRITEL(0, PCIX0_POM1SA);
	PCIX_WRITEL(0, PCIX0_POM2SA);
	PCIX_WRITEL(0, PCIX0_PIM0SA);
	PCIX_WRITEL(0, PCIX0_PIM0SAH);
	PCIX_WRITEL(0, PCIX0_PIM1SA);
	PCIX_WRITEL(0, PCIX0_PIM2SA);
	PCIX_WRITEL(0, PCIX0_PIM2SAH);

	/* Setup 2GB PLB->PCI outbound mem window (3_8000_0000->0_8000_0000) */
	PCIX_WRITEL(0x00000003, PCIX0_POM0LAH);
	PCIX_WRITEL(0x80000000, PCIX0_POM0LAL);
	PCIX_WRITEL(0x00000000, PCIX0_POM0PCIAH);
	PCIX_WRITEL(0x80000000, PCIX0_POM0PCIAL);
	PCIX_WRITEL(0x80000001, PCIX0_POM0SA);

	/* Setup 2GB PCI->PLB inbound memory window at 0, enable MSIs */
	PCIX_WRITEL(0x00000000, PCIX0_PIM0LAH);
	PCIX_WRITEL(0x00000000, PCIX0_PIM0LAL);
	PCIX_WRITEL(0x80000007, PCIX0_PIM0SA);
	PCIX_WRITEL(0xffffffff, PCIX0_PIM0SAH);

	iounmap(pcix_reg_base);

	eieio();
}

static void __init
taishan_setup_hose(void)
{
	struct pci_controller *hose;

	/* Configure windows on the PCI-X host bridge */
	taishan_setup_pcix();

	hose = pcibios_alloc_controller();

	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;

	hose->pci_mem_offset = TAISHAN_PCI_MEM_OFFSET;

	pci_init_resource(&hose->io_resource,
			TAISHAN_PCI_LOWER_IO,
			TAISHAN_PCI_UPPER_IO,
			IORESOURCE_IO,
			"PCI host bridge");

	pci_init_resource(&hose->mem_resources[0],
			TAISHAN_PCI_LOWER_MEM,
			TAISHAN_PCI_UPPER_MEM,
			IORESOURCE_MEM,
			"PCI host bridge");

	hose->io_space.start = TAISHAN_PCI_LOWER_IO;
	hose->io_space.end = TAISHAN_PCI_UPPER_IO;
	hose->mem_space.start = TAISHAN_PCI_LOWER_MEM;
	hose->mem_space.end = TAISHAN_PCI_UPPER_MEM;
	hose->io_base_virt = ioremap64(TAISHAN_PCI_IO_BASE, TAISHAN_PCI_IO_SIZE);
	isa_io_base = (unsigned long) hose->io_base_virt;

	setup_indirect_pci(hose,
			TAISHAN_PCI_CFGA_PLB32,
			TAISHAN_PCI_CFGD_PLB32);
	hose->set_cfg_type = 1;

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = taishan_map_irq;
}


static void __init
taishan_early_serial_map(void)
{
	struct uart_port port;

	/* Setup ioremapped serial port access */
	memset(&port, 0, sizeof(port));
	port.membase = ioremap64(PPC440GX_UART0_ADDR, 8);
	port.irq = UART0_INT;
	port.uartclk = clocks.uart0;
	port.regshift = 0;
	port.iotype = UPIO_MEM;
	port.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST;
	port.line = 0;

	if (early_serial_setup(&port) != 0)
		printk("Early serial init of port 0 failed\n");

#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
	/* Configure debug serial access */
	gen550_init(0, &port);

	/* Purge TLB entry added in head_44x.S for early serial access */
	_tlbie(UART0_IO_BASE, 0);
#endif

	port.membase = ioremap64(PPC440GX_UART1_ADDR, 8);
	port.irq = UART1_INT;
	port.uartclk = clocks.uart1;
	port.line = 1;

	if (early_serial_setup(&port) != 0)
		printk("Early serial init of port 1 failed\n");

#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
	/* Configure debug serial access */
	gen550_init(1, &port);
#endif
}

static void __init
taishan_setup_arch(void)
{
	taishan_set_emacdata();

	ibm440gx_tah_enable();

	/*
	 * Determine various clocks.
	 * To be completely correct we should get SysClk
	 * from FPGA, because it can be changed by on-board switches
	 * --ebs
	 */
	ibm440gx_get_clocks(&clocks, 33333333, 6 * 1843200);
	ocp_sys_info.opb_bus_freq = clocks.opb;

	/* init to some ~sane value until calibrate_delay() runs */
        loops_per_jiffy = 50000000/HZ;

	/* Setup PCI host bridge */
	taishan_setup_hose();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_HDA1;
#endif

	taishan_early_serial_map();

	/* Identify the system */
	printk("AMCC PowerPC 440GX Taishan Platform\n");
}

static void __init taishan_init(void)
{
	ibm440gx_l2c_setup(&clocks);
}

void __init platform_init(unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, unsigned long r7)
{
	ibm44x_platform_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = taishan_setup_arch;
	ppc_md.show_cpuinfo = taishan_show_cpuinfo;
	ppc_md.get_irq = NULL;		/* Set in ppc4xx_pic_init() */

	ppc_md.calibrate_decr = taishan_calibrate_decr;

#ifdef CONFIG_KGDB
	ppc_md.early_serial_map = taishan_early_serial_map;
#endif
	ppc_md.init = taishan_init;
}

