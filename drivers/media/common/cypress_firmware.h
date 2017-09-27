/*
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@posteo.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for downloading the firmware to Cypress FX 1
 * and 2 based devices.
 *
 */

#ifndef CYPRESS_FIRMWARE_H
#define CYPRESS_FIRMWARE_H

#define CYPRESS_AN2135  0
#define CYPRESS_AN2235  1
#define CYPRESS_FX2     2

/* commonly used firmware download types and function */
struct hexline {
	u8 len;
	u32 addr;
	u8 type;
	u8 data[255];
	u8 chk;
};

int cypress_load_firmware(struct usb_device *, const struct firmware *, int);

#endif
