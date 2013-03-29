/*
 * arch/arm/plat-sunxi/pm/standby/standby_cfg.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
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

#ifndef __STANDBY_CFG_H__
#define __STANDBY_CFG_H__


//config wakeup source for standby
#define ALLOW_DISABLE_HOSC          (1)     // if allow disable hosc

#define STANDBY_LDO1_VOL            (1300)  //LDO1 voltage value
#define STANDBY_LDO2_VOL            (3000)  //LDO2 voltage value
#define STANDBY_LDO3_VOL            (2800)  //LDO3 voltage value
#define STANDBY_LDO4_VOL            (3300)  //LDO4 voltage value
#define STANDBY_DCDC2_VOL           (1000)  //DCDC2 voltage value
#define STANDBY_DCDC3_VOL           (1000)  //DCDC3 voltage value


#endif  /* __STANDBY_CFG_H__ */


