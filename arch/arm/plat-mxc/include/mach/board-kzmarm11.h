/*
 *  Copyright (C) 2009  Yoichi Yuasa <yuasa@linux-mips.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#ifndef __ARM_ARCH_BOARD_KZM_ARM11_H
#define __ARM_ARCH_BOARD_KZM_ARM11_H

/*
 *  KZM-ARM11-01 Board Control Registers on FPGA
 */
#define KZM_ARM11_CTL1		(MX31_CS4_BASE_ADDR + 0x1000)
#define KZM_ARM11_CTL2		(MX31_CS4_BASE_ADDR + 0x1001)
#define KZM_ARM11_RSW1		(MX31_CS4_BASE_ADDR + 0x1002)
#define KZM_ARM11_BACK_LIGHT	(MX31_CS4_BASE_ADDR + 0x1004)
#define KZM_ARM11_FPGA_REV	(MX31_CS4_BASE_ADDR + 0x1008)
#define KZM_ARM11_7SEG_LED	(MX31_CS4_BASE_ADDR + 0x1010)
#define KZM_ARM11_LEDS		(MX31_CS4_BASE_ADDR + 0x1020)
#define KZM_ARM11_DIPSW2	(MX31_CS4_BASE_ADDR + 0x1003)

/*
 * External UART for touch panel on FPGA
 */
#define KZM_ARM11_16550		(MX31_CS4_BASE_ADDR + 0x1050)

#endif /* __ARM_ARCH_BOARD_KZM_ARM11_H */

