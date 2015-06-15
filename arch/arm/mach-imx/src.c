/*
 * Copyright 2011-2015 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reset-controller.h>
#include <linux/smp.h>
#include <asm/smp_plat.h>
#include "common.h"
#include "hardware.h"

#define SRC_SCR				0x000
#define SRC_GPR1			0x020
#define BP_SRC_SCR_WARM_RESET_ENABLE	0
#define BP_SRC_SCR_SW_GPU_RST		1
#define BP_SRC_SCR_SW_VPU_RST		2
#define BP_SRC_SCR_SW_IPU1_RST		3
#define BP_SRC_SCR_SW_OPEN_VG_RST	4
#define BP_SRC_SCR_SW_IPU2_RST		12
#define BP_SRC_SCR_CORE1_RST		14
#define BP_SRC_SCR_CORE1_ENABLE		22
/* below is for i.MX7D */
#define SRC_GPR1_V2			0x074
#define SRC_A7RCR0			0x004
#define SRC_A7RCR1			0x008
#define SRC_M4RCR			0x00C

#define BP_SRC_A7RCR0_A7_CORE_RESET0   0
#define BP_SRC_A7RCR1_A7_CORE1_ENABLE  1

static void __iomem *src_base;
static DEFINE_SPINLOCK(src_lock);
static bool m4_is_enabled;

static const int sw_reset_bits[5] = {
	BP_SRC_SCR_SW_GPU_RST,
	BP_SRC_SCR_SW_VPU_RST,
	BP_SRC_SCR_SW_IPU1_RST,
	BP_SRC_SCR_SW_OPEN_VG_RST,
	BP_SRC_SCR_SW_IPU2_RST
};

bool imx_src_is_m4_enabled(void)
{
	return m4_is_enabled;
}

static int imx_src_reset_module(struct reset_controller_dev *rcdev,
		unsigned long sw_reset_idx)
{
	unsigned long timeout;
	unsigned long flags;
	int bit;
	u32 val;

	if (!src_base)
		return -ENODEV;

	if (sw_reset_idx >= ARRAY_SIZE(sw_reset_bits))
		return -EINVAL;

	bit = 1 << sw_reset_bits[sw_reset_idx];

	spin_lock_irqsave(&src_lock, flags);
	val = readl_relaxed(src_base + SRC_SCR);
	val |= bit;
	writel_relaxed(val, src_base + SRC_SCR);
	spin_unlock_irqrestore(&src_lock, flags);

	timeout = jiffies + msecs_to_jiffies(1000);
	while (readl(src_base + SRC_SCR) & bit) {
		if (time_after(jiffies, timeout))
			return -ETIME;
		cpu_relax();
	}

	return 0;
}

static struct reset_control_ops imx_src_ops = {
	.reset = imx_src_reset_module,
};

static struct reset_controller_dev imx_reset_controller = {
	.ops = &imx_src_ops,
	.nr_resets = ARRAY_SIZE(sw_reset_bits),
};

void imx_enable_cpu(int cpu, bool enable)
{
	u32 mask, val;

	cpu = cpu_logical_map(cpu);
	spin_lock(&src_lock);
	if (cpu_is_imx7d()) {
		/* enable core */
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
		spin_unlock(&src_lock);
}

void imx_set_cpu_jump(int cpu, void *jump_addr)
{
	spin_lock(&src_lock);
	cpu = cpu_logical_map(cpu);
	if (cpu_is_imx7d())
		writel_relaxed(virt_to_phys(jump_addr),
			src_base + SRC_GPR1_V2 + cpu * 8);
	else
		writel_relaxed(virt_to_phys(jump_addr),
			src_base + SRC_GPR1 + cpu * 8);
	spin_unlock(&src_lock);
}

u32 imx_get_cpu_arg(int cpu)
{
	cpu = cpu_logical_map(cpu);
	if (cpu_is_imx7d())
		return readl_relaxed(src_base + SRC_GPR1_V2
			+ cpu * 8 + 4);
	else
		return readl_relaxed(src_base + SRC_GPR1
			+ cpu * 8 + 4);
}

void imx_set_cpu_arg(int cpu, u32 arg)
{
	cpu = cpu_logical_map(cpu);
	if (cpu_is_imx7d())
		writel_relaxed(arg, src_base + SRC_GPR1_V2
			+ cpu * 8 + 4);
	else
		writel_relaxed(arg, src_base + SRC_GPR1
			+ cpu * 8 + 4);
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

	if (cpu_is_imx7d()) {
		val = readl_relaxed(src_base + SRC_M4RCR);
		if (((val & BIT(3)) == BIT(3)) && !(val & BIT(0)))
			m4_is_enabled = true;
		else
			m4_is_enabled = false;
		return;
	}

	imx_reset_controller.of_node = np;
	if (IS_ENABLED(CONFIG_RESET_CONTROLLER))
		reset_controller_register(&imx_reset_controller);

	/*
	 * force warm reset sources to generate cold reset
	 * for a more reliable restart
	 */
	spin_lock(&src_lock);
	val = readl_relaxed(src_base + SRC_SCR);

	/* bit 4 is m4c_non_sclr_rst on i.MX6SX */
	if (cpu_is_imx6sx() && ((val &
		(1 << BP_SRC_SCR_SW_OPEN_VG_RST)) == 0))
		m4_is_enabled = true;
	else
		m4_is_enabled = false;

	val &= ~(1 << BP_SRC_SCR_WARM_RESET_ENABLE);
	writel_relaxed(val, src_base + SRC_SCR);
	spin_unlock(&src_lock);
}
