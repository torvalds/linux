/*drivers/cir/bu92747guw_cir.h - driver for bu92747guw
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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
 
#ifndef _DRIVERS_CIR_BU92747GUW_CIR_H
#define _DRIVERS_CIR_BU92747GUW_CIR_H

#include <linux/ioctl.h>

/* irda registers addr */
#define REG_TXD_ADDR		0
#define REG_RXD_ADDR		0
#define REG_IER_ADDR		2
#define REG_EIR_ADDR		4
#define REG_MCR_ADDR		6
#define REG_PWR_FIT_ADDR	8
#define REG_TRCR_ADDR		10
#define REG_FTLV_ADDR		12
#define REG_FLV_ADDR		14
#define REG_FLVII_ADDR		16
#define REG_FLVIII_ADDR		18
#define REG_FLVIV_ADDR		20
#define REG_TRCRII_ADDR		22
#define REG_TXEC_ADDR		24
#define REG_WREC_ADDR		26

//remote reg
#define REG_SETTING0    0x00
#define REG_SETTING1    0x01

#define REG_BASE_CLOCK  0X02

#define REG_CLO1        0x03
#define REG_CLO0        0x04
#define REG_CHI1        0x05
#define REG_CHI0        0x06

#define REG_HLO1        0x07
#define REG_HLO0        0x08
#define REG_HHI1        0x09
#define REG_HHI0        0x0a

#define REG_D0LO1       0x0b
#define REG_D0LO0       0x0c
#define REG_D0HI1       0x0d
#define REG_D0HI0       0x0e

#define REG_D1LO1       0x0f
#define REG_D1LO0       0x10
#define REG_D1HI1       0x11
#define REG_D1HI0       0x12

#define REG_ENDLEN1     0x13
#define REG_ENDLEN0     0x14
#define REG_BITLEN      0x15
#define REG_FRMLEN1     0x16
#define REG_REMLEN0     0x17

#define REG_OUT0        0x18
#define REG_OUT1        0x19
#define REG_OUT2        0x1A
#define REG_OUT3        0x1B
#define REG_OUT4        0x1C
#define REG_OUT5        0x1D
#define REG_OUT6        0x1E
#define REG_OUT7        0x1F
#define REG_OUT8        0x20
#define REG_OUT9        0x21
#define REG_OUT10       0x22
#define REG_OUT11       0x23
#define REG_OUT12       0x24
#define REG_OUT13       0x25
#define REG_OUT14       0x26
#define REG_OUT15       0x27

#define REG_IRQC        0x28
#define REG_SEND        0x29
#define REG_RST         0x2a
#define REG_REGS        0X2b

//
#define REG0_OPM        (1<<5)
#define REG0_DIVS       (1<<4)
#define REG0_IRQE       (1<<3)
#define REG0_INV1       (1<<2)
#define REG0_INV0       (1<<1)
#define REG0_PWR        (1<<0)

#define REG1_FRMB       (1<<5)
#define REG1_FRME       (1<<4)
#define REG1_RPT        (1<<0)

#define ul64 unsigned long long 
struct rk29_cir_struct_info {
	u16  carry_high;                // carry_high
	u16  carry_low;        // carry_low	
	
	s32  repeat;         // 是否是 重复帧
	u8   inv;                 //00 01 10 11   Lsb->inv0 
	u8 frame_bit_len;           // 命令帧有效位数
	
	u16 stop_bit_interval;         //  period of end part  NEC-560us
	
	ul64 frame;               //  命令帧  LSB->MSB
	u32 frame_interval;       //   frame interval   NEC-108000s
	
	u16 head_burst_time;      //pan-3360us(USA) or 3680(Europe)、       nec-9000、 sharp-0、 sony-600us   
	u16 head_space_time;            //pan-6720us(USA) or 7360(Europe)、 nec-450us、sharp-0、 sony-3000us   

	u16	logic_high_burst_time; //pan-840us(USA) or 508(Europe)、   nec-560、   sharp-320、  sony-1200us    		 // logic 1 burst time	
	u16	logic_high_space_time; 			 //pan-3360us(USA) or 3680(Europe)、 nec-1690、  sharp-2000、 sony-1800  // logic 1 time

	u16	logic_low_burst_time; //pan-840us(USA) or 508(Europe)、          nec-560、   sharp-320、  sony-600us	  	// logic 0 burst time	
	u16	logic_low_space_time; 			 //pan-1680us(USA) or 1816(Europe)、 nec-560、  sharp-1000、 sony-1200 // logic 0 time

};

#define CIR_FRAME_SIZE sizeof(struct rk29_cir_struct_info)

#define BU92747IO	'B'

/* IOCTLs for BU92747*/
/*
#define BU92747_IOCTL_STOP		            _IO(BU92747IO, 0x01)
#define BU92747_IOCTL_START		            _IO(BU92747IO, 0x02)
#define BU92747_IOCTL_SET_FORMAT            _IOW(BU92747IO, 0x04, char[CIR_FRAME_SIZE])
#define BU92747_IOCTL_SEND_DATA             _IOW(BU92747IO, 0x08, char[CIR_FRAME_SIZE])
*/

#define BU92747_IOCTL_STOP		         _IO(BU92747IO, 0x01)
#define BU92747_IOCTL_START		         _IO(BU92747IO, 0x02)
#define BU92747_IOCTL_CARRIER            _IOW(BU92747IO, 0x04, char[CIR_FRAME_SIZE])
#define BU92747_IOCTL_DATA               _IOW(BU92747IO, 0x06, char[CIR_FRAME_SIZE])
#define BU92747_IOCTL_PULSE              _IOW(BU92747IO, 0x08, char[CIR_FRAME_SIZE])
#define BU92747_IOCTL_REPEAT             _IOW(BU92747IO, 0x0A, char[CIR_FRAME_SIZE])
#define BU92747_IOCTL_DURATION           _IOW(BU92747IO, 0x0C, char[CIR_FRAME_SIZE])
#define BU92747_IOCTL_PARAMETER          _IOW(BU92747IO, 0x0E, char[CIR_FRAME_SIZE])
#define BU92747_IOCTL_FORMATE            _IOW(BU92747IO, 0x0F, char[CIR_FRAME_SIZE])


/*status*/
#define BU92747_STOP		4
#define BU92747_BUSY		3
#define BU92747_SUSPEND     2
#define BU92747_OPEN        1
#define BU92747_CLOSE       0

struct bu92747guw_platform_data {
    u32 intr_pin;
    int (*iomux_init)(void);
    int (*iomux_deinit)(void);
    int (*cir_pwr_ctl)(int en);
};

#endif  /*_DRIVERS_CIR_BU92747GUW_CIR_H*/
