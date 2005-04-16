/*
 * Speech Design Integrated Voicemail board specific definitions
 * - IVMS8  (small,  8 channels)
 * - IVML24 (large, 24 channels)
 *
 * In 2.5 when we force a new bootloader, we can merge these two, and add
 * in _MACH_'s for them. -- Tom
 *
 * Copyright (c) 2000, 2001 Wolfgang Denk (wd@denx.de)
 */

#ifdef __KERNEL__
#ifndef __ASM_IVMS8_H__
#define __ASM_IVMS8_H__

#include <linux/config.h>

#include <asm/ppcboot.h>

#define IVMS_IMMR_BASE	0xFFF00000	/* phys. addr of IMMR */
#define IVMS_IMAP_SIZE	(64 * 1024)	/* size of mapped area */

#define IMAP_ADDR	IVMS_IMMR_BASE	/* phys. base address of IMMR area */
#define IMAP_SIZE	IVMS_IMAP_SIZE	/* mapped size of IMMR area */

#define PCMCIA_MEM_ADDR	((uint)0xFE100000)
#define PCMCIA_MEM_SIZE	((uint)(64 * 1024))

#define FEC_INTERRUPT	 9		/* = SIU_LEVEL4 */
#define IDE0_INTERRUPT	10		/* = IRQ5 */
#define CPM_INTERRUPT	11		/* = SIU_LEVEL5 (was: SIU_LEVEL2) */
#define PHY_INTERRUPT	12		/* = IRQ6 */

/* override the default number of IDE hardware interfaces */
#define MAX_HWIFS	1

/*
 * Definitions for IDE0 Interface
 */
#define IDE0_BASE_OFFSET		0x0000	/* Offset in PCMCIA memory */
#define IDE0_DATA_REG_OFFSET		0x0000
#define IDE0_ERROR_REG_OFFSET		0x0081
#define IDE0_NSECTOR_REG_OFFSET		0x0082
#define IDE0_SECTOR_REG_OFFSET		0x0083
#define IDE0_LCYL_REG_OFFSET		0x0084
#define IDE0_HCYL_REG_OFFSET		0x0085
#define IDE0_SELECT_REG_OFFSET		0x0086
#define IDE0_STATUS_REG_OFFSET		0x0087
#define IDE0_CONTROL_REG_OFFSET		0x0106
#define IDE0_IRQ_REG_OFFSET		0x000A	/* not used */

/* We don't use the 8259. */
#define NR_8259_INTS	0

#endif /* __ASM_IVMS8_H__ */
#endif /* __KERNEL__ */
