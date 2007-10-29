/*
 * Ocotea board specific routines
 *
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright 2003-2005 MontaVista Software Inc.
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
#include <linux/ide.h>
#include <linux/initrd.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ocp.h>
#include <asm/pci-bridge.h>
#include <asm/time.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/ppc4xx_pic.h>
#include <asm/ppcboot.h>
#include <asm/tlbflush.h>

#include <syslib/gen550.h>
#include <syslib/ibm440gx_common.h>

extern bd_t __res;

static struct ibm44x_clocks clocks __initdata;

static void __init
ocotea_calibrate_decr(void)
{
	unsigned int freq;

	if (mfspr(SPRN_CCR1) & CCR1_TCS)
		freq = OCOTEA_TMR_CLK;
	else
		freq = clocks.cpu;

	ibm44x_calibrate_decr(freq);
}

static int
ocotea_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: IBM\n");
	seq_printf(m, "machine\t\t: PPC440GX EVB (Ocotea)\n");
	ibm440gx_show_cpuinfo(m);
	return 0;
}

static inline int
ocotea_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *	PCI IDSEL/INTPIN->INTLINE
	 * 	   A   B   C   D
	 */
	{
		{ 23, 23, 23, 23 },	/* IDSEL 1 - PCI Slot 0 */
		{ 24, 24, 24, 24 },	/* IDSEL 2 - PCI Slot 1 */
		{ 25, 25, 25, 25 },	/* IDSEL 3 - PCI Slot 2 */
		{ 26, 26, 26, 26 },	/* IDSEL 4 - PCI Slot 3 */
	};

	const long min_idsel = 1, max_idsel = 4, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

static void __init ocotea_set_emacdata(void)
{
	struct ocp_def *def;
	struct ocp_func_emac_data *emacdata;
	int i;

	/*
	 * Note: Current rev. board only operates in Group 4a
	 * mode, so we always set EMAC0-1 for SMII and EMAC2-3
	 * for RGMII (though these could run in RTBI just the same).
	 *
	 * The FPGA reg 3 information isn't even suitable for
	 * determining the phy_mode, so if the board becomes
	 * usable in !4a, it will be necessary to parse an environment
	 * variable from the firmware or similar to properly configure
	 * the phy_map/phy_mode.
	 */
	/* Set phy_map, phy_mode, and mac_addr for each EMAC */
	for (i=0; i<4; i++) {
		def = ocp_get_one_device(OCP_VENDOR_IBM, OCP_FUNC_EMAC, i);
		emacdata = def->additions;
		if (i < 2) {
			emacdata->phy_map = 0x00000001;	/* Skip 0x00 */
			emacdata->phy_mode = PHY_MODE_SMII;
		}
		else {
			emacdata->phy_map = 0x0000ffff; /* Skip 0x00-0x0f */
			emacdata->phy_mode = PHY_MODE_RGMII;
		}
		if (i == 0)
			memcpy(emacdata->mac_addr, __res.bi_enetaddr, 6);
		else if (i == 1)
			memcpy(emacdata->mac_addr, __res.bi_enet1addr, 6);
		else if (i == 2)
			memcpy(emacdata->mac_addr, __res.bi_enet2addr, 6);
		else if (i == 3)
			memcpy(emacdata->mac_addr, __res.bi_enet3addr, 6);
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
ocotea_setup_pcix(void)
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

	eieio();
}

static void __init
ocotea_setup_hose(void)
{
	struct pci_controller *hose;

	/* Configure windows on the PCI-X host bridge */
	ocotea_setup_pcix();

	hose = pcibios_alloc_controller();

	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;

	hose->pci_mem_offset = OCOTEA_PCI_MEM_OFFSET;

	pci_init_resource(&hose->io_resource,
			OCOTEA_PCI_LOWER_IO,
			OCOTEA_PCI_UPPER_IO,
			IORESOURCE_IO,
			"PCI host bridge");

	pci_init_resource(&hose->mem_resources[0],
			OCOTEA_PCI_LOWER_MEM,
			OCOTEA_PCI_UPPER_MEM,
			IORESOURCE_MEM,
			"PCI host bridge");

	hose->io_space.start = OCOTEA_PCI_LOWER_IO;
	hose->io_space.end = OCOTEA_PCI_UPPER_IO;
	hose->mem_space.start = OCOTEA_PCI_LOWER_MEM;
	hose->mem_space.end = OCOTEA_PCI_UPPER_MEM;
	hose->io_base_virt = ioremap64(OCOTEA_PCI_IO_BASE, OCOTEA_PCI_IO_SIZE);
	isa_io_base = (unsigned long) hose->io_base_virt;

	setup_indirect_pci(hose,
			OCOTEA_PCI_CFGA_PLB32,
			OCOTEA_PCI_CFGD_PLB32);
	hose->set_cfg_type = 1;

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = ocotea_map_irq;
}


TODC_ALLOC();

static void __init
ocotea_early_serial_map(void)
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

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 0 failed\n");
	}

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

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 1 failed\n");
	}

#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
	/* Configure debug serial access */
	gen550_init(1, &port);
#endif
}

static void __init
ocotea_setup_arch(void)
{
	ocotea_set_emacdata();

	ibm440gx_tah_enable();

	/*
	 * Determine various clocks.
	 * To be completely correct we should get SysClk
	 * from FPGA, because it can be changed by on-board switches
	 * --ebs
	 */
	ibm440gx_get_clocks(&clocks, 33300000, 6 * 1843200);
	ocp_sys_info.opb_bus_freq = clocks.opb;

	/* Setup TODC access */
	TODC_INIT(TODC_TYPE_DS1743,
			0,
			0,
			ioremap64(OCOTEA_RTC_ADDR, OCOTEA_RTC_SIZE),
			8);

	/* init to some ~sane value until calibrate_delay() runs */
        loops_per_jiffy = 50000000/HZ;

	/* Setup PCI host bridge */
	ocotea_setup_hose();

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

	ocotea_early_serial_map();

	/* Identify the system */
	printk("IBM Ocotea port (MontaVista Software, Inc. <source@mvista.com>)\n");
}

static void __init ocotea_init(void)
{
	ibm440gx_l2c_setup(&clocks);
}

void __init platform_init(unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, unsigned long r7)
{
	ibm440gx_platform_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = ocotea_setup_arch;
	ppc_md.show_cpuinfo = ocotea_show_cpuinfo;
	ppc_md.get_irq = NULL;		/* Set in ppc4xx_pic_init() */

	ppc_md.calibrate_decr = ocotea_calibrate_decr;
	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;

	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;
#ifdef CONFIG_KGDB
	ppc_md.early_serial_map = ocotea_early_serial_map;
#endif
	ppc_md.init = ocotea_init;
}
