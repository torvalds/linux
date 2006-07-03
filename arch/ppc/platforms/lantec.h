/*
 * LANTEC board specific definitions
 *
 * Copyright (c) 2001 Wolfgang Denk (wd@denx.de)
 */

#ifndef __MACH_LANTEC_H
#define __MACH_LANTEC_H


#include <asm/ppcboot.h>

#define	IMAP_ADDR	0xFFF00000	/* physical base address of IMMR area	*/
#define IMAP_SIZE	(64 * 1024)	/* mapped size of IMMR area		*/

/* We don't use the 8259.
*/
#define NR_8259_INTS	0

#endif	/* __MACH_LANTEC_H */
