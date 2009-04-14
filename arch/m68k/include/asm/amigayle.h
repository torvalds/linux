/*
** asm-m68k/amigayle.h -- This header defines the registers of the gayle chip
**                        found on the Amiga 1200
**                        This information was found by disassembling card.resource,
**                        so the definitions may not be 100% correct
**                        anyone has an official doc ?
**
** Copyright 1997 by Alain Malek
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
** Created: 11/28/97 by Alain Malek
*/

#ifndef _M68K_AMIGAYLE_H_
#define _M68K_AMIGAYLE_H_

#include <linux/types.h>
#include <asm/amigahw.h>

/* memory layout */

#define GAYLE_RAM		(0x600000+zTwoBase)
#define GAYLE_RAMSIZE		(0x400000)
#define GAYLE_ATTRIBUTE		(0xa00000+zTwoBase)
#define GAYLE_ATTRIBUTESIZE	(0x020000)
#define GAYLE_IO		(0xa20000+zTwoBase)	/* 16bit and even 8bit registers */
#define GAYLE_IOSIZE		(0x010000)
#define GAYLE_IO_8BITODD	(0xa30000+zTwoBase)	/* odd 8bit registers */

/* offset for accessing odd IO registers */
#define GAYLE_ODD		(GAYLE_IO_8BITODD-GAYLE_IO-1)

/* GAYLE registers */

struct GAYLE {
	u_char cardstatus;
	u_char pad0[0x1000-1];

	u_char intreq;
	u_char pad1[0x1000-1];

	u_char inten;
	u_char pad2[0x1000-1];

	u_char config;
	u_char pad3[0x1000-1];
};

#define GAYLE_ADDRESS	(0xda8000)	/* gayle main registers base address */

#define GAYLE_RESET	(0xa40000)	/* write 0x00 to start reset,
                                           read 1 byte to stop reset */

#define gayle (*(volatile struct GAYLE *)(zTwoBase+GAYLE_ADDRESS))
#define gayle_reset (*(volatile u_char *)(zTwoBase+GAYLE_RESET))

#define gayle_attribute ((volatile u_char *)(GAYLE_ATTRIBUTE))

#if 0
#define gayle_inb(a) readb( GAYLE_IO+(a)+(((a)&1)*GAYLE_ODD) )
#define gayle_outb(v,a) writeb( v, GAYLE_IO+(a)+(((a)&1)*GAYLE_ODD) )

#define gayle_inw(a) readw( GAYLE_IO+(a) )
#define gayle_outw(v,a) writew( v, GAYLE_IO+(a) )
#endif

/* GAYLE_CARDSTATUS bit def */

#define GAYLE_CS_CCDET		0x40	/* credit card detect */
#define GAYLE_CS_BVD1		0x20	/* battery voltage detect 1 */
#define GAYLE_CS_SC		0x20	/* credit card status change */
#define GAYLE_CS_BVD2		0x10	/* battery voltage detect 2 */
#define GAYLE_CS_DA		0x10	/* digital audio */
#define GAYLE_CS_WR		0x08	/* write enable (1 == enabled) */
#define GAYLE_CS_BSY		0x04	/* credit card busy */
#define GAYLE_CS_IRQ		0x04	/* interrupt request */

/* GAYLE_IRQ bit def */

#define GAYLE_IRQ_IDE		0x80
#define GAYLE_IRQ_CCDET		0x40
#define GAYLE_IRQ_BVD1		0x20
#define GAYLE_IRQ_SC		0x20
#define GAYLE_IRQ_BVD2		0x10
#define GAYLE_IRQ_DA		0x10
#define GAYLE_IRQ_WR		0x08
#define GAYLE_IRQ_BSY		0x04
#define GAYLE_IRQ_IRQ		0x04
#define GAYLE_IRQ_IDEACK1	0x02
#define GAYLE_IRQ_IDEACK0	0x01

/* GAYLE_CONFIG bit def
   (bit 0-1 for program voltage, bit 2-3 for access speed */

#define GAYLE_CFG_0V		0x00
#define GAYLE_CFG_5V		0x01
#define GAYLE_CFG_12V		0x02

#define GAYLE_CFG_100NS		0x08
#define GAYLE_CFG_150NS		0x04
#define GAYLE_CFG_250NS		0x00
#define GAYLE_CFG_720NS		0x0c

#endif /* asm-m68k/amigayle.h */
