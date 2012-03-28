/*
 * Copyright 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/common.h>
#include <mach/mx53.h>

/*
 * Lookup table for attaching a specific name and platform_data pointer to
 * devices as they get created by of_platform_populate().  Ideally this table
 * would not exist, but the current clock implementation depends on some devices
 * having a specific name.
 */
static const struct of_dev_auxdata imx53_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("fsl,imx53-uart", MX53_UART1_BASE_ADDR, "imx21-uart.0", NULL),
	OF_DEV_AUXDATA("fsl,imx53-uart", MX53_UART2_BASE_ADDR, "imx21-uart.1", NULL),
	OF_DEV_AUXDATA("fsl,imx53-uart", MX53_UART3_BASE_ADDR, "imx21-uart.2", NULL),
	OF_DEV_AUXDATA("fsl,imx53-uart", MX53_UART4_BASE_ADDR, "imx21-uart.3", NULL),
	OF_DEV_AUXDATA("fsl,imx53-uart", MX53_UART5_BASE_ADDR, "imx21-uart.4", NULL),
	OF_DEV_AUXDATA("fsl,imx53-fec", MX53_FEC_BASE_ADDR, "imx25-fec.0", NULL),
	OF_DEV_AUXDATA("fsl,imx53-esdhc", MX53_ESDHC1_BASE_ADDR, "sdhci-esdhc-imx53.0", NULL),
	OF_DEV_AUXDATA("fsl,imx53-esdhc", MX53_ESDHC2_BASE_ADDR, "sdhci-esdhc-imx53.1", NULL),
	OF_DEV_AUXDATA("fsl,imx53-esdhc", MX53_ESDHC3_BASE_ADDR, "sdhci-esdhc-imx53.2", NULL),
	OF_DEV_AUXDATA("fsl,imx53-esdhc", MX53_ESDHC4_BASE_ADDR, "sdhci-esdhc-imx53.3", NULL),
	OF_DEV_AUXDATA("fsl,imx53-ecspi", MX53_ECSPI1_BASE_ADDR, "imx51-ecspi.0", NULL),
	OF_DEV_AUXDATA("fsl,imx53-ecspi", MX53_ECSPI2_BASE_ADDR, "imx51-ecspi.1", NULL),
	OF_DEV_AUXDATA("fsl,imx53-cspi", MX53_CSPI_BASE_ADDR, "imx35-cspi.0", NULL),
	OF_DEV_AUXDATA("fsl,imx53-i2c", MX53_I2C1_BASE_ADDR, "imx-i2c.0", NULL),
	OF_DEV_AUXDATA("fsl,imx53-i2c", MX53_I2C2_BASE_ADDR, "imx-i2c.1", NULL),
	OF_DEV_AUXDATA("fsl,imx53-i2c", MX53_I2C3_BASE_ADDR, "imx-i2c.2", NULL),
	OF_DEV_AUXDATA("fsl,imx53-sdma", MX53_SDMA_BASE_ADDR, "imx35-sdma", NULL),
	OF_DEV_AUXDATA("fsl,imx53-wdt", MX53_WDOG1_BASE_ADDR, "imx2-wdt.0", NULL),
	{ /* sentinel */ }
};

static int __init imx53_tzic_add_irq_domain(struct device_node *np,
				struct device_node *interrupt_parent)
{
	irq_domain_add_legacy(np, 128, 0, 0, &irq_domain_simple_ops, NULL);
	return 0;
}

static int __init imx53_gpio_add_irq_domain(struct device_node *np,
				struct device_node *interrupt_parent)
{
	static int gpio_irq_base = MXC_GPIO_IRQ_START + ARCH_NR_GPIOS;

	gpio_irq_base -= 32;
	irq_domain_add_legacy(np, 32, gpio_irq_base, 0, &irq_domain_simple_ops, NULL);

	return 0;
}

static const struct of_device_id imx53_irq_match[] __initconst = {
	{ .compatible = "fsl,imx53-tzic", .data = imx53_tzic_add_irq_domain, },
	{ .compatible = "fsl,imx53-gpio", .data = imx53_gpio_add_irq_domain, },
	{ /* sentinel */ }
};

static const struct of_device_id imx53_iomuxc_of_match[] __initconst = {
	{ .compatible = "fsl,imx53-iomuxc-ard", .data = imx53_ard_common_init, },
	{ .compatible = "fsl,imx53-iomuxc-evk", .data = imx53_evk_common_init, },
	{ .compatible = "fsl,imx53-iomuxc-qsb", .data = imx53_qsb_common_init, },
	{ .compatible = "fsl,imx53-iomuxc-smd", .data = imx53_smd_common_init, },
	{ /* sentinel */ }
};

static void __init imx53_dt_init(void)
{
	struct device_node *node;
	const struct of_device_id *of_id;
	void (*func)(void);

	of_irq_init(imx53_irq_match);

	node = of_find_matching_node(NULL, imx53_iomuxc_of_match);
	if (node) {
		of_id = of_match_node(imx53_iomuxc_of_match, node);
		func = of_id->data;
		func();
		of_node_put(node);
	}

	of_platform_populate(NULL, of_default_bus_match_table,
			     imx53_auxdata_lookup, NULL);
}

static void __init imx53_timer_init(void)
{
	mx53_clocks_init_dt();
}

static struct sys_timer imx53_timer = {
	.init = imx53_timer_init,
};

static const char *imx53_dt_board_compat[] __initdata = {
	"fsl,imx53-ard",
	"fsl,imx53-evk",
	"fsl,imx53-qsb",
	"fsl,imx53-smd",
	"fsl,imx53",
	NULL
};

DT_MACHINE_START(IMX53_DT, "Freescale i.MX53 (Device Tree Support)")
	.map_io		= mx53_map_io,
	.init_early	= imx53_init_early,
	.init_irq	= mx53_init_irq,
	.handle_irq	= imx53_handle_irq,
	.timer		= &imx53_timer,
	.init_machine	= imx53_dt_init,
	.dt_compat	= imx53_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END
