/*
 * arch/arm/mach-meson6tv/cache.c
 *
 * Copyright (C) 2013 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <asm/hardware/cache-l2x0.h>

#include <plat/io.h>
#include <mach/io.h>

#if 0
extern void meson6_l2x0_init(void __iomem *);

#ifdef  CONFIG_MESON_L2CC_OPTIMIZE
static inline u32 __init read_actlr(void)
{
	u32 actlr;

	__asm__("mrc p15, 0, %0, c1, c0, 1\n" : "=r" (actlr));
	printk(KERN_INFO "===actlr=0x%x\n", actlr);
	return actlr;
}

static inline void __init write_actlr(u32 actlr)
{
	__asm__("mcr p15, 0, %0, c1, c0, 1\n" : : "r" (actlr));
}
#endif


static int __init meson_cache_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	__u32 prefetch, aux, scu_ctrl;
	void __iomem *l2x0_base;
	void __iomem *scu_base = (void __iomem *) IO_PERIPH_BASE;
	/*
	* Early BRESP, I/D prefetch enabled
	* Shared attribute override enabled
	* Full line of zero enabled
	*/
//      l2x0_init((void __iomem *)IO_PL310_BASE, ((1 << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT) |
//              (0x1 << 25) |
//              (0x1 << L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT) |
//              (0x1 << L2X0_AUX_CTRL_NS_INT_CTRL_SHIFT))|(0x2 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT), L2X0_AUX_CTRL_MASK);
// 	(3<<28)|(3<<26) | (1<<11) |(1<<10)|(1<<13)|(1<<22), ~0);

	l2x0_base=(void __iomem *)IO_PL310_BASE;

#ifdef  CONFIG_MESON_L2CC_OPTIMIZE
	l2x0_init(l2x0_base, ((1<<L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT) |
			      (1<<L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			      (1<<L2X0_AUX_CTRL_NS_INT_CTRL_SHIFT) |
			      (1<<L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT) |
			      (1<<L2X0_AUX_CTRL_SHARE_OVERRIDE_SHIFT) |
			      (1<<L2X0_AUX_CTRL_SHARE_INVALID_SHIFT) |
			      (1<<L2X0_AUX_CTRL_STORE_BUFFER_LIMIT_SHIFT) |
			      (1<<L2X0_AUX_CTRL_HIGH_PRIORITY_SO_DEVICE_SHIFT) |
			      (1<<0 | (1<<L2X0_AUX_CTRL_EARLY_BRESP_SHIFT))),
			      ~0);
#else /* CONFIG_MESON_L2CC_OPTIMIZE */
	l2x0_init(l2x0_base, ((1<<L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT) |
			      (1<<L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			      (1<<L2X0_AUX_CTRL_NS_INT_CTRL_SHIFT) |
			      (1<<L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT) |
			      (1<<L2X0_AUX_CTRL_SHARE_OVERRIDE_SHIFT) |
			      (1<<L2X0_AUX_CTRL_SHARE_INVALID_SHIFT) |
			      (1<<L2X0_AUX_CTRL_STORE_BUFFER_LIMIT_SHIFT) |
			      (1<<L2X0_AUX_CTRL_HIGH_PRIORITY_SO_DEVICE_SHIFT)),
			      ~0);
#endif /* CONFIG_MESON_L2CC_OPTIMIZE */

#ifdef CONFIG_MESON_L2CC_STANDBY
	writel_relaxed(0x3, l2x0_base + L2X0_POWER_CTRL);//enable dynamic clock gate & standby mode
#endif /* CONFIG_MESON_L2CC_STANDBY */

	prefetch = readl_relaxed(l2x0_base+ L2X0_PREFETCH_CTRL);
	/* prefetch 1+6 lines over */
	prefetch |= 0x6;

#ifdef CONFIG_SMP
	prefetch |= (1<<28) | (1<<29);
#ifdef CONFIG_MESON_L2CC_OPTIMIZE
	prefetch	|= (1<<24);
#endif /* CONFIG_MESON_L2CC_OPTIMIZE */
	writel_relaxed(prefetch, l2x0_base + L2X0_PREFETCH_CTRL);
#else /* CONFIG_SMP */
	prefetch |= (1<<30) | (1<<24) | (1<<28) | (1<<29);
	writel_relaxed(prefetch, l2x0_base + L2X0_PREFETCH_CTRL);
#endif /* CONFIG_SMP */

#ifdef 	CONFIG_MESON_L2CC_OPTIMIZE
	write_actlr((read_actlr()|(1<<3) | (1<<1)));
	read_actlr();

	scu_ctrl = __raw_readl(scu_base + 0);
	scu_ctrl |= (1<<3);
	__raw_writel(scu_ctrl, scu_base + 0);
	printk("SCU_CTRL: scu_ctrl=0x%x\n", scu_ctrl);

	aux=readl_relaxed(l2x0_base+ L2X0_AUX_CTRL);
	prefetch=readl_relaxed(l2x0_base+ L2X0_PREFETCH_CTRL);
	printk("pl310: aux=0x%x, prefetch=0x%x\n", aux, prefetch);
#endif /* CONFIG_MESON_L2CC_OPTIMIZE */

#endif /* CONFIG_CACHE_L2X0 */
	return 0;
}
early_initcall(meson_cache_init);

#else
static int __init meson_cache_of_init(void)
{
	int aux = 0;
	/*
		put some default aux setting here
	*/

	l2x0_of_init(aux,~0);
	return 0;
}
early_initcall(meson_cache_of_init);
#endif
