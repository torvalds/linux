/* Common header-file of the Linux driver for the TwinhanDTV Alpha/MagicBoxII
 * USB2.0 DVB-T receiver.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * Thanks to Twinhan who kindly provided hardware and information.
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#ifndef _DVB_USB_VP7045_H_
#define _DVB_USB_VP7045_H_

#define DVB_USB_LOG_PREFIX "vp7045"
#include "dvb-usb.h"

/* vp7045 commands */

/* Twinhan Vendor requests */
#define TH_COMMAND_IN                     0xC0
#define TH_COMMAND_OUT                    0xC1

/* command bytes */
#define TUNER_REG_READ                    0x03
#define TUNER_REG_WRITE                   0x04

#define RC_VAL_READ                       0x05
 #define RC_NO_KEY                        0x44

#define SET_TUNER_POWER                   0x06
#define CHECK_TUNER_POWER                 0x12
 #define Tuner_Power_ON                   1
 #define Tuner_Power_OFF                  0

#define GET_USB_SPEED                     0x07
 #define USB_SPEED_LOW                    0
 #define USB_SPEED_FULL                   1
 #define USB_SPEED_HIGH                   2

#define LOCK_TUNER_COMMAND                0x09

#define TUNER_SIGNAL_READ                 0x0A

/* FX2 eeprom */
#define SET_EE_VALUE                      0x10
#define GET_EE_VALUE                      0x11
 #define FX2_ID_ADDR                      0x00
 #define VID_MSB_ADDR                     0x02
 #define VID_LSB_ADDR                     0x01
 #define PID_MSB_ADDR                     0x04
 #define PID_LSB_ADDR                     0x03
 #define MAC_0_ADDR                       0x07
 #define MAC_1_ADDR                       0x08
 #define MAC_2_ADDR                       0x09
 #define MAC_3_ADDR                       0x0a
 #define MAC_4_ADDR                       0x0b
 #define MAC_5_ADDR                       0x0c

#define RESET_FX2                         0x13

#define FW_VERSION_READ                   0x0B
#define VENDOR_STRING_READ                0x0C
#define PRODUCT_STRING_READ               0x0D
#define FW_BCD_VERSION_READ               0x14

extern struct dvb_frontend * vp7045_fe_attach(struct dvb_usb_device *d);
extern int vp7045_usb_op(struct dvb_usb_device *d, u8 cmd, u8 *out, int outlen, u8 *in, int inlen,int msec);
extern u8 vp7045_read_reg(struct dvb_usb_device *d, u8 reg);

#endif
