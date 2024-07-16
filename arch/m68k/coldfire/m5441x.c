// SPDX-License-Identifier: GPL-2.0
/*
 *	m5441x.c -- support for Coldfire m5441x processors
 *
 *	(C) Copyright Steven King <sfking@fdwdc.com>
 */

#include <linux/clkdev.h>
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
DEFINE_CLK(0, "flexcan.0", 8, MCF_CLK);
DEFINE_CLK(0, "flexcan.1", 9, MCF_CLK);
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
DEFINE_CLK(0, "sdhci-esdhc-mcf.0", 51, MCF_CLK);
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

DEFINE_CLK(2, "ipg.0", 0, MCF_CLK);
DEFINE_CLK(2, "ahb.0", 1, MCF_CLK);
DEFINE_CLK(2, "per.0", 2, MCF_CLK);

static struct clk_lookup m5411x_clk_lookup[] = {
	CLKDEV_INIT("flexbus", NULL, &__clk_0_2),
	CLKDEV_INIT("mcfcan.0", NULL, &__clk_0_8),
	CLKDEV_INIT("mcfcan.1", NULL, &__clk_0_9),
	CLKDEV_INIT("imx1-i2c.1", NULL, &__clk_0_14),
	CLKDEV_INIT("mcfdspi.1", NULL, &__clk_0_15),
	CLKDEV_INIT("edma", NULL, &__clk_0_17),
	CLKDEV_INIT("intc.0", NULL, &__clk_0_18),
	CLKDEV_INIT("intc.1", NULL, &__clk_0_19),
	CLKDEV_INIT("intc.2", NULL, &__clk_0_20),
	CLKDEV_INIT("imx1-i2c.0", NULL, &__clk_0_22),
	CLKDEV_INIT("fsl-dspi.0", NULL, &__clk_0_23),
	CLKDEV_INIT("mcfuart.0", NULL, &__clk_0_24),
	CLKDEV_INIT("mcfuart.1", NULL, &__clk_0_25),
	CLKDEV_INIT("mcfuart.2", NULL, &__clk_0_26),
	CLKDEV_INIT("mcfuart.3", NULL, &__clk_0_27),
	CLKDEV_INIT("mcftmr.0", NULL, &__clk_0_28),
	CLKDEV_INIT("mcftmr.1", NULL, &__clk_0_29),
	CLKDEV_INIT("mcftmr.2", NULL, &__clk_0_30),
	CLKDEV_INIT("mcftmr.3", NULL, &__clk_0_31),
	CLKDEV_INIT("mcfpit.0", NULL, &__clk_0_32),
	CLKDEV_INIT("mcfpit.1", NULL, &__clk_0_33),
	CLKDEV_INIT("mcfpit.2", NULL, &__clk_0_34),
	CLKDEV_INIT("mcfpit.3", NULL, &__clk_0_35),
	CLKDEV_INIT("mcfeport.0", NULL, &__clk_0_37),
	CLKDEV_INIT("mcfadc.0", NULL, &__clk_0_38),
	CLKDEV_INIT("mcfdac.0", NULL, &__clk_0_39),
	CLKDEV_INIT("mcfrtc.0", NULL, &__clk_0_42),
	CLKDEV_INIT("mcfsim.0", NULL, &__clk_0_43),
	CLKDEV_INIT("mcfusb-otg.0", NULL, &__clk_0_44),
	CLKDEV_INIT("mcfusb-host.0", NULL, &__clk_0_45),
	CLKDEV_INIT("mcfddr-sram.0", NULL, &__clk_0_46),
	CLKDEV_INIT("mcfssi.0", NULL, &__clk_0_47),
	CLKDEV_INIT(NULL, "pll.0", &__clk_0_48),
	CLKDEV_INIT("mcfrng.0", NULL, &__clk_0_49),
	CLKDEV_INIT("mcfssi.1", NULL, &__clk_0_50),
	CLKDEV_INIT("sdhci-esdhc-mcf.0", NULL, &__clk_0_51),
	CLKDEV_INIT("enet-fec.0", NULL, &__clk_0_53),
	CLKDEV_INIT("enet-fec.1", NULL, &__clk_0_54),
	CLKDEV_INIT("switch.0", NULL, &__clk_0_55),
	CLKDEV_INIT("switch.1", NULL, &__clk_0_56),
	CLKDEV_INIT("nand.0", NULL, &__clk_0_63),
	CLKDEV_INIT("mcfow.0", NULL, &__clk_1_2),
	CLKDEV_INIT("imx1-i2c.2", NULL, &__clk_1_4),
	CLKDEV_INIT("imx1-i2c.3", NULL, &__clk_1_5),
	CLKDEV_INIT("imx1-i2c.4", NULL, &__clk_1_6),
	CLKDEV_INIT("imx1-i2c.5", NULL, &__clk_1_7),
	CLKDEV_INIT("mcfuart.4", NULL, &__clk_1_24),
	CLKDEV_INIT("mcfuart.5", NULL, &__clk_1_25),
	CLKDEV_INIT("mcfuart.6", NULL, &__clk_1_26),
	CLKDEV_INIT("mcfuart.7", NULL, &__clk_1_27),
	CLKDEV_INIT("mcfuart.8", NULL, &__clk_1_28),
	CLKDEV_INIT("mcfuart.9", NULL, &__clk_1_29),
	CLKDEV_INIT("mcfpwm.0", NULL, &__clk_1_34),
	CLKDEV_INIT(NULL, "sys.0", &__clk_1_36),
	CLKDEV_INIT("gpio.0", NULL, &__clk_1_37),
	CLKDEV_INIT("ipg.0", NULL, &__clk_2_0),
	CLKDEV_INIT("ahb.0", NULL, &__clk_2_1),
	CLKDEV_INIT("per.0", NULL, &__clk_2_2),
};

static struct clk * const enable_clks[] __initconst = {
	/* make sure these clocks are enabled */
	&__clk_0_8, /* flexcan.0 */
	&__clk_0_9, /* flexcan.1 */
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
	&__clk_0_51, /* esdhc */

	&__clk_1_36, /* CCM/reset module/Power management */
	&__clk_1_37, /* gpio */
};
static struct clk * const disable_clks[] __initconst = {
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

static void __clk_enable2(struct clk *clk)
{
	__raw_writel(__raw_readl(MCFSDHC_CLK) | (1 << clk->slot), MCFSDHC_CLK);
}

static void __clk_disable2(struct clk *clk)
{
	__raw_writel(__raw_readl(MCFSDHC_CLK) & ~(1 << clk->slot), MCFSDHC_CLK);
}

struct clk_ops clk_ops2 = {
	.enable		= __clk_enable2,
	.disable	= __clk_disable2,
};

static void __init m5441x_clk_init(void)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(enable_clks); ++i)
		__clk_init_enabled(enable_clks[i]);
	/* make sure these clocks are disabled */
	for (i = 0; i < ARRAY_SIZE(disable_clks); ++i)
		__clk_init_disabled(disable_clks[i]);

	clkdev_add_table(m5411x_clk_lookup, ARRAY_SIZE(m5411x_clk_lookup));
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
