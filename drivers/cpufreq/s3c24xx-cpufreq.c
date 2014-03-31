/*
 * Copyright (c) 2006-2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX CPU Frequency scaling
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/slab.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/cpu.h>
#include <plat/clock.h>
#include <plat/cpu-freq-core.h>

#include <mach/regs-clock.h>

/* note, cpufreq support deals in kHz, no Hz */

static struct cpufreq_driver s3c24xx_driver;
static struct s3c_cpufreq_config cpu_cur;
static struct s3c_iotimings s3c24xx_iotiming;
static struct cpufreq_frequency_table *pll_reg;
static unsigned int last_target = ~0;
static unsigned int ftab_size;
static struct cpufreq_frequency_table *ftab;

static struct clk *_clk_mpll;
static struct clk *_clk_xtal;
static struct clk *clk_fclk;
static struct clk *clk_hclk;
static struct clk *clk_pclk;
static struct clk *clk_arm;

#ifdef CONFIG_ARM_S3C24XX_CPUFREQ_DEBUGFS
struct s3c_cpufreq_config *s3c_cpufreq_getconfig(void)
{
	return &cpu_cur;
}

struct s3c_iotimings *s3c_cpufreq_getiotimings(void)
{
	return &s3c24xx_iotiming;
}
#endif /* CONFIG_ARM_S3C24XX_CPUFREQ_DEBUGFS */

static void s3c_cpufreq_getcur(struct s3c_cpufreq_config *cfg)
{
	unsigned long fclk, pclk, hclk, armclk;

	cfg->freq.fclk = fclk = clk_get_rate(clk_fclk);
	cfg->freq.hclk = hclk = clk_get_rate(clk_hclk);
	cfg->freq.pclk = pclk = clk_get_rate(clk_pclk);
	cfg->freq.armclk = armclk = clk_get_rate(clk_arm);

	cfg->pll.driver_data = __raw_readl(S3C2410_MPLLCON);
	cfg->pll.frequency = fclk;

	cfg->freq.hclk_tns = 1000000000 / (cfg->freq.hclk / 10);

	cfg->divs.h_divisor = fclk / hclk;
	cfg->divs.p_divisor = fclk / pclk;
}

static inline void s3c_cpufreq_calc(struct s3c_cpufreq_config *cfg)
{
	unsigned long pll = cfg->pll.frequency;

	cfg->freq.fclk = pll;
	cfg->freq.hclk = pll / cfg->divs.h_divisor;
	cfg->freq.pclk = pll / cfg->divs.p_divisor;

	/* convert hclk into 10ths of nanoseconds for io calcs */
	cfg->freq.hclk_tns = 1000000000 / (cfg->freq.hclk / 10);
}

static inline int closer(unsigned int target, unsigned int n, unsigned int c)
{
	int diff_cur = abs(target - c);
	int diff_new = abs(target - n);

	return (diff_new < diff_cur);
}

static void s3c_cpufreq_show(const char *pfx,
				 struct s3c_cpufreq_config *cfg)
{
	s3c_freq_dbg("%s: Fvco=%u, F=%lu, A=%lu, H=%lu (%u), P=%lu (%u)\n",
		     pfx, cfg->pll.frequency, cfg->freq.fclk, cfg->freq.armclk,
		     cfg->freq.hclk, cfg->divs.h_divisor,
		     cfg->freq.pclk, cfg->divs.p_divisor);
}

/* functions to wrapper the driver info calls to do the cpu specific work */

static void s3c_cpufreq_setio(struct s3c_cpufreq_config *cfg)
{
	if (cfg->info->set_iotiming)
		(cfg->info->set_iotiming)(cfg, &s3c24xx_iotiming);
}

static int s3c_cpufreq_calcio(struct s3c_cpufreq_config *cfg)
{
	if (cfg->info->calc_iotiming)
		return (cfg->info->calc_iotiming)(cfg, &s3c24xx_iotiming);

	return 0;
}

static void s3c_cpufreq_setrefresh(struct s3c_cpufreq_config *cfg)
{
	(cfg->info->set_refresh)(cfg);
}

static void s3c_cpufreq_setdivs(struct s3c_cpufreq_config *cfg)
{
	(cfg->info->set_divs)(cfg);
}

static int s3c_cpufreq_calcdivs(struct s3c_cpufreq_config *cfg)
{
	return (cfg->info->calc_divs)(cfg);
}

static void s3c_cpufreq_setfvco(struct s3c_cpufreq_config *cfg)
{
	(cfg->info->set_fvco)(cfg);
}

static inline void s3c_cpufreq_resume_clocks(void)
{
	cpu_cur.info->resume_clocks();
}

static inline void s3c_cpufreq_updateclk(struct clk *clk,
					 unsigned int freq)
{
	clk_set_rate(clk, freq);
}

static int s3c_cpufreq_settarget(struct cpufreq_policy *policy,
				 unsigned int target_freq,
				 struct cpufreq_frequency_table *pll)
{
	struct s3c_cpufreq_freqs freqs;
	struct s3c_cpufreq_config cpu_new;
	unsigned long flags;

	cpu_new = cpu_cur;  /* copy new from current */

	s3c_cpufreq_show("cur", &cpu_cur);

	/* TODO - check for DMA currently outstanding */

	cpu_new.pll = pll ? *pll : cpu_cur.pll;

	if (pll)
		freqs.pll_changing = 1;

	/* update our frequencies */

	cpu_new.freq.armclk = target_freq;
	cpu_new.freq.fclk = cpu_new.pll.frequency;

	if (s3c_cpufreq_calcdivs(&cpu_new) < 0) {
		printk(KERN_ERR "no divisors for %d\n", target_freq);
		goto err_notpossible;
	}

	s3c_freq_dbg("%s: got divs\n", __func__);

	s3c_cpufreq_calc(&cpu_new);

	s3c_freq_dbg("%s: calculated frequencies for new\n", __func__);

	if (cpu_new.freq.hclk != cpu_cur.freq.hclk) {
		if (s3c_cpufreq_calcio(&cpu_new) < 0) {
			printk(KERN_ERR "%s: no IO timings\n", __func__);
			goto err_notpossible;
		}
	}

	s3c_cpufreq_show("new", &cpu_new);

	/* setup our cpufreq parameters */

	freqs.old = cpu_cur.freq;
	freqs.new = cpu_new.freq;

	freqs.freqs.old = cpu_cur.freq.armclk / 1000;
	freqs.freqs.new = cpu_new.freq.armclk / 1000;

	/* update f/h/p clock settings before we issue the change
	 * notification, so that drivers do not need to do anything
	 * special if they want to recalculate on CPUFREQ_PRECHANGE. */

	s3c_cpufreq_updateclk(_clk_mpll, cpu_new.pll.frequency);
	s3c_cpufreq_updateclk(clk_fclk, cpu_new.freq.fclk);
	s3c_cpufreq_updateclk(clk_hclk, cpu_new.freq.hclk);
	s3c_cpufreq_updateclk(clk_pclk, cpu_new.freq.pclk);

	/* start the frequency change */
	cpufreq_notify_transition(policy, &freqs.freqs, CPUFREQ_PRECHANGE);

	/* If hclk is staying the same, then we do not need to
	 * re-write the IO or the refresh timings whilst we are changing
	 * speed. */

	local_irq_save(flags);

	/* is our memory clock slowing down? */
	if (cpu_new.freq.hclk < cpu_cur.freq.hclk) {
		s3c_cpufreq_setrefresh(&cpu_new);
		s3c_cpufreq_setio(&cpu_new);
	}

	if (cpu_new.freq.fclk == cpu_cur.freq.fclk) {
		/* not changing PLL, just set the divisors */

		s3c_cpufreq_setdivs(&cpu_new);
	} else {
		if (cpu_new.freq.fclk < cpu_cur.freq.fclk) {
			/* slow the cpu down, then set divisors */

			s3c_cpufreq_setfvco(&cpu_new);
			s3c_cpufreq_setdivs(&cpu_new);
		} else {
			/* set the divisors, then speed up */

			s3c_cpufreq_setdivs(&cpu_new);
			s3c_cpufreq_setfvco(&cpu_new);
		}
	}

	/* did our memory clock speed up */
	if (cpu_new.freq.hclk > cpu_cur.freq.hclk) {
		s3c_cpufreq_setrefresh(&cpu_new);
		s3c_cpufreq_setio(&cpu_new);
	}

	/* update our current settings */
	cpu_cur = cpu_new;

	local_irq_restore(flags);

	/* notify everyone we've done this */
	cpufreq_notify_transition(policy, &freqs.freqs, CPUFREQ_POSTCHANGE);

	s3c_freq_dbg("%s: finished\n", __func__);
	return 0;

 err_notpossible:
	printk(KERN_ERR "no compatible settings for %d\n", target_freq);
	return -EINVAL;
}

/* s3c_cpufreq_target
 *
 * called by the cpufreq core to adjust the frequency that the CPU
 * is currently running at.
 */

static int s3c_cpufreq_target(struct cpufreq_policy *policy,
			      unsigned int target_freq,
			      unsigned int relation)
{
	struct cpufreq_frequency_table *pll;
	unsigned int index;

	/* avoid repeated calls which cause a needless amout of duplicated
	 * logging output (and CPU time as the calculation process is
	 * done) */
	if (target_freq == last_target)
		return 0;

	last_target = target_freq;

	s3c_freq_dbg("%s: policy %p, target %u, relation %u\n",
		     __func__, policy, target_freq, relation);

	if (ftab) {
		if (cpufreq_frequency_table_target(policy, ftab,
						   target_freq, relation,
						   &index)) {
			s3c_freq_dbg("%s: table failed\n", __func__);
			return -EINVAL;
		}

		s3c_freq_dbg("%s: adjust %d to entry %d (%u)\n", __func__,
			     target_freq, index, ftab[index].frequency);
		target_freq = ftab[index].frequency;
	}

	target_freq *= 1000;  /* convert target to Hz */

	/* find the settings for our new frequency */

	if (!pll_reg || cpu_cur.lock_pll) {
		/* either we've not got any PLL values, or we've locked
		 * to the current one. */
		pll = NULL;
	} else {
		struct cpufreq_policy tmp_policy;
		int ret;

		/* we keep the cpu pll table in Hz, to ensure we get an
		 * accurate value for the PLL output. */

		tmp_policy.min = policy->min * 1000;
		tmp_policy.max = policy->max * 1000;
		tmp_policy.cpu = policy->cpu;

		/* cpufreq_frequency_table_target uses a pointer to 'index'
		 * which is the number of the table entry, not the value of
		 * the table entry's index field. */

		ret = cpufreq_frequency_table_target(&tmp_policy, pll_reg,
						     target_freq, relation,
						     &index);

		if (ret < 0) {
			printk(KERN_ERR "%s: no PLL available\n", __func__);
			goto err_notpossible;
		}

		pll = pll_reg + index;

		s3c_freq_dbg("%s: target %u => %u\n",
			     __func__, target_freq, pll->frequency);

		target_freq = pll->frequency;
	}

	return s3c_cpufreq_settarget(policy, target_freq, pll);

 err_notpossible:
	printk(KERN_ERR "no compatible settings for %d\n", target_freq);
	return -EINVAL;
}

struct clk *s3c_cpufreq_clk_get(struct device *dev, const char *name)
{
	struct clk *clk;

	clk = clk_get(dev, name);
	if (IS_ERR(clk))
		printk(KERN_ERR "cpufreq: failed to get clock '%s'\n", name);

	return clk;
}

static int s3c_cpufreq_init(struct cpufreq_policy *policy)
{
	policy->clk = clk_arm;
	return cpufreq_generic_init(policy, ftab, cpu_cur.info->latency);
}

static int __init s3c_cpufreq_initclks(void)
{
	_clk_mpll = s3c_cpufreq_clk_get(NULL, "mpll");
	_clk_xtal = s3c_cpufreq_clk_get(NULL, "xtal");
	clk_fclk = s3c_cpufreq_clk_get(NULL, "fclk");
	clk_hclk = s3c_cpufreq_clk_get(NULL, "hclk");
	clk_pclk = s3c_cpufreq_clk_get(NULL, "pclk");
	clk_arm = s3c_cpufreq_clk_get(NULL, "armclk");

	if (IS_ERR(clk_fclk) || IS_ERR(clk_hclk) || IS_ERR(clk_pclk) ||
	    IS_ERR(_clk_mpll) || IS_ERR(clk_arm) || IS_ERR(_clk_xtal)) {
		printk(KERN_ERR "%s: could not get clock(s)\n", __func__);
		return -ENOENT;
	}

	printk(KERN_INFO "%s: clocks f=%lu,h=%lu,p=%lu,a=%lu\n", __func__,
	       clk_get_rate(clk_fclk) / 1000,
	       clk_get_rate(clk_hclk) / 1000,
	       clk_get_rate(clk_pclk) / 1000,
	       clk_get_rate(clk_arm) / 1000);

	return 0;
}

#ifdef CONFIG_PM
static struct cpufreq_frequency_table suspend_pll;
static unsigned int suspend_freq;

static int s3c_cpufreq_suspend(struct cpufreq_policy *policy)
{
	suspend_pll.frequency = clk_get_rate(_clk_mpll);
	suspend_pll.driver_data = __raw_readl(S3C2410_MPLLCON);
	suspend_freq = clk_get_rate(clk_arm);

	return 0;
}

static int s3c_cpufreq_resume(struct cpufreq_policy *policy)
{
	int ret;

	s3c_freq_dbg("%s: resuming with policy %p\n", __func__, policy);

	last_target = ~0;	/* invalidate last_target setting */

	/* first, find out what speed we resumed at. */
	s3c_cpufreq_resume_clocks();

	/* whilst we will be called later on, we try and re-set the
	 * cpu frequencies as soon as possible so that we do not end
	 * up resuming devices and then immediately having to re-set
	 * a number of settings once these devices have restarted.
	 *
	 * as a note, it is expected devices are not used until they
	 * have been un-suspended and at that time they should have
	 * used the updated clock settings.
	 */

	ret = s3c_cpufreq_settarget(NULL, suspend_freq, &suspend_pll);
	if (ret) {
		printk(KERN_ERR "%s: failed to reset pll/freq\n", __func__);
		return ret;
	}

	return 0;
}
#else
#define s3c_cpufreq_resume NULL
#define s3c_cpufreq_suspend NULL
#endif

static struct cpufreq_driver s3c24xx_driver = {
	.flags		= CPUFREQ_STICKY | CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.target		= s3c_cpufreq_target,
	.get		= cpufreq_generic_get,
	.init		= s3c_cpufreq_init,
	.suspend	= s3c_cpufreq_suspend,
	.resume		= s3c_cpufreq_resume,
	.name		= "s3c24xx",
};


int __init s3c_cpufreq_register(struct s3c_cpufreq_info *info)
{
	if (!info || !info->name) {
		printk(KERN_ERR "%s: failed to pass valid information\n",
		       __func__);
		return -EINVAL;
	}

	printk(KERN_INFO "S3C24XX CPU Frequency driver, %s cpu support\n",
	       info->name);

	/* check our driver info has valid data */

	BUG_ON(info->set_refresh == NULL);
	BUG_ON(info->set_divs == NULL);
	BUG_ON(info->calc_divs == NULL);

	/* info->set_fvco is optional, depending on whether there
	 * is a need to set the clock code. */

	cpu_cur.info = info;

	/* Note, driver registering should probably update locktime */

	return 0;
}

int __init s3c_cpufreq_setboard(struct s3c_cpufreq_board *board)
{
	struct s3c_cpufreq_board *ours;

	if (!board) {
		printk(KERN_INFO "%s: no board data\n", __func__);
		return -EINVAL;
	}

	/* Copy the board information so that each board can make this
	 * initdata. */

	ours = kzalloc(sizeof(*ours), GFP_KERNEL);
	if (ours == NULL) {
		printk(KERN_ERR "%s: no memory\n", __func__);
		return -ENOMEM;
	}

	*ours = *board;
	cpu_cur.board = ours;

	return 0;
}

static int __init s3c_cpufreq_auto_io(void)
{
	int ret;

	if (!cpu_cur.info->get_iotiming) {
		printk(KERN_ERR "%s: get_iotiming undefined\n", __func__);
		return -ENOENT;
	}

	printk(KERN_INFO "%s: working out IO settings\n", __func__);

	ret = (cpu_cur.info->get_iotiming)(&cpu_cur, &s3c24xx_iotiming);
	if (ret)
		printk(KERN_ERR "%s: failed to get timings\n", __func__);

	return ret;
}

/* if one or is zero, then return the other, otherwise return the min */
#define do_min(_a, _b) ((_a) == 0 ? (_b) : (_b) == 0 ? (_a) : min(_a, _b))

/**
 * s3c_cpufreq_freq_min - find the minimum settings for the given freq.
 * @dst: The destination structure
 * @a: One argument.
 * @b: The other argument.
 *
 * Create a minimum of each frequency entry in the 'struct s3c_freq',
 * unless the entry is zero when it is ignored and the non-zero argument
 * used.
 */
static void s3c_cpufreq_freq_min(struct s3c_freq *dst,
				 struct s3c_freq *a, struct s3c_freq *b)
{
	dst->fclk = do_min(a->fclk, b->fclk);
	dst->hclk = do_min(a->hclk, b->hclk);
	dst->pclk = do_min(a->pclk, b->pclk);
	dst->armclk = do_min(a->armclk, b->armclk);
}

static inline u32 calc_locktime(u32 freq, u32 time_us)
{
	u32 result;

	result = freq * time_us;
	result = DIV_ROUND_UP(result, 1000 * 1000);

	return result;
}

static void s3c_cpufreq_update_loctkime(void)
{
	unsigned int bits = cpu_cur.info->locktime_bits;
	u32 rate = (u32)clk_get_rate(_clk_xtal);
	u32 val;

	if (bits == 0) {
		WARN_ON(1);
		return;
	}

	val = calc_locktime(rate, cpu_cur.info->locktime_u) << bits;
	val |= calc_locktime(rate, cpu_cur.info->locktime_m);

	printk(KERN_INFO "%s: new locktime is 0x%08x\n", __func__, val);
	__raw_writel(val, S3C2410_LOCKTIME);
}

static int s3c_cpufreq_build_freq(void)
{
	int size, ret;

	if (!cpu_cur.info->calc_freqtable)
		return -EINVAL;

	kfree(ftab);
	ftab = NULL;

	size = cpu_cur.info->calc_freqtable(&cpu_cur, NULL, 0);
	size++;

	ftab = kmalloc(sizeof(*ftab) * size, GFP_KERNEL);
	if (!ftab) {
		printk(KERN_ERR "%s: no memory for tables\n", __func__);
		return -ENOMEM;
	}

	ftab_size = size;

	ret = cpu_cur.info->calc_freqtable(&cpu_cur, ftab, size);
	s3c_cpufreq_addfreq(ftab, ret, size, CPUFREQ_TABLE_END);

	return 0;
}

static int __init s3c_cpufreq_initcall(void)
{
	int ret = 0;

	if (cpu_cur.info && cpu_cur.board) {
		ret = s3c_cpufreq_initclks();
		if (ret)
			goto out;

		/* get current settings */
		s3c_cpufreq_getcur(&cpu_cur);
		s3c_cpufreq_show("cur", &cpu_cur);

		if (cpu_cur.board->auto_io) {
			ret = s3c_cpufreq_auto_io();
			if (ret) {
				printk(KERN_ERR "%s: failed to get io timing\n",
				       __func__);
				goto out;
			}
		}

		if (cpu_cur.board->need_io && !cpu_cur.info->set_iotiming) {
			printk(KERN_ERR "%s: no IO support registered\n",
			       __func__);
			ret = -EINVAL;
			goto out;
		}

		if (!cpu_cur.info->need_pll)
			cpu_cur.lock_pll = 1;

		s3c_cpufreq_update_loctkime();

		s3c_cpufreq_freq_min(&cpu_cur.max, &cpu_cur.board->max,
				     &cpu_cur.info->max);

		if (cpu_cur.info->calc_freqtable)
			s3c_cpufreq_build_freq();

		ret = cpufreq_register_driver(&s3c24xx_driver);
	}

 out:
	return ret;
}

late_initcall(s3c_cpufreq_initcall);

/**
 * s3c_plltab_register - register CPU PLL table.
 * @plls: The list of PLL entries.
 * @plls_no: The size of the PLL entries @plls.
 *
 * Register the given set of PLLs with the system.
 */
int __init s3c_plltab_register(struct cpufreq_frequency_table *plls,
			       unsigned int plls_no)
{
	struct cpufreq_frequency_table *vals;
	unsigned int size;

	size = sizeof(*vals) * (plls_no + 1);

	vals = kmalloc(size, GFP_KERNEL);
	if (vals) {
		memcpy(vals, plls, size);
		pll_reg = vals;

		/* write a terminating entry, we don't store it in the
		 * table that is stored in the kernel */
		vals += plls_no;
		vals->frequency = CPUFREQ_TABLE_END;

		printk(KERN_INFO "cpufreq: %d PLL entries\n", plls_no);
	} else
		printk(KERN_ERR "cpufreq: no memory for PLL tables\n");

	return vals ? 0 : -ENOMEM;
}
