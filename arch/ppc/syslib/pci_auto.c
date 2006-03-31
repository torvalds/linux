/*
 * PCI autoconfiguration library
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * The CardBus support is very preliminary.  Preallocating space is
 * the way to go but will require some change in card services to
 * make it useful.  Eventually this will ensure that we can put
 * multiple CB bridges behind multiple P2P bridges.  For now, at
 * least it ensures that we place the CB bridge BAR and assigned
 * initial bus numbers.  I definitely need to do something about
 * the lack of 16-bit I/O support. -MDP
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <asm/pci-bridge.h>

#define	PCIAUTO_IDE_MODE_MASK		0x05

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif /* DEBUG */

static int pciauto_upper_iospc;
static int pciauto_upper_memspc;

void __init pciauto_setup_bars(struct pci_controller *hose,
		int current_bus,
		int pci_devfn,
		int bar_limit)
{
	int bar_response, bar_size, bar_value;
	int bar, addr_mask;
	int * upper_limit;
	int found_mem64 = 0;

	DBG("PCI Autoconfig: Found Bus %d, Device %d, Function %d\n",
		current_bus, PCI_SLOT(pci_devfn), PCI_FUNC(pci_devfn) );

	for (bar = PCI_BASE_ADDRESS_0; bar <= bar_limit; bar+=4) {
		/* Tickle the BAR and get the response */
		early_write_config_dword(hose,
				current_bus,
				pci_devfn,
				bar,
				0xffffffff);
		early_read_config_dword(hose,
				current_bus,
				pci_devfn,
				bar,
				&bar_response);

		/* If BAR is not implemented go to the next BAR */
		if (!bar_response)
			continue;

		/* Check the BAR type and set our address mask */
		if (bar_response & PCI_BASE_ADDRESS_SPACE) {
			addr_mask = PCI_BASE_ADDRESS_IO_MASK;
			upper_limit = &pciauto_upper_iospc;
			DBG("PCI Autoconfig: BAR 0x%x, I/O, ", bar);
		} else {
			if ( (bar_response & PCI_BASE_ADDRESS_MEM_TYPE_MASK) ==
			PCI_BASE_ADDRESS_MEM_TYPE_64)
				found_mem64 = 1;

			addr_mask = PCI_BASE_ADDRESS_MEM_MASK;	
			upper_limit = &pciauto_upper_memspc;
			DBG("PCI Autoconfig: BAR 0x%x, Mem ", bar);
		}

		/* Calculate requested size */
		bar_size = ~(bar_response & addr_mask) + 1;

		/* Allocate a base address */
		bar_value = (*upper_limit - bar_size) & ~(bar_size - 1);

		/* Write it out and update our limit */
		early_write_config_dword(hose,
				current_bus,
				pci_devfn,
				bar,
				bar_value);

		*upper_limit = bar_value;

		/*
		 * If we are a 64-bit decoder then increment to the
		 * upper 32 bits of the bar and force it to locate
		 * in the lower 4GB of memory.
		 */
		if (found_mem64) {
			bar += 4;
			early_write_config_dword(hose,
					current_bus,
					pci_devfn,
					bar,
					0x00000000);
			found_mem64 = 0;
		}

		DBG("size=0x%x, address=0x%x\n",
			bar_size, bar_value);
	}

}

void __init pciauto_prescan_setup_bridge(struct pci_controller *hose,
		int current_bus,
		int pci_devfn,
		int sub_bus,
		int *iosave,
		int *memsave)
{
	/* Configure bus number registers */
	early_write_config_byte(hose,
			current_bus,
			pci_devfn,
			PCI_PRIMARY_BUS,
			current_bus);
	early_write_config_byte(hose,
			current_bus,
			pci_devfn,
			PCI_SECONDARY_BUS,
			sub_bus + 1);
	early_write_config_byte(hose,
			current_bus,
			pci_devfn,
			PCI_SUBORDINATE_BUS,
			0xff);

	/* Round memory allocator to 1MB boundary */
	pciauto_upper_memspc &= ~(0x100000 - 1);
	*memsave = pciauto_upper_memspc;

	/* Round I/O allocator to 4KB boundary */
	pciauto_upper_iospc &= ~(0x1000 - 1);
	*iosave = pciauto_upper_iospc;

	/* Set up memory and I/O filter limits, assume 32-bit I/O space */
	early_write_config_word(hose,
			current_bus,
			pci_devfn,
			PCI_MEMORY_LIMIT,
			((pciauto_upper_memspc - 1) & 0xfff00000) >> 16);
	early_write_config_byte(hose,
			current_bus,
			pci_devfn,
			PCI_IO_LIMIT,
			((pciauto_upper_iospc - 1) & 0x0000f000) >> 8);
	early_write_config_word(hose,
			current_bus,
			pci_devfn,
			PCI_IO_LIMIT_UPPER16,
			((pciauto_upper_iospc - 1) & 0xffff0000) >> 16);

	/* Zero upper 32 bits of prefetchable base/limit */
	early_write_config_dword(hose,
			current_bus,
			pci_devfn,
			PCI_PREF_BASE_UPPER32,
			0);
	early_write_config_dword(hose,
			current_bus,
			pci_devfn,
			PCI_PREF_LIMIT_UPPER32,
			0);
}

void __init pciauto_postscan_setup_bridge(struct pci_controller *hose,
		int current_bus,
		int pci_devfn,
		int sub_bus,
		int *iosave,
		int *memsave)
{
	int cmdstat;

	/* Configure bus number registers */
	early_write_config_byte(hose,
			current_bus,
			pci_devfn,
			PCI_SUBORDINATE_BUS,
			sub_bus);

	/*
	 * Round memory allocator to 1MB boundary.
	 * If no space used, allocate minimum.
	 */
	pciauto_upper_memspc &= ~(0x100000 - 1);
	if (*memsave == pciauto_upper_memspc)
		pciauto_upper_memspc -= 0x00100000;

	early_write_config_word(hose,
			current_bus,
			pci_devfn,
			PCI_MEMORY_BASE,
			pciauto_upper_memspc >> 16);

	/* Allocate 1MB for pre-fretch */
	early_write_config_word(hose,
			current_bus,
			pci_devfn,
			PCI_PREF_MEMORY_LIMIT,
			((pciauto_upper_memspc - 1) & 0xfff00000) >> 16);

	pciauto_upper_memspc -= 0x100000;

	early_write_config_word(hose,
			current_bus,
			pci_devfn,
			PCI_PREF_MEMORY_BASE,
			pciauto_upper_memspc >> 16);

	/* Round I/O allocator to 4KB boundary */
	pciauto_upper_iospc &= ~(0x1000 - 1);
	if (*iosave == pciauto_upper_iospc)
		pciauto_upper_iospc -= 0x1000;

	early_write_config_byte(hose,
			current_bus,
			pci_devfn,
			PCI_IO_BASE,
			(pciauto_upper_iospc & 0x0000f000) >> 8);
	early_write_config_word(hose,
			current_bus,
			pci_devfn,
			PCI_IO_BASE_UPPER16,
			pciauto_upper_iospc >> 16);

	/* Enable memory and I/O accesses, enable bus master */
	early_read_config_dword(hose,
			current_bus,
			pci_devfn,
			PCI_COMMAND,
			&cmdstat);
	early_write_config_dword(hose,
			current_bus,
			pci_devfn,
			PCI_COMMAND,
			cmdstat |
			PCI_COMMAND_IO |
			PCI_COMMAND_MEMORY |
			PCI_COMMAND_MASTER);
}

void __init pciauto_prescan_setup_cardbus_bridge(struct pci_controller *hose,
		int current_bus,
		int pci_devfn,
		int sub_bus,
		int *iosave,
		int *memsave)
{
	/* Configure bus number registers */
	early_write_config_byte(hose,
			current_bus,
			pci_devfn,
			PCI_PRIMARY_BUS,
			current_bus);
	early_write_config_byte(hose,
			current_bus,
			pci_devfn,
			PCI_SECONDARY_BUS,
			sub_bus + 1);
	early_write_config_byte(hose,
			current_bus,
			pci_devfn,
			PCI_SUBORDINATE_BUS,
			0xff);

	/* Round memory allocator to 4KB boundary */
	pciauto_upper_memspc &= ~(0x1000 - 1);
	*memsave = pciauto_upper_memspc;

	/* Round I/O allocator to 4 byte boundary */
	pciauto_upper_iospc &= ~(0x4 - 1);
	*iosave = pciauto_upper_iospc;

	/* Set up memory and I/O filter limits, assume 32-bit I/O space */
	early_write_config_dword(hose,
			current_bus,
			pci_devfn,
			0x20,
			pciauto_upper_memspc - 1);
	early_write_config_dword(hose,
			current_bus,
			pci_devfn,
			0x30,
			pciauto_upper_iospc - 1);
}

void __init pciauto_postscan_setup_cardbus_bridge(struct pci_controller *hose,
		int current_bus,
		int pci_devfn,
		int sub_bus,
		int *iosave,
		int *memsave)
{
	int cmdstat;

	/*
	 * Configure subordinate bus number.  The PCI subsystem
	 * bus scan will renumber buses (reserving three additional
	 * for this PCI<->CardBus bridge for the case where a CardBus
	 * adapter contains a P2P or CB2CB bridge.
	 */
	early_write_config_byte(hose,
			current_bus,
			pci_devfn,
			PCI_SUBORDINATE_BUS,
			sub_bus);

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
	pciauto_upper_memspc -= 0x00400000;
	pciauto_upper_iospc -= 0x00004000;

	/* Round memory allocator to 4KB boundary */
	pciauto_upper_memspc &= ~(0x1000 - 1);

	early_write_config_dword(hose,
			current_bus,
			pci_devfn,
			0x1c,
			pciauto_upper_memspc);

	/* Round I/O allocator to 4 byte boundary */
	pciauto_upper_iospc &= ~(0x4 - 1);
	early_write_config_dword(hose,
			current_bus,
			pci_devfn,
			0x2c,
			pciauto_upper_iospc);

	/* Enable memory and I/O accesses, enable bus master */
	early_read_config_dword(hose,
			current_bus,
			pci_devfn,
			PCI_COMMAND,
			&cmdstat);
	early_write_config_dword(hose,
			current_bus,
			pci_devfn,
			PCI_COMMAND,
			cmdstat |
			PCI_COMMAND_IO |
			PCI_COMMAND_MEMORY |
			PCI_COMMAND_MASTER);
}

int __init pciauto_bus_scan(struct pci_controller *hose, int current_bus)
{
	int sub_bus, pci_devfn, pci_class, cmdstat, found_multi = 0;
	unsigned short vid;
	unsigned char header_type;

	/*
	 * Fetch our I/O and memory space upper boundaries used
	 * to allocated base addresses on this hose.
	 */
	if (current_bus == hose->first_busno) {
		pciauto_upper_iospc = hose->io_space.end + 1;
		pciauto_upper_memspc = hose->mem_space.end + 1;
	}

	sub_bus = current_bus;

	for (pci_devfn = 0; pci_devfn < 0xff; pci_devfn++) {
		/* Skip our host bridge */
		if ( (current_bus == hose->first_busno) && (pci_devfn == 0) )
			continue;

		if (PCI_FUNC(pci_devfn) && !found_multi)
			continue;

		/* If config space read fails from this device, move on */
		if (early_read_config_byte(hose,
				current_bus,
				pci_devfn,
				PCI_HEADER_TYPE,
				&header_type))
			continue;

		if (!PCI_FUNC(pci_devfn))
			found_multi = header_type & 0x80;

		early_read_config_word(hose,
				current_bus,
				pci_devfn,
				PCI_VENDOR_ID,
				&vid);

		if (vid != 0xffff) {
			early_read_config_dword(hose,
					current_bus,
					pci_devfn,
					PCI_CLASS_REVISION, &pci_class);
			if ( (pci_class >> 16) == PCI_CLASS_BRIDGE_PCI ) {
				int iosave, memsave;

				DBG("PCI Autoconfig: Found P2P bridge, device %d\n", PCI_SLOT(pci_devfn));
				/* Allocate PCI I/O and/or memory space */
				pciauto_setup_bars(hose,
						current_bus,
						pci_devfn,
						PCI_BASE_ADDRESS_1);

				pciauto_prescan_setup_bridge(hose,
						current_bus,
						pci_devfn,
						sub_bus,
						&iosave,
						&memsave);
				sub_bus = pciauto_bus_scan(hose, sub_bus+1);
				pciauto_postscan_setup_bridge(hose,
						current_bus,
						pci_devfn,
						sub_bus,
						&iosave,
						&memsave);
			} else if ((pci_class >> 16) == PCI_CLASS_BRIDGE_CARDBUS) {
				int iosave, memsave;

				DBG("PCI Autoconfig: Found CardBus bridge, device %d function %d\n", PCI_SLOT(pci_devfn), PCI_FUNC(pci_devfn));
				/* Place CardBus Socket/ExCA registers */
				pciauto_setup_bars(hose,
						current_bus,
						pci_devfn,
						PCI_BASE_ADDRESS_0);

				pciauto_prescan_setup_cardbus_bridge(hose,
						current_bus,
						pci_devfn,
						sub_bus,
						&iosave,
						&memsave);
				sub_bus = pciauto_bus_scan(hose, sub_bus+1);
				pciauto_postscan_setup_cardbus_bridge(hose,
						current_bus,
						pci_devfn,
						sub_bus,
						&iosave,
						&memsave);
			} else {
				if ((pci_class >> 16) == PCI_CLASS_STORAGE_IDE) {
					unsigned char prg_iface;

					early_read_config_byte(hose,
							current_bus,
							pci_devfn,
							PCI_CLASS_PROG,
							&prg_iface);
					if (!(prg_iface & PCIAUTO_IDE_MODE_MASK)) {
						DBG("PCI Autoconfig: Skipping legacy mode IDE controller\n");
						continue;
					}
				}
				/* Allocate PCI I/O and/or memory space */
				pciauto_setup_bars(hose,
						current_bus,
						pci_devfn,
						PCI_BASE_ADDRESS_5);

				/*
				 * Enable some standard settings
				 */
				early_read_config_dword(hose,
						current_bus,
						pci_devfn,
						PCI_COMMAND,
						&cmdstat);
				early_write_config_dword(hose,
						current_bus,
						pci_devfn,
						PCI_COMMAND,
						cmdstat |
						PCI_COMMAND_IO |
						PCI_COMMAND_MEMORY |
						PCI_COMMAND_MASTER);
				early_write_config_byte(hose,
						current_bus,
						pci_devfn,
						PCI_LATENCY_TIMER,
						0x80);
			}
		}
	}
	return sub_bus;
}
