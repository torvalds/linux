/* DVB USB compliant Linux driver for the Friio USB2.0 ISDB-T receiver.
 *
 * Copyright (C) 2009 Akihiro Tsukada <tskd2@yahoo.co.jp>
 *
 * This module is based off the the gl861 and vp702x modules.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 2.
 *
 * see Documentation/media/dvb-drivers/dvb-usb.rst for more information
 */
#ifndef _DVB_USB_FRIIO_H_
#define _DVB_USB_FRIIO_H_

/**
 *      Friio Components
 *       USB hub:                                AU4254
 *         USB controller(+ TS dmx & streaming): GL861
 *         Frontend:                             comtech JDVBT-90502
 *             (tuner PLL:                       tua6034, I2C addr:(0xC0 >> 1))
 *             (OFDM demodulator:                TC90502, I2C addr:(0x30 >> 1))
 *         LED x3 (+LNB) control:                PIC 16F676
 *         EEPROM:                               24C08
 *
 *        (USB smart card reader:                AU9522)
 *
 */

#define DVB_USB_LOG_PREFIX "friio"
#include "dvb-usb.h"

extern int dvb_usb_friio_debug;
#define deb_info(args...) dprintk(dvb_usb_friio_debug, 0x01, args)
#define deb_xfer(args...) dprintk(dvb_usb_friio_debug, 0x02, args)
#define deb_rc(args...)   dprintk(dvb_usb_friio_debug, 0x04, args)
#define deb_fe(args...)   dprintk(dvb_usb_friio_debug, 0x08, args)

/* Vendor requests */
#define GL861_WRITE		0x40
#define GL861_READ		0xc0

/* command bytes */
#define GL861_REQ_I2C_WRITE	0x01
#define GL861_REQ_I2C_READ	0x02
/* For control msg with data argument */
/* Used for accessing the PLL on the secondary I2C bus of FE via GL861 */
#define GL861_REQ_I2C_DATA_CTRL_WRITE	0x03

#define GL861_ALTSETTING_COUNT	2
#define FRIIO_BULK_ALTSETTING	0
#define FRIIO_ISOC_ALTSETTING	1

/* LED & LNB control via PIC. */
/* basically, it's serial control with clock and strobe. */
/* write the below 4bit control data to the reg 0x00 at the I2C addr 0x00 */
/* when controlling the LEDs, 32bit(saturation, R, G, B) is sent on the bit3*/
#define FRIIO_CTL_LNB (1 << 0)
#define FRIIO_CTL_STROBE (1 << 1)
#define FRIIO_CTL_CLK (1 << 2)
#define FRIIO_CTL_LED (1 << 3)

/* Front End related */

#define FRIIO_DEMOD_ADDR  (0x30 >> 1)
#define FRIIO_PLL_ADDR  (0xC0 >> 1)

#define JDVBT90502_PLL_CLK	4000000
#define JDVBT90502_PLL_DIVIDER	28

#define JDVBT90502_2ND_I2C_REG 0xFE

/* byte index for pll i2c command data structure*/
/* see datasheet for tua6034 */
#define DEMOD_REDIRECT_REG 0
#define ADDRESS_BYTE       1
#define DIVIDER_BYTE1      2
#define DIVIDER_BYTE2      3
#define CONTROL_BYTE       4
#define BANDSWITCH_BYTE    5
#define AGC_CTRL_BYTE      5
#define PLL_CMD_LEN        6

/* bit masks for PLL STATUS response */
#define PLL_STATUS_POR_MODE   0x80 /* 1: Power on Reset (test) Mode */
#define PLL_STATUS_LOCKED     0x40 /* 1: locked */
#define PLL_STATUS_AGC_ACTIVE 0x08 /* 1:active */
#define PLL_STATUS_TESTMODE   0x07 /* digital output level (5 level) */
  /* 0.15Vcc step   0x00: < 0.15Vcc, ..., 0x04: >= 0.6Vcc (<= 1Vcc) */


struct jdvbt90502_config {
	u8 demod_address; /* i2c addr for demodulator IC */
	u8 pll_address;   /* PLL addr on the secondary i2c*/
};
extern struct jdvbt90502_config friio_fe_config;

extern struct dvb_frontend *jdvbt90502_attach(struct dvb_usb_device *d);
#endif
