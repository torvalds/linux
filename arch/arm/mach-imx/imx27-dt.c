/*
 * Copyright 2012 Sascha Hauer, Pengutronix
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/common.h>
#include <mach/mx27.h>

static const struct of_dev_auxdata imx27_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("fsl,imx27-uart", MX27_UART1_BASE_ADDR, "imx21-uart.0", NULL),
	OF_DEV_AUXDATA("fsl,imx27-uart", MX27_UART2_BASE_ADDR, "imx21-uart.1", NULL),
	OF_DEV_AUXDATA("fsl,imx27-uart", MX27_UART3_BASE_ADDR, "imx21-uart.2", NULL),
	OF_DEV_AUXDATA("fsl,imx27-fec", MX27_FEC_BASE_ADDR, "imx27-fec.0", NULL),
	OF_DEV_AUXDATA("fsl,imx27-i2c", MX27_I2C1_BASE_ADDR, "imx-i2c.0", NULL),
	OF_DEV_AUXDATA("fsl,imx27-i2c", MX27_I2C2_BASE_ADDR, "imx-i2c.1", NULL),
	OF_DEV_AUXDATA("fsl,imx27-cspi", MX27_CSPI1_BASE_ADDR, "imx27-cspi.0", NULL),
	OF_DEV_AUXDATA("fsl,imx27-cspi", MX27_CSPI2_BASE_ADDR, "imx27-cspi.1", NULL),
	OF_DEV_AUXDATA("fsl,imx27-cspi", MX27_CSPI3_BASE_ADDR, "imx27-cspi.2", NULL),
	OF_DEV_AUXDATA("fsl,imx27-wdt", MX27_WDOG_BASE_ADDR, "imx2-wdt.0", NULL),
	OF_DEV_AUXDATA("fsl,imx27-nand", MX27_NFC_BASE_ADDR, "mxc_nand.0", NULL),
	{ /* sentinel */ }
};

static int __init imx27_avic_add_irq_domain(struct device_node *np,
				struct device_node *interrupt_parent)
{
	irq_domain_add_legacy(np, 64, 0, 0, &irq_domain_simple_ops, NULL);
	return 0;
}

static int __init imx27_gpio_add_irq_domain(struct device_node *np,
				struct device_node *interrupt_parent)
{
	static int gpio_irq_base = MXC_GPIO_IRQ_START + ARCH_NR_GPIOS;

	gpio_irq_base -= 32;
	irq_domain_add_legacy(np, 32, gpio_irq_base, 0, &irq_domain_simple_ops,
				NULL);

	return 0;
}

static const struct of_device_id imx27_irq_match[] __initconst = {
	{ .compatible = "fsl,imx27-avic", .data = imx27_avic_add_irq_domain, },
	{ .compatible = "fsl,imx27-gpio", .data = imx27_gpio_add_irq_domain, },
	{ /* sentinel */ }
};

static void __init imx27_dt_init(void)
{
	of_irq_init(imx27_irq_match);

	of_platform_populate(NULL, of_default_bus_match_table,
			     imx27_auxdata_lookup, NULL);
}

static void __init imx27_timer_init(void)
{
	mx27_clocks_init_dt();
}

static struct sys_timer imx27_timer = {
	.init = imx27_timer_init,
};

static const char * const imx27_dt_board_compat[] __initconst = {
	"fsl,imx27",
	NULL
};

DT_MACHINE_START(IMX27_DT, "Freescale i.MX27 (Device Tree Support)")
	.map_io		= mx27_map_io,
	.init_early	= imx27_init_early,
	.init_irq	= mx27_init_irq,
	.handle_irq	= imx27_handle_irq,
	.timer		= &imx27_timer,
	.init_machine	= imx27_dt_init,
	.dt_compat	= imx27_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END
