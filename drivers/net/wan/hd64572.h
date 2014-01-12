/*
 * hd64572.h	Description of the Hitachi HD64572 (SCA-II), valid for 
 * 		CPU modes 0 & 2.
 *
 * Author:	Ivan Passos <ivan@cyclades.com>
 *
 * Copyright:   (c) 2000-2001 Cyclades Corp.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * $Log: hd64572.h,v $
 * Revision 3.1  2001/06/15 12:41:10  regina
 * upping major version number
 *
 * Revision 1.1.1.1  2001/06/13 20:24:49  daniela
 * PC300 initial CVS version (3.4.0-pre1)
 *
 * Revision 1.0 2000/01/25 ivan
 * Initial version.
 *
 */

#ifndef __HD64572_H
#define __HD64572_H

/* Illegal Access Register */
#define	ILAR	0x00

/* Wait Controller Registers */
#define PABR0L	0x20	/* Physical Addr Boundary Register 0 L */
#define PABR0H	0x21	/* Physical Addr Boundary Register 0 H */
#define PABR1L	0x22	/* Physical Addr Boundary Register 1 L */
#define PABR1H	0x23	/* Physical Addr Boundary Register 1 H */
#define WCRL	0x24	/* Wait Control Register L */
#define WCRM	0x25	/* Wait Control Register M */
#define WCRH	0x26	/* Wait Control Register H */

/* Interrupt Registers */
#define IVR	0x60	/* Interrupt Vector Register */
#define IMVR	0x64	/* Interrupt Modified Vector Register */
#define ITCR	0x68	/* Interrupt Control Register */
#define ISR0	0x6c	/* Interrupt Status Register 0 */
#define ISR1	0x70	/* Interrupt Status Register 1 */
#define IER0	0x74	/* Interrupt Enable Register 0 */
#define IER1	0x78	/* Interrupt Enable Register 1 */

/* Register Access Macros (chan is 0 or 1 in _any_ case) */
#define	M_REG(reg, chan)	(reg + 0x80*chan)		/* MSCI */
#define	DRX_REG(reg, chan)	(reg + 0x40*chan)		/* DMA Rx */
#define	DTX_REG(reg, chan)	(reg + 0x20*(2*chan + 1))	/* DMA Tx */
#define	TRX_REG(reg, chan)	(reg + 0x20*chan)		/* Timer Rx */
#define	TTX_REG(reg, chan)	(reg + 0x10*(2*chan + 1))	/* Timer Tx */
#define	ST_REG(reg, chan)	(reg + 0x80*chan)		/* Status Cnt */
#define IR0_DRX(val, chan)	((val)<<(8*(chan)))		/* Int DMA Rx */
#define IR0_DTX(val, chan)	((val)<<(4*(2*chan + 1)))	/* Int DMA Tx */
#define IR0_M(val, chan)	((val)<<(8*(chan)))		/* Int MSCI */

/* MSCI Channel Registers */
#define MSCI0_OFFSET 0x00
#define MSCI1_OFFSET 0x80

#define MD0	0x138	/* Mode reg 0 */
#define MD1	0x139	/* Mode reg 1 */
#define MD2	0x13a	/* Mode reg 2 */
#define MD3	0x13b	/* Mode reg 3 */
#define CTL	0x130	/* Control reg */
#define RXS	0x13c	/* RX clock source */
#define TXS	0x13d	/* TX clock source */
#define EXS	0x13e	/* External clock input selection */
#define TMCT	0x144	/* Time constant (Tx) */
#define TMCR	0x145	/* Time constant (Rx) */
#define CMD	0x128	/* Command reg */
#define ST0	0x118	/* Status reg 0 */
#define ST1	0x119	/* Status reg 1 */
#define ST2	0x11a	/* Status reg 2 */
#define ST3	0x11b	/* Status reg 3 */
#define ST4	0x11c	/* Status reg 4 */
#define FST	0x11d	/* frame Status reg  */
#define IE0	0x120	/* Interrupt enable reg 0 */
#define IE1	0x121	/* Interrupt enable reg 1 */
#define IE2	0x122	/* Interrupt enable reg 2 */
#define IE4	0x124	/* Interrupt enable reg 4 */
#define FIE	0x125	/* Frame Interrupt enable reg  */
#define SA0	0x140	/* Syn Address reg 0 */
#define SA1	0x141	/* Syn Address reg 1 */
#define IDL	0x142	/* Idle register */
#define TRBL	0x100	/* TX/RX buffer reg L */ 
#define TRBK	0x101	/* TX/RX buffer reg K */ 
#define TRBJ	0x102	/* TX/RX buffer reg J */ 
#define TRBH	0x103	/* TX/RX buffer reg H */ 
#define TRC0	0x148	/* TX Ready control reg 0 */ 
#define TRC1	0x149	/* TX Ready control reg 1 */ 
#define RRC	0x14a	/* RX Ready control reg */ 
#define CST0	0x108	/* Current Status Register 0 */ 
#define CST1	0x109	/* Current Status Register 1 */ 
#define CST2	0x10a	/* Current Status Register 2 */ 
#define CST3	0x10b	/* Current Status Register 3 */ 
#define GPO	0x131	/* General Purpose Output Pin Ctl Reg */
#define TFS	0x14b	/* Tx Start Threshold Ctl Reg */
#define TFN	0x143	/* Inter-transmit-frame Time Fill Ctl Reg */
#define TBN	0x110	/* Tx Buffer Number Reg */
#define RBN	0x111	/* Rx Buffer Number Reg */
#define TNR0	0x150	/* Tx DMA Request Ctl Reg 0 */
#define TNR1	0x151	/* Tx DMA Request Ctl Reg 1 */
#define TCR	0x152	/* Tx DMA Critical Request Reg */
#define RNR	0x154	/* Rx DMA Request Ctl Reg */
#define RCR	0x156	/* Rx DMA Critical Request Reg */

/* Timer Registers */
#define TIMER0RX_OFFSET 0x00
#define TIMER0TX_OFFSET 0x10
#define TIMER1RX_OFFSET 0x20
#define TIMER1TX_OFFSET 0x30

#define TCNTL	0x200	/* Timer Upcounter L */
#define TCNTH	0x201	/* Timer Upcounter H */
#define TCONRL	0x204	/* Timer Constant Register L */
#define TCONRH	0x205	/* Timer Constant Register H */
#define TCSR	0x206	/* Timer Control/Status Register */
#define TEPR	0x207	/* Timer Expand Prescale Register */

/* DMA registers */
#define PCR		0x40		/* DMA priority control reg */
#define DRR		0x44		/* DMA reset reg */
#define DMER		0x07		/* DMA Master Enable reg */
#define BTCR		0x08		/* Burst Tx Ctl Reg */
#define BOLR		0x0c		/* Back-off Length Reg */
#define DSR_RX(chan)	(0x48 + 2*chan)	/* DMA Status Reg (Rx) */
#define DSR_TX(chan)	(0x49 + 2*chan)	/* DMA Status Reg (Tx) */
#define DIR_RX(chan)	(0x4c + 2*chan)	/* DMA Interrupt Enable Reg (Rx) */
#define DIR_TX(chan)	(0x4d + 2*chan)	/* DMA Interrupt Enable Reg (Tx) */
#define FCT_RX(chan)	(0x50 + 2*chan)	/* Frame End Interrupt Counter (Rx) */
#define FCT_TX(chan)	(0x51 + 2*chan)	/* Frame End Interrupt Counter (Tx) */
#define DMR_RX(chan)	(0x54 + 2*chan)	/* DMA Mode Reg (Rx) */
#define DMR_TX(chan)	(0x55 + 2*chan)	/* DMA Mode Reg (Tx) */
#define DCR_RX(chan)	(0x58 + 2*chan)	/* DMA Command Reg (Rx) */
#define DCR_TX(chan)	(0x59 + 2*chan)	/* DMA Command Reg (Tx) */

/* DMA Channel Registers */
#define DMAC0RX_OFFSET 0x00
#define DMAC0TX_OFFSET 0x20
#define DMAC1RX_OFFSET 0x40
#define DMAC1TX_OFFSET 0x60

#define DARL	0x80	/* Dest Addr Register L (single-block, RX only) */
#define DARH	0x81	/* Dest Addr Register H (single-block, RX only) */
#define DARB	0x82	/* Dest Addr Register B (single-block, RX only) */
#define DARBH	0x83	/* Dest Addr Register BH (single-block, RX only) */
#define SARL	0x80	/* Source Addr Register L (single-block, TX only) */
#define SARH	0x81	/* Source Addr Register H (single-block, TX only) */
#define SARB	0x82	/* Source Addr Register B (single-block, TX only) */
#define DARBH	0x83	/* Source Addr Register BH (single-block, TX only) */
#define BARL	0x80	/* Buffer Addr Register L (chained-block) */
#define BARH	0x81	/* Buffer Addr Register H (chained-block) */
#define BARB	0x82	/* Buffer Addr Register B (chained-block) */
#define BARBH	0x83	/* Buffer Addr Register BH (chained-block) */
#define CDAL	0x84	/* Current Descriptor Addr Register L */
#define CDAH	0x85	/* Current Descriptor Addr Register H */
#define CDAB	0x86	/* Current Descriptor Addr Register B */
#define CDABH	0x87	/* Current Descriptor Addr Register BH */
#define EDAL	0x88	/* Error Descriptor Addr Register L */
#define EDAH	0x89	/* Error Descriptor Addr Register H */
#define EDAB	0x8a	/* Error Descriptor Addr Register B */
#define EDABH	0x8b	/* Error Descriptor Addr Register BH */
#define BFLL	0x90	/* RX Buffer Length L (only RX) */
#define BFLH	0x91	/* RX Buffer Length H (only RX) */
#define BCRL	0x8c	/* Byte Count Register L */
#define BCRH	0x8d	/* Byte Count Register H */

/* Block Descriptor Structure */
typedef struct {
	unsigned long	next;		/* pointer to next block descriptor */
	unsigned long	ptbuf;		/* buffer pointer */
	unsigned short	len;		/* data length */
	unsigned char	status;		/* status */
	unsigned char	filler[5];	/* alignment filler (16 bytes) */ 
} pcsca_bd_t;

/* Block Descriptor Structure */
typedef struct {
	u32 cp;			/* pointer to next block descriptor */
	u32 bp;			/* buffer pointer */
	u16 len;		/* data length */
	u8 stat;		/* status */
	u8 unused;		/* pads to 4-byte boundary */
}pkt_desc;


/*
	Descriptor Status definitions:

	Bit	Transmission	Reception

	7	EOM		EOM
	6	-		Short Frame
	5	-		Abort
	4	-		Residual bit
	3	Underrun	Overrun	
	2	-		CRC
	1	Ownership	Ownership
	0	EOT		-
*/
#define DST_EOT		0x01	/* End of transmit command */
#define DST_OSB		0x02	/* Ownership bit */
#define DST_CRC		0x04	/* CRC Error */
#define DST_OVR		0x08	/* Overrun */
#define DST_UDR		0x08	/* Underrun */
#define DST_RBIT	0x10	/* Residual bit */
#define DST_ABT		0x20	/* Abort */
#define DST_SHRT	0x40	/* Short Frame  */
#define DST_EOM		0x80	/* End of Message  */

/* Packet Descriptor Status bits */

#define ST_TX_EOM     0x80	/* End of frame */
#define ST_TX_UNDRRUN 0x08
#define ST_TX_OWNRSHP 0x02
#define ST_TX_EOT     0x01	/* End of transmission */

#define ST_RX_EOM     0x80	/* End of frame */
#define ST_RX_SHORT   0x40	/* Short frame */
#define ST_RX_ABORT   0x20	/* Abort */
#define ST_RX_RESBIT  0x10	/* Residual bit */
#define ST_RX_OVERRUN 0x08	/* Overrun */
#define ST_RX_CRC     0x04	/* CRC */
#define ST_RX_OWNRSHP 0x02

#define ST_ERROR_MASK 0x7C

/* Status Counter Registers */
#define CMCR	0x158	/* Counter Master Ctl Reg */
#define TECNTL	0x160	/* Tx EOM Counter L */
#define TECNTM	0x161	/* Tx EOM Counter M */
#define TECNTH	0x162	/* Tx EOM Counter H */
#define TECCR	0x163	/* Tx EOM Counter Ctl Reg */
#define URCNTL	0x164	/* Underrun Counter L */
#define URCNTH	0x165	/* Underrun Counter H */
#define URCCR	0x167	/* Underrun Counter Ctl Reg */
#define RECNTL	0x168	/* Rx EOM Counter L */
#define RECNTM	0x169	/* Rx EOM Counter M */
#define RECNTH	0x16a	/* Rx EOM Counter H */
#define RECCR	0x16b	/* Rx EOM Counter Ctl Reg */
#define ORCNTL	0x16c	/* Overrun Counter L */
#define ORCNTH	0x16d	/* Overrun Counter H */
#define ORCCR	0x16f	/* Overrun Counter Ctl Reg */
#define CECNTL	0x170	/* CRC Counter L */
#define CECNTH	0x171	/* CRC Counter H */
#define CECCR	0x173	/* CRC Counter Ctl Reg */
#define ABCNTL	0x174	/* Abort frame Counter L */
#define ABCNTH	0x175	/* Abort frame Counter H */
#define ABCCR	0x177	/* Abort frame Counter Ctl Reg */
#define SHCNTL	0x178	/* Short frame Counter L */
#define SHCNTH	0x179	/* Short frame Counter H */
#define SHCCR	0x17b	/* Short frame Counter Ctl Reg */
#define RSCNTL	0x17c	/* Residual bit Counter L */
#define RSCNTH	0x17d	/* Residual bit Counter H */
#define RSCCR	0x17f	/* Residual bit Counter Ctl Reg */

/* Register Programming Constants */

#define IR0_DMIC	0x00000001
#define IR0_DMIB	0x00000002
#define IR0_DMIA	0x00000004
#define IR0_EFT		0x00000008
#define IR0_DMAREQ	0x00010000
#define IR0_TXINT	0x00020000
#define IR0_RXINTB	0x00040000
#define IR0_RXINTA	0x00080000
#define IR0_TXRDY	0x00100000
#define IR0_RXRDY	0x00200000

#define MD0_CRC16_0	0x00
#define MD0_CRC16_1	0x01
#define MD0_CRC32	0x02
#define MD0_CRC_CCITT	0x03
#define MD0_CRCC0	0x04
#define MD0_CRCC1	0x08
#define MD0_AUTO_ENA	0x10
#define MD0_ASYNC	0x00
#define MD0_BY_MSYNC	0x20
#define MD0_BY_BISYNC	0x40
#define MD0_BY_EXT	0x60
#define MD0_BIT_SYNC	0x80
#define MD0_TRANSP	0xc0

#define MD0_HDLC        0x80	/* Bit-sync HDLC mode */

#define MD0_CRC_NONE	0x00
#define MD0_CRC_16_0	0x04
#define MD0_CRC_16	0x05
#define MD0_CRC_ITU32	0x06
#define MD0_CRC_ITU	0x07

#define MD1_NOADDR	0x00
#define MD1_SADDR1	0x40
#define MD1_SADDR2	0x80
#define MD1_DADDR	0xc0

#define MD2_NRZI_IEEE	0x40
#define MD2_MANCHESTER	0x80
#define MD2_FM_MARK	0xA0
#define MD2_FM_SPACE	0xC0
#define MD2_LOOPBACK	0x03	/* Local data Loopback */

#define MD2_F_DUPLEX	0x00
#define MD2_AUTO_ECHO	0x01
#define MD2_LOOP_HI_Z	0x02
#define MD2_LOOP_MIR	0x03
#define MD2_ADPLL_X8	0x00
#define MD2_ADPLL_X16	0x08
#define MD2_ADPLL_X32	0x10
#define MD2_NRZ		0x00
#define MD2_NRZI	0x20
#define MD2_NRZ_IEEE	0x40
#define MD2_MANCH	0x00
#define MD2_FM1		0x20
#define MD2_FM0		0x40
#define MD2_FM		0x80

#define CTL_RTS		0x01
#define CTL_DTR		0x02
#define CTL_SYN		0x04
#define CTL_IDLC	0x10
#define CTL_UDRNC	0x20
#define CTL_URSKP	0x40
#define CTL_URCT	0x80

#define CTL_NORTS	0x01
#define CTL_NODTR	0x02
#define CTL_IDLE	0x10

#define	RXS_BR0		0x01
#define	RXS_BR1		0x02
#define	RXS_BR2		0x04
#define	RXS_BR3		0x08
#define	RXS_ECLK	0x00
#define	RXS_ECLK_NS	0x20
#define	RXS_IBRG	0x40
#define	RXS_PLL1	0x50
#define	RXS_PLL2	0x60
#define	RXS_PLL3	0x70
#define	RXS_DRTXC	0x80

#define	TXS_BR0		0x01
#define	TXS_BR1		0x02
#define	TXS_BR2		0x04
#define	TXS_BR3		0x08
#define	TXS_ECLK	0x00
#define	TXS_IBRG	0x40
#define	TXS_RCLK	0x60
#define	TXS_DTRXC	0x80

#define	EXS_RES0	0x01
#define	EXS_RES1	0x02
#define	EXS_RES2	0x04
#define	EXS_TES0	0x10
#define	EXS_TES1	0x20
#define	EXS_TES2	0x40

#define CLK_BRG_MASK	0x0F
#define CLK_PIN_OUT	0x80
#define CLK_LINE    	0x00	/* clock line input */
#define CLK_BRG     	0x40	/* internal baud rate generator */
#define CLK_TX_RXCLK	0x60	/* TX clock from RX clock */

#define CMD_RX_RST	0x11
#define CMD_RX_ENA	0x12
#define CMD_RX_DIS	0x13
#define CMD_RX_CRC_INIT	0x14
#define CMD_RX_MSG_REJ	0x15
#define CMD_RX_MP_SRCH	0x16
#define CMD_RX_CRC_EXC	0x17
#define CMD_RX_CRC_FRC	0x18
#define CMD_TX_RST	0x01
#define CMD_TX_ENA	0x02
#define CMD_TX_DISA	0x03
#define CMD_TX_CRC_INIT	0x04
#define CMD_TX_CRC_EXC	0x05
#define CMD_TX_EOM	0x06
#define CMD_TX_ABORT	0x07
#define CMD_TX_MP_ON	0x08
#define CMD_TX_BUF_CLR	0x09
#define CMD_TX_DISB	0x0b
#define CMD_CH_RST	0x21
#define CMD_SRCH_MODE	0x31
#define CMD_NOP		0x00

#define CMD_RESET	0x21
#define CMD_TX_ENABLE	0x02
#define CMD_RX_ENABLE	0x12

#define ST0_RXRDY	0x01
#define ST0_TXRDY	0x02
#define ST0_RXINTB	0x20
#define ST0_RXINTA	0x40
#define ST0_TXINT	0x80

#define ST1_IDLE	0x01
#define ST1_ABORT	0x02
#define ST1_CDCD	0x04
#define ST1_CCTS	0x08
#define ST1_SYN_FLAG	0x10
#define ST1_CLMD	0x20
#define ST1_TXIDLE	0x40
#define ST1_UDRN	0x80

#define ST2_CRCE	0x04
#define ST2_ONRN	0x08
#define ST2_RBIT	0x10
#define ST2_ABORT	0x20
#define ST2_SHORT	0x40
#define ST2_EOM		0x80

#define ST3_RX_ENA	0x01
#define ST3_TX_ENA	0x02
#define ST3_DCD		0x04
#define ST3_CTS		0x08
#define ST3_SRCH_MODE	0x10
#define ST3_SLOOP	0x20
#define ST3_GPI		0x80

#define ST4_RDNR	0x01
#define ST4_RDCR	0x02
#define ST4_TDNR	0x04
#define ST4_TDCR	0x08
#define ST4_OCLM	0x20
#define ST4_CFT		0x40
#define ST4_CGPI	0x80

#define FST_CRCEF	0x04
#define FST_OVRNF	0x08
#define FST_RBIF	0x10
#define FST_ABTF	0x20
#define FST_SHRTF	0x40
#define FST_EOMF	0x80

#define IE0_RXRDY	0x01
#define IE0_TXRDY	0x02
#define IE0_RXINTB	0x20
#define IE0_RXINTA	0x40
#define IE0_TXINT	0x80
#define IE0_UDRN	0x00008000 /* TX underrun MSCI interrupt enable */
#define IE0_CDCD	0x00000400 /* CD level change interrupt enable */

#define IE1_IDLD	0x01
#define IE1_ABTD	0x02
#define IE1_CDCD	0x04
#define IE1_CCTS	0x08
#define IE1_SYNCD	0x10
#define IE1_CLMD	0x20
#define IE1_IDL		0x40
#define IE1_UDRN	0x80

#define IE2_CRCE	0x04
#define IE2_OVRN	0x08
#define IE2_RBIT	0x10
#define IE2_ABT		0x20
#define IE2_SHRT	0x40
#define IE2_EOM		0x80

#define IE4_RDNR	0x01
#define IE4_RDCR	0x02
#define IE4_TDNR	0x04
#define IE4_TDCR	0x08
#define IE4_OCLM	0x20
#define IE4_CFT		0x40
#define IE4_CGPI	0x80

#define FIE_CRCEF	0x04
#define FIE_OVRNF	0x08
#define FIE_RBIF	0x10
#define FIE_ABTF	0x20
#define FIE_SHRTF	0x40
#define FIE_EOMF	0x80

#define DSR_DWE		0x01
#define DSR_DE		0x02
#define DSR_REF		0x04
#define DSR_UDRF	0x04
#define DSR_COA		0x08
#define DSR_COF		0x10
#define DSR_BOF		0x20
#define DSR_EOM		0x40
#define DSR_EOT		0x80

#define DIR_REF		0x04
#define DIR_UDRF	0x04
#define DIR_COA		0x08
#define DIR_COF		0x10
#define DIR_BOF		0x20
#define DIR_EOM		0x40
#define DIR_EOT		0x80

#define DIR_REFE	0x04
#define DIR_UDRFE	0x04
#define DIR_COAE	0x08
#define DIR_COFE	0x10
#define DIR_BOFE	0x20
#define DIR_EOME	0x40
#define DIR_EOTE	0x80

#define DMR_CNTE	0x02
#define DMR_NF		0x04
#define DMR_SEOME	0x08
#define DMR_TMOD	0x10

#define DMER_DME        0x80	/* DMA Master Enable */

#define DCR_SW_ABT	0x01
#define DCR_FCT_CLR	0x02

#define DCR_ABORT	0x01
#define DCR_CLEAR_EOF	0x02

#define PCR_COTE	0x80
#define PCR_PR0		0x01
#define PCR_PR1		0x02
#define PCR_PR2		0x04
#define PCR_CCC		0x08
#define PCR_BRC		0x10
#define PCR_OSB		0x40
#define PCR_BURST	0x80

#endif /* (__HD64572_H) */
