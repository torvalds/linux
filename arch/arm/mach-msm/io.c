/* arch/arm/mach-msm/io.c
 *
 * MSM7K, QSD io support
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2008-2011, Code Aurora Forum. All rights reserved.
 * Author: Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/export.h>

#include <mach/hardware.h>
#include <asm/page.h>
#include <mach/msm_iomap.h>
#include <asm/mach/map.h>

#include <mach/board.h>

#define MSM_CHIP_DEVICE(name, chip) {			      \
		.virtual = (unsigned long) MSM_##name##_BASE, \
		.pfn = __phys_to_pfn(chip##_##name##_PHYS), \
		.length = chip##_##name##_SIZE, \
		.type = MT_DEVICE_NONSHARED, \
	 }

#define MSM_DEVICE(name) MSM_CHIP_DEVICE(name, MSM)

#if defined(CONFIG_ARCH_MSM7X00A) || defined(CONFIG_ARCH_MSM7X27) \
	|| defined(CONFIG_ARCH_MSM7X25)
static struct map_desc msm_io_desc[] __initdata = {
	MSM_DEVICE(VIC),
	MSM_CHIP_DEVICE(CSR, MSM7X00),
	MSM_DEVICE(DMOV),
	MSM_CHIP_DEVICE(GPIO1, MSM7X00),
	MSM_CHIP_DEVICE(GPIO2, MSM7X00),
	MSM_DEVICE(CLK_CTL),
#if defined(CONFIG_DEBUG_MSM_UART1) || defined(CONFIG_DEBUG_MSM_UART2) || \
	defined(CONFIG_DEBUG_MSM_UART3)
	MSM_DEVICE(DEBUG_UART),
#endif
#ifdef CONFIG_ARCH_MSM7X30
	MSM_DEVICE(GCC),
#endif
	{
		.virtual =  (unsigned long) MSM_SHARED_RAM_BASE,
		.pfn = __phys_to_pfn(MSM_SHARED_RAM_PHYS),
		.length =   MSM_SHARED_RAM_SIZE,
		.type =     MT_DEVICE,
	},
};

void __init msm_map_common_io(void)
{
	/* Make sure the peripheral register window is closed, since
	 * we will use PTE flags (TEX[1]=1,B=0,C=1) to determine which
	 * pages are peripheral interface or not.
	 */
	asm("mcr p15, 0, %0, c15, c2, 4" : : "r" (0));
	iotable_init(msm_io_desc, ARRAY_SIZE(msm_io_desc));
}
#endif

#ifdef CONFIG_ARCH_QSD8X50
static struct map_desc qsd8x50_io_desc[] __initdata = {
	MSM_DEVICE(VIC),
	MSM_CHIP_DEVICE(CSR, QSD8X50),
	MSM_DEVICE(DMOV),
	MSM_CHIP_DEVICE(GPIO1, QSD8X50),
	MSM_CHIP_DEVICE(GPIO2, QSD8X50),
	MSM_DEVICE(CLK_CTL),
	MSM_DEVICE(SIRC),
	MSM_DEVICE(SCPLL),
	MSM_DEVICE(AD5),
	MSM_DEVICE(MDC),
#if defined(CONFIG_DEBUG_MSM_UART1) || defined(CONFIG_DEBUG_MSM_UART2) || \
	defined(CONFIG_DEBUG_MSM_UART3)
	MSM_DEVICE(DEBUG_UART),
#endif
	{
		.virtual =  (unsigned long) MSM_SHARED_RAM_BASE,
		.pfn = __phys_to_pfn(MSM_SHARED_RAM_PHYS),
		.length =   MSM_SHARED_RAM_SIZE,
		.type =     MT_DEVICE,
	},
};

void __init msm_map_qsd8x50_io(void)
{
	iotable_init(qsd8x50_io_desc, ARRAY_SIZE(qsd8x50_io_desc));
}
#endif /* CONFIG_ARCH_QSD8X50 */

#ifdef CONFIG_ARCH_MSM8X60
static struct map_desc msm8x60_io_desc[] __initdata = {
	MSM_CHIP_DEVICE(QGIC_DIST, MSM8X60),
	MSM_CHIP_DEVICE(QGIC_CPU, MSM8X60),
	MSM_CHIP_DEVICE(TMR, MSM8X60),
	MSM_CHIP_DEVICE(TMR0, MSM8X60),
	MSM_DEVICE(ACC),
	MSM_DEVICE(GCC),
#ifdef CONFIG_DEBUG_MSM8660_UART
	MSM_DEVICE(DEBUG_UART),
#endif
};

void __init msm_map_msm8x60_io(void)
{
	iotable_init(msm8x60_io_desc, ARRAY_SIZE(msm8x60_io_desc));
}
#endif /* CONFIG_ARCH_MSM8X60 */

#ifdef CONFIG_ARCH_MSM8960
static struct map_desc msm8960_io_desc[] __initdata = {
	MSM_CHIP_DEVICE(QGIC_DIST, MSM8960),
	MSM_CHIP_DEVICE(QGIC_CPU, MSM8960),
	MSM_CHIP_DEVICE(TMR, MSM8960),
	MSM_CHIP_DEVICE(TMR0, MSM8960),
#ifdef CONFIG_DEBUG_MSM8960_UART
	MSM_DEVICE(DEBUG_UART),
#endif
};

void __init msm_map_msm8960_io(void)
{
	iotable_init(msm8960_io_desc, ARRAY_SIZE(msm8960_io_desc));
}
#endif /* CONFIG_ARCH_MSM8960 */

#ifdef CONFIG_ARCH_MSM7X30
static struct map_desc msm7x30_io_desc[] __initdata = {
	MSM_DEVICE(VIC),
	MSM_CHIP_DEVICE(CSR, MSM7X30),
	MSM_DEVICE(DMOV),
	MSM_CHIP_DEVICE(GPIO1, MSM7X30),
	MSM_CHIP_DEVICE(GPIO2, MSM7X30),
	MSM_DEVICE(CLK_CTL),
	MSM_DEVICE(CLK_CTL_SH2),
	MSM_DEVICE(AD5),
	MSM_DEVICE(MDC),
	MSM_DEVICE(ACC),
	MSM_DEVICE(SAW),
	MSM_DEVICE(GCC),
	MSM_DEVICE(TCSR),
#if defined(CONFIG_DEBUG_MSM_UART1) || defined(CONFIG_DEBUG_MSM_UART2) || \
	defined(CONFIG_DEBUG_MSM_UART3)
	MSM_DEVICE(DEBUG_UART),
#endif
	{
		.virtual =  (unsigned long) MSM_SHARED_RAM_BASE,
		.pfn = __phys_to_pfn(MSM_SHARED_RAM_PHYS),
		.length =   MSM_SHARED_RAM_SIZE,
		.type =     MT_DEVICE,
	},
};

void __init msm_map_msm7x30_io(void)
{
	iotable_init(msm7x30_io_desc, ARRAY_SIZE(msm7x30_io_desc));
}
#endif /* CONFIG_ARCH_MSM7X30 */

void __iomem *__msm_ioremap_caller(unsigned long phys_addr, size_t size,
				   unsigned int mtype, void *caller)
{
	if (mtype == MT_DEVICE) {
		/* The peripherals in the 88000000 - D0000000 range
		 * are only accessible by type MT_DEVICE_NONSHARED.
		 * Adjust mtype as necessary to make this "just work."
		 */
		if ((phys_addr >= 0x88000000) && (phys_addr < 0xD0000000))
			mtype = MT_DEVICE_NONSHARED;
	}

	return __arm_ioremap_caller(phys_addr, size, mtype, caller);
}
