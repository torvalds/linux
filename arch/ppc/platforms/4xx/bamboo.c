/*
 * Bamboo board specific routines
 *
 * Wade Farnsworth <wfarnsworth@mvista.com>
 * Copyright 2004 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
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
#include <linux/ethtool.h>

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

#include <syslib/gen550.h>
#include <syslib/ibm440gx_common.h>

extern bd_t __res;

static struct ibm44x_clocks clocks __initdata;

/*
 * Bamboo external IRQ triggering/polarity settings
 */
unsigned char ppc4xx_uic_ext_irq_cfg[] __initdata = {
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* IRQ0: Ethernet transceiver */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE), /* IRQ1: Expansion connector */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* IRQ2: PCI slot 0 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* IRQ3: PCI slot 1 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* IRQ4: PCI slot 2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* IRQ5: PCI slot 3 */
	(IRQ_SENSE_EDGE  | IRQ_POLARITY_NEGATIVE), /* IRQ6: SMI pushbutton */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* IRQ7: EXT */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* IRQ8: EXT */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* IRQ9: EXT */
};

static void __init
bamboo_calibrate_decr(void)
{
	unsigned int freq;

	if (mfspr(SPRN_CCR1) & CCR1_TCS)
		freq = BAMBOO_TMRCLK;
	else
		freq = clocks.cpu;

	ibm44x_calibrate_decr(freq);

}

static int
bamboo_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: IBM\n");
	seq_printf(m, "machine\t\t: PPC440EP EVB (Bamboo)\n");

	return 0;
}

static inline int
bamboo_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *	PCI IDSEL/INTPIN->INTLINE
	 * 	   A   B   C   D
	 */
	{
		{ 28, 28, 28, 28 },	/* IDSEL 1 - PCI Slot 0 */
		{ 27, 27, 27, 27 },	/* IDSEL 2 - PCI Slot 1 */
		{ 26, 26, 26, 26 },	/* IDSEL 3 - PCI Slot 2 */
		{ 25, 25, 25, 25 },	/* IDSEL 4 - PCI Slot 3 */
	};

	const long min_idsel = 1, max_idsel = 4, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

static void __init bamboo_set_emacdata(void)
{
	u8 * base_addr;
	struct ocp_def *def;
	struct ocp_func_emac_data *emacdata;
	u8 val;
	int mode;
	u32 excluded = 0;

	base_addr = ioremap64(BAMBOO_FPGA_SELECTION1_REG_ADDR, 16);
	val = readb(base_addr);
	iounmap((void *) base_addr);
	if (BAMBOO_SEL_MII(val))
		mode = PHY_MODE_MII;
	else if (BAMBOO_SEL_RMII(val))
		mode = PHY_MODE_RMII;
	else
		mode = PHY_MODE_SMII;

	/*
	 * SW2 on the Bamboo is used for ethernet configuration and is accessed
	 * via the CONFIG2 register in the FPGA.  If the ANEG pin is set,
	 * overwrite the supported features with the settings in SW2.
	 *
	 * This is used as a workaround for the improperly biased RJ-45 sockets
	 * on the Rev. 0 Bamboo.  By default only 10baseT is functional.
	 * Removing inductors L17 and L18 from the board allows 100baseT, but
	 * disables 10baseT.  The Rev. 1 has no such limitations.
	 */

	base_addr = ioremap64(BAMBOO_FPGA_CONFIG2_REG_ADDR, 8);
	val = readb(base_addr);
	iounmap((void *) base_addr);
	if (!BAMBOO_AUTONEGOTIATE(val)) {
		excluded |= SUPPORTED_Autoneg;
		if (BAMBOO_FORCE_100Mbps(val)) {
			excluded |= SUPPORTED_10baseT_Full;
			excluded |= SUPPORTED_10baseT_Half;
			if (BAMBOO_FULL_DUPLEX_EN(val))
				excluded |= SUPPORTED_100baseT_Half;
			else
				excluded |= SUPPORTED_100baseT_Full;
		} else {
			excluded |= SUPPORTED_100baseT_Full;
			excluded |= SUPPORTED_100baseT_Half;
			if (BAMBOO_FULL_DUPLEX_EN(val))
				excluded |= SUPPORTED_10baseT_Half;
			else
				excluded |= SUPPORTED_10baseT_Full;
		}
	}

	/* Set mac_addr, phy mode and unsupported phy features for each EMAC */

	def = ocp_get_one_device(OCP_VENDOR_IBM, OCP_FUNC_EMAC, 0);
	emacdata = def->additions;
	memcpy(emacdata->mac_addr, __res.bi_enetaddr, 6);
	emacdata->phy_mode = mode;
	emacdata->phy_feat_exc = excluded;

	def = ocp_get_one_device(OCP_VENDOR_IBM, OCP_FUNC_EMAC, 1);
	emacdata = def->additions;
	memcpy(emacdata->mac_addr, __res.bi_enet1addr, 6);
	emacdata->phy_mode = mode;
	emacdata->phy_feat_exc = excluded;
}

static int
bamboo_exclude_device(unsigned char bus, unsigned char devfn)
{
	return (bus == 0 && devfn == 0);
}

#define PCI_READW(offset) \
        (readw((void *)((u32)pci_reg_base+offset)))

#define PCI_WRITEW(value, offset) \
	(writew(value, (void *)((u32)pci_reg_base+offset)))

#define PCI_WRITEL(value, offset) \
	(writel(value, (void *)((u32)pci_reg_base+offset)))

static void __init
bamboo_setup_pci(void)
{
	void *pci_reg_base;
	unsigned long memory_size;
	memory_size = ppc_md.find_end_of_memory();

	pci_reg_base = ioremap64(BAMBOO_PCIL0_BASE, BAMBOO_PCIL0_SIZE);

	/* Enable PCI I/O, Mem, and Busmaster cycles */
	PCI_WRITEW(PCI_READW(PCI_COMMAND) |
		   PCI_COMMAND_MEMORY |
		   PCI_COMMAND_MASTER, PCI_COMMAND);

	/* Disable region first */
	PCI_WRITEL(0, BAMBOO_PCIL0_PMM0MA);

	/* PLB starting addr: 0x00000000A0000000 */
	PCI_WRITEL(BAMBOO_PCI_PHY_MEM_BASE, BAMBOO_PCIL0_PMM0LA);

	/* PCI start addr, 0xA0000000 (PCI Address) */
	PCI_WRITEL(BAMBOO_PCI_MEM_BASE, BAMBOO_PCIL0_PMM0PCILA);
	PCI_WRITEL(0, BAMBOO_PCIL0_PMM0PCIHA);

	/* Enable no pre-fetch, enable region */
	PCI_WRITEL(((0xffffffff -
		     (BAMBOO_PCI_UPPER_MEM - BAMBOO_PCI_MEM_BASE)) | 0x01),
		      BAMBOO_PCIL0_PMM0MA);

	/* Disable region one */
	PCI_WRITEL(0, BAMBOO_PCIL0_PMM1MA);
	PCI_WRITEL(0, BAMBOO_PCIL0_PMM1LA);
	PCI_WRITEL(0, BAMBOO_PCIL0_PMM1PCILA);
	PCI_WRITEL(0, BAMBOO_PCIL0_PMM1PCIHA);
	PCI_WRITEL(0, BAMBOO_PCIL0_PMM1MA);

	/* Disable region two */
	PCI_WRITEL(0, BAMBOO_PCIL0_PMM2MA);
	PCI_WRITEL(0, BAMBOO_PCIL0_PMM2LA);
	PCI_WRITEL(0, BAMBOO_PCIL0_PMM2PCILA);
	PCI_WRITEL(0, BAMBOO_PCIL0_PMM2PCIHA);
	PCI_WRITEL(0, BAMBOO_PCIL0_PMM2MA);

	/* Now configure the PCI->PLB windows, we only use PTM1
	 *
	 * For Inbound flow, set the window size to all available memory
	 * This is required because if size is smaller,
	 * then Eth/PCI DD would fail as PCI card not able to access
	 * the memory allocated by DD.
	 */

	PCI_WRITEL(0, BAMBOO_PCIL0_PTM1MS);	/* disabled region 1 */
	PCI_WRITEL(0, BAMBOO_PCIL0_PTM1LA);	/* begin of address map */

	memory_size = 1 << fls(memory_size - 1);

	/* Size low + Enabled */
	PCI_WRITEL((0xffffffff - (memory_size - 1)) | 0x1, BAMBOO_PCIL0_PTM1MS);

	eieio();
	iounmap(pci_reg_base);
}

static void __init
bamboo_setup_hose(void)
{
	unsigned int bar_response, bar;
	struct pci_controller *hose;

	bamboo_setup_pci();

	hose = pcibios_alloc_controller();

	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;

	hose->pci_mem_offset = BAMBOO_PCI_MEM_OFFSET;

	pci_init_resource(&hose->io_resource,
			BAMBOO_PCI_LOWER_IO,
			BAMBOO_PCI_UPPER_IO,
			IORESOURCE_IO,
			"PCI host bridge");

	pci_init_resource(&hose->mem_resources[0],
			BAMBOO_PCI_LOWER_MEM,
			BAMBOO_PCI_UPPER_MEM,
			IORESOURCE_MEM,
			"PCI host bridge");

	ppc_md.pci_exclude_device = bamboo_exclude_device;

	hose->io_space.start = BAMBOO_PCI_LOWER_IO;
	hose->io_space.end = BAMBOO_PCI_UPPER_IO;
	hose->mem_space.start = BAMBOO_PCI_LOWER_MEM;
	hose->mem_space.end = BAMBOO_PCI_UPPER_MEM;
	isa_io_base =
		(unsigned long)ioremap64(BAMBOO_PCI_IO_BASE, BAMBOO_PCI_IO_SIZE);
	hose->io_base_virt = (void *)isa_io_base;

	setup_indirect_pci(hose,
			BAMBOO_PCI_CFGA_PLB32,
			BAMBOO_PCI_CFGD_PLB32);
	hose->set_cfg_type = 1;

	/* Zero config bars */
	for (bar = PCI_BASE_ADDRESS_1; bar <= PCI_BASE_ADDRESS_2; bar += 4) {
		early_write_config_dword(hose, hose->first_busno,
					 PCI_FUNC(hose->first_busno), bar,
					 0x00000000);
		early_read_config_dword(hose, hose->first_busno,
					PCI_FUNC(hose->first_busno), bar,
					&bar_response);
	}

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = bamboo_map_irq;
}

TODC_ALLOC();

static void __init
bamboo_early_serial_map(void)
{
	struct uart_port port;

	/* Setup ioremapped serial port access */
	memset(&port, 0, sizeof(port));
	port.membase = ioremap64(PPC440EP_UART0_ADDR, 8);
	port.irq = 0;
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
#endif

	port.membase = ioremap64(PPC440EP_UART1_ADDR, 8);
	port.irq = 1;
	port.uartclk = clocks.uart1;
	port.line = 1;

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 1 failed\n");
	}

#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
	/* Configure debug serial access */
	gen550_init(1, &port);
#endif

	port.membase = ioremap64(PPC440EP_UART2_ADDR, 8);
	port.irq = 3;
	port.uartclk = clocks.uart2;
	port.line = 2;

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 2 failed\n");
	}

#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
	/* Configure debug serial access */
	gen550_init(2, &port);
#endif

	port.membase = ioremap64(PPC440EP_UART3_ADDR, 8);
	port.irq = 4;
	port.uartclk = clocks.uart3;
	port.line = 3;

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 3 failed\n");
	}
}

static void __init
bamboo_setup_arch(void)
{

	bamboo_set_emacdata();

	ibm440gx_get_clocks(&clocks, 33333333, 6 * 1843200);
	ocp_sys_info.opb_bus_freq = clocks.opb;

	/* Setup TODC access */
	TODC_INIT(TODC_TYPE_DS1743,
			0,
			0,
			ioremap64(BAMBOO_RTC_ADDR, BAMBOO_RTC_SIZE),
			8);

	/* init to some ~sane value until calibrate_delay() runs */
        loops_per_jiffy = 50000000/HZ;

	/* Setup PCI host bridge */
	bamboo_setup_hose();

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

	bamboo_early_serial_map();

	/* Identify the system */
	printk("IBM Bamboo port (MontaVista Software, Inc. (source@mvista.com))\n");
}

void __init platform_init(unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, unsigned long r7)
{
	ibm44x_platform_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = bamboo_setup_arch;
	ppc_md.show_cpuinfo = bamboo_show_cpuinfo;
	ppc_md.get_irq = NULL;		/* Set in ppc4xx_pic_init() */

	ppc_md.calibrate_decr = bamboo_calibrate_decr;
	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;

	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;
#ifdef CONFIG_KGDB
	ppc_md.early_serial_map = bamboo_early_serial_map;
#endif
}

