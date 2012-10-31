/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
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

#ifndef __LCD_PANNEL_CFG_H__
#define __LCD_PANNEL_CFG_H__

#include "dev_lcd.h"

void LCD_power_on_generic(__u32 sel);
void LCD_power_off_generic(__u32 sel);
void LCD_bl_open_generic(__u32 sel);
void LCD_bl_close_generic(__u32 sel);
__s32 LCD_open_flow_generic(__u32 sel);
__s32 LCD_close_flow_generic(__u32 sel);
__s32 LCD_user_defined_func_generic(__u32 sel, __u32 para1, __u32 para2,
				    __u32 para3);
void LCD_get_panel_funs_generic(__lcd_panel_fun_t *fun);

void LCD_get_panel_funs_0(__lcd_panel_fun_t *fun);
void LCD_get_panel_funs_1(__lcd_panel_fun_t *fun);

#define BIT0	0x00000001
#define BIT1	0x00000002
#define BIT2	0x00000004
#define BIT3	0x00000008
#define BIT4	0x00000010
#define BIT5	0x00000020
#define BIT6	0x00000040
#define BIT7	0x00000080
#define BIT8	0x00000100
#define BIT9	0x00000200
#define BIT10	0x00000400
#define BIT11	0x00000800
#define BIT12	0x00001000
#define BIT13	0x00002000
#define BIT14	0x00004000
#define BIT15	0x00008000
#define BIT16	0x00010000
#define BIT17	0x00020000
#define BIT18	0x00040000
#define BIT19	0x00080000
#define BIT20	0x00100000
#define BIT21	0x00200000
#define BIT22	0x00400000
#define BIT23	0x00800000
#define BIT24	0x01000000
#define BIT25	0x02000000
#define BIT26	0x04000000
#define BIT27	0x08000000
#define BIT28	0x10000000
#define BIT29	0x20000000
#define BIT30	0x40000000
#define BIT31	0x80000000

#define sys_get_wvalue(n) (*((volatile __u32 *)(n))) /* word input */
#define sys_put_wvalue(n,c) (*((volatile __u32 *)(n)) = (c)) /* word output */

#endif
