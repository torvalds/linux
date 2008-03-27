/*
 * arch/arm/mach-orion/addr-map.c
 *
 * Address map functions for Marvell Orion System On Chip
 *
 * Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mbus.h>
#include <asm/hardware.h>
#include "common.h"

/*
 * The Orion has fully programable address map. There's a separate address
 * map for each of the device _master_ interfaces, e.g. CPU, PCI, PCIE, USB,
 * Gigabit Ethernet, DMA/XOR engines, etc. Each interface has its own
 * address decode windows that allow it to access any of the Orion resources.
 *
 * CPU address decoding --
 * Linux assumes that it is the boot loader that already setup the access to
 * DDR and internal registers.
 * Setup access to PCI and PCI-E IO/MEM space is issued by core.c.
 * Setup access to various devices located on the device bus interface (e.g.
 * flashes, RTC, etc) should be issued by machine-setup.c according to
 * specific board population (by using orion_setup_cpu_win()).
 *
 * Non-CPU Masters address decoding --
 * Unlike the CPU, we setup the access from Orion's master interfaces to DDR
 * banks only (the typical use case).
 * Setup access for each master to DDR is issued by common.c.
 *
 * Note: although orion_setbits() and orion_clrbits() are not atomic
 * no locking is necessary here since code in this file is only called
 * at boot time when there is no concurrency issues.
 */

/*
 * Generic Address Decode Windows bit settings
 */
#define TARGET_DDR		0
#define TARGET_PCI		3
#define TARGET_PCIE		4
#define TARGET_DEV_BUS		1
#define ATTR_DDR_CS(n)		(((n) ==0) ? 0xe :	\
				((n) == 1) ? 0xd :	\
				((n) == 2) ? 0xb :	\
				((n) == 3) ? 0x7 : 0xf)
#define ATTR_PCIE_MEM		0x59
#define ATTR_PCIE_IO		0x51
#define ATTR_PCI_MEM		0x59
#define ATTR_PCI_IO		0x51
#define ATTR_DEV_CS0		0x1e
#define ATTR_DEV_CS1		0x1d
#define ATTR_DEV_CS2		0x1b
#define ATTR_DEV_BOOT		0xf
#define WIN_EN			1

/*
 * Helpers to get DDR banks info
 */
#define DDR_BASE_CS(n)		ORION_DDR_REG(0x1500 + ((n) * 8))
#define DDR_SIZE_CS(n)		ORION_DDR_REG(0x1504 + ((n) * 8))
#define DDR_MAX_CS		4
#define DDR_REG_TO_SIZE(reg)	(((reg) | 0xffffff) + 1)
#define DDR_REG_TO_BASE(reg)	((reg) & 0xff000000)
#define DDR_BANK_EN		1

/*
 * CPU Address Decode Windows registers
 */
#define CPU_WIN_CTRL(n)		ORION_BRIDGE_REG(0x000 | ((n) << 4))
#define CPU_WIN_BASE(n)		ORION_BRIDGE_REG(0x004 | ((n) << 4))
#define CPU_WIN_REMAP_LO(n)	ORION_BRIDGE_REG(0x008 | ((n) << 4))
#define CPU_WIN_REMAP_HI(n)	ORION_BRIDGE_REG(0x00c | ((n) << 4))
#define CPU_MAX_WIN		8

/*
 * Use this CPU address decode windows allocation
 */
#define CPU_WIN_PCIE_IO		0
#define CPU_WIN_PCI_IO		1
#define CPU_WIN_PCIE_MEM	2
#define CPU_WIN_PCI_MEM		3
#define CPU_WIN_DEV_BOOT	4
#define CPU_WIN_DEV_CS0		5
#define CPU_WIN_DEV_CS1		6
#define CPU_WIN_DEV_CS2		7

/*
 * Gigabit Ethernet Address Decode Windows registers
 */
#define ETH_WIN_BASE(win)	ORION_ETH_REG(0x200 + ((win) * 8))
#define ETH_WIN_SIZE(win)	ORION_ETH_REG(0x204 + ((win) * 8))
#define ETH_WIN_REMAP(win)	ORION_ETH_REG(0x280 + ((win) * 4))
#define ETH_WIN_EN		ORION_ETH_REG(0x290)
#define ETH_WIN_PROT		ORION_ETH_REG(0x294)
#define ETH_MAX_WIN		6
#define ETH_MAX_REMAP_WIN	4

/*
 * USB Address Decode Windows registers
 */
#define USB_WIN_CTRL(i, w)	((i == 0) ? ORION_USB0_REG(0x320 + ((w) << 4)) \
					: ORION_USB1_REG(0x320 + ((w) << 4)))
#define USB_WIN_BASE(i, w)	((i == 0) ? ORION_USB0_REG(0x324 + ((w) << 4)) \
					: ORION_USB1_REG(0x324 + ((w) << 4)))
#define USB_MAX_WIN		4

/*
 * SATA Address Decode Windows registers
 */
#define SATA_WIN_CTRL(win)	ORION_SATA_REG(0x30 + ((win) * 0x10))
#define SATA_WIN_BASE(win)	ORION_SATA_REG(0x34 + ((win) * 0x10))
#define SATA_MAX_WIN		4


struct mbus_dram_target_info orion_mbus_dram_info;

static int __init orion_cpu_win_can_remap(u32 win)
{
	u32 dev, rev;

	orion_pcie_id(&dev, &rev);
	if ((dev == MV88F5281_DEV_ID && win < 4)
	    || (dev == MV88F5182_DEV_ID && win < 2)
	    || (dev == MV88F5181_DEV_ID && win < 2))
		return 1;

	return 0;
}

void __init orion_setup_cpu_win(enum orion_target target, u32 base, u32 size, int remap)
{
	u32 win, attr, ctrl;

	switch (target) {
	case ORION_PCIE_IO:
		target = TARGET_PCIE;
		attr = ATTR_PCIE_IO;
		win = CPU_WIN_PCIE_IO;
		break;
	case ORION_PCI_IO:
		target = TARGET_PCI;
		attr = ATTR_PCI_IO;
		win = CPU_WIN_PCI_IO;
		break;
	case ORION_PCIE_MEM:
		target = TARGET_PCIE;
		attr = ATTR_PCIE_MEM;
		win = CPU_WIN_PCIE_MEM;
		break;
	case ORION_PCI_MEM:
		target = TARGET_PCI;
		attr = ATTR_PCI_MEM;
		win = CPU_WIN_PCI_MEM;
		break;
	case ORION_DEV_BOOT:
		target = TARGET_DEV_BUS;
		attr = ATTR_DEV_BOOT;
		win = CPU_WIN_DEV_BOOT;
		break;
	case ORION_DEV0:
		target = TARGET_DEV_BUS;
		attr = ATTR_DEV_CS0;
		win = CPU_WIN_DEV_CS0;
		break;
	case ORION_DEV1:
		target = TARGET_DEV_BUS;
		attr = ATTR_DEV_CS1;
		win = CPU_WIN_DEV_CS1;
		break;
	case ORION_DEV2:
		target = TARGET_DEV_BUS;
		attr = ATTR_DEV_CS2;
		win = CPU_WIN_DEV_CS2;
		break;
	case ORION_DDR:
	case ORION_REGS:
		/*
		 * Must be mapped by bootloader.
		 */
	default:
		target = attr = win = -1;
		BUG();
	}

	base &= 0xffff0000;
	ctrl = (((size - 1) & 0xffff0000) | (attr << 8) |
		(target << 4) | WIN_EN);

	orion_write(CPU_WIN_BASE(win), base);
	orion_write(CPU_WIN_CTRL(win), ctrl);

	if (orion_cpu_win_can_remap(win)) {
		if (remap >= 0) {
			orion_write(CPU_WIN_REMAP_LO(win), remap & 0xffff0000);
			orion_write(CPU_WIN_REMAP_HI(win), 0);
		} else {
			orion_write(CPU_WIN_REMAP_LO(win), base);
			orion_write(CPU_WIN_REMAP_HI(win), 0);
		}
	}
}

void __init orion_setup_cpu_wins(void)
{
	int i;
	int cs;

	/*
	 * First, disable and clear windows
	 */
	for (i = 0; i < CPU_MAX_WIN; i++) {
		orion_write(CPU_WIN_BASE(i), 0);
		orion_write(CPU_WIN_CTRL(i), 0);
		if (orion_cpu_win_can_remap(i)) {
			orion_write(CPU_WIN_REMAP_LO(i), 0);
			orion_write(CPU_WIN_REMAP_HI(i), 0);
		}
	}

	/*
	 * Setup windows for PCI+PCIe IO+MEM space.
	 */
	orion_setup_cpu_win(ORION_PCIE_IO, ORION_PCIE_IO_PHYS_BASE,
				ORION_PCIE_IO_SIZE, ORION_PCIE_IO_BUS_BASE);
	orion_setup_cpu_win(ORION_PCI_IO, ORION_PCI_IO_PHYS_BASE,
				ORION_PCI_IO_SIZE, ORION_PCI_IO_BUS_BASE);
	orion_setup_cpu_win(ORION_PCIE_MEM, ORION_PCIE_MEM_PHYS_BASE,
				ORION_PCIE_MEM_SIZE, -1);
	orion_setup_cpu_win(ORION_PCI_MEM, ORION_PCI_MEM_PHYS_BASE,
				ORION_PCI_MEM_SIZE, -1);

	/*
	 * Setup MBUS dram target info.
	 */
	orion_mbus_dram_info.mbus_dram_target_id = TARGET_DDR;

	for (i = 0, cs = 0; i < 4; i++) {
		u32 base = readl(DDR_BASE_CS(i));
		u32 size = readl(DDR_SIZE_CS(i));

		/*
		 * Chip select enabled?
		 */
		if (size & 1) {
			struct mbus_dram_window *w;

			w = &orion_mbus_dram_info.cs[cs++];
			w->cs_index = i;
			w->mbus_attr = 0xf & ~(1 << i);
			w->base = base & 0xff000000;
			w->size = (size | 0x00ffffff) + 1;
		}
	}
	orion_mbus_dram_info.num_cs = cs;
}

void __init orion_setup_usb_wins(void)
{
	int i;
	u32 usb_if, dev, rev;
	u32 max_usb_if = 1;

	orion_pcie_id(&dev, &rev);
	if (dev == MV88F5182_DEV_ID)
		max_usb_if = 2;

	for (usb_if = 0; usb_if < max_usb_if; usb_if++) {
		/*
		 * First, disable and clear windows
		 */
		for (i = 0; i < USB_MAX_WIN; i++) {
			orion_write(USB_WIN_BASE(usb_if, i), 0);
			orion_write(USB_WIN_CTRL(usb_if, i), 0);
		}

		/*
		 * Setup windows for DDR banks.
		 */
		for (i = 0; i < DDR_MAX_CS; i++) {
			u32 base, size;
			size = orion_read(DDR_SIZE_CS(i));
			base = orion_read(DDR_BASE_CS(i));
			if (size & DDR_BANK_EN) {
				base = DDR_REG_TO_BASE(base);
				size = DDR_REG_TO_SIZE(size);
				orion_write(USB_WIN_CTRL(usb_if, i),
						((size-1) & 0xffff0000) |
						(ATTR_DDR_CS(i) << 8) |
						(TARGET_DDR << 4) | WIN_EN);
				orion_write(USB_WIN_BASE(usb_if, i),
						base & 0xffff0000);
			}
		}
	}
}

void __init orion_setup_eth_wins(void)
{
	int i;

	/*
	 * First, disable and clear windows
	 */
	for (i = 0; i < ETH_MAX_WIN; i++) {
		orion_write(ETH_WIN_BASE(i), 0);
		orion_write(ETH_WIN_SIZE(i), 0);
		orion_setbits(ETH_WIN_EN, 1 << i);
		orion_clrbits(ETH_WIN_PROT, 0x3 << (i * 2));
		if (i < ETH_MAX_REMAP_WIN)
			orion_write(ETH_WIN_REMAP(i), 0);
	}

	/*
	 * Setup windows for DDR banks.
	 */
	for (i = 0; i < DDR_MAX_CS; i++) {
		u32 base, size;
		size = orion_read(DDR_SIZE_CS(i));
		base = orion_read(DDR_BASE_CS(i));
		if (size & DDR_BANK_EN) {
			base = DDR_REG_TO_BASE(base);
			size = DDR_REG_TO_SIZE(size);
			orion_write(ETH_WIN_SIZE(i), (size-1) & 0xffff0000);
			orion_write(ETH_WIN_BASE(i), (base & 0xffff0000) |
					(ATTR_DDR_CS(i) << 8) |
					TARGET_DDR);
			orion_clrbits(ETH_WIN_EN, 1 << i);
			orion_setbits(ETH_WIN_PROT, 0x3 << (i * 2));
		}
	}
}

void __init orion_setup_sata_wins(void)
{
	int i;

	/*
	 * First, disable and clear windows
	 */
	for (i = 0; i < SATA_MAX_WIN; i++) {
		orion_write(SATA_WIN_BASE(i), 0);
		orion_write(SATA_WIN_CTRL(i), 0);
	}

	/*
	 * Setup windows for DDR banks.
	 */
	for (i = 0; i < DDR_MAX_CS; i++) {
		u32 base, size;
		size = orion_read(DDR_SIZE_CS(i));
		base = orion_read(DDR_BASE_CS(i));
		if (size & DDR_BANK_EN) {
			base = DDR_REG_TO_BASE(base);
			size = DDR_REG_TO_SIZE(size);
			orion_write(SATA_WIN_CTRL(i),
					((size-1) & 0xffff0000) |
					(ATTR_DDR_CS(i) << 8) |
					(TARGET_DDR << 4) | WIN_EN);
			orion_write(SATA_WIN_BASE(i),
					base & 0xffff0000);
		}
	}
}
