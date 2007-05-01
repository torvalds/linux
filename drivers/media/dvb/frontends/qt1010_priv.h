/*
 *  Driver for Quantek QT1010 silicon tuner
 *
 *  Copyright (C) 2006 Antti Palosaari <crope@iki.fi>
 *                     Aapo Tahkola <aet@rasterburn.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef QT1010_PRIV_H
#define QT1010_PRIV_H

/*
reg def meaning
=== === =======
00  00  ?
01  a0  ? operation start/stop; start=80, stop=00
02  00  ?
03  19  ?
04  00  ?
05  00  ? maybe band selection
06  00  ?
07  2b  set frequency: 32 MHz scale, n*32 MHz
08  0b  ?
09  10  ? changes every 8/24 MHz; values 1d/1c
0a  08  set frequency: 4 MHz scale, n*4 MHz
0b  41  ? changes every 2/2 MHz; values 45/45
0c  e1  ?
0d  94  ?
0e  b6  ?
0f  2c  ?
10  10  ?
11  f1  ? maybe device specified adjustment
12  11  ? maybe device specified adjustment
13  3f  ?
14  1f  ?
15  3f  ?
16  ff  ?
17  ff  ?
18  f7  ?
19  80  ?
1a  d0  set frequency: 125 kHz scale, n*125 kHz
1b  00  ?
1c  89  ?
1d  00  ?
1e  00  ? looks like operation register; write cmd here, read result from 1f-26
1f  20  ? chip initialization
20  e0  ? chip initialization
21  20  ?
22  d0  ?
23  d0  ?
24  d0  ?
25  40  ? chip initialization
26  08  ?
27  29  ?
28  55  ?
29  39  ?
2a  13  ?
2b  01  ?
2c  ea  ?
2d  00  ?
2e  00  ? not used?
2f  00  ? not used?
*/

#define QT1010_STEP         125000 /*  125 kHz used by Windows drivers,
				      hw could be more precise but we don't
				      know how to use */
#define QT1010_MIN_FREQ   48000000 /*   48 MHz */
#define QT1010_MAX_FREQ  860000000 /*  860 MHz */
#define QT1010_OFFSET   1246000000 /* 1246 MHz */

#define QT1010_WR 0
#define QT1010_RD 1
#define QT1010_M1 3

typedef struct {
	u8 oper, reg, val;
} qt1010_i2c_oper_t;

struct qt1010_priv {
	struct qt1010_config *cfg;
	struct i2c_adapter   *i2c;

	u8 reg1f_init_val;
	u8 reg20_init_val;
	u8 reg25_init_val;

	u32 frequency;
	u32 bandwidth;
};

#endif
