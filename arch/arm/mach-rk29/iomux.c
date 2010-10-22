/*
 * arch/arm/mach-rk29/iomux.c
 *
 *Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/spinlock.h>

#include <mach/rk29_iomap.h>  
#include <mach/iomux.h>


static struct mux_config rk29_muxs[] = {
/*
 *	 description				mux  mode   mux	  mux  
 *						reg  offset inter mode
 */
//MUX_CFG(GPIOE_I2C0_SEL_NAME,		 	GPIO0L,   30,    2,	  0,	DEFAULT)	 

};