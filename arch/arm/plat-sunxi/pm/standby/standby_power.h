/*
 * arch/arm/plat-sunxi/pm/standby/standby_power.h
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

#ifndef __STANDBY_POWER_H__
#define __STANDBY_POWER_H__

#include "standby_cfg.h"

enum power_vol_type_e{

    POWER_VOL_DCDC1,
    POWER_VOL_DCDC2,
    POWER_VOL_DCDC3,
    POWER_VOL_LDO1,
    POWER_VOL_LDO2,
    POWER_VOL_LDO3,
    POWER_VOL_LDO4,

};

#define AXP_ADDR        (0x34)
#define AXP_IICBUS      (0)
#define AXP20_LDO1      (0x00)
#define AXP20_LDO2      (0x28)
#define AXP20_LDO3      (0x29)
#define AXP20_LDO4      (0x28)
#define AXP20_BUCK2     (0x23)
#define AXP20_BUCK3     (0x27)

#define AXP20_IRQEN1    (0x40)
#define AXP20_IRQEN2    (0x41)
#define AXP20_IRQEN3    (0x42)
#define AXP20_IRQEN4    (0x43)
#define AXP20_IRQEN5    (0x44)

#define AXP20_IRQ1      (0x48)
#define AXP20_IRQ2      (0x49)
#define AXP20_IRQ3      (0x4A)
#define AXP20_IRQ4      (0x4B)
#define AXP20_IRQ5      (0x4C)

#define AXP20LDO1       1300


#define AXP(_id, min, max, step, vreg, shift, nbits)    \
{                               \
    .id = _id,                  \
    .min_uV        = (min),     \
    .max_uV        = (max),     \
    .step_uV    = (step),       \
    .vol_reg    =  (vreg),      \
    .vol_shift    = (shift),    \
    .vol_nbits    = (nbits),    \
}

struct axp_info {
    enum    power_vol_type_e id;
    int     min_uV;
    int     max_uV;
    int     step_uV;
    int     vol_reg;
    int     vol_shift;
    int     vol_nbits;
};


#define AXP_WAKEUP_KEY          (1<<0)
#define AXP_WAKEUP_LOWBATT      (1<<1)
#define AXP_WAKEUP_USB          (1<<2)
#define AXP_WAKEUP_AC           (1<<3)

#define AXP_WAKEUP              (AXP_WAKEUP_KEY | AXP_WAKEUP_LOWBATT)

extern __s32 standby_power_init(void);
extern __s32 standby_power_exit(void);
extern void  standby_set_voltage(enum power_vol_type_e type, __s32 voltage);
extern __u32 standby_get_voltage(enum power_vol_type_e type);


#endif  /* __STANDBY_POWER_H__ */


