/*
 * arch/arm/mach-tegra/suspend.c
 *
 * CPU complex suspend & resume functions for Tegra SoCs
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/suspend.h>
#include <linux/slab.h>
#include <linux/serial_reg.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include <linux/regulator/machine.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <asm/localtimer.h>
#include <asm/tlbflush.h>

#include <mach/iomap.h>
#include <mach/iovmm.h>
#include <mach/irqs.h>
#include <mach/legacy_irq.h>
#include <mach/suspend.h>

#include "power.h"

/* NOTE: only add elements to the end of this structure, since the assembly
 * code uses hard-coded offsets */
struct suspend_context
{
	u32 cpu_burst;
	u32 clk_csite_src;
	u32 pllx_base;
	u32 pllx_misc;
	u32 pllx_timeout;
	u32 twd_ctrl;
	u32 twd_load;
	u32 cclk_divider;
};

volatile struct suspend_context tegra_sctx;

#ifdef CONFIG_HOTPLUG_CPU
static void __iomem *pmc = IO_ADDRESS(TEGRA_PMC_BASE);
static void __iomem *clk_rst = IO_ADDRESS(TEGRA_CLK_RESET_BASE);
static void __iomem *flow_ctrl = IO_ADDRESS(TEGRA_FLOW_CTRL_BASE);
static void __iomem *evp_reset = IO_ADDRESS(TEGRA_EXCEPTION_VECTORS_BASE)+0x100;
static void __iomem *tmrus = IO_ADDRESS(TEGRA_TMRUS_BASE);
#endif

#define PMC_CTRL		0x0
#define PMC_CTRL_LATCH_WAKEUPS	(1 << 5)
#define PMC_WAKE_MASK		0xc
#define PMC_WAKE_LEVEL		0x10
#define PMC_DPAD_ORIDE		0x1C
#define PMC_WAKE_DELAY		0xe0
#define PMC_DPD_SAMPLE  	0x20

#define PMC_WAKE_STATUS		0x14
#define PMC_SW_WAKE_STATUS	0x18
#define PMC_COREPWRGOOD_TIMER	0x3c
#define PMC_SCRATCH0		0x50
#define PMC_SCRATCH1		0x54
#define PMC_CPUPWRGOOD_TIMER	0xc8
#define PMC_CPUPWROFF_TIMER	0xcc
#define PMC_COREPWROFF_TIMER	PMC_WAKE_DELAY
#define PMC_SCRATCH38		0x134
#define PMC_SCRATCH39		0x138
#define PMC_SCRATCH41		0x140

#define CLK_RESET_CCLK_BURST	0x20
#define CLK_RESET_CCLK_DIVIDER  0x24
#define CLK_RESET_PLLC_BASE	0x80
#define CLK_RESET_PLLM_BASE	0x90
#define CLK_RESET_PLLX_BASE	0xe0
#define CLK_RESET_PLLX_MISC	0xe4
#define CLK_RESET_SOURCE_CSITE	0x1d4


#define CLK_RESET_CCLK_BURST_POLICY_SHIFT 28
#define CLK_RESET_CCLK_BURST_POLICY_PLLM   3
#define CLK_RESET_CCLK_BURST_POLICY_PLLX   8

#define FLOW_CTRL_CPU_CSR	0x8
#define FLOW_CTRL_CPU1_CSR	0x18

static struct clk *tegra_pclk = NULL;
static const struct tegra_suspend_platform_data *pdata = NULL;
static unsigned long wb0_restore = 0;
static enum tegra_suspend_mode current_suspend_mode;

unsigned long tegra_cpu_power_good_time(void)
{
	if (WARN_ON_ONCE(!pdata))
		return 5000;

	return pdata->cpu_timer;
}

unsigned long tegra_cpu_power_off_time(void)
{
	if (WARN_ON_ONCE(!pdata))
		return 5000;

	return pdata->cpu_off_timer;
}

enum tegra_suspend_mode tegra_get_suspend_mode(void)
{
	if (!pdata)
		return TEGRA_SUSPEND_NONE;

	return pdata->suspend_mode;
}

static void set_power_timers(unsigned long us_on, unsigned long us_off,
			     long rate)
{
	static int last_pclk = 0;
	unsigned long long ticks;
	unsigned long long pclk;

	if (WARN_ON_ONCE(rate <= 0))
		pclk = 100000000;
	else
		pclk = rate;

	if (rate != last_pclk) {
		ticks = (us_on * pclk) + 999999ull;
		do_div(ticks, 1000000);
		writel((unsigned long)ticks, pmc + PMC_CPUPWRGOOD_TIMER);

		ticks = (us_off * pclk) + 999999ull;
		do_div(ticks, 1000000);
		writel((unsigned long)ticks, pmc + PMC_CPUPWROFF_TIMER);
		wmb();
	}
	last_pclk = pclk;
}

/*
 * suspend_cpu_complex
 *
 *   disable periodic IRQs used for DVFS to prevent suspend wakeups
 *   disable coresight debug interface
 *
 *
 */
static noinline void restore_cpu_complex(void)
{
	unsigned int reg;

	/* restore original burst policy setting; PLLX state restored
	 * by CPU boot-up code - wait for PLL stabilization if PLLX
	 * was enabled, or if explicitly requested by caller */

	BUG_ON(readl(clk_rst + CLK_RESET_PLLX_BASE) != tegra_sctx.pllx_base);

	if (tegra_sctx.pllx_base & (1<<30)) {
		while (readl(tmrus)-tegra_sctx.pllx_timeout >= 0x80000000UL)
			cpu_relax();
	}
	writel(tegra_sctx.cclk_divider, clk_rst + CLK_RESET_CCLK_DIVIDER);
	writel(tegra_sctx.cpu_burst, clk_rst + CLK_RESET_CCLK_BURST);
	writel(tegra_sctx.clk_csite_src, clk_rst + CLK_RESET_SOURCE_CSITE);

	/* do not power-gate the CPU when flow controlled */
	reg = readl(flow_ctrl + FLOW_CTRL_CPU_CSR);
	reg &= ~((1<<5) | (1<<4) | 1); /* clear WFE bitmask */
	reg |= (1<<14); /* write-1-clear event flag */
	writel(reg, flow_ctrl + FLOW_CTRL_CPU_CSR);
	wmb();

	writel(tegra_sctx.twd_ctrl, twd_base + 0x8);
	writel(tegra_sctx.twd_load, twd_base + 0);

	gic_dist_restore(0);
	get_irq_chip(IRQ_LOCALTIMER)->unmask(IRQ_LOCALTIMER);

	enable_irq(INT_SYS_STATS_MON);
}

static noinline void suspend_cpu_complex(void)
{
	unsigned int reg;
	int i;

	disable_irq(INT_SYS_STATS_MON);

	/* switch coresite to clk_m, save off original source */
	tegra_sctx.clk_csite_src = readl(clk_rst + CLK_RESET_SOURCE_CSITE);
	writel(3<<30, clk_rst + CLK_RESET_SOURCE_CSITE);

	tegra_sctx.cpu_burst = readl(clk_rst + CLK_RESET_CCLK_BURST);
	tegra_sctx.pllx_base = readl(clk_rst + CLK_RESET_PLLX_BASE);
	tegra_sctx.pllx_misc = readl(clk_rst + CLK_RESET_PLLX_MISC);
	tegra_sctx.cclk_divider = readl(clk_rst + CLK_RESET_CCLK_DIVIDER);

	tegra_sctx.twd_ctrl = readl(twd_base + 0x8);
	tegra_sctx.twd_load = readl(twd_base + 0);
	local_timer_stop();

	reg = readl(flow_ctrl + FLOW_CTRL_CPU_CSR);
	/* clear any pending events, set the WFE bitmap to specify just
	 * CPU0, and clear any pending events for this CPU */
	reg &= ~(1<<5); /* clear CPU1 WFE */
	reg |= (1<<14) | (1<<4) | 1; /* enable CPU0 WFE */
	writel(reg, flow_ctrl + FLOW_CTRL_CPU_CSR);
	wmb();

	for (i=1; i<num_present_cpus(); i++) {
		unsigned int offs = FLOW_CTRL_CPU1_CSR + (i-1)*8;
		reg = readl(flow_ctrl + offs);
		writel(reg | (1<<14), flow_ctrl + offs);
		wmb();
	}

	gic_cpu_exit(0);
	gic_dist_save(0);
}

unsigned int tegra_suspend_lp2(unsigned int us)
{
	unsigned int mode;
	unsigned long orig, reg;
	unsigned int remain;

	reg = readl(pmc + PMC_CTRL);
	mode = (reg >> TEGRA_POWER_PMC_SHIFT) & TEGRA_POWER_PMC_MASK;
	mode |= TEGRA_POWER_CPU_PWRREQ_OE;
	if (pdata->separate_req)
		mode |= TEGRA_POWER_PWRREQ_OE;
	else
		mode &= ~TEGRA_POWER_PWRREQ_OE;
	mode &= ~TEGRA_POWER_EFFECT_LP0;

	orig = readl(evp_reset);
	writel(virt_to_phys(tegra_lp2_startup), evp_reset);

	set_power_timers(pdata->cpu_timer, pdata->cpu_off_timer,
			 clk_get_rate(tegra_pclk));

	if (us)
		tegra_lp2_set_trigger(us);

	suspend_cpu_complex();
	flush_cache_all();
	/* structure is written by reset code, so the L2 lines
	 * must be invalidated */
	outer_flush_range(__pa(&tegra_sctx),__pa(&tegra_sctx+1));
	barrier();

	__cortex_a9_save(mode);
	/* return from __cortex_a9_restore */
	barrier();
	restore_cpu_complex();

	remain = tegra_lp2_timer_remain();
	if (us)
		tegra_lp2_set_trigger(0);

	writel(orig, evp_reset);

	return remain;
}

#ifdef CONFIG_PM

/* ensures that sufficient time is passed for a register write to
 * serialize into the 32KHz domain */
static void pmc_32kwritel(u32 val, unsigned long offs)
{
	writel(val, pmc + offs);
	udelay(130);
}

static u8 *iram_save = NULL;
static unsigned int iram_save_size = 0;
static void __iomem *iram_code = IO_ADDRESS(TEGRA_IRAM_CODE_AREA);

static void tegra_suspend_dram(bool do_lp0)
{
	unsigned int mode = TEGRA_POWER_SDRAM_SELFREFRESH;
	unsigned long orig, reg;

	orig = readl(evp_reset);
	/* copy the reset vector and SDRAM shutdown code into IRAM */
	memcpy(iram_save, iram_code, iram_save_size);
	memcpy(iram_code, (void *)__tegra_lp1_reset, iram_save_size);

	set_power_timers(pdata->cpu_timer, pdata->cpu_off_timer, 32768);

	reg = readl(pmc + PMC_CTRL);
	mode |= ((reg >> TEGRA_POWER_PMC_SHIFT) & TEGRA_POWER_PMC_MASK);

	if (!do_lp0) {
		writel(TEGRA_IRAM_CODE_AREA, evp_reset);

		mode |= TEGRA_POWER_CPU_PWRREQ_OE;
		if (pdata->separate_req)
			mode |= TEGRA_POWER_PWRREQ_OE;
		else
			mode &= ~TEGRA_POWER_PWRREQ_OE;
		mode &= ~TEGRA_POWER_EFFECT_LP0;

		tegra_legacy_irq_set_lp1_wake_mask();
	} else {
		u32 boot_flag = readl(pmc + PMC_SCRATCH0);
		pmc_32kwritel(boot_flag | 1, PMC_SCRATCH0);
		pmc_32kwritel(wb0_restore, PMC_SCRATCH1);
		writel(0x0, pmc + PMC_SCRATCH39);
		mode |= TEGRA_POWER_CPU_PWRREQ_OE;
		mode |= TEGRA_POWER_PWRREQ_OE;
		mode |= TEGRA_POWER_EFFECT_LP0;

		/* for platforms where the core & CPU power requests are
		 * combined as a single request to the PMU, transition to
		 * LP0 state by temporarily enabling both requests
		 */
		if (!pdata->separate_req) {
			reg |= ((mode & TEGRA_POWER_PMC_MASK) <<
				TEGRA_POWER_PMC_SHIFT);
			pmc_32kwritel(reg, PMC_CTRL);
			mode &= ~TEGRA_POWER_CPU_PWRREQ_OE;
		}

		tegra_set_lp0_wake_pads(pdata->wake_enb, pdata->wake_high,
			pdata->wake_any);
	}

	suspend_cpu_complex();
	flush_cache_all();
	l2x0_shutdown();

	__cortex_a9_save(mode);
	restore_cpu_complex();

	writel(orig, evp_reset);
	l2x0_restart();

	if (!do_lp0) {
		memcpy(iram_code, iram_save, iram_save_size);
		tegra_legacy_irq_restore_mask();
	} else {
		/* for platforms where the core & CPU power requests are
		 * combined as a single request to the PMU, transition out
		 * of LP0 state by temporarily enabling both requests
		 */
		if (!pdata->separate_req) {
			reg = readl(pmc + PMC_CTRL);
			reg |= (TEGRA_POWER_CPU_PWRREQ_OE << TEGRA_POWER_PMC_SHIFT);
			pmc_32kwritel(reg, PMC_CTRL);
			reg &= ~(TEGRA_POWER_PWRREQ_OE << TEGRA_POWER_PMC_SHIFT);
			writel(reg, pmc + PMC_CTRL);
		}
	}

	wmb();
}

static int tegra_suspend_begin(suspend_state_t state)
{
	return regulator_suspend_prepare(state);
}

static int tegra_suspend_prepare_late(void)
{
	disable_irq(INT_SYS_STATS_MON);
	return tegra_iovmm_suspend();
}

static void tegra_suspend_wake(void)
{
	tegra_iovmm_resume();
	enable_irq(INT_SYS_STATS_MON);
}

static u8 uart_state[5];

static int tegra_debug_uart_suspend(void)
{
	void __iomem *uart;
	u32 lcr;

	if (TEGRA_DEBUG_UART_BASE == 0)
		return 0;

	uart = IO_ADDRESS(TEGRA_DEBUG_UART_BASE);

	lcr = readb(uart + UART_LCR * 4);

	uart_state[0] = lcr;
	uart_state[1] = readb(uart + UART_MCR * 4);

	/* DLAB = 0 */
	writeb(lcr & ~UART_LCR_DLAB, uart + UART_LCR * 4);

	uart_state[2] = readb(uart + UART_IER * 4);

	/* DLAB = 1 */
	writeb(lcr | UART_LCR_DLAB, uart + UART_LCR * 4);

	uart_state[3] = readb(uart + UART_DLL * 4);
	uart_state[4] = readb(uart + UART_DLM * 4);

	writeb(lcr, uart + UART_LCR * 4);

	return 0;
}

static void tegra_debug_uart_resume(void)
{
	void __iomem *uart;
	u32 lcr;

	if (TEGRA_DEBUG_UART_BASE == 0)
		return;

	uart = IO_ADDRESS(TEGRA_DEBUG_UART_BASE);

	lcr = uart_state[0];

	writeb(uart_state[1], uart + UART_MCR * 4);

	/* DLAB = 0 */
	writeb(lcr & ~UART_LCR_DLAB, uart + UART_LCR * 4);

	writeb(uart_state[2], uart + UART_IER * 4);

	/* DLAB = 1 */
	writeb(lcr | UART_LCR_DLAB, uart + UART_LCR * 4);

	writeb(uart_state[3], uart + UART_DLL * 4);
	writeb(uart_state[4], uart + UART_DLM * 4);

	writeb(lcr, uart + UART_LCR * 4);
}

#define MC_SECURITY_START	0x6c
#define MC_SECURITY_SIZE	0x70

static int tegra_suspend_enter(suspend_state_t state)
{
	struct irq_desc *desc;
	void __iomem *mc = IO_ADDRESS(TEGRA_MC_BASE);
	unsigned long flags;
	u32 mc_data[2];
	int irq;
	bool do_lp0 = (current_suspend_mode == TEGRA_SUSPEND_LP0);
	bool do_lp2 = (current_suspend_mode == TEGRA_SUSPEND_LP2);
	int lp_state;

	if (do_lp2)
		lp_state = 2;
	else if (do_lp0)
		lp_state = 0;
	else
		lp_state = 1;

	local_irq_save(flags);

	pr_info("Entering suspend state LP%d\n", lp_state);
	if (do_lp0) {
		tegra_irq_suspend();
		tegra_dma_suspend();
		tegra_debug_uart_suspend();
		tegra_pinmux_suspend();
		tegra_timer_suspend();
		tegra_gpio_suspend();
		tegra_clk_suspend();

		mc_data[0] = readl(mc + MC_SECURITY_START);
		mc_data[1] = readl(mc + MC_SECURITY_SIZE);
	}

	for_each_irq_desc(irq, desc) {
		if ((desc->status & IRQ_WAKEUP) &&
		    (desc->status & IRQ_SUSPENDED)) {
			get_irq_chip(irq)->unmask(irq);
		}
	}

	if (do_lp2)
		tegra_suspend_lp2(0);
	else
		tegra_suspend_dram(do_lp0);

	for_each_irq_desc(irq, desc) {
		if ((desc->status & IRQ_WAKEUP) &&
		    (desc->status & IRQ_SUSPENDED)) {
			get_irq_chip(irq)->mask(irq);
		}
	}

	/* Clear DPD sample */
	writel(0x0, pmc + PMC_DPD_SAMPLE);

	if (do_lp0) {
		writel(mc_data[0], mc + MC_SECURITY_START);
		writel(mc_data[1], mc + MC_SECURITY_SIZE);

		tegra_clk_resume();
		tegra_gpio_resume();
		tegra_timer_resume();
		tegra_pinmux_resume();
		tegra_debug_uart_resume();
		tegra_dma_resume();
		tegra_irq_resume();
	}

	local_irq_restore(flags);

	return 0;
}

static struct platform_suspend_ops tegra_suspend_ops = {
	.valid		= suspend_valid_only_mem,
	.begin		= tegra_suspend_begin,
	.prepare_late	= tegra_suspend_prepare_late,
	.wake		= tegra_suspend_wake,
	.enter		= tegra_suspend_enter,
};
#endif

static unsigned long lp0_vec_orig_start = 0;
static unsigned long lp0_vec_orig_size = 0;

static int __init tegra_lp0_vec_arg(char *options)
{
	char *p = options;

	lp0_vec_orig_size = memparse(p, &p);
	if (*p == '@')
		lp0_vec_orig_start = memparse(p+1, &p);

	return 0;
}
__setup("lp0_vec=", tegra_lp0_vec_arg);

void __init tegra_init_suspend(struct tegra_suspend_platform_data *plat)
{
	u32 reg, mode;

	tegra_pclk = clk_get_sys(NULL, "pclk");
	BUG_ON(!tegra_pclk);
	pdata = plat;
	(void)reg;
	(void)mode;

	if (plat->suspend_mode == TEGRA_SUSPEND_LP0 &&
			lp0_vec_orig_size && lp0_vec_orig_start) {
		unsigned char *reloc_lp0;
		unsigned long tmp;
		void __iomem *orig;
		reloc_lp0 = kmalloc(lp0_vec_orig_size+L1_CACHE_BYTES-1,
				    GFP_KERNEL);
		WARN_ON(!reloc_lp0);
		if (!reloc_lp0)
			goto out;

		orig = ioremap(lp0_vec_orig_start, lp0_vec_orig_size);
		WARN_ON(!orig);
		if (!orig) {
			kfree(reloc_lp0);
			goto out;
		}
		tmp = (unsigned long) reloc_lp0;
		tmp = (tmp + L1_CACHE_BYTES - 1) & ~(L1_CACHE_BYTES-1);
		reloc_lp0 = (unsigned char *)tmp;
		memcpy(reloc_lp0, orig, lp0_vec_orig_size);
		iounmap(orig);
		wb0_restore = virt_to_phys(reloc_lp0);
	}
out:
	if (plat->suspend_mode == TEGRA_SUSPEND_LP0 && !wb0_restore) {
		pr_warning("Suspend mode LP0 requested, but missing lp0_vec\n");
		pr_warning("Disabling LP0\n");
		plat->suspend_mode = TEGRA_SUSPEND_LP1;
	}

#ifdef CONFIG_PM
	iram_save_size = (unsigned long)__tegra_iram_end;
	iram_save_size -= (unsigned long)__tegra_lp1_reset;

	iram_save = kmalloc(iram_save_size, GFP_KERNEL);
	if (!iram_save) {
		pr_err("%s: unable to allocate memory for SDRAM self-refresh "
		       "LP0/LP1 unavailable\n", __func__);
		plat->suspend_mode = TEGRA_SUSPEND_LP2;
	}
	/* CPU reset vector for LP0 and LP1 */
	writel(virt_to_phys(tegra_lp2_startup), pmc + PMC_SCRATCH41);

	/* Always enable CPU power request; just normal polarity is supported */
	reg = readl(pmc + PMC_CTRL);
	BUG_ON(reg & (TEGRA_POWER_CPU_PWRREQ_POLARITY << TEGRA_POWER_PMC_SHIFT));
	reg |= (TEGRA_POWER_CPU_PWRREQ_OE << TEGRA_POWER_PMC_SHIFT);
	pmc_32kwritel(reg, PMC_CTRL);

	/* Configure core power request and system clock control if LP0
	   is supported */
	writel(pdata->core_timer, pmc + PMC_COREPWRGOOD_TIMER);
	writel(pdata->core_off_timer, pmc + PMC_COREPWROFF_TIMER);
	reg = readl(pmc + PMC_CTRL);
	mode = (reg >> TEGRA_POWER_PMC_SHIFT) & TEGRA_POWER_PMC_MASK;

	mode &= ~TEGRA_POWER_SYSCLK_POLARITY;
	mode &= ~TEGRA_POWER_PWRREQ_POLARITY;

	if (!pdata->sysclkreq_high)
		mode |= TEGRA_POWER_SYSCLK_POLARITY;
	if (!pdata->corereq_high)
		mode |= TEGRA_POWER_PWRREQ_POLARITY;

	/* configure output inverters while the request is tristated */
	reg |= (mode << TEGRA_POWER_PMC_SHIFT);
	pmc_32kwritel(reg, PMC_CTRL);

	/* now enable requests */
	reg |= (TEGRA_POWER_SYSCLK_OE << TEGRA_POWER_PMC_SHIFT);
	if (pdata->separate_req)
		reg |= (TEGRA_POWER_PWRREQ_OE << TEGRA_POWER_PMC_SHIFT);
	writel(reg, pmc + PMC_CTRL);

	if (pdata->suspend_mode == TEGRA_SUSPEND_LP0)
		lp0_suspend_init();

	suspend_set_ops(&tegra_suspend_ops);
#endif

	current_suspend_mode = plat->suspend_mode;
}

#ifdef CONFIG_DEBUG_FS
static const char *tegra_suspend_name[TEGRA_MAX_SUSPEND_MODE] = {
	[TEGRA_SUSPEND_NONE]	= "none",
	[TEGRA_SUSPEND_LP2]	= "lp2",
	[TEGRA_SUSPEND_LP1]	= "lp1",
	[TEGRA_SUSPEND_LP0]	= "lp0",
};

static int tegra_suspend_debug_show(struct seq_file *s, void *data)
{
	seq_printf(s, "%s\n", tegra_suspend_name[*(int *)s->private]);
	return 0;
}

static int tegra_suspend_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, tegra_suspend_debug_show, inode->i_private);
}

static int tegra_suspend_debug_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[32];
	int buf_size;
	int i;
	struct seq_file *s = file->private_data;
	enum tegra_suspend_mode *val = s->private;

	memset(buf, 0x00, sizeof(buf));
	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	for (i = 0; i < TEGRA_MAX_SUSPEND_MODE; i++) {
		if (!strnicmp(buf, tegra_suspend_name[i],
		    strlen(tegra_suspend_name[i]))) {
			if (i > pdata->suspend_mode)
				return -EINVAL;
			*val = i;
			return count;
		}
	}

	return -EINVAL;
}

static const struct file_operations tegra_suspend_debug_fops = {
	.open		= tegra_suspend_debug_open,
	.write		= tegra_suspend_debug_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init tegra_suspend_debug_init(void)
{
	struct dentry *d;

	d = debugfs_create_file("suspend_mode", 0755, NULL,
		(void *)&current_suspend_mode, &tegra_suspend_debug_fops);
	if (!d) {
		pr_info("Failed to create suspend_mode debug file\n");
		return -ENOMEM;
	}

	return 0;
}

late_initcall(tegra_suspend_debug_init);
#endif
