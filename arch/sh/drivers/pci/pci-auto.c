/*
 * PCI autoconfiguration library
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * Copyright 2000, 2001 MontaVista Software Inc.
 * Copyright 2001 Bradley D. LaRonde <brad@ltc.com>
 * Copyright 2003 Paul Mundt <lethal@linux-sh.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

/*
 * Modified for MIPS by Jun Sun, jsun@mvista.com
 *
 * . Simplify the interface between pci_auto and the rest: a single function.
 * . Assign resources from low address to upper address.
 * . change most int to u32.
 *
 * Further modified to include it as mips generic code, ppopov@mvista.com.
 *
 * 2001-10-26  Bradley D. LaRonde <brad@ltc.com>
 * - Add a top_bus argument to the "early config" functions so that
 *   they can set a fake parent bus pointer to convince the underlying
 *   pci ops to use type 1 configuration for sub busses.
 * - Set bridge base and limit registers correctly.
 * - Align io and memory base properly before and after bridge setup.
 * - Don't fall through to pci_setup_bars for bridge.
 * - Reformat the debug output to look more like lspci's output.
 *
 * Cloned for SuperH by M. R. Brown, mrbrown@0xd6.org
 *
 * 2003-08-05  Paul Mundt <lethal@linux-sh.org>
 * - Don't update the BAR values on systems that already have valid addresses
 *   and don't want these updated for whatever reason, by way of a new config
 *   option check. However, we still read in the old BAR values so that they
 *   can still be reported through the debug output.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>

#define	DEBUG
#ifdef	DEBUG
#define	DBG(x...)	printk(x)
#else
#define	DBG(x...)
#endif

/*
 * These functions are used early on before PCI scanning is done
 * and all of the pci_dev and pci_bus structures have been created.
 */
static struct pci_dev *fake_pci_dev(struct pci_channel *hose,
	int top_bus, int busnr, int devfn)
{
	static struct pci_dev dev;
	static struct pci_bus bus;

	dev.bus = &bus;
	dev.sysdata = hose;
	dev.devfn = devfn;
	bus.number = busnr;
	bus.ops = hose->pci_ops;
	bus.sysdata = hose;

	if(busnr != top_bus)
		/* Fake a parent bus structure. */
		bus.parent = &bus;
	else
		bus.parent = NULL;

	return &dev;
}

#define EARLY_PCI_OP(rw, size, type)					\
static int early_##rw##_config_##size(struct pci_channel *hose,		\
	int top_bus, int bus, int devfn, int offset, type value)	\
{									\
	return pci_##rw##_config_##size(				\
		fake_pci_dev(hose, top_bus, bus, devfn),		\
		offset, value);						\
}

EARLY_PCI_OP(read, byte, u8 *)
EARLY_PCI_OP(read, word, u16 *)
EARLY_PCI_OP(read, dword, u32 *)
EARLY_PCI_OP(write, byte, u8)
EARLY_PCI_OP(write, word, u16)
EARLY_PCI_OP(write, dword, u32)

static struct resource *io_resource_inuse;
static struct resource *mem_resource_inuse;

static u32 pciauto_lower_iospc;
static u32 pciauto_upper_iospc;

static u32 pciauto_lower_memspc;
static u32 pciauto_upper_memspc;

static void __init
pciauto_setup_bars(struct pci_channel *hose,
		   int top_bus,
		   int current_bus,
		   int pci_devfn,
		   int bar_limit)
{
	u32 bar_response, bar_size, bar_value;
	u32 bar, addr_mask, bar_nr = 0;
	u32 * upper_limit;
	u32 * lower_limit;
	int found_mem64 = 0;

	for (bar = PCI_BASE_ADDRESS_0; bar <= bar_limit; bar+=4) {
		u32 bar_addr;

		/* Read the old BAR value */
		early_read_config_dword(hose, top_bus,
					current_bus,
					pci_devfn,
					bar,
					&bar_addr);

		/* Tickle the BAR and get the response */
		early_write_config_dword(hose, top_bus,
					 current_bus,
					 pci_devfn,
					 bar,
					 0xffffffff);

		early_read_config_dword(hose, top_bus,
					current_bus,
					pci_devfn,
					bar,
					&bar_response);

		/*
		 * Write the old BAR value back out, only update the BAR
		 * if we implicitly want resources to be updated, which
		 * is done by the generic code further down. -- PFM.
		 */
		early_write_config_dword(hose, top_bus,
					 current_bus,
					 pci_devfn,
					 bar,
					 bar_addr);

		/* If BAR is not implemented go to the next BAR */
		if (!bar_response)
			continue;

		/*
		 * Workaround for a BAR that doesn't use its upper word,
		 * like the ALi 1535D+ PCI DC-97 Controller Modem (M5457).
		 * bdl <brad@ltc.com>
		 */
		if (!(bar_response & 0xffff0000))
			bar_response |= 0xffff0000;

retry:
		/* Check the BAR type and set our address mask */
		if (bar_response & PCI_BASE_ADDRESS_SPACE) {
			addr_mask = PCI_BASE_ADDRESS_IO_MASK;
			upper_limit = &pciauto_upper_iospc;
			lower_limit = &pciauto_lower_iospc;
			DBG("        I/O");
		} else {
			if ((bar_response & PCI_BASE_ADDRESS_MEM_TYPE_MASK) ==
			    PCI_BASE_ADDRESS_MEM_TYPE_64)
				found_mem64 = 1;

			addr_mask = PCI_BASE_ADDRESS_MEM_MASK;
			upper_limit = &pciauto_upper_memspc;
			lower_limit = &pciauto_lower_memspc;
			DBG("        Mem");
		}


		/* Calculate requested size */
		bar_size = ~(bar_response & addr_mask) + 1;

		/* Allocate a base address */
		bar_value = ((*lower_limit - 1) & ~(bar_size - 1)) + bar_size;

		if ((bar_value + bar_size) > *upper_limit) {
			if (bar_response & PCI_BASE_ADDRESS_SPACE) {
				if (io_resource_inuse->child) {
					io_resource_inuse =
						io_resource_inuse->child;
					pciauto_lower_iospc =
						io_resource_inuse->start;
					pciauto_upper_iospc =
						io_resource_inuse->end + 1;
					goto retry;
				}

			} else {
				if (mem_resource_inuse->child) {
					mem_resource_inuse =
						mem_resource_inuse->child;
					pciauto_lower_memspc =
						mem_resource_inuse->start;
					pciauto_upper_memspc =
						mem_resource_inuse->end + 1;
					goto retry;
				}
			}
			DBG(" unavailable -- skipping, value %x size %x\n",
					bar_value, bar_size);
			continue;
		}

		if (bar_value < *lower_limit || (bar_value + bar_size) >= *upper_limit) {
			DBG(" unavailable -- skipping, value %x size %x\n",
					bar_value, bar_size);
			continue;
		}

#ifdef CONFIG_PCI_AUTO_UPDATE_RESOURCES
		/* Write it out and update our limit */
		early_write_config_dword(hose, top_bus, current_bus, pci_devfn,
					 bar, bar_value);
#endif

		*lower_limit = bar_value + bar_size;

		/*
		 * If we are a 64-bit decoder then increment to the
		 * upper 32 bits of the bar and force it to locate
		 * in the lower 4GB of memory.
		 */
		if (found_mem64) {
			bar += 4;
			early_write_config_dword(hose, top_bus,
						 current_bus,
						 pci_devfn,
						 bar,
						 0x00000000);
		}

		DBG(" at 0x%.8x [size=0x%x]\n", bar_value, bar_size);

		bar_nr++;
	}

}

static void __init
pciauto_prescan_setup_bridge(struct pci_channel *hose,
			     int top_bus,
			     int current_bus,
			     int pci_devfn,
			     int sub_bus)
{
	/* Configure bus number registers */
	early_write_config_byte(hose, top_bus, current_bus, pci_devfn,
	                        PCI_PRIMARY_BUS, current_bus);
	early_write_config_byte(hose, top_bus, current_bus, pci_devfn,
				PCI_SECONDARY_BUS, sub_bus + 1);
	early_write_config_byte(hose, top_bus, current_bus, pci_devfn,
				PCI_SUBORDINATE_BUS, 0xff);

	/* Align memory and I/O to 1MB and 4KB boundaries. */
	pciauto_lower_memspc = (pciauto_lower_memspc + (0x100000 - 1))
		& ~(0x100000 - 1);
	pciauto_lower_iospc = (pciauto_lower_iospc + (0x1000 - 1))
		& ~(0x1000 - 1);

	/* Set base (lower limit) of address range behind bridge. */
	early_write_config_word(hose, top_bus, current_bus, pci_devfn,
		PCI_MEMORY_BASE, pciauto_lower_memspc >> 16);
	early_write_config_byte(hose, top_bus, current_bus, pci_devfn,
		PCI_IO_BASE, (pciauto_lower_iospc & 0x0000f000) >> 8);
	early_write_config_word(hose, top_bus, current_bus, pci_devfn,
		PCI_IO_BASE_UPPER16, pciauto_lower_iospc >> 16);

	/* We don't support prefetchable memory for now, so disable */
	early_write_config_word(hose, top_bus, current_bus, pci_devfn,
				PCI_PREF_MEMORY_BASE, 0);
	early_write_config_word(hose, top_bus, current_bus, pci_devfn,
				PCI_PREF_MEMORY_LIMIT, 0);
}

static void __init
pciauto_postscan_setup_bridge(struct pci_channel *hose,
			      int top_bus,
			      int current_bus,
			      int pci_devfn,
			      int sub_bus)
{
	u32 temp;

	/*
	 * [jsun] we always bump up baselines a little, so that if there
	 * nothing behind P2P bridge, we don't wind up overlapping IO/MEM
	 * spaces.
	 */
	pciauto_lower_memspc += 1;
	pciauto_lower_iospc += 1;

	/* Configure bus number registers */
	early_write_config_byte(hose, top_bus, current_bus, pci_devfn,
				PCI_SUBORDINATE_BUS, sub_bus);

	/* Set upper limit of address range behind bridge. */
	early_write_config_word(hose, top_bus, current_bus, pci_devfn,
		PCI_MEMORY_LIMIT, pciauto_lower_memspc >> 16);
	early_write_config_byte(hose, top_bus, current_bus, pci_devfn,
		PCI_IO_LIMIT, (pciauto_lower_iospc & 0x0000f000) >> 8);
	early_write_config_word(hose, top_bus, current_bus, pci_devfn,
		PCI_IO_LIMIT_UPPER16, pciauto_lower_iospc >> 16);

	/* Align memory and I/O to 1MB and 4KB boundaries. */
	pciauto_lower_memspc = (pciauto_lower_memspc + (0x100000 - 1))
		& ~(0x100000 - 1);
	pciauto_lower_iospc = (pciauto_lower_iospc + (0x1000 - 1))
		& ~(0x1000 - 1);

	/* Enable memory and I/O accesses, enable bus master */
	early_read_config_dword(hose, top_bus, current_bus, pci_devfn,
		PCI_COMMAND, &temp);
	early_write_config_dword(hose, top_bus, current_bus, pci_devfn,
		PCI_COMMAND, temp | PCI_COMMAND_IO | PCI_COMMAND_MEMORY
		| PCI_COMMAND_MASTER);
}

static void __init
pciauto_prescan_setup_cardbus_bridge(struct pci_channel *hose,
			int top_bus,
			int current_bus,
			int pci_devfn,
			int sub_bus)
{
	/* Configure bus number registers */
	early_write_config_byte(hose, top_bus, current_bus, pci_devfn,
				PCI_PRIMARY_BUS, current_bus);
	early_write_config_byte(hose, top_bus, current_bus, pci_devfn,
				PCI_SECONDARY_BUS, sub_bus + 1);
	early_write_config_byte(hose, top_bus, current_bus, pci_devfn,
				PCI_SUBORDINATE_BUS, 0xff);

	/* Align memory and I/O to 4KB and 4 byte boundaries. */
	pciauto_lower_memspc = (pciauto_lower_memspc + (0x1000 - 1))
		& ~(0x1000 - 1);
	pciauto_lower_iospc = (pciauto_lower_iospc + (0x4 - 1))
		& ~(0x4 - 1);

	early_write_config_dword(hose, top_bus, current_bus, pci_devfn,
		PCI_CB_MEMORY_BASE_0, pciauto_lower_memspc);
	early_write_config_dword(hose, top_bus, current_bus, pci_devfn,
		PCI_CB_IO_BASE_0, pciauto_lower_iospc);
}

static void __init
pciauto_postscan_setup_cardbus_bridge(struct pci_channel *hose,
			int top_bus,
			int current_bus,
			int pci_devfn,
			int sub_bus)
{
	u32 temp;

	/*
	 * [jsun] we always bump up baselines a little, so that if there
	 * nothing behind P2P bridge, we don't wind up overlapping IO/MEM
	 * spaces.
	 */
	pciauto_lower_memspc += 1;
	pciauto_lower_iospc += 1;

	/*
	 * Configure subordinate bus number.  The PCI subsystem
	 * bus scan will renumber buses (reserving three additional
	 * for this PCI<->CardBus bridge for the case where a CardBus
	 * adapter contains a P2P or CB2CB bridge.
	 */

	early_write_config_byte(hose, top_bus, current_bus, pci_devfn,
				PCI_SUBORDINATE_BUS, sub_bus);

	/*
	 * Reserve an additional 4MB for mem space and 16KB for
	 * I/O space.  This should cover any additional space
	 * requirement of unusual CardBus devices with
	 * additional bridges that can consume more address space.
	 *
	 * Although pcmcia-cs currently will reprogram bridge
	 * windows, the goal is to add an option to leave them
	 * alone and use the bridge window ranges as the regions
	 * that are searched for free resources upon hot-insertion
	 * of a device.  This will allow a PCI<->CardBus bridge
	 * configured by this routine to happily live behind a
	 * P2P bridge in a system.
	 */
	/* Align memory and I/O to 4KB and 4 byte boundaries. */
	pciauto_lower_memspc = (pciauto_lower_memspc + (0x1000 - 1))
		& ~(0x1000 - 1);
	pciauto_lower_iospc = (pciauto_lower_iospc + (0x4 - 1))
		& ~(0x4 - 1);
	/* Set up memory and I/O filter limits, assume 32-bit I/O space */
	early_write_config_dword(hose, top_bus, current_bus, pci_devfn,
		PCI_CB_MEMORY_LIMIT_0, pciauto_lower_memspc - 1);
	early_write_config_dword(hose, top_bus, current_bus, pci_devfn,
		PCI_CB_IO_LIMIT_0, pciauto_lower_iospc - 1);

	/* Enable memory and I/O accesses, enable bus master */
	early_read_config_dword(hose, top_bus, current_bus, pci_devfn,
		PCI_COMMAND, &temp);
	early_write_config_dword(hose, top_bus, current_bus, pci_devfn,
		PCI_COMMAND, temp | PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_MASTER);
}

#define	PCIAUTO_IDE_MODE_MASK		0x05

static int __init
pciauto_bus_scan(struct pci_channel *hose, int top_bus, int current_bus)
{
	int sub_bus;
	u32 pci_devfn, pci_class, cmdstat, found_multi=0;
	unsigned short vid, did;
	unsigned char header_type;
	int devfn_start = 0;
	int devfn_stop = 0xff;

	sub_bus = current_bus;

	if (hose->first_devfn)
		devfn_start = hose->first_devfn;
	if (hose->last_devfn)
		devfn_stop = hose->last_devfn;

	for (pci_devfn=devfn_start; pci_devfn<devfn_stop; pci_devfn++) {

		if (PCI_FUNC(pci_devfn) && !found_multi)
			continue;

		early_read_config_word(hose, top_bus, current_bus, pci_devfn,
				       PCI_VENDOR_ID, &vid);

		if (vid == 0xffff) continue;

		early_read_config_byte(hose, top_bus, current_bus, pci_devfn,
				       PCI_HEADER_TYPE, &header_type);

		if (!PCI_FUNC(pci_devfn))
			found_multi = header_type & 0x80;

		early_read_config_word(hose, top_bus, current_bus, pci_devfn,
				       PCI_DEVICE_ID, &did);

		early_read_config_dword(hose, top_bus, current_bus, pci_devfn,
					PCI_CLASS_REVISION, &pci_class);

		DBG("%.2x:%.2x.%x Class %.4x: %.4x:%.4x",
			current_bus, PCI_SLOT(pci_devfn), PCI_FUNC(pci_devfn),
			pci_class >> 16, vid, did);
		if (pci_class & 0xff)
			DBG(" (rev %.2x)", pci_class & 0xff);
		DBG("\n");

		if ((pci_class >> 16) == PCI_CLASS_BRIDGE_PCI) {
			DBG("        Bridge: primary=%.2x, secondary=%.2x\n",
				current_bus, sub_bus + 1);
			pciauto_prescan_setup_bridge(hose, top_bus, current_bus,
						     pci_devfn, sub_bus);
			DBG("Scanning sub bus %.2x, I/O 0x%.8x, Mem 0x%.8x\n",
				sub_bus + 1,
				pciauto_lower_iospc, pciauto_lower_memspc);
			sub_bus = pciauto_bus_scan(hose, top_bus, sub_bus+1);
			DBG("Back to bus %.2x\n", current_bus);
			pciauto_postscan_setup_bridge(hose, top_bus, current_bus,
							pci_devfn, sub_bus);
			continue;
		} else if ((pci_class >> 16) == PCI_CLASS_BRIDGE_CARDBUS) {
			DBG("  CARDBUS  Bridge: primary=%.2x, secondary=%.2x\n",
				current_bus, sub_bus + 1);
			DBG("PCI Autoconfig: Found CardBus bridge, device %d function %d\n", PCI_SLOT(pci_devfn), PCI_FUNC(pci_devfn));
			/* Place CardBus Socket/ExCA registers */
			pciauto_setup_bars(hose, top_bus, current_bus, pci_devfn, PCI_BASE_ADDRESS_0);

			pciauto_prescan_setup_cardbus_bridge(hose, top_bus,
					current_bus, pci_devfn, sub_bus);

			DBG("Scanning sub bus %.2x, I/O 0x%.8x, Mem 0x%.8x\n",
				sub_bus + 1,
				pciauto_lower_iospc, pciauto_lower_memspc);
			sub_bus = pciauto_bus_scan(hose, top_bus, sub_bus+1);
			DBG("Back to bus %.2x, sub_bus is %x\n", current_bus, sub_bus);
			pciauto_postscan_setup_cardbus_bridge(hose, top_bus,
					current_bus, pci_devfn, sub_bus);
			continue;
		} else if ((pci_class >> 16) == PCI_CLASS_STORAGE_IDE) {

			unsigned char prg_iface;

			early_read_config_byte(hose, top_bus, current_bus,
				pci_devfn, PCI_CLASS_PROG, &prg_iface);
			if (!(prg_iface & PCIAUTO_IDE_MODE_MASK)) {
				DBG("Skipping legacy mode IDE controller\n");
				continue;
			}
		}

		/*
		 * Found a peripheral, enable some standard
		 * settings
		 */
		early_read_config_dword(hose, top_bus, current_bus, pci_devfn,
					PCI_COMMAND, &cmdstat);
		early_write_config_dword(hose, top_bus, current_bus, pci_devfn,
					 PCI_COMMAND, cmdstat | PCI_COMMAND_IO |
					 PCI_COMMAND_MEMORY |
					 PCI_COMMAND_MASTER);
		early_write_config_byte(hose, top_bus, current_bus, pci_devfn,
					PCI_LATENCY_TIMER, 0x80);

		/* Allocate PCI I/O and/or memory space */
		pciauto_setup_bars(hose, top_bus, current_bus, pci_devfn, PCI_BASE_ADDRESS_5);
	}
	return sub_bus;
}

int __init
pciauto_assign_resources(int busno, struct pci_channel *hose)
{
	/* setup resource limits */
	io_resource_inuse = hose->io_resource;
	mem_resource_inuse = hose->mem_resource;

	pciauto_lower_iospc = io_resource_inuse->start;
	pciauto_upper_iospc = io_resource_inuse->end + 1;
	pciauto_lower_memspc = mem_resource_inuse->start;
	pciauto_upper_memspc = mem_resource_inuse->end + 1;
	DBG("Autoconfig PCI channel 0x%p\n", hose);
	DBG("Scanning bus %.2x, I/O 0x%.8x:0x%.8x, Mem 0x%.8x:0x%.8x\n",
		busno, pciauto_lower_iospc, pciauto_upper_iospc, 
		pciauto_lower_memspc, pciauto_upper_memspc);

	return pciauto_bus_scan(hose, busno, busno);
}
