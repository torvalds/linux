/*
 * vlv2_plat_clock.h
 *
 * Copyright (C) 2013 Intel Corp
 * Author: Asutosh Pathak <asutosh.pathak@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

#ifndef __VLV2_PLAT_CLOCK_H
#define __VLV2_PLAT_CLOCK_H

int vlv2_plat_set_clock_freq(int clock_num, int freq_type);
int vlv2_plat_get_clock_freq(int clock_num);

int vlv2_plat_configure_clock(int clock_num, u32 conf);
int vlv2_plat_get_clock_status(int clock_num);

#endif /* __VLV2_PLAT_CLOCK_H */
