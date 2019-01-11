// SPDX-License-Identifier: GPL-2.0
/*
 * Pinctrl for Cirrus Logic CS47L90
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
static const unsigned int cs47l90_mif1_pins[] = { 8, 9 };
static const unsigned int cs47l90_mif2_pins[] = { 10, 11 };
static const unsigned int cs47l90_mif3_pins[] = { 12, 13 };
static const unsigned int cs47l90_aif1_pins[] = { 14, 15, 16, 17 };
static const unsigned int cs47l90_aif2_pins[] = { 18, 19, 20, 21 };
static const unsigned int cs47l90_aif3_pins[] = { 22, 23, 24, 25 };
static const unsigned int cs47l90_aif4_pins[] = { 26, 27, 28, 29 };
static const unsigned int cs47l90_dmic4_pins[] = { 30, 31 };
static const unsigned int cs47l90_dmic5_pins[] = { 32, 33 };
static const unsigned int cs47l90_dmic3_pins[] = { 34, 35 };
static const unsigned int cs47l90_spk1_pins[] = { 36, 37 };

static const struct madera_pin_groups cs47l90_pin_groups[] = {
	{ "aif1", cs47l90_aif1_pins, ARRAY_SIZE(cs47l90_aif1_pins) },
	{ "aif2", cs47l90_aif2_pins, ARRAY_SIZE(cs47l90_aif2_pins) },
	{ "aif3", cs47l90_aif3_pins, ARRAY_SIZE(cs47l90_aif3_pins) },
	{ "aif4", cs47l90_aif4_pins, ARRAY_SIZE(cs47l90_aif4_pins) },
	{ "mif1", cs47l90_mif1_pins, ARRAY_SIZE(cs47l90_mif1_pins) },
	{ "mif2", cs47l90_mif2_pins, ARRAY_SIZE(cs47l90_mif2_pins) },
	{ "mif3", cs47l90_mif3_pins, ARRAY_SIZE(cs47l90_mif3_pins) },
	{ "dmic3", cs47l90_dmic3_pins, ARRAY_SIZE(cs47l90_dmic3_pins) },
	{ "dmic4", cs47l90_dmic4_pins, ARRAY_SIZE(cs47l90_dmic4_pins) },
	{ "dmic5", cs47l90_dmic5_pins, ARRAY_SIZE(cs47l90_dmic5_pins) },
	{ "pdmspk1", cs47l90_spk1_pins, ARRAY_SIZE(cs47l90_spk1_pins) },
};

const struct madera_pin_chip cs47l90_pin_chip = {
	.n_pins = CS47L90_NUM_GPIOS,
	.pin_groups = cs47l90_pin_groups,
	.n_pin_groups = ARRAY_SIZE(cs47l90_pin_groups),
};
