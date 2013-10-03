/*
 * Copyright (C) 2010-2013 Bluecherry, LLC <http://www.bluecherrydvr.com>
 *
 * Original author:
 * Ben Collins <bcollins@ubuntu.com>
 *
 * Additional work by:
 * John Brooks <john.brooks@bluecherry.net>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/delay.h>

#include "solo6x10.h"

/* Control */
#define EE_SHIFT_CLK	0x04
#define EE_CS		0x08
#define EE_DATA_WRITE	0x02
#define EE_DATA_READ	0x01
#define EE_ENB		(0x80 | EE_CS)

#define eeprom_delay()	udelay(100)
#if 0
#define eeprom_delay()	solo_reg_read(solo_dev, SOLO_EEPROM_CTRL)
#define eeprom_delay()	({				\
	int i, ret;					\
	udelay(100);					\
	for (i = ret = 0; i < 1000 && !ret; i++)	\
		ret = solo_eeprom_reg_read(solo_dev);	\
})
#endif
#define ADDR_LEN	6

/* Commands */
#define EE_EWEN_CMD	4
#define EE_EWDS_CMD	4
#define EE_WRITE_CMD	5
#define EE_READ_CMD	6
#define EE_ERASE_CMD	7

static unsigned int solo_eeprom_reg_read(struct solo_dev *solo_dev)
{
	return solo_reg_read(solo_dev, SOLO_EEPROM_CTRL) & EE_DATA_READ;
}

static void solo_eeprom_reg_write(struct solo_dev *solo_dev, u32 data)
{
	solo_reg_write(solo_dev, SOLO_EEPROM_CTRL, data);
	eeprom_delay();
}

static void solo_eeprom_cmd(struct solo_dev *solo_dev, int cmd)
{
	int i;

	solo_eeprom_reg_write(solo_dev, SOLO_EEPROM_ACCESS_EN);
	solo_eeprom_reg_write(solo_dev, SOLO_EEPROM_ENABLE);

	for (i = 4 + ADDR_LEN; i >= 0; i--) {
		int dataval = (cmd & (1 << i)) ? EE_DATA_WRITE : 0;

		solo_eeprom_reg_write(solo_dev, SOLO_EEPROM_ENABLE | dataval);
		solo_eeprom_reg_write(solo_dev, SOLO_EEPROM_ENABLE |
				      EE_SHIFT_CLK | dataval);
	}

	solo_eeprom_reg_write(solo_dev, SOLO_EEPROM_ENABLE);
}

unsigned int solo_eeprom_ewen(struct solo_dev *solo_dev, int w_en)
{
	int ewen_cmd = (w_en ? 0x3f : 0) | (EE_EWEN_CMD << ADDR_LEN);
	unsigned int retval = 0;
	int i;

	solo_eeprom_cmd(solo_dev, ewen_cmd);

	for (i = 0; i < 16; i++) {
		solo_eeprom_reg_write(solo_dev, SOLO_EEPROM_ENABLE |
				      EE_SHIFT_CLK);
		retval = (retval << 1) | solo_eeprom_reg_read(solo_dev);
		solo_eeprom_reg_write(solo_dev, SOLO_EEPROM_ENABLE);
		retval = (retval << 1) | solo_eeprom_reg_read(solo_dev);
	}

	solo_eeprom_reg_write(solo_dev, ~EE_CS);
	retval = (retval << 1) | solo_eeprom_reg_read(solo_dev);

	return retval;
}

unsigned short solo_eeprom_read(struct solo_dev *solo_dev, int loc)
{
	int read_cmd = loc | (EE_READ_CMD << ADDR_LEN);
	unsigned short retval = 0;
	int i;

	solo_eeprom_cmd(solo_dev, read_cmd);

	for (i = 0; i < 16; i++) {
		solo_eeprom_reg_write(solo_dev, SOLO_EEPROM_ENABLE |
				      EE_SHIFT_CLK);
		retval = (retval << 1) | solo_eeprom_reg_read(solo_dev);
		solo_eeprom_reg_write(solo_dev, SOLO_EEPROM_ENABLE);
	}

	solo_eeprom_reg_write(solo_dev, ~EE_CS);

	return retval;
}

int solo_eeprom_write(struct solo_dev *solo_dev, int loc,
		      unsigned short data)
{
	int write_cmd = loc | (EE_WRITE_CMD << ADDR_LEN);
	unsigned int retval;
	int i;

	solo_eeprom_cmd(solo_dev, write_cmd);

	for (i = 15; i >= 0; i--) {
		unsigned int dataval = (data >> i) & 1;

		solo_eeprom_reg_write(solo_dev, EE_ENB);
		solo_eeprom_reg_write(solo_dev,
				      EE_ENB | (dataval << 1) | EE_SHIFT_CLK);
	}

	solo_eeprom_reg_write(solo_dev, EE_ENB);
	solo_eeprom_reg_write(solo_dev, ~EE_CS);
	solo_eeprom_reg_write(solo_dev, EE_ENB);

	for (i = retval = 0; i < 10000 && !retval; i++)
		retval = solo_eeprom_reg_read(solo_dev);

	solo_eeprom_reg_write(solo_dev, ~EE_CS);

	return !retval;
}
