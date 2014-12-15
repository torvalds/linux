/*
 *  arch/arm/mach-meson/include/mach/voltage.h
 *
 *  Copyright (C) 2010 AMLOGIC, INC.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ARCH_ARM_MESON6_VOLTAGE_H
#define __ARCH_ARM_MESON6_VOLTAGE_H

struct meson_opp {
    unsigned int freq;  /* in kHz */
    unsigned int min_uV;
    unsigned int max_uV;
};

extern struct meson_opp meson_vcck_opp_table[];
extern const int meson_vcck_opp_table_size;

extern unsigned int meson_vcck_cur_max_freq(struct regulator *reg,
                                            struct meson_opp *table, int tablesize);
extern int meson_vcck_scale(struct regulator *reg, struct meson_opp *table,
                            int tablesize, unsigned int frequency);

#endif //__ARCH_ARM_MESON6_VOLTAGE_H
