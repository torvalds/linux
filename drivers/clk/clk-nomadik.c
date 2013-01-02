#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk-provider.h>

/*
 * The Nomadik clock tree is described in the STN8815A12 DB V4.2
 * reference manual for the chip, page 94 ff.
 */

void __init nomadik_clk_init(void)
{
	struct clk *clk;

	clk = clk_register_fixed_rate(NULL, "apb_pclk", NULL, CLK_IS_ROOT, 0);
	clk_register_clkdev(clk, "apb_pclk", NULL);
	clk_register_clkdev(clk, NULL, "gpio.0");
	clk_register_clkdev(clk, NULL, "gpio.1");
	clk_register_clkdev(clk, NULL, "gpio.2");
	clk_register_clkdev(clk, NULL, "gpio.3");
	clk_register_clkdev(clk, NULL, "rng");
	clk_register_clkdev(clk, NULL, "fsmc-nand");

	/*
	 * The 2.4 MHz TIMCLK reference clock is active at boot time, this is
	 * actually the MXTALCLK @19.2 MHz divided by 8. This clock is used
	 * by the timers and watchdog. See page 105 ff.
	 */
	clk = clk_register_fixed_rate(NULL, "TIMCLK", NULL, CLK_IS_ROOT,
				      2400000);
	clk_register_clkdev(clk, NULL, "mtu0");
	clk_register_clkdev(clk, NULL, "mtu1");

	/*
	 * At boot time, PLL2 is set to generate a set of fixed clocks,
	 * one of them is CLK48, the 48 MHz clock, routed to the UART, MMC/SD
	 * I2C, IrDA, USB and SSP blocks.
	 */
	clk = clk_register_fixed_rate(NULL, "CLK48", NULL, CLK_IS_ROOT,
				      48000000);
	clk_register_clkdev(clk, NULL, "uart0");
	clk_register_clkdev(clk, NULL, "uart1");
	clk_register_clkdev(clk, NULL, "mmci");
	clk_register_clkdev(clk, NULL, "ssp");
	clk_register_clkdev(clk, NULL, "nmk-i2c.0");
	clk_register_clkdev(clk, NULL, "nmk-i2c.1");
}
