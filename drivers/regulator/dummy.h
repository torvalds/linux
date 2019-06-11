/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * dummy.h
 *
 * Copyright 2010 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This is useful for systems with mixed controllable and
 * non-controllable regulators, as well as for allowing testing on
 * systems with no controllable regulators.
 */

#ifndef _DUMMY_H
#define _DUMMY_H

struct regulator_dev;

extern struct regulator_dev *dummy_regulator_rdev;

void __init regulator_dummy_init(void);

#endif
