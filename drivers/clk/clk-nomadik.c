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
	of_clk_init(cpu8815_clk_match);
}
