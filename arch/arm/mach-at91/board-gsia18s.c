/*
 *  Copyright (C) 2010 Christian Glindkamp <christian.glindkamp@taskit.de>
 *                     taskit GmbH
 *                2010 Igor Plyatov <plyatov@gmail.com>
 *                     GeoSIG Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/w1-gpio.h>
#include <linux/i2c.h>
#include <linux/i2c/pcf857x.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/at91sam9_smc.h>
#include <mach/hardware.h>

#include "at91_aic.h"
#include "board.h"
#include "sam9_smc.h"
#include "generic.h"
#include "gsia18s.h"
#include "stamp9g20.h"
#include "gpio.h"

static void __init gsia18s_init_early(void)
{
	stamp9g20_init_early();
}

/*
 * Two USB Host ports
 */
static struct at91_usbh_data __initdata usbh_data = {
	.ports		= 2,
	.vbus_pin	= {-EINVAL, -EINVAL},
	.overcurrent_pin= {-EINVAL, -EINVAL},
};

/*
 * USB Device port
 */
static struct at91_udc_data __initdata udc_data = {
	.vbus_pin	= AT91_PIN_PA22,
	.pullup_pin	= -EINVAL,		/* pull-up driven by UDC */
};

/*
 * MACB Ethernet device
 */
static struct macb_platform_data __initdata macb_data = {
	.phy_irq_pin	= AT91_PIN_PA28,
	.is_rmii	= 1,
};

/*
 * LEDs and GPOs
 */
static struct gpio_led gpio_leds[] = {
	{
		.name			= "gpo:spi1reset",
		.gpio			= AT91_PIN_PC1,
		.active_low		= 0,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	{
		.name			= "gpo:trig_net_out",
		.gpio			= AT91_PIN_PB20,
		.active_low		= 0,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	{
		.name			= "gpo:trig_net_dir",
		.gpio			= AT91_PIN_PB19,
		.active_low		= 0,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	{
		.name			= "gpo:charge_dis",
		.gpio			= AT91_PIN_PC2,
		.active_low		= 0,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	{
		.name			= "led:event",
		.gpio			= AT91_PIN_PB17,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	{
		.name			= "led:lan",
		.gpio			= AT91_PIN_PB18,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	{
		.name			= "led:error",
		.gpio			= AT91_PIN_PB16,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_ON,
	}
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device leds = {
	.name	= "leds-gpio",
	.id	= 0,
	.dev	= {
		.platform_data	= &gpio_led_info,
	}
};

static void __init gsia18s_leds_init(void)
{
	platform_device_register(&leds);
}

/* PCF8574 0x20 GPIO - U1 on the GS_IA18-CB_V3 board */
static struct gpio_led pcf_gpio_leds1[] = {
	{ /* bit 0 */
		.name			= "gpo:hdc_power",
		.gpio			= PCF_GPIO_HDC_POWER,
		.active_low		= 0,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	{ /* bit 1 */
		.name			= "gpo:wifi_setup",
		.gpio			= PCF_GPIO_WIFI_SETUP,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	{ /* bit 2 */
		.name			= "gpo:wifi_enable",
		.gpio			= PCF_GPIO_WIFI_ENABLE,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	{ /* bit 3	*/
		.name			= "gpo:wifi_reset",
		.gpio			= PCF_GPIO_WIFI_RESET,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_ON,
	},
	/* bit 4 used as GPI	*/
	{ /* bit 5 */
		.name			= "gpo:gps_setup",
		.gpio			= PCF_GPIO_GPS_SETUP,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	{ /* bit 6 */
		.name			= "gpo:gps_standby",
		.gpio			= PCF_GPIO_GPS_STANDBY,
		.active_low		= 0,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_ON,
	},
	{ /* bit 7 */
		.name			= "gpo:gps_power",
		.gpio			= PCF_GPIO_GPS_POWER,
		.active_low		= 0,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	}
};

static struct gpio_led_platform_data pcf_gpio_led_info1 = {
	.leds		= pcf_gpio_leds1,
	.num_leds	= ARRAY_SIZE(pcf_gpio_leds1),
};

static struct platform_device pcf_leds1 = {
	.name	= "leds-gpio", /* GS_IA18-CB_board */
	.id	= 1,
	.dev	= {
		.platform_data	= &pcf_gpio_led_info1,
	}
};

/* PCF8574 0x22 GPIO - U1 on the GS_2G_OPT1-A_V0 board (Alarm) */
static struct gpio_led pcf_gpio_leds2[] = {
	{ /* bit 0 */
		.name			= "gpo:alarm_1",
		.gpio			= PCF_GPIO_ALARM1,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	{ /* bit 1 */
		.name			= "gpo:alarm_2",
		.gpio			= PCF_GPIO_ALARM2,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	{ /* bit 2 */
		.name			= "gpo:alarm_3",
		.gpio			= PCF_GPIO_ALARM3,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	{ /* bit 3 */
		.name			= "gpo:alarm_4",
		.gpio			= PCF_GPIO_ALARM4,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
	/* bits 4, 5, 6 not used */
	{ /* bit 7 */
		.name			= "gpo:alarm_v_relay_on",
		.gpio			= PCF_GPIO_ALARM_V_RELAY_ON,
		.active_low		= 0,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
};

static struct gpio_led_platform_data pcf_gpio_led_info2 = {
	.leds		= pcf_gpio_leds2,
	.num_leds	= ARRAY_SIZE(pcf_gpio_leds2),
};

static struct platform_device pcf_leds2 = {
	.name	= "leds-gpio",
	.id	= 2,
	.dev	= {
		.platform_data	= &pcf_gpio_led_info2,
	}
};

/* PCF8574 0x24 GPIO U1 on the GS_2G-OPT23-A_V0 board (Modem) */
static struct gpio_led pcf_gpio_leds3[] = {
	{ /* bit 0 */
		.name			= "gpo:modem_power",
		.gpio			= PCF_GPIO_MODEM_POWER,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_OFF,
	},
		/* bits 1 and 2 not used */
	{ /* bit 3 */
		.name			= "gpo:modem_reset",
		.gpio			= PCF_GPIO_MODEM_RESET,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_ON,
	},
		/* bits 4, 5 and 6 not used */
	{ /* bit 7 */
		.name			= "gpo:trx_reset",
		.gpio			= PCF_GPIO_TRX_RESET,
		.active_low		= 1,
		.default_trigger	= "none",
		.default_state		= LEDS_GPIO_DEFSTATE_ON,
	}
};

static struct gpio_led_platform_data pcf_gpio_led_info3 = {
	.leds		= pcf_gpio_leds3,
	.num_leds	= ARRAY_SIZE(pcf_gpio_leds3),
};

static struct platform_device pcf_leds3 = {
	.name	= "leds-gpio",
	.id	= 3,
	.dev	= {
		.platform_data	= &pcf_gpio_led_info3,
	}
};

static void __init gsia18s_pcf_leds_init(void)
{
	platform_device_register(&pcf_leds1);
	platform_device_register(&pcf_leds2);
	platform_device_register(&pcf_leds3);
}

/*
 * SPI busses.
 */
static struct spi_board_info gsia18s_spi_devices[] = {
	{ /* User accessible spi0, cs0 used for communication with MSP RTC */
		.modalias	= "spidev",
		.bus_num	= 0,
		.chip_select	= 0,
		.max_speed_hz	= 580000,
		.mode		= SPI_MODE_1,
	},
	{ /* User accessible spi1, cs0 used for communication with int. DSP */
		.modalias	= "spidev",
		.bus_num	= 1,
		.chip_select	= 0,
		.max_speed_hz	= 5600000,
		.mode		= SPI_MODE_0,
	},
	{ /* User accessible spi1, cs1 used for communication with ext. DSP */
		.modalias	= "spidev",
		.bus_num	= 1,
		.chip_select	= 1,
		.max_speed_hz	= 5600000,
		.mode		= SPI_MODE_0,
	},
	{ /* User accessible spi1, cs2 used for communication with ext. DSP */
		.modalias	= "spidev",
		.bus_num	= 1,
		.chip_select	= 2,
		.max_speed_hz	= 5600000,
		.mode		= SPI_MODE_0,
	},
	{ /* User accessible spi1, cs3 used for communication with ext. DSP */
		.modalias	= "spidev",
		.bus_num	= 1,
		.chip_select	= 3,
		.max_speed_hz	= 5600000,
		.mode		= SPI_MODE_0,
	}
};

/*
 * GPI Buttons
 */
static struct gpio_keys_button buttons[] = {
	{
		.gpio		= GPIO_TRIG_NET_IN,
		.code		= BTN_1,
		.desc		= "TRIG_NET_IN",
		.type		= EV_KEY,
		.active_low	= 0,
		.wakeup		= 1,
	},
	{ /* SW80 on the GS_IA18_S-MN board*/
		.gpio		= GPIO_CARD_UNMOUNT_0,
		.code		= BTN_2,
		.desc		= "Card umount 0",
		.type		= EV_KEY,
		.active_low	= 1,
		.wakeup		= 1,
	},
	{ /* SW79 on the GS_IA18_S-MN board*/
		.gpio		= GPIO_CARD_UNMOUNT_1,
		.code		= BTN_3,
		.desc		= "Card umount 1",
		.type		= EV_KEY,
		.active_low	= 1,
		.wakeup		= 1,
	},
	{ /* SW280 on the GS_IA18-CB board*/
		.gpio		= GPIO_KEY_POWER,
		.code		= KEY_POWER,
		.desc		= "Power Off Button",
		.type		= EV_KEY,
		.active_low	= 0,
		.wakeup		= 1,
	}
};

static struct gpio_keys_platform_data button_data = {
	.buttons	= buttons,
	.nbuttons	= ARRAY_SIZE(buttons),
};

static struct platform_device button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &button_data,
	}
};

static void __init gsia18s_add_device_buttons(void)
{
	at91_set_gpio_input(GPIO_TRIG_NET_IN, 1);
	at91_set_deglitch(GPIO_TRIG_NET_IN, 1);
	at91_set_gpio_input(GPIO_CARD_UNMOUNT_0, 1);
	at91_set_deglitch(GPIO_CARD_UNMOUNT_0, 1);
	at91_set_gpio_input(GPIO_CARD_UNMOUNT_1, 1);
	at91_set_deglitch(GPIO_CARD_UNMOUNT_1, 1);
	at91_set_gpio_input(GPIO_KEY_POWER, 0);
	at91_set_deglitch(GPIO_KEY_POWER, 1);

	platform_device_register(&button_device);
}

/*
 * I2C
 */
static int pcf8574x_0x20_setup(struct i2c_client *client, int gpio,
				unsigned int ngpio, void *context)
{
	int status;

	status = gpio_request(gpio + PCF_GPIO_ETH_DETECT, "eth_det");
	if (status < 0) {
		pr_err("error: can't request GPIO%d\n",
			gpio + PCF_GPIO_ETH_DETECT);
		return status;
	}
	status = gpio_direction_input(gpio + PCF_GPIO_ETH_DETECT);
	if (status < 0) {
		pr_err("error: can't setup GPIO%d as input\n",
			gpio + PCF_GPIO_ETH_DETECT);
		return status;
	}
	status = gpio_export(gpio + PCF_GPIO_ETH_DETECT, false);
	if (status < 0) {
		pr_err("error: can't export GPIO%d\n",
			gpio + PCF_GPIO_ETH_DETECT);
		return status;
	}
	status = gpio_sysfs_set_active_low(gpio + PCF_GPIO_ETH_DETECT, 1);
	if (status < 0) {
		pr_err("error: gpio_sysfs_set active_low(GPIO%d, 1)\n",
			gpio + PCF_GPIO_ETH_DETECT);
		return status;
	}

	return 0;
}

static int pcf8574x_0x20_teardown(struct i2c_client *client, int gpio,
					unsigned ngpio, void *context)
{
	gpio_free(gpio + PCF_GPIO_ETH_DETECT);
	return 0;
}

static struct pcf857x_platform_data pcf20_pdata = {
	.gpio_base	= GS_IA18_S_PCF_GPIO_BASE0,
	.n_latch	= (1 << 4),
	.setup		= pcf8574x_0x20_setup,
	.teardown	= pcf8574x_0x20_teardown,
};

static struct pcf857x_platform_data pcf22_pdata = {
	.gpio_base	= GS_IA18_S_PCF_GPIO_BASE1,
};

static struct pcf857x_platform_data pcf24_pdata = {
	.gpio_base	= GS_IA18_S_PCF_GPIO_BASE2,
};

static struct i2c_board_info __initdata gsia18s_i2c_devices[] = {
	{ /* U1 on the GS_IA18-CB_V3 board */
		I2C_BOARD_INFO("pcf8574", 0x20),
		.platform_data = &pcf20_pdata,
	},
	{ /* U1 on the GS_2G_OPT1-A_V0 board (Alarm) */
		I2C_BOARD_INFO("pcf8574", 0x22),
		.platform_data = &pcf22_pdata,
	},
	{ /* U1 on the GS_2G-OPT23-A_V0 board (Modem) */
		I2C_BOARD_INFO("pcf8574", 0x24),
		.platform_data = &pcf24_pdata,
	},
	{ /* U161 on the GS_IA18_S-MN board */
		I2C_BOARD_INFO("24c1024", 0x50),
	},
	{ /* U162 on the GS_IA18_S-MN board */
		I2C_BOARD_INFO("24c01", 0x53),
	},
};

/*
 * Compact Flash
 */
static struct at91_cf_data __initdata gsia18s_cf1_data = {
	.irq_pin	= AT91_PIN_PA27,
	.det_pin	= AT91_PIN_PB30,
	.vcc_pin	= -EINVAL,
	.rst_pin	= AT91_PIN_PB31,
	.chipselect	= 5,
	.flags		= AT91_CF_TRUE_IDE,
};

/* Power Off by RTC */
static void gsia18s_power_off(void)
{
	pr_notice("Power supply will be switched off automatically now or after 60 seconds without ArmDAS.\n");
	at91_set_gpio_output(AT91_PIN_PA25, 1);
	/* Spin to death... */
	while (1)
		;
}

static int __init gsia18s_power_off_init(void)
{
	pm_power_off = gsia18s_power_off;
	return 0;
}

/* ---------------------------------------------------------------------------*/

static void __init gsia18s_board_init(void)
{
	/*
	 * USART0 on ttyS1 (Rx, Tx, CTS, RTS, DTR, DSR, DCD, RI).
	 * Used for Internal Analog Modem.
	 */
	at91_register_uart(AT91SAM9260_ID_US0, 1,
				ATMEL_UART_CTS | ATMEL_UART_RTS |
				ATMEL_UART_DTR | ATMEL_UART_DSR |
				ATMEL_UART_DCD | ATMEL_UART_RI);
	/*
	 * USART1 on ttyS2 (Rx, Tx, CTS, RTS).
	 * Used for GPS or WiFi or Data stream.
	 */
	at91_register_uart(AT91SAM9260_ID_US1, 2,
				ATMEL_UART_CTS | ATMEL_UART_RTS);
	/*
	 * USART2 on ttyS3 (Rx, Tx, CTS, RTS).
	 * Used for External Modem.
	 */
	at91_register_uart(AT91SAM9260_ID_US2, 3,
				ATMEL_UART_CTS | ATMEL_UART_RTS);
	/*
	 * USART3 on ttyS4 (Rx, Tx, RTS).
	 * Used for RS-485.
	 */
	at91_register_uart(AT91SAM9260_ID_US3, 4, ATMEL_UART_RTS);

	/*
	 * USART4 on ttyS5 (Rx, Tx).
	 * Used for TRX433 Radio Module.
	 */
	at91_register_uart(AT91SAM9260_ID_US4, 5, 0);
	stamp9g20_board_init();
	at91_add_device_usbh(&usbh_data);
	at91_add_device_udc(&udc_data);
	at91_add_device_eth(&macb_data);
	gsia18s_leds_init();
	gsia18s_pcf_leds_init();
	gsia18s_add_device_buttons();
	at91_add_device_i2c(gsia18s_i2c_devices,
				ARRAY_SIZE(gsia18s_i2c_devices));
	at91_add_device_cf(&gsia18s_cf1_data);
	at91_add_device_spi(gsia18s_spi_devices,
				ARRAY_SIZE(gsia18s_spi_devices));
	gsia18s_power_off_init();
}

MACHINE_START(GSIA18S, "GS_IA18_S")
	.init_time	= at91_init_time,
	.map_io		= at91_map_io,
	.handle_irq	= at91_aic_handle_irq,
	.init_early	= gsia18s_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= gsia18s_board_init,
MACHINE_END
