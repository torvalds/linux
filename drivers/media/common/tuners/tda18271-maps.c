/*
    tda18271-maps.c - driver for the Philips / NXP TDA18271 silicon tuner

    Copyright (C) 2007, 2008 Michael Krufky <mkrufky@linuxtv.org>

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

struct tda18271_pll_map {
	u32 lomax;
	u8 pd; /* post div */
	u8 d;  /*      div */
};

struct tda18271_map {
	u32 rfmax;
	u8  val;
};

/*---------------------------------------------------------------------*/

static struct tda18271_pll_map tda18271c1_main_pll[] = {
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

static struct tda18271_pll_map tda18271c2_main_pll[] = {
	{ .lomax =  33125, .pd = 0x57, .d = 0xf0 },
	{ .lomax =  35500, .pd = 0x56, .d = 0xe0 },
	{ .lomax =  38188, .pd = 0x55, .d = 0xd0 },
	{ .lomax =  41375, .pd = 0x54, .d = 0xc0 },
	{ .lomax =  45125, .pd = 0x53, .d = 0xb0 },
	{ .lomax =  49688, .pd = 0x52, .d = 0xa0 },
	{ .lomax =  55188, .pd = 0x51, .d = 0x90 },
	{ .lomax =  62125, .pd = 0x50, .d = 0x80 },
	{ .lomax =  66250, .pd = 0x47, .d = 0x78 },
	{ .lomax =  71000, .pd = 0x46, .d = 0x70 },
	{ .lomax =  76375, .pd = 0x45, .d = 0x68 },
	{ .lomax =  82750, .pd = 0x44, .d = 0x60 },
	{ .lomax =  90250, .pd = 0x43, .d = 0x58 },
	{ .lomax =  99375, .pd = 0x42, .d = 0x50 },
	{ .lomax = 110375, .pd = 0x41, .d = 0x48 },
	{ .lomax = 124250, .pd = 0x40, .d = 0x40 },
	{ .lomax = 132500, .pd = 0x37, .d = 0x3c },
	{ .lomax = 142000, .pd = 0x36, .d = 0x38 },
	{ .lomax = 152750, .pd = 0x35, .d = 0x34 },
	{ .lomax = 165500, .pd = 0x34, .d = 0x30 },
	{ .lomax = 180500, .pd = 0x33, .d = 0x2c },
	{ .lomax = 198750, .pd = 0x32, .d = 0x28 },
	{ .lomax = 220750, .pd = 0x31, .d = 0x24 },
	{ .lomax = 248500, .pd = 0x30, .d = 0x20 },
	{ .lomax = 265000, .pd = 0x27, .d = 0x1e },
	{ .lomax = 284000, .pd = 0x26, .d = 0x1c },
	{ .lomax = 305500, .pd = 0x25, .d = 0x1a },
	{ .lomax = 331000, .pd = 0x24, .d = 0x18 },
	{ .lomax = 361000, .pd = 0x23, .d = 0x16 },
	{ .lomax = 397500, .pd = 0x22, .d = 0x14 },
	{ .lomax = 441500, .pd = 0x21, .d = 0x12 },
	{ .lomax = 497000, .pd = 0x20, .d = 0x10 },
	{ .lomax = 530000, .pd = 0x17, .d = 0x0f },
	{ .lomax = 568000, .pd = 0x16, .d = 0x0e },
	{ .lomax = 611000, .pd = 0x15, .d = 0x0d },
	{ .lomax = 662000, .pd = 0x14, .d = 0x0c },
	{ .lomax = 722000, .pd = 0x13, .d = 0x0b },
	{ .lomax = 795000, .pd = 0x12, .d = 0x0a },
	{ .lomax = 883000, .pd = 0x11, .d = 0x09 },
	{ .lomax = 994000, .pd = 0x10, .d = 0x08 },
	{ .lomax =      0, .pd = 0x00, .d = 0x00 }, /* end */
};

static struct tda18271_pll_map tda18271c1_cal_pll[] = {
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

static struct tda18271_pll_map tda18271c2_cal_pll[] = {
	{ .lomax =   33813, .pd = 0xdd, .d = 0xd0 },
	{ .lomax =   36625, .pd = 0xdc, .d = 0xc0 },
	{ .lomax =   39938, .pd = 0xdb, .d = 0xb0 },
	{ .lomax =   43938, .pd = 0xda, .d = 0xa0 },
	{ .lomax =   48813, .pd = 0xd9, .d = 0x90 },
	{ .lomax =   54938, .pd = 0xd8, .d = 0x80 },
	{ .lomax =   62813, .pd = 0xd3, .d = 0x70 },
	{ .lomax =   67625, .pd = 0xcd, .d = 0x68 },
	{ .lomax =   73250, .pd = 0xcc, .d = 0x60 },
	{ .lomax =   79875, .pd = 0xcb, .d = 0x58 },
	{ .lomax =   87875, .pd = 0xca, .d = 0x50 },
	{ .lomax =   97625, .pd = 0xc9, .d = 0x48 },
	{ .lomax =  109875, .pd = 0xc8, .d = 0x40 },
	{ .lomax =  125625, .pd = 0xc3, .d = 0x38 },
	{ .lomax =  135250, .pd = 0xbd, .d = 0x34 },
	{ .lomax =  146500, .pd = 0xbc, .d = 0x30 },
	{ .lomax =  159750, .pd = 0xbb, .d = 0x2c },
	{ .lomax =  175750, .pd = 0xba, .d = 0x28 },
	{ .lomax =  195250, .pd = 0xb9, .d = 0x24 },
	{ .lomax =  219750, .pd = 0xb8, .d = 0x20 },
	{ .lomax =  251250, .pd = 0xb3, .d = 0x1c },
	{ .lomax =  270500, .pd = 0xad, .d = 0x1a },
	{ .lomax =  293000, .pd = 0xac, .d = 0x18 },
	{ .lomax =  319500, .pd = 0xab, .d = 0x16 },
	{ .lomax =  351500, .pd = 0xaa, .d = 0x14 },
	{ .lomax =  390500, .pd = 0xa9, .d = 0x12 },
	{ .lomax =  439500, .pd = 0xa8, .d = 0x10 },
	{ .lomax =  502500, .pd = 0xa3, .d = 0x0e },
	{ .lomax =  541000, .pd = 0x9d, .d = 0x0d },
	{ .lomax =  586000, .pd = 0x9c, .d = 0x0c },
	{ .lomax =  639000, .pd = 0x9b, .d = 0x0b },
	{ .lomax =  703000, .pd = 0x9a, .d = 0x0a },
	{ .lomax =  781000, .pd = 0x99, .d = 0x09 },
	{ .lomax =  879000, .pd = 0x98, .d = 0x08 },
	{ .lomax =       0, .pd = 0x00, .d = 0x00 }, /* end */
};

static struct tda18271_map tda18271_bp_filter[] = {
	{ .rfmax =  62000, .val = 0x00 },
	{ .rfmax =  84000, .val = 0x01 },
	{ .rfmax = 100000, .val = 0x02 },
	{ .rfmax = 140000, .val = 0x03 },
	{ .rfmax = 170000, .val = 0x04 },
	{ .rfmax = 180000, .val = 0x05 },
	{ .rfmax = 865000, .val = 0x06 },
	{ .rfmax =      0, .val = 0x00 }, /* end */
};

static struct tda18271_map tda18271c1_km[] = {
	{ .rfmax =  61100, .val = 0x74 },
	{ .rfmax = 350000, .val = 0x40 },
	{ .rfmax = 720000, .val = 0x30 },
	{ .rfmax = 865000, .val = 0x40 },
	{ .rfmax =      0, .val = 0x00 }, /* end */
};

static struct tda18271_map tda18271c2_km[] = {
	{ .rfmax =  47900, .val = 0x38 },
	{ .rfmax =  61100, .val = 0x44 },
	{ .rfmax = 350000, .val = 0x30 },
	{ .rfmax = 720000, .val = 0x24 },
	{ .rfmax = 865000, .val = 0x3c },
	{ .rfmax =      0, .val = 0x00 }, /* end */
};

static struct tda18271_map tda18271_rf_band[] = {
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

static struct tda18271_map tda18271_gain_taper[] = {
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

static struct tda18271_map tda18271c1_rf_cal[] = {
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

static struct tda18271_map tda18271c2_rf_cal[] = {
	{ .rfmax =  41000, .val = 0x0f },
	{ .rfmax =  43000, .val = 0x1c },
	{ .rfmax =  45000, .val = 0x2f },
	{ .rfmax =  46000, .val = 0x39 },
	{ .rfmax =  47000, .val = 0x40 },
	{ .rfmax =  47900, .val = 0x50 },
	{ .rfmax =  49100, .val = 0x16 },
	{ .rfmax =  50000, .val = 0x18 },
	{ .rfmax =  51000, .val = 0x20 },
	{ .rfmax =  53000, .val = 0x28 },
	{ .rfmax =  55000, .val = 0x2b },
	{ .rfmax =  56000, .val = 0x32 },
	{ .rfmax =  57000, .val = 0x35 },
	{ .rfmax =  58000, .val = 0x3e },
	{ .rfmax =  59000, .val = 0x43 },
	{ .rfmax =  60000, .val = 0x4e },
	{ .rfmax =  61100, .val = 0x55 },
	{ .rfmax =  63000, .val = 0x0f },
	{ .rfmax =  64000, .val = 0x11 },
	{ .rfmax =  65000, .val = 0x12 },
	{ .rfmax =  66000, .val = 0x15 },
	{ .rfmax =  67000, .val = 0x16 },
	{ .rfmax =  68000, .val = 0x17 },
	{ .rfmax =  70000, .val = 0x19 },
	{ .rfmax =  71000, .val = 0x1c },
	{ .rfmax =  72000, .val = 0x1d },
	{ .rfmax =  73000, .val = 0x1f },
	{ .rfmax =  74000, .val = 0x20 },
	{ .rfmax =  75000, .val = 0x21 },
	{ .rfmax =  76000, .val = 0x24 },
	{ .rfmax =  77000, .val = 0x25 },
	{ .rfmax =  78000, .val = 0x27 },
	{ .rfmax =  80000, .val = 0x28 },
	{ .rfmax =  81000, .val = 0x29 },
	{ .rfmax =  82000, .val = 0x2d },
	{ .rfmax =  83000, .val = 0x2e },
	{ .rfmax =  84000, .val = 0x2f },
	{ .rfmax =  85000, .val = 0x31 },
	{ .rfmax =  86000, .val = 0x33 },
	{ .rfmax =  87000, .val = 0x34 },
	{ .rfmax =  88000, .val = 0x35 },
	{ .rfmax =  89000, .val = 0x37 },
	{ .rfmax =  90000, .val = 0x38 },
	{ .rfmax =  91000, .val = 0x39 },
	{ .rfmax =  93000, .val = 0x3c },
	{ .rfmax =  94000, .val = 0x3e },
	{ .rfmax =  95000, .val = 0x3f },
	{ .rfmax =  96000, .val = 0x40 },
	{ .rfmax =  97000, .val = 0x42 },
	{ .rfmax =  99000, .val = 0x45 },
	{ .rfmax = 100000, .val = 0x46 },
	{ .rfmax = 102000, .val = 0x48 },
	{ .rfmax = 103000, .val = 0x4a },
	{ .rfmax = 105000, .val = 0x4d },
	{ .rfmax = 106000, .val = 0x4e },
	{ .rfmax = 107000, .val = 0x50 },
	{ .rfmax = 108000, .val = 0x51 },
	{ .rfmax = 110000, .val = 0x54 },
	{ .rfmax = 111000, .val = 0x56 },
	{ .rfmax = 112000, .val = 0x57 },
	{ .rfmax = 113000, .val = 0x58 },
	{ .rfmax = 114000, .val = 0x59 },
	{ .rfmax = 115000, .val = 0x5c },
	{ .rfmax = 116000, .val = 0x5d },
	{ .rfmax = 117000, .val = 0x5f },
	{ .rfmax = 119000, .val = 0x60 },
	{ .rfmax = 120000, .val = 0x64 },
	{ .rfmax = 121000, .val = 0x65 },
	{ .rfmax = 122000, .val = 0x66 },
	{ .rfmax = 123000, .val = 0x68 },
	{ .rfmax = 124000, .val = 0x69 },
	{ .rfmax = 125000, .val = 0x6c },
	{ .rfmax = 126000, .val = 0x6d },
	{ .rfmax = 127000, .val = 0x6e },
	{ .rfmax = 128000, .val = 0x70 },
	{ .rfmax = 129000, .val = 0x71 },
	{ .rfmax = 130000, .val = 0x75 },
	{ .rfmax = 131000, .val = 0x77 },
	{ .rfmax = 132000, .val = 0x78 },
	{ .rfmax = 133000, .val = 0x7b },
	{ .rfmax = 134000, .val = 0x7e },
	{ .rfmax = 135000, .val = 0x81 },
	{ .rfmax = 136000, .val = 0x82 },
	{ .rfmax = 137000, .val = 0x87 },
	{ .rfmax = 138000, .val = 0x88 },
	{ .rfmax = 139000, .val = 0x8d },
	{ .rfmax = 140000, .val = 0x8e },
	{ .rfmax = 141000, .val = 0x91 },
	{ .rfmax = 142000, .val = 0x95 },
	{ .rfmax = 143000, .val = 0x9a },
	{ .rfmax = 144000, .val = 0x9d },
	{ .rfmax = 145000, .val = 0xa1 },
	{ .rfmax = 146000, .val = 0xa2 },
	{ .rfmax = 147000, .val = 0xa4 },
	{ .rfmax = 148000, .val = 0xa9 },
	{ .rfmax = 149000, .val = 0xae },
	{ .rfmax = 150000, .val = 0xb0 },
	{ .rfmax = 151000, .val = 0xb1 },
	{ .rfmax = 152000, .val = 0xb7 },
	{ .rfmax = 153000, .val = 0xbd },
	{ .rfmax = 154000, .val = 0x20 },
	{ .rfmax = 155000, .val = 0x22 },
	{ .rfmax = 156000, .val = 0x24 },
	{ .rfmax = 157000, .val = 0x25 },
	{ .rfmax = 158000, .val = 0x27 },
	{ .rfmax = 159000, .val = 0x29 },
	{ .rfmax = 160000, .val = 0x2c },
	{ .rfmax = 161000, .val = 0x2d },
	{ .rfmax = 163000, .val = 0x2e },
	{ .rfmax = 164000, .val = 0x2f },
	{ .rfmax = 165000, .val = 0x30 },
	{ .rfmax = 166000, .val = 0x11 },
	{ .rfmax = 167000, .val = 0x12 },
	{ .rfmax = 168000, .val = 0x13 },
	{ .rfmax = 169000, .val = 0x14 },
	{ .rfmax = 170000, .val = 0x15 },
	{ .rfmax = 172000, .val = 0x16 },
	{ .rfmax = 173000, .val = 0x17 },
	{ .rfmax = 174000, .val = 0x18 },
	{ .rfmax = 175000, .val = 0x1a },
	{ .rfmax = 176000, .val = 0x1b },
	{ .rfmax = 178000, .val = 0x1d },
	{ .rfmax = 179000, .val = 0x1e },
	{ .rfmax = 180000, .val = 0x1f },
	{ .rfmax = 181000, .val = 0x20 },
	{ .rfmax = 182000, .val = 0x21 },
	{ .rfmax = 183000, .val = 0x22 },
	{ .rfmax = 184000, .val = 0x24 },
	{ .rfmax = 185000, .val = 0x25 },
	{ .rfmax = 186000, .val = 0x26 },
	{ .rfmax = 187000, .val = 0x27 },
	{ .rfmax = 188000, .val = 0x29 },
	{ .rfmax = 189000, .val = 0x2a },
	{ .rfmax = 190000, .val = 0x2c },
	{ .rfmax = 191000, .val = 0x2d },
	{ .rfmax = 192000, .val = 0x2e },
	{ .rfmax = 193000, .val = 0x2f },
	{ .rfmax = 194000, .val = 0x30 },
	{ .rfmax = 195000, .val = 0x33 },
	{ .rfmax = 196000, .val = 0x35 },
	{ .rfmax = 198000, .val = 0x36 },
	{ .rfmax = 200000, .val = 0x38 },
	{ .rfmax = 201000, .val = 0x3c },
	{ .rfmax = 202000, .val = 0x3d },
	{ .rfmax = 203500, .val = 0x3e },
	{ .rfmax = 206000, .val = 0x0e },
	{ .rfmax = 208000, .val = 0x0f },
	{ .rfmax = 212000, .val = 0x10 },
	{ .rfmax = 216000, .val = 0x11 },
	{ .rfmax = 217000, .val = 0x12 },
	{ .rfmax = 218000, .val = 0x13 },
	{ .rfmax = 220000, .val = 0x14 },
	{ .rfmax = 222000, .val = 0x15 },
	{ .rfmax = 225000, .val = 0x16 },
	{ .rfmax = 228000, .val = 0x17 },
	{ .rfmax = 231000, .val = 0x18 },
	{ .rfmax = 234000, .val = 0x19 },
	{ .rfmax = 235000, .val = 0x1a },
	{ .rfmax = 236000, .val = 0x1b },
	{ .rfmax = 237000, .val = 0x1c },
	{ .rfmax = 240000, .val = 0x1d },
	{ .rfmax = 242000, .val = 0x1f },
	{ .rfmax = 247000, .val = 0x20 },
	{ .rfmax = 249000, .val = 0x21 },
	{ .rfmax = 252000, .val = 0x22 },
	{ .rfmax = 253000, .val = 0x23 },
	{ .rfmax = 254000, .val = 0x24 },
	{ .rfmax = 256000, .val = 0x25 },
	{ .rfmax = 259000, .val = 0x26 },
	{ .rfmax = 262000, .val = 0x27 },
	{ .rfmax = 264000, .val = 0x28 },
	{ .rfmax = 267000, .val = 0x29 },
	{ .rfmax = 269000, .val = 0x2a },
	{ .rfmax = 271000, .val = 0x2b },
	{ .rfmax = 273000, .val = 0x2c },
	{ .rfmax = 275000, .val = 0x2d },
	{ .rfmax = 277000, .val = 0x2e },
	{ .rfmax = 279000, .val = 0x2f },
	{ .rfmax = 282000, .val = 0x30 },
	{ .rfmax = 284000, .val = 0x31 },
	{ .rfmax = 286000, .val = 0x32 },
	{ .rfmax = 287000, .val = 0x33 },
	{ .rfmax = 290000, .val = 0x34 },
	{ .rfmax = 293000, .val = 0x35 },
	{ .rfmax = 295000, .val = 0x36 },
	{ .rfmax = 297000, .val = 0x37 },
	{ .rfmax = 300000, .val = 0x38 },
	{ .rfmax = 303000, .val = 0x39 },
	{ .rfmax = 305000, .val = 0x3a },
	{ .rfmax = 306000, .val = 0x3b },
	{ .rfmax = 307000, .val = 0x3c },
	{ .rfmax = 310000, .val = 0x3d },
	{ .rfmax = 312000, .val = 0x3e },
	{ .rfmax = 315000, .val = 0x3f },
	{ .rfmax = 318000, .val = 0x40 },
	{ .rfmax = 320000, .val = 0x41 },
	{ .rfmax = 323000, .val = 0x42 },
	{ .rfmax = 324000, .val = 0x43 },
	{ .rfmax = 325000, .val = 0x44 },
	{ .rfmax = 327000, .val = 0x45 },
	{ .rfmax = 331000, .val = 0x46 },
	{ .rfmax = 334000, .val = 0x47 },
	{ .rfmax = 337000, .val = 0x48 },
	{ .rfmax = 339000, .val = 0x49 },
	{ .rfmax = 340000, .val = 0x4a },
	{ .rfmax = 341000, .val = 0x4b },
	{ .rfmax = 343000, .val = 0x4c },
	{ .rfmax = 345000, .val = 0x4d },
	{ .rfmax = 349000, .val = 0x4e },
	{ .rfmax = 352000, .val = 0x4f },
	{ .rfmax = 353000, .val = 0x50 },
	{ .rfmax = 355000, .val = 0x51 },
	{ .rfmax = 357000, .val = 0x52 },
	{ .rfmax = 359000, .val = 0x53 },
	{ .rfmax = 361000, .val = 0x54 },
	{ .rfmax = 362000, .val = 0x55 },
	{ .rfmax = 364000, .val = 0x56 },
	{ .rfmax = 368000, .val = 0x57 },
	{ .rfmax = 370000, .val = 0x58 },
	{ .rfmax = 372000, .val = 0x59 },
	{ .rfmax = 375000, .val = 0x5a },
	{ .rfmax = 376000, .val = 0x5b },
	{ .rfmax = 377000, .val = 0x5c },
	{ .rfmax = 379000, .val = 0x5d },
	{ .rfmax = 382000, .val = 0x5e },
	{ .rfmax = 384000, .val = 0x5f },
	{ .rfmax = 385000, .val = 0x60 },
	{ .rfmax = 386000, .val = 0x61 },
	{ .rfmax = 388000, .val = 0x62 },
	{ .rfmax = 390000, .val = 0x63 },
	{ .rfmax = 393000, .val = 0x64 },
	{ .rfmax = 394000, .val = 0x65 },
	{ .rfmax = 396000, .val = 0x66 },
	{ .rfmax = 397000, .val = 0x67 },
	{ .rfmax = 398000, .val = 0x68 },
	{ .rfmax = 400000, .val = 0x69 },
	{ .rfmax = 402000, .val = 0x6a },
	{ .rfmax = 403000, .val = 0x6b },
	{ .rfmax = 407000, .val = 0x6c },
	{ .rfmax = 408000, .val = 0x6d },
	{ .rfmax = 409000, .val = 0x6e },
	{ .rfmax = 410000, .val = 0x6f },
	{ .rfmax = 411000, .val = 0x70 },
	{ .rfmax = 412000, .val = 0x71 },
	{ .rfmax = 413000, .val = 0x72 },
	{ .rfmax = 414000, .val = 0x73 },
	{ .rfmax = 417000, .val = 0x74 },
	{ .rfmax = 418000, .val = 0x75 },
	{ .rfmax = 420000, .val = 0x76 },
	{ .rfmax = 422000, .val = 0x77 },
	{ .rfmax = 423000, .val = 0x78 },
	{ .rfmax = 424000, .val = 0x79 },
	{ .rfmax = 427000, .val = 0x7a },
	{ .rfmax = 428000, .val = 0x7b },
	{ .rfmax = 429000, .val = 0x7d },
	{ .rfmax = 432000, .val = 0x7f },
	{ .rfmax = 434000, .val = 0x80 },
	{ .rfmax = 435000, .val = 0x81 },
	{ .rfmax = 436000, .val = 0x83 },
	{ .rfmax = 437000, .val = 0x84 },
	{ .rfmax = 438000, .val = 0x85 },
	{ .rfmax = 439000, .val = 0x86 },
	{ .rfmax = 440000, .val = 0x87 },
	{ .rfmax = 441000, .val = 0x88 },
	{ .rfmax = 442000, .val = 0x89 },
	{ .rfmax = 445000, .val = 0x8a },
	{ .rfmax = 446000, .val = 0x8b },
	{ .rfmax = 447000, .val = 0x8c },
	{ .rfmax = 448000, .val = 0x8e },
	{ .rfmax = 449000, .val = 0x8f },
	{ .rfmax = 450000, .val = 0x90 },
	{ .rfmax = 452000, .val = 0x91 },
	{ .rfmax = 453000, .val = 0x93 },
	{ .rfmax = 454000, .val = 0x94 },
	{ .rfmax = 456000, .val = 0x96 },
	{ .rfmax = 457000, .val = 0x98 },
	{ .rfmax = 461000, .val = 0x11 },
	{ .rfmax = 468000, .val = 0x12 },
	{ .rfmax = 472000, .val = 0x13 },
	{ .rfmax = 473000, .val = 0x14 },
	{ .rfmax = 474000, .val = 0x15 },
	{ .rfmax = 481000, .val = 0x16 },
	{ .rfmax = 486000, .val = 0x17 },
	{ .rfmax = 491000, .val = 0x18 },
	{ .rfmax = 498000, .val = 0x19 },
	{ .rfmax = 499000, .val = 0x1a },
	{ .rfmax = 501000, .val = 0x1b },
	{ .rfmax = 506000, .val = 0x1c },
	{ .rfmax = 511000, .val = 0x1d },
	{ .rfmax = 516000, .val = 0x1e },
	{ .rfmax = 520000, .val = 0x1f },
	{ .rfmax = 521000, .val = 0x20 },
	{ .rfmax = 525000, .val = 0x21 },
	{ .rfmax = 529000, .val = 0x22 },
	{ .rfmax = 533000, .val = 0x23 },
	{ .rfmax = 539000, .val = 0x24 },
	{ .rfmax = 541000, .val = 0x25 },
	{ .rfmax = 547000, .val = 0x26 },
	{ .rfmax = 549000, .val = 0x27 },
	{ .rfmax = 551000, .val = 0x28 },
	{ .rfmax = 556000, .val = 0x29 },
	{ .rfmax = 561000, .val = 0x2a },
	{ .rfmax = 563000, .val = 0x2b },
	{ .rfmax = 565000, .val = 0x2c },
	{ .rfmax = 569000, .val = 0x2d },
	{ .rfmax = 571000, .val = 0x2e },
	{ .rfmax = 577000, .val = 0x2f },
	{ .rfmax = 580000, .val = 0x30 },
	{ .rfmax = 582000, .val = 0x31 },
	{ .rfmax = 584000, .val = 0x32 },
	{ .rfmax = 588000, .val = 0x33 },
	{ .rfmax = 591000, .val = 0x34 },
	{ .rfmax = 596000, .val = 0x35 },
	{ .rfmax = 598000, .val = 0x36 },
	{ .rfmax = 603000, .val = 0x37 },
	{ .rfmax = 604000, .val = 0x38 },
	{ .rfmax = 606000, .val = 0x39 },
	{ .rfmax = 612000, .val = 0x3a },
	{ .rfmax = 615000, .val = 0x3b },
	{ .rfmax = 617000, .val = 0x3c },
	{ .rfmax = 621000, .val = 0x3d },
	{ .rfmax = 622000, .val = 0x3e },
	{ .rfmax = 625000, .val = 0x3f },
	{ .rfmax = 632000, .val = 0x40 },
	{ .rfmax = 633000, .val = 0x41 },
	{ .rfmax = 634000, .val = 0x42 },
	{ .rfmax = 642000, .val = 0x43 },
	{ .rfmax = 643000, .val = 0x44 },
	{ .rfmax = 647000, .val = 0x45 },
	{ .rfmax = 650000, .val = 0x46 },
	{ .rfmax = 652000, .val = 0x47 },
	{ .rfmax = 657000, .val = 0x48 },
	{ .rfmax = 661000, .val = 0x49 },
	{ .rfmax = 662000, .val = 0x4a },
	{ .rfmax = 665000, .val = 0x4b },
	{ .rfmax = 667000, .val = 0x4c },
	{ .rfmax = 670000, .val = 0x4d },
	{ .rfmax = 673000, .val = 0x4e },
	{ .rfmax = 676000, .val = 0x4f },
	{ .rfmax = 677000, .val = 0x50 },
	{ .rfmax = 681000, .val = 0x51 },
	{ .rfmax = 683000, .val = 0x52 },
	{ .rfmax = 686000, .val = 0x53 },
	{ .rfmax = 688000, .val = 0x54 },
	{ .rfmax = 689000, .val = 0x55 },
	{ .rfmax = 691000, .val = 0x56 },
	{ .rfmax = 695000, .val = 0x57 },
	{ .rfmax = 698000, .val = 0x58 },
	{ .rfmax = 703000, .val = 0x59 },
	{ .rfmax = 704000, .val = 0x5a },
	{ .rfmax = 705000, .val = 0x5b },
	{ .rfmax = 707000, .val = 0x5c },
	{ .rfmax = 710000, .val = 0x5d },
	{ .rfmax = 712000, .val = 0x5e },
	{ .rfmax = 717000, .val = 0x5f },
	{ .rfmax = 718000, .val = 0x60 },
	{ .rfmax = 721000, .val = 0x61 },
	{ .rfmax = 722000, .val = 0x62 },
	{ .rfmax = 723000, .val = 0x63 },
	{ .rfmax = 725000, .val = 0x64 },
	{ .rfmax = 727000, .val = 0x65 },
	{ .rfmax = 730000, .val = 0x66 },
	{ .rfmax = 732000, .val = 0x67 },
	{ .rfmax = 735000, .val = 0x68 },
	{ .rfmax = 740000, .val = 0x69 },
	{ .rfmax = 741000, .val = 0x6a },
	{ .rfmax = 742000, .val = 0x6b },
	{ .rfmax = 743000, .val = 0x6c },
	{ .rfmax = 745000, .val = 0x6d },
	{ .rfmax = 747000, .val = 0x6e },
	{ .rfmax = 748000, .val = 0x6f },
	{ .rfmax = 750000, .val = 0x70 },
	{ .rfmax = 752000, .val = 0x71 },
	{ .rfmax = 754000, .val = 0x72 },
	{ .rfmax = 757000, .val = 0x73 },
	{ .rfmax = 758000, .val = 0x74 },
	{ .rfmax = 760000, .val = 0x75 },
	{ .rfmax = 763000, .val = 0x76 },
	{ .rfmax = 764000, .val = 0x77 },
	{ .rfmax = 766000, .val = 0x78 },
	{ .rfmax = 767000, .val = 0x79 },
	{ .rfmax = 768000, .val = 0x7a },
	{ .rfmax = 773000, .val = 0x7b },
	{ .rfmax = 774000, .val = 0x7c },
	{ .rfmax = 776000, .val = 0x7d },
	{ .rfmax = 777000, .val = 0x7e },
	{ .rfmax = 778000, .val = 0x7f },
	{ .rfmax = 779000, .val = 0x80 },
	{ .rfmax = 781000, .val = 0x81 },
	{ .rfmax = 783000, .val = 0x82 },
	{ .rfmax = 784000, .val = 0x83 },
	{ .rfmax = 785000, .val = 0x84 },
	{ .rfmax = 786000, .val = 0x85 },
	{ .rfmax = 793000, .val = 0x86 },
	{ .rfmax = 794000, .val = 0x87 },
	{ .rfmax = 795000, .val = 0x88 },
	{ .rfmax = 797000, .val = 0x89 },
	{ .rfmax = 799000, .val = 0x8a },
	{ .rfmax = 801000, .val = 0x8b },
	{ .rfmax = 802000, .val = 0x8c },
	{ .rfmax = 803000, .val = 0x8d },
	{ .rfmax = 804000, .val = 0x8e },
	{ .rfmax = 810000, .val = 0x90 },
	{ .rfmax = 811000, .val = 0x91 },
	{ .rfmax = 812000, .val = 0x92 },
	{ .rfmax = 814000, .val = 0x93 },
	{ .rfmax = 816000, .val = 0x94 },
	{ .rfmax = 817000, .val = 0x96 },
	{ .rfmax = 818000, .val = 0x97 },
	{ .rfmax = 820000, .val = 0x98 },
	{ .rfmax = 821000, .val = 0x99 },
	{ .rfmax = 822000, .val = 0x9a },
	{ .rfmax = 828000, .val = 0x9b },
	{ .rfmax = 829000, .val = 0x9d },
	{ .rfmax = 830000, .val = 0x9f },
	{ .rfmax = 831000, .val = 0xa0 },
	{ .rfmax = 833000, .val = 0xa1 },
	{ .rfmax = 835000, .val = 0xa2 },
	{ .rfmax = 836000, .val = 0xa3 },
	{ .rfmax = 837000, .val = 0xa4 },
	{ .rfmax = 838000, .val = 0xa6 },
	{ .rfmax = 840000, .val = 0xa8 },
	{ .rfmax = 842000, .val = 0xa9 },
	{ .rfmax = 845000, .val = 0xaa },
	{ .rfmax = 846000, .val = 0xab },
	{ .rfmax = 847000, .val = 0xad },
	{ .rfmax = 848000, .val = 0xae },
	{ .rfmax = 852000, .val = 0xaf },
	{ .rfmax = 853000, .val = 0xb0 },
	{ .rfmax = 858000, .val = 0xb1 },
	{ .rfmax = 860000, .val = 0xb2 },
	{ .rfmax = 861000, .val = 0xb3 },
	{ .rfmax = 862000, .val = 0xb4 },
	{ .rfmax = 863000, .val = 0xb6 },
	{ .rfmax = 864000, .val = 0xb8 },
	{ .rfmax = 865000, .val = 0xb9 },
	{ .rfmax =      0, .val = 0x00 }, /* end */
};

static struct tda18271_map tda18271_ir_measure[] = {
	{ .rfmax =  30000, .val = 4 },
	{ .rfmax = 200000, .val = 5 },
	{ .rfmax = 600000, .val = 6 },
	{ .rfmax = 865000, .val = 7 },
	{ .rfmax =      0, .val = 0 }, /* end */
};

static struct tda18271_map tda18271_rf_cal_dc_over_dt[] = {
	{ .rfmax =  47900, .val = 0x00 },
	{ .rfmax =  55000, .val = 0x00 },
	{ .rfmax =  61100, .val = 0x0a },
	{ .rfmax =  64000, .val = 0x0a },
	{ .rfmax =  82000, .val = 0x14 },
	{ .rfmax =  84000, .val = 0x19 },
	{ .rfmax = 119000, .val = 0x1c },
	{ .rfmax = 124000, .val = 0x20 },
	{ .rfmax = 129000, .val = 0x2a },
	{ .rfmax = 134000, .val = 0x32 },
	{ .rfmax = 139000, .val = 0x39 },
	{ .rfmax = 144000, .val = 0x3e },
	{ .rfmax = 149000, .val = 0x3f },
	{ .rfmax = 152600, .val = 0x40 },
	{ .rfmax = 154000, .val = 0x40 },
	{ .rfmax = 164700, .val = 0x41 },
	{ .rfmax = 203500, .val = 0x32 },
	{ .rfmax = 353000, .val = 0x19 },
	{ .rfmax = 356000, .val = 0x1a },
	{ .rfmax = 359000, .val = 0x1b },
	{ .rfmax = 363000, .val = 0x1c },
	{ .rfmax = 366000, .val = 0x1d },
	{ .rfmax = 369000, .val = 0x1e },
	{ .rfmax = 373000, .val = 0x1f },
	{ .rfmax = 376000, .val = 0x20 },
	{ .rfmax = 379000, .val = 0x21 },
	{ .rfmax = 383000, .val = 0x22 },
	{ .rfmax = 386000, .val = 0x23 },
	{ .rfmax = 389000, .val = 0x24 },
	{ .rfmax = 393000, .val = 0x25 },
	{ .rfmax = 396000, .val = 0x26 },
	{ .rfmax = 399000, .val = 0x27 },
	{ .rfmax = 402000, .val = 0x28 },
	{ .rfmax = 404000, .val = 0x29 },
	{ .rfmax = 407000, .val = 0x2a },
	{ .rfmax = 409000, .val = 0x2b },
	{ .rfmax = 412000, .val = 0x2c },
	{ .rfmax = 414000, .val = 0x2d },
	{ .rfmax = 417000, .val = 0x2e },
	{ .rfmax = 419000, .val = 0x2f },
	{ .rfmax = 422000, .val = 0x30 },
	{ .rfmax = 424000, .val = 0x31 },
	{ .rfmax = 427000, .val = 0x32 },
	{ .rfmax = 429000, .val = 0x33 },
	{ .rfmax = 432000, .val = 0x34 },
	{ .rfmax = 434000, .val = 0x35 },
	{ .rfmax = 437000, .val = 0x36 },
	{ .rfmax = 439000, .val = 0x37 },
	{ .rfmax = 442000, .val = 0x38 },
	{ .rfmax = 444000, .val = 0x39 },
	{ .rfmax = 447000, .val = 0x3a },
	{ .rfmax = 449000, .val = 0x3b },
	{ .rfmax = 457800, .val = 0x3c },
	{ .rfmax = 465000, .val = 0x0f },
	{ .rfmax = 477000, .val = 0x12 },
	{ .rfmax = 483000, .val = 0x14 },
	{ .rfmax = 502000, .val = 0x19 },
	{ .rfmax = 508000, .val = 0x1b },
	{ .rfmax = 519000, .val = 0x1c },
	{ .rfmax = 522000, .val = 0x1d },
	{ .rfmax = 524000, .val = 0x1e },
	{ .rfmax = 534000, .val = 0x1f },
	{ .rfmax = 549000, .val = 0x20 },
	{ .rfmax = 554000, .val = 0x22 },
	{ .rfmax = 584000, .val = 0x24 },
	{ .rfmax = 589000, .val = 0x26 },
	{ .rfmax = 658000, .val = 0x27 },
	{ .rfmax = 664000, .val = 0x2c },
	{ .rfmax = 669000, .val = 0x2d },
	{ .rfmax = 699000, .val = 0x2e },
	{ .rfmax = 704000, .val = 0x30 },
	{ .rfmax = 709000, .val = 0x31 },
	{ .rfmax = 714000, .val = 0x32 },
	{ .rfmax = 724000, .val = 0x33 },
	{ .rfmax = 729000, .val = 0x36 },
	{ .rfmax = 739000, .val = 0x38 },
	{ .rfmax = 744000, .val = 0x39 },
	{ .rfmax = 749000, .val = 0x3b },
	{ .rfmax = 754000, .val = 0x3c },
	{ .rfmax = 759000, .val = 0x3d },
	{ .rfmax = 764000, .val = 0x3e },
	{ .rfmax = 769000, .val = 0x3f },
	{ .rfmax = 774000, .val = 0x40 },
	{ .rfmax = 779000, .val = 0x41 },
	{ .rfmax = 784000, .val = 0x43 },
	{ .rfmax = 789000, .val = 0x46 },
	{ .rfmax = 794000, .val = 0x48 },
	{ .rfmax = 799000, .val = 0x4b },
	{ .rfmax = 804000, .val = 0x4f },
	{ .rfmax = 809000, .val = 0x54 },
	{ .rfmax = 814000, .val = 0x59 },
	{ .rfmax = 819000, .val = 0x5d },
	{ .rfmax = 824000, .val = 0x61 },
	{ .rfmax = 829000, .val = 0x68 },
	{ .rfmax = 834000, .val = 0x6e },
	{ .rfmax = 839000, .val = 0x75 },
	{ .rfmax = 844000, .val = 0x7e },
	{ .rfmax = 849000, .val = 0x82 },
	{ .rfmax = 854000, .val = 0x84 },
	{ .rfmax = 859000, .val = 0x8f },
	{ .rfmax = 865000, .val = 0x9a },
	{ .rfmax =      0, .val = 0x00 }, /* end */
};

/*---------------------------------------------------------------------*/

struct tda18271_thermo_map {
	u8 d;
	u8 r0;
	u8 r1;
};

static struct tda18271_thermo_map tda18271_thermometer[] = {
	{ .d = 0x00, .r0 = 60, .r1 =  92 },
	{ .d = 0x01, .r0 = 62, .r1 =  94 },
	{ .d = 0x02, .r0 = 66, .r1 =  98 },
	{ .d = 0x03, .r0 = 64, .r1 =  96 },
	{ .d = 0x04, .r0 = 74, .r1 = 106 },
	{ .d = 0x05, .r0 = 72, .r1 = 104 },
	{ .d = 0x06, .r0 = 68, .r1 = 100 },
	{ .d = 0x07, .r0 = 70, .r1 = 102 },
	{ .d = 0x08, .r0 = 90, .r1 = 122 },
	{ .d = 0x09, .r0 = 88, .r1 = 120 },
	{ .d = 0x0a, .r0 = 84, .r1 = 116 },
	{ .d = 0x0b, .r0 = 86, .r1 = 118 },
	{ .d = 0x0c, .r0 = 76, .r1 = 108 },
	{ .d = 0x0d, .r0 = 78, .r1 = 110 },
	{ .d = 0x0e, .r0 = 82, .r1 = 114 },
	{ .d = 0x0f, .r0 = 80, .r1 = 112 },
	{ .d = 0x00, .r0 =  0, .r1 =   0 }, /* end */
};

int tda18271_lookup_thermometer(struct dvb_frontend *fe)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	int val, i = 0;

	while (tda18271_thermometer[i].d < (regs[R_TM] & 0x0f)) {
		if (tda18271_thermometer[i + 1].d == 0)
			break;
		i++;
	}

	if ((regs[R_TM] & 0x20) == 0x20)
		val = tda18271_thermometer[i].r1;
	else
		val = tda18271_thermometer[i].r0;

	tda_map("(%d) tm = %d\n", i, val);

	return val;
}

/*---------------------------------------------------------------------*/

struct tda18271_cid_target_map {
	u32 rfmax;
	u8  target;
	u16 limit;
};

static struct tda18271_cid_target_map tda18271_cid_target[] = {
	{ .rfmax =  46000, .target = 0x04, .limit =  1800 },
	{ .rfmax =  52200, .target = 0x0a, .limit =  1500 },
	{ .rfmax =  70100, .target = 0x01, .limit =  4000 },
	{ .rfmax = 136800, .target = 0x18, .limit =  4000 },
	{ .rfmax = 156700, .target = 0x18, .limit =  4000 },
	{ .rfmax = 186250, .target = 0x0a, .limit =  4000 },
	{ .rfmax = 230000, .target = 0x0a, .limit =  4000 },
	{ .rfmax = 345000, .target = 0x18, .limit =  4000 },
	{ .rfmax = 426000, .target = 0x0e, .limit =  4000 },
	{ .rfmax = 489500, .target = 0x1e, .limit =  4000 },
	{ .rfmax = 697500, .target = 0x32, .limit =  4000 },
	{ .rfmax = 842000, .target = 0x3a, .limit =  4000 },
	{ .rfmax =      0, .target = 0x00, .limit =     0 }, /* end */
};

int tda18271_lookup_cid_target(struct dvb_frontend *fe,
			       u32 *freq, u8 *cid_target, u16 *count_limit)
{
	int i = 0;

	while ((tda18271_cid_target[i].rfmax * 1000) < *freq) {
		if (tda18271_cid_target[i + 1].rfmax == 0)
			break;
		i++;
	}
	*cid_target  = tda18271_cid_target[i].target;
	*count_limit = tda18271_cid_target[i].limit;

	tda_map("(%d) cid_target = %02x, count_limit = %d\n", i,
		tda18271_cid_target[i].target, tda18271_cid_target[i].limit);

	return 0;
}

/*---------------------------------------------------------------------*/

static struct tda18271_rf_tracking_filter_cal tda18271_rf_band_template[] = {
	{ .rfmax =  47900, .rfband = 0x00,
	  .rf1_def =  46000, .rf2_def =      0, .rf3_def =      0 },
	{ .rfmax =  61100, .rfband = 0x01,
	  .rf1_def =  52200, .rf2_def =      0, .rf3_def =      0 },
	{ .rfmax = 152600, .rfband = 0x02,
	  .rf1_def =  70100, .rf2_def = 136800, .rf3_def =      0 },
	{ .rfmax = 164700, .rfband = 0x03,
	  .rf1_def = 156700, .rf2_def =      0, .rf3_def =      0 },
	{ .rfmax = 203500, .rfband = 0x04,
	  .rf1_def = 186250, .rf2_def =      0, .rf3_def =      0 },
	{ .rfmax = 457800, .rfband = 0x05,
	  .rf1_def = 230000, .rf2_def = 345000, .rf3_def = 426000 },
	{ .rfmax = 865000, .rfband = 0x06,
	  .rf1_def = 489500, .rf2_def = 697500, .rf3_def = 842000 },
	{ .rfmax =      0, .rfband = 0x00,
	  .rf1_def =      0, .rf2_def =      0, .rf3_def =      0 }, /* end */
};

int tda18271_lookup_rf_band(struct dvb_frontend *fe, u32 *freq, u8 *rf_band)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	struct tda18271_rf_tracking_filter_cal *map = priv->rf_cal_state;
	int i = 0;

	while ((map[i].rfmax * 1000) < *freq) {
		if (tda18271_debug & DBG_ADV)
			tda_map("(%d) rfmax = %d < freq = %d, "
				"rf1_def = %d, rf2_def = %d, rf3_def = %d, "
				"rf1 = %d, rf2 = %d, rf3 = %d, "
				"rf_a1 = %d, rf_a2 = %d, "
				"rf_b1 = %d, rf_b2 = %d\n",
				i, map[i].rfmax * 1000, *freq,
				map[i].rf1_def, map[i].rf2_def, map[i].rf3_def,
				map[i].rf1, map[i].rf2, map[i].rf3,
				map[i].rf_a1, map[i].rf_a2,
				map[i].rf_b1, map[i].rf_b2);
		if (map[i].rfmax == 0)
			return -EINVAL;
		i++;
	}
	if (rf_band)
		*rf_band = map[i].rfband;

	tda_map("(%d) rf_band = %02x\n", i, map[i].rfband);

	return i;
}

/*---------------------------------------------------------------------*/

struct tda18271_map_layout {
	struct tda18271_pll_map *main_pll;
	struct tda18271_pll_map *cal_pll;

	struct tda18271_map *rf_cal;
	struct tda18271_map *rf_cal_kmco;
	struct tda18271_map *rf_cal_dc_over_dt;

	struct tda18271_map *bp_filter;
	struct tda18271_map *rf_band;
	struct tda18271_map *gain_taper;
	struct tda18271_map *ir_measure;
};

/*---------------------------------------------------------------------*/

int tda18271_lookup_pll_map(struct dvb_frontend *fe,
			    enum tda18271_map_type map_type,
			    u32 *freq, u8 *post_div, u8 *div)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	struct tda18271_pll_map *map = NULL;
	unsigned int i = 0;
	char *map_name;
	int ret = 0;

	BUG_ON(!priv->maps);

	switch (map_type) {
	case MAIN_PLL:
		map = priv->maps->main_pll;
		map_name = "main_pll";
		break;
	case CAL_PLL:
		map = priv->maps->cal_pll;
		map_name = "cal_pll";
		break;
	default:
		/* we should never get here */
		map_name = "undefined";
		break;
	}

	if (!map) {
		tda_warn("%s map is not set!\n", map_name);
		ret = -EINVAL;
		goto fail;
	}

	while ((map[i].lomax * 1000) < *freq) {
		if (map[i + 1].lomax == 0) {
			tda_map("%s: frequency (%d) out of range\n",
				map_name, *freq);
			ret = -ERANGE;
			break;
		}
		i++;
	}
	*post_div = map[i].pd;
	*div      = map[i].d;

	tda_map("(%d) %s: post div = 0x%02x, div = 0x%02x\n",
		i, map_name, *post_div, *div);
fail:
	return ret;
}

int tda18271_lookup_map(struct dvb_frontend *fe,
			enum tda18271_map_type map_type,
			u32 *freq, u8 *val)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	struct tda18271_map *map = NULL;
	unsigned int i = 0;
	char *map_name;
	int ret = 0;

	BUG_ON(!priv->maps);

	switch (map_type) {
	case BP_FILTER:
		map = priv->maps->bp_filter;
		map_name = "bp_filter";
		break;
	case RF_CAL_KMCO:
		map = priv->maps->rf_cal_kmco;
		map_name = "km";
		break;
	case RF_BAND:
		map = priv->maps->rf_band;
		map_name = "rf_band";
		break;
	case GAIN_TAPER:
		map = priv->maps->gain_taper;
		map_name = "gain_taper";
		break;
	case RF_CAL:
		map = priv->maps->rf_cal;
		map_name = "rf_cal";
		break;
	case IR_MEASURE:
		map = priv->maps->ir_measure;
		map_name = "ir_measure";
		break;
	case RF_CAL_DC_OVER_DT:
		map = priv->maps->rf_cal_dc_over_dt;
		map_name = "rf_cal_dc_over_dt";
		break;
	default:
		/* we should never get here */
		map_name = "undefined";
		break;
	}

	if (!map) {
		tda_warn("%s map is not set!\n", map_name);
		ret = -EINVAL;
		goto fail;
	}

	while ((map[i].rfmax * 1000) < *freq) {
		if (map[i + 1].rfmax == 0) {
			tda_map("%s: frequency (%d) out of range\n",
				map_name, *freq);
			ret = -ERANGE;
			break;
		}
		i++;
	}
	*val = map[i].val;

	tda_map("(%d) %s: 0x%02x\n", i, map_name, *val);
fail:
	return ret;
}

/*---------------------------------------------------------------------*/

static struct tda18271_std_map tda18271c1_std_map = {
	.fm_radio = { .if_freq = 1250, .fm_rfn = 1, .agc_mode = 3, .std = 0,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x18 */
	.atv_b    = { .if_freq = 6750, .fm_rfn = 0, .agc_mode = 1, .std = 6,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0e */
	.atv_dk   = { .if_freq = 7750, .fm_rfn = 0, .agc_mode = 1, .std = 7,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0f */
	.atv_gh   = { .if_freq = 7750, .fm_rfn = 0, .agc_mode = 1, .std = 7,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0f */
	.atv_i    = { .if_freq = 7750, .fm_rfn = 0, .agc_mode = 1, .std = 7,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0f */
	.atv_l    = { .if_freq = 7750, .fm_rfn = 0, .agc_mode = 1, .std = 7,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0f */
	.atv_lc   = { .if_freq = 1250, .fm_rfn = 0, .agc_mode = 1, .std = 7,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0f */
	.atv_mn   = { .if_freq = 5750, .fm_rfn = 0, .agc_mode = 1, .std = 5,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0d */
	.atsc_6   = { .if_freq = 3250, .fm_rfn = 0, .agc_mode = 3, .std = 4,
		      .if_lvl = 1, .rfagc_top = 0x37, }, /* EP3[4:0] 0x1c */
	.dvbt_6   = { .if_freq = 3300, .fm_rfn = 0, .agc_mode = 3, .std = 4,
		      .if_lvl = 1, .rfagc_top = 0x37, }, /* EP3[4:0] 0x1c */
	.dvbt_7   = { .if_freq = 3800, .fm_rfn = 0, .agc_mode = 3, .std = 5,
		      .if_lvl = 1, .rfagc_top = 0x37, }, /* EP3[4:0] 0x1d */
	.dvbt_8   = { .if_freq = 4300, .fm_rfn = 0, .agc_mode = 3, .std = 6,
		      .if_lvl = 1, .rfagc_top = 0x37, }, /* EP3[4:0] 0x1e */
	.qam_6    = { .if_freq = 4000, .fm_rfn = 0, .agc_mode = 3, .std = 5,
		      .if_lvl = 1, .rfagc_top = 0x37, }, /* EP3[4:0] 0x1d */
	.qam_8    = { .if_freq = 5000, .fm_rfn = 0, .agc_mode = 3, .std = 7,
		      .if_lvl = 1, .rfagc_top = 0x37, }, /* EP3[4:0] 0x1f */
};

static struct tda18271_std_map tda18271c2_std_map = {
	.fm_radio = { .if_freq = 1250, .fm_rfn = 1, .agc_mode = 3, .std = 0,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x18 */
	.atv_b    = { .if_freq = 6000, .fm_rfn = 0, .agc_mode = 1, .std = 5,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0d */
	.atv_dk   = { .if_freq = 6900, .fm_rfn = 0, .agc_mode = 1, .std = 6,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0e */
	.atv_gh   = { .if_freq = 7100, .fm_rfn = 0, .agc_mode = 1, .std = 6,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0e */
	.atv_i    = { .if_freq = 7250, .fm_rfn = 0, .agc_mode = 1, .std = 6,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0e */
	.atv_l    = { .if_freq = 6900, .fm_rfn = 0, .agc_mode = 1, .std = 6,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0e */
	.atv_lc   = { .if_freq = 1250, .fm_rfn = 0, .agc_mode = 1, .std = 6,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0e */
	.atv_mn   = { .if_freq = 5400, .fm_rfn = 0, .agc_mode = 1, .std = 4,
		      .if_lvl = 0, .rfagc_top = 0x2c, }, /* EP3[4:0] 0x0c */
	.atsc_6   = { .if_freq = 3250, .fm_rfn = 0, .agc_mode = 3, .std = 4,
		      .if_lvl = 1, .rfagc_top = 0x37, }, /* EP3[4:0] 0x1c */
	.dvbt_6   = { .if_freq = 3300, .fm_rfn = 0, .agc_mode = 3, .std = 4,
		      .if_lvl = 1, .rfagc_top = 0x37, }, /* EP3[4:0] 0x1c */
	.dvbt_7   = { .if_freq = 3500, .fm_rfn = 0, .agc_mode = 3, .std = 4,
		      .if_lvl = 1, .rfagc_top = 0x37, }, /* EP3[4:0] 0x1c */
	.dvbt_8   = { .if_freq = 4000, .fm_rfn = 0, .agc_mode = 3, .std = 5,
		      .if_lvl = 1, .rfagc_top = 0x37, }, /* EP3[4:0] 0x1d */
	.qam_6    = { .if_freq = 4000, .fm_rfn = 0, .agc_mode = 3, .std = 5,
		      .if_lvl = 1, .rfagc_top = 0x37, }, /* EP3[4:0] 0x1d */
	.qam_8    = { .if_freq = 5000, .fm_rfn = 0, .agc_mode = 3, .std = 7,
		      .if_lvl = 1, .rfagc_top = 0x37, }, /* EP3[4:0] 0x1f */
};

/*---------------------------------------------------------------------*/

static struct tda18271_map_layout tda18271c1_map_layout = {
	.main_pll          = tda18271c1_main_pll,
	.cal_pll           = tda18271c1_cal_pll,

	.rf_cal            = tda18271c1_rf_cal,
	.rf_cal_kmco       = tda18271c1_km,

	.bp_filter         = tda18271_bp_filter,
	.rf_band           = tda18271_rf_band,
	.gain_taper        = tda18271_gain_taper,
	.ir_measure        = tda18271_ir_measure,
};

static struct tda18271_map_layout tda18271c2_map_layout = {
	.main_pll          = tda18271c2_main_pll,
	.cal_pll           = tda18271c2_cal_pll,

	.rf_cal            = tda18271c2_rf_cal,
	.rf_cal_kmco       = tda18271c2_km,

	.rf_cal_dc_over_dt = tda18271_rf_cal_dc_over_dt,

	.bp_filter         = tda18271_bp_filter,
	.rf_band           = tda18271_rf_band,
	.gain_taper        = tda18271_gain_taper,
	.ir_measure        = tda18271_ir_measure,
};

int tda18271_assign_map_layout(struct dvb_frontend *fe)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	int ret = 0;

	switch (priv->id) {
	case TDA18271HDC1:
		priv->maps = &tda18271c1_map_layout;
		memcpy(&priv->std, &tda18271c1_std_map,
		       sizeof(struct tda18271_std_map));
		break;
	case TDA18271HDC2:
		priv->maps = &tda18271c2_map_layout;
		memcpy(&priv->std, &tda18271c2_std_map,
		       sizeof(struct tda18271_std_map));
		break;
	default:
		ret = -EINVAL;
		break;
	}
	memcpy(priv->rf_cal_state, &tda18271_rf_band_template,
	       sizeof(tda18271_rf_band_template));

	return ret;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
