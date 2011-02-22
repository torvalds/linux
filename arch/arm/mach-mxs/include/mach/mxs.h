/*
 * Copyright (C) 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __MACH_MXS_H__
#define __MACH_MXS_H__

#ifndef __ASSEMBLER__
#include <linux/io.h>
#endif
#include <asm/mach-types.h>
#include <mach/hardware.h>

/*
 * MXS CPU types
 */
#define cpu_is_mx23()		(machine_is_mx23evk())
#define cpu_is_mx28()		(machine_is_mx28evk())

/*
 * IO addresses common to MXS-based
 */
#define MXS_IO_BASE_ADDR		0x80000000
#define MXS_IO_SIZE			SZ_1M

#define MXS_ICOLL_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x000000)
#define MXS_APBH_DMA_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x004000)
#define MXS_BCH_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x00a000)
#define MXS_GPMI_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x00c000)
#define MXS_PINCTRL_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x018000)
#define MXS_DIGCTL_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x01c000)
#define MXS_APBX_DMA_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x024000)
#define MXS_DCP_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x028000)
#define MXS_PXP_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x02a000)
#define MXS_OCOTP_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x02c000)
#define MXS_AXI_AHB0_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x02e000)
#define MXS_LCDIF_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x030000)
#define MXS_CLKCTRL_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x040000)
#define MXS_SAIF0_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x042000)
#define MXS_POWER_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x044000)
#define MXS_SAIF1_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x046000)
#define MXS_LRADC_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x050000)
#define MXS_SPDIF_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x054000)
#define MXS_I2C0_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x058000)
#define MXS_PWM_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x064000)
#define MXS_TIMROT_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x068000)
#define MXS_AUART1_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x06c000)
#define MXS_AUART2_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x06e000)
#define MXS_DRAM_BASE_ADDR		(MXS_IO_BASE_ADDR + 0x0e0000)

/*
 * It maps the whole address space to [0xf4000000, 0xf50fffff].
 *
 *	OCRAM	0x00000000+0x020000	->	0xf4000000+0x020000
 *	IO	0x80000000+0x100000	->	0xf5000000+0x100000
 */
#define MXS_IO_P2V(x)	(0xf4000000 +					\
			(((x) & 0x80000000) >> 7) +			\
			(((x) & 0x000fffff)))

#define MXS_IO_ADDRESS(x)	IOMEM(MXS_IO_P2V(x))

#define mxs_map_entry(soc, name, _type)	{				\
	.virtual = soc ## _IO_P2V(soc ## _ ## name ## _BASE_ADDR),	\
	.pfn = __phys_to_pfn(soc ## _ ## name ## _BASE_ADDR),		\
	.length = soc ## _ ## name ## _SIZE,				\
	.type = _type,							\
}

#define MXS_SET_ADDR		0x4
#define MXS_CLR_ADDR		0x8
#define MXS_TOG_ADDR		0xc

#ifndef __ASSEMBLER__
static inline void __mxs_setl(u32 mask, void __iomem *reg)
{
	__raw_writel(mask, reg + MXS_SET_ADDR);
}

static inline void __mxs_clrl(u32 mask, void __iomem *reg)
{
	__raw_writel(mask, reg + MXS_CLR_ADDR);
}

static inline void __mxs_togl(u32 mask, void __iomem *reg)
{
	__raw_writel(mask, reg + MXS_TOG_ADDR);
}
#endif

#endif /* __MACH_MXS_H__ */
