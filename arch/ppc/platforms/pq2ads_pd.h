#ifndef __PQ2ADS_PD_H
#define __PQ2ADS_PD_H
/*
 * arch/ppc/platforms/82xx/pq2ads_pd.h
 *
 * Some defines for MPC82xx board-specific PlatformDevice descriptions
 *
 * 2005 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

/* FCC1 Clock Source Configuration.  These can be redefined in the board specific file.
   Can only choose from CLK9-12 */

#define F1_RXCLK	11
#define F1_TXCLK	10

/* FCC2 Clock Source Configuration.  These can be redefined in the board specific file.
   Can only choose from CLK13-16 */
#define F2_RXCLK	15
#define F2_TXCLK	16

/* FCC3 Clock Source Configuration.  These can be redefined in the board specific file.
   Can only choose from CLK13-16 */
#define F3_RXCLK	13
#define F3_TXCLK	14

/* Automatically generates register configurations */
#define PC_CLK(x)	((uint)(1<<(x-1)))	/* FCC CLK I/O ports */

#define CMXFCR_RF1CS(x)	((uint)((x-5)<<27))	/* FCC1 Receive Clock Source */
#define CMXFCR_TF1CS(x)	((uint)((x-5)<<24))	/* FCC1 Transmit Clock Source */
#define CMXFCR_RF2CS(x)	((uint)((x-9)<<19))	/* FCC2 Receive Clock Source */
#define CMXFCR_TF2CS(x) ((uint)((x-9)<<16))	/* FCC2 Transmit Clock Source */
#define CMXFCR_RF3CS(x)	((uint)((x-9)<<11))	/* FCC3 Receive Clock Source */
#define CMXFCR_TF3CS(x) ((uint)((x-9)<<8))	/* FCC3 Transmit Clock Source */

#define PC_F1RXCLK	PC_CLK(F1_RXCLK)
#define PC_F1TXCLK	PC_CLK(F1_TXCLK)
#define CMX1_CLK_ROUTE	(CMXFCR_RF1CS(F1_RXCLK) | CMXFCR_TF1CS(F1_TXCLK))
#define CMX1_CLK_MASK	((uint)0xff000000)

#define PC_F2RXCLK	PC_CLK(F2_RXCLK)
#define PC_F2TXCLK	PC_CLK(F2_TXCLK)
#define CMX2_CLK_ROUTE	(CMXFCR_RF2CS(F2_RXCLK) | CMXFCR_TF2CS(F2_TXCLK))
#define CMX2_CLK_MASK	((uint)0x00ff0000)

#define PC_F3RXCLK	PC_CLK(F3_RXCLK)
#define PC_F3TXCLK	PC_CLK(F3_TXCLK)
#define CMX3_CLK_ROUTE	(CMXFCR_RF3CS(F3_RXCLK) | CMXFCR_TF3CS(F3_TXCLK))
#define CMX3_CLK_MASK	((uint)0x0000ff00)

/* I/O Pin assignment for FCC1.  I don't yet know the best way to do this,
 * but there is little variation among the choices.
 */
#define PA1_COL		0x00000001U
#define PA1_CRS		0x00000002U
#define PA1_TXER	0x00000004U
#define PA1_TXEN	0x00000008U
#define PA1_RXDV	0x00000010U
#define PA1_RXER	0x00000020U
#define PA1_TXDAT	0x00003c00U
#define PA1_RXDAT	0x0003c000U
#define PA1_PSORA0	(PA1_RXDAT | PA1_TXDAT)
#define PA1_PSORA1	(PA1_COL | PA1_CRS | PA1_TXER | PA1_TXEN | \
		PA1_RXDV | PA1_RXER)
#define PA1_DIRA0	(PA1_RXDAT | PA1_CRS | PA1_COL | PA1_RXER | PA1_RXDV)
#define PA1_DIRA1	(PA1_TXDAT | PA1_TXEN | PA1_TXER)


/* I/O Pin assignment for FCC2.  I don't yet know the best way to do this,
 * but there is little variation among the choices.
 */
#define PB2_TXER	0x00000001U
#define PB2_RXDV	0x00000002U
#define PB2_TXEN	0x00000004U
#define PB2_RXER	0x00000008U
#define PB2_COL		0x00000010U
#define PB2_CRS		0x00000020U
#define PB2_TXDAT	0x000003c0U
#define PB2_RXDAT	0x00003c00U
#define PB2_PSORB0	(PB2_RXDAT | PB2_TXDAT | PB2_CRS | PB2_COL | \
		PB2_RXER | PB2_RXDV | PB2_TXER)
#define PB2_PSORB1	(PB2_TXEN)
#define PB2_DIRB0	(PB2_RXDAT | PB2_CRS | PB2_COL | PB2_RXER | PB2_RXDV)
#define PB2_DIRB1	(PB2_TXDAT | PB2_TXEN | PB2_TXER)


/* I/O Pin assignment for FCC3.  I don't yet know the best way to do this,
 * but there is little variation among the choices.
 */
#define PB3_RXDV	0x00004000U
#define PB3_RXER	0x00008000U
#define PB3_TXER	0x00010000U
#define PB3_TXEN	0x00020000U
#define PB3_COL		0x00040000U
#define PB3_CRS		0x00080000U
#define PB3_TXDAT	0x0f000000U
#define PB3_RXDAT	0x00f00000U
#define PB3_PSORB0	(PB3_RXDAT | PB3_TXDAT | PB3_CRS | PB3_COL | \
		PB3_RXER | PB3_RXDV | PB3_TXER | PB3_TXEN)
#define PB3_PSORB1	0
#define PB3_DIRB0	(PB3_RXDAT | PB3_CRS | PB3_COL | PB3_RXER | PB3_RXDV)
#define PB3_DIRB1	(PB3_TXDAT | PB3_TXEN | PB3_TXER)

#define FCC_MEM_OFFSET(x) (CPM_FCC_SPECIAL_BASE + (x*128))
#define FCC1_MEM_OFFSET FCC_MEM_OFFSET(0)
#define FCC2_MEM_OFFSET FCC_MEM_OFFSET(1)

#endif
