/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/ 
#ifndef __RTL8712_GP_BITDEF_H__
#define __RTL8712_GP_BITDEF_H__

//GPIO_CTRL
#define	_GPIO_MOD_MSK			0xFF000000
#define	_GPIO_MOD_SHT			24
#define	_GPIO_IO_SEL_MSK		0x00FF0000
#define	_GPIO_IO_SEL_SHT		16
#define	_GPIO_OUT_MSK			0x0000FF00
#define	_GPIO_OUT_SHT			8
#define	_GPIO_IN_MSK			0x000000FF
#define	_GPIO_IN_SHT			0

#endif	//__RTL8712_GP_BITDEF_H__



//SYS_PINMUX_CFG
#define	_GPIOSEL_MSK			0x0003
#define	_GPIOSEL_SHT			0

//LED_CFG
#define _LED1SV				BIT(7)
#define _LED1CM_MSK			0x0070
#define _LED1CM_SHT			4
#define _LED0SV				BIT(3)
#define _LED0CM_MSK			0x0007
#define _LED0CM_SHT			0

//PHY_REG
#define _HST_RDRDY_SHT			0
#define _HST_RDRDY_MSK			0xFF
#define _HST_RDRDY			BIT(_HST_RDRDY_SHT)
#define _CPU_WTBUSY_SHT			1
#define _CPU_WTBUSY_MSK			0xFF
#define _CPU_WTBUSY			BIT(_CPU_WTBUSY_SHT)

//

// 11. General Purpose Registers   (Offset: 0x02E0 - 0x02FF)

//       8192S GPIO Config Setting (offset 0x2F1, 1 byte)

//----------------------------------------------------------------------------

#define            GPIOMUX_EN                 BIT(3) // When this bit is set to "1", GPIO PINs will switch to MAC GPIO Function

#define            GPIOSEL_GPIO                0      // UART or JTAG or pure GPIO

#define            GPIOSEL_PHYDBG           1      // PHYDBG

#define            GPIOSEL_BT                    2      // BT_coex

#define            GPIOSEL_WLANDBG		 3      // WLANDBG

#define            GPIOSEL_GPIO_MASK     ~(BIT(0)|BIT(1))


// HW Readio OFF switch (GPIO BIT)

#define            HAL_8192S_HW_GPIO_OFF_BIT    BIT(3)

#define            HAL_8192S_HW_GPIO_OFF_MASK        0xF7

#define            HAL_8192S_HW_GPIO_WPS_BIT   BIT(4)

