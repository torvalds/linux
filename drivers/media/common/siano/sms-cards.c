/*
 *  Card-specific functions for the Siano SMS1xxx USB dongle
 *
 *  Copyright (c) 2008 Michael Krufky <mkrufky@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation;
 *
 *  Software distributed under the License is distributed on an "AS IS"
 *  basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *
 *  See the GNU General Public License for more details.
 */

#include "sms-cards.h"
#include "smsir.h"
#include <linux/module.h>

static struct sms_board sms_boards[] = {
	[SMS_BOARD_UNKNOWN] = {
		.name	= "Unknown board",
		.type = SMS_UNKNOWN_TYPE,
		.default_mode = DEVICE_MODE_NONE,
	},
	[SMS1XXX_BOARD_SIANO_STELLAR] = {
		.name	= "Siano Stellar Digital Receiver",
		.type	= SMS_STELLAR,
		.default_mode = DEVICE_MODE_DVBT_BDA,
	},
	[SMS1XXX_BOARD_SIANO_NOVA_A] = {
		.name	= "Siano Nova A Digital Receiver",
		.type	= SMS_NOVA_A0,
		.default_mode = DEVICE_MODE_DVBT_BDA,
	},
	[SMS1XXX_BOARD_SIANO_NOVA_B] = {
		.name	= "Siano Nova B Digital Receiver",
		.type	= SMS_NOVA_B0,
		.default_mode = DEVICE_MODE_DVBT_BDA,
	},
	[SMS1XXX_BOARD_SIANO_VEGA] = {
		.name	= "Siano Vega Digital Receiver",
		.type	= SMS_VEGA,
		.default_mode = DEVICE_MODE_CMMB,
	},
	[SMS1XXX_BOARD_HAUPPAUGE_CATAMOUNT] = {
		.name	= "Hauppauge Catamount",
		.type	= SMS_STELLAR,
		.fw[DEVICE_MODE_DVBT_BDA] = SMS_FW_DVBT_STELLAR,
		.default_mode = DEVICE_MODE_DVBT_BDA,
	},
	[SMS1XXX_BOARD_HAUPPAUGE_OKEMO_A] = {
		.name	= "Hauppauge Okemo-A",
		.type	= SMS_NOVA_A0,
		.fw[DEVICE_MODE_DVBT_BDA] = SMS_FW_DVBT_NOVA_A,
		.default_mode = DEVICE_MODE_DVBT_BDA,
	},
	[SMS1XXX_BOARD_HAUPPAUGE_OKEMO_B] = {
		.name	= "Hauppauge Okemo-B",
		.type	= SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = SMS_FW_DVBT_NOVA_B,
		.default_mode = DEVICE_MODE_DVBT_BDA,
	},
	[SMS1XXX_BOARD_HAUPPAUGE_WINDHAM] = {
		.name	= "Hauppauge WinTV MiniStick",
		.type	= SMS_NOVA_B0,
		.fw[DEVICE_MODE_ISDBT_BDA] = SMS_FW_ISDBT_HCW_55XXX,
		.fw[DEVICE_MODE_DVBT_BDA]  = SMS_FW_DVBT_HCW_55XXX,
		.default_mode = DEVICE_MODE_DVBT_BDA,
		.rc_codes = RC_MAP_HAUPPAUGE,
		.board_cfg.leds_power = 26,
		.board_cfg.led0 = 27,
		.board_cfg.led1 = 28,
		.board_cfg.ir = 9,
		.led_power = 26,
		.led_lo    = 27,
		.led_hi    = 28,
	},
	[SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD] = {
		.name	= "Hauppauge WinTV MiniCard",
		.type	= SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = SMS_FW_DVBT_HCW_55XXX,
		.default_mode = DEVICE_MODE_DVBT_BDA,
		.lna_ctrl  = 29,
		.board_cfg.foreign_lna0_ctrl = 29,
		.rf_switch = 17,
		.board_cfg.rf_switch_uhf = 17,
	},
	[SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD_R2] = {
		.name	= "Hauppauge WinTV MiniCard",
		.type	= SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = SMS_FW_DVBT_HCW_55XXX,
		.default_mode = DEVICE_MODE_DVBT_BDA,
		.lna_ctrl  = -1,
	},
	[SMS1XXX_BOARD_SIANO_NICE] = {
		.name = "Siano Nice Digital Receiver",
		.type = SMS_NOVA_B0,
		.default_mode = DEVICE_MODE_DVBT_BDA,
	},
	[SMS1XXX_BOARD_SIANO_VENICE] = {
		.name = "Siano Venice Digital Receiver",
		.type = SMS_VEGA,
		.default_mode = DEVICE_MODE_CMMB,
	},
	[SMS1XXX_BOARD_SIANO_STELLAR_ROM] = {
		.name = "Siano Stellar Digital Receiver ROM",
		.type = SMS_STELLAR,
		.default_mode = DEVICE_MODE_DVBT_BDA,
		.intf_num = 1,
	},
	[SMS1XXX_BOARD_ZTE_DVB_DATA_CARD] = {
		.name = "ZTE Data Card Digital Receiver",
		.type = SMS_NOVA_B0,
		.default_mode = DEVICE_MODE_DVBT_BDA,
		.intf_num = 5,
		.mtu = 15792,
	},
	[SMS1XXX_BOARD_ONDA_MDTV_DATA_CARD] = {
		.name = "ONDA Data Card Digital Receiver",
		.type = SMS_NOVA_B0,
		.default_mode = DEVICE_MODE_DVBT_BDA,
		.intf_num = 6,
		.mtu = 15792,
	},
	[SMS1XXX_BOARD_SIANO_MING] = {
		.name = "Siano Ming Digital Receiver",
		.type = SMS_MING,
		.default_mode = DEVICE_MODE_CMMB,
	},
	[SMS1XXX_BOARD_SIANO_PELE] = {
		.name = "Siano Pele Digital Receiver",
		.type = SMS_PELE,
		.default_mode = DEVICE_MODE_ISDBT_BDA,
	},
	[SMS1XXX_BOARD_SIANO_RIO] = {
		.name = "Siano Rio Digital Receiver",
		.type = SMS_RIO,
		.default_mode = DEVICE_MODE_ISDBT_BDA,
	},
	[SMS1XXX_BOARD_SIANO_DENVER_1530] = {
		.name = "Siano Denver (ATSC-M/H) Digital Receiver",
		.type = SMS_DENVER_1530,
		.default_mode = DEVICE_MODE_ATSC,
		.crystal = 2400,
	},
	[SMS1XXX_BOARD_SIANO_DENVER_2160] = {
		.name = "Siano Denver (TDMB) Digital Receiver",
		.type = SMS_DENVER_2160,
		.default_mode = DEVICE_MODE_DAB_TDMB,
	},
	[SMS1XXX_BOARD_PCTV_77E] = {
		.name	= "Hauppauge microStick 77e",
		.type	= SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = SMS_FW_DVB_NOVA_12MHZ_B0,
		.default_mode = DEVICE_MODE_DVBT_BDA,
	},
};

struct sms_board *sms_get_board(unsigned id)
{
	BUG_ON(id >= ARRAY_SIZE(sms_boards));

	return &sms_boards[id];
}
EXPORT_SYMBOL_GPL(sms_get_board);
static inline void sms_gpio_assign_11xx_default_led_config(
		struct smscore_config_gpio *p_gpio_config) {
	p_gpio_config->direction = SMS_GPIO_DIRECTION_OUTPUT;
	p_gpio_config->inputcharacteristics =
		SMS_GPIO_INPUTCHARACTERISTICS_NORMAL;
	p_gpio_config->outputdriving = SMS_GPIO_OUTPUTDRIVING_4mA;
	p_gpio_config->outputslewrate = SMS_GPIO_OUTPUT_SLEW_RATE_0_45_V_NS;
	p_gpio_config->pullupdown = SMS_GPIO_PULLUPDOWN_NONE;
}

int sms_board_event(struct smscore_device_t *coredev,
		    enum SMS_BOARD_EVENTS gevent)
{
	struct smscore_config_gpio my_gpio_config;

	sms_gpio_assign_11xx_default_led_config(&my_gpio_config);

	switch (gevent) {
	case BOARD_EVENT_POWER_INIT: /* including hotplug */
		break; /* BOARD_EVENT_BIND */

	case BOARD_EVENT_POWER_SUSPEND:
		break; /* BOARD_EVENT_POWER_SUSPEND */

	case BOARD_EVENT_POWER_RESUME:
		break; /* BOARD_EVENT_POWER_RESUME */

	case BOARD_EVENT_BIND:
		break; /* BOARD_EVENT_BIND */

	case BOARD_EVENT_SCAN_PROG:
		break; /* BOARD_EVENT_SCAN_PROG */
	case BOARD_EVENT_SCAN_COMP:
		break; /* BOARD_EVENT_SCAN_COMP */
	case BOARD_EVENT_EMERGENCY_WARNING_SIGNAL:
		break; /* BOARD_EVENT_EMERGENCY_WARNING_SIGNAL */
	case BOARD_EVENT_FE_LOCK:
		break; /* BOARD_EVENT_FE_LOCK */
	case BOARD_EVENT_FE_UNLOCK:
		break; /* BOARD_EVENT_FE_UNLOCK */
	case BOARD_EVENT_DEMOD_LOCK:
		break; /* BOARD_EVENT_DEMOD_LOCK */
	case BOARD_EVENT_DEMOD_UNLOCK:
		break; /* BOARD_EVENT_DEMOD_UNLOCK */
	case BOARD_EVENT_RECEPTION_MAX_4:
		break; /* BOARD_EVENT_RECEPTION_MAX_4 */
	case BOARD_EVENT_RECEPTION_3:
		break; /* BOARD_EVENT_RECEPTION_3 */
	case BOARD_EVENT_RECEPTION_2:
		break; /* BOARD_EVENT_RECEPTION_2 */
	case BOARD_EVENT_RECEPTION_1:
		break; /* BOARD_EVENT_RECEPTION_1 */
	case BOARD_EVENT_RECEPTION_LOST_0:
		break; /* BOARD_EVENT_RECEPTION_LOST_0 */
	case BOARD_EVENT_MULTIPLEX_OK:
		break; /* BOARD_EVENT_MULTIPLEX_OK */
	case BOARD_EVENT_MULTIPLEX_ERRORS:
		break; /* BOARD_EVENT_MULTIPLEX_ERRORS */

	default:
		pr_err("Unknown SMS board event\n");
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sms_board_event);

static int sms_set_gpio(struct smscore_device_t *coredev, int pin, int enable)
{
	int lvl, ret;
	u32 gpio;
	struct smscore_config_gpio gpioconfig = {
		.direction            = SMS_GPIO_DIRECTION_OUTPUT,
		.pullupdown           = SMS_GPIO_PULLUPDOWN_NONE,
		.inputcharacteristics = SMS_GPIO_INPUTCHARACTERISTICS_NORMAL,
		.outputslewrate       = SMS_GPIO_OUTPUT_SLEW_RATE_FAST,
		.outputdriving        = SMS_GPIO_OUTPUTDRIVING_S_4mA,
	};

	if (pin == 0)
		return -EINVAL;

	if (pin < 0) {
		/* inverted gpio */
		gpio = pin * -1;
		lvl = enable ? 0 : 1;
	} else {
		gpio = pin;
		lvl = enable ? 1 : 0;
	}

	ret = smscore_configure_gpio(coredev, gpio, &gpioconfig);
	if (ret < 0)
		return ret;

	return smscore_set_gpio(coredev, gpio, lvl);
}

int sms_board_setup(struct smscore_device_t *coredev)
{
	int board_id = smscore_get_board_id(coredev);
	struct sms_board *board = sms_get_board(board_id);

	switch (board_id) {
	case SMS1XXX_BOARD_HAUPPAUGE_WINDHAM:
		/* turn off all LEDs */
		sms_set_gpio(coredev, board->led_power, 0);
		sms_set_gpio(coredev, board->led_hi, 0);
		sms_set_gpio(coredev, board->led_lo, 0);
		break;
	case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD_R2:
	case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD:
		/* turn off LNA */
		sms_set_gpio(coredev, board->lna_ctrl, 0);
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sms_board_setup);

int sms_board_power(struct smscore_device_t *coredev, int onoff)
{
	int board_id = smscore_get_board_id(coredev);
	struct sms_board *board = sms_get_board(board_id);

	switch (board_id) {
	case SMS1XXX_BOARD_HAUPPAUGE_WINDHAM:
		/* power LED */
		sms_set_gpio(coredev,
			     board->led_power, onoff ? 1 : 0);
		break;
	case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD_R2:
	case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD:
		/* LNA */
		if (!onoff)
			sms_set_gpio(coredev, board->lna_ctrl, 0);
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sms_board_power);

int sms_board_led_feedback(struct smscore_device_t *coredev, int led)
{
	int board_id = smscore_get_board_id(coredev);
	struct sms_board *board = sms_get_board(board_id);

	/* don't touch GPIO if LEDs are already set */
	if (smscore_led_state(coredev, -1) == led)
		return 0;

	switch (board_id) {
	case SMS1XXX_BOARD_HAUPPAUGE_WINDHAM:
		sms_set_gpio(coredev,
			     board->led_lo, (led & SMS_LED_LO) ? 1 : 0);
		sms_set_gpio(coredev,
			     board->led_hi, (led & SMS_LED_HI) ? 1 : 0);

		smscore_led_state(coredev, led);
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sms_board_led_feedback);

int sms_board_lna_control(struct smscore_device_t *coredev, int onoff)
{
	int board_id = smscore_get_board_id(coredev);
	struct sms_board *board = sms_get_board(board_id);

	pr_debug("%s: LNA %s\n", __func__, onoff ? "enabled" : "disabled");

	switch (board_id) {
	case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD_R2:
	case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD:
		sms_set_gpio(coredev,
			     board->rf_switch, onoff ? 1 : 0);
		return sms_set_gpio(coredev,
				    board->lna_ctrl, onoff ? 1 : 0);
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(sms_board_lna_control);

int sms_board_load_modules(int id)
{
	request_module("smsdvb");
	return 0;
}
EXPORT_SYMBOL_GPL(sms_board_load_modules);
