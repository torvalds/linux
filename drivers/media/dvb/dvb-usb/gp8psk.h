/* DVB USB compliant Linux driver for the
 *  - GENPIX 8pks/qpsk/DCII USB2.0 DVB-S module
 *
 * Copyright (C) 2006 Alan Nisota (alannisota@gmail.com)
 * Copyright (C) 2006,2007 Alan Nisota (alannisota@gmail.com)
 *
 * Thanks to GENPIX for the sample code used to implement this module.
 *
 * This module is based off the vp7045 and vp702x modules
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#ifndef _DVB_USB_GP8PSK_H_
#define _DVB_USB_GP8PSK_H_

#define DVB_USB_LOG_PREFIX "gp8psk"
#include "dvb-usb.h"

extern int dvb_usb_gp8psk_debug;
#define deb_info(args...) dprintk(dvb_usb_gp8psk_debug,0x01,args)
#define deb_xfer(args...) dprintk(dvb_usb_gp8psk_debug,0x02,args)
#define deb_rc(args...)   dprintk(dvb_usb_gp8psk_debug,0x04,args)
#define deb_fe(args...)   dprintk(dvb_usb_gp8psk_debug,0x08,args)
/* gp8psk commands */

/* Twinhan Vendor requests */
#define TH_COMMAND_IN                     0xC0
#define TH_COMMAND_OUT                    0xC1

/* gp8psk commands */

#define GET_8PSK_CONFIG                 0x80    /* in */
#define SET_8PSK_CONFIG                 0x81
#define I2C_WRITE			0x83
#define I2C_READ			0x84
#define ARM_TRANSFER                    0x85
#define TUNE_8PSK                       0x86
#define GET_SIGNAL_STRENGTH             0x87    /* in */
#define LOAD_BCM4500                    0x88
#define BOOT_8PSK                       0x89    /* in */
#define START_INTERSIL                  0x8A    /* in */
#define SET_LNB_VOLTAGE                 0x8B
#define SET_22KHZ_TONE                  0x8C
#define SEND_DISEQC_COMMAND             0x8D
#define SET_DVB_MODE                    0x8E
#define SET_DN_SWITCH                   0x8F
#define GET_SIGNAL_LOCK                 0x90    /* in */
#define GET_SERIAL_NUMBER               0x93    /* in */
#define USE_EXTRA_VOLT                  0x94
#define CW3K_INIT			0x9d

/* PSK_configuration bits */
#define bm8pskStarted                   0x01
#define bm8pskFW_Loaded                 0x02
#define bmIntersilOn                    0x04
#define bmDVBmode                       0x08
#define bm22kHz                         0x10
#define bmSEL18V                        0x20
#define bmDCtuned                       0x40
#define bmArmed                         0x80

/* Satellite modulation modes */
#define ADV_MOD_DVB_QPSK 0     /* DVB-S QPSK */
#define ADV_MOD_TURBO_QPSK 1   /* Turbo QPSK */
#define ADV_MOD_TURBO_8PSK 2   /* Turbo 8PSK (also used for Trellis 8PSK) */
#define ADV_MOD_TURBO_16QAM 3  /* Turbo 16QAM (also used for Trellis 8PSK) */

#define ADV_MOD_DCII_C_QPSK 4  /* Digicipher II Combo */
#define ADV_MOD_DCII_I_QPSK 5  /* Digicipher II I-stream */
#define ADV_MOD_DCII_Q_QPSK 6  /* Digicipher II Q-stream */
#define ADV_MOD_DCII_C_OQPSK 7 /* Digicipher II offset QPSK */
#define ADV_MOD_DSS_QPSK 8     /* DSS (DIRECTV) QPSK */
#define ADV_MOD_DVB_BPSK 9     /* DVB-S BPSK */

#define GET_USB_SPEED                     0x07
 #define USB_SPEED_LOW                    0
 #define USB_SPEED_FULL                   1
 #define USB_SPEED_HIGH                   2

#define RESET_FX2                         0x13

#define FW_VERSION_READ                   0x0B
#define VENDOR_STRING_READ                0x0C
#define PRODUCT_STRING_READ               0x0D
#define FW_BCD_VERSION_READ               0x14

extern struct dvb_frontend * gp8psk_fe_attach(struct dvb_usb_device *d);
extern int gp8psk_usb_in_op(struct dvb_usb_device *d, u8 req, u16 value, u16 index, u8 *b, int blen);
extern int gp8psk_usb_out_op(struct dvb_usb_device *d, u8 req, u16 value,
			     u16 index, u8 *b, int blen);

#endif
