/*
 * File:	linux/drivers/serial/bfin_sport_uart.h
 *
 * Based on:	include/asm-blackfin/mach-533/bfin_serial_5xx.h
 * Author:	Roy Huang <roy.huang>analog.com>
 *
 * Created:	Nov 22, 2006
 * Copyright:	(C) Analog Device Inc.
 * Description: this driver enable SPORTs on Blackfin emulate UART.
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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */


#define OFFSET_TCR1		0x00	/* Transmit Configuration 1 Register */
#define OFFSET_TCR2		0x04	/* Transmit Configuration 2 Register */
#define OFFSET_TCLKDIV		0x08	/* Transmit Serial Clock Divider Register */
#define OFFSET_TFSDIV		0x0C	/* Transmit Frame Sync Divider Register */
#define OFFSET_TX		0x10	/* Transmit Data Register		*/
#define OFFSET_RX		0x18	/* Receive Data Register		*/
#define OFFSET_RCR1		0x20	/* Receive Configuration 1 Register	*/
#define OFFSET_RCR2		0x24	/* Receive Configuration 2 Register	*/
#define OFFSET_RCLKDIV		0x28	/* Receive Serial Clock Divider Register */
#define OFFSET_RFSDIV		0x2c	/* Receive Frame Sync Divider Register */
#define OFFSET_STAT		0x30	/* Status Register			*/

#define SPORT_GET_TCR1(sport)		bfin_read16(((sport)->port.membase + OFFSET_TCR1))
#define SPORT_GET_TCR2(sport)		bfin_read16(((sport)->port.membase + OFFSET_TCR2))
#define SPORT_GET_TCLKDIV(sport)	bfin_read16(((sport)->port.membase + OFFSET_TCLKDIV))
#define SPORT_GET_TFSDIV(sport)		bfin_read16(((sport)->port.membase + OFFSET_TFSDIV))
#define SPORT_GET_TX(sport)		bfin_read16(((sport)->port.membase + OFFSET_TX))
#define SPORT_GET_RX(sport)		bfin_read16(((sport)->port.membase + OFFSET_RX))
#define SPORT_GET_RX32(sport)		bfin_read32(((sport)->port.membase + OFFSET_RX))
#define SPORT_GET_RCR1(sport)		bfin_read16(((sport)->port.membase + OFFSET_RCR1))
#define SPORT_GET_RCR2(sport)		bfin_read16(((sport)->port.membase + OFFSET_RCR2))
#define SPORT_GET_RCLKDIV(sport)	bfin_read16(((sport)->port.membase + OFFSET_RCLKDIV))
#define SPORT_GET_RFSDIV(sport)		bfin_read16(((sport)->port.membase + OFFSET_RFSDIV))
#define SPORT_GET_STAT(sport)		bfin_read16(((sport)->port.membase + OFFSET_STAT))

#define SPORT_PUT_TCR1(sport, v)	bfin_write16(((sport)->port.membase + OFFSET_TCR1), v)
#define SPORT_PUT_TCR2(sport, v)	bfin_write16(((sport)->port.membase + OFFSET_TCR2), v)
#define SPORT_PUT_TCLKDIV(sport, v)	bfin_write16(((sport)->port.membase + OFFSET_TCLKDIV), v)
#define SPORT_PUT_TFSDIV(sport, v)	bfin_write16(((sport)->port.membase + OFFSET_TFSDIV), v)
#define SPORT_PUT_TX(sport, v)		bfin_write16(((sport)->port.membase + OFFSET_TX), v)
#define SPORT_PUT_RX(sport, v)		bfin_write16(((sport)->port.membase + OFFSET_RX), v)
#define SPORT_PUT_RCR1(sport, v)	bfin_write16(((sport)->port.membase + OFFSET_RCR1), v)
#define SPORT_PUT_RCR2(sport, v)	bfin_write16(((sport)->port.membase + OFFSET_RCR2), v)
#define SPORT_PUT_RCLKDIV(sport, v)	bfin_write16(((sport)->port.membase + OFFSET_RCLKDIV), v)
#define SPORT_PUT_RFSDIV(sport, v)	bfin_write16(((sport)->port.membase + OFFSET_RFSDIV), v)
#define SPORT_PUT_STAT(sport, v)	bfin_write16(((sport)->port.membase + OFFSET_STAT), v)
