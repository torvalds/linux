/*
 * Speech Design SPD8xxTS board specific definitions
 *
 * Copyright (c) 2000,2001 Wolfgang Denk (wd@denx.de)
 */

#ifdef __KERNEL__
#ifndef __ASM_SPD8XX_H__
#define __ASM_SPD8XX_H__

#include <linux/config.h>

#include <asm/ppcboot.h>

#ifndef __ASSEMBLY__
#define SPD_IMMR_BASE	0xFFF00000	/* phys. addr of IMMR */
#define SPD_IMAP_SIZE	(64 * 1024)	/* size of mapped area */

#define IMAP_ADDR	SPD_IMMR_BASE	/* physical base address of IMMR area */
#define IMAP_SIZE	SPD_IMAP_SIZE	/* mapped size of IMMR area */

#define PCMCIA_MEM_ADDR	((uint)0xFE100000)
#define PCMCIA_MEM_SIZE	((uint)(64 * 1024))

#define IDE0_INTERRUPT	10		/* = IRQ5 */
#define IDE1_INTERRUPT	12		/* = IRQ6 */
#define CPM_INTERRUPT	13		/* = SIU_LEVEL6 (was: SIU_LEVEL2) */

/* override the default number of IDE hardware interfaces */
#define MAX_HWIFS	2

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

/*
 * Definitions for IDE1 Interface
 */
#define IDE1_BASE_OFFSET		0x0C00	/* Offset in PCMCIA memory */
#define IDE1_DATA_REG_OFFSET		0x0000
#define IDE1_ERROR_REG_OFFSET		0x0081
#define IDE1_NSECTOR_REG_OFFSET		0x0082
#define IDE1_SECTOR_REG_OFFSET		0x0083
#define IDE1_LCYL_REG_OFFSET		0x0084
#define IDE1_HCYL_REG_OFFSET		0x0085
#define IDE1_SELECT_REG_OFFSET		0x0086
#define IDE1_STATUS_REG_OFFSET		0x0087
#define IDE1_CONTROL_REG_OFFSET		0x0106
#define IDE1_IRQ_REG_OFFSET		0x000A	/* not used */

/* CPM Ethernet through SCCx.
 *
 * Bits in parallel I/O port registers that have to be set/cleared
 * to configure the pins for SCC2 use.
 */
#define PA_ENET_MDC	((ushort)0x0001)	/* PA 15 !!! */
#define PA_ENET_MDIO	((ushort)0x0002)	/* PA 14 !!! */
#define PA_ENET_RXD	((ushort)0x0004)	/* PA 13 */
#define PA_ENET_TXD	((ushort)0x0008)	/* PA 12 */
#define PA_ENET_RCLK	((ushort)0x0200)	/* PA  6 */
#define PA_ENET_TCLK	((ushort)0x0400)	/* PA  5 */

#define PB_ENET_TENA	((uint)0x00002000)	/* PB 18 */

#define PC_ENET_CLSN	((ushort)0x0040)	/* PC  9 */
#define PC_ENET_RENA	((ushort)0x0080)	/* PC  8 */
#define PC_ENET_RESET	((ushort)0x0100)	/* PC  7 !!! */

/* Control bits in the SICR to route TCLK (CLK3) and RCLK (CLK2) to
 * SCC2.  Also, make sure GR2 (bit 16) and SC2 (bit 17) are zero.
 */
#define SICR_ENET_MASK	((uint)0x0000ff00)
#define SICR_ENET_CLKRT	((uint)0x00002E00)

/* We don't use the 8259.
*/
#define NR_8259_INTS	0

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_SPD8XX_H__ */
#endif /* __KERNEL__ */
