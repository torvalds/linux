/*
    NetWinder Floating Point Emulator
    (c) Rebel.com, 1998-1999

    Direct questions, comments to Scott Bambrough <scottb@netwinder.org>

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

#ifndef __FPSR_H__
#define __FPSR_H__

/*
The FPSR is a 32 bit register consisting of 4 parts, each exactly
one byte.

	SYSTEM ID
	EXCEPTION TRAP ENABLE BYTE
	SYSTEM CONTROL BYTE
	CUMULATIVE EXCEPTION FLAGS BYTE
	
The FPCR is a 32 bit register consisting of bit flags.
*/

/* SYSTEM ID
------------
Note: the system id byte is read only  */

typedef unsigned int FPSR;  /* type for floating point status register */
typedef unsigned int FPCR;  /* type for floating point control register */

#define MASK_SYSID		0xff000000
#define BIT_HARDWARE		0x80000000
#define FP_EMULATOR		0x01000000	/* System ID for emulator */ 
#define FP_ACCELERATOR		0x81000000	/* System ID for FPA11 */

/* EXCEPTION TRAP ENABLE BYTE
----------------------------- */

#define MASK_TRAP_ENABLE	0x00ff0000
#define MASK_TRAP_ENABLE_STRICT	0x001f0000
#define BIT_IXE		0x00100000   /* inexact exception enable */
#define BIT_UFE		0x00080000   /* underflow exception enable */
#define BIT_OFE		0x00040000   /* overflow exception enable */
#define BIT_DZE		0x00020000   /* divide by zero exception enable */
#define BIT_IOE		0x00010000   /* invalid operation exception enable */

/* SYSTEM CONTROL BYTE
---------------------- */

#define MASK_SYSTEM_CONTROL	0x0000ff00
#define MASK_TRAP_STRICT	0x00001f00

#define BIT_AC	0x00001000	/* use alternative C-flag definition
				   for compares */
#define BIT_EP	0x00000800	/* use expanded packed decimal format */
#define BIT_SO	0x00000400	/* select synchronous operation of FPA */
#define BIT_NE	0x00000200	/* NaN exception bit */
#define BIT_ND	0x00000100	/* no denormalized numbers bit */

/* CUMULATIVE EXCEPTION FLAGS BYTE
---------------------------------- */

#define MASK_EXCEPTION_FLAGS		0x000000ff
#define MASK_EXCEPTION_FLAGS_STRICT	0x0000001f

#define BIT_IXC		0x00000010	/* inexact exception flag */
#define BIT_UFC		0x00000008	/* underflow exception flag */
#define BIT_OFC		0x00000004	/* overfloat exception flag */
#define BIT_DZC		0x00000002	/* divide by zero exception flag */
#define BIT_IOC		0x00000001	/* invalid operation exception flag */

/* Floating Point Control Register
----------------------------------*/

#define BIT_RU		0x80000000	/* rounded up bit */
#define BIT_IE		0x10000000	/* inexact bit */
#define BIT_MO		0x08000000	/* mantissa overflow bit */
#define BIT_EO		0x04000000	/* exponent overflow bit */
#define BIT_SB		0x00000800	/* store bounce */
#define BIT_AB		0x00000400	/* arithmetic bounce */
#define BIT_RE		0x00000200	/* rounding exception */
#define BIT_DA		0x00000100	/* disable FPA */

#define MASK_OP		0x00f08010	/* AU operation code */
#define MASK_PR		0x00080080	/* AU precision */
#define MASK_S1		0x00070000	/* AU source register 1 */
#define MASK_S2		0x00000007	/* AU source register 2 */
#define MASK_DS		0x00007000	/* AU destination register */
#define MASK_RM		0x00000060	/* AU rounding mode */
#define MASK_ALU	0x9cfff2ff	/* only ALU can write these bits */
#define MASK_RESET	0x00000d00	/* bits set on reset, all others cleared */
#define MASK_WFC	MASK_RESET
#define MASK_RFC	~MASK_RESET

#endif
