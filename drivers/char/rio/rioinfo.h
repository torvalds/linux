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
**	Module		: rioinfo.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 14:07:49
**	Retrieved	: 11/6/98 14:07:50
**
**  ident @(#)rioinfo.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef __rioinfo_h
#define __rioinfo_h

#ifdef SCCS_LABELS
static char *_rioinfo_h_sccs_ = "@(#)rioinfo.h	1.2";
#endif

/*
** Host card data structure
*/
struct RioHostInfo {
	long	location;	/* RIO Card Base I/O address */
	long	vector;		/* RIO Card IRQ vector */
	int	bus;		/* ISA/EISA/MCA/PCI */
	int	mode;		/* pointer to host mode - INTERRUPT / POLLED */
	struct old_sgttyb
		* Sg;		/* pointer to default term characteristics */
};


/* Mode in rio device info */
#define INTERRUPTED_MODE	0x01		/* Interrupt is generated */
#define POLLED_MODE		0x02		/* No interrupt */
#define AUTO_MODE		0x03		/* Auto mode */

#define WORD_ACCESS_MODE	0x10		/* Word Access Mode */
#define BYTE_ACCESS_MODE	0x20		/* Byte Access Mode */


/* Bus type that RIO supports */
#define ISA_BUS			0x01		/* The card is ISA */
#define EISA_BUS		0x02		/* The card is EISA */
#define MCA_BUS			0x04		/* The card is MCA */
#define PCI_BUS			0x08		/* The card is PCI */

/*
** 11.11.1998 ARG - ESIL ???? part fix
** Moved definition for 'CHAN' here from rioinfo.c (it is now
** called 'DEF_TERM_CHARACTERISTICS').
*/

#define DEF_TERM_CHARACTERISTICS \
{ \
	B19200, B19200,				/* input and output speed */ \
	'H' - '@',				/* erase char */ \
	-1,					/* 2nd erase char */ \
	'U' - '@',				/* kill char */ \
	ECHO | CRMOD,				/* mode */ \
	'C' - '@',				/* interrupt character */ \
	'\\' - '@',				/* quit char */ \
	'Q' - '@',				/* start char */ \
	'S' - '@',				/* stop char */ \
	'D' - '@',				/* EOF */ \
	-1,					/* brk */ \
	(LCRTBS | LCRTERA | LCRTKIL | LCTLECH),	/* local mode word */ \
	'Z' - '@',				/* process stop */ \
	'Y' - '@',				/* delayed stop */ \
	'R' - '@',				/* reprint line */ \
	'O' - '@',				/* flush output */ \
	'W' - '@',				/* word erase */ \
	'V' - '@'				/* literal next char */ \
}

#endif /* __rioinfo_h */
