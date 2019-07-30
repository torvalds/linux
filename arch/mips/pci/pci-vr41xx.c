// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  pci-vr41xx.c, PCI Control Unit routines for the NEC VR4100 series.
 *
 *  Copyright (C) 2001-2003 MontaVista Software Inc.
 *    Author: Yoichi Yuasa <source@mvista.com>
 *  Copyright (C) 2004-2008  Yoichi Yuasa <yuasa@linux-mips.org>
 *  Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 */
/*
 * Changes:
 *  MontaVista Software Inc. <source@mvista.com>
 *  - New creation, NEC VR4122 and VR4131 are supported.
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/types.h>

#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/vr41xx/pci.h>
#include <asm/vr41xx/vr41xx.h>

#include "pci-vr41xx.h"

extern struct pci_ops vr41xx_pci_ops;

static void __iomem *pciu_base;

#define pciu_read(offset)		readl(pciu_base + (offset))
#define pciu_write(offset, value)	writel((value), pciu_base + (offset))

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
	.name	= "PCI Memory resources",
	.start	= PCI_MEM_RESOURCE_START,
	.end	= PCI_MEM_RESOURCE_END,
	.flags	= IORESOURCE_MEM,
};

static struct resource pci_io_resource = {
	.name	= "PCI I/O resources",
	.start	= PCI_IO_RESOURCE_START,
	.end	= PCI_IO_RESOURCE_END,
	.flags	= IORESOURCE_IO,
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
	.pci_ops	= &vr41xx_pci_ops,
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

	if (request_mem_region(PCIU_BASE, PCIU_SIZE, "PCIU") == NULL)
		return -EBUSY;

	pciu_base = ioremap(PCIU_BASE, PCIU_SIZE);
	if (pciu_base == NULL) {
		release_mem_region(PCIU_BASE, PCIU_SIZE);
		return -EBUSY;
	}

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
		pciu_write(PCICLKSELREG, EQUAL_VTCLOCK);
	else if ((vtclock / 2) < pci_clock_max)
		pciu_write(PCICLKSELREG, HALF_VTCLOCK);
	else if (current_cpu_data.processor_id >= PRID_VR4131_REV2_1 &&
		 (vtclock / 3) < pci_clock_max)
		pciu_write(PCICLKSELREG, ONE_THIRD_VTCLOCK);
	else if ((vtclock / 4) < pci_clock_max)
		pciu_write(PCICLKSELREG, QUARTER_VTCLOCK);
	else {
		printk(KERN_ERR "PCI Clock is over 33MHz.\n");
		iounmap(pciu_base);
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
		pciu_write(PCIMMAW1REG, val);
	} else {
		val = pciu_read(PCIMMAW1REG);
		val &= ~WINEN;
		pciu_write(PCIMMAW1REG, val);
	}

	if (setup->master_memory2 != NULL) {
		master = setup->master_memory2;
		val = IBA(master->bus_base_address) |
		      MASTER_MSK(master->address_mask) |
		      WINEN |
		      PCIA(master->pci_base_address);
		pciu_write(PCIMMAW2REG, val);
	} else {
		val = pciu_read(PCIMMAW2REG);
		val &= ~WINEN;
		pciu_write(PCIMMAW2REG, val);
	}

	if (setup->target_memory1 != NULL) {
		target = setup->target_memory1;
		val = TARGET_MSK(target->address_mask) |
		      WINEN |
		      ITA(target->bus_base_address);
		pciu_write(PCITAW1REG, val);
	} else {
		val = pciu_read(PCITAW1REG);
		val &= ~WINEN;
		pciu_write(PCITAW1REG, val);
	}

	if (setup->target_memory2 != NULL) {
		target = setup->target_memory2;
		val = TARGET_MSK(target->address_mask) |
		      WINEN |
		      ITA(target->bus_base_address);
		pciu_write(PCITAW2REG, val);
	} else {
		val = pciu_read(PCITAW2REG);
		val &= ~WINEN;
		pciu_write(PCITAW2REG, val);
	}

	if (setup->master_io != NULL) {
		master = setup->master_io;
		val = IBA(master->bus_base_address) |
		      MASTER_MSK(master->address_mask) |
		      WINEN |
		      PCIIA(master->pci_base_address);
		pciu_write(PCIMIOAWREG, val);
	} else {
		val = pciu_read(PCIMIOAWREG);
		val &= ~WINEN;
		pciu_write(PCIMIOAWREG, val);
	}

	if (setup->exclusive_access == CANNOT_LOCK_FROM_DEVICE)
		pciu_write(PCIEXACCREG, UNLOCK);
	else
		pciu_write(PCIEXACCREG, 0);

	if (current_cpu_type() == CPU_VR4122)
		pciu_write(PCITRDYVREG, TRDYV(setup->wait_time_limit_from_irdy_to_trdy));

	pciu_write(LATTIMEREG, MLTIM(setup->master_latency_timer));

	if (setup->mailbox != NULL) {
		mailbox = setup->mailbox;
		val = MBADD(mailbox->base_address) | TYPE_32BITSPACE |
		      MSI_MEMORY | PREF_APPROVAL;
		pciu_write(MAILBAREG, val);
	}

	if (setup->target_window1) {
		window = setup->target_window1;
		val = PMBA(window->base_address) | TYPE_32BITSPACE |
		      MSI_MEMORY | PREF_APPROVAL;
		pciu_write(PCIMBA1REG, val);
	}

	if (setup->target_window2) {
		window = setup->target_window2;
		val = PMBA(window->base_address) | TYPE_32BITSPACE |
		      MSI_MEMORY | PREF_APPROVAL;
		pciu_write(PCIMBA2REG, val);
	}

	val = pciu_read(RETVALREG);
	val &= ~RTYVAL_MASK;
	val |= RTYVAL(setup->retry_limit);
	pciu_write(RETVALREG, val);

	val = pciu_read(PCIAPCNTREG);
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

	pciu_write(PCIAPCNTREG, val);

	pciu_write(COMMANDREG, PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
			       PCI_COMMAND_MASTER | PCI_COMMAND_PARITY |
			       PCI_COMMAND_SERR);

	/* Clear bus error */
	pciu_read(BUSERRADREG);

	pciu_write(PCIENREG, PCIU_CONFIG_DONE);

	if (setup->mem_resource != NULL)
		vr41xx_pci_controller.mem_resource = setup->mem_resource;

	if (setup->io_resource != NULL) {
		vr41xx_pci_controller.io_resource = setup->io_resource;
	} else {
		set_io_port_base(IO_PORT_BASE);
		ioport_resource.start = IO_PORT_RESOURCE_START;
		ioport_resource.end = IO_PORT_RESOURCE_END;
	}

	if (setup->master_io) {
		void __iomem *io_map_base;
		struct resource *res = vr41xx_pci_controller.io_resource;
		master = setup->master_io;
		io_map_base = ioremap(master->bus_base_address,
				      resource_size(res));
		if (!io_map_base)
			return -EBUSY;

		vr41xx_pci_controller.io_map_base = (unsigned long)io_map_base;
	}

	register_pci_controller(&vr41xx_pci_controller);

	return 0;
}

arch_initcall(vr41xx_pciu_init);
