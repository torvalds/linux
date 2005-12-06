/*
 * arch/ppc/platforms/4xx/yucca.c
 *
 * Yucca board specific routines
 *
 * Roland Dreier <rolandd@cisco.com> (based on luan.c by Matt Porter)
 *
 * Copyright 2004-2005 MontaVista Software Inc.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
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
#include <syslib/ppc440spe_pcie.h>

extern bd_t __res;

static struct ibm44x_clocks clocks __initdata;

static void __init
yucca_calibrate_decr(void)
{
	unsigned int freq;

	if (mfspr(SPRN_CCR1) & CCR1_TCS)
		freq = YUCCA_TMR_CLK;
	else
		freq = clocks.cpu;

	ibm44x_calibrate_decr(freq);
}

static int
yucca_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: AMCC\n");
	seq_printf(m, "machine\t\t: PPC440SPe EVB (Yucca)\n");

	return 0;
}

static enum {
	HOSE_UNKNOWN,
	HOSE_PCIX,
	HOSE_PCIE0,
	HOSE_PCIE1,
	HOSE_PCIE2
} hose_type[4];

static inline int
yucca_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	struct pci_controller *hose = pci_bus_to_hose(dev->bus->number);

	if (hose_type[hose->index] == HOSE_PCIX) {
		static char pci_irq_table[][4] =
		/*
		 *	PCI IDSEL/INTPIN->INTLINE
		 *	  A   B   C   D
		 */
		{
			{ 81, -1, -1, -1 },	/* IDSEL 1 - PCIX0 Slot 0 */
		};
		const long min_idsel = 1, max_idsel = 1, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	} else if (hose_type[hose->index] == HOSE_PCIE0) {
		static char pci_irq_table[][4] =
		/*
		 *	PCI IDSEL/INTPIN->INTLINE
		 *	  A   B   C   D
		 */
		{
			{ 96, 97, 98, 99 },
		};
		const long min_idsel = 1, max_idsel = 1, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	} else if (hose_type[hose->index] == HOSE_PCIE1) {
		static char pci_irq_table[][4] =
		/*
		 *	PCI IDSEL/INTPIN->INTLINE
		 *	  A   B   C   D
		 */
		{
			{ 100, 101, 102, 103 },
		};
		const long min_idsel = 1, max_idsel = 1, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	} else if (hose_type[hose->index] == HOSE_PCIE2) {
		static char pci_irq_table[][4] =
		/*
		 *	PCI IDSEL/INTPIN->INTLINE
		 *	  A   B   C   D
		 */
		{
			{ 104, 105, 106, 107 },
		};
		const long min_idsel = 1, max_idsel = 1, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	}
	return -1;
}

static void __init yucca_set_emacdata(void)
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

static int __init yucca_pcie_card_present(int port)
{
   void __iomem *pcie_fpga_base;
   u16 reg;

   pcie_fpga_base = ioremap64(YUCCA_FPGA_REG_BASE, YUCCA_FPGA_REG_SIZE);
   reg = in_be16(pcie_fpga_base + FPGA_REG1C);
   iounmap(pcie_fpga_base);

   switch(port) {
   case 0: return !(reg & FPGA_REG1C_PE0_PRSNT);
   case 1: return !(reg & FPGA_REG1C_PE1_PRSNT);
   case 2: return !(reg & FPGA_REG1C_PE2_PRSNT);
   default: return 0;
   }
}

/*
 * For the given slot, set rootpoint mode, send power to the slot,
 * turn on the green LED and turn off the yellow LED, enable the clock
 * and turn off reset.
 */
static void __init yucca_setup_pcie_fpga_rootpoint(int port)
{
	void __iomem *pcie_reg_fpga_base;
	u16 power, clock, green_led, yellow_led, reset_off, rootpoint, endpoint;

	pcie_reg_fpga_base = ioremap64(YUCCA_FPGA_REG_BASE, YUCCA_FPGA_REG_SIZE);

	switch(port) {
	case 0:
		rootpoint   = FPGA_REG1C_PE0_ROOTPOINT;
		endpoint    = 0;
		power 	    = FPGA_REG1A_PE0_PWRON;
		green_led   = FPGA_REG1A_PE0_GLED;
		clock 	    = FPGA_REG1A_PE0_REFCLK_ENABLE;
		yellow_led  = FPGA_REG1A_PE0_YLED;
		reset_off   = FPGA_REG1C_PE0_PERST;
		break;
	case 1:
		rootpoint   = 0;
		endpoint    = FPGA_REG1C_PE1_ENDPOINT;
		power 	    = FPGA_REG1A_PE1_PWRON;
		green_led   = FPGA_REG1A_PE1_GLED;
		clock 	    = FPGA_REG1A_PE1_REFCLK_ENABLE;
		yellow_led  = FPGA_REG1A_PE1_YLED;
		reset_off   = FPGA_REG1C_PE1_PERST;
		break;
	case 2:
		rootpoint   = 0;
		endpoint    = FPGA_REG1C_PE2_ENDPOINT;
		power 	    = FPGA_REG1A_PE2_PWRON;
		green_led   = FPGA_REG1A_PE2_GLED;
		clock 	    = FPGA_REG1A_PE2_REFCLK_ENABLE;
		yellow_led  = FPGA_REG1A_PE2_YLED;
		reset_off   = FPGA_REG1C_PE2_PERST;
		break;

	default:
		return;
	}

	out_be16(pcie_reg_fpga_base + FPGA_REG1A,
		 ~(power | clock | green_led) &
		 (yellow_led | in_be16(pcie_reg_fpga_base + FPGA_REG1A)));
	out_be16(pcie_reg_fpga_base + FPGA_REG1C,
		 ~(endpoint | reset_off) &
		 (rootpoint | in_be16(pcie_reg_fpga_base + FPGA_REG1C)));

	/*
	 * Leave device in reset for a while after powering on the
	 * slot to give it a chance to initialize.
	 */
	mdelay(250);

	out_be16(pcie_reg_fpga_base + FPGA_REG1C,
		 reset_off | in_be16(pcie_reg_fpga_base + FPGA_REG1C));

	iounmap(pcie_reg_fpga_base);
}

static void __init
yucca_setup_hoses(void)
{
	struct pci_controller *hose;
	char name[20];
	int i;

	if (0 && ppc440spe_init_pcie()) {
		printk(KERN_WARNING "PPC440SPe PCI Express initialization failed\n");
		return;
	}

	for (i = 0; i <= 2; ++i) {
		if (!yucca_pcie_card_present(i))
			continue;

		printk(KERN_INFO "PCIE%d: card present\n", i);
		yucca_setup_pcie_fpga_rootpoint(i);
		if (ppc440spe_init_pcie_rootport(i)) {
			printk(KERN_WARNING "PCIE%d: initialization failed\n", i);
			continue;
		}

		hose = pcibios_alloc_controller();
		if (!hose)
			return;

		sprintf(name, "PCIE%d host bridge", i);
		pci_init_resource(&hose->io_resource,
				  YUCCA_PCIX_LOWER_IO,
				  YUCCA_PCIX_UPPER_IO,
				  IORESOURCE_IO,
				  name);

		hose->mem_space.start = YUCCA_PCIE_LOWER_MEM +
			i * YUCCA_PCIE_MEM_SIZE;
		hose->mem_space.end   = hose->mem_space.start +
			YUCCA_PCIE_MEM_SIZE - 1;

		pci_init_resource(&hose->mem_resources[0],
				  hose->mem_space.start,
				  hose->mem_space.end,
				  IORESOURCE_MEM,
				  name);

		hose->first_busno = 0;
		hose->last_busno  = 15;
		hose_type[hose->index] = HOSE_PCIE0 + i;

		ppc440spe_setup_pcie(hose, i);
		hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);
	}

	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = yucca_map_irq;
}

TODC_ALLOC();

static void __init
yucca_early_serial_map(void)
{
	struct uart_port port;

	/* Setup ioremapped serial port access */
	memset(&port, 0, sizeof(port));
	port.membase = ioremap64(PPC440SPE_UART0_ADDR, 8);
	port.irq = UART0_INT;
	port.uartclk = clocks.uart0;
	port.regshift = 0;
	port.iotype = SERIAL_IO_MEM;
	port.flags = ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST;
	port.line = 0;

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 0 failed\n");
	}

	port.membase = ioremap64(PPC440SPE_UART1_ADDR, 8);
	port.irq = UART1_INT;
	port.uartclk = clocks.uart1;
	port.line = 1;

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 1 failed\n");
	}

	port.membase = ioremap64(PPC440SPE_UART2_ADDR, 8);
	port.irq = UART2_INT;
	port.uartclk = BASE_BAUD;
	port.line = 2;

	if (early_serial_setup(&port) != 0) {
		printk("Early serial init of port 2 failed\n");
	}
}

static void __init
yucca_setup_arch(void)
{
	yucca_set_emacdata();

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
	/* 440GX and 440SPe clocking is the same - rd */
	ibm440gx_get_clocks(&clocks, 33333333, 6 * 1843200);
	ocp_sys_info.opb_bus_freq = clocks.opb;

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000/HZ;

	/* Setup PCIXn host bridges */
	yucca_setup_hoses();

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

	yucca_early_serial_map();

	/* Identify the system */
	printk("Yucca port (Roland Dreier <rolandd@cisco.com>)\n");
}

void __init platform_init(unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, unsigned long r7)
{
	ibm44x_platform_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = yucca_setup_arch;
	ppc_md.show_cpuinfo = yucca_show_cpuinfo;
	ppc_md.find_end_of_memory = ibm440sp_find_end_of_memory;
	ppc_md.get_irq = NULL;		/* Set in ppc4xx_pic_init() */

	ppc_md.calibrate_decr = yucca_calibrate_decr;
#ifdef CONFIG_KGDB
	ppc_md.early_serial_map = yucca_early_serial_map;
#endif
}
