/*
 *	include/asm-mips/dec/ecc.h
 *
 *	ECC handling logic definitions common to DECstation/DECsystem
 *	5000/200 (KN02), 5000/240 (KN03), 5000/260 (KN05) and
 *	DECsystem 5900 (KN03), 5900/260 (KN05) systems.
 *
 *	Copyright (C) 2003  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#ifndef __ASM_MIPS_DEC_ECC_H
#define __ASM_MIPS_DEC_ECC_H

/*
 * Error Address Register bits.
 * The register is r/wc -- any write clears it.
 */
#define KN0X_EAR_VALID		(1<<31)		/* error data valid, bus IRQ */
#define KN0X_EAR_CPU		(1<<30)		/* CPU/DMA transaction */
#define KN0X_EAR_WRITE		(1<<29)		/* write/read transaction */
#define KN0X_EAR_ECCERR		(1<<28)		/* ECC/timeout or overrun */
#define KN0X_EAR_RES_27		(1<<27)		/* unused */
#define KN0X_EAR_ADDRESS	(0x7ffffff<<0)	/* address involved */

/*
 * Error Syndrome Register bits.
 * The register is frozen when EAR.VALID is set, otherwise it records bits
 * from the last memory read.  The register is r/wc -- any write clears it.
 */
#define KN0X_ESR_VLDHI		(1<<31)		/* error data valid hi word */
#define KN0X_ESR_CHKHI		(0x7f<<24)	/* check bits read from mem */
#define KN0X_ESR_SNGHI		(1<<23)		/* single/double bit error */
#define KN0X_ESR_SYNHI		(0x7f<<16)	/* syndrome from ECC logic */
#define KN0X_ESR_VLDLO		(1<<15)		/* error data valid lo word */
#define KN0X_ESR_CHKLO		(0x7f<<8)	/* check bits read from mem */
#define KN0X_ESR_SNGLO		(1<<7)		/* single/double bit error */
#define KN0X_ESR_SYNLO		(0x7f<<0)	/* syndrome from ECC logic */


#ifndef __ASSEMBLY__

#include <linux/interrupt.h>

struct pt_regs;

extern void dec_ecc_be_init(void);
extern int dec_ecc_be_handler(struct pt_regs *regs, int is_fixup);
extern irqreturn_t dec_ecc_be_interrupt(int irq, void *dev_id);
#endif

#endif /* __ASM_MIPS_DEC_ECC_H */
