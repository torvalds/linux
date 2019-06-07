// SPDX-License-Identifier: GPL-2.0
/*
 *	m5441x.c -- support for Coldfire m5441x processors
 *
 *	(C) Copyright Steven King <sfking@fdwdc.com>
 */

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>
#include <asm/mcfdma.h>
#include <asm/mcfclk.h>

DEFINE_CLK(0, "flexbus", 2, MCF_CLK);
DEFINE_CLK(0, "mcfcan.0", 8, MCF_CLK);
DEFINE_CLK(0, "mcfcan.1", 9, MCF_CLK);
DEFINE_CLK(0, "imx1-i2c.1", 14, MCF_CLK);
DEFINE_CLK(0, "mcfdspi.1", 15, MCF_CLK);
DEFINE_CLK(0, "edma", 17, MCF_CLK);
DEFINE_CLK(0, "intc.0", 18, MCF_CLK);
DEFINE_CLK(0, "intc.1", 19, MCF_CLK);
DEFINE_CLK(0, "intc.2", 20, MCF_CLK);
DEFINE_CLK(0, "imx1-i2c.0", 22, MCF_CLK);
DEFINE_CLK(0, "fsl-dspi.0", 23, MCF_CLK);
DEFINE_CLK(0, "mcfuart.0", 24, MCF_BUSCLK);
DEFINE_CLK(0, "mcfuart.1", 25, MCF_BUSCLK);
DEFINE_CLK(0, "mcfuart.2", 26, MCF_BUSCLK);
DEFINE_CLK(0, "mcfuart.3", 27, MCF_BUSCLK);
DEFINE_CLK(0, "mcftmr.0", 28, MCF_CLK);
DEFINE_CLK(0, "mcftmr.1", 29, MCF_CLK);
DEFINE_CLK(0, "mcftmr.2", 30, MCF_CLK);
DEFINE_CLK(0, "mcftmr.3", 31, MCF_CLK);
DEFINE_CLK(0, "mcfpit.0", 32, MCF_CLK);
DEFINE_CLK(0, "mcfpit.1", 33, MCF_CLK);
DEFINE_CLK(0, "mcfpit.2", 34, MCF_CLK);
DEFINE_CLK(0, "mcfpit.3", 35, MCF_CLK);
DEFINE_CLK(0, "mcfeport.0", 37, MCF_CLK);
DEFINE_CLK(0, "mcfadc.0", 38, MCF_CLK);
DEFINE_CLK(0, "mcfdac.0", 39, MCF_CLK);
DEFINE_CLK(0, "mcfrtc.0", 42, MCF_CLK);
DEFINE_CLK(0, "mcfsim.0", 43, MCF_CLK);
DEFINE_CLK(0, "mcfusb-otg.0", 44, MCF_CLK);
DEFINE_CLK(0, "mcfusb-host.0", 45, MCF_CLK);
DEFINE_CLK(0, "mcfddr-sram.0", 46, MCF_CLK);
DEFINE_CLK(0, "mcfssi.0", 47, MCF_CLK);
DEFINE_CLK(0, "pll.0", 48, MCF_CLK);
DEFINE_CLK(0, "mcfrng.0", 49, MCF_CLK);
DEFINE_CLK(0, "mcfssi.1", 50, MCF_CLK);
DEFINE_CLK(0, "mcfsdhc.0", 51, MCF_CLK);
DEFINE_CLK(0, "enet-fec.0", 53, MCF_CLK);
DEFINE_CLK(0, "enet-fec.1", 54, MCF_CLK);
DEFINE_CLK(0, "switch.0", 55, MCF_CLK);
DEFINE_CLK(0, "switch.1", 56, MCF_CLK);
DEFINE_CLK(0, "nand.0", 63, MCF_CLK);

DEFINE_CLK(1, "mcfow.0", 2, MCF_CLK);
DEFINE_CLK(1, "imx1-i2c.2", 4, MCF_CLK);
DEFINE_CLK(1, "imx1-i2c.3", 5, MCF_CLK);
DEFINE_CLK(1, "imx1-i2c.4", 6, MCF_CLK);
DEFINE_CLK(1, "imx1-i2c.5", 7, MCF_CLK);
DEFINE_CLK(1, "mcfuart.4", 24, MCF_BUSCLK);
DEFINE_CLK(1, "mcfuart.5", 25, MCF_BUSCLK);
DEFINE_CLK(1, "mcfuart.6", 26, MCF_BUSCLK);
DEFINE_CLK(1, "mcfuart.7", 27, MCF_BUSCLK);
DEFINE_CLK(1, "mcfuart.8", 28, MCF_BUSCLK);
DEFINE_CLK(1, "mcfuart.9", 29, MCF_BUSCLK);
DEFINE_CLK(1, "mcfpwm.0", 34, MCF_BUSCLK);
DEFINE_CLK(1, "sys.0", 36, MCF_BUSCLK);
DEFINE_CLK(1, "gpio.0", 37, MCF_BUSCLK);

struct clk *mcf_clks[] = {
	&__clk_0_2,
	&__clk_0_8,
	&__clk_0_9,
	&__clk_0_14,
	&__clk_0_15,
	&__clk_0_17,
	&__clk_0_18,
	&__clk_0_19,
	&__clk_0_20,
	&__clk_0_22,
	&__clk_0_23,
	&__clk_0_24,
	&__clk_0_25,
	&__clk_0_26,
	&__clk_0_27,
	&__clk_0_28,
	&__clk_0_29,
	&__clk_0_30,
	&__clk_0_31,
	&__clk_0_32,
	&__clk_0_33,
	&__clk_0_34,
	&__clk_0_35,
	&__clk_0_37,
	&__clk_0_38,
	&__clk_0_39,
	&__clk_0_42,
	&__clk_0_43,
	&__clk_0_44,
	&__clk_0_45,
	&__clk_0_46,
	&__clk_0_47,
	&__clk_0_48,
	&__clk_0_49,
	&__clk_0_50,
	&__clk_0_51,
	&__clk_0_53,
	&__clk_0_54,
	&__clk_0_55,
	&__clk_0_56,
	&__clk_0_63,

	&__clk_1_2,
	&__clk_1_4,
	&__clk_1_5,
	&__clk_1_6,
	&__clk_1_7,
	&__clk_1_24,
	&__clk_1_25,
	&__clk_1_26,
	&__clk_1_27,
	&__clk_1_28,
	&__clk_1_29,
	&__clk_1_34,
	&__clk_1_36,
	&__clk_1_37,
	NULL,
};


static struct clk * const enable_clks[] __initconst = {
	/* make sure these clocks are enabled */
	&__clk_0_15, /* dspi.1 */
	&__clk_0_17, /* eDMA */
	&__clk_0_18, /* intc0 */
	&__clk_0_19, /* intc0 */
	&__clk_0_20, /* intc0 */
	&__clk_0_23, /* dspi.0 */
	&__clk_0_24, /* uart0 */
	&__clk_0_25, /* uart1 */
	&__clk_0_26, /* uart2 */
	&__clk_0_27, /* uart3 */

	&__clk_0_33, /* pit.1 */
	&__clk_0_37, /* eport */
	&__clk_0_48, /* pll */

	&__clk_1_36, /* CCM/reset module/Power management */
	&__clk_1_37, /* gpio */
};
static struct clk * const disable_clks[] __initconst = {
	&__clk_0_8, /* can.0 */
	&__clk_0_9, /* can.1 */
	&__clk_0_14, /* i2c.1 */
	&__clk_0_22, /* i2c.0 */
	&__clk_0_23, /* dspi.0 */
	&__clk_0_28, /* tmr.1 */
	&__clk_0_29, /* tmr.2 */
	&__clk_0_30, /* tmr.2 */
	&__clk_0_31, /* tmr.3 */
	&__clk_0_32, /* pit.0 */
	&__clk_0_34, /* pit.2 */
	&__clk_0_35, /* pit.3 */
	&__clk_0_38, /* adc */
	&__clk_0_39, /* dac */
	&__clk_0_44, /* usb otg */
	&__clk_0_45, /* usb host */
	&__clk_0_47, /* ssi.0 */
	&__clk_0_49, /* rng */
	&__clk_0_50, /* ssi.1 */
	&__clk_0_51, /* eSDHC */
	&__clk_0_53, /* enet-fec */
	&__clk_0_54, /* enet-fec */
	&__clk_0_55, /* switch.0 */
	&__clk_0_56, /* switch.1 */

	&__clk_1_2, /* 1-wire */
	&__clk_1_4, /* i2c.2 */
	&__clk_1_5, /* i2c.3 */
	&__clk_1_6, /* i2c.4 */
	&__clk_1_7, /* i2c.5 */
	&__clk_1_24, /* uart 4 */
	&__clk_1_25, /* uart 5 */
	&__clk_1_26, /* uart 6 */
	&__clk_1_27, /* uart 7 */
	&__clk_1_28, /* uart 8 */
	&__clk_1_29, /* uart 9 */
};

static void __init m5441x_clk_init(void)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(enable_clks); ++i)
		__clk_init_enabled(enable_clks[i]);
	/* make sure these clocks are disabled */
	for (i = 0; i < ARRAY_SIZE(disable_clks); ++i)
		__clk_init_disabled(disable_clks[i]);
}

static void __init m5441x_uarts_init(void)
{
	__raw_writeb(0x0f, MCFGPIO_PAR_UART0);
	__raw_writeb(0x00, MCFGPIO_PAR_UART1);
	__raw_writeb(0x00, MCFGPIO_PAR_UART2);
}

static void __init m5441x_fec_init(void)
{
	__raw_writeb(0x03, MCFGPIO_PAR_FEC);
}

void __init config_BSP(char *commandp, int size)
{
	m5441x_clk_init();
	mach_sched_init = hw_timer_init;
	m5441x_uarts_init();
	m5441x_fec_init();
}
