/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * Author: Bengt Jonsson <bengt.g.jonsson@stericsson.com> for ST-Ericsson
 *
 * MOP500 board specific initialization for regulators
 */

#ifndef __BOARD_MOP500_REGULATORS_H
#define __BOARD_MOP500_REGULATORS_H

#include <linux/regulator/machine.h>
#include <linux/regulator/ab8500.h>

extern struct ab8500_regulator_reg_init
ab8500_regulator_reg_init[AB8500_NUM_REGULATOR_REGISTERS];
extern struct regulator_init_data ab8500_regulators[AB8500_NUM_REGULATORS];
extern struct regulator_init_data tps61052_regulator;

#endif
