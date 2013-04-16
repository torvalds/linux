#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/of.h>

/*
 * The Nomadik clock tree is described in the STN8815A12 DB V4.2
 * reference manual for the chip, page 94 ff.
 */

static const __initconst struct of_device_id cpu8815_clk_match[] = {
	{ .compatible = "fixed-clock", .data = of_fixed_clk_setup, },
	{ /* sentinel */ }
};

void __init nomadik_clk_init(void)
{
	struct clk *clk;

	clk = clk_register_fixed_rate(NULL, "apb_pclk", NULL, CLK_IS_ROOT, 0);
	clk_register_clkdev(clk, "apb_pclk", NULL);

	/*
	 * The 2.4 MHz TIMCLK reference clock is active at boot time, this is
	 * actually the MXTALCLK @19.2 MHz divided by 8. This clock is used
	 * by the timers and watchdog. See page 105 ff.
	 */
	clk = clk_register_fixed_rate(NULL, "TIMCLK", NULL, CLK_IS_ROOT,
				      2400000);
	clk_register_clkdev(clk, NULL, "mtu0");
	clk_register_clkdev(clk, NULL, "mtu1");

	of_clk_init(cpu8815_clk_match);
}
