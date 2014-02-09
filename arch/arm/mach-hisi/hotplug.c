/*
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2013 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include "core.h"

/* Sysctrl registers in Hi3620 SoC */
#define SCISOEN				0xc0
#define SCISODIS			0xc4
#define SCPERPWREN			0xd0
#define SCPERPWRDIS			0xd4
#define SCCPUCOREEN			0xf4
#define SCCPUCOREDIS			0xf8
#define SCPERCTRL0			0x200
#define SCCPURSTEN			0x410
#define SCCPURSTDIS			0x414

/*
 * bit definition in SCISOEN/SCPERPWREN/...
 *
 * CPU2_ISO_CTRL	(1 << 5)
 * CPU3_ISO_CTRL	(1 << 6)
 * ...
 */
#define CPU2_ISO_CTRL			(1 << 5)

/*
 * bit definition in SCPERCTRL0
 *
 * CPU0_WFI_MASK_CFG	(1 << 28)
 * CPU1_WFI_MASK_CFG	(1 << 29)
 * ...
 */
#define CPU0_WFI_MASK_CFG		(1 << 28)

/*
 * bit definition in SCCPURSTEN/...
 *
 * CPU0_SRST_REQ_EN	(1 << 0)
 * CPU1_SRST_REQ_EN	(1 << 1)
 * ...
 */
#define CPU0_HPM_SRST_REQ_EN		(1 << 22)
#define CPU0_DBG_SRST_REQ_EN		(1 << 12)
#define CPU0_NEON_SRST_REQ_EN		(1 << 4)
#define CPU0_SRST_REQ_EN		(1 << 0)

enum {
	HI3620_CTRL,
	ERROR_CTRL,
};

static void __iomem *ctrl_base;
static int id;

static void set_cpu_hi3620(int cpu, bool enable)
{
	u32 val = 0;

	if (enable) {
		/* MTCMOS set */
		if ((cpu == 2) || (cpu == 3))
			writel_relaxed(CPU2_ISO_CTRL << (cpu - 2),
				       ctrl_base + SCPERPWREN);
		udelay(100);

		/* Enable core */
		writel_relaxed(0x01 << cpu, ctrl_base + SCCPUCOREEN);

		/* unreset */
		val = CPU0_DBG_SRST_REQ_EN | CPU0_NEON_SRST_REQ_EN
			| CPU0_SRST_REQ_EN;
		writel_relaxed(val << cpu, ctrl_base + SCCPURSTDIS);
		/* reset */
		val |= CPU0_HPM_SRST_REQ_EN;
		writel_relaxed(val << cpu, ctrl_base + SCCPURSTEN);

		/* ISO disable */
		if ((cpu == 2) || (cpu == 3))
			writel_relaxed(CPU2_ISO_CTRL << (cpu - 2),
				       ctrl_base + SCISODIS);
		udelay(1);

		/* WFI Mask */
		val = readl_relaxed(ctrl_base + SCPERCTRL0);
		val &= ~(CPU0_WFI_MASK_CFG << cpu);
		writel_relaxed(val, ctrl_base + SCPERCTRL0);

		/* Unreset */
		val = CPU0_DBG_SRST_REQ_EN | CPU0_NEON_SRST_REQ_EN
			| CPU0_SRST_REQ_EN | CPU0_HPM_SRST_REQ_EN;
		writel_relaxed(val << cpu, ctrl_base + SCCPURSTDIS);
	} else {
		/* wfi mask */
		val = readl_relaxed(ctrl_base + SCPERCTRL0);
		val |= (CPU0_WFI_MASK_CFG << cpu);
		writel_relaxed(val, ctrl_base + SCPERCTRL0);

		/* disable core*/
		writel_relaxed(0x01 << cpu, ctrl_base + SCCPUCOREDIS);

		if ((cpu == 2) || (cpu == 3)) {
			/* iso enable */
			writel_relaxed(CPU2_ISO_CTRL << (cpu - 2),
				       ctrl_base + SCISOEN);
			udelay(1);
		}

		/* reset */
		val = CPU0_DBG_SRST_REQ_EN | CPU0_NEON_SRST_REQ_EN
			| CPU0_SRST_REQ_EN | CPU0_HPM_SRST_REQ_EN;
		writel_relaxed(val << cpu, ctrl_base + SCCPURSTEN);

		if ((cpu == 2) || (cpu == 3)) {
			/* MTCMOS unset */
			writel_relaxed(CPU2_ISO_CTRL << (cpu - 2),
				       ctrl_base + SCPERPWRDIS);
			udelay(100);
		}
	}
}

static int hi3xxx_hotplug_init(void)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "hisilicon,sysctrl");
	if (node) {
		ctrl_base = of_iomap(node, 0);
		id = HI3620_CTRL;
		return 0;
	}
	id = ERROR_CTRL;
	return -ENOENT;
}

void hi3xxx_set_cpu(int cpu, bool enable)
{
	if (!ctrl_base) {
		if (hi3xxx_hotplug_init() < 0)
			return;
	}

	if (id == HI3620_CTRL)
		set_cpu_hi3620(cpu, enable);
}

static inline void cpu_enter_lowpower(void)
{
	unsigned int v;

	flush_cache_all();

	/*
	 * Turn off coherency and L1 D-cache
	 */
	asm volatile(
	"	mrc	p15, 0, %0, c1, c0, 1\n"
	"	bic	%0, %0, #0x40\n"
	"	mcr	p15, 0, %0, c1, c0, 1\n"
	"	mrc	p15, 0, %0, c1, c0, 0\n"
	"	bic	%0, %0, #0x04\n"
	"	mcr	p15, 0, %0, c1, c0, 0\n"
	  : "=&r" (v)
	  : "r" (0)
	  : "cc");
}

void hi3xxx_cpu_die(unsigned int cpu)
{
	cpu_enter_lowpower();
	hi3xxx_set_cpu_jump(cpu, phys_to_virt(0));
	cpu_do_idle();

	/* We should have never returned from idle */
	panic("cpu %d unexpectedly exit from shutdown\n", cpu);
}

int hi3xxx_cpu_kill(unsigned int cpu)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(50);

	while (hi3xxx_get_cpu_jump(cpu))
		if (time_after(jiffies, timeout))
			return 0;
	hi3xxx_set_cpu(cpu, false);
	return 1;
}
