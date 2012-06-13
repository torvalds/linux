/*
 * Copyright (C) Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 * based on code from the following
 * Copyright 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2009-2010 Pegatron Corporation. All Rights Reserved.
 * Copyright 2009-2010 Genesi USA, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <linux/mfd/mc13892.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <mach/ulpi.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-mx51.h>

#include <asm/setup.h>
#include <asm/system_info.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "devices-imx51.h"
#include "efika.h"

#define EFIKASB_USBH2_STP	IMX_GPIO_NR(2, 20)
#define EFIKASB_GREEN_LED	IMX_GPIO_NR(1, 3)
#define EFIKASB_WHITE_LED	IMX_GPIO_NR(2, 25)
#define EFIKASB_PCBID0		IMX_GPIO_NR(2, 28)
#define EFIKASB_PCBID1		IMX_GPIO_NR(2, 29)
#define EFIKASB_PWRKEY		IMX_GPIO_NR(2, 31)
#define EFIKASB_LID		IMX_GPIO_NR(3, 14)
#define EFIKASB_POWEROFF	IMX_GPIO_NR(4, 13)
#define EFIKASB_RFKILL		IMX_GPIO_NR(3, 1)

#define MX51_PAD_PWRKEY IOMUX_PAD(0x48c, 0x0f8, 1, 0x0,   0, PAD_CTL_PUS_100K_UP | PAD_CTL_PKE)
#define MX51_PAD_SD1_CD	IOMUX_PAD(0x47c, 0x0e8, 1, __NA_, 0, MX51_ESDHC_PAD_CTRL)

static iomux_v3_cfg_t mx51efikasb_pads[] = {
	/* USB HOST2 */
	MX51_PAD_EIM_D16__USBH2_DATA0,
	MX51_PAD_EIM_D17__USBH2_DATA1,
	MX51_PAD_EIM_D18__USBH2_DATA2,
	MX51_PAD_EIM_D19__USBH2_DATA3,
	MX51_PAD_EIM_D20__USBH2_DATA4,
	MX51_PAD_EIM_D21__USBH2_DATA5,
	MX51_PAD_EIM_D22__USBH2_DATA6,
	MX51_PAD_EIM_D23__USBH2_DATA7,
	MX51_PAD_EIM_A24__USBH2_CLK,
	MX51_PAD_EIM_A25__USBH2_DIR,
	MX51_PAD_EIM_A26__USBH2_STP,
	MX51_PAD_EIM_A27__USBH2_NXT,

	/* leds */
	MX51_PAD_EIM_CS0__GPIO2_25,
	MX51_PAD_GPIO1_3__GPIO1_3,

	/* pcb id */
	MX51_PAD_EIM_CS3__GPIO2_28,
	MX51_PAD_EIM_CS4__GPIO2_29,

	/* lid */
	MX51_PAD_CSI1_VSYNC__GPIO3_14,

	/* power key*/
	MX51_PAD_PWRKEY,

	/* wifi/bt button */
	MX51_PAD_DI1_PIN12__GPIO3_1,

	/* power off */
	MX51_PAD_CSI2_VSYNC__GPIO4_13,

	/* wdog reset */
	MX51_PAD_GPIO1_4__WDOG1_WDOG_B,

	/* BT */
	MX51_PAD_EIM_A17__GPIO2_11,

	MX51_PAD_SD1_CD,
};

static int initialize_usbh2_port(struct platform_device *pdev)
{
	iomux_v3_cfg_t usbh2stp = MX51_PAD_EIM_A26__USBH2_STP;
	iomux_v3_cfg_t usbh2gpio = MX51_PAD_EIM_A26__GPIO2_20;

	mxc_iomux_v3_setup_pad(usbh2gpio);
	gpio_request(EFIKASB_USBH2_STP, "usbh2_stp");
	gpio_direction_output(EFIKASB_USBH2_STP, 0);
	msleep(1);
	gpio_set_value(EFIKASB_USBH2_STP, 1);
	msleep(1);

	gpio_free(EFIKASB_USBH2_STP);
	mxc_iomux_v3_setup_pad(usbh2stp);

	mdelay(10);

	return mx51_initialize_usb_hw(pdev->id, MXC_EHCI_ITC_NO_THRESHOLD);
}

static struct mxc_usbh_platform_data usbh2_config __initdata = {
	.init   = initialize_usbh2_port,
	.portsc = MXC_EHCI_MODE_ULPI,
};

static void __init mx51_efikasb_usb(void)
{
	usbh2_config.otg = imx_otg_ulpi_create(ULPI_OTG_DRVVBUS |
			ULPI_OTG_DRVVBUS_EXT | ULPI_OTG_EXTVBUSIND);
	if (usbh2_config.otg)
		imx51_add_mxc_ehci_hs(2, &usbh2_config);
}

static const struct gpio_led mx51_efikasb_leds[] __initconst = {
	{
		.name = "efikasb:green",
		.default_trigger = "default-on",
		.gpio = EFIKASB_GREEN_LED,
		.active_low = 1,
	},
	{
		.name = "efikasb:white",
		.default_trigger = "caps",
		.gpio = EFIKASB_WHITE_LED,
	},
};

static const struct gpio_led_platform_data
		mx51_efikasb_leds_data __initconst = {
	.leds = mx51_efikasb_leds,
	.num_leds = ARRAY_SIZE(mx51_efikasb_leds),
};

static struct gpio_keys_button mx51_efikasb_keys[] = {
	{
		.code = KEY_POWER,
		.gpio = EFIKASB_PWRKEY,
		.type = EV_KEY,
		.desc = "Power Button",
		.wakeup = 1,
		.active_low = 1,
	},
	{
		.code = SW_LID,
		.gpio = EFIKASB_LID,
		.type = EV_SW,
		.desc = "Lid Switch",
		.active_low = 1,
	},
	{
		.code = KEY_RFKILL,
		.gpio = EFIKASB_RFKILL,
		.type = EV_KEY,
		.desc = "rfkill",
		.active_low = 1,
	},
};

static const struct gpio_keys_platform_data mx51_efikasb_keys_data __initconst = {
	.buttons = mx51_efikasb_keys,
	.nbuttons = ARRAY_SIZE(mx51_efikasb_keys),
};

static struct esdhc_platform_data sd0_pdata = {
#define EFIKASB_SD1_CD	IMX_GPIO_NR(2, 27)
	.cd_gpio = EFIKASB_SD1_CD,
	.cd_type = ESDHC_CD_GPIO,
	.wp_type = ESDHC_WP_CONTROLLER,
};

static struct esdhc_platform_data sd1_pdata = {
	.cd_type = ESDHC_CD_CONTROLLER,
	.wp_type = ESDHC_WP_CONTROLLER,
};

static struct regulator *pwgt1, *pwgt2;

static void mx51_efikasb_power_off(void)
{
	gpio_set_value(EFIKA_USB_PHY_RESET, 0);

	if (!IS_ERR(pwgt1) && !IS_ERR(pwgt2)) {
		regulator_disable(pwgt2);
		regulator_disable(pwgt1);
	}
	gpio_direction_output(EFIKASB_POWEROFF, 1);
}

static int __init mx51_efikasb_power_init(void)
{
	pwgt1 = regulator_get(NULL, "pwgt1");
	pwgt2 = regulator_get(NULL, "pwgt2");
	if (!IS_ERR(pwgt1) && !IS_ERR(pwgt2)) {
		regulator_enable(pwgt1);
		regulator_enable(pwgt2);
	}
	gpio_request(EFIKASB_POWEROFF, "poweroff");
	pm_power_off = mx51_efikasb_power_off;

	regulator_has_full_constraints();

	return 0;
}

static void __init mx51_efikasb_init_late(void)
{
	imx51_init_late();
	mx51_efikasb_power_init();
}

/* 01     R1.3 board
   10     R2.0 board */
static void __init mx51_efikasb_board_id(void)
{
	int id;

	gpio_request(EFIKASB_PCBID0, "pcb id0");
	gpio_direction_input(EFIKASB_PCBID0);
	gpio_request(EFIKASB_PCBID1, "pcb id1");
	gpio_direction_input(EFIKASB_PCBID1);

	id = gpio_get_value(EFIKASB_PCBID0) ? 1 : 0;
	id |= (gpio_get_value(EFIKASB_PCBID1) ? 1 : 0) << 1;

	switch (id) {
	default:
		break;
	case 1:
		system_rev = 0x13;
		break;
	case 2:
		system_rev = 0x20;
		break;
	}
}

static void __init efikasb_board_init(void)
{
	imx51_soc_init();

	mxc_iomux_v3_setup_multiple_pads(mx51efikasb_pads,
					ARRAY_SIZE(mx51efikasb_pads));
	efika_board_common_init();

	mx51_efikasb_board_id();
	mx51_efikasb_usb();
	imx51_add_sdhci_esdhc_imx(0, &sd0_pdata);
	imx51_add_sdhci_esdhc_imx(1, &sd1_pdata);

	gpio_led_register_device(-1, &mx51_efikasb_leds_data);
	imx_add_gpio_keys(&mx51_efikasb_keys_data);
}

static void __init mx51_efikasb_timer_init(void)
{
	mx51_clocks_init(32768, 24000000, 22579200, 24576000);
}

static struct sys_timer mx51_efikasb_timer = {
	.init	= mx51_efikasb_timer_init,
};

MACHINE_START(MX51_EFIKASB, "Genesi Efika MX (Smartbook)")
	.atag_offset = 0x100,
	.map_io = mx51_map_io,
	.init_early = imx51_init_early,
	.init_irq = mx51_init_irq,
	.handle_irq = imx51_handle_irq,
	.init_machine =  efikasb_board_init,
	.init_late = mx51_efikasb_init_late,
	.timer = &mx51_efikasb_timer,
	.restart	= mxc_restart,
MACHINE_END
