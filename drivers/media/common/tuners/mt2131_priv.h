/*
 *  Driver for Microtune MT2131 "QAM/8VSB single chip tuner"
 *
 *  Copyright (c) 2006 Steven Toth <stoth@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __MT2131_PRIV_H__
#define __MT2131_PRIV_H__

/* Regs */
#define MT2131_PWR              0x07
#define MT2131_UPC_1            0x0b
#define MT2131_AGC_RL           0x10
#define MT2131_MISC_2           0x15

/* frequency values in KHz */
#define MT2131_IF1              1220
#define MT2131_IF2              44000
#define MT2131_FREF             16000

struct mt2131_priv {
	struct mt2131_config *cfg;
	struct i2c_adapter   *i2c;

	u32 frequency;
	u32 bandwidth;
};

#endif /* __MT2131_PRIV_H__ */

/*
 * Local variables:
 * c-basic-offset: 8
 */
