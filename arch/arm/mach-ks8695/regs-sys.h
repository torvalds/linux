/*
 * arch/arm/mach-ks8695/include/mach/regs-sys.h
 *
 * Copyright (C) 2006 Ben Dooks <ben@simtec.co.uk>
 * Copyright (C) 2006 Simtec Electronics
 *
 * KS8695 - System control registers and bit definitions
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef KS8695_SYS_H
#define KS8695_SYS_H

#define KS8695_SYS_OFFSET	(0xF0000 + 0x0000)
#define KS8695_SYS_VA		(KS8695_IO_VA + KS8695_SYS_OFFSET)
#define KS8695_SYS_PA		(KS8695_IO_PA + KS8695_SYS_OFFSET)


#define KS8695_SYSCFG		(0x00)		/* System Configuration Register */
#define KS8695_CLKCON		(0x04)		/* System Clock and Bus Control Register */


/* System Configuration Register */
#define SYSCFG_SPRBP		(0x3ff << 16)	/* Register Bank Base Pointer */

/* System Clock and Bus Control Register */
#define CLKCON_SFMODE		(1 << 8)	/* System Fast Mode for Simulation */
#define CLKCON_SCDC		(7 << 0)	/* System Clock Divider Select */


#endif
