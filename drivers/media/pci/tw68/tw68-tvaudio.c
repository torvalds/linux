/*
 *  tw68_controls.c
 *  Part of the device driver for Techwell 68xx based cards
 *
 *  Much of this code is derived from the cx88 and sa7134 drivers, which
 *  were in turn derived from the bt87x driver.  The original work was by
 *  Gerd Knorr; more recently the code was enhanced by Mauro Carvalho Chehab,
 *  Hans Verkuil, Andy Walls and many others.  Their work is gratefully
 *  acknowledged.  Full credit goes to them - any problems within this code
 *  are mine.
 *
 *  Copyright (C) 2009  William M. Brack <wbrack@mmm.com.hk>
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "tw68.h"

int tw68_tvaudio_rx2mode(u32 rx)
{
	return 0;
}

void tw68_tvaudio_setmute(struct tw68_dev *dev)
{
	return;
}

void tw68_tvaudio_setinput(struct tw68_dev *dev, struct tw68_input *in)
{
	return;
}

void tw68_tvaudio_setvolume(struct tw68_dev *dev, int level)
{
	return;
}

int tw68_tvaudio_getstereo(struct tw68_dev *dev)
{
	return 0;
}

void tw68_tvaudio_init(struct tw68_dev *dev)
{
	return;
}

int tw68_tvaudio_init2(struct tw68_dev *dev)
{
	return 0;
}

int tw68_tvaudio_fini(struct tw68_dev *dev)
{
	return 0;
}

int tw68_tvaudio_do_scan(struct tw68_dev *dev)
{
	return 0;
}

void tw68_enable_i2s(struct tw68_dev *dev)
{
	return;
}

