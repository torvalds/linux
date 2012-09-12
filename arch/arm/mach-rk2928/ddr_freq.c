#include <mach/ddr.h>

#include <linux/earlysuspend.h>
#include <linux/delay.h>

#define ddr_print(x...) printk( "DDR DEBUG: " x )

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

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ddr_early_suspend(struct early_suspend *h)
{
    //Enable auto self refresh  0x01*32 DDR clk cycle
    ddr_print("run in %s\n",__func__);
    ddr_set_auto_self_refresh(true);
    
    return;
}

static void ddr_late_resume(struct early_suspend *h)
{
    //Disable auto self refresh
    ddr_print("run in %s\n",__func__);
    ddr_set_auto_self_refresh(false);

    return;
}

static int rk30_ddr_late_init (void)
{
    register_early_suspend(&ddr.early_suspend);
    return 0;
}
late_initcall(rk30_ddr_late_init);
#endif
