/*
 * MicroSys IP860 VMEBus board specific definitions
 *
 * Copyright (c) 2000, 2001 Wolfgang Denk (wd@denx.de)
 */

#ifndef __MACH_IP860_H
#define __MACH_IP860_H


#include <asm/ppcboot.h>

#define	IP860_IMMR_BASE	0xF1000000	/* phys. addr of IMMR			*/
#define	IP860_IMAP_SIZE	(64 * 1024)	/* size of mapped area			*/

#define	IMAP_ADDR	IP860_IMMR_BASE	/* physical base address of IMMR area	*/
#define IMAP_SIZE	IP860_IMAP_SIZE	/* mapped size of IMMR area		*/

/*
 * MPC8xx Chip Select Usage
 */
#define	IP860_BOOT_CS		0	/* Boot (VMEBus or Flash) Chip Select 0	*/
#define IP860_FLASH_CS		1	/* Flash	    is on Chip Select 1	*/
#define IP860_SDRAM_CS		2	/* SDRAM	    is on Chip Select 2	*/
#define	IP860_SRAM_CS		3	/* SRAM		    is on Chip Select 3	*/
#define IP860_BCSR_CS		4	/* BCSR		    is on Chip Select 4	*/
#define IP860_IP_CS		5	/* IP Slots	   are on Chip Select 5	*/
#define IP860_VME_STD_CS	6	/* VME Standard I/O is on Chip Select 6	*/
#define IP860_VME_SHORT_CS	7	/* VME Short    I/O is on Chip Select 7	*/

/* We don't use the 8259.
*/
#define NR_8259_INTS	0

#endif	/* __MACH_IP860_H */
