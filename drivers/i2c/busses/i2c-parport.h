/* ------------------------------------------------------------------------ *
 * i2c-parport.h I2C bus over parallel port                                 *
 * ------------------------------------------------------------------------ *
   Copyright (C) 2003-2004 Jean Delvare <khali@linux-fr.org>
   
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
 * ------------------------------------------------------------------------ */

#ifdef DATA
#undef DATA
#endif

#define DATA	0
#define STAT	1
#define CTRL	2

struct lineop {
	u8 val;
	u8 port;
	u8 inverted;
};

struct adapter_parm {
	struct lineop setsda;
	struct lineop setscl;
	struct lineop getsda;
	struct lineop getscl;
	struct lineop init;
};

static struct adapter_parm adapter_parm[] = {
	/* type 0: Philips adapter */
	{
		.setsda	= { 0x80, DATA, 1 },
		.setscl	= { 0x08, CTRL, 0 },
		.getsda	= { 0x80, STAT, 0 },
		.getscl	= { 0x08, STAT, 0 },
	},
	/* type 1: home brew teletext adapter */
	{
		.setsda	= { 0x02, DATA, 0 },
		.setscl	= { 0x01, DATA, 0 },
		.getsda	= { 0x80, STAT, 1 },
	},
	/* type 2: Velleman K8000 adapter */
	{
		.setsda	= { 0x02, CTRL, 1 },
		.setscl	= { 0x08, CTRL, 1 },
		.getsda	= { 0x10, STAT, 0 },
	},
	/* type 3: ELV adapter */
	{
		.setsda	= { 0x02, DATA, 1 },
		.setscl	= { 0x01, DATA, 1 },
		.getsda	= { 0x40, STAT, 1 },
		.getscl	= { 0x08, STAT, 1 },
	},
	/* type 4: ADM1032 evaluation board */
	{
		.setsda	= { 0x02, DATA, 1 },
		.setscl	= { 0x01, DATA, 1 },
		.getsda	= { 0x10, STAT, 1 },
		.init	= { 0xf0, DATA, 0 },
	},
	/* type 5: ADM1025, ADM1030 and ADM1031 evaluation boards */
	{
		.setsda	= { 0x02, DATA, 1 },
		.setscl	= { 0x01, DATA, 1 },
		.getsda	= { 0x10, STAT, 1 },
	},
	/* type 6: Barco LPT->DVI (K5800236) adapter */
	{
		.setsda	= { 0x02, DATA, 1 },
		.setscl	= { 0x01, DATA, 1 },
		.getsda	= { 0x20, STAT, 0 },
		.getscl	= { 0x40, STAT, 0 },
		.init	= { 0xfc, DATA, 0 },
	},
};

static int type;
module_param(type, int, 0);
MODULE_PARM_DESC(type,
	"Type of adapter:\n"
	" 0 = Philips adapter\n"
	" 1 = home brew teletext adapter\n"
	" 2 = Velleman K8000 adapter\n"
	" 3 = ELV adapter\n"
	" 4 = ADM1032 evaluation board\n"
	" 5 = ADM1025, ADM1030 and ADM1031 evaluation boards\n"
	" 6 = Barco LPT->DVI (K5800236) adapter\n"
);
