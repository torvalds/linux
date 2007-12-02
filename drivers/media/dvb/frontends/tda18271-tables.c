/*
    tda18271-tables.c - driver for the Philips / NXP TDA18271 silicon tuner

    Copyright (C) 2007 Michael Krufky (mkrufky@linuxtv.org)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "tda18271-priv.h"

struct tda18271_pll_map tda18271_main_pll[] = {
	{ .lomax =  32000, .pd = 0x5f, .d = 0xf0 },
	{ .lomax =  35000, .pd = 0x5e, .d = 0xe0 },
	{ .lomax =  37000, .pd = 0x5d, .d = 0xd0 },
	{ .lomax =  41000, .pd = 0x5c, .d = 0xc0 },
	{ .lomax =  44000, .pd = 0x5b, .d = 0xb0 },
	{ .lomax =  49000, .pd = 0x5a, .d = 0xa0 },
	{ .lomax =  54000, .pd = 0x59, .d = 0x90 },
	{ .lomax =  61000, .pd = 0x58, .d = 0x80 },
	{ .lomax =  65000, .pd = 0x4f, .d = 0x78 },
	{ .lomax =  70000, .pd = 0x4e, .d = 0x70 },
	{ .lomax =  75000, .pd = 0x4d, .d = 0x68 },
	{ .lomax =  82000, .pd = 0x4c, .d = 0x60 },
	{ .lomax =  89000, .pd = 0x4b, .d = 0x58 },
	{ .lomax =  98000, .pd = 0x4a, .d = 0x50 },
	{ .lomax = 109000, .pd = 0x49, .d = 0x48 },
	{ .lomax = 123000, .pd = 0x48, .d = 0x40 },
	{ .lomax = 131000, .pd = 0x3f, .d = 0x3c },
	{ .lomax = 141000, .pd = 0x3e, .d = 0x38 },
	{ .lomax = 151000, .pd = 0x3d, .d = 0x34 },
	{ .lomax = 164000, .pd = 0x3c, .d = 0x30 },
	{ .lomax = 179000, .pd = 0x3b, .d = 0x2c },
	{ .lomax = 197000, .pd = 0x3a, .d = 0x28 },
	{ .lomax = 219000, .pd = 0x39, .d = 0x24 },
	{ .lomax = 246000, .pd = 0x38, .d = 0x20 },
	{ .lomax = 263000, .pd = 0x2f, .d = 0x1e },
	{ .lomax = 282000, .pd = 0x2e, .d = 0x1c },
	{ .lomax = 303000, .pd = 0x2d, .d = 0x1a },
	{ .lomax = 329000, .pd = 0x2c, .d = 0x18 },
	{ .lomax = 359000, .pd = 0x2b, .d = 0x16 },
	{ .lomax = 395000, .pd = 0x2a, .d = 0x14 },
	{ .lomax = 438000, .pd = 0x29, .d = 0x12 },
	{ .lomax = 493000, .pd = 0x28, .d = 0x10 },
	{ .lomax = 526000, .pd = 0x1f, .d = 0x0f },
	{ .lomax = 564000, .pd = 0x1e, .d = 0x0e },
	{ .lomax = 607000, .pd = 0x1d, .d = 0x0d },
	{ .lomax = 658000, .pd = 0x1c, .d = 0x0c },
	{ .lomax = 718000, .pd = 0x1b, .d = 0x0b },
	{ .lomax = 790000, .pd = 0x1a, .d = 0x0a },
	{ .lomax = 877000, .pd = 0x19, .d = 0x09 },
	{ .lomax = 987000, .pd = 0x18, .d = 0x08 },
	{ .lomax =      0, .pd = 0x00, .d = 0x00 }, /* end */
};

struct tda18271_pll_map tda18271_cal_pll[] = {
	{ .lomax =   33000, .pd = 0xdd, .d = 0xd0 },
	{ .lomax =   36000, .pd = 0xdc, .d = 0xc0 },
	{ .lomax =   40000, .pd = 0xdb, .d = 0xb0 },
	{ .lomax =   44000, .pd = 0xda, .d = 0xa0 },
	{ .lomax =   49000, .pd = 0xd9, .d = 0x90 },
	{ .lomax =   55000, .pd = 0xd8, .d = 0x80 },
	{ .lomax =   63000, .pd = 0xd3, .d = 0x70 },
	{ .lomax =   67000, .pd = 0xcd, .d = 0x68 },
	{ .lomax =   73000, .pd = 0xcc, .d = 0x60 },
	{ .lomax =   80000, .pd = 0xcb, .d = 0x58 },
	{ .lomax =   88000, .pd = 0xca, .d = 0x50 },
	{ .lomax =   98000, .pd = 0xc9, .d = 0x48 },
	{ .lomax =  110000, .pd = 0xc8, .d = 0x40 },
	{ .lomax =  126000, .pd = 0xc3, .d = 0x38 },
	{ .lomax =  135000, .pd = 0xbd, .d = 0x34 },
	{ .lomax =  147000, .pd = 0xbc, .d = 0x30 },
	{ .lomax =  160000, .pd = 0xbb, .d = 0x2c },
	{ .lomax =  176000, .pd = 0xba, .d = 0x28 },
	{ .lomax =  196000, .pd = 0xb9, .d = 0x24 },
	{ .lomax =  220000, .pd = 0xb8, .d = 0x20 },
	{ .lomax =  252000, .pd = 0xb3, .d = 0x1c },
	{ .lomax =  271000, .pd = 0xad, .d = 0x1a },
	{ .lomax =  294000, .pd = 0xac, .d = 0x18 },
	{ .lomax =  321000, .pd = 0xab, .d = 0x16 },
	{ .lomax =  353000, .pd = 0xaa, .d = 0x14 },
	{ .lomax =  392000, .pd = 0xa9, .d = 0x12 },
	{ .lomax =  441000, .pd = 0xa8, .d = 0x10 },
	{ .lomax =  505000, .pd = 0xa3, .d = 0x0e },
	{ .lomax =  543000, .pd = 0x9d, .d = 0x0d },
	{ .lomax =  589000, .pd = 0x9c, .d = 0x0c },
	{ .lomax =  642000, .pd = 0x9b, .d = 0x0b },
	{ .lomax =  707000, .pd = 0x9a, .d = 0x0a },
	{ .lomax =  785000, .pd = 0x99, .d = 0x09 },
	{ .lomax =  883000, .pd = 0x98, .d = 0x08 },
	{ .lomax = 1010000, .pd = 0x93, .d = 0x07 },
	{ .lomax =       0, .pd = 0x00, .d = 0x00 }, /* end */
};

struct tda18271_map tda18271_bp_filter[] = {
	{ .rfmax =  62000, .val = 0x00 },
	{ .rfmax =  84000, .val = 0x01 },
	{ .rfmax = 100000, .val = 0x02 },
	{ .rfmax = 140000, .val = 0x03 },
	{ .rfmax = 170000, .val = 0x04 },
	{ .rfmax = 180000, .val = 0x05 },
	{ .rfmax = 865000, .val = 0x06 },
	{ .rfmax =      0, .val = 0x00 }, /* end */
};

struct tda18271_map tda18271_km[] = {
	{ .rfmax =  61100, .val = 0x74 },
	{ .rfmax = 350000, .val = 0x40 },
	{ .rfmax = 720000, .val = 0x30 },
	{ .rfmax = 865000, .val = 0x40 },
	{ .rfmax =      0, .val = 0x00 }, /* end */
};

struct tda18271_map tda18271_rf_band[] = {
	{ .rfmax =  47900, .val = 0x00 },
	{ .rfmax =  61100, .val = 0x01 },
/*	{ .rfmax = 152600, .val = 0x02 }, */
	{ .rfmax = 121200, .val = 0x02 },
	{ .rfmax = 164700, .val = 0x03 },
	{ .rfmax = 203500, .val = 0x04 },
	{ .rfmax = 457800, .val = 0x05 },
	{ .rfmax = 865000, .val = 0x06 },
	{ .rfmax =      0, .val = 0x00 }, /* end */
};

struct tda18271_map tda18271_gain_taper[] = {
	{ .rfmax =  45400, .val = 0x1f },
	{ .rfmax =  45800, .val = 0x1e },
	{ .rfmax =  46200, .val = 0x1d },
	{ .rfmax =  46700, .val = 0x1c },
	{ .rfmax =  47100, .val = 0x1b },
	{ .rfmax =  47500, .val = 0x1a },
	{ .rfmax =  47900, .val = 0x19 },
	{ .rfmax =  49600, .val = 0x17 },
	{ .rfmax =  51200, .val = 0x16 },
	{ .rfmax =  52900, .val = 0x15 },
	{ .rfmax =  54500, .val = 0x14 },
	{ .rfmax =  56200, .val = 0x13 },
	{ .rfmax =  57800, .val = 0x12 },
	{ .rfmax =  59500, .val = 0x11 },
	{ .rfmax =  61100, .val = 0x10 },
	{ .rfmax =  67600, .val = 0x0d },
	{ .rfmax =  74200, .val = 0x0c },
	{ .rfmax =  80700, .val = 0x0b },
	{ .rfmax =  87200, .val = 0x0a },
	{ .rfmax =  93800, .val = 0x09 },
	{ .rfmax = 100300, .val = 0x08 },
	{ .rfmax = 106900, .val = 0x07 },
	{ .rfmax = 113400, .val = 0x06 },
	{ .rfmax = 119900, .val = 0x05 },
	{ .rfmax = 126500, .val = 0x04 },
	{ .rfmax = 133000, .val = 0x03 },
	{ .rfmax = 139500, .val = 0x02 },
	{ .rfmax = 146100, .val = 0x01 },
	{ .rfmax = 152600, .val = 0x00 },
	{ .rfmax = 154300, .val = 0x1f },
	{ .rfmax = 156100, .val = 0x1e },
	{ .rfmax = 157800, .val = 0x1d },
	{ .rfmax = 159500, .val = 0x1c },
	{ .rfmax = 161200, .val = 0x1b },
	{ .rfmax = 163000, .val = 0x1a },
	{ .rfmax = 164700, .val = 0x19 },
	{ .rfmax = 170200, .val = 0x17 },
	{ .rfmax = 175800, .val = 0x16 },
	{ .rfmax = 181300, .val = 0x15 },
	{ .rfmax = 186900, .val = 0x14 },
	{ .rfmax = 192400, .val = 0x13 },
	{ .rfmax = 198000, .val = 0x12 },
	{ .rfmax = 203500, .val = 0x11 },
	{ .rfmax = 216200, .val = 0x14 },
	{ .rfmax = 228900, .val = 0x13 },
	{ .rfmax = 241600, .val = 0x12 },
	{ .rfmax = 254400, .val = 0x11 },
	{ .rfmax = 267100, .val = 0x10 },
	{ .rfmax = 279800, .val = 0x0f },
	{ .rfmax = 292500, .val = 0x0e },
	{ .rfmax = 305200, .val = 0x0d },
	{ .rfmax = 317900, .val = 0x0c },
	{ .rfmax = 330700, .val = 0x0b },
	{ .rfmax = 343400, .val = 0x0a },
	{ .rfmax = 356100, .val = 0x09 },
	{ .rfmax = 368800, .val = 0x08 },
	{ .rfmax = 381500, .val = 0x07 },
	{ .rfmax = 394200, .val = 0x06 },
	{ .rfmax = 406900, .val = 0x05 },
	{ .rfmax = 419700, .val = 0x04 },
	{ .rfmax = 432400, .val = 0x03 },
	{ .rfmax = 445100, .val = 0x02 },
	{ .rfmax = 457800, .val = 0x01 },
	{ .rfmax = 476300, .val = 0x19 },
	{ .rfmax = 494800, .val = 0x18 },
	{ .rfmax = 513300, .val = 0x17 },
	{ .rfmax = 531800, .val = 0x16 },
	{ .rfmax = 550300, .val = 0x15 },
	{ .rfmax = 568900, .val = 0x14 },
	{ .rfmax = 587400, .val = 0x13 },
	{ .rfmax = 605900, .val = 0x12 },
	{ .rfmax = 624400, .val = 0x11 },
	{ .rfmax = 642900, .val = 0x10 },
	{ .rfmax = 661400, .val = 0x0f },
	{ .rfmax = 679900, .val = 0x0e },
	{ .rfmax = 698400, .val = 0x0d },
	{ .rfmax = 716900, .val = 0x0c },
	{ .rfmax = 735400, .val = 0x0b },
	{ .rfmax = 753900, .val = 0x0a },
	{ .rfmax = 772500, .val = 0x09 },
	{ .rfmax = 791000, .val = 0x08 },
	{ .rfmax = 809500, .val = 0x07 },
	{ .rfmax = 828000, .val = 0x06 },
	{ .rfmax = 846500, .val = 0x05 },
	{ .rfmax = 865000, .val = 0x04 },
	{ .rfmax =      0, .val = 0x00 }, /* end */
};

struct tda18271_map tda18271_rf_cal[] = {
	{ .rfmax = 41000, .val = 0x1e },
	{ .rfmax = 43000, .val = 0x30 },
	{ .rfmax = 45000, .val = 0x43 },
	{ .rfmax = 46000, .val = 0x4d },
	{ .rfmax = 47000, .val = 0x54 },
	{ .rfmax = 47900, .val = 0x64 },
	{ .rfmax = 49100, .val = 0x20 },
	{ .rfmax = 50000, .val = 0x22 },
	{ .rfmax = 51000, .val = 0x2a },
	{ .rfmax = 53000, .val = 0x32 },
	{ .rfmax = 55000, .val = 0x35 },
	{ .rfmax = 56000, .val = 0x3c },
	{ .rfmax = 57000, .val = 0x3f },
	{ .rfmax = 58000, .val = 0x48 },
	{ .rfmax = 59000, .val = 0x4d },
	{ .rfmax = 60000, .val = 0x58 },
	{ .rfmax = 61100, .val = 0x5f },
	{ .rfmax =     0, .val = 0x00 }, /* end */
};

struct tda18271_map tda18271_ir_measure[] = {
	{ .rfmax =  30000, .val = 4},
	{ .rfmax = 200000, .val = 5},
	{ .rfmax = 600000, .val = 6},
	{ .rfmax = 865000, .val = 7},
	{ .rfmax =      0, .val = 0}, /* end */
};

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
