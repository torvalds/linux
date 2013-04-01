/*
 * Copyright (C) 2012 UPI semi <Finley_huang@upi-semi.com>. All Rights Reserved.
 * 5152 Light Sensor Driver for Linux 2.6
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __us5152_H__
#define __us5152_H__

#include <linux/types.h>

#define PWR_MODE_DOWN_MASK     0x80
#define PWR_MODE_OPERATE_MASK     0x7F


/*us5152 Slave Addr*/
#define LIGHT_ADDR      0x72         

/*Interrupt PIN for S3C6410*/
#define IRQ_LIGHT_INT IRQ_EINT(6)

/*Register Set*/
#define REGS_CR0          	0x00
#define REGS_CR1          	0x01
#define REGS_CR2          	0x02
#define REGS_CR3          	0x03
//ALS
#define REGS_INT_LSB_TH_LO      0x04
#define REGS_INT_MSB_TH_LO      0x05
#define REGS_INT_LSB_TH_HI      0x06
#define REGS_INT_MSB_TH_HI      0x07
//ALS data
#define REGS_LBS_SENSOR         0x0C
#define REGS_MBS_SENSOR         0x0D

#define REGS_CR10          	0x10
#define REGS_CR11          	0x11
#define REGS_VERSION_ID      	0x1F
#define REGS_CHIP_ID      	0xB2

/*ShutDown_EN*/
#define CR0_OPERATION		0x0
#define CR0_SHUTDOWN_EN		0x1

#define CR0_SHUTDOWN_SHIFT   	(7)
#define CR0_SHUTDOWN_MASK    	(0x1 << CR0_SHUTDOWN_SHIFT)

/*OneShot_EN*/
#define CR0_ONESHOT_EN		0x01

#define CR0_ONESHOT_SHIFT   	(6)
#define CR0_ONESHOT_MASK    	(0x1 << CR0_ONESHOT_SHIFT)

/*Operation Mode*/
#define CR0_OPMODE_ALSANDPS	0x0 
#define CR0_OPMODE_ALSONLY	0x1 
#define CR0_OPMODE_IRONLY		0x2 

#define CR0_OPMODE_SHIFT       	(4)
#define CR0_OPMODE_MASK        	(0x3 << CR0_OPMODE_SHIFT)

/*all int flag (PROX, INT_A, INT_P)*/
#define CR0_ALL_INT_CLEAR	0x0

#define CR0_ALL_INT_SHIFT       (1)
#define CR0_ALL_INT_MASK        (0x7 << CR0_ALL_INT_SHIFT)


/*indicator of object proximity detection*/
#define CR0_PROX_CLEAR		0x0

#define CR0_PROX_SHIFT       	(3)
#define CR0_PROX_MASK        	(0x1 << CR0_PROX_SHIFT)

/*interrupt status of proximity sensor*/
#define CR0_INTP_CLEAR		0x0

#define CR0_INTP_SHIFT       	(2)
#define CR0_INTP_MASK        	(0x1 << CR0_INTP_SHIFT)

/*interrupt status of ambient sensor*/
#define CR0_INTA_CLEAR		0x0

#define CR0_INTA_SHIFT       	(1)
#define CR0_INTA_MASK        	(0x1 << CR0_INTA_SHIFT)

/*Word mode enable*/
#define CR0_WORD_EN		0x1

#define CR0_WORD_SHIFT       	(0)
#define CR0_WORD_MASK        	(0x1 << CR0_WORD_SHIFT)


/*ALS fault queue depth for interrupt enent output*/
#define CR1_ALS_FQ_1		0x0 
#define CR1_ALS_FQ_4		0x1 
#define CR1_ALS_FQ_8		0x2
#define CR1_ALS_FQ_16		0x3
#define CR1_ALS_FQ_24		0x4
#define CR1_ALS_FQ_32		0x5
#define CR1_ALS_FQ_48		0x6
#define CR1_ALS_FQ_63		0x7

#define CR1_ALS_FQ_SHIFT       	(5)
#define CR1_ALS_FQ_MASK        	(0x7 << CR1_ALS_FQ_SHIFT)

/*resolution for ALS*/
#define CR1_ALS_RES_12BIT	0x0 
#define CR1_ALS_RES_14BIT	0x1 
#define CR1_ALS_RES_16BIT	0x2
#define CR1_ALS_RES_16BIT_2	0x3

#define CR1_ALS_RES_SHIFT      	(3)
#define CR1_ALS_RES_MASK       	(0x3 << CR1_ALS_RES_SHIFT)

/*sensing amplifier selection for ALS*/
#define CR1_ALS_GAIN_X1		0x0 
#define CR1_ALS_GAIN_X2		0x1 
#define CR1_ALS_GAIN_X4		0x2
#define CR1_ALS_GAIN_X8		0x3
#define CR1_ALS_GAIN_X16	0x4
#define CR1_ALS_GAIN_X32	0x5
#define CR1_ALS_GAIN_X64	0x6
#define CR1_ALS_GAIN_X128	0x7

#define CR1_ALS_GAIN_SHIFT      (0)
#define CR1_ALS_GAIN_MASK       (0x7 << CR1_ALS_GAIN_SHIFT)


/*PS fault queue depth for interrupt event output*/
#define CR2_PS_FQ_1		0x0 
#define CR2_PS_FQ_4		0x1 
#define CR2_PS_FQ_8		0x2
#define CR2_PS_FQ_15		0x3

#define CR2_PS_FQ_SHIFT      	(6)
#define CR2_PS_FQ_MASK       	(0x3 << CR2_PS_FQ_SHIFT)

/*interrupt type setting */
/*low active*/
#define CR2_INT_LEVEL		0x0 
/*low pulse*/
#define CR2_INT_PULSE		0x1 

#define CR2_INT_SHIFT      	(5)
#define CR2_INT_MASK       	(0x1 << CR2_INT_SHIFT)

/*resolution for PS*/
#define CR2_PS_RES_12		0x0 
#define CR2_PS_RES_14		0x1 
#define CR2_PS_RES_16		0x2
#define CR2_PS_RES_16_2		0x3

#define CR2_PS_RES_SHIFT      	(3)
#define CR2_PS_RES_MASK       	(0x3 << CR2_PS_RES_SHIFT)

/*sensing amplifier selection for PS*/
#define CR2_PS_GAIN_1		0x0 
#define CR2_PS_GAIN_2		0x1 
#define CR2_PS_GAIN_4		0x2
#define CR2_PS_GAIN_8		0x3
#define CR2_PS_GAIN_16		0x4
#define CR2_PS_GAIN_32		0x5
#define CR2_PS_GAIN_64		0x6
#define CR2_PS_GAIN_128		0x7

#define CR2_PS_GAIN_SHIFT      	(0)
#define CR2_PS_GAIN_MASK       	(0x7 << CR2_PS_GAIN_SHIFT)

/*wait-time slot selection*/
#define CR3_WAIT_SEL_0		0x0 
#define CR3_WAIT_SEL_4		0x1 
#define CR3_WAIT_SEL_8		0x2
#define CR3_WAIT_SEL_16		0x3

#define CR3_WAIT_SEL_SHIFT      (6)
#define CR3_WAIT_SEL_MASK       (0x3 << CR3_WAIT_SEL_SHIFT)

/*IR-LED drive peak current setting*/
#define CR3_LEDDR_12_5		0x0 
#define CR3_LEDDR_25		0x1 
#define CR3_LEDDR_50		0x2
#define CR3_LEDDR_100		0x3

#define CR3_LEDDR_SHIFT      	(4)
#define CR3_LEDDR_MASK       	(0x3 << CR3_LEDDR_SHIFT)

/*INT pin source selection*/
#define CR3_INT_SEL_BATH	0x0 
#define CR3_INT_SEL_ALS		0x1 
#define CR3_INT_SEL_PS		0x2
#define CR3_INT_SEL_PSAPP	0x3

#define CR3_INT_SEL_SHIFT      	(2)
#define CR3_INT_SEL_MASK       	(0x3 << CR3_INT_SEL_SHIFT)

/*software reset for register and core*/
#define CR3_SOFTRST_EN		0x1

#define CR3_SOFTRST_SHIFT      	(0)
#define CR3_SOFTRST_MASK       	(0x1 << CR3_SOFTRST_SHIFT)

/*modulation frequency of LED driver*/
#define CR10_FREQ_DIV2		0x0 
#define CR10_FREQ_DIV4		0x1 
#define CR10_FREQ_DIV8		0x2
#define CR10_FREQ_DIV16		0x3

#define CR10_FREQ_SHIFT      	(1)
#define CR10_FREQ_MASK       	(0x3 << CR10_FREQ_SHIFT)

/*50/60 Rejection enable*/
#define CR10_REJ_5060_DIS	0x00
#define CR10_REJ_5060_EN	0x01

#define CR10_REJ_5060_SHIFT     (0)
#define CR10_REJ_5060_MASK      (0x1 << CR10_REJ_5060_SHIFT)

#define us5152_NUM_CACHABLE_REGS 0x12
#endif
