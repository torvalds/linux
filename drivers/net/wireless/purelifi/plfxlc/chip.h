/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 pureLiFi
 */

#ifndef PLFXLC_CHIP_H
#define PLFXLC_CHIP_H

#include <net/mac80211.h>

#include "usb.h"

enum unit_type {
	STA = 0,
	AP = 1,
};

enum {
	PLFXLC_RADIO_OFF = 0,
	PLFXLC_RADIO_ON = 1,
};

struct plfxlc_chip {
	struct plfxlc_usb usb;
	struct mutex mutex; /* lock to protect chip data */
	enum unit_type unit_type;
	u16 link_led;
	u8 beacon_set;
	u16 beacon_interval;
};

struct plfxlc_mc_hash {
	u32 low;
	u32 high;
};

#define plfxlc_chip_dev(chip) (&(chip)->usb.intf->dev)

void plfxlc_chip_init(struct plfxlc_chip *chip,
		      struct ieee80211_hw *hw,
		      struct usb_interface *intf);

void plfxlc_chip_release(struct plfxlc_chip *chip);

void plfxlc_chip_disable_rxtx(struct plfxlc_chip *chip);

int plfxlc_chip_init_hw(struct plfxlc_chip *chip);

int plfxlc_chip_enable_rxtx(struct plfxlc_chip *chip);

int plfxlc_chip_set_rate(struct plfxlc_chip *chip, u8 rate);

int plfxlc_set_beacon_interval(struct plfxlc_chip *chip, u16 interval,
			       u8 dtim_period, int type);

int plfxlc_chip_switch_radio(struct plfxlc_chip *chip, u16 value);

static inline struct plfxlc_chip *plfxlc_usb_to_chip(struct plfxlc_usb
							 *usb)
{
	return container_of(usb, struct plfxlc_chip, usb);
}

static inline void plfxlc_mc_add_all(struct plfxlc_mc_hash *hash)
{
	hash->low  = 0xffffffff;
	hash->high = 0xffffffff;
}

#endif /* PLFXLC_CHIP_H */
