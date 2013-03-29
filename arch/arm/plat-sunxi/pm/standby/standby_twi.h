/*
 * arch/arm/plat-sunxi/pm/standby/standby_twi.h
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

#ifndef __STANDBY_TWI_H__
#define __STANDBY_TWI_H__

#include "standby_cfg.h"


typedef struct tag_twic_reg
{
    volatile unsigned int reg_saddr;
    volatile unsigned int reg_xsaddr;
    volatile unsigned int reg_data;
    volatile unsigned int reg_ctl;
    volatile unsigned int reg_status;
    volatile unsigned int reg_clkr;
    volatile unsigned int reg_reset;
    volatile unsigned int reg_efr;
    volatile unsigned int reg_lctl;

}__twic_reg_t;



enum twi_op_type_e{
    TWI_OP_RD,
    TWI_OP_WR,
};


extern __s32 standby_twi_init(int group);
extern __s32 standby_twi_exit(void);
extern __s32 twi_byte_rw(enum twi_op_type_e op, __u8 saddr, __u8 baddr, __u8 *data);



#endif  /* __STANDBY_TWI_H__ */
