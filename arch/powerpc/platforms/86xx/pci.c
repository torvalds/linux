/*
 * MPC86XX pci setup code
 *
 * Recode: ZHANG WEI <wei.zhang@freescale.com>
 * Initial author: Xianghua Xiao <x.xiao@freescale.com>
 *
 * Copyright 2006 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/serial.h>

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pcie.h>

#include "mpc86xx.h"

#undef DEBUG

#ifdef DEBUG
#define DBG(fmt, args...) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ## args)
#else
#define DBG(fmt, args...)
#endif

struct pcie_outbound_window_regs {
	uint    pexotar;               /* 0x.0 - PCI Express outbound translation address register */
	uint    pexotear;              /* 0x.4 - PCI Express outbound translation extended address register */
	uint    pexowbar;              /* 0x.8 - PCI Express outbound window base address register */
	char    res1[4];
	uint    pexowar;               /* 0x.10 - PCI Express outbound window attributes register */
	char    res2[12];
};

struct pcie_inbound_window_regs {
	uint    pexitar;               /* 0x.0 - PCI Express inbound translation address register */
	char    res1[4];
	uint    pexiwbar;              /* 0x.8 - PCI Express inbound window base address register */
	uint    pexiwbear;             /* 0x.c - PCI Express inbound window base extended address register */
	uint    pexiwar;               /* 0x.10 - PCI Express inbound window attributes register */
	char    res2[12];
};

static void __init setup_pcie_atmu(struct pci_controller *hose, struct resource *rsrc)
{
	volatile struct ccsr_pex *pcie;
	volatile struct pcie_outbound_window_regs *pcieow;
	volatile struct pcie_inbound_window_regs *pcieiw;
	int i = 0;

	DBG("PCIE memory map start 0x%x, size 0x%x\n", rsrc->start,
			rsrc->end - rsrc->start + 1);
	pcie = ioremap(rsrc->start, rsrc->end - rsrc->start + 1);

	/* Disable all windows (except pexowar0 since its ignored) */
	pcie->pexowar1 = 0;
	pcie->pexowar2 = 0;
 	pcie->pexowar3 = 0;
 	pcie->pexowar4 = 0;
 	pcie->pexiwar1 = 0;
 	pcie->pexiwar2 = 0;
 	pcie->pexiwar3 = 0;

 	pcieow = (struct pcie_outbound_window_regs *)&pcie->pexotar1;
 	pcieiw = (struct pcie_inbound_window_regs *)&pcie->pexitar1;

 	/* Setup outbound MEM window */
 	for(i = 0; i < 3; i++)
 		if (hose->mem_resources[i].flags & IORESOURCE_MEM){
 			DBG("PCIE MEM resource start 0x%08x, size 0x%08x.\n",
 				hose->mem_resources[i].start,
 				hose->mem_resources[i].end
 				  - hose->mem_resources[i].start + 1);
 			pcieow->pexotar = (hose->mem_resources[i].start) >> 12
 				& 0x000fffff;
 			pcieow->pexotear = 0;
 			pcieow->pexowbar = (hose->mem_resources[i].start) >> 12
 				& 0x000fffff;
 			/* Enable, Mem R/W */
 			pcieow->pexowar = 0x80044000 |
 				(__ilog2(hose->mem_resources[i].end
 					 - hose->mem_resources[i].start + 1)
 				 - 1);
 			pcieow++;
 		}

 	/* Setup outbound IO window */
 	if (hose->io_resource.flags & IORESOURCE_IO){
 		DBG("PCIE IO resource start 0x%08x, size 0x%08x, phy base 0x%08x.\n",
 			hose->io_resource.start,
 			hose->io_resource.end - hose->io_resource.start + 1,
 			hose->io_base_phys);
 		pcieow->pexotar = (hose->io_resource.start) >> 12 & 0x000fffff;
 		pcieow->pexotear = 0;
 		pcieow->pexowbar = (hose->io_base_phys) >> 12 & 0x000fffff;
 		/* Enable, IO R/W */
 		pcieow->pexowar = 0x80088000 | (__ilog2(hose->io_resource.end
 					- hose->io_resource.start + 1) - 1);
 	}

 	/* Setup 2G inbound Memory Window @ 0 */
 	pcieiw->pexitar = 0x00000000;
 	pcieiw->pexiwbar = 0x00000000;
 	/* Enable, Prefetch, Local Mem, Snoop R/W, 2G */
 	pcieiw->pexiwar = 0xa0f5501e;
}

static void __init
mpc86xx_setup_pcie(struct pci_controller *hose, u32 pcie_offset, u32 pcie_size)
{
	u16 cmd;
	unsigned int temps;

	DBG("PCIE host controller register offset 0x%08x, size 0x%08x.\n",
			pcie_offset, pcie_size);

	early_read_config_word(hose, 0, 0, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_SERR | PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY
	    | PCI_COMMAND_IO;
	early_write_config_word(hose, 0, 0, PCI_COMMAND, cmd);

	early_write_config_byte(hose, 0, 0, PCI_LATENCY_TIMER, 0x80);

	/* PCIE Bus, Fix the MPC8641D host bridge's location to bus 0xFF. */
	early_read_config_dword(hose, 0, 0, PCI_PRIMARY_BUS, &temps);
	temps = (temps & 0xff000000) | (0xff) | (0x0 << 8) | (0xfe << 16);
	early_write_config_dword(hose, 0, 0, PCI_PRIMARY_BUS, temps);
}

int mpc86xx_exclude_device(u_char bus, u_char devfn)
{
	if (bus == 0 && PCI_SLOT(devfn) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

int __init add_bridge(struct device_node *dev)
{
	int len;
	struct pci_controller *hose;
	struct resource rsrc;
	const int *bus_range;
	int has_address = 0;
	int primary = 0;

	DBG("Adding PCIE host bridge %s\n", dev->full_name);

	/* Fetch host bridge registers address */
	has_address = (of_address_to_resource(dev, 0, &rsrc) == 0);

	/* Get bus range if any */
	bus_range = of_get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int))
		printk(KERN_WARNING "Can't get bus-range for %s, assume"
		       " bus 0\n", dev->full_name);

	hose = pcibios_alloc_controller();
	if (!hose)
		return -ENOMEM;
	hose->arch_data = dev;
	hose->set_cfg_type = 1;

	/* last_busno = 0xfe cause by MPC8641 PCIE bug */
	hose->first_busno = bus_range ? bus_range[0] : 0x0;
	hose->last_busno = bus_range ? bus_range[1] : 0xfe;

	setup_indirect_pcie(hose, rsrc.start, rsrc.start + 0x4);

	/* Setup the PCIE host controller. */
	mpc86xx_setup_pcie(hose, rsrc.start, rsrc.end - rsrc.start + 1);

	if ((rsrc.start & 0xfffff) == 0x8000)
		primary = 1;

	printk(KERN_INFO "Found MPC86xx PCIE host bridge at 0x%08lx. "
	       "Firmware bus number: %d->%d\n",
	       (unsigned long) rsrc.start,
	       hose->first_busno, hose->last_busno);

	DBG(" ->Hose at 0x%p, cfg_addr=0x%p,cfg_data=0x%p\n",
		hose, hose->cfg_addr, hose->cfg_data);

	/* Interpret the "ranges" property */
	/* This also maps the I/O region and sets isa_io/mem_base */
	pci_process_bridge_OF_ranges(hose, dev, primary);

	/* Setup PEX window registers */
	setup_pcie_atmu(hose, &rsrc);

	return 0;
}
