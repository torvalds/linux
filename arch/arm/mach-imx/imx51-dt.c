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

#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/common.h>
#include <mach/mx51.h>

/*
 * Lookup table for attaching a specific name and platform_data pointer to
 * devices as they get created by of_platform_populate().  Ideally this table
 * would not exist, but the current clock implementation depends on some devices
 * having a specific name.
 */
static const struct of_dev_auxdata imx51_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("fsl,imx51-uart", MX51_UART1_BASE_ADDR, "imx21-uart.0", NULL),
	OF_DEV_AUXDATA("fsl,imx51-uart", MX51_UART2_BASE_ADDR, "imx21-uart.1", NULL),
	OF_DEV_AUXDATA("fsl,imx51-uart", MX51_UART3_BASE_ADDR, "imx21-uart.2", NULL),
	OF_DEV_AUXDATA("fsl,imx51-fec", MX51_FEC_BASE_ADDR, "imx27-fec.0", NULL),
	OF_DEV_AUXDATA("fsl,imx51-esdhc", MX51_ESDHC1_BASE_ADDR, "sdhci-esdhc-imx51.0", NULL),
	OF_DEV_AUXDATA("fsl,imx51-esdhc", MX51_ESDHC2_BASE_ADDR, "sdhci-esdhc-imx51.1", NULL),
	OF_DEV_AUXDATA("fsl,imx51-esdhc", MX51_ESDHC3_BASE_ADDR, "sdhci-esdhc-imx51.2", NULL),
	OF_DEV_AUXDATA("fsl,imx51-esdhc", MX51_ESDHC4_BASE_ADDR, "sdhci-esdhc-imx51.3", NULL),
	OF_DEV_AUXDATA("fsl,imx51-ecspi", MX51_ECSPI1_BASE_ADDR, "imx51-ecspi.0", NULL),
	OF_DEV_AUXDATA("fsl,imx51-ecspi", MX51_ECSPI2_BASE_ADDR, "imx51-ecspi.1", NULL),
	OF_DEV_AUXDATA("fsl,imx51-cspi", MX51_CSPI_BASE_ADDR, "imx35-cspi.0", NULL),
	OF_DEV_AUXDATA("fsl,imx51-i2c", MX51_I2C1_BASE_ADDR, "imx-i2c.0", NULL),
	OF_DEV_AUXDATA("fsl,imx51-i2c", MX51_I2C2_BASE_ADDR, "imx-i2c.1", NULL),
	OF_DEV_AUXDATA("fsl,imx51-sdma", MX51_SDMA_BASE_ADDR, "imx35-sdma", NULL),
	OF_DEV_AUXDATA("fsl,imx51-wdt", MX51_WDOG1_BASE_ADDR, "imx2-wdt.0", NULL),
	{ /* sentinel */ }
};

static const struct of_device_id imx51_iomuxc_of_match[] __initconst = {
	{ .compatible = "fsl,imx51-iomuxc-babbage", .data = imx51_babbage_common_init, },
	{ /* sentinel */ }
};

static void __init imx51_dt_init(void)
{
	struct device_node *node;
	const struct of_device_id *of_id;
	void (*func)(void);

	node = of_find_matching_node(NULL, imx51_iomuxc_of_match);
	if (node) {
		of_id = of_match_node(imx51_iomuxc_of_match, node);
		func = of_id->data;
		func();
		of_node_put(node);
	}

	of_platform_populate(NULL, of_default_bus_match_table,
			     imx51_auxdata_lookup, NULL);
}

static void __init imx51_timer_init(void)
{
	mx51_clocks_init_dt();
}

static struct sys_timer imx51_timer = {
	.init = imx51_timer_init,
};

static const char *imx51_dt_board_compat[] __initdata = {
	"fsl,imx51",
	NULL
};

DT_MACHINE_START(IMX51_DT, "Freescale i.MX51 (Device Tree Support)")
	.map_io		= mx51_map_io,
	.init_early	= imx51_init_early,
	.init_irq	= mx51_init_irq,
	.handle_irq	= imx51_handle_irq,
	.timer		= &imx51_timer,
	.init_machine	= imx51_dt_init,
	.init_late	= imx51_init_late,
	.dt_compat	= imx51_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END
