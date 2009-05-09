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

#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/gpio.h>
#include <mach/mux.h>
#include <mach/usb.h>
#include <mach/board.h>
#include <mach/keypad.h>
#include <mach/common.h>
#include <mach/dsp_common.h>
#include <mach/omapfb.h>
#include <mach/lcd_mipid.h>
#include <mach/mmc.h>

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

static int nokia770_keymap[] = {
	KEY(0, 1, GROUP_0 | KEY_UP),
	KEY(0, 2, GROUP_1 | KEY_F5),
	KEY(1, 0, GROUP_0 | KEY_LEFT),
	KEY(1, 1, GROUP_0 | KEY_ENTER),
	KEY(1, 2, GROUP_0 | KEY_RIGHT),
	KEY(2, 0, GROUP_1 | KEY_ESC),
	KEY(2, 1, GROUP_0 | KEY_DOWN),
	KEY(2, 2, GROUP_1 | KEY_F4),
	KEY(3, 0, GROUP_2 | KEY_F7),
	KEY(3, 1, GROUP_2 | KEY_F8),
	KEY(3, 2, GROUP_2 | KEY_F6),
	0
};

static struct resource nokia770_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_kp_platform_data nokia770_kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap		= nokia770_keymap,
	.keymapsize	= ARRAY_SIZE(nokia770_keymap),
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

static void mipid_dev_init(void)
{
	const struct omap_lcd_config *conf;

	conf = omap_get_config(OMAP_TAG_LCD, struct omap_lcd_config);
	if (conf != NULL) {
		nokia770_mipid_platform_data.nreset_gpio = conf->nreset_gpio;
		nokia770_mipid_platform_data.data_lines = conf->data_lines;
	}
}

static void ads7846_dev_init(void)
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
	.slots[0]       = {
		.set_power		= nokia770_mmc_set_power,
		.get_cover_state	= nokia770_mmc_get_cover_state,
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

#if	defined(CONFIG_OMAP_DSP)
/*
 * audio power control
 */
#define	HEADPHONE_GPIO		14
#define	AMPLIFIER_CTRL_GPIO	58

static struct clk *dspxor_ck;
static DEFINE_MUTEX(audio_pwr_lock);
/*
 * audio_pwr_state
 * +--+-------------------------+---------------------------------------+
 * |-1|down			|power-up request -> 0			|
 * +--+-------------------------+---------------------------------------+
 * | 0|up			|power-down(1) request -> 1		|
 * |  |				|power-down(2) request -> (ignore)	|
 * +--+-------------------------+---------------------------------------+
 * | 1|up,			|power-up request -> 0			|
 * |  |received down(1) request	|power-down(2) request -> -1		|
 * +--+-------------------------+---------------------------------------+
 */
static int audio_pwr_state = -1;

static inline void aic23_power_up(void)
{
}
static inline void aic23_power_down(void)
{
}

/*
 * audio_pwr_up / down should be called under audio_pwr_lock
 */
static void nokia770_audio_pwr_up(void)
{
	clk_enable(dspxor_ck);

	/* Turn on codec */
	aic23_power_up();

	if (gpio_get_value(HEADPHONE_GPIO))
		/* HP not connected, turn on amplifier */
		gpio_set_value(AMPLIFIER_CTRL_GPIO, 1);
	else
		/* HP connected, do not turn on amplifier */
		printk("HP connected\n");
}

static void codec_delayed_power_down(struct work_struct *work)
{
	mutex_lock(&audio_pwr_lock);
	if (audio_pwr_state == -1)
		aic23_power_down();
	clk_disable(dspxor_ck);
	mutex_unlock(&audio_pwr_lock);
}

static DECLARE_DELAYED_WORK(codec_power_down_work, codec_delayed_power_down);

static void nokia770_audio_pwr_down(void)
{
	/* Turn off amplifier */
	gpio_set_value(AMPLIFIER_CTRL_GPIO, 0);

	/* Turn off codec: schedule delayed work */
	schedule_delayed_work(&codec_power_down_work, HZ / 20);	/* 50ms */
}

static int
nokia770_audio_pwr_up_request(struct dsp_kfunc_device *kdev, int stage)
{
	mutex_lock(&audio_pwr_lock);
	if (audio_pwr_state == -1)
		nokia770_audio_pwr_up();
	/* force audio_pwr_state = 0, even if it was 1. */
	audio_pwr_state = 0;
	mutex_unlock(&audio_pwr_lock);
	return 0;
}

static int
nokia770_audio_pwr_down_request(struct dsp_kfunc_device *kdev, int stage)
{
	mutex_lock(&audio_pwr_lock);
	switch (stage) {
	case 1:
		if (audio_pwr_state == 0)
			audio_pwr_state = 1;
		break;
	case 2:
		if (audio_pwr_state == 1) {
			nokia770_audio_pwr_down();
			audio_pwr_state = -1;
		}
		break;
	}
	mutex_unlock(&audio_pwr_lock);
	return 0;
}

static struct dsp_kfunc_device nokia770_audio_device = {
	.name	 = "audio",
	.type	 = DSP_KFUNC_DEV_TYPE_AUDIO,
	.enable  = nokia770_audio_pwr_up_request,
	.disable = nokia770_audio_pwr_down_request,
};

static __init int omap_dsp_init(void)
{
	int ret;

	dspxor_ck = clk_get(0, "dspxor_ck");
	if (IS_ERR(dspxor_ck)) {
		printk(KERN_ERR "couldn't acquire dspxor_ck\n");
		return PTR_ERR(dspxor_ck);
	}

	ret = dsp_kfunc_device_register(&nokia770_audio_device);
	if (ret) {
		printk(KERN_ERR
		       "KFUNC device registration faild: %s\n",
		       nokia770_audio_device.name);
		goto out;
	}
	return 0;
 out:
	return ret;
}
#else
#define omap_dsp_init()		do {} while (0)
#endif	/* CONFIG_OMAP_DSP */

static void __init omap_nokia770_init(void)
{
	platform_add_devices(nokia770_devices, ARRAY_SIZE(nokia770_devices));
	spi_register_board_info(nokia770_spi_board_info,
				ARRAY_SIZE(nokia770_spi_board_info));
	omap_gpio_init();
	omap_serial_init();
	omap_register_i2c_bus(1, 100, NULL, 0);
	omap_dsp_init();
	ads7846_dev_init();
	mipid_dev_init();
	omap_usb_init(&nokia770_usb_config);
	nokia770_mmc_init();
}

static void __init omap_nokia770_map_io(void)
{
	omap1_map_common_io();
}

MACHINE_START(NOKIA770, "Nokia 770")
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= omap_nokia770_map_io,
	.init_irq	= omap_nokia770_init_irq,
	.init_machine	= omap_nokia770_init,
	.timer		= &omap_timer,
MACHINE_END
