/*
 * Broadcom STB CPU SMP and hotplug support for ARM
 *
 * Copyright (C) 2013-2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/printk.h>
#include <linux/regmap.h>
#include <linux/smp.h>
#include <linux/mfd/syscon.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/mach-types.h>
#include <asm/smp_plat.h>

enum {
	ZONE_MAN_CLKEN_MASK		= BIT(0),
	ZONE_MAN_RESET_CNTL_MASK	= BIT(1),
	ZONE_MAN_MEM_PWR_MASK		= BIT(4),
	ZONE_RESERVED_1_MASK		= BIT(5),
	ZONE_MAN_ISO_CNTL_MASK		= BIT(6),
	ZONE_MANUAL_CONTROL_MASK	= BIT(7),
	ZONE_PWR_DN_REQ_MASK		= BIT(9),
	ZONE_PWR_UP_REQ_MASK		= BIT(10),
	ZONE_BLK_RST_ASSERT_MASK	= BIT(12),
	ZONE_PWR_OFF_STATE_MASK		= BIT(25),
	ZONE_PWR_ON_STATE_MASK		= BIT(26),
	ZONE_DPG_PWR_STATE_MASK		= BIT(28),
	ZONE_MEM_PWR_STATE_MASK		= BIT(29),
	ZONE_RESET_STATE_MASK		= BIT(31),
	CPU0_PWR_ZONE_CTRL_REG		= 1,
	CPU_RESET_CONFIG_REG		= 2,
};

static void __iomem *cpubiuctrl_block;
static void __iomem *hif_cont_block;
static u32 cpu0_pwr_zone_ctrl_reg;
static u32 cpu_rst_cfg_reg;
static u32 hif_cont_reg;

#ifdef CONFIG_HOTPLUG_CPU
/*
 * We must quiesce a dying CPU before it can be killed by the boot CPU. Because
 * one or more cache may be disabled, we must flush to ensure coherency. We
 * cannot use traditionl completion structures or spinlocks as they rely on
 * coherency.
 */
static DEFINE_PER_CPU_ALIGNED(int, per_cpu_sw_state);

static int per_cpu_sw_state_rd(u32 cpu)
{
	sync_cache_r(SHIFT_PERCPU_PTR(&per_cpu_sw_state, per_cpu_offset(cpu)));
	return per_cpu(per_cpu_sw_state, cpu);
}

static void per_cpu_sw_state_wr(u32 cpu, int val)
{
	dmb();
	per_cpu(per_cpu_sw_state, cpu) = val;
	sync_cache_w(SHIFT_PERCPU_PTR(&per_cpu_sw_state, per_cpu_offset(cpu)));
}
#else
static inline void per_cpu_sw_state_wr(u32 cpu, int val) { }
#endif

static void __iomem *pwr_ctrl_get_base(u32 cpu)
{
	void __iomem *base = cpubiuctrl_block + cpu0_pwr_zone_ctrl_reg;
	base += (cpu_logical_map(cpu) * 4);
	return base;
}

static u32 pwr_ctrl_rd(u32 cpu)
{
	void __iomem *base = pwr_ctrl_get_base(cpu);
	return readl_relaxed(base);
}

static void pwr_ctrl_set(unsigned int cpu, u32 val, u32 mask)
{
	void __iomem *base = pwr_ctrl_get_base(cpu);
	writel((readl(base) & mask) | val, base);
}

static void pwr_ctrl_clr(unsigned int cpu, u32 val, u32 mask)
{
	void __iomem *base = pwr_ctrl_get_base(cpu);
	writel((readl(base) & mask) & ~val, base);
}

#define POLL_TMOUT_MS 500
static int pwr_ctrl_wait_tmout(unsigned int cpu, u32 set, u32 mask)
{
	const unsigned long timeo = jiffies + msecs_to_jiffies(POLL_TMOUT_MS);
	u32 tmp;

	do {
		tmp = pwr_ctrl_rd(cpu) & mask;
		if (!set == !tmp)
			return 0;
	} while (time_before(jiffies, timeo));

	tmp = pwr_ctrl_rd(cpu) & mask;
	if (!set == !tmp)
		return 0;

	return -ETIMEDOUT;
}

static void cpu_rst_cfg_set(u32 cpu, int set)
{
	u32 val;
	val = readl_relaxed(cpubiuctrl_block + cpu_rst_cfg_reg);
	if (set)
		val |= BIT(cpu_logical_map(cpu));
	else
		val &= ~BIT(cpu_logical_map(cpu));
	writel_relaxed(val, cpubiuctrl_block + cpu_rst_cfg_reg);
}

static void cpu_set_boot_addr(u32 cpu, unsigned long boot_addr)
{
	const int reg_ofs = cpu_logical_map(cpu) * 8;
	writel_relaxed(0, hif_cont_block + hif_cont_reg + reg_ofs);
	writel_relaxed(boot_addr, hif_cont_block + hif_cont_reg + 4 + reg_ofs);
}

static void brcmstb_cpu_boot(u32 cpu)
{
	/* Mark this CPU as "up" */
	per_cpu_sw_state_wr(cpu, 1);

	/*
	 * Set the reset vector to point to the secondary_startup
	 * routine
	 */
	cpu_set_boot_addr(cpu, virt_to_phys(secondary_startup));

	/* Unhalt the cpu */
	cpu_rst_cfg_set(cpu, 0);
}

static void brcmstb_cpu_power_on(u32 cpu)
{
	/*
	 * The secondary cores power was cut, so we must go through
	 * power-on initialization.
	 */
	pwr_ctrl_set(cpu, ZONE_MAN_ISO_CNTL_MASK, 0xffffff00);
	pwr_ctrl_set(cpu, ZONE_MANUAL_CONTROL_MASK, -1);
	pwr_ctrl_set(cpu, ZONE_RESERVED_1_MASK, -1);

	pwr_ctrl_set(cpu, ZONE_MAN_MEM_PWR_MASK, -1);

	if (pwr_ctrl_wait_tmout(cpu, 1, ZONE_MEM_PWR_STATE_MASK))
		panic("ZONE_MEM_PWR_STATE_MASK set timeout");

	pwr_ctrl_set(cpu, ZONE_MAN_CLKEN_MASK, -1);

	if (pwr_ctrl_wait_tmout(cpu, 1, ZONE_DPG_PWR_STATE_MASK))
		panic("ZONE_DPG_PWR_STATE_MASK set timeout");

	pwr_ctrl_clr(cpu, ZONE_MAN_ISO_CNTL_MASK, -1);
	pwr_ctrl_set(cpu, ZONE_MAN_RESET_CNTL_MASK, -1);
}

static int brcmstb_cpu_get_power_state(u32 cpu)
{
	int tmp = pwr_ctrl_rd(cpu);
	return (tmp & ZONE_RESET_STATE_MASK) ? 0 : 1;
}

#ifdef CONFIG_HOTPLUG_CPU

static void brcmstb_cpu_die(u32 cpu)
{
	v7_exit_coherency_flush(all);

	per_cpu_sw_state_wr(cpu, 0);

	/* Sit and wait to die */
	wfi();

	/* We should never get here... */
	while (1)
		;
}

static int brcmstb_cpu_kill(u32 cpu)
{
	/*
	 * Ordinarily, the hardware forbids power-down of CPU0 (which is good
	 * because it is the boot CPU), but this is not true when using BPCM
	 * manual mode.  Consequently, we must avoid turning off CPU0 here to
	 * ensure that TI2C master reset will work.
	 */
	if (cpu == 0) {
		pr_warn("SMP: refusing to power off CPU0\n");
		return 1;
	}

	while (per_cpu_sw_state_rd(cpu))
		;

	pwr_ctrl_set(cpu, ZONE_MANUAL_CONTROL_MASK, -1);
	pwr_ctrl_clr(cpu, ZONE_MAN_RESET_CNTL_MASK, -1);
	pwr_ctrl_clr(cpu, ZONE_MAN_CLKEN_MASK, -1);
	pwr_ctrl_set(cpu, ZONE_MAN_ISO_CNTL_MASK, -1);
	pwr_ctrl_clr(cpu, ZONE_MAN_MEM_PWR_MASK, -1);

	if (pwr_ctrl_wait_tmout(cpu, 0, ZONE_MEM_PWR_STATE_MASK))
		panic("ZONE_MEM_PWR_STATE_MASK clear timeout");

	pwr_ctrl_clr(cpu, ZONE_RESERVED_1_MASK, -1);

	if (pwr_ctrl_wait_tmout(cpu, 0, ZONE_DPG_PWR_STATE_MASK))
		panic("ZONE_DPG_PWR_STATE_MASK clear timeout");

	/* Flush pipeline before resetting CPU */
	mb();

	/* Assert reset on the CPU */
	cpu_rst_cfg_set(cpu, 1);

	return 1;
}

#endif /* CONFIG_HOTPLUG_CPU */

static int __init setup_hifcpubiuctrl_regs(struct device_node *np)
{
	int rc = 0;
	char *name;
	struct device_node *syscon_np = NULL;

	name = "syscon-cpu";

	syscon_np = of_parse_phandle(np, name, 0);
	if (!syscon_np) {
		pr_err("can't find phandle %s\n", name);
		rc = -EINVAL;
		goto cleanup;
	}

	cpubiuctrl_block = of_iomap(syscon_np, 0);
	if (!cpubiuctrl_block) {
		pr_err("iomap failed for cpubiuctrl_block\n");
		rc = -EINVAL;
		goto cleanup;
	}

	rc = of_property_read_u32_index(np, name, CPU0_PWR_ZONE_CTRL_REG,
					&cpu0_pwr_zone_ctrl_reg);
	if (rc) {
		pr_err("failed to read 1st entry from %s property (%d)\n", name,
			rc);
		rc = -EINVAL;
		goto cleanup;
	}

	rc = of_property_read_u32_index(np, name, CPU_RESET_CONFIG_REG,
					&cpu_rst_cfg_reg);
	if (rc) {
		pr_err("failed to read 2nd entry from %s property (%d)\n", name,
			rc);
		rc = -EINVAL;
		goto cleanup;
	}

cleanup:
	of_node_put(syscon_np);
	return rc;
}

static int __init setup_hifcont_regs(struct device_node *np)
{
	int rc = 0;
	char *name;
	struct device_node *syscon_np = NULL;

	name = "syscon-cont";

	syscon_np = of_parse_phandle(np, name, 0);
	if (!syscon_np) {
		pr_err("can't find phandle %s\n", name);
		rc = -EINVAL;
		goto cleanup;
	}

	hif_cont_block = of_iomap(syscon_np, 0);
	if (!hif_cont_block) {
		pr_err("iomap failed for hif_cont_block\n");
		rc = -EINVAL;
		goto cleanup;
	}

	/* Offset is at top of hif_cont_block */
	hif_cont_reg = 0;

cleanup:
	of_node_put(syscon_np);
	return rc;
}

static void __init brcmstb_cpu_ctrl_setup(unsigned int max_cpus)
{
	int rc;
	struct device_node *np;
	char *name;

	name = "brcm,brcmstb-smpboot";
	np = of_find_compatible_node(NULL, NULL, name);
	if (!np) {
		pr_err("can't find compatible node %s\n", name);
		return;
	}

	rc = setup_hifcpubiuctrl_regs(np);
	if (rc)
		return;

	rc = setup_hifcont_regs(np);
	if (rc)
		return;
}

static int brcmstb_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	/* Missing the brcm,brcmstb-smpboot DT node? */
	if (!cpubiuctrl_block || !hif_cont_block)
		return -ENODEV;

	/* Bring up power to the core if necessary */
	if (brcmstb_cpu_get_power_state(cpu) == 0)
		brcmstb_cpu_power_on(cpu);

	brcmstb_cpu_boot(cpu);

	return 0;
}

static const struct smp_operations brcmstb_smp_ops __initconst = {
	.smp_prepare_cpus	= brcmstb_cpu_ctrl_setup,
	.smp_boot_secondary	= brcmstb_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill		= brcmstb_cpu_kill,
	.cpu_die		= brcmstb_cpu_die,
#endif
};

CPU_METHOD_OF_DECLARE(brcmstb_smp, "brcm,brahma-b15", &brcmstb_smp_ops);
