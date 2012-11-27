#include <mach/ddr.h>

#include <linux/earlysuspend.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/clk.h>

#define ddr_print(x...) printk( "DDR DEBUG: " x )
static struct clk *ddr_clk;

struct ddr {
	int suspend;
	struct early_suspend early_suspend;
	struct clk *ddr_pll;
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

uint32_t ddr_set_rate(uint32_t nMHz)
{
    //ddr_print("%s freq=%dMHz\n", __func__,nMHz);
    nMHz = ddr_change_freq(nMHz);
	clk_set_rate(ddr.ddr_pll, 0);
    return nMHz;
}
#ifdef CONFIG_HAS_EARLYSUSPEND
static uint32_t ddr_resume_freq=DDR_FREQ;
static uint32_t ddr_suspend_freq=200;
static void ddr_early_suspend(struct early_suspend *h)
{
	uint32_t value;

	//Enable auto self refresh  0x01*32 DDR clk cycle
	ddr_set_auto_self_refresh(true);

	ddr_resume_freq=clk_get_rate(ddr_clk)/1000000;

	clk_set_rate(ddr_clk,ddr_suspend_freq*1000*1000);

	//   value = ddr_set_rate(ddr_suspend_freq);
	ddr_print("init success!!! freq=%dMHz\n", clk_get_rate(ddr_clk));

	return;
}

static void ddr_late_resume(struct early_suspend *h)
{
	uint32_t value;

	//Disable auto self refresh
	ddr_set_auto_self_refresh(false);

	clk_set_rate(ddr_clk,ddr_resume_freq*1000*1000);

	ddr_print("init success!!! freq=%dMHz\n", clk_get_rate(ddr_clk));

    return;
}

static int rk30_ddr_late_init (void)
{
    ddr.ddr_pll = clk_get(NULL, "ddr_pll");
    ddr_clk = clk_get(NULL, "ddr");
    register_early_suspend(&ddr.early_suspend);
    return 0;
}
late_initcall(rk30_ddr_late_init);
#endif
