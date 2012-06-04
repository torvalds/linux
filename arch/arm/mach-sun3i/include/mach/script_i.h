/*
 * arch/arm/mach-sun3i/include/mach/script_i.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Jerry Wang <wangflord@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef  _SCRIPT_I_H_
#define  _SCRIPT_I_H_

typedef struct
{
	int  main_key_count;
	int  version[3];
}
script_head_t;

typedef struct
{
	char main_name[32];
	int  lenth;
	int  offset;
}
script_main_key_t;

typedef struct
{
	char sub_name[32];
	int  offset;
	int  pattern;
}
script_sub_key_t;


#endif  // _SCRIPT_I_H_


