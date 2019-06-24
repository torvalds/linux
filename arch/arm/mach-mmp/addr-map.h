/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *   Common address map definitions
 */

#ifndef __ASM_MACH_ADDR_MAP_H
#define __ASM_MACH_ADDR_MAP_H

/* APB - Application Subsystem Peripheral Bus
 *
 * NOTE: the DMA controller registers are actually on the AXI fabric #1
 * slave port to AHB/APB bridge, due to its close relationship to those
 * peripherals on APB, let's count it into the ABP mapping area.
 */
#define APB_PHYS_BASE		0xd4000000
#define APB_VIRT_BASE		IOMEM(0xfe000000)
#define APB_PHYS_SIZE		0x00200000

#define AXI_PHYS_BASE		0xd4200000
#define AXI_VIRT_BASE		IOMEM(0xfe200000)
#define AXI_PHYS_SIZE		0x00200000

/* Static Memory Controller - Chip Select 0 and 1 */
#define SMC_CS0_PHYS_BASE	0x80000000
#define SMC_CS0_PHYS_SIZE	0x10000000
#define SMC_CS1_PHYS_BASE	0x90000000
#define SMC_CS1_PHYS_SIZE	0x10000000

#define APMU_VIRT_BASE		(AXI_VIRT_BASE + 0x82800)
#define APMU_REG(x)		(APMU_VIRT_BASE + (x))

#define APBC_VIRT_BASE		(APB_VIRT_BASE + 0x015000)
#define APBC_REG(x)		(APBC_VIRT_BASE + (x))

#define MPMU_VIRT_BASE		(APB_VIRT_BASE + 0x50000)
#define MPMU_REG(x)		(MPMU_VIRT_BASE + (x))

#define CIU_VIRT_BASE		(AXI_VIRT_BASE + 0x82c00)
#define CIU_REG(x)		(CIU_VIRT_BASE + (x))

#endif /* __ASM_MACH_ADDR_MAP_H */
