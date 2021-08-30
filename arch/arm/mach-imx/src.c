// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reset-controller.h>
#include <linux/smp.h>
#include <asm/smp_plat.h>
#include "common.h"
#include "hardware.h"

#define SRC_SCR				0x000
#define SRC_GPR1_V1			0x020
#define SRC_GPR1_V2			0x074
#define SRC_GPR1(gpr_v2)		((gpr_v2) ? SRC_GPR1_V2 : SRC_GPR1_V1)
#define BP_SRC_SCR_WARM_RESET_ENABLE	0
#define BP_SRC_SCR_SW_GPU_RST		1
#define BP_SRC_SCR_SW_VPU_RST		2
#define BP_SRC_SCR_SW_IPU1_RST		3
#define BP_SRC_SCR_SW_OPEN_VG_RST	4
#define BP_SRC_SCR_SW_IPU2_RST		12
#define BP_SRC_SCR_CORE1_RST		14
#define BP_SRC_SCR_CORE1_ENABLE		22
/* below is for i.MX7D */
#define SRC_A7RCR1			0x008
#define BP_SRC_A7RCR1_A7_CORE1_ENABLE	1
#define GPC_CPU_PGC_SW_PUP_REQ		0xf0
#define GPC_CPU_PGC_SW_PDN_REQ		0xfc
#define GPC_PGC_C1			0x840
#define BM_CPU_PGC_SW_PDN_PUP_REQ_CORE1_A7	0x2

static void __iomem *src_base;
static DEFINE_SPINLOCK(scr_lock);
static bool gpr_v2;
static void __iomem *gpc_base;

static const int sw_reset_bits[5] = {
	BP_SRC_SCR_SW_GPU_RST,
	BP_SRC_SCR_SW_VPU_RST,
	BP_SRC_SCR_SW_IPU1_RST,
	BP_SRC_SCR_SW_OPEN_VG_RST,
	BP_SRC_SCR_SW_IPU2_RST
};

static int imx_src_reset_module(struct reset_controller_dev *rcdev,
		unsigned long sw_reset_idx)
{
	unsigned long timeout;
	unsigned long flags;
	int bit;
	u32 val;

	if (sw_reset_idx >= ARRAY_SIZE(sw_reset_bits))
		return -EINVAL;

	bit = 1 << sw_reset_bits[sw_reset_idx];

	spin_lock_irqsave(&scr_lock, flags);
	val = readl_relaxed(src_base + SRC_SCR);
	val |= bit;
	writel_relaxed(val, src_base + SRC_SCR);
	spin_unlock_irqrestore(&scr_lock, flags);

	timeout = jiffies + msecs_to_jiffies(1000);
	while (readl(src_base + SRC_SCR) & bit) {
		if (time_after(jiffies, timeout))
			return -ETIME;
		cpu_relax();
	}

	return 0;
}

static const struct reset_control_ops imx_src_ops = {
	.reset = imx_src_reset_module,
};

static struct reset_controller_dev imx_reset_controller = {
	.ops = &imx_src_ops,
	.nr_resets = ARRAY_SIZE(sw_reset_bits),
};

static void imx_gpcv2_set_m_core_pgc(bool enable, u32 offset)
{
	writel_relaxed(enable, gpc_base + offset);
}

/*
 * The motivation for bringing up the second i.MX7D core inside the kernel
 * is that legacy vendor bootloaders usually do not implement PSCI support.
 * This is a significant blocker for systems in the field that are running old
 * bootloader versions to upgrade to a modern mainline kernel version, as only
 * one CPU of the i.MX7D would be brought up.
 * Bring up the second i.MX7D core inside the kernel to make the migration
 * path to mainline kernel easier for the existing iMX7D users.
 */
void imx_gpcv2_set_core1_pdn_pup_by_software(bool pdn)
{
	u32 reg = pdn ? GPC_CPU_PGC_SW_PDN_REQ : GPC_CPU_PGC_SW_PUP_REQ;
	u32 val, pup;
	int ret;

	imx_gpcv2_set_m_core_pgc(true, GPC_PGC_C1);
	val = readl_relaxed(gpc_base + reg);
	val |= BM_CPU_PGC_SW_PDN_PUP_REQ_CORE1_A7;
	writel_relaxed(val, gpc_base + reg);

	ret = readl_relaxed_poll_timeout_atomic(gpc_base + reg, pup,
				!(pup & BM_CPU_PGC_SW_PDN_PUP_REQ_CORE1_A7),
				5, 1000000);
	if (ret < 0) {
		pr_err("i.MX7D: CORE1_A7 power up timeout\n");
		val &= ~BM_CPU_PGC_SW_PDN_PUP_REQ_CORE1_A7;
		writel_relaxed(val, gpc_base + reg);
	}

	imx_gpcv2_set_m_core_pgc(false, GPC_PGC_C1);
}

void imx_enable_cpu(int cpu, bool enable)
{
	u32 mask, val;

	cpu = cpu_logical_map(cpu);
	spin_lock(&scr_lock);
	if (gpr_v2) {
		if (enable)
			imx_gpcv2_set_core1_pdn_pup_by_software(false);

		mask = 1 << (BP_SRC_A7RCR1_A7_CORE1_ENABLE + cpu - 1);
		val = readl_relaxed(src_base + SRC_A7RCR1);
		val = enable ? val | mask : val & ~mask;
		writel_relaxed(val, src_base + SRC_A7RCR1);
	} else {
		mask = 1 << (BP_SRC_SCR_CORE1_ENABLE + cpu - 1);
		val = readl_relaxed(src_base + SRC_SCR);
		val = enable ? val | mask : val & ~mask;
		val |= 1 << (BP_SRC_SCR_CORE1_RST + cpu - 1);
		writel_relaxed(val, src_base + SRC_SCR);
	}
	spin_unlock(&scr_lock);
}

void imx_set_cpu_jump(int cpu, void *jump_addr)
{
	cpu = cpu_logical_map(cpu);
	writel_relaxed(__pa_symbol(jump_addr),
		       src_base + SRC_GPR1(gpr_v2) + cpu * 8);
}

u32 imx_get_cpu_arg(int cpu)
{
	cpu = cpu_logical_map(cpu);
	return readl_relaxed(src_base + SRC_GPR1(gpr_v2) + cpu * 8 + 4);
}

void imx_set_cpu_arg(int cpu, u32 arg)
{
	cpu = cpu_logical_map(cpu);
	writel_relaxed(arg, src_base + SRC_GPR1(gpr_v2) + cpu * 8 + 4);
}

void __init imx_src_init(void)
{
	struct device_node *np;
	u32 val;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx51-src");
	if (!np)
		return;
	src_base = of_iomap(np, 0);
	WARN_ON(!src_base);

	imx_reset_controller.of_node = np;
	if (IS_ENABLED(CONFIG_RESET_CONTROLLER))
		reset_controller_register(&imx_reset_controller);

	/*
	 * force warm reset sources to generate cold reset
	 * for a more reliable restart
	 */
	spin_lock(&scr_lock);
	val = readl_relaxed(src_base + SRC_SCR);
	val &= ~(1 << BP_SRC_SCR_WARM_RESET_ENABLE);
	writel_relaxed(val, src_base + SRC_SCR);
	spin_unlock(&scr_lock);
}

void __init imx7_src_init(void)
{
	struct device_node *np;

	gpr_v2 = true;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx7d-src");
	if (!np)
		return;

	src_base = of_iomap(np, 0);
	if (!src_base)
		return;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx7d-gpc");
	if (!np)
		return;

	gpc_base = of_iomap(np, 0);
	if (!gpc_base)
		return;
}
