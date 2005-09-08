/*
 * TQM8xx(L) board specific definitions
 *
 * Copyright (c) 1999-2002 Wolfgang Denk (wd@denx.de)
 */

#ifdef __KERNEL__
#ifndef __MACH_TQM8xx_H
#define __MACH_TQM8xx_H

#include <linux/config.h>

#include <asm/ppcboot.h>

#ifndef __ASSEMBLY__
#define	TQM_IMMR_BASE	0xFFF00000	/* phys. addr of IMMR */
#define	TQM_IMAP_SIZE	(64 * 1024)	/* size of mapped area */

#define	IMAP_ADDR	TQM_IMMR_BASE	/* physical base address of IMMR area */
#define IMAP_SIZE	TQM_IMAP_SIZE	/* mapped size of IMMR area */

/*-----------------------------------------------------------------------
 * PCMCIA stuff
 *-----------------------------------------------------------------------
 *
 */
#define PCMCIA_MEM_SIZE		( 64 << 20 )

#ifndef CONFIG_KUP4K
# define	MAX_HWIFS	1	/* overwrite default in include/asm-ppc/ide.h	*/

#else	/* CONFIG_KUP4K */

# define	MAX_HWIFS	2	/* overwrite default in include/asm-ppc/ide.h	*/
# ifndef __ASSEMBLY__
# include <asm/8xx_immap.h>
static __inline__ void ide_led(int on)
{
	volatile immap_t	*immap = (immap_t *)IMAP_ADDR;

	if (on) {
		immap->im_ioport.iop_padat &= ~0x80;
	} else {
		immap->im_ioport.iop_padat |= 0x80;
	}
}
# endif	/* __ASSEMBLY__ */
# define IDE_LED(x) ide_led((x))
#endif	/* CONFIG_KUP4K */

/*
 * Definitions for IDE0 Interface
 */
#define IDE0_BASE_OFFSET		0
#define IDE0_DATA_REG_OFFSET		(PCMCIA_MEM_SIZE + 0x320)
#define IDE0_ERROR_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 1)
#define IDE0_NSECTOR_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 2)
#define IDE0_SECTOR_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 3)
#define IDE0_LCYL_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 4)
#define IDE0_HCYL_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 5)
#define IDE0_SELECT_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 6)
#define IDE0_STATUS_REG_OFFSET		(2 * PCMCIA_MEM_SIZE + 0x320 + 7)
#define IDE0_CONTROL_REG_OFFSET		0x0106
#define IDE0_IRQ_REG_OFFSET		0x000A	/* not used */

/* define IO_BASE for PCMCIA */
#define _IO_BASE 0x80000000
#define _IO_BASE_SIZE  (64<<10)

#define	FEC_INTERRUPT		 9	/* = SIU_LEVEL4			*/
#define PHY_INTERRUPT		12	/* = IRQ6			*/
#define	IDE0_INTERRUPT		13

#ifdef CONFIG_IDE
#endif

/*-----------------------------------------------------------------------
 * CPM Ethernet through SCCx.
 *-----------------------------------------------------------------------
 *
 */

/***  TQM823L, TQM850L  ***********************************************/

#if defined(CONFIG_TQM823L) || defined(CONFIG_TQM850L)
/* Bits in parallel I/O port registers that have to be set/cleared
 * to configure the pins for SCC1 use.
 */
#define PA_ENET_RXD	((ushort)0x0004)	/* PA 13 */
#define PA_ENET_TXD	((ushort)0x0008)	/* PA 12 */
#define PA_ENET_RCLK	((ushort)0x0100)	/* PA  7 */
#define PA_ENET_TCLK	((ushort)0x0400)	/* PA  5 */

#define PB_ENET_TENA	((uint)0x00002000)	/* PB 18 */

#define PC_ENET_CLSN	((ushort)0x0040)	/* PC  9 */
#define PC_ENET_RENA	((ushort)0x0080)	/* PC  8 */

/* Control bits in the SICR to route TCLK (CLK3) and RCLK (CLK1) to
 * SCC2.  Also, make sure GR2 (bit 16) and SC2 (bit 17) are zero.
 */
#define SICR_ENET_MASK	((uint)0x0000ff00)
#define SICR_ENET_CLKRT	((uint)0x00002600)
#endif	/* CONFIG_TQM823L, CONFIG_TQM850L */

/***  TQM860L  ********************************************************/

#ifdef CONFIG_TQM860L
/* Bits in parallel I/O port registers that have to be set/cleared
 * to configure the pins for SCC1 use.
 */
#define PA_ENET_RXD	((ushort)0x0001)	/* PA 15 */
#define PA_ENET_TXD	((ushort)0x0002)	/* PA 14 */
#define PA_ENET_RCLK	((ushort)0x0100)	/* PA  7 */
#define PA_ENET_TCLK	((ushort)0x0400)	/* PA  5 */

#define PC_ENET_TENA	((ushort)0x0001)	/* PC 15 */
#define PC_ENET_CLSN	((ushort)0x0010)	/* PC 11 */
#define PC_ENET_RENA	((ushort)0x0020)	/* PC 10 */

/* Control bits in the SICR to route TCLK (CLK3) and RCLK (CLK1) to
 * SCC1.  Also, make sure GR1 (bit 24) and SC1 (bit 25) are zero.
 */
#define SICR_ENET_MASK	((uint)0x000000ff)
#define SICR_ENET_CLKRT	((uint)0x00000026)
#endif	/* CONFIG_TQM860L */

/***  FPS850L  *********************************************************/

#ifdef CONFIG_FPS850L
/* Bits in parallel I/O port registers that have to be set/cleared
 * to configure the pins for SCC1 use.
 */
#define PA_ENET_RXD	((ushort)0x0004)	/* PA 13 */
#define PA_ENET_TXD	((ushort)0x0008)	/* PA 12 */
#define PA_ENET_RCLK	((ushort)0x0100)	/* PA  7 */
#define PA_ENET_TCLK	((ushort)0x0400)	/* PA  5 */

#define PC_ENET_TENA	((ushort)0x0002)	/* PC 14 */
#define PC_ENET_CLSN	((ushort)0x0040)	/* PC  9 */
#define PC_ENET_RENA	((ushort)0x0080)	/* PC  8 */

/* Control bits in the SICR to route TCLK (CLK2) and RCLK (CLK4) to
 * SCC2.  Also, make sure GR2 (bit 16) and SC2 (bit 17) are zero.
 */
#define SICR_ENET_MASK	((uint)0x0000ff00)
#define SICR_ENET_CLKRT	((uint)0x00002600)
#endif	/* CONFIG_FPS850L */

/* We don't use the 8259.
*/
#define NR_8259_INTS	0

#endif /* !__ASSEMBLY__ */
#endif	/* __MACH_TQM8xx_H */
#endif /* __KERNEL__ */
