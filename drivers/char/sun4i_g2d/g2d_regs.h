/* g2d_regs.h
 *
 * Copyright (c)	2011 xxxx Electronics
 *					2011 Yupu Tang
 *
 * @ F23 G2D driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA02111-1307USA
 */

#ifndef __G2D_MIXER_REGS_H
#define __G2D_MIXER_REGS_H

/*
	Graphics 2D General Registers
*/
#define G2D_BASE_ADDR			(0x01e80000)/* Base Address			*/
#define G2D_CONTROL_REG			(0x00)		/* Control register			*/
#define G2D_STATUS_REG			(0x04)		/* Status register		*/
#define G2D_SCAN_ORDER_REG		(0x08)		/* DMA scan order control register		*/

/*
	Graphics 2D Input Address Parameter Setting Registers
*/
#define G2D_DMA_HADDR_REG		(0x0c)		/* Input DMA high 4 bits start addr register	*/
#define G2D_DMA0_LADDR_REG		(0x10)		/* Input DMA0 low 32 bits start addr register	*/
#define G2D_DMA1_LADDR_REG		(0x14)		/* Input DMA1 low 32 bits start addr register	*/
#define G2D_DMA2_LADDR_REG		(0x18)		/* Input DMA2 low 32 bits start addr register	*/
#define G2D_DMA3_LADDR_REG		(0x1c)		/* Input DMA3 low 32 bits start addr register	*/

/*
	Graphics 2D Input Linewidth Buffer Parameter Setting Registers
*/
#define G2D_DMA0_STRIDE_REG		(0x20)		/* Input DMA0 line stride register	*/
#define G2D_DMA1_STRIDE_REG		(0x24)		/* Input DMA1 line stride register	*/
#define G2D_DMA2_STRIDE_REG		(0x28)		/* Input DMA2 line stride register	*/
#define G2D_DMA3_STRIDE_REG		(0x2c)		/* Input DMA3 line stride register	*/

#define G2D_DMA0_SIZE_REG		(0x30)		/* Input DMA0 memory block size register	*/
#define G2D_DMA1_SIZE_REG		(0x34)		/* Input DMA1 memory block size register	*/
#define G2D_DMA2_SIZE_REG		(0x38)		/* Input DMA2 memory block size register	*/
#define G2D_DMA3_SIZE_REG		(0x3c)		/* Input DMA3 memory block size register	*/
#define G2D_DMA0_COOR_REG		(0x40)		/* Input DMA0 memory block coordinate register	*/
#define G2D_DMA1_COOR_REG		(0x44)		/* Input DMA1 memory block coordinate register	*/
#define G2D_DMA2_COOR_REG		(0x48)		/* Input DMA2 memory block coordinate register	*/
#define G2D_DMA3_COOR_REG		(0x4c)		/* Input DMA3 memory block coordinate register	*/

#define G2D_DMA0_CONTROL_REG	(0x50)		/* Input DMA0 control register	*/
#define G2D_DMA1_CONTROL_REG	(0x54)		/* Input DMA1 control register	*/
#define G2D_DMA2_CONTROL_REG	(0x58)		/* Input DMA2 control register	*/
#define G2D_DMA3_CONTROL_REG	(0x5c)		/* Input DMA3 control register	*/

#define G2D_DMA0_FILLCOLOR_REG	(0x60)		/* Input DMA0 fillcolor register	*/
#define G2D_DMA1_FILLCOLOR_REG	(0x64)		/* Input DMA1 fillcolor register	*/
#define G2D_DMA2_FILLCOLOR_REG	(0x68)		/* Input DMA2 fillcolor register	*/
#define G2D_DMA3_FILLCOLOR_REG	(0x6c)		/* Input DMA3 fillcolor register	*/

#define G2D_CSC0_CONTROL_REG	(0x74)		/* Color space converter0 control register	*/
#define G2D_CSC1_CONTROL_REG	(0x78)		/* Color space converter1 control register	*/

#define G2D_SCALER_CONTROL_REG	(0x80)		/* Scaler control register	*/
#define G2D_SCALER_SIZE_REG		(0x84)		/* Scaler output size control register	*/
#define G2D_SCALER_HFACTOR_REG	(0x88)		/* Scaler horizontal scaling factor register	*/
#define G2D_SCALER_VFACTOR_REG	(0x8c)		/* Scaler vertical scaling factor register	*/
#define G2D_SCALER_HPHASE_REG	(0x90)		/* Scaler horizontal start phase register	*/
#define G2D_SCALER_VPHASE_REG	(0x94)		/* Scaler vertical start phase register	*/

#define G2D_ROP_CONTROL_REG		(0xb0)		/* Rop control register	*/
#define G2D_ROP_INDEX0_REG		(0xb8)		/* Rop index0 control table setting register	*/
#define G2D_ROP_INDEX1_REG		(0xbc)		/* Rop index1 control table setting register	*/

#define G2D_CK_CONTROL_REG		(0xc0)		/* Colorkey/alpha control register	*/
#define G2D_CK_MINCOLOR_REG		(0xc4)		/* Colorkey min color control register	*/
#define G2D_CK_MAXCOLOR_REG		(0xc8)		/* Colorkey max color control register	*/
#define G2D_ROP_FILLCOLOR_REG	(0xcc)		/* Rop output fillcolor setting register	*/
#define G2D_CSC2_CONTROL_REG	(0xd0)		/* Color space converter2 control register	*/
#define G2D_OUTPUT_CONTROL_REG	(0xe0)		/* Output control register	*/
#define G2D_OUTPUT_SIZE_REG		(0xe8)		/* Output size register	*/
#define G2D_OUTPUT_HADDR_REG	(0xec)		/* Output high 4 bits address control register	*/
#define G2D_OUTPUT0_LADDR_REG	(0xf0)		/* Output low 32 bits address control register	*/
#define G2D_OUTPUT1_LADDR_REG	(0xf4)		/* Output low 32 bits address control register	*/
#define G2D_OUTPUT2_LADDR_REG	(0xf8)		/* Output low 32 bits address control register	*/

#define G2D_OUTPUT0_STRIDE_REG	(0x100)		/* Output channel0 line stride control register	*/
#define G2D_OUTPUT1_STRIDE_REG	(0x104)		/* Output channel1 line stride control register	*/
#define G2D_OUTPUT2_STRIDE_REG	(0x108)		/* Output channel2 line stride control register	*/
#define G2D_OALPHA_CONTROL_REG	(0x120)		/* Output alpha control register	*/

#define G2D_CSC01_ADDR_REG		(0x180)		/* CSC0/1 coefficient/constant start addr register(0x180-0x1ac)	*/
#define G2D_CSC2_ADDR_REG		(0x1c0)		/* CSC2 coefficient/constant start addr register(0x1c0-0x1ec)	*/
#define G2D_SCALER_HFILTER_REG	(0x200)		/* Scaling horizontal filtering coefficient ram block register(0x200-0x27c)	*/
#define G2D_SCALER_VFILTER_REG	(0x280)		/* Scaling vertical filtering coefficient ram block register(0x280-0x2fc)	*/
#define G2D_PALETTE_TAB_REG		(0x400)		/* Scaling horizontal filtering coefficient ram block register(0x400-0x7fc)	*/

/* Mixer start control */
#define G2D_ENABLE_CTRL			(1<<0)
#define G2D_DISABLE_CTRL		(0<<0)
#define G2D_START_CTRL			(1<<1)
#define G2D_STOP_CTRL			(0<<1)
#define G2D_FINISH_IRQ_ENABLE	(1<<8)
#define G2D_ERROR_IRQ_ENABLE	(1<<9)

/* Scan mode select */
#define G2D_TOP_DOWN_LR		(0<<8)
#define G2D_TOP_DOWN_RL		(1<<8)
#define G2D_DOWN_TOP_LR		(2<<8)
#define G2D_DOWN_TOP_RL		(3<<8)

/* Input DMA setting */
#define G2D_FILL_ENABLE		(1<<16)
#define G2D_FILL_DISABLE	(0<<16)

/* rotation/mirror mode */
#define G2D_TRANSFORM_COPY			(0<<4)
#define G2D_TRANSFORM_ROTATE90		(5<<4)
#define G2D_TRANSFORM_ROTATE180		(3<<4)
#define G2D_TRANSFORM_ROTATE270		(6<<4)
#define G2D_TRANSFORM_HFLIP			(1<<4)
#define G2D_TRANSFORM_VFLIP			(2<<4)
#define G2D_TRANSFORM_MIRROR45		(7<<4)
#define G2D_TRANSFORM_MIRROR135		(4<<4)

/* Work Mode Select */
#define G2D_NORMAL_MODE		(0<<1)
#define G2D_PALETTE_MODE	(1<<1)
#define G2D_IDMA_ENABLE		(1<<0)
#define G2D_IDMA_DISABLE	(0<<0)

/* Scaler Control Select */
#define G2D_SCALER_DISABLE	(0<<0)
#define G2D_SCALER_ENABLE	(1<<0)
#define G2D_SCALER_4TAP4	(0<<4)
#define G2D_SCALER_2TAP4	(1<<4)
#define G2D_SCALER_1TAP4	(2<<4)

#define get_bvalue(n)	(*((volatile __u8 *)(n)))          /* byte input */
#define put_bvalue(n,c)	(*((volatile __u8 *)(n)) = (c))    /* byte output */
#define get_hvalue(n)	(*((volatile __u16 *)(n)))         /* half word input */
#define put_hvalue(n,c)	(*((volatile __u16 *)(n)) = (c))   /* half word output */
#define get_wvalue(n)	(*((volatile __u32 *)(n)))          /* word input */
#define put_wvalue(n,c)	(*((volatile __u32 *)(n)) = (c))    /* word output */

#endif /* __G2D_MIXER_REGS_H */

