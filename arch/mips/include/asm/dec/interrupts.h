/*
 * Miscellaneous definitions used to initialise the interrupt vector table
 * with the machine-specific interrupt routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997 by Paul M. Antoine.
 * reworked 1998 by Harald Koerfgen.
 * Copyright (C) 2001, 2002, 2003  Maciej W. Rozycki
 */

#ifndef __ASM_DEC_INTERRUPTS_H
#define __ASM_DEC_INTERRUPTS_H

#include <irq.h>
#include <asm/mipsregs.h>


/*
 * The list of possible system devices which provide an
 * interrupt.  Not all devices exist on a given system.
 */
#define DEC_IRQ_CASCADE		0	/* cascade from CSR or I/O ASIC */

/* Ordinary interrupts */
#define DEC_IRQ_AB_RECV		1	/* ACCESS.bus receive */
#define DEC_IRQ_AB_XMIT		2	/* ACCESS.bus transmit */
#define DEC_IRQ_DZ11		3	/* DZ11 (DC7085) serial */
#define DEC_IRQ_ASC		4	/* ASC (NCR53C94) SCSI */
#define DEC_IRQ_FLOPPY		5	/* 82077 FDC */
#define DEC_IRQ_FPU		6	/* R3k FPU */
#define DEC_IRQ_HALT		7	/* HALT button or from ACCESS.Bus */
#define DEC_IRQ_ISDN		8	/* Am79C30A ISDN */
#define DEC_IRQ_LANCE		9	/* LANCE (Am7990) Ethernet */
#define DEC_IRQ_BUS		10	/* memory, I/O bus read/write errors */
#define DEC_IRQ_PSU		11	/* power supply unit warning */
#define DEC_IRQ_RTC		12	/* DS1287 RTC */
#define DEC_IRQ_SCC0		13	/* SCC (Z85C30) serial #0 */
#define DEC_IRQ_SCC1		14	/* SCC (Z85C30) serial #1 */
#define DEC_IRQ_SII		15	/* SII (DC7061) SCSI */
#define DEC_IRQ_TC0		16	/* TURBOchannel slot #0 */
#define DEC_IRQ_TC1		17	/* TURBOchannel slot #1 */
#define DEC_IRQ_TC2		18	/* TURBOchannel slot #2 */
#define DEC_IRQ_TIMER		19	/* ARC periodic timer */
#define DEC_IRQ_VIDEO		20	/* framebuffer */

/* I/O ASIC DMA interrupts */
#define DEC_IRQ_ASC_MERR	21	/* ASC memory read error */
#define DEC_IRQ_ASC_ERR		22	/* ASC page overrun */
#define DEC_IRQ_ASC_DMA		23	/* ASC buffer pointer loaded */
#define DEC_IRQ_FLOPPY_ERR	24	/* FDC error */
#define DEC_IRQ_ISDN_ERR	25	/* ISDN memory read/overrun error */
#define DEC_IRQ_ISDN_RXDMA	26	/* ISDN recv buffer pointer loaded */
#define DEC_IRQ_ISDN_TXDMA	27	/* ISDN xmit buffer pointer loaded */
#define DEC_IRQ_LANCE_MERR	28	/* LANCE memory read error */
#define DEC_IRQ_SCC0A_RXERR	29	/* SCC0A (printer) receive overrun */
#define DEC_IRQ_SCC0A_RXDMA	30	/* SCC0A receive half page */
#define DEC_IRQ_SCC0A_TXERR	31	/* SCC0A xmit memory read/overrun */
#define DEC_IRQ_SCC0A_TXDMA	32	/* SCC0A transmit page end */
#define DEC_IRQ_AB_RXERR	33	/* ACCESS.bus receive overrun */
#define DEC_IRQ_AB_RXDMA	34	/* ACCESS.bus receive half page */
#define DEC_IRQ_AB_TXERR	35	/* ACCESS.bus xmit memory read/ovrn */
#define DEC_IRQ_AB_TXDMA	36	/* ACCESS.bus transmit page end */
#define DEC_IRQ_SCC1A_RXERR	37	/* SCC1A (modem) receive overrun */
#define DEC_IRQ_SCC1A_RXDMA	38	/* SCC1A receive half page */
#define DEC_IRQ_SCC1A_TXERR	39	/* SCC1A xmit memory read/overrun */
#define DEC_IRQ_SCC1A_TXDMA	40	/* SCC1A transmit page end */

/* TC5 & TC6 are virtual slots for KN02's onboard devices */
#define DEC_IRQ_TC5		DEC_IRQ_ASC	/* virtual PMAZ-AA */
#define DEC_IRQ_TC6		DEC_IRQ_LANCE	/* virtual PMAD-AA */

#define DEC_NR_INTS		41


/* Largest of cpu mask_nr tables. */
#define DEC_MAX_CPU_INTS	6
/* Largest of asic mask_nr tables. */
#define DEC_MAX_ASIC_INTS	9


/*
 * CPU interrupt bits common to all systems.
 */
#define DEC_CPU_INR_FPU		7	/* R3k FPU */
#define DEC_CPU_INR_SW1		1	/* software #1 */
#define DEC_CPU_INR_SW0		0	/* software #0 */

#define DEC_CPU_IRQ_BASE	MIPS_CPU_IRQ_BASE	/* first IRQ assigned to CPU */

#define DEC_CPU_IRQ_NR(n)	((n) + DEC_CPU_IRQ_BASE)
#define DEC_CPU_IRQ_MASK(n)	(1 << ((n) + CAUSEB_IP))
#define DEC_CPU_IRQ_ALL		(0xff << CAUSEB_IP)


#ifndef __ASSEMBLER__

/*
 * Interrupt table structures to hide differences between systems.
 */
typedef union { int i; void *p; } int_ptr;
extern int dec_interrupt[DEC_NR_INTS];
extern int_ptr cpu_mask_nr_tbl[DEC_MAX_CPU_INTS][2];
extern int_ptr asic_mask_nr_tbl[DEC_MAX_ASIC_INTS][2];
extern int cpu_fpu_mask;


/*
 * Common interrupt routine prototypes for all DECStations
 */
extern void kn02_io_int(void);
extern void kn02xa_io_int(void);
extern void kn03_io_int(void);
extern void asic_dma_int(void);
extern void asic_all_int(void);
extern void kn02_all_int(void);
extern void cpu_all_int(void);

extern void dec_intr_unimplemented(void);
extern void asic_intr_unimplemented(void);

#endif /* __ASSEMBLER__ */

#endif
