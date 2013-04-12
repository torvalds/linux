/*
    comedi/drivers/plx9052.h
    Definitions for the PLX-9052 PCI interface chip

    Copyright (C) 2002 MEV Ltd. <http://www.mev.co.uk/>

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef _PLX9052_H_
#define _PLX9052_H_

/*
 * INTCSR - Interrupt Control/Status register
 */
#define PLX9052_INTCSR			0x4c
#define PLX9052_INTCSR_LI1ENAB		(1 << 0)  /* LI1 enabled */
#define PLX9052_INTCSR_LI1POL		(1 << 1)  /* LI1 active high */
#define PLX9052_INTCSR_LI1STAT		(1 << 2)  /* LI1 active */
#define PLX9052_INTCSR_LI2ENAB		(1 << 3)  /* LI2 enabled */
#define PLX9052_INTCSR_LI2POL		(1 << 4)  /* LI2 active high */
#define PLX9052_INTCSR_LI2STAT		(1 << 5)  /* LI2 active */
#define PLX9052_INTCSR_PCIENAB		(1 << 6)  /* PCIINT enabled */
#define PLX9052_INTCSR_SOFTINT		(1 << 7)  /* generate soft int */
#define PLX9052_INTCSR_LI1SEL		(1 << 8)  /* LI1 edge */
#define PLX9052_INTCSR_LI2SEL		(1 << 9)  /* LI2 edge */
#define PLX9052_INTCSR_LI1CLRINT	(1 << 10) /* LI1 clear int */
#define PLX9052_INTCSR_LI2CLRINT	(1 << 11) /* LI2 clear int */
#define PLX9052_INTCSR_ISAMODE		(1 << 12) /* ISA interface mode */

#endif /* _PLX9052_H_ */
