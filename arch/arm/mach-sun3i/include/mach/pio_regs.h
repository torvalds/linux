/*
 * arch/arm/mach-sun3i/include/mach/pio_regs.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __PIO_H__
#define __PIO_H__


/*
*********************************************************************************************************
*   PIO Controller define          < maintained by Jerry >
*********************************************************************************************************
*/
#define PA_PIO_BASE		PIO_BASE
#define VA_PIO_BASE		IO_ADDRESS(PA_PIO_BASE)



#define			PIOC_READ_REG(reg)		     (reg)
#define			PIOC_WRITE_REG(reg, val)     ((reg)=(val))

#define         PIOC_INPUT_EN                0xf0
#define         PIOC_OUTPUT_EN               0xf1

#define         PIOC_INPUT                   0x05
#define         PIOC_OUTPUT                  0x0A
#define         PIOC_DISABLE                 0
#define         PIOC_ENABLE                  1

#define         PIOC_PULL_ENABLE             1
#define         PIOC_PULL_DISABLE            0

#define         PIOC_INT0                    (1 << 0)
#define         PIOC_INT1                    (1 << 1)
#define         PIOC_INT2                    (1 << 2)
#define         PIOC_INT3                    (1 << 3)
#define         PIOC_INT4                    (1 << 4)
#define         PIOC_INT5                    (1 << 5)
#define         PIOC_INT6                    (1 << 6)
#define         PIOC_INT7                    (1 << 7)

#define         PIOC_INT_POS_EDGE            0xff00        //上升沿触发
#define         PIOC_INT_NES_EDGE            0xff01        //下降沿触发
#define         PIOC_INT_HIGH_LVL            0xff02        //高电平触发
#define         PIOC_INT_LOW_LVL             0xff03        //低电平触发

  /* offset */
#define PIOC_REG_o_A_CFG0           0x00
#define PIOC_REG_o_A_CFG1           0x04
#define PIOC_REG_o_A_CFG2           0x08
#define PIOC_REG_o_A_DATA           0x0C
#define PIOC_REG_o_A_MULDRV0        0x10
#define PIOC_REG_o_A_MULDRV1        0x14
#define PIOC_REG_o_A_PULL0          0x18
#define PIOC_REG_o_A_PULL1          0x1C

#define PIOC_REG_o_B_CFG0           0x20
#define PIOC_REG_o_B_CFG1           0x24
#define PIOC_REG_o_B_CFG2           0x28
#define PIOC_REG_o_B_DATA           0x2C
#define PIOC_REG_o_B_MULDRV0        0x30
#define PIOC_REG_o_B_MULDRV1        0x34
#define PIOC_REG_o_B_PULL0          0x38
#define PIOC_REG_o_B_PULL1          0x3C

#define PIOC_REG_o_C_CFG0           0x40
#define PIOC_REG_o_C_CFG1           0x44
#define PIOC_REG_o_C_CFG2           0x48
#define PIOC_REG_o_C_DATA           0x4C
#define PIOC_REG_o_C_MULDRV0        0x50
#define PIOC_REG_o_C_MULDRV1        0x54
#define PIOC_REG_o_C_PULL0          0x58
#define PIOC_REG_o_C_PULL1          0x5C

#define PIOC_REG_o_D_CFG0           0x60
#define PIOC_REG_o_D_CFG1           0x64
#define PIOC_REG_o_D_CFG2           0x68
#define PIOC_REG_o_D_CFG3           0x6C
#define PIOC_REG_o_D_DATA           0x70
#define PIOC_REG_o_D_MULDRV0        0x74
#define PIOC_REG_o_D_MULDRV1        0x78
#define PIOC_REG_o_D_PULL0          0x7C
#define PIOC_REG_o_D_PULL1          0x80

#define PIOC_REG_o_E_CFG0           0x84
#define PIOC_REG_o_E_CFG1           0x88
#define PIOC_REG_o_E_DATA           0x8C
#define PIOC_REG_o_E_MULDRV0        0x90
#define PIOC_REG_o_E_PULL0          0x94

#define PIOC_REG_o_F_MULDRV0        0x98
#define PIOC_REG_o_F_PULL0          0x9C

#define PIOC_REG_o_INT              0xA0
#endif

