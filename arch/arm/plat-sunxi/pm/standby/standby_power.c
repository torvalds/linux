/*
 * arch/arm/plat-sunxi/pm/standby/standby_power.c
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

#include "standby_i.h"

/*
*********************************************************************************************************
*                           standby_power_init
*
* Description: init power for standby.
*
* Arguments  : none;
*
* Returns    : result;
*********************************************************************************************************
*/
__s32 standby_power_init(void)
{
    __u8 val, mask, reg_val;
    __s32   i;

	standby_twi_init(AXP_IICBUS);

    #if(AXP_WAKEUP & AXP_WAKEUP_KEY)
    /* enable pek long/short */
	twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN3, &reg_val);
	reg_val |= 0x03;
	twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN3, &reg_val);
    #endif

    #if(AXP_WAKEUP & AXP_WAKEUP_LOWBATT)
    /* enable low voltage warning */
	twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN4, &reg_val);
	reg_val |= 0x03;
	twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN4, &reg_val);
    /* clear pending */
	reg_val = 0x03;
	twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQ4, &reg_val);
    #endif

    return 0;
}


/*
*********************************************************************************************************
*                           standby_power_exit
*
* Description: exit power for standby.
*
* Arguments  : none;
*
* Returns    : result;
*********************************************************************************************************
*/
__s32 standby_power_exit(void)
{
    __u8    reg_val;

	twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQ4, &reg_val);
	twi_byte_rw(TWI_OP_WR, AXP_ADDR,0x0E, &reg_val);

    #if(AXP_WAKEUP & AXP_WAKEUP_KEY)
    /* disable pek long/short */
	twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN3, &reg_val);
	reg_val &= ~0x03;
	twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN3, &reg_val);
    #endif

    #if(AXP_WAKEUP & AXP_WAKEUP_LOWBATT)
    /* disable low voltage warning */
	twi_byte_rw(TWI_OP_RD, AXP_ADDR,AXP20_IRQEN4, &reg_val);
	reg_val &= ~0x03;
	twi_byte_rw(TWI_OP_WR, AXP_ADDR,AXP20_IRQEN4, &reg_val);
    #endif

    standby_twi_exit();
    return 0;
}


static inline int check_range(struct axp_info *info,__s32 voltage)
{
	if (voltage < info->min_uV || voltage > info->max_uV)
		return -1;

	return 0;
}

static int axp20_ldo4_data[] = {
    1250, 1300, 1400, 1500, 1600, 1700,
    1800, 1900, 2000, 2500, 2700, 2800,
    3000, 3100, 3200, 3300
};

static struct axp_info axp20_info[] = {
	AXP(POWER_VOL_LDO1,	 AXP20LDO1,	AXP20LDO1,	  0, AXP20_LDO1,  0, 0),//ldo1 for rtc
	AXP(POWER_VOL_LDO2,	      1800,      3300,  100, AXP20_LDO2,  4, 4),//ldo2 for analog1
	AXP(POWER_VOL_LDO3,	       700,      3500,   25, AXP20_LDO3,  0, 7),//ldo3 for digital
	AXP(POWER_VOL_LDO4,	      1250,      3300,  100, AXP20_LDO4,  0, 4),//ldo4 for analog2
	AXP(POWER_VOL_DCDC2,       700,      2275,   25, AXP20_BUCK2, 0, 6),//buck2 for core
	AXP(POWER_VOL_DCDC3,       700,      3500,   25, AXP20_BUCK3, 0, 7),//buck3 for memery
};

static inline struct axp_info *find_info(int id)
{
	struct axp_info *ri;
	int i;

	for (i = 0; i < sizeof(axp20_info)/sizeof(struct axp_info); i++) {
		ri = &axp20_info[i];
		if (ri->id == id)
			return ri;
	}
	return 0;
}

/*
*********************************************************************************************************
*                           standby_set_voltage
*
*Description: set voltage for standby;
*
*Arguments  : type      voltage type, defined as "enum power_vol_type_e";
*             voltage   voltage value, based on "mv";
*
*Return     : none;
*
*Notes      :
*
*********************************************************************************************************
*/
void  standby_set_voltage(enum power_vol_type_e type, __s32 voltage)
{
	struct axp_info *info = 0;
	__u8 val, mask, reg_val;

	info = find_info(type);
	if (info == 0) {
		return;
	}

	if (check_range(info, voltage)) {
		return;
	}

	if (type != POWER_VOL_LDO4)
		val = raw_lib_udiv((voltage-info->min_uV+info->step_uV-1), info->step_uV);
	else{
		if(voltage == 1250000 ){
			val = 0;
		}
		else{
			val = raw_lib_udiv((voltage-1200000+info->step_uV-1), info->step_uV);
			if(val > 16){
				val = val - 6;
			}
			else if(val > 13){
				val = val - 5;
			}
			else if(val > 12){
				val = val - 4;
			}
			else if(val > 8)
				val = 8;
		}
	}


	val <<= info->vol_shift;
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;

	twi_byte_rw(TWI_OP_RD,AXP_ADDR,info->vol_reg, &reg_val);

	if ((reg_val & mask) != val) {
		reg_val = (reg_val & ~mask) | val;
		twi_byte_rw(TWI_OP_WR,AXP_ADDR,info->vol_reg, &reg_val);
	}

	return;
}


/*
*********************************************************************************************************
*                           standby_get_voltage
*
*Description: get voltage for standby;
*
*Arguments  : type  voltage type, defined as "enum power_vol_type_e";
*
*Return     : voltage value, based on "mv";
*
*Notes      :
*
*********************************************************************************************************
*/
__u32 standby_get_voltage(enum power_vol_type_e type)
{
	struct axp_info *info = 0;
	__u8 val, mask;
	int ret;

	info = find_info(type);
	if (info == 0) {
		return -1;
	}

	twi_byte_rw(TWI_OP_RD,AXP_ADDR,info->vol_reg, &val);

	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	val = (val & mask) >> info->vol_shift;
	if (type != POWER_VOL_LDO4)
		return info->min_uV + info->step_uV * val;
	else
		return axp20_ldo4_data[val]*1000;
}


