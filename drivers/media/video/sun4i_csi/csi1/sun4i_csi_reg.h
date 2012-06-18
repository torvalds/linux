/*
 * drivers/media/video/sun4i_csi/csi1/sun4i_csi_reg.h
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

/*
 * Sun4i Camera register define
 * Author:raymonxiu
*/
#ifndef _SUN4I_CSI_REG_H_
#define _SUN4I_CSI_REG_H_

#define  W(addr, val)   writel(val, addr)
#define  R(addr)        readl(addr)
#define  S(addr,bit)	writel(readl(addr)|bit,addr)
#define  C(addr,bit)	writel(readl(addr)&(~bit),addr)

#define CSI0_REGS_BASE        0x01c09000
#define CSI1_REGS_BASE        0X01c1D000
#define CSI0_REG_SIZE 				0x1000
#define CSI1_REG_SIZE 				0x1000

#define CSI_REG_EN           (0x00)
#define CSI_REG_CONF         (0x04)
#define CSI_REG_CTRL         (0x08)
#define CSI_REG_SCALE        (0x0C)
#define CSI_REG_BUF_0_A      (0x10)
#define CSI_REG_BUF_0_B      (0x14)
#define CSI_REG_BUF_1_A      (0x18)
#define CSI_REG_BUF_1_B      (0x1C)
#define CSI_REG_BUF_2_A      (0x20)
#define CSI_REG_BUF_2_B      (0x24)
#define CSI_REG_BUF_CTRL     (0x28)
#define CSI_REG_STATUS       (0x2C)
#define CSI_REG_INT_EN       (0x30)
#define CSI_REG_INT_STATUS   (0x34)
#define CSI_REG_RESIZE_H     (0x40)
#define CSI_REG_RESIZE_V     (0x44)
#define CSI_REG_BUF_LENGTH   (0x48)

#endif  /* _CSI_H_ */
