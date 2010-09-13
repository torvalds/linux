#ifndef _MRST_MAX3110_H
#define _MRST_MAX3110_H

#define MAX3110_HIGH_CLK	0x1	/* 3.6864 MHZ */
#define MAX3110_LOW_CLK		0x0	/* 1.8432 MHZ */

/* status bits for all 4 MAX3110 operate modes */
#define MAX3110_READ_DATA_AVAILABLE	(1 << 15)
#define MAX3110_WRITE_BUF_EMPTY		(1 << 14)

#define WC_TAG			(3 << 14)
#define RC_TAG			(1 << 14)
#define WD_TAG			(2 << 14)
#define RD_TAG			(0 << 14)

/* bits def for write configuration */
#define WC_FIFO_ENABLE_MASK	(1 << 13)
#define WC_FIFO_ENABLE		(0 << 13)

#define WC_SW_SHDI		(1 << 12)

#define WC_IRQ_MASK		(0xF << 8)
#define WC_TXE_IRQ_ENABLE	(1 << 11)	/* TX empty irq */
#define WC_RXA_IRQ_ENABLE	(1 << 10)	/* RX availabe irq */
#define WC_PAR_HIGH_IRQ_ENABLE	(1 << 9)
#define WC_REC_ACT_IRQ_ENABLE	(1 << 8)

#define WC_IRDA_ENABLE		(1 << 7)

#define WC_STOPBITS_MASK	(1 << 6)
#define WC_2_STOPBITS		(1 << 6)
#define WC_1_STOPBITS		(0 << 6)

#define WC_PARITY_ENABLE_MASK	(1 << 5)
#define WC_PARITY_ENABLE	(1 << 5)

#define WC_WORDLEN_MASK		(1 << 4)
#define WC_7BIT_WORD		(1 << 4)
#define WC_8BIT_WORD		(0 << 4)

#define WC_BAUD_DIV_MASK	(0xF)
#define WC_BAUD_DR1		(0x0)
#define WC_BAUD_DR2		(0x1)
#define WC_BAUD_DR4		(0x2)
#define WC_BAUD_DR8		(0x3)
#define WC_BAUD_DR16		(0x4)
#define WC_BAUD_DR32		(0x5)
#define WC_BAUD_DR64		(0x6)
#define WC_BAUD_DR128		(0x7)
#define WC_BAUD_DR3		(0x8)
#define WC_BAUD_DR6		(0x9)
#define WC_BAUD_DR12		(0xA)
#define WC_BAUD_DR24		(0xB)
#define WC_BAUD_DR48		(0xC)
#define WC_BAUD_DR96		(0xD)
#define WC_BAUD_DR192		(0xE)
#define WC_BAUD_DR384		(0xF)

#define M3110_RX_FIFO_DEPTH	8
#endif
