/*
 *  pci-vr41xx.c, PCI Control Unit routines for the NEC VR4100 series.
 *
 *  Copyright (C) 2001-2003 MontaVista Software Inc.
 *    Author: Yoichi Yuasa <yyuasa@mvista.com or source@mvista.com>
 *  Copyright (C) 2004  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*
 * Changes:
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - New creation, NEC VR4122 and VR4131 are supported.
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>

#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/vr41xx/vr41xx.h>

#include "pci-vr41xx.h"

extern struct pci_ops vr41xx_pci_ops;

static struct pci_master_address_conversion pci_master_memory1 = {
	.bus_base_address	= PCI_MASTER_MEM1_BUS_BASE_ADDRESS,
	.address_mask		= PCI_MASTER_MEM1_ADDRESS_MASK,
	.pci_base_address	= PCI_MASTER_MEM1_PCI_BASE_ADDRESS,
};

static struct pci_target_address_conversion pci_target_memory1 = {
	.address_mask		= PCI_TARGET_MEM1_ADDRESS_MASK,
	.bus_base_address	= PCI_TARGET_MEM1_BUS_BASE_ADDRESS,
};

static struct pci_master_address_conversion pci_master_io = {
	.bus_base_address	= PCI_MASTER_IO_BUS_BASE_ADDRESS,
	.address_mask		= PCI_MASTER_IO_ADDRESS_MASK,
	.pci_base_address	= PCI_MASTER_IO_PCI_BASE_ADDRESS,
};

static struct pci_mailbox_address pci_mailbox = {
	.base_address		= PCI_MAILBOX_BASE_ADDRESS,
};

static struct pci_target_address_window pci_target_window1 = {
	.base_address		= PCI_TARGET_WINDOW1_BASE_ADDRESS,
};

static struct resource pci_mem_resource = {
	.name   = "PCI Memory resources",
	.start  = PCI_MEM_RESOURCE_START,
	.end    = PCI_MEM_RESOURCE_END,
	.flags  = IORESOURCE_MEM,
};

static struct resource pci_io_resource = {
	.name   = "PCI I/O resources",
	.start  = PCI_IO_RESOURCE_START,
	.end    = PCI_IO_RESOURCE_END,
	.flags  = IORESOURCE_IO,
};

static struct pci_controller_unit_setup vr41xx_pci_controller_unit_setup = {
	.master_memory1				= &pci_master_memory1,
	.target_memory1				= &pci_target_memory1,
	.master_io				= &pci_master_io,
	.exclusive_access			= CANNOT_LOCK_FROM_DEVICE,
	.wait_time_limit_from_irdy_to_trdy	= 0,
	.mailbox				= &pci_mailbox,
	.target_window1				= &pci_target_window1,
	.master_latency_timer			= 0x80,
	.retry_limit				= 0,
	.arbiter_priority_control		= PCI_ARBITRATION_MODE_FAIR,
	.take_away_gnt_mode			= PCI_TAKE_AWAY_GNT_DISABLE,
};

static struct pci_controller vr41xx_pci_controller = {
	.pci_ops        = &vr41xx_pci_ops,
	.mem_resource	= &pci_mem_resource,
	.io_resource	= &pci_io_resource,
};

void __init vr41xx_pciu_setup(struct pci_controller_unit_setup *setup)
{
	vr41xx_pci_controller_unit_setup = *setup;
}

static int __init vr41xx_pciu_init(void)
{
	struct pci_controller_unit_setup *setup;
	struct pci_master_address_conversion *master;
	struct pci_target_address_conversion *target;
	struct pci_mailbox_address *mailbox;
	struct pci_target_address_window *window;
	unsigned long vtclock, pci_clock_max;
	uint32_t val;

	setup = &vr41xx_pci_controller_unit_setup;

	/* Disable PCI interrupt */
	vr41xx_disable_pciint();

	/* Supply VTClock to PCIU */
	vr41xx_supply_clock(PCIU_CLOCK);

	/* Dummy write, waiting for supply of VTClock. */
	vr41xx_disable_pciint();

	/* Select PCI clock */
	if (setup->pci_clock_max != 0)
		pci_clock_max = setup->pci_clock_max;
	else
		pci_clock_max = PCI_CLOCK_MAX;
	vtclock = vr41xx_get_vtclock_frequency();
	if (vtclock < pci_clock_max)
		writel(EQUAL_VTCLOCK, PCICLKSELREG);
	else if ((vtclock / 2) < pci_clock_max)
		writel(HALF_VTCLOCK, PCICLKSELREG);
	else if (current_cpu_data.processor_id >= PRID_VR4131_REV2_1 &&
	         (vtclock / 3) < pci_clock_max)
		writel(ONE_THIRD_VTCLOCK, PCICLKSELREG);
	else if ((vtclock / 4) < pci_clock_max)
		writel(QUARTER_VTCLOCK, PCICLKSELREG);
	else {
		printk(KERN_ERR "PCI Clock is over 33MHz.\n");
		return -EINVAL;
	}

	/* Supply PCI clock by PCI bus */
	vr41xx_supply_clock(PCI_CLOCK);

	if (setup->master_memory1 != NULL) {
		master = setup->master_memory1;
		val = IBA(master->bus_base_address) |
		      MASTER_MSK(master->address_mask) |
		      WINEN |
		      PCIA(master->pci_base_address);
		writel(val, PCIMMAW1REG);
	} else {
		val = readl(PCIMMAW1REG);
		val &= ~WINEN;
		writel(val, PCIMMAW1REG);
	}

	if (setup->master_memory2 != NULL) {
		master = setup->master_memory2;
		val = IBA(master->bus_base_address) |
		      MASTER_MSK(master->address_mask) |
		      WINEN |
		      PCIA(master->pci_base_address);
		writel(val, PCIMMAW2REG);
	} else {
		val = readl(PCIMMAW2REG);
		val &= ~WINEN;
		writel(val, PCIMMAW2REG);
	}

	if (setup->target_memory1 != NULL) {
		target = setup->target_memory1;
		val = TARGET_MSK(target->address_mask) |
		      WINEN |
		      ITA(target->bus_base_address);
		writel(val, PCITAW1REG);
	} else {
		val = readl(PCITAW1REG);
		val &= ~WINEN;
		writel(val, PCITAW1REG);
	}

	if (setup->target_memory2 != NULL) {
		target = setup->target_memory2;
		val = TARGET_MSK(target->address_mask) |
		      WINEN |
		      ITA(target->bus_base_address);
		writel(val, PCITAW2REG);
	} else {
		val = readl(PCITAW2REG);
		val &= ~WINEN;
		writel(val, PCITAW2REG);
	}

	if (setup->master_io != NULL) {
		master = setup->master_io;
		val = IBA(master->bus_base_address) |
		      MASTER_MSK(master->address_mask) |
		      WINEN |
		      PCIIA(master->pci_base_address);
		writel(val, PCIMIOAWREG);
	} else {
		val = readl(PCIMIOAWREG);
		val &= ~WINEN;
		writel(val, PCIMIOAWREG);
	}

	if (setup->exclusive_access == CANNOT_LOCK_FROM_DEVICE)
		writel(UNLOCK, PCIEXACCREG);
	else
		writel(0, PCIEXACCREG);

	if (current_cpu_data.cputype == CPU_VR4122)
		writel(TRDYV(setup->wait_time_limit_from_irdy_to_trdy), PCITRDYVREG);

	writel(MLTIM(setup->master_latency_timer), LATTIMEREG);

	if (setup->mailbox != NULL) {
		mailbox = setup->mailbox;
		val = MBADD(mailbox->base_address) | TYPE_32BITSPACE |
		      MSI_MEMORY | PREF_APPROVAL;
		writel(val, MAILBAREG);
	}

	if (setup->target_window1) {
		window = setup->target_window1;
		val = PMBA(window->base_address) | TYPE_32BITSPACE |
		      MSI_MEMORY | PREF_APPROVAL;
		writel(val, PCIMBA1REG);
	}

	if (setup->target_window2) {
		window = setup->target_window2;
		val = PMBA(window->base_address) | TYPE_32BITSPACE |
		      MSI_MEMORY | PREF_APPROVAL;
		writel(val, PCIMBA2REG);
	}

	val = readl(RETVALREG);
	val &= ~RTYVAL_MASK;
	val |= RTYVAL(setup->retry_limit);
	writel(val, RETVALREG);

	val = readl(PCIAPCNTREG);
	val &= ~(TKYGNT | PAPC);

	switch (setup->arbiter_priority_control) {
	case PCI_ARBITRATION_MODE_ALTERNATE_0:
		val |= PAPC_ALTERNATE_0;
		break;
	case PCI_ARBITRATION_MODE_ALTERNATE_B:
		val |= PAPC_ALTERNATE_B;
		break;
	default:
		val |= PAPC_FAIR;
		break;
	}

	if (setup->take_away_gnt_mode == PCI_TAKE_AWAY_GNT_ENABLE)
		val |= TKYGNT_ENABLE;

	writel(val, PCIAPCNTREG);

	writel(PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
	       PCI_COMMAND_PARITY | PCI_COMMAND_SERR, COMMANDREG);

	/* Clear bus error */
	readl(BUSERRADREG);

	writel(BLOODY_CONFIG_DONE, PCIENREG);

	if (setup->mem_resource != NULL)
		vr41xx_pci_controller.mem_resource = setup->mem_resource;

	if (setup->io_resource != NULL) {
		vr41xx_pci_controller.io_resource = setup->io_resource;
	} else {
		set_io_port_base(IO_PORT_BASE);
		ioport_resource.start = IO_PORT_RESOURCE_START;
		ioport_resource.end = IO_PORT_RESOURCE_END;
	}

	register_pci_controller(&vr41xx_pci_controller);

	return 0;
}

arch_initcall(vr41xx_pciu_init);
