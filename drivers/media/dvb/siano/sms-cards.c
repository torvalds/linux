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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "sms-cards.h"

static int sms_dbg;
module_param_named(cards_dbg, sms_dbg, int, 0644);
MODULE_PARM_DESC(cards_dbg, "set debug level (info=1, adv=2 (or-able))");

static struct sms_board sms_boards[] = {
	[SMS_BOARD_UNKNOWN] = {
		.name	= "Unknown board",
	},
	[SMS1XXX_BOARD_SIANO_STELLAR] = {
		.name	= "Siano Stellar Digital Receiver",
		.type	= SMS_STELLAR,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-stellar-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_SIANO_NOVA_A] = {
		.name	= "Siano Nova A Digital Receiver",
		.type	= SMS_NOVA_A0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-nova-a-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_SIANO_NOVA_B] = {
		.name	= "Siano Nova B Digital Receiver",
		.type	= SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-nova-b-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_SIANO_VEGA] = {
		.name	= "Siano Vega Digital Receiver",
		.type	= SMS_VEGA,
	},
	[SMS1XXX_BOARD_HAUPPAUGE_CATAMOUNT] = {
		.name	= "Hauppauge Catamount",
		.type	= SMS_STELLAR,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-stellar-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_HAUPPAUGE_OKEMO_A] = {
		.name	= "Hauppauge Okemo-A",
		.type	= SMS_NOVA_A0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-nova-a-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_HAUPPAUGE_OKEMO_B] = {
		.name	= "Hauppauge Okemo-B",
		.type	= SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-nova-b-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_HAUPPAUGE_WINDHAM] = {
		.name	= "Hauppauge WinTV MiniStick",
		.type	= SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-hcw-55xxx-dvbt-02.fw",
		.led_power = 26,
		.led_lo    = 27,
		.led_hi    = 28,
	},
	[SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD] = {
		.name	= "Hauppauge WinTV MiniCard",
		.type	= SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-hcw-55xxx-dvbt-02.fw",
		.lna_ctrl  = 29,
		.rf_switch = 17,
	},
	[SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD_R2] = {
		.name	= "Hauppauge WinTV MiniCard",
		.type	= SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-hcw-55xxx-dvbt-02.fw",
		.lna_ctrl  = -1,
	},
};

struct sms_board *sms_get_board(int id)
{
	BUG_ON(id >= ARRAY_SIZE(sms_boards));

	return &sms_boards[id];
}
EXPORT_SYMBOL_GPL(sms_get_board);

static int sms_set_gpio(struct smscore_device_t *coredev, int pin, int enable)
{
	int lvl, ret;
	u32 gpio;
	struct smscore_gpio_config gpioconfig = {
		.direction            = SMS_GPIO_DIRECTION_OUTPUT,
		.pullupdown           = SMS_GPIO_PULLUPDOWN_NONE,
		.inputcharacteristics = SMS_GPIO_INPUTCHARACTERISTICS_NORMAL,
		.outputslewrate       = SMS_GPIO_OUTPUTSLEWRATE_FAST,
		.outputdriving        = SMS_GPIO_OUTPUTDRIVING_4mA,
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

	/* dont touch GPIO if LEDs are already set */
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

	sms_debug("%s: LNA %s", __func__, onoff ? "enabled" : "disabled");

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
	switch (id) {
	case SMS1XXX_BOARD_HAUPPAUGE_CATAMOUNT:
	case SMS1XXX_BOARD_HAUPPAUGE_OKEMO_A:
	case SMS1XXX_BOARD_HAUPPAUGE_OKEMO_B:
	case SMS1XXX_BOARD_HAUPPAUGE_WINDHAM:
		request_module("smsdvb");
		break;
	default:
		/* do nothing */
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sms_board_load_modules);
