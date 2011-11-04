/*********************************************************
 *                                                       *
 * Definition of D-Link DE-620 Ethernet Pocket adapter   *
 *                                                       *
 *********************************************************/

/* DE-620's CMD port Command */
#define CS0		0x08	/* 1->0 command strobe */
#define ICEN		0x04	/* 0=enable DL3520 host interface */
#define DS0		0x02	/* 1->0 data strobe 0 */
#define DS1		0x01	/* 1->0 data strobe 1 */

#define WDIR		0x20	/* general 0=read  1=write */
#define RDIR		0x00	/*  (not 100% confirm ) */
#define PS2WDIR		0x00	/* ps/2 mode 1=read, 0=write */
#define PS2RDIR		0x20

#define IRQEN		0x10	/* 1 = enable printer IRQ line */
#define SELECTIN	0x08	/* 1 = select printer */
#define INITP		0x04	/* 0 = initial printer */
#define AUTOFEED	0x02	/* 1 = printer auto form feed */
#define STROBE		0x01	/* 0->1 data strobe */

#define RESET		0x08
#define NIS0		0x20	/* 0 = BNC, 1 = UTP */
#define NCTL0		0x10

/* DE-620 DIC Command */
#define W_DUMMY		0x00	/* DIC reserved command */
#define W_CR		0x20	/* DIC write command register */
#define W_NPR		0x40	/* DIC write Next Page Register */
#define W_TBR		0x60	/* DIC write Tx Byte Count 1 reg */
#define W_RSA		0x80	/* DIC write Remote Start Addr 1 */

/* DE-620's STAT port bits 7-4 */
#define EMPTY		0x80	/* 1 = receive buffer empty */
#define INTLEVEL	0x40	/* 1 = interrupt level is high */
#define TXBF1		0x20	/* 1 = transmit buffer 1 is in use */
#define TXBF0		0x10	/* 1 = transmit buffer 0 is in use */
#define READY		0x08	/* 1 = h/w ready to accept cmd/data */

/* IDC 1 Command */
#define	W_RSA1		0xa0	/* write remote start address 1 */
#define	W_RSA0		0xa1	/* write remote start address 0 */
#define	W_NPRF		0xa2	/* write next page register NPR15-NPR8 */
#define	W_DFR		0xa3	/* write delay factor register */
#define	W_CPR		0xa4	/* write current page register */
#define	W_SPR		0xa5	/* write start page register */
#define	W_EPR		0xa6	/* write end page register */
#define	W_SCR		0xa7	/* write system configuration register */
#define	W_TCR		0xa8	/* write Transceiver Configuration reg */
#define	W_EIP		0xa9	/* write EEPM Interface port */
#define	W_PAR0		0xaa	/* write physical address register 0 */
#define	W_PAR1		0xab	/* write physical address register 1 */
#define	W_PAR2		0xac	/* write physical address register 2 */
#define	W_PAR3		0xad	/* write physical address register 3 */
#define	W_PAR4		0xae	/* write physical address register 4 */
#define	W_PAR5		0xaf	/* write physical address register 5 */

/* IDC 2 Command */
#define	R_STS		0xc0	/* read status register */
#define	R_CPR		0xc1	/* read current page register */
#define	R_BPR		0xc2	/* read boundary page register */
#define	R_TDR		0xc3	/* read time domain reflectometry reg */

/* STATUS Register */
#define EEDI		0x80	/* EEPM DO pin */
#define TXSUC		0x40	/* tx success */
#define T16		0x20	/* tx fail 16 times */
#define TS1		0x40	/* 0=Tx success, 1=T16 */
#define TS0		0x20	/* 0=Tx success, 1=T16 */
#define RXGOOD		0x10	/* rx a good packet */
#define RXCRC		0x08	/* rx a CRC error packet */
#define RXSHORT		0x04	/* rx a short packet */
#define COLS		0x02	/* coaxial collision status */
#define LNKS		0x01	/* UTP link status */

/* Command Register */
#define CLEAR		0x10	/* reset part of hardware */
#define NOPER		0x08	/* No Operation */
#define RNOP		0x08
#define RRA		0x06	/* After RR then auto-advance NPR & BPR(=NPR-1) */
#define RRN		0x04	/* Normal Remote Read mode */
#define RW1		0x02	/* Remote Write tx buffer 1  ( page 6 - 11 ) */
#define RW0		0x00	/* Remote Write tx buffer 0  ( page 0 - 5 ) */
#define TXEN		0x01	/* 0->1 tx enable */

/* System Configuration Register */
#define TESTON		0x80	/* test host data transfer reliability */
#define SLEEP		0x40	/* sleep mode */
#if 0
#define FASTMODE	0x04	/* fast mode for intel 82360SL fast mode */
#define BYTEMODE	0x02	/* byte mode */
#else
#define FASTMODE	0x20	/* fast mode for intel 82360SL fast mode */
#define BYTEMODE	0x10	/* byte mode */
#endif
#define NIBBLEMODE	0x00	/* nibble mode */
#define IRQINV		0x08	/* turn off IRQ line inverter */
#define IRQNML		0x00	/* turn on IRQ line inverter */
#define INTON		0x04
#define AUTOFFSET	0x02	/* auto shift address to TPR+12 */
#define AUTOTX		0x01	/* auto tx when leave RW mode */

/* Transceiver Configuration Register */
#define JABBER		0x80	/* generate jabber condition */
#define TXSUCINT	0x40	/* enable tx success interrupt */
#define T16INT		0x20	/* enable T16 interrupt */
#define RXERRPKT	0x10	/* accept CRC error or short packet */
#define EXTERNALB2	0x0C	/* external loopback 2 */
#define EXTERNALB1	0x08	/* external loopback 1 */
#define INTERNALB	0x04	/* internal loopback */
#define NMLOPERATE	0x00	/* normal operation */
#define RXPBM		0x03	/* rx physical, broadcast, multicast */
#define RXPB		0x02	/* rx physical, broadcast */
#define RXALL		0x01	/* rx all packet */
#define RXOFF		0x00	/* rx disable */
