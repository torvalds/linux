/*
 * (C) Copyright 2003
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * (C) Copyright 2004 Red Hat, Inc.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/immap_cpm2.h>
#include <asm/mpc8260.h>

#include "m8260_pci.h"


/* PCI bus configuration registers.
 */

static void __init m8260_setup_pci(struct pci_controller *hose)
{
	volatile cpm2_map_t *immap = cpm2_immr;
	unsigned long pocmr;
	u16 tempShort;

#ifndef CONFIG_ATC 	/* already done in U-Boot */
	/* 
	 * Setting required to enable IRQ1-IRQ7 (SIUMCR [DPPC]), 
	 * and local bus for PCI (SIUMCR [LBPC]).
	 */
	immap->im_siu_conf.siu_82xx.sc_siumcr = 0x00640000;
#endif

	/* Make PCI lowest priority */
	/* Each 4 bits is a device bus request  and the MS 4bits 
	   is highest priority */
	/* Bus               4bit value 
	   ---               ----------
	   CPM high          0b0000
	   CPM middle        0b0001
	   CPM low           0b0010
	   PCI reguest       0b0011
	   Reserved          0b0100
	   Reserved          0b0101
	   Internal Core     0b0110
	   External Master 1 0b0111
	   External Master 2 0b1000
	   External Master 3 0b1001
	   The rest are reserved */
	immap->im_siu_conf.siu_82xx.sc_ppc_alrh = 0x61207893;

	/* Park bus on core while modifying PCI Bus accesses */
	immap->im_siu_conf.siu_82xx.sc_ppc_acr = 0x6;

	/* 
	 * Set up master window that allows the CPU to access PCI space. This 
	 * window is set up using the first SIU PCIBR registers.
	 */
	immap->im_memctl.memc_pcimsk0 = MPC826x_PCI_MASK;
	immap->im_memctl.memc_pcibr0 =	MPC826x_PCI_BASE | PCIBR_ENABLE;

	/* Disable machine check on no response or target abort */
	immap->im_pci.pci_emr = cpu_to_le32(0x1fe7);
	/* Release PCI RST (by default the PCI RST signal is held low)  */
	immap->im_pci.pci_gcr = cpu_to_le32(PCIGCR_PCI_BUS_EN);

	/* give it some time */
	mdelay(1);

	/* 
	 * Set up master window that allows the CPU to access PCI Memory (prefetch) 
	 * space. This window is set up using the first set of Outbound ATU registers.
	 */
	immap->im_pci.pci_potar0 = cpu_to_le32(MPC826x_PCI_LOWER_MEM >> 12);
	immap->im_pci.pci_pobar0 = cpu_to_le32((MPC826x_PCI_LOWER_MEM - MPC826x_PCI_MEM_OFFSET) >> 12);
	pocmr = ((MPC826x_PCI_UPPER_MEM - MPC826x_PCI_LOWER_MEM) >> 12) ^ 0xfffff;
	immap->im_pci.pci_pocmr0 = cpu_to_le32(pocmr | POCMR_ENABLE | POCMR_PREFETCH_EN);

	/* 
	 * Set up master window that allows the CPU to access PCI Memory (non-prefetch) 
	 * space. This window is set up using the second set of Outbound ATU registers.
	 */
	immap->im_pci.pci_potar1 = cpu_to_le32(MPC826x_PCI_LOWER_MMIO >> 12);
	immap->im_pci.pci_pobar1 = cpu_to_le32((MPC826x_PCI_LOWER_MMIO - MPC826x_PCI_MMIO_OFFSET) >> 12);
	pocmr = ((MPC826x_PCI_UPPER_MMIO - MPC826x_PCI_LOWER_MMIO) >> 12) ^ 0xfffff;
	immap->im_pci.pci_pocmr1 = cpu_to_le32(pocmr | POCMR_ENABLE);

	/* 
	 * Set up master window that allows the CPU to access PCI IO space. This window
	 * is set up using the third set of Outbound ATU registers.
	 */
	immap->im_pci.pci_potar2 = cpu_to_le32(MPC826x_PCI_IO_BASE >> 12);
	immap->im_pci.pci_pobar2 = cpu_to_le32(MPC826x_PCI_LOWER_IO >> 12);
	pocmr = ((MPC826x_PCI_UPPER_IO - MPC826x_PCI_LOWER_IO) >> 12) ^ 0xfffff;
	immap->im_pci.pci_pocmr2 = cpu_to_le32(pocmr | POCMR_ENABLE | POCMR_PCI_IO);

	/* 
	 * Set up slave window that allows PCI masters to access MPC826x local memory. 
	 * This window is set up using the first set of Inbound ATU registers
	 */

	immap->im_pci.pci_pitar0 = cpu_to_le32(MPC826x_PCI_SLAVE_MEM_LOCAL >> 12);
	immap->im_pci.pci_pibar0 = cpu_to_le32(MPC826x_PCI_SLAVE_MEM_BUS >> 12);
	pocmr = ((MPC826x_PCI_SLAVE_MEM_SIZE-1) >> 12) ^ 0xfffff;
	immap->im_pci.pci_picmr0 = cpu_to_le32(pocmr | PICMR_ENABLE | PICMR_PREFETCH_EN);

	/* See above for description - puts PCI request as highest priority */
	immap->im_siu_conf.siu_82xx.sc_ppc_alrh = 0x03124567;

	/* Park the bus on the PCI */
	immap->im_siu_conf.siu_82xx.sc_ppc_acr = PPC_ACR_BUS_PARK_PCI;

	/* Host mode - specify the bridge as a host-PCI bridge */
	early_write_config_word(hose, 0, 0, PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_HOST);

	/* Enable the host bridge to be a master on the PCI bus, and to act as a PCI memory target */
	early_read_config_word(hose, 0, 0, PCI_COMMAND, &tempShort);
	early_write_config_word(hose, 0, 0, PCI_COMMAND,
				tempShort | PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY);
}

void __init m8260_find_bridges(void)
{
	extern int pci_assign_all_busses;
	struct pci_controller * hose;

	pci_assign_all_busses = 1;

	hose = pcibios_alloc_controller();

	if (!hose)
		return;

	ppc_md.pci_swizzle = common_swizzle;

	hose->first_busno = 0;
	hose->bus_offset = 0;
	hose->last_busno = 0xff;

	setup_m8260_indirect_pci(hose, 
				 (unsigned long)&cpm2_immr->im_pci.pci_cfg_addr,
				 (unsigned long)&cpm2_immr->im_pci.pci_cfg_data);

	m8260_setup_pci(hose);
        hose->pci_mem_offset = MPC826x_PCI_MEM_OFFSET;

        isa_io_base =
                (unsigned long) ioremap(MPC826x_PCI_IO_BASE,
                                        MPC826x_PCI_IO_SIZE);
        hose->io_base_virt = (void *) isa_io_base;
 
        /* setup resources */
        pci_init_resource(&hose->mem_resources[0],
			  MPC826x_PCI_LOWER_MEM,
			  MPC826x_PCI_UPPER_MEM,
			  IORESOURCE_MEM|IORESOURCE_PREFETCH, "PCI prefetchable memory");

        pci_init_resource(&hose->mem_resources[1],
			  MPC826x_PCI_LOWER_MMIO,
			  MPC826x_PCI_UPPER_MMIO,
			  IORESOURCE_MEM, "PCI memory");

        pci_init_resource(&hose->io_resource,
			  MPC826x_PCI_LOWER_IO,
			  MPC826x_PCI_UPPER_IO,
			  IORESOURCE_IO, "PCI I/O");
}
