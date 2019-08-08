/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * DVB USB Linux driver for Alcor Micro AU6610 DVB-T USB2.0.
 *
 * Copyright (C) 2006 Antti Palosaari <crope@iki.fi>
 */

#ifndef AU6610_H
#define AU6610_H
#include "dvb_usb.h"

#define AU6610_REQ_I2C_WRITE	0x14
#define AU6610_REQ_I2C_READ	0x13
#define AU6610_REQ_USB_WRITE	0x16
#define AU6610_REQ_USB_READ	0x15

#define AU6610_USB_TIMEOUT 1000

#endif
