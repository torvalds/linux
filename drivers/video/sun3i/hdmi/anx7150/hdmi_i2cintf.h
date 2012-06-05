/*
 * drivers/video/sun3i/hdmi/anx7150/hdmi_i2cintf.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Danling <danliang@allwinnertech.com>
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

//  ANALOGIX Company
//  ANX7150 Demo Firmware on SST
//  Version 0.50	2006/09/20

#ifndef _ANX7150__I2C_INTF_H
#define _ANX7150__I2C_INTF_H

#include "../hdmi_hal.h"

#define ANX7150_PORT0_ADDR	0x76//0x72 //  //ANX7150
#define ANX7150_PORT1_ADDR	0x7e//0x7A //  //ANX7150


__s32 ANX7150_i2c_Request(void);
__s32 ANX7150_i2c_Release(void);
__s32 ANX7150_i2c_write_p0_reg(__u8 offset, __u8 d);
__s32 ANX7150_i2c_write_p1_reg(__u8 offset, __u8 d);
__s32 ANX7150_i2c_read_p0_reg(__u8 offset, __u8 *d);
__s32 ANX7150_i2c_read_p1_reg(__u8 offset, __u8 *d);


void ANX7150_Resetn_Pin(__u32 value);
#endif

