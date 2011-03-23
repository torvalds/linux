/*
 * arch/arm/mach-mv78xx0/include/mach/mv78xx0.h
 *
 * Generic definitions for Marvell MV78xx0 SoC flavors:
 *  MV781x0 and MV782x0.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_MV78XX0_H
#define __ASM_ARCH_MV78XX0_H

/*
 * Marvell MV78xx0 address maps.
 *
 * phys
 * c0000000	PCIe Memory space
 * f0800000	PCIe #0 I/O space
 * f0900000	PCIe #1 I/O space
 * f0a00000	PCIe #2 I/O space
 * f0b00000	PCIe #3 I/O space
 * f0c00000	PCIe #4 I/O space
 * f0d00000	PCIe #5 I/O space
 * f0e00000	PCIe #6 I/O space
 * f0f00000	PCIe #7 I/O space
 * f1000000	on-chip peripheral registers
 *
 * virt		phys		size
 * fe400000	f102x000	16K	core-specific peripheral registers
 * fe700000	f0800000	1M	PCIe #0 I/O space
 * fe800000	f0900000	1M	PCIe #1 I/O space
 * fe900000	f0a00000	1M	PCIe #2 I/O space
 * fea00000	f0b00000	1M	PCIe #3 I/O space
 * feb00000	f0c00000	1M	PCIe #4 I/O space
 * fec00000	f0d00000	1M	PCIe #5 I/O space
 * fed00000	f0e00000	1M	PCIe #6 I/O space
 * fee00000	f0f00000	1M	PCIe #7 I/O space
 * fef00000	f1000000	1M	on-chip peripheral registers
 */
#define MV78XX0_CORE0_REGS_PHYS_BASE	0xf1020000
#define MV78XX0_CORE1_REGS_PHYS_BASE	0xf1024000
#define MV78XX0_CORE_REGS_VIRT_BASE	0xfe400000
#define MV78XX0_CORE_REGS_SIZE		SZ_16K

#define MV78XX0_PCIE_IO_PHYS_BASE(i)	(0xf0800000 + ((i) << 20))
#define MV78XX0_PCIE_IO_VIRT_BASE(i)	(0xfe700000 + ((i) << 20))
#define MV78XX0_PCIE_IO_SIZE		SZ_1M

#define MV78XX0_REGS_PHYS_BASE		0xf1000000
#define MV78XX0_REGS_VIRT_BASE		0xfef00000
#define MV78XX0_REGS_SIZE		SZ_1M

#define MV78XX0_PCIE_MEM_PHYS_BASE	0xc0000000
#define MV78XX0_PCIE_MEM_SIZE		0x30000000

/*
 * Core-specific peripheral registers.
 */
#define BRIDGE_VIRT_BASE	(MV78XX0_CORE_REGS_VIRT_BASE)

/*
 * Register Map
 */
#define DDR_VIRT_BASE		(MV78XX0_REGS_VIRT_BASE | 0x00000)
#define  DDR_WINDOW_CPU0_BASE	(DDR_VIRT_BASE | 0x1500)
#define  DDR_WINDOW_CPU1_BASE	(DDR_VIRT_BASE | 0x1570)

#define DEV_BUS_PHYS_BASE	(MV78XX0_REGS_PHYS_BASE | 0x10000)
#define DEV_BUS_VIRT_BASE	(MV78XX0_REGS_VIRT_BASE | 0x10000)
#define  SAMPLE_AT_RESET_LOW	(DEV_BUS_VIRT_BASE | 0x0030)
#define  SAMPLE_AT_RESET_HIGH	(DEV_BUS_VIRT_BASE | 0x0034)
#define  GPIO_VIRT_BASE		(DEV_BUS_VIRT_BASE | 0x0100)
#define  I2C_0_PHYS_BASE	(DEV_BUS_PHYS_BASE | 0x1000)
#define  I2C_1_PHYS_BASE	(DEV_BUS_PHYS_BASE | 0x1100)
#define  UART0_PHYS_BASE	(DEV_BUS_PHYS_BASE | 0x2000)
#define  UART0_VIRT_BASE	(DEV_BUS_VIRT_BASE | 0x2000)
#define  UART1_PHYS_BASE	(DEV_BUS_PHYS_BASE | 0x2100)
#define  UART1_VIRT_BASE	(DEV_BUS_VIRT_BASE | 0x2100)
#define  UART2_PHYS_BASE	(DEV_BUS_PHYS_BASE | 0x2200)
#define  UART2_VIRT_BASE	(DEV_BUS_VIRT_BASE | 0x2200)
#define  UART3_PHYS_BASE	(DEV_BUS_PHYS_BASE | 0x2300)
#define  UART3_VIRT_BASE	(DEV_BUS_VIRT_BASE | 0x2300)

#define GE10_PHYS_BASE		(MV78XX0_REGS_PHYS_BASE | 0x30000)
#define GE11_PHYS_BASE		(MV78XX0_REGS_PHYS_BASE | 0x34000)

#define PCIE00_VIRT_BASE	(MV78XX0_REGS_VIRT_BASE | 0x40000)
#define PCIE01_VIRT_BASE	(MV78XX0_REGS_VIRT_BASE | 0x44000)
#define PCIE02_VIRT_BASE	(MV78XX0_REGS_VIRT_BASE | 0x48000)
#define PCIE03_VIRT_BASE	(MV78XX0_REGS_VIRT_BASE | 0x4c000)

#define USB0_PHYS_BASE		(MV78XX0_REGS_PHYS_BASE | 0x50000)
#define USB1_PHYS_BASE		(MV78XX0_REGS_PHYS_BASE | 0x51000)
#define USB2_PHYS_BASE		(MV78XX0_REGS_PHYS_BASE | 0x52000)

#define GE00_PHYS_BASE		(MV78XX0_REGS_PHYS_BASE | 0x70000)
#define GE01_PHYS_BASE		(MV78XX0_REGS_PHYS_BASE | 0x74000)

#define PCIE10_VIRT_BASE	(MV78XX0_REGS_VIRT_BASE | 0x80000)
#define PCIE11_VIRT_BASE	(MV78XX0_REGS_VIRT_BASE | 0x84000)
#define PCIE12_VIRT_BASE	(MV78XX0_REGS_VIRT_BASE | 0x88000)
#define PCIE13_VIRT_BASE	(MV78XX0_REGS_VIRT_BASE | 0x8c000)

#define SATA_PHYS_BASE		(MV78XX0_REGS_PHYS_BASE | 0xa0000)

/*
 * Supported devices and revisions.
 */
#define MV78X00_Z0_DEV_ID	0x6381
#define MV78X00_REV_Z0		1

#define MV78100_DEV_ID		0x7810
#define MV78100_REV_A0		1
#define MV78100_REV_A1		2

#define MV78200_DEV_ID		0x7820
#define MV78200_REV_A0		1

#endif
