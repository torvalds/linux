/*
 * Copyright (C) 2012 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/hardware/coresight.h>
#include <linux/amba/bus.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/syscore_ops.h>
#include <plat/cpu.h>

/* CoreSight components */
#define CS_PTM0_BASE	(0x1089c000)
#define CS_PTM1_BASE	(0x1089d000)
#define CS_ETB_BASE	(0x10881000)
#define CS_FUNNEL_BASE	(0x10884000)

#define CS_PTM_COUNT 2

static void __iomem *cs_etb_regs;
static void __iomem *cs_funnel_regs;
static void __iomem *cs_ptm_regs[CS_PTM_COUNT];

static u32 cs_etb_save_state_ffcr;
static u32 cs_etb_save_state_ctl;

static struct cs_ptm_save_state {
	u32 offset;
	u32 val[CS_PTM_COUNT];
} cs_ptm_save_state[] = {
	{ .offset = 0x000 * 4, }, /* ETMCR */
	{ .offset = 0x002 * 4, }, /* ETMTRIGGER */
	{ .offset = 0x004 * 4, }, /* ETMSR */
	{ .offset = 0x006 * 4, }, /* ETMTSSCR */
	{ .offset = 0x008 * 4, }, /* ETMTEEVR */
	{ .offset = 0x009 * 4, }, /* ETMTECR1 */
	{ .offset = 0x00B * 4, }, /* ETMFFLR */
	{ .offset = 0x010 * 4, }, /* ETMACVR0 */
	{ .offset = 0x011 * 4, }, /* ETMACVR1 */
	{ .offset = 0x012 * 4, }, /* ETMACVR2 */
	{ .offset = 0x013 * 4, }, /* ETMACVR3 */
	{ .offset = 0x014 * 4, }, /* ETMACVR4 */
	{ .offset = 0x015 * 4, }, /* ETMACVR5 */
	{ .offset = 0x016 * 4, }, /* ETMACVR6 */
	{ .offset = 0x017 * 4, }, /* ETMACVR7 */
	{ .offset = 0x020 * 4, }, /* ETMACTR0 */
	{ .offset = 0x021 * 4, }, /* ETMACTR1 */
	{ .offset = 0x022 * 4, }, /* ETMACTR2 */
	{ .offset = 0x023 * 4, }, /* ETMACTR3 */
	{ .offset = 0x024 * 4, }, /* ETMACTR4 */
	{ .offset = 0x025 * 4, }, /* ETMACTR5 */
	{ .offset = 0x026 * 4, }, /* ETMACTR6 */
	{ .offset = 0x027 * 4, }, /* ETMACTR7 */
	{ .offset = 0x050 * 4, }, /* ETMCNTRLDVR0 */
	{ .offset = 0x051 * 4, }, /* ETMCNTRLDVR1 */
	{ .offset = 0x054 * 4, }, /* ETMCNTENR0 */
	{ .offset = 0x055 * 4, }, /* ETMCNTENR1 */
	{ .offset = 0x058 * 4, }, /* ETMCNTRLDEVR0 */
	{ .offset = 0x059 * 4, }, /* ETMCNTRLDEVR1 */
	{ .offset = 0x05C * 4, }, /* ETMCNTVR0 */
	{ .offset = 0x05D * 4, }, /* ETMCNTVR1 */
	{ .offset = 0x060 * 4, }, /* ETMSQabEVR */
	{ .offset = 0x067 * 4, }, /* ETMSQR */
	{ .offset = 0x068 * 4, }, /* ETMEXTOUTEVR0 */
	{ .offset = 0x069 * 4, }, /* ETMEXTOUTEVR1 */
	{ .offset = 0x06C * 4, }, /* ETMCIDCVR0 */
	{ .offset = 0x06F * 4, }, /* ETMCIDCMR */
	{ .offset = 0x078 * 4, }, /* ETMSYNCFR */
	{ .offset = 0x07B * 4, }, /* ETMEXTINSELR */
	{ .offset = 0x07E * 4, }, /* ETMTSEVR */
	{ .offset = 0x07F * 4, }, /* ETMAUXCR[a] */
	{ .offset = 0x080 * 4, }, /* ETMTRACEIDR */
	{ .offset = 0x090 * 4, }, /* ETMVMIDCVR[a] */
	{ .offset = 0x3E8 * 4, }, /* ETMCLAIMSET[a] */
	{ .offset = 0x3E9 * 4, }, /* ETMCLAIMCLR[a] */
};

static void cs_unlock(void *regs)
{
	writel(UNLOCK_MAGIC, regs + CSMR_LOCKACCESS);
	readl(regs + CSMR_LOCKSTATUS);
}

static void cs_relock(void *regs)
{
	writel(0, regs + CSMR_LOCKACCESS);
}

static struct amba_device s5p_device_etb = {
	.dev = {
		.init_name	= "etb",
	},
	.res = {
		.start		= CS_ETB_BASE,
		.end		= CS_ETB_BASE + SZ_4K - 1,
		.flags		= IORESOURCE_MEM,
	},
	.periphid		= 0x002bb907,
};

static struct amba_device s5p_device_ptm0 = {
	.dev = {
		.init_name	= "ptm0",
	},
	.res = {
		.start		= CS_PTM0_BASE,
		.end		= CS_PTM0_BASE + SZ_4K - 1,
		.flags		= IORESOURCE_MEM,
	},
	.periphid		= 0x001bb950,
};

static struct amba_device s5p_device_ptm1 = {
	.dev = {
		.init_name	= "ptm1",
	},
	.res = {
		.start		= CS_PTM1_BASE,
		.end		= CS_PTM1_BASE + SZ_4K - 1,
		.flags		= IORESOURCE_MEM,
	},
	.periphid		= 0x001bb950,
};

static void cs_init_static_regs(void)
{
	cs_unlock(cs_funnel_regs);

	/*
	 * Set PTM1 to higher priority than PTM0 so cs_suspend_cpu(1)
	 * can succeed during suspend if tracing is active. This trick will
	 * not work if CPU0 is ever powered down before CPU1.
	 */
	writel(0x00fac681, cs_funnel_regs + 0x4);

	/* enable PTM0 and PTM1 (port 0 and 1) */
	writel(0x303, cs_funnel_regs);

	cs_relock(cs_funnel_regs);
}

static int cs_ptm_wait_progbit(int cpu)
{
	int i;
	u32 etmsr;
	u32 etmpdsr;

	for (i = 0; i < 50000; i++) {
		etmsr = readl(cs_ptm_regs[cpu] + ETMR_STATUS);
		if (etmsr & ETMST_PROGBIT) {
			pr_debug("%s: %d done, loop count %d\n",
				 __func__, cpu, i);
			return 0;
		}
		udelay(10);
	};
	etmpdsr = readl(cs_ptm_regs[cpu] + ETMMR_PDSR);
	pr_err("%s: cpu %d timeout, etmsr %x, etmpdsr %x\n",
	       __func__, cpu, etmsr, etmpdsr);
	return -ETIMEDOUT;
}

static void cs_suspend_cpu(int cpu)
{
	int i;
	u32 etmpdsr;

	pr_debug("%s: cpu %d\n", __func__, cpu);
	cs_unlock(cs_ptm_regs[cpu]);

	etmpdsr = readl(cs_ptm_regs[cpu] + ETMMR_PDSR);
	if (!(etmpdsr & BIT(0))) {
		pr_err("%s: skip save: ptm%d is powered down, etmpdsr %x\n",
			__func__, cpu, etmpdsr);
		goto err;
	}
	pr_debug("%s: cpu %d, etmpdsr %x\n", __func__, cpu, etmpdsr);

	/* Set OS-Lock and empty fifo */
	writel(UNLOCK_MAGIC, cs_ptm_regs[cpu] + ETMMR_OSLAR);
	cs_ptm_wait_progbit(cpu);

	for (i = 0; i < ARRAY_SIZE(cs_ptm_save_state); i++)
		cs_ptm_save_state[i].val[cpu] =
			readl(cs_ptm_regs[cpu] + cs_ptm_save_state[i].offset);

err:
	cs_relock(cs_ptm_regs[cpu]);
	pr_debug("%s: %d done\n", __func__, cpu);
}

static void cs_resume_cpu(int cpu)
{
	int i;
	u32 etmpdsr;

	pr_debug("%s: cpu %d\n", __func__, cpu);

	cs_unlock(cs_ptm_regs[cpu]);

	/* Read ETMPDSR to clear Sticky-Register-State bit */
	etmpdsr = readl(cs_ptm_regs[cpu] + ETMMR_PDSR);
	if (!(etmpdsr & BIT(0))) {
		pr_err("%s: skip restore: ptm%d is powered down, etmpdsr %x\n",
			__func__, cpu, etmpdsr);
		goto err;
	}
	pr_debug("%s: cpu %d, etmpdsr %x\n", __func__, cpu, etmpdsr);

	for (i = 0; i < ARRAY_SIZE(cs_ptm_save_state); i++)
		writel(cs_ptm_save_state[i].val[cpu],
			cs_ptm_regs[cpu] + cs_ptm_save_state[i].offset);

	/* Clear OS-Lock */
	writel(0, cs_ptm_regs[cpu] + ETMMR_OSLAR);

err:
	cs_relock(cs_ptm_regs[cpu]);
	pr_debug("%s: cpu %d done\n", __func__, cpu);
}

static int cs_suspend(void)
{
	pr_debug("%s\n", __func__);

	cs_unlock(cs_etb_regs);
	cs_etb_save_state_ctl = readl(cs_etb_regs + ETBR_CTRL);
	cs_etb_save_state_ffcr = readl(cs_etb_regs + ETBR_FORMATTERCTRL);
	cs_relock(cs_etb_regs);

	pr_debug("%s done\n", __func__);
	return 0;
}

static void cs_resume(void)
{
	pr_debug("%s\n", __func__);

	cs_init_static_regs();

	cs_unlock(cs_etb_regs);
	writel(cs_etb_save_state_ffcr, cs_etb_regs + ETBR_FORMATTERCTRL);
	writel(cs_etb_save_state_ctl, cs_etb_regs + ETBR_CTRL);
	cs_relock(cs_etb_regs);

	pr_debug("%s done\n", __func__);
}

static int cs_cpu_pm_notifier(struct notifier_block *self,
			      unsigned long cmd, void *v)
{
	int cpu = smp_processor_id();
	switch (cmd) {
	case CPU_PM_ENTER:
		cs_suspend_cpu(cpu);
		break;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		cs_resume_cpu(cpu);
		break;
	case CPU_CLUSTER_PM_ENTER:
		break;
	case CPU_CLUSTER_PM_ENTER_FAILED:
	case CPU_CLUSTER_PM_EXIT:
		break;
	}

	return NOTIFY_OK;
}

static int __cpuinit cs_cpu_notifier(struct notifier_block *nfb,
				     unsigned long action, void *hcpu)
{
	int cpu = (unsigned long)hcpu;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
	case CPU_DOWN_FAILED:
		cs_resume_cpu(cpu);
		break;
	case CPU_DYING:
		cs_suspend_cpu(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct syscore_ops cs_syscore_ops = {
	.suspend = cs_suspend,
	.resume = cs_resume,
};

static struct notifier_block __cpuinitdata cs_cpu_pm_notifier_block = {
	.notifier_call = cs_cpu_pm_notifier,
};

static struct notifier_block __cpuinitdata cs_cpu_notifier_block = {
	.notifier_call = cs_cpu_notifier,
};

static int __init cs_exynos5_init(void)
{
	int i;
	int ret;

	if (!soc_is_exynos5250())
		return -ENODEV;

	cs_etb_regs = ioremap(CS_ETB_BASE, SZ_4K);
	if (!cs_etb_regs)
		return -ENOMEM;

	cs_funnel_regs = ioremap(CS_FUNNEL_BASE, SZ_4K);
	if (!cs_funnel_regs)
		return -ENOMEM;

	cs_ptm_regs[0] = ioremap(CS_PTM0_BASE, SZ_4K);
	if (!cs_ptm_regs[0])
		return -ENOMEM;

	cs_ptm_regs[1] = ioremap(CS_PTM1_BASE, SZ_4K);
	if (!cs_ptm_regs[1])
		return -ENOMEM;

	cs_init_static_regs();
	for (i = 0; i < ARRAY_SIZE(cs_ptm_regs); i++) {
		cs_unlock(cs_ptm_regs[i]);
		writel(0, cs_ptm_regs[i] + ETMMR_OSLAR);
		cs_relock(cs_ptm_regs[i]);
	}

	ret = cpu_pm_register_notifier(&cs_cpu_pm_notifier_block);
	if (ret < 0)
		return ret;

	ret = register_cpu_notifier(&cs_cpu_notifier_block);
	if (ret < 0)
		return ret;

	register_syscore_ops(&cs_syscore_ops);

	ret = amba_device_register(&s5p_device_etb, &iomem_resource);
	if (ret < 0)
		return ret;
	ret = amba_device_register(&s5p_device_ptm0, &iomem_resource);
	if (ret < 0)
		return ret;
	ret = amba_device_register(&s5p_device_ptm1, &iomem_resource);
	if (ret < 0)
		return ret;

	return 0;
}

subsys_initcall(cs_exynos5_init);
