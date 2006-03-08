/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004, 06 by Ralf Baechle (ralf@linux-mips.org)
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/mv643xx.h>

#include <linux/init.h>

#include <asm/marvell.h>

/*
 * We assume the address ranges have already been setup appropriately by
 * the firmware.  PMON in case of the Ocelot C does that.
 */
static struct resource mv_pci_io_mem0_resource = {
	.name	= "MV64340 PCI0 IO MEM",
	.flags	= IORESOURCE_IO
};

static struct resource mv_pci_mem0_resource = {
	.name	= "MV64340 PCI0 MEM",
	.flags	= IORESOURCE_MEM
};

static struct mv_pci_controller mv_bus0_controller = {
	.pcic = {
		.pci_ops	= &mv_pci_ops,
		.mem_resource	= &mv_pci_mem0_resource,
		.io_resource	= &mv_pci_io_mem0_resource,
	},
	.config_addr	= MV64340_PCI_0_CONFIG_ADDR,
	.config_vreg	= MV64340_PCI_0_CONFIG_DATA_VIRTUAL_REG,
};

static uint32_t mv_io_base, mv_io_size;

static void mv64340_pci0_init(void)
{
	uint32_t mem0_base, mem0_size;
	uint32_t io_base, io_size;

	io_base = MV_READ(MV64340_PCI_0_IO_BASE_ADDR) << 16;
	io_size = (MV_READ(MV64340_PCI_0_IO_SIZE) + 1) << 16;
	mem0_base = MV_READ(MV64340_PCI_0_MEMORY0_BASE_ADDR) << 16;
	mem0_size = (MV_READ(MV64340_PCI_0_MEMORY0_SIZE) + 1) << 16;

	mv_pci_io_mem0_resource.start		= 0;
	mv_pci_io_mem0_resource.end		= io_size - 1;
	mv_pci_mem0_resource.start		= mem0_base;
	mv_pci_mem0_resource.end		= mem0_base + mem0_size - 1;
	mv_bus0_controller.pcic.mem_offset	= mem0_base;
	mv_bus0_controller.pcic.io_offset	= 0;

	ioport_resource.end		= io_size - 1;

	register_pci_controller(&mv_bus0_controller.pcic);

	mv_io_base = io_base;
	mv_io_size = io_size;
}

static struct resource mv_pci_io_mem1_resource = {
	.name	= "MV64340 PCI1 IO MEM",
	.flags	= IORESOURCE_IO
};

static struct resource mv_pci_mem1_resource = {
	.name	= "MV64340 PCI1 MEM",
	.flags	= IORESOURCE_MEM
};

static struct mv_pci_controller mv_bus1_controller = {
	.pcic = {
		.pci_ops	= &mv_pci_ops,
		.mem_resource	= &mv_pci_mem1_resource,
		.io_resource	= &mv_pci_io_mem1_resource,
	},
	.config_addr	= MV64340_PCI_1_CONFIG_ADDR,
	.config_vreg	= MV64340_PCI_1_CONFIG_DATA_VIRTUAL_REG,
};

static __init void mv64340_pci1_init(void)
{
	uint32_t mem0_base, mem0_size;
	uint32_t io_base, io_size;

	io_base = MV_READ(MV64340_PCI_1_IO_BASE_ADDR) << 16;
	io_size = (MV_READ(MV64340_PCI_1_IO_SIZE) + 1) << 16;
	mem0_base = MV_READ(MV64340_PCI_1_MEMORY0_BASE_ADDR) << 16;
	mem0_size = (MV_READ(MV64340_PCI_1_MEMORY0_SIZE) + 1) << 16;

	/*
	 * Here we assume the I/O window of second bus to be contiguous with
	 * the first.  A gap is no problem but would waste address space for
	 * remapping the port space.
	 */
	mv_pci_io_mem1_resource.start		= mv_io_size;
	mv_pci_io_mem1_resource.end		= mv_io_size + io_size - 1;
	mv_pci_mem1_resource.start		= mem0_base;
	mv_pci_mem1_resource.end		= mem0_base + mem0_size - 1;
	mv_bus1_controller.pcic.mem_offset	= mem0_base;
	mv_bus1_controller.pcic.io_offset	= 0;

	ioport_resource.end		= io_base + io_size -mv_io_base - 1;

	register_pci_controller(&mv_bus1_controller.pcic);

	mv_io_size = io_base + io_size - mv_io_base;
}

static __init int __init ocelot_c_pci_init(void)
{
	unsigned long io_v_base;
	uint32_t enable;

	enable = ~MV_READ(MV64340_BASE_ADDR_ENABLE);

	/*
	 * We require at least one enabled I/O or PCI memory window or we
	 * will ignore this PCI bus.  We ignore PCI windows 1, 2 and 3.
	 */
	if (enable & (0x01 <<  9) || enable & (0x01 << 10))
		mv64340_pci0_init();

	if (enable & (0x01 << 14) || enable & (0x01 << 15))
		mv64340_pci1_init();

	if (mv_io_size) {
		io_v_base = (unsigned long) ioremap(mv_io_base, mv_io_size);
		if (!io_v_base)
			panic("Could not ioremap I/O port range");

		set_io_port_base(io_v_base);
	}

	return 0;
}

arch_initcall(ocelot_c_pci_init);
