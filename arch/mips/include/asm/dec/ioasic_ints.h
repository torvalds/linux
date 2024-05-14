/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Definitions for the interrupt related bits in the I/O ASIC
 * interrupt status register (and the interrupt mask register, of course)
 *
 * Created with Information from:
 *
 * "DEC 3000 300/400/500/600/700/800/900 AXP Models System Programmer's Manual"
 *
 * and the Mach Sources
 *
 * Copyright (C) 199x  the Anonymous
 * Copyright (C) 2002  Maciej W. Rozycki
 */

#ifndef __ASM_DEC_IOASIC_INTS_H
#define __ASM_DEC_IOASIC_INTS_H

/*
 * The upper 16 bits are a part of the I/O ASIC's internal DMA engine
 * and thus are common to all I/O ASIC machines.  The exception is
 * the Maxine, which makes use of the FLOPPY and ISDN bits (otherwise
 * unused) and has a different SCC wiring.
 */
					/* all systems */
#define IO_INR_SCC0A_TXDMA	31	/* SCC0A transmit page end */
#define IO_INR_SCC0A_TXERR	30	/* SCC0A transmit memory read error */
#define IO_INR_SCC0A_RXDMA	29	/* SCC0A receive half page */
#define IO_INR_SCC0A_RXERR	28	/* SCC0A receive overrun */
#define IO_INR_ASC_DMA		19	/* ASC buffer pointer loaded */
#define IO_INR_ASC_ERR		18	/* ASC page overrun */
#define IO_INR_ASC_MERR		17	/* ASC memory read error */
#define IO_INR_LANCE_MERR	16	/* LANCE memory read error */

					/* except Maxine */
#define IO_INR_SCC1A_TXDMA	27	/* SCC1A transmit page end */
#define IO_INR_SCC1A_TXERR	26	/* SCC1A transmit memory read error */
#define IO_INR_SCC1A_RXDMA	25	/* SCC1A receive half page */
#define IO_INR_SCC1A_RXERR	24	/* SCC1A receive overrun */
#define IO_INR_RES_23		23	/* unused */
#define IO_INR_RES_22		22	/* unused */
#define IO_INR_RES_21		21	/* unused */
#define IO_INR_RES_20		20	/* unused */

					/* Maxine */
#define IO_INR_AB_TXDMA		27	/* ACCESS.bus transmit page end */
#define IO_INR_AB_TXERR		26	/* ACCESS.bus xmit memory read error */
#define IO_INR_AB_RXDMA		25	/* ACCESS.bus receive half page */
#define IO_INR_AB_RXERR		24	/* ACCESS.bus receive overrun */
#define IO_INR_FLOPPY_ERR	23	/* FDC error */
#define IO_INR_ISDN_TXDMA	22	/* ISDN xmit buffer pointer loaded */
#define IO_INR_ISDN_RXDMA	21	/* ISDN recv buffer pointer loaded */
#define IO_INR_ISDN_ERR		20	/* ISDN memory read/overrun error */

#define IO_INR_DMA		16	/* first DMA IRQ */

/*
 * The lower 16 bits are system-specific and thus defined in
 * system-specific headers.
 */


#define IO_IRQ_BASE		8	/* first IRQ assigned to I/O ASIC */
#define IO_IRQ_LINES		32	/* number of I/O ASIC interrupts */

#define IO_IRQ_NR(n)		((n) + IO_IRQ_BASE)
#define IO_IRQ_MASK(n)		(1 << (n))
#define IO_IRQ_ALL		0x0000ffff
#define IO_IRQ_DMA		0xffff0000

#endif /* __ASM_DEC_IOASIC_INTS_H */
