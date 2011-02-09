/*
 * linux/arch/arm/mach-omap1/board-nokia770.c
 *
 * Modified from board-generic.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/clk.h>
#include <linux/omapfb.h>

#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/gpio.h>
#include <plat/mux.h>
#include <plat/usb.h>
#include <plat/board.h>
#include <plat/keypad.h>
#include <plat/common.h>
#include <plat/hwa742.h>
#include <plat/lcd_mipid.h>
#include <plat/mmc.h>
#include <plat/clock.h>

#define ADS7846_PENDOWN_GPIO	15

static void __init omap_nokia770_init_irq(void)
{
	/* On Nokia 770, the SleepX signal is masked with an
	 * MPUIO line by default.  It has to be unmasked for it
	 * to become functional */

	/* SleepX mask direction */
	omap_writew((omap_readw(0xfffb5008) & ~2), 0xfffb5008);
	/* Unmask SleepX signal */
	omap_writew((omap_readw(0xfffb5004) & ~2), 0xfffb5004);

	omap1_init_common_hw();
	omap_init_irq();
}

static const unsigned int nokia770_keymap[] = {
	KEY(1, 0, GROUP_0 | KEY_UP),
	KEY(2, 0, GROUP_1 | KEY_F5),
	KEY(0, 1, GROUP_0 | KEY_LEFT),
	KEY(1, 1, GROUP_0 | KEY_ENTER),
	KEY(2, 1, GROUP_0 | KEY_RIGHT),
	KEY(0, 2, GROUP_1 | KEY_ESC),
	KEY(1, 2, GROUP_0 | KEY_DOWN),
	KEY(2, 2, GROUP_1 | KEY_F4),
	KEY(0, 3, GROUP_2 | KEY_F7),
	KEY(1, 3, GROUP_2 | KEY_F8),
	KEY(2, 3, GROUP_2 | KEY_F6),
};

static struct resource nokia770_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static const struct matrix_keymap_data nokia770_keymap_data = {
	.keymap		= nokia770_keymap,
	.keymap_size	= ARRAY_SIZE(nokia770_keymap),
};

static struct omap_kp_platform_data nokia770_kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap_data	= &nokia770_keymap_data,
	.delay		= 4,
};

static struct platform_device nokia770_kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &nokia770_kp_data,
	},
	.num_resources	= ARRAY_SIZE(nokia770_kp_resources),
	.resource	= nokia770_kp_resources,
};

static struct platform_device *nokia770_devices[] __initdata = {
	&nokia770_kp_device,
};

static void mipid_shutdown(struct mipid_platform_data *pdata)
{
	if (pdata->nreset_gpio != -1) {
		printk(KERN_INFO "shutdown LCD\n");
		gpio_set_value(pdata->nreset_gpio, 0);
		msleep(120);
	}
}

static struct mipid_platform_data nokia770_mipid_platform_data = {
	.shutdown = mipid_shutdown,
};

static void __init mipid_dev_init(void)
{
	const struct omap_lcd_config *conf;

	conf = omap_get_config(OMAP_TAG_LCD, struct omap_lcd_config);
	if (conf != NULL) {
		nokia770_mipid_platform_data.nreset_gpio = conf->nreset_gpio;
		nokia770_mipid_platform_data.data_lines = conf->data_lines;
	}
}

static void __init ads7846_dev_init(void)
{
	if (gpio_request(ADS7846_PENDOWN_GPIO, "ADS7846 pendown") < 0)
		printk(KERN_ERR "can't get ads7846 pen down GPIO\n");
}

static int ads7846_get_pendown_state(void)
{
	return !gpio_get_value(ADS7846_PENDOWN_GPIO);
}

static struct ads7846_platform_data nokia770_ads7846_platform_data __initdata = {
	.x_max		= 0x0fff,
	.y_max		= 0x0fff,
	.x_plate_ohms	= 180,
	.pressure_max	= 255,
	.debounce_max	= 10,
	.debounce_tol	= 3,
	.debounce_rep	= 1,
	.get_pendown_state	= ads7846_get_pendown_state,
};

static struct spi_board_info nokia770_spi_board_info[] __initdata = {
	[0] = {
		.modalias       = "lcd_mipid",
		.bus_num        = 2,
		.chip_select    = 3,
		.max_speed_hz   = 12000000,
		.platform_data	= &nokia770_mipid_platform_data,
	},
	[1] = {
		.modalias       = "ads7846",
		.bus_num        = 2,
		.chip_select    = 0,
		.max_speed_hz   = 2500000,
		.irq		= OMAP_GPIO_IRQ(15),
		.platform_data	= &nokia770_ads7846_platform_data,
	},
};

static struct hwa742_platform_data nokia770_hwa742_platform_data = {
	.te_connected		= 1,
};

static void __init hwa742_dev_init(void)
{
	clk_add_alias("hwa_sys_ck", NULL, "bclk", NULL);
	omapfb_set_ctrl_platform_data(&nokia770_hwa742_platform_data);
}

/* assume no Mini-AB port */

static struct omap_usb_config nokia770_usb_config __initdata = {
	.otg		= 1,
	.register_host	= 1,
	.register_dev	= 1,
	.hmc_mode	= 16,
	.pins[0]	= 6,
};

#if defined(CONFIG_MMC_OMAP) || defined(CONFIG_MMC_OMAP_MODULE)

#define NOKIA770_GPIO_MMC_POWER		41
#define NOKIA770_GPIO_MMC_SWITCH	23

static int nokia770_mmc_set_power(struct device *dev, int slot, int power_on,
				int vdd)
{
	gpio_set_value(NOKIA770_GPIO_MMC_POWER, power_on);
	return 0;
}

static int nokia770_mmc_get_cover_state(struct device *dev, int slot)
{
	return gpio_get_value(NOKIA770_GPIO_MMC_SWITCH);
}

static struct omap_mmc_platform_data nokia770_mmc2_data = {
	.nr_slots                       = 1,
	.dma_mask			= 0xffffffff,
	.max_freq                       = 12000000,
	.slots[0]       = {
		.set_power		= nokia770_mmc_set_power,
		.get_cover_state	= nokia770_mmc_get_cover_state,
		.ocr_mask               = MMC_VDD_32_33|MMC_VDD_33_34,
		.name                   = "mmcblk",
	},
};

static struct omap_mmc_platform_data *nokia770_mmc_data[OMAP16XX_NR_MMC];

static void __init nokia770_mmc_init(void)
{
	int ret;

	ret = gpio_request(NOKIA770_GPIO_MMC_POWER, "MMC power");
	if (ret < 0)
		return;
	gpio_direction_output(NOKIA770_GPIO_MMC_POWER, 0);

	ret = gpio_request(NOKIA770_GPIO_MMC_SWITCH, "MMC cover");
	if (ret < 0) {
		gpio_free(NOKIA770_GPIO_MMC_POWER);
		return;
	}
	gpio_direction_input(NOKIA770_GPIO_MMC_SWITCH);

	/* Only the second MMC controller is used */
	nokia770_mmc_data[1] = &nokia770_mmc2_data;
	omap1_init_mmc(nokia770_mmc_data, OMAP16XX_NR_MMC);
}

#else
static inline void nokia770_mmc_init(void)
{
}
#endif

static void __init omap_nokia770_init(void)
{
	platform_add_devices(nokia770_devices, ARRAY_SIZE(nokia770_devices));
	spi_register_board_info(nokia770_spi_board_info,
				ARRAY_SIZE(nokia770_spi_board_info));
	omap_serial_init();
	omap_register_i2c_bus(1, 100, NULL, 0);
	hwa742_dev_init();
	ads7846_dev_init();
	mipid_dev_init();
	omap1_usb_init(&nokia770_usb_config);
	nokia770_mmc_init();
}

static void __init omap_nokia770_map_io(void)
{
	omap1_map_common_io();
}

MACHINE_START(NOKIA770, "Nokia 770")
	.boot_params	= 0x10000100,
	.map_io		= omap_nokia770_map_io,
	.reserve	= omap_reserve,
	.init_irq	= omap_nokia770_init_irq,
	.init_machine	= omap_nokia770_init,
	.timer		= &omap_timer,
MACHINE_END
