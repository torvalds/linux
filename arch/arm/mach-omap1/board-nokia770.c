// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-omap1/board-nokia770.c
 *
 * Modified from board-generic.c
 */
#include <linux/clkdev.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/gpio/machine.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/omapfb.h>

#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#include <linux/platform_data/keypad-omap.h>
#include <linux/platform_data/lcd-mipid.h>
#include <linux/platform_data/gpio-omap.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "mux.h"
#include "hardware.h"
#include "usb.h"
#include "common.h"
#include "clock.h"
#include "mmc.h"

#define ADS7846_PENDOWN_GPIO	15

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

static const struct omap_lcd_config nokia770_lcd_config __initconst = {
	.ctrl_name	= "hwa742",
};

static void __init mipid_dev_init(void)
{
	nokia770_mipid_platform_data.nreset_gpio = 13;
	nokia770_mipid_platform_data.data_lines = 16;

	omapfb_set_lcd_config(&nokia770_lcd_config);
}

static struct ads7846_platform_data nokia770_ads7846_platform_data __initdata = {
	.x_max		= 0x0fff,
	.y_max		= 0x0fff,
	.x_plate_ohms	= 180,
	.pressure_max	= 255,
	.debounce_max	= 10,
	.debounce_tol	= 3,
	.debounce_rep	= 1,
	.gpio_pendown	= ADS7846_PENDOWN_GPIO,
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
		.platform_data	= &nokia770_ads7846_platform_data,
	},
};

static void __init hwa742_dev_init(void)
{
	clk_add_alias("hwa_sys_ck", NULL, "bclk", NULL);
}

/* assume no Mini-AB port */

static struct omap_usb_config nokia770_usb_config __initdata = {
	.otg		= 1,
	.register_host	= 1,
	.register_dev	= 1,
	.hmc_mode	= 16,
	.pins[0]	= 6,
	.extcon		= "tahvo-usb",
};

#if IS_ENABLED(CONFIG_MMC_OMAP)

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

#if IS_ENABLED(CONFIG_I2C_CBUS_GPIO)
static struct gpiod_lookup_table nokia770_cbus_gpio_table = {
	.dev_id = "i2c-cbus-gpio.2",
	.table = {
		GPIO_LOOKUP_IDX("mpuio", 9, NULL, 0, 0), /* clk */
		GPIO_LOOKUP_IDX("mpuio", 10, NULL, 1, 0), /* dat */
		GPIO_LOOKUP_IDX("mpuio", 11, NULL, 2, 0), /* sel */
		{ },
	},
};

static struct platform_device nokia770_cbus_device = {
	.name   = "i2c-cbus-gpio",
	.id     = 2,
};

static struct i2c_board_info nokia770_i2c_board_info_2[] __initdata = {
	{
		I2C_BOARD_INFO("retu", 0x01),
	},
	{
		I2C_BOARD_INFO("tahvo", 0x02),
	},
};

static void __init nokia770_cbus_init(void)
{
	const int retu_irq_gpio = 62;
	const int tahvo_irq_gpio = 40;

	if (gpio_request_one(retu_irq_gpio, GPIOF_IN, "Retu IRQ"))
		return;
	if (gpio_request_one(tahvo_irq_gpio, GPIOF_IN, "Tahvo IRQ")) {
		gpio_free(retu_irq_gpio);
		return;
	}
	irq_set_irq_type(gpio_to_irq(retu_irq_gpio), IRQ_TYPE_EDGE_RISING);
	irq_set_irq_type(gpio_to_irq(tahvo_irq_gpio), IRQ_TYPE_EDGE_RISING);
	nokia770_i2c_board_info_2[0].irq = gpio_to_irq(retu_irq_gpio);
	nokia770_i2c_board_info_2[1].irq = gpio_to_irq(tahvo_irq_gpio);
	i2c_register_board_info(2, nokia770_i2c_board_info_2,
				ARRAY_SIZE(nokia770_i2c_board_info_2));
	gpiod_add_lookup_table(&nokia770_cbus_gpio_table);
	platform_device_register(&nokia770_cbus_device);
}
#else /* CONFIG_I2C_CBUS_GPIO */
static void __init nokia770_cbus_init(void)
{
}
#endif /* CONFIG_I2C_CBUS_GPIO */

static void __init omap_nokia770_init(void)
{
	/* On Nokia 770, the SleepX signal is masked with an
	 * MPUIO line by default.  It has to be unmasked for it
	 * to become functional */

	/* SleepX mask direction */
	omap_writew((omap_readw(0xfffb5008) & ~2), 0xfffb5008);
	/* Unmask SleepX signal */
	omap_writew((omap_readw(0xfffb5004) & ~2), 0xfffb5004);

	platform_add_devices(nokia770_devices, ARRAY_SIZE(nokia770_devices));
	nokia770_spi_board_info[1].irq = gpio_to_irq(15);
	spi_register_board_info(nokia770_spi_board_info,
				ARRAY_SIZE(nokia770_spi_board_info));
	omap_serial_init();
	omap_register_i2c_bus(1, 100, NULL, 0);
	hwa742_dev_init();
	mipid_dev_init();
	omap1_usb_init(&nokia770_usb_config);
	nokia770_mmc_init();
	nokia770_cbus_init();
}

MACHINE_START(NOKIA770, "Nokia 770")
	.atag_offset	= 0x100,
	.map_io		= omap16xx_map_io,
	.init_early     = omap1_init_early,
	.init_irq	= omap1_init_irq,
	.handle_irq	= omap1_handle_irq,
	.init_machine	= omap_nokia770_init,
	.init_late	= omap1_init_late,
	.init_time	= omap1_timer_init,
	.restart	= omap1_restart,
MACHINE_END
