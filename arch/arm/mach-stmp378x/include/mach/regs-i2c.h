/*
 * stmp378x: I2C register definitions
 *
 * Copyright (c) 2008 Freescale Semiconductor
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#define REGS_I2C_BASE	(STMP3XXX_REGS_BASE + 0x58000)
#define REGS_I2C_PHYS	0x80058000
#define REGS_I2C_SIZE	0x2000

#define HW_I2C_CTRL0		0x0
#define BM_I2C_CTRL0_XFER_COUNT	0x0000FFFF
#define BP_I2C_CTRL0_XFER_COUNT	0
#define BM_I2C_CTRL0_DIRECTION	0x00010000
#define BM_I2C_CTRL0_MASTER_MODE	0x00020000
#define BM_I2C_CTRL0_PRE_SEND_START	0x00080000
#define BM_I2C_CTRL0_POST_SEND_STOP	0x00100000
#define BM_I2C_CTRL0_RETAIN_CLOCK	0x00200000
#define BM_I2C_CTRL0_SEND_NAK_ON_LAST	0x02000000
#define BM_I2C_CTRL0_CLKGATE	0x40000000
#define BM_I2C_CTRL0_SFTRST	0x80000000

#define HW_I2C_TIMING0		0x10

#define HW_I2C_TIMING1		0x20

#define HW_I2C_TIMING2		0x30

#define HW_I2C_CTRL1		0x40
#define BM_I2C_CTRL1_SLAVE_IRQ	0x00000001
#define BP_I2C_CTRL1_SLAVE_IRQ	0
#define BM_I2C_CTRL1_SLAVE_STOP_IRQ	0x00000002
#define BM_I2C_CTRL1_MASTER_LOSS_IRQ	0x00000004
#define BM_I2C_CTRL1_EARLY_TERM_IRQ	0x00000008
#define BM_I2C_CTRL1_OVERSIZE_XFER_TERM_IRQ	0x00000010
#define BM_I2C_CTRL1_NO_SLAVE_ACK_IRQ	0x00000020
#define BM_I2C_CTRL1_DATA_ENGINE_CMPLT_IRQ	0x00000040
#define BM_I2C_CTRL1_BUS_FREE_IRQ	0x00000080
#define BM_I2C_CTRL1_CLR_GOT_A_NAK	0x10000000

#define HW_I2C_VERSION		0x90
