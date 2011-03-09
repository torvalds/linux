/*
 * Freescale STMP378X development board support
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/spi/spi.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/pins.h>
#include <mach/pinmux.h>
#include <mach/platform.h>
#include <mach/stmp3xxx.h>
#include <mach/mmc.h>
#include <mach/gpmi.h>

#include "stmp378x.h"

static struct platform_device *devices[] = {
	&stmp3xxx_dbguart,
	&stmp3xxx_appuart,
	&stmp3xxx_watchdog,
	&stmp3xxx_touchscreen,
	&stmp3xxx_rtc,
	&stmp3xxx_keyboard,
	&stmp3xxx_framebuffer,
	&stmp3xxx_backlight,
	&stmp3xxx_rotdec,
	&stmp3xxx_persistent,
	&stmp3xxx_dcp_bootstream,
	&stmp3xxx_dcp,
	&stmp3xxx_battery,
	&stmp378x_pxp,
	&stmp378x_i2c,
};

static struct pin_desc i2c_pins_desc[] = {
	{ PINID_I2C_SCL, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_I2C_SDA, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
};

static struct pin_group i2c_pins = {
	.pins		= i2c_pins_desc,
	.nr_pins	= ARRAY_SIZE(i2c_pins_desc),
};

static struct pin_desc dbguart_pins_0[] = {
	{ PINID_PWM0, PIN_FUN3, },
	{ PINID_PWM1, PIN_FUN3, },
};

static struct pin_group dbguart_pins[] = {
	[0] = {
		.pins		= dbguart_pins_0,
		.nr_pins	= ARRAY_SIZE(dbguart_pins_0),
	},
};

static int dbguart_pins_control(int id, int request)
{
	int r = 0;

	if (request)
		r = stmp3xxx_request_pin_group(&dbguart_pins[id], "debug uart");
	else
		stmp3xxx_release_pin_group(&dbguart_pins[id], "debug uart");
	return r;
}

static struct pin_desc appuart_pins_0[] = {
	{ PINID_AUART1_CTS, PIN_FUN1, PIN_4MA, PIN_1_8V, 0, },
	{ PINID_AUART1_RTS, PIN_FUN1, PIN_4MA, PIN_1_8V, 0, },
	{ PINID_AUART1_RX, PIN_FUN1, PIN_4MA, PIN_1_8V, 0, },
	{ PINID_AUART1_TX, PIN_FUN1, PIN_4MA, PIN_1_8V, 0, },
};

static struct pin_desc appuart_pins_1[] = {
#if 0 /* enable these when second appuart will be connected */
	{ PINID_AUART2_CTS, PIN_FUN1, PIN_4MA, PIN_1_8V, 0, },
	{ PINID_AUART2_RTS, PIN_FUN1, PIN_4MA, PIN_1_8V, 0, },
	{ PINID_AUART2_RX, PIN_FUN1, PIN_4MA, PIN_1_8V, 0, },
	{ PINID_AUART2_TX, PIN_FUN1, PIN_4MA, PIN_1_8V, 0, },
#endif
};

static struct pin_desc mmc_pins_desc[] = {
	{ PINID_SSP1_DATA0, PIN_FUN1, PIN_8MA, PIN_3_3V, 1 },
	{ PINID_SSP1_DATA1, PIN_FUN1, PIN_8MA, PIN_3_3V, 1 },
	{ PINID_SSP1_DATA2, PIN_FUN1, PIN_8MA, PIN_3_3V, 1 },
	{ PINID_SSP1_DATA3, PIN_FUN1, PIN_8MA, PIN_3_3V, 1 },
	{ PINID_SSP1_CMD, PIN_FUN1, PIN_8MA, PIN_3_3V, 1 },
	{ PINID_SSP1_SCK, PIN_FUN1, PIN_8MA, PIN_3_3V, 0 },
	{ PINID_SSP1_DETECT, PIN_FUN1, PIN_8MA, PIN_3_3V, 0 },
};

static struct pin_group mmc_pins = {
	.pins		= mmc_pins_desc,
	.nr_pins	= ARRAY_SIZE(mmc_pins_desc),
};

static int stmp3xxxmmc_get_wp(void)
{
	return gpio_get_value(PINID_PWM4);
}

static int stmp3xxxmmc_hw_init_ssp1(void)
{
	int ret;

	ret = stmp3xxx_request_pin_group(&mmc_pins, "mmc");
	if (ret)
		goto out;

	/* Configure write protect GPIO pin */
	ret = gpio_request(PINID_PWM4, "mmc wp");
	if (ret)
		goto out_wp;

	gpio_direction_input(PINID_PWM4);

	/* Configure POWER pin as gpio to drive power to MMC slot */
	ret = gpio_request(PINID_PWM3, "mmc power");
	if (ret)
		goto out_power;

	gpio_direction_output(PINID_PWM3, 0);
	mdelay(100);

	return 0;

out_power:
	gpio_free(PINID_PWM4);
out_wp:
	stmp3xxx_release_pin_group(&mmc_pins, "mmc");
out:
	return ret;
}

static void stmp3xxxmmc_hw_release_ssp1(void)
{
	gpio_free(PINID_PWM3);
	gpio_free(PINID_PWM4);
	stmp3xxx_release_pin_group(&mmc_pins, "mmc");
}

static void stmp3xxxmmc_cmd_pullup_ssp1(int enable)
{
	stmp3xxx_pin_pullup(PINID_SSP1_CMD, enable, "mmc");
}

static unsigned long
stmp3xxxmmc_setclock_ssp1(void __iomem *base, unsigned long hz)
{
	struct clk *ssp, *parent;
	char *p;
	long r;

	ssp = clk_get(NULL, "ssp");

	/* using SSP1, no timeout, clock rate 1 */
	writel(BF(2, SSP_TIMING_CLOCK_DIVIDE) |
	       BF(0xFFFF, SSP_TIMING_TIMEOUT),
	       base + HW_SSP_TIMING);

	p = (hz > 1000000) ? "io" : "osc_24M";
	parent = clk_get(NULL, p);
	clk_set_parent(ssp, parent);
	r = clk_set_rate(ssp, 2 * hz / 1000);
	clk_put(parent);
	clk_put(ssp);

	return hz;
}

static struct stmp3xxxmmc_platform_data mmc_data = {
	.hw_init	= stmp3xxxmmc_hw_init_ssp1,
	.hw_release	= stmp3xxxmmc_hw_release_ssp1,
	.get_wp		= stmp3xxxmmc_get_wp,
	.cmd_pullup	= stmp3xxxmmc_cmd_pullup_ssp1,
	.setclock	= stmp3xxxmmc_setclock_ssp1,
};


static struct pin_group appuart_pins[] = {
	[0] = {
		.pins		= appuart_pins_0,
		.nr_pins	= ARRAY_SIZE(appuart_pins_0),
	},
	[1] = {
		.pins		= appuart_pins_1,
		.nr_pins	= ARRAY_SIZE(appuart_pins_1),
	},
};

static struct pin_desc ssp1_pins_desc[] = {
	{ PINID_SSP1_SCK,	PIN_FUN1, PIN_8MA, PIN_3_3V, 0, },
	{ PINID_SSP1_CMD,	PIN_FUN1, PIN_4MA, PIN_3_3V, 0, },
	{ PINID_SSP1_DATA0,	PIN_FUN1, PIN_4MA, PIN_3_3V, 0, },
	{ PINID_SSP1_DATA3,	PIN_FUN1, PIN_4MA, PIN_3_3V, 0, },
};

static struct pin_desc ssp2_pins_desc[] = {
	{ PINID_GPMI_WRN,	PIN_FUN3, PIN_8MA, PIN_3_3V, 0, },
	{ PINID_GPMI_RDY1,	PIN_FUN3, PIN_4MA, PIN_3_3V, 0, },
	{ PINID_GPMI_D00,	PIN_FUN3, PIN_4MA, PIN_3_3V, 0, },
	{ PINID_GPMI_D03,	PIN_FUN3, PIN_4MA, PIN_3_3V, 0, },
};

static struct pin_group ssp1_pins = {
	.pins = ssp1_pins_desc,
	.nr_pins = ARRAY_SIZE(ssp1_pins_desc),
};

static struct pin_group ssp2_pins = {
	.pins = ssp1_pins_desc,
	.nr_pins = ARRAY_SIZE(ssp2_pins_desc),
};

static struct pin_desc gpmi_pins_desc[] = {
	{ PINID_GPMI_CE0N, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_CE1N, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GMPI_CE2N, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_CLE, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_ALE, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_WPN, PIN_FUN1, PIN_12MA, PIN_3_3V, 0 },
	{ PINID_GPMI_RDY1, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_D00, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_D01, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_D02, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_D03, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_D04, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_D05, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_D06, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_D07, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_RDY0, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_RDY2, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_RDY3, PIN_FUN1, PIN_4MA, PIN_3_3V, 0 },
	{ PINID_GPMI_WRN, PIN_FUN1, PIN_12MA, PIN_3_3V, 0 },
	{ PINID_GPMI_RDN, PIN_FUN1, PIN_12MA, PIN_3_3V, 0 },
};

static struct pin_group gpmi_pins = {
	.pins		= gpmi_pins_desc,
	.nr_pins	= ARRAY_SIZE(gpmi_pins_desc),
};

static struct mtd_partition gpmi_partitions[] = {
	[0] = {
		.name	= "boot",
		.size	= 10 * SZ_1M,
		.offset	= 0,
	},
	[1] = {
		.name	= "data",
		.size	= MTDPART_SIZ_FULL,
		.offset	= MTDPART_OFS_APPEND,
	},
};

static struct gpmi_platform_data gpmi_data = {
	.pins = &gpmi_pins,
	.nr_parts = ARRAY_SIZE(gpmi_partitions),
	.parts = gpmi_partitions,
	.part_types = { "cmdline", NULL },
};

static struct spi_board_info spi_board_info[] __initdata = {
#if defined(CONFIG_ENC28J60) || defined(CONFIG_ENC28J60_MODULE)
	{
		.modalias       = "enc28j60",
		.max_speed_hz   = 6 * 1000 * 1000,
		.bus_num	= 1,
		.chip_select    = 0,
		.platform_data  = NULL,
	},
#endif
};

static void __init stmp378x_devb_init(void)
{
	stmp3xxx_pinmux_init(NR_REAL_IRQS);

	/* init stmp3xxx platform */
	stmp3xxx_init();

	stmp3xxx_dbguart.dev.platform_data = dbguart_pins_control;
	stmp3xxx_appuart.dev.platform_data = appuart_pins;
	stmp3xxx_mmc.dev.platform_data = &mmc_data;
	stmp3xxx_gpmi.dev.platform_data = &gpmi_data;
	stmp3xxx_spi1.dev.platform_data = &ssp1_pins;
	stmp3xxx_spi2.dev.platform_data = &ssp2_pins;
	stmp378x_i2c.dev.platform_data = &i2c_pins;

	/* register spi devices */
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));

	/* add board's devices */
	platform_add_devices(devices, ARRAY_SIZE(devices));

	/* add devices selected by command line ssp1= and ssp2= options */
	stmp3xxx_ssp1_device_register();
	stmp3xxx_ssp2_device_register();
}

MACHINE_START(STMP378X, "STMP378X")
	.boot_params	= 0x40000100,
	.map_io		= stmp378x_map_io,
	.init_irq	= stmp378x_init_irq,
	.timer		= &stmp3xxx_timer,
	.init_machine	= stmp378x_devb_init,
MACHINE_END
