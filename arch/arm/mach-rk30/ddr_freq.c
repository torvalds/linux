#include <mach/ddr.h>

#include <linux/earlysuspend.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/clk.h>

#define ddr_print(x...) printk( "DDR DEBUG: " x )

struct ddr {
	int suspend;
	struct early_suspend early_suspend;
	struct clk *ddr_pll;
	struct clk *clk;
};

static void ddr_early_suspend(struct early_suspend *h);
static void ddr_late_resume(struct early_suspend *h);

static struct ddr ddr = {
	.early_suspend = {
		.suspend = ddr_early_suspend,
		.resume = ddr_late_resume,
		.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 50,
	},
};

static volatile bool __sramdata cpu1_pause;
static inline bool is_cpu1_paused(void) { smp_rmb(); return cpu1_pause; }
static inline void set_cpu1_pause(bool pause) { cpu1_pause = pause; smp_wmb(); }
#define MAX_TIMEOUT (16000000UL << 6) //>0.64s

static void __ddr_change_freq(void *info)
{
	uint32_t *value = info;
	u32 timeout = MAX_TIMEOUT;

	while (!is_cpu1_paused() && --timeout);
	if (timeout == 0)
		return;

	*value = ddr_change_freq(*value);

	set_cpu1_pause(false);
}

/* Do not use stack, safe on SMP */
static void __sramfunc pause_cpu1(void *info)
{
	u32 timeout = MAX_TIMEOUT;
	unsigned long flags;
	local_irq_save(flags);

	set_cpu1_pause(true);
	while (is_cpu1_paused() && --timeout);

	local_irq_restore(flags);
}

static uint32_t _ddr_change_freq(uint32_t nMHz)
{
	int this_cpu = get_cpu();

	set_cpu1_pause(false);
	if (this_cpu == 0) {
		if (smp_call_function_single(1, (smp_call_func_t)pause_cpu1, NULL, 0) == 0) {
			u32 timeout = MAX_TIMEOUT;
			while (!is_cpu1_paused() && --timeout);
			if (timeout == 0)
				goto out;
		}

		nMHz = ddr_change_freq(nMHz);

		set_cpu1_pause(false);
	} else {
		smp_call_function_single(0, __ddr_change_freq, &nMHz, 0);

		pause_cpu1(NULL);
	}

out:
	put_cpu();

	return nMHz;
}

uint32_t ddr_set_rate(uint32_t nMHz)
{
	nMHz = _ddr_change_freq(nMHz);
	clk_set_rate(ddr.ddr_pll, 0);
	return nMHz;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static uint32_t ddr_resume_freq;
static uint32_t ddr_suspend_freq = 120 * 1000000;
static void ddr_early_suspend(struct early_suspend *h)
{
	//Enable auto self refresh  0x01*32 DDR clk cycle
	ddr_set_auto_self_refresh(true);

	ddr_resume_freq = clk_get_rate(ddr.clk);

	clk_set_rate(ddr.clk, ddr_suspend_freq);

	ddr_print("%s: freq=%luMHz\n", __func__, clk_get_rate(ddr.clk)/1000000);

	return;
}

static void ddr_late_resume(struct early_suspend *h)
{
	//Disable auto self refresh
	ddr_set_auto_self_refresh(false);

	clk_set_rate(ddr.clk, ddr_resume_freq);

	ddr_print("%s: freq=%luMHz\n", __func__, clk_get_rate(ddr.clk)/1000000);

	return;
}

static int rk30_ddr_late_init (void)
{
	ddr.ddr_pll = clk_get(NULL, "ddr_pll");
	ddr.clk = clk_get(NULL, "ddr");
	register_early_suspend(&ddr.early_suspend);
	return 0;
}
late_initcall(rk30_ddr_late_init);
#endif
