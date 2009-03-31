/*
 * Atheros AR9170 USB driver
 *
 * Driver specific definitions
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2009, Christian Lamparter <chunkeey@web.de>
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
 * along with this program; see the file COPYING.  If not, see
 * http://www.gnu.org/licenses/.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *    Copyright (c) 2007-2008 Atheros Communications, Inc.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __USB_H
#define __USB_H

#include <linux/usb.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/leds.h>
#include <net/wireless.h>
#include <net/mac80211.h>
#include <linux/firmware.h>
#include "eeprom.h"
#include "hw.h"
#include "ar9170.h"

#define AR9170_NUM_RX_URBS			16

struct firmware;

struct ar9170_usb {
	struct ar9170 common;
	struct usb_device *udev;
	struct usb_interface *intf;

	struct usb_anchor rx_submitted;
	struct usb_anchor tx_submitted;

	spinlock_t cmdlock;
	struct completion cmd_wait;
	int readlen;
	u8 *readbuf;

	const struct firmware *init_values;
	const struct firmware *firmware;
};

#endif /* __USB_H */
