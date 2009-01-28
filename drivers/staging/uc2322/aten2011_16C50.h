/*
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


#if !defined(_16C50_H)
#define	_16C50_H

/*************************************
 * Bit definitions for each register *
 *************************************/
#define LCR_BITS_5		0x00	/* 5 bits/char */
#define LCR_BITS_6		0x01	/* 6 bits/char */
#define LCR_BITS_7		0x02	/* 7 bits/char */
#define LCR_BITS_8		0x03	/* 8 bits/char */
#define LCR_BITS_MASK		0x03	/* Mask for bits/char field */

#define LCR_STOP_1		0x00	/* 1 stop bit */
#define LCR_STOP_1_5		0x04	/* 1.5 stop bits (if 5   bits/char) */
#define LCR_STOP_2		0x04	/* 2 stop bits   (if 6-8 bits/char) */
#define LCR_STOP_MASK		0x04	/* Mask for stop bits field */

#define LCR_PAR_NONE		0x00	/* No parity */
#define LCR_PAR_ODD		0x08	/* Odd parity */
#define LCR_PAR_EVEN		0x18	/* Even parity */
#define LCR_PAR_MARK		0x28	/* Force parity bit to 1 */
#define LCR_PAR_SPACE		0x38	/* Force parity bit to 0 */
#define LCR_PAR_MASK		0x38	/* Mask for parity field */

#define LCR_SET_BREAK		0x40	/* Set Break condition */
#define LCR_DL_ENABLE		0x80	/* Enable access to divisor latch */

#define MCR_DTR			0x01	/* Assert DTR */
#define MCR_RTS			0x02	/* Assert RTS */
#define MCR_OUT1		0x04	/* Loopback only: Sets state of RI */
#define MCR_MASTER_IE		0x08	/* Enable interrupt outputs */
#define MCR_LOOPBACK		0x10	/* Set internal (digital) loopback mode */
#define MCR_XON_ANY		0x20	/* Enable any char to exit XOFF mode */

#define ATEN2011_MSR_CTS	0x10	/* Current state of CTS */
#define ATEN2011_MSR_DSR	0x20	/* Current state of DSR */
#define ATEN2011_MSR_RI		0x40	/* Current state of RI */
#define ATEN2011_MSR_CD		0x80	/* Current state of CD */

#endif	/* if !defined(_16C50_H) */

