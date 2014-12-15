/*
 * arch/arm/mach-meson6/voltage.c
 *
 * Copyright (C) 2011-2012 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/regulator/machine.h>

#include "voltage.h"

struct meson_opp meson_vcck_opp_table[] = {
    /* freq must be in descending order */
    {
        .freq   = 1500000,
        .min_uV = 1209000,
        .max_uV = 1209000,
    },
    {
        .freq   = 1320000,
        .min_uV = 1209000,
        .max_uV = 1209000,
    },
    {
        .freq   = 1200000,
        .min_uV = 1192000,
        .max_uV = 1192000,
    },
	/* {
        .freq   = 1000000,
        .min_uV = 1143000,
        .max_uV = 1143000,
    },*/
    {
        .freq   = 1080000,
        .min_uV = 1110000,
        .max_uV = 1110000,
    },
    {
        .freq   = 840000,
        .min_uV = 1012000,
        .max_uV = 1012000,
    },
    {
        .freq   = 600000,
        .min_uV = 996000,
        .max_uV = 996000,
    },
    {
        .freq   = 200000,
        .min_uV = 979000,
        .max_uV = 979000,
    }
};

const int meson_vcck_opp_table_size = ARRAY_SIZE(meson_vcck_opp_table);

unsigned int meson_vcck_cur_max_freq(struct regulator *reg,
                                     struct meson_opp *table, int tablesize)
{
    int i, v;
    if (!reg)
        return 0;
    v = regulator_get_voltage(reg);
    if (v <= 0)
        return 0;
    for (i = 0; i < tablesize; i++) {
        if (table[i].min_uV <= v &&
            table[i].max_uV >= v) {
            return table[i].freq;
        }
    }
    return 0;
}
EXPORT_SYMBOL(meson_vcck_cur_max_freq);

int meson_vcck_scale(struct regulator *reg, struct meson_opp *table,
                     int tablesize, unsigned int frequency)
{
    int i, optimal;
    struct meson_opp *opp;
    if (!reg)
        return -ENODEV;

    optimal = 0;
    for (i = 0; i < tablesize; i++) {
        if (table[i].freq >= frequency)
            optimal = i;
    }
    opp = &table[optimal];

    pr_devel("vcck_set_voltage setting {%u,%u} for %u\n", opp->min_uV, opp->freq, frequency);

    return regulator_set_voltage(reg, opp->min_uV, opp->max_uV);
}
EXPORT_SYMBOL(meson_vcck_scale);

