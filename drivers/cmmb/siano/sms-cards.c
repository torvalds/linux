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

struct usb_device_id smsusb_id_table[] = {
	{ USB_DEVICE(0x187f, 0x0010),
		.driver_info = SMS1XXX_BOARD_SIANO_STELLAR },
	{ USB_DEVICE(0x187f, 0x0100),
		.driver_info = SMS1XXX_BOARD_SIANO_STELLAR },
	{ USB_DEVICE(0x187f, 0x0200),
		.driver_info = SMS1XXX_BOARD_SIANO_NOVA_A },
	{ USB_DEVICE(0x187f, 0x0201),
		.driver_info = SMS1XXX_BOARD_SIANO_NOVA_B },
	{ USB_DEVICE(0x187f, 0x0300),
		.driver_info = SMS1XXX_BOARD_SIANO_VEGA },
	{ USB_DEVICE(0x2040, 0x1700),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_CATAMOUNT },
	{ USB_DEVICE(0x2040, 0x1800),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_OKEMO_A },
	{ USB_DEVICE(0x2040, 0x1801),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_OKEMO_B },
	{ USB_DEVICE(0x2040, 0x2000),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD },
	{ USB_DEVICE(0x2040, 0x2009),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD_R2 },
	{ USB_DEVICE(0x2040, 0x200a),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD },
	{ USB_DEVICE(0x2040, 0x2010),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD },
	{ USB_DEVICE(0x2040, 0x2019),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD },
	{ USB_DEVICE(0x2040, 0x5500),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0x5510),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0x5520),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0x5530),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0x5580),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0x5590),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x187f, 0x0202),
		.driver_info = SMS1XXX_BOARD_SIANO_NICE },
	{ USB_DEVICE(0x187f, 0x0301),
		.driver_info = SMS1XXX_BOARD_SIANO_VENICE },
	{ } /* Terminating entry */
	};

MODULE_DEVICE_TABLE(usb, smsusb_id_table);

static struct sms_board sms_boards[] = {
	[SMS_BOARD_UNKNOWN] = {
	/* 0 */
		.name = "Unknown board",
	},
	[SMS1XXX_BOARD_SIANO_STELLAR] = {
	/* 1 */
		.name =
		"Siano Stellar Digital Receiver",
		.type = SMS_STELLAR,
		.fw[DEVICE_MODE_DVBT_BDA] =
		"sms1xxx-stellar-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_SIANO_NOVA_A] = {
	/* 2 */
		.name = "Siano Nova A Digital Receiver",
		.type = SMS_NOVA_A0,
		.fw[DEVICE_MODE_DVBT_BDA] =
		"sms1xxx-nova-a-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_SIANO_NOVA_B] = {
	/* 3 */
		.name = "Siano Nova B Digital Receiver",
		.type = SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] =
		"sms1xxx-nova-b-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_SIANO_VEGA] = {
	/* 4 */
		.name = "Siano Vega Digital Receiver",
		.type = SMS_VEGA,
	},
	[SMS1XXX_BOARD_HAUPPAUGE_CATAMOUNT] = {
	/* 5 */
		.name = "Hauppauge Catamount",
		.type = SMS_STELLAR,
		.fw[DEVICE_MODE_DVBT_BDA] =
		"sms1xxx-stellar-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_HAUPPAUGE_OKEMO_A] = {
	/* 6 */
		.name = "Hauppauge Okemo-A",
		.type = SMS_NOVA_A0,
		.fw[DEVICE_MODE_DVBT_BDA] =
		"sms1xxx-nova-a-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_HAUPPAUGE_OKEMO_B] = {
	/* 7 */
		.name = "Hauppauge Okemo-B",
		.type = SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] =
		"sms1xxx-nova-b-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_HAUPPAUGE_WINDHAM] = {
	/* 8 */
		.name = "Hauppauge WinTV MiniStick",
		.type = SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-hcw-55xxx-dvbt-02.fw",
		.board_cfg.leds_power = 26,
		.board_cfg.led0 = 27,
		.board_cfg.led1 = 28,
	},
	[SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD] = {
	/* 9 */
		.name = "Hauppauge WinTV MiniCard",
		.type = SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-hcw-55xxx-dvbt-02.fw",
		.board_cfg.foreign_lna0_ctrl = 29,
	},
	[SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD_R2] = {
	/* 10 */
		.name = "Hauppauge WinTV MiniCard",
		.type = SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-hcw-55xxx-dvbt-02.fw",
		.board_cfg.foreign_lna0_ctrl = 1,
	},
	[SMS1XXX_BOARD_SIANO_NICE] = {
	/* 11 */
		.name = "Siano Nice Digital Receiver",
		.type = SMS_NOVA_B0,
	},
	[SMS1XXX_BOARD_SIANO_VENICE] = {
	/* 12 */
		.name = "Siano Venice Digital Receiver",
		.type = SMS_VEGA,
	},
};

struct sms_board *sms_get_board(int id)
{
	BUG_ON(id >= ARRAY_SIZE(sms_boards));
	return &sms_boards[id];
}

static inline void sms_gpio_assign_11xx_default_led_config(
		struct smscore_gpio_config *pGpioConfig) {
	pGpioConfig->Direction = SMS_GPIO_DIRECTION_OUTPUT;
	pGpioConfig->InputCharacteristics =
		SMS_GPIO_INPUTCHARACTERISTICS_NORMAL;
	pGpioConfig->OutputDriving = SMS_GPIO_OUTPUTDRIVING_4mA;
	pGpioConfig->OutputSlewRate = SMS_GPIO_OUTPUTSLEWRATE_0_45_V_NS;
	pGpioConfig->PullUpDown = SMS_GPIO_PULLUPDOWN_NONE;
}

int sms_board_event(struct smscore_device_t *coredev,
		enum SMS_BOARD_EVENTS gevent) {
	int board_id = smscore_get_board_id(coredev);
	struct sms_board *board = sms_get_board(board_id);
	struct smscore_gpio_config MyGpioConfig;

	sms_gpio_assign_11xx_default_led_config(&MyGpioConfig);

	switch (gevent) {
	case BOARD_EVENT_POWER_INIT: /* including hotplug */
		switch (board_id) {
		case SMS1XXX_BOARD_HAUPPAUGE_WINDHAM:
			/* set I/O and turn off all LEDs */
			smscore_gpio_configure(coredev,
					board->board_cfg.leds_power,
					&MyGpioConfig);
			smscore_gpio_set_level(coredev,
					board->board_cfg.leds_power, 0);
			smscore_gpio_configure(coredev, board->board_cfg.led0,
					&MyGpioConfig);
			smscore_gpio_set_level(coredev,
					board->board_cfg.led0, 0);
			smscore_gpio_configure(coredev, board->board_cfg.led1,
					&MyGpioConfig);
			smscore_gpio_set_level(coredev,
					board->board_cfg.led1, 0);
			break;
		case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD_R2:
		case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD:
			/* set I/O and turn off LNA */
			smscore_gpio_configure(coredev,
					board->board_cfg.foreign_lna0_ctrl,
					&MyGpioConfig);
			smscore_gpio_set_level(coredev,
					board->board_cfg.foreign_lna0_ctrl,
					0);
			break;
		}
		break; /* BOARD_EVENT_BIND */

	case BOARD_EVENT_POWER_SUSPEND:
		switch (board_id) {
		case SMS1XXX_BOARD_HAUPPAUGE_WINDHAM:
			smscore_gpio_set_level(coredev,
						board->board_cfg.leds_power, 0);
			smscore_gpio_set_level(coredev,
						board->board_cfg.led0, 0);
			smscore_gpio_set_level(coredev,
						board->board_cfg.led1, 0);
			break;
		case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD_R2:
		case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD:
			smscore_gpio_set_level(coredev,
					board->board_cfg.foreign_lna0_ctrl,
					0);
			break;
		}
		break; /* BOARD_EVENT_POWER_SUSPEND */

	case BOARD_EVENT_POWER_RESUME:
		switch (board_id) {
		case SMS1XXX_BOARD_HAUPPAUGE_WINDHAM:
			smscore_gpio_set_level(coredev,
						board->board_cfg.leds_power, 1);
			smscore_gpio_set_level(coredev,
						board->board_cfg.led0, 1);
			smscore_gpio_set_level(coredev,
						board->board_cfg.led1, 0);
			break;
		case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD_R2:
		case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD:
			smscore_gpio_set_level(coredev,
					board->board_cfg.foreign_lna0_ctrl,
					1);
			break;
		}
		break; /* BOARD_EVENT_POWER_RESUME */

	case BOARD_EVENT_BIND:
		switch (board_id) {
		case SMS1XXX_BOARD_HAUPPAUGE_WINDHAM:
			smscore_gpio_set_level(coredev,
				board->board_cfg.leds_power, 1);
			smscore_gpio_set_level(coredev,
				board->board_cfg.led0, 1);
			smscore_gpio_set_level(coredev,
				board->board_cfg.led1, 0);
			break;
		case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD_R2:
		case SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD:
			smscore_gpio_set_level(coredev,
					board->board_cfg.foreign_lna0_ctrl,
					1);
			break;
		}
		break; /* BOARD_EVENT_BIND */

	case BOARD_EVENT_SCAN_PROG:
		break; /* BOARD_EVENT_SCAN_PROG */
	case BOARD_EVENT_SCAN_COMP:
		break; /* BOARD_EVENT_SCAN_COMP */
	case BOARD_EVENT_EMERGENCY_WARNING_SIGNAL:
		break; /* BOARD_EVENT_EMERGENCY_WARNING_SIGNAL */
	case BOARD_EVENT_FE_LOCK:
		switch (board_id) {
		case SMS1XXX_BOARD_HAUPPAUGE_WINDHAM:
			smscore_gpio_set_level(coredev,
			board->board_cfg.led1, 1);
			break;
		}
		break; /* BOARD_EVENT_FE_LOCK */
	case BOARD_EVENT_FE_UNLOCK:
		switch (board_id) {
		case SMS1XXX_BOARD_HAUPPAUGE_WINDHAM:
			smscore_gpio_set_level(coredev,
						board->board_cfg.led1, 0);
			break;
		}
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
		switch (board_id) {
		case SMS1XXX_BOARD_HAUPPAUGE_WINDHAM:
			smscore_gpio_set_level(coredev,
						board->board_cfg.led1, 1);
			break;
		}
		break; /* BOARD_EVENT_MULTIPLEX_OK */
	case BOARD_EVENT_MULTIPLEX_ERRORS:
		switch (board_id) {
		case SMS1XXX_BOARD_HAUPPAUGE_WINDHAM:
			smscore_gpio_set_level(coredev,
						board->board_cfg.led1, 0);
			break;
		}
		break; /* BOARD_EVENT_MULTIPLEX_ERRORS */

	default:
		sms_err("Unknown SMS board event");
		break;
	}
	return 0;
}
