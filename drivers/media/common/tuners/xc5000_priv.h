/*
 *  Driver for Xceive XC5000 "QAM/8VSB single chip tuner"
 *
 *  Copyright (c) 2007 Steven Toth <stoth@hauppauge.com>
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

#ifndef XC5000_PRIV_H
#define XC5000_PRIV_H

struct xc5000_priv {
	struct xc5000_config *cfg;
	struct i2c_adapter   *i2c;

	u32 freq_hz;
	u32 bandwidth;
	u8  video_standard;
	u8  rf_mode;
	u8  fwloaded;
};

#endif
