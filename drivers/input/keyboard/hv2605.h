/*
 * drivers/input/keyboard/hv2605.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
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

#ifndef __LINUX_HV_KEYPAD_H__
#define __LINUX_HV_KEYPAD_H__



#define HV_NAME	"hv_keypad"

struct hv_keypad_platform_data{
	u16	intr;		/* irq number	*/
};

#define PIO_BASE_ADDRESS (0xf1c20800)
#define PIOA_CFG1_REG    (PIO_BASE_ADDRESS+0x4)
#define PIOA_DATA        (PIO_BASE_ADDRESS+0x10)  
#define DELAY_PERIOD     (5)


#endif //__LINUX_HV_KEYPAD_H__

