/*
 * arch/arm/mach-mvebu/kirkwood.h
 *
 * Generic definitions for Marvell Kirkwood SoC flavors:
 * 88F6180, 88F6192 and 88F6281.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define KIRKWOOD_REGS_PHYS_BASE	0xf1000000
#define DDR_PHYS_BASE           (KIRKWOOD_REGS_PHYS_BASE + 0x00000)
#define BRIDGE_PHYS_BASE	(KIRKWOOD_REGS_PHYS_BASE + 0x20000)

#define DDR_OPERATION_BASE	(DDR_PHYS_BASE + 0x1418)

#define CPU_CONFIG_PHYS		(BRIDGE_PHYS_BASE + 0x0100)
#define CPU_CONFIG_ERROR_PROP	0x00000004

#define CPU_CONTROL_PHYS	(BRIDGE_PHYS_BASE + 0x0104)
#define MEMORY_PM_CTRL_PHYS	(BRIDGE_PHYS_BASE + 0x0118)
