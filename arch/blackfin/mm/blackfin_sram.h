/*
 * File:         arch/blackfin/mm/blackfin_sram.h
 * Based on:     arch/blackfin/mm/blackfin_sram.c
 * Author:       Mike Frysinger
 *
 * Created:      Aug 2006
 * Description:  Local prototypes meant for internal use only
 *
 * Modified:
 *               Copyright 2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __BLACKFIN_SRAM_H__
#define __BLACKFIN_SRAM_H__

extern void *l1sram_alloc(size_t);

#endif
