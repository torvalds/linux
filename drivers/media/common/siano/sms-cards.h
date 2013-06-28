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

#ifndef __SMS_CARDS_H__
#define __SMS_CARDS_H__

#include <linux/usb.h>
#include "smscoreapi.h"
#include "smsir.h"

#define SMS_BOARD_UNKNOWN 0
#define SMS1XXX_BOARD_SIANO_STELLAR 1
#define SMS1XXX_BOARD_SIANO_NOVA_A  2
#define SMS1XXX_BOARD_SIANO_NOVA_B  3
#define SMS1XXX_BOARD_SIANO_VEGA    4
#define SMS1XXX_BOARD_HAUPPAUGE_CATAMOUNT 5
#define SMS1XXX_BOARD_HAUPPAUGE_OKEMO_A 6
#define SMS1XXX_BOARD_HAUPPAUGE_OKEMO_B 7
#define SMS1XXX_BOARD_HAUPPAUGE_WINDHAM 8
#define SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD 9
#define SMS1XXX_BOARD_HAUPPAUGE_TIGER_MINICARD_R2 10
#define SMS1XXX_BOARD_SIANO_NICE	11
#define SMS1XXX_BOARD_SIANO_VENICE	12
#define SMS1XXX_BOARD_SIANO_STELLAR_ROM 13
#define SMS1XXX_BOARD_ZTE_DVB_DATA_CARD	14
#define SMS1XXX_BOARD_ONDA_MDTV_DATA_CARD 15
#define SMS1XXX_BOARD_SIANO_MING	16
#define SMS1XXX_BOARD_SIANO_PELE	17
#define SMS1XXX_BOARD_SIANO_RIO		18
#define SMS1XXX_BOARD_SIANO_DENVER_1530	19
#define SMS1XXX_BOARD_SIANO_DENVER_2160 20

struct sms_board_gpio_cfg {
	int lna_vhf_exist;
	int lna_vhf_ctrl;
	int lna_uhf_exist;
	int lna_uhf_ctrl;
	int lna_uhf_d_ctrl;
	int lna_sband_exist;
	int lna_sband_ctrl;
	int lna_sband_d_ctrl;
	int foreign_lna0_ctrl;
	int foreign_lna1_ctrl;
	int foreign_lna2_ctrl;
	int rf_switch_vhf;
	int rf_switch_uhf;
	int rf_switch_sband;
	int leds_power;
	int led0;
	int led1;
	int led2;
	int led3;
	int led4;
	int ir;
	int eeprom_wp;
	int mrc_sense;
	int mrc_pdn_resetn;
	int mrc_gp0; /* mrcs spi int */
	int mrc_gp1;
	int mrc_gp2;
	int mrc_gp3;
	int mrc_gp4;
	int host_spi_gsp_ts_int;
};

struct sms_board {
	enum sms_device_type_st type;
	char *name, *fw[DEVICE_MODE_MAX];
	struct sms_board_gpio_cfg board_cfg;
	char *rc_codes;				/* Name of IR codes table */

	/* gpios */
	int led_power, led_hi, led_lo, lna_ctrl, rf_switch;

	char intf_num;
	int default_mode;
	unsigned int mtu;
	unsigned int crystal;
	struct sms_antenna_config_ST *antenna_config;
};

struct sms_board *sms_get_board(unsigned id);

extern struct smscore_device_t *coredev;

enum SMS_BOARD_EVENTS {
	BOARD_EVENT_POWER_INIT,
	BOARD_EVENT_POWER_SUSPEND,
	BOARD_EVENT_POWER_RESUME,
	BOARD_EVENT_BIND,
	BOARD_EVENT_SCAN_PROG,
	BOARD_EVENT_SCAN_COMP,
	BOARD_EVENT_EMERGENCY_WARNING_SIGNAL,
	BOARD_EVENT_FE_LOCK,
	BOARD_EVENT_FE_UNLOCK,
	BOARD_EVENT_DEMOD_LOCK,
	BOARD_EVENT_DEMOD_UNLOCK,
	BOARD_EVENT_RECEPTION_MAX_4,
	BOARD_EVENT_RECEPTION_3,
	BOARD_EVENT_RECEPTION_2,
	BOARD_EVENT_RECEPTION_1,
	BOARD_EVENT_RECEPTION_LOST_0,
	BOARD_EVENT_MULTIPLEX_OK,
	BOARD_EVENT_MULTIPLEX_ERRORS
};

int sms_board_event(struct smscore_device_t *coredev,
		enum SMS_BOARD_EVENTS gevent);

int sms_board_setup(struct smscore_device_t *coredev);

#define SMS_LED_OFF 0
#define SMS_LED_LO  1
#define SMS_LED_HI  2
int sms_board_led_feedback(struct smscore_device_t *coredev, int led);
int sms_board_power(struct smscore_device_t *coredev, int onoff);
int sms_board_lna_control(struct smscore_device_t *coredev, int onoff);

extern int sms_board_load_modules(int id);

#endif /* __SMS_CARDS_H__ */
