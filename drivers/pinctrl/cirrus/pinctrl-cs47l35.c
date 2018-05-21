// SPDX-License-Identifier: GPL-2.0
/*
 * Pinctrl for Cirrus Logic CS47L35
 *
 * Copyright (C) 2016-2017 Cirrus Logic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2.
 */

#include <linux/err.h>
#include <linux/mfd/madera/core.h>

#include "pinctrl-madera.h"

/*
 * The alt func groups are the most commonly used functions we place these at
 * the lower function indexes for convenience, and the less commonly used gpio
 * functions at higher indexes.
 *
 * To stay consistent with the datasheet the function names are the same as
 * the group names for that function's pins
 *
 * Note - all 1 less than in datasheet because these are zero-indexed
 */
static const unsigned int cs47l35_aif3_pins[] = { 0, 1, 2, 3 };
static const unsigned int cs47l35_spk_pins[] = { 4, 5 };
static const unsigned int cs47l35_aif1_pins[] = { 7, 8, 9, 10 };
static const unsigned int cs47l35_aif2_pins[] = { 11, 12, 13, 14 };
static const unsigned int cs47l35_mif1_pins[] = { 6, 15 };

static const struct madera_pin_groups cs47l35_pin_groups[] = {
	{ "aif1", cs47l35_aif1_pins, ARRAY_SIZE(cs47l35_aif1_pins) },
	{ "aif2", cs47l35_aif2_pins, ARRAY_SIZE(cs47l35_aif2_pins) },
	{ "aif3", cs47l35_aif3_pins, ARRAY_SIZE(cs47l35_aif3_pins) },
	{ "mif1", cs47l35_mif1_pins, ARRAY_SIZE(cs47l35_mif1_pins) },
	{ "pdmspk1", cs47l35_spk_pins, ARRAY_SIZE(cs47l35_spk_pins) },
};

const struct madera_pin_chip cs47l35_pin_chip = {
	.n_pins = CS47L35_NUM_GPIOS,
	.pin_groups = cs47l35_pin_groups,
	.n_pin_groups = ARRAY_SIZE(cs47l35_pin_groups),
};
