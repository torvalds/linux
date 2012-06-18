/*
 * arch/arm/mach-sun3i/pm/standby/standby_twi.h
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

#ifndef _STANDBY_TWI_H_
#define _STANDBY_TWI_H_

#include "ePDK.h"
#include "standby_cfg.h"
#include "standby_reg.h"



#define TWI_OP_RD                   (0)
#define TWI_OP_WR                   (1)


extern __s32 standby_twi_init(void);
extern __s32 standby_twi_exit(void);
extern __s32 twi_byte_rw(__s32 op_type, __u8 saddr, __u8 baddr, __u8 *data);

#endif  //_STANDBY_TWI_H_

