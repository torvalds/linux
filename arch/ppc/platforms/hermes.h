/*
 * Multidata HERMES-PRO ( / SL ) board specific definitions
 *
 * Copyright (c) 2000, 2001 Wolfgang Denk (wd@denx.de)
 */

#ifndef __MACH_HERMES_H
#define __MACH_HERMES_H

#include <linux/config.h>

#include <asm/ppcboot.h>

#define	HERMES_IMMR_BASE    0xFF000000	/* phys. addr of IMMR			*/
#define	HERMES_IMAP_SIZE   (64 * 1024)	/* size of mapped area			*/

#define	IMAP_ADDR     HERMES_IMMR_BASE	/* physical base address of IMMR area	*/
#define IMAP_SIZE     HERMES_IMAP_SIZE	/* mapped size of IMMR area		*/

#define	FEC_INTERRUPT	 9		/* = SIU_LEVEL4				*/
#define	CPM_INTERRUPT	11		/* = SIU_LEVEL5 (was: SIU_LEVEL2)	*/

/* We don't use the 8259.
*/
#define NR_8259_INTS	0

#endif	/* __MACH_HERMES_H */
