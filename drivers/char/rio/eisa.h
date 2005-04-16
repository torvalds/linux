/*
** -----------------------------------------------------------------------------
**
**  Perle Specialix driver for Linux
**  Ported from existing RIO Driver for SCO sources.

 *
 *  (C) 1990 - 2000 Specialix International Ltd., Byfleet, Surrey, UK.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**	Module		: eisa.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 11:34:10
**	Retrieved	: 11/6/98 11:34:21
**
**  ident @(#)eisa.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef __rio_eisa_h__
#define __rio_eisa_h__

#ifdef SCCS_LABELS
#ifndef lint
static char *_eisa_h_sccs_ = "@(#)eisa.h	1.2";
#endif
#endif

/*
** things to do with the EISA bus
*/

#define RIO_EISA_STRING_ADDRESS 	0xfffd9	/* where EISA is stored */

#define	RIO_MAX_EISA_SLOTS		16	/* how many EISA slots? */

#define	RIO_EISA_IDENT			0x984D	/* Specialix */
#define	RIO_EISA_PRODUCT_CODE		0x14	/* Code 14 */
#define	RIO_EISA_ENABLE_BIT		0x01	/* To enable card */

#define	EISA_MEMORY_BASE_LO		0xC00	/* A16-A23 */
#define	EISA_MEMORY_BASE_HI		0xC01	/* A24-A31 */
#define	EISA_INTERRUPT_VEC		0xC02	/* see below */
#define	EISA_CONTROL_PORT		0xC02	/* see below */
#define	EISA_INTERRUPT_RESET		0xC03	/* read to clear IRQ */

#define	EISA_PRODUCT_IDENT_LO		0xC80	/* where RIO_EISA_IDENT is */
#define	EISA_PRODUCT_IDENT_HI		0xC81
#define	EISA_PRODUCT_NUMBER		0xC82   /* where PROD_CODE is */
#define	EISA_REVISION_NUMBER		0xC83	/* revision (1dp) */
#define	EISA_ENABLE			0xC84	/* set LSB to enable card */
#define	EISA_UNIQUE_NUM_0		0xC88	/* vomit */
#define	EISA_UNIQUE_NUM_1		0xC8A
#define	EISA_UNIQUE_NUM_2		0xC90	/* bit strangely arranged */
#define	EISA_UNIQUE_NUM_3		0xC92
#define	EISA_MANUF_YEAR			0xC98	/* when */
#define	EISA_MANUF_WEEK			0xC9A	/* more when */

#define	EISA_TP_BOOT_FROM_RAM	0x01
#define	EISA_TP_BOOT_FROM_LINK	0x00
#define	EISA_TP_FAST_LINKS	0x02
#define	EISA_TP_SLOW_LINKS	0x00
#define	EISA_TP_BUS_ENABLE	0x04
#define	EISA_TP_BUS_DISABLE	0x00
#define	EISA_TP_RUN		0x08
#define	EISA_TP_RESET		0x00
#define	EISA_POLLED		0x00
#define	EISA_IRQ_3		0x30
#define	EISA_IRQ_4		0x40
#define	EISA_IRQ_5		0x50
#define	EISA_IRQ_6		0x60
#define	EISA_IRQ_7		0x70
#define	EISA_IRQ_9		0x90
#define	EISA_IRQ_10		0xA0
#define	EISA_IRQ_11		0xB0
#define	EISA_IRQ_12		0xC0
#define	EISA_IRQ_14		0xE0
#define	EISA_IRQ_15		0xF0

#define	EISA_INTERRUPT_MASK	0xF0
#define	EISA_CONTROL_MASK	0x0F

#define	RIO_EISA_DEFAULT_MODE	EISA_TP_SLOW_LINKS

#define	RIOEisaToIvec(X)	(uchar )((uchar)((X) & EISA_INTERRUPT_MASK)>>4)

#define	INBZ(z,x)	inb(((z)<<12) | (x))
#define	OUTBZ(z,x,y)	outb((((z)<<12) | (x)), y)

#endif /* __rio_eisa_h__ */
