/*
 * Luan board specific routines
 *
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright 2004-2005 MontaVista Software Inc.
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

#include <syslib/ibm44x_common.h>
#include <syslib/ibm440gx_common.h>
#include <syslib/ibm440sp_common.h>

extern bd_t __res;

static struct ibm44x_clocks clocks __initdata;

static void __init
luan_calibrate_decr(void)
{
	unsigned int freq;

	if (mfspr(SPRN_CCR1) & CCR1_TCS)
		freq = LUAN_TMR_CLK;
	else
		freq = clocks.cpu;

	ibm44x_calibrate_decr(freq);
}

static int
luan_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: IBM\n");
	seq_printf(m, "machine\t\t: PPC440SP EVB (Luan)\n");

	return 0;
}

static inline int
luan_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	struct pci_controller *hose = pci_bus_to_hose(dev->bus->number);

	/* PCIX0 in adapter mode, no host interrupt routing */

	/* PCIX1 */
	if (hose->index == 0) {
		static char pci_irq_table[][4] =
		/*
		 *	PCI IDSEL/INTPIN->INTLINE
		 *	  A   B   C   D
		 */
		{
			{ 49, 49, 49, 49 },	/* IDSEL 1 - PCIX1 Slot 0 */
			{ 49, 49, 49, 49 },	/* IDSEL 2 - PCIX1 Slot 1 */
			{ 49, 49, 49, 49 },	/* IDSEL 3 - PCIX1 Slot 2 */
			{ 49, 49, 49, 49 },	/* IDSEL 4 - PCIX1 Slot 3 */
		};
		const long min_idsel = 1, max_idsel = 4, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	/* PCIX2 */
	} else if (hose->index == 1) {
		static char pci_irq_table[][4] =
		/*
		 *	PCI IDSEL/INTPIN->INTLINE
		 *	  A   B   C   D
		 */
		{
			{ 50, 50, 50, 50 },	/* IDSEL 1 - PCIX2 Slot 0 */
			{ 50, 50, 50, 50 },	/* IDSEL 2 - PCIX2 Slot 1 */
			{ 50, 50, 50, 50 },	/* IDSEL 3 - PCIX2 Slot 2 */
			{ 50, 50, 50, 50 },	/* IDSEL 4 - PCIX2 Slot 3 */
		};
		const long min_idsel = 1, max_idsel = 4, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	}
	return -1;
}

static void __init luan_set_emacdata(void)
{
	struct ocp_def *def;
	struct ocp_func_emac_data *emacdata;

	/* Set phy_map, phy_mode, and mac_addr for the EMAC */
	def = ocp_get_one_device(OCP_VENDOR_IBM, OCP_FUNC_EMAC, 0);
	emacdata = def->additions;
	emacdata->phy_map = 0x00000001;	/* Skip 0x00 */
	emacdata->phy_mode = PHY_MODE_GMII;
	memcpy(emacdata->mac_addr, __res.bi_enetaddr, 6);
}

#define PCIX_READW(offset) \
	(readw((void *)((u32)pcix_reg_base+offset)))

#define PCIX_WRITEW(value, offset) \
	(writew(value, (void *)((u32)pcix_reg_base+offset)))

#define PCIX_WRITEL(value, offset) \
	(writel(value, (void *)((u32)pcix_reg_base+offset)))

static void __init
luan_setup_pcix(void)
{
	int i;
	void *pcix_reg_base;

	for (i=0;i<3;i++) {
		pcix_reg_base = ioremap64(PCIX0_REG_BASE + i*PCIX_REG_OFFSET, PCIX_REG_SIZE);

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

		/*
		 * Setup 512MB PLB->PCI outbound mem window
		 * (a_n000_0000->0_n000_0000)
		 * */
		PCIX_WRITEL(0x0000000a, PCIX0_POM0LAH);
		PCIX_WRITEL(0x80000000 | i*LUAN_PCIX_MEM_SIZE, PCIX0_POM0LAL);
		PCIX_WRITEL(0x00000000, PCIX0_POM0PCIAH);
		PCIX_WRITEL(0x80000000 | i*LUAN_PCIX_MEM_SIZE, PCIX0_POM0PCIAL);
		PCIX_WRITEL(0xe0000001, PCIX0_POM0SA);

		/* Setup 2GB PCI->PLB inbound memory window at 0, enable MSIs */
		PCIX_WRITEL(0x00000000, PCIX0_PIM0LAH);
		PCIX_WRITEL(0x00000000, PCIX0_PIM0LAL);
		PCIX_WRITEL(0xe0000007, PCIX0_PIM0SA);
		PCIX_WRITEL(0xffffffff, PCIX0_PIM0SAH);

		iounmap(pcix_reg_base);
	}

	eieio();
}

static void __init
luan_setup_hose(struct pci_controller *hose,
		int lower_mem,
		int upper_mem,
		int cfga,
		int cfgd,
		u64 pcix_io_base)
{
	char name[20];

	sprintf(name, "PCIX%d host bridge", hose->index);

	hose->pci_mem_offset = LUAN_PCIX_MEM_OFFSET;

	pci_init_resource(&hose->io_resource,
			LUAN_PCIX_LOWER_IO,
			LUAN_PCIX_UPPER_IO,
			IORESOURCE_IO,
			name);

	pci_init_resource(&hose->mem_resources[0],
			lower_mem,
			upper_mem,
			IORESOURCE_MEM,
			name);

	hose->io_space.start = LUAN_PCIX_LOWER_IO;
	hose->io_space.end = LUAN_PCIX_UPPER_IO;
	hose->mem_space.start = lower_mem;
	hose->mem_space.end = upper_mem;
	hose->io_base_virt = ioremap64(pcix_io_base, PCIX_IO_SIZE);
	isa_io_base = (unsigned long) hose->io_base_virt;

	setup_indirect_pci(hose, cfga, cfgd);
	hose->set_cfg_type = 1;
}

static void __init
luan_setup_hoses(void)
{
	struct pci_controller *hose1, *hose2;

	/* Configure windows on the PCI-X host bridge */
	luan_setup_pcix();

	/* Allocate hoses for PCIX1 and PCIX2 */
	hose1 = pcibios_alloc_controller();
	hose2 = pcibios_alloc_controller();
	if (!hose1 || !hose2)
		return;

	/* Setup PCIX1 */
	hose1->first_busno = 0;
	hose1->last_busno = 0xff;

	luan_setup_hose(hose1,
			LUAN_PCIX1_LOWER_MEM,
			LUAN_PCIX1_UPPER_MEM,
			PCIX1_CFGA,
			PCIX1_CFGD,
			PCIX1_IO_BASE);

	hose1->last_busno = pciauto_bus_scan(hose1, hose1->first_busno);

	/* Setup PCIX2 */
	hose2->first_busno = hose1->last_busno + 1;
	hose2->last_busno = 0xff;

	luan_setup_hose(hose2,
			LUAN_PCIX2_LOWER_MEM,
			LUAN_PCIX2_UPPER_MEM,
			PCIX2_CFGA,
			PCIX2_CFGD,
			PCIX2_IO_BASE);

	hose2->last_busno = pciauto_bus_scan(hose2, hose2->first_busno);

	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = luan_map_irq;
}

TODC_ALLOC();

static void __init
luan_early_serial_map(void)
{
	struct uart_port port;

	/* Setup ioremapped serial port access */
	memset(&port, 0, sizeof(port));
	port.membase = ioremap64(PPC440SP_UART0_ADDR, 8);
	port.irq = UART0_INT;
	port.uartclk = clocks.uart0;
	port.regshift = 0;
	port.iotype = UPIO_MEM;
	port.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST;
	port.line = 0;

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 0 failed\n");
	}

	port.membase = ioremap64(PPC440SP_UART1_ADDR, 8);
	port.irq = UART1_INT;
	port.uartclk = clocks.uart1;
	port.line = 1;

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 1 failed\n");
	}

	port.membase = ioremap64(PPC440SP_UART2_ADDR, 8);
	port.irq = UART2_INT;
	port.uartclk = BASE_BAUD;
	port.line = 2;

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 2 failed\n");
	}
}

static void __init
luan_setup_arch(void)
{
	luan_set_emacdata();

#if !defined(CONFIG_BDI_SWITCH)
	/*
	 * The Abatron BDI JTAG debugger does not tolerate others
	 * mucking with the debug registers.
	 */
        mtspr(SPRN_DBCR0, (DBCR0_TDE | DBCR0_IDM));
#endif

	/*
	 * Determine various clocks.
	 * To be completely correct we should get SysClk
	 * from FPGA, because it can be changed by on-board switches
	 * --ebs
	 */
	/* 440GX and 440SP clocking is the same -mdp */
	ibm440gx_get_clocks(&clocks, 33333333, 6 * 1843200);
	ocp_sys_info.opb_bus_freq = clocks.opb;

	/* init to some ~sane value until calibrate_delay() runs */
        loops_per_jiffy = 50000000/HZ;

	/* Setup PCIXn host bridges */
	luan_setup_hoses();

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

	luan_early_serial_map();

	/* Identify the system */
	printk("Luan port (MontaVista Software, Inc. <source@mvista.com>)\n");
}

void __init platform_init(unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, unsigned long r7)
{
	ibm44x_platform_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = luan_setup_arch;
	ppc_md.show_cpuinfo = luan_show_cpuinfo;
	ppc_md.find_end_of_memory = ibm440sp_find_end_of_memory;
	ppc_md.get_irq = NULL;		/* Set in ppc4xx_pic_init() */

	ppc_md.calibrate_decr = luan_calibrate_decr;
#ifdef CONFIG_KGDB
	ppc_md.early_serial_map = luan_early_serial_map;
#endif
}
