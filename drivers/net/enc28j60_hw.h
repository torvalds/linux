/*
 * enc28j60_hw.h: EDTP FrameThrower style enc28j60 registers
 *
 * $Id: enc28j60_hw.h,v 1.9 2007/12/14 11:59:16 claudio Exp $
 */

#ifndef _ENC28J60_HW_H
#define _ENC28J60_HW_H

/*
 * ENC28J60 Control Registers
 * Control register definitions are a combination of address,
 * bank number, and Ethernet/MAC/PHY indicator bits.
 * - Register address	(bits 0-4)
 * - Bank number	(bits 5-6)
 * - MAC/MII indicator	(bit 7)
 */
#define ADDR_MASK	0x1F
#define BANK_MASK	0x60
#define SPRD_MASK	0x80
/* All-bank registers */
#define EIE		0x1B
#define EIR		0x1C
#define ESTAT		0x1D
#define ECON2		0x1E
#define ECON1		0x1F
/* Bank 0 registers */
#define ERDPTL		(0x00|0x00)
#define ERDPTH		(0x01|0x00)
#define EWRPTL		(0x02|0x00)
#define EWRPTH		(0x03|0x00)
#define ETXSTL		(0x04|0x00)
#define ETXSTH		(0x05|0x00)
#define ETXNDL		(0x06|0x00)
#define ETXNDH		(0x07|0x00)
#define ERXSTL		(0x08|0x00)
#define ERXSTH		(0x09|0x00)
#define ERXNDL		(0x0A|0x00)
#define ERXNDH		(0x0B|0x00)
#define ERXRDPTL	(0x0C|0x00)
#define ERXRDPTH	(0x0D|0x00)
#define ERXWRPTL	(0x0E|0x00)
#define ERXWRPTH	(0x0F|0x00)
#define EDMASTL		(0x10|0x00)
#define EDMASTH		(0x11|0x00)
#define EDMANDL		(0x12|0x00)
#define EDMANDH		(0x13|0x00)
#define EDMADSTL	(0x14|0x00)
#define EDMADSTH	(0x15|0x00)
#define EDMACSL		(0x16|0x00)
#define EDMACSH		(0x17|0x00)
/* Bank 1 registers */
#define EHT0		(0x00|0x20)
#define EHT1		(0x01|0x20)
#define EHT2		(0x02|0x20)
#define EHT3		(0x03|0x20)
#define EHT4		(0x04|0x20)
#define EHT5		(0x05|0x20)
#define EHT6		(0x06|0x20)
#define EHT7		(0x07|0x20)
#define EPMM0		(0x08|0x20)
#define EPMM1		(0x09|0x20)
#define EPMM2		(0x0A|0x20)
#define EPMM3		(0x0B|0x20)
#define EPMM4		(0x0C|0x20)
#define EPMM5		(0x0D|0x20)
#define EPMM6		(0x0E|0x20)
#define EPMM7		(0x0F|0x20)
#define EPMCSL		(0x10|0x20)
#define EPMCSH		(0x11|0x20)
#define EPMOL		(0x14|0x20)
#define EPMOH		(0x15|0x20)
#define EWOLIE		(0x16|0x20)
#define EWOLIR		(0x17|0x20)
#define ERXFCON		(0x18|0x20)
#define EPKTCNT		(0x19|0x20)
/* Bank 2 registers */
#define MACON1		(0x00|0x40|SPRD_MASK)
/* #define MACON2	(0x01|0x40|SPRD_MASK) */
#define MACON3		(0x02|0x40|SPRD_MASK)
#define MACON4		(0x03|0x40|SPRD_MASK)
#define MABBIPG		(0x04|0x40|SPRD_MASK)
#define MAIPGL		(0x06|0x40|SPRD_MASK)
#define MAIPGH		(0x07|0x40|SPRD_MASK)
#define MACLCON1	(0x08|0x40|SPRD_MASK)
#define MACLCON2	(0x09|0x40|SPRD_MASK)
#define MAMXFLL		(0x0A|0x40|SPRD_MASK)
#define MAMXFLH		(0x0B|0x40|SPRD_MASK)
#define MAPHSUP		(0x0D|0x40|SPRD_MASK)
#define MICON		(0x11|0x40|SPRD_MASK)
#define MICMD		(0x12|0x40|SPRD_MASK)
#define MIREGADR	(0x14|0x40|SPRD_MASK)
#define MIWRL		(0x16|0x40|SPRD_MASK)
#define MIWRH		(0x17|0x40|SPRD_MASK)
#define MIRDL		(0x18|0x40|SPRD_MASK)
#define MIRDH		(0x19|0x40|SPRD_MASK)
/* Bank 3 registers */
#define MAADR1		(0x00|0x60|SPRD_MASK)
#define MAADR0		(0x01|0x60|SPRD_MASK)
#define MAADR3		(0x02|0x60|SPRD_MASK)
#define MAADR2		(0x03|0x60|SPRD_MASK)
#define MAADR5		(0x04|0x60|SPRD_MASK)
#define MAADR4		(0x05|0x60|SPRD_MASK)
#define EBSTSD		(0x06|0x60)
#define EBSTCON		(0x07|0x60)
#define EBSTCSL		(0x08|0x60)
#define EBSTCSH		(0x09|0x60)
#define MISTAT		(0x0A|0x60|SPRD_MASK)
#define EREVID		(0x12|0x60)
#define ECOCON		(0x15|0x60)
#define EFLOCON		(0x17|0x60)
#define EPAUSL		(0x18|0x60)
#define EPAUSH		(0x19|0x60)
/* PHY registers */
#define PHCON1		0x00
#define PHSTAT1		0x01
#define PHHID1		0x02
#define PHHID2		0x03
#define PHCON2		0x10
#define PHSTAT2		0x11
#define PHIE		0x12
#define PHIR		0x13
#define PHLCON		0x14

/* ENC28J60 EIE Register Bit Definitions */
#define EIE_INTIE	0x80
#define EIE_PKTIE	0x40
#define EIE_DMAIE	0x20
#define EIE_LINKIE	0x10
#define EIE_TXIE	0x08
/* #define EIE_WOLIE	0x04 (reserved) */
#define EIE_TXERIE	0x02
#define EIE_RXERIE	0x01
/* ENC28J60 EIR Register Bit Definitions */
#define EIR_PKTIF	0x40
#define EIR_DMAIF	0x20
#define EIR_LINKIF	0x10
#define EIR_TXIF	0x08
/* #define EIR_WOLIF	0x04 (reserved) */
#define EIR_TXERIF	0x02
#define EIR_RXERIF	0x01
/* ENC28J60 ESTAT Register Bit Definitions */
#define ESTAT_INT	0x80
#define ESTAT_LATECOL	0x10
#define ESTAT_RXBUSY	0x04
#define ESTAT_TXABRT	0x02
#define ESTAT_CLKRDY	0x01
/* ENC28J60 ECON2 Register Bit Definitions */
#define ECON2_AUTOINC	0x80
#define ECON2_PKTDEC	0x40
#define ECON2_PWRSV	0x20
#define ECON2_VRPS	0x08
/* ENC28J60 ECON1 Register Bit Definitions */
#define ECON1_TXRST	0x80
#define ECON1_RXRST	0x40
#define ECON1_DMAST	0x20
#define ECON1_CSUMEN	0x10
#define ECON1_TXRTS	0x08
#define ECON1_RXEN	0x04
#define ECON1_BSEL1	0x02
#define ECON1_BSEL0	0x01
/* ENC28J60 MACON1 Register Bit Definitions */
#define MACON1_LOOPBK	0x10
#define MACON1_TXPAUS	0x08
#define MACON1_RXPAUS	0x04
#define MACON1_PASSALL	0x02
#define MACON1_MARXEN	0x01
/* ENC28J60 MACON2 Register Bit Definitions */
#define MACON2_MARST	0x80
#define MACON2_RNDRST	0x40
#define MACON2_MARXRST	0x08
#define MACON2_RFUNRST	0x04
#define MACON2_MATXRST	0x02
#define MACON2_TFUNRST	0x01
/* ENC28J60 MACON3 Register Bit Definitions */
#define MACON3_PADCFG2	0x80
#define MACON3_PADCFG1	0x40
#define MACON3_PADCFG0	0x20
#define MACON3_TXCRCEN	0x10
#define MACON3_PHDRLEN	0x08
#define MACON3_HFRMLEN	0x04
#define MACON3_FRMLNEN	0x02
#define MACON3_FULDPX	0x01
/* ENC28J60 MICMD Register Bit Definitions */
#define MICMD_MIISCAN	0x02
#define MICMD_MIIRD	0x01
/* ENC28J60 MISTAT Register Bit Definitions */
#define MISTAT_NVALID	0x04
#define MISTAT_SCAN	0x02
#define MISTAT_BUSY	0x01
/* ENC28J60 ERXFCON Register Bit Definitions */
#define ERXFCON_UCEN	0x80
#define ERXFCON_ANDOR	0x40
#define ERXFCON_CRCEN	0x20
#define ERXFCON_PMEN	0x10
#define ERXFCON_MPEN	0x08
#define ERXFCON_HTEN	0x04
#define ERXFCON_MCEN	0x02
#define ERXFCON_BCEN	0x01

/* ENC28J60 PHY PHCON1 Register Bit Definitions */
#define PHCON1_PRST	0x8000
#define PHCON1_PLOOPBK	0x4000
#define PHCON1_PPWRSV	0x0800
#define PHCON1_PDPXMD	0x0100
/* ENC28J60 PHY PHSTAT1 Register Bit Definitions */
#define PHSTAT1_PFDPX	0x1000
#define PHSTAT1_PHDPX	0x0800
#define PHSTAT1_LLSTAT	0x0004
#define PHSTAT1_JBSTAT	0x0002
/* ENC28J60 PHY PHSTAT2 Register Bit Definitions */
#define PHSTAT2_TXSTAT	(1 << 13)
#define PHSTAT2_RXSTAT	(1 << 12)
#define PHSTAT2_COLSTAT	(1 << 11)
#define PHSTAT2_LSTAT	(1 << 10)
#define PHSTAT2_DPXSTAT	(1 << 9)
#define PHSTAT2_PLRITY	(1 << 5)
/* ENC28J60 PHY PHCON2 Register Bit Definitions */
#define PHCON2_FRCLINK	0x4000
#define PHCON2_TXDIS	0x2000
#define PHCON2_JABBER	0x0400
#define PHCON2_HDLDIS	0x0100
/* ENC28J60 PHY PHIE Register Bit Definitions */
#define PHIE_PLNKIE	(1 << 4)
#define PHIE_PGEIE	(1 << 1)
/* ENC28J60 PHY PHIR Register Bit Definitions */
#define PHIR_PLNKIF	(1 << 4)
#define PHIR_PGEIF	(1 << 1)

/* ENC28J60 Packet Control Byte Bit Definitions */
#define PKTCTRL_PHUGEEN		0x08
#define PKTCTRL_PPADEN		0x04
#define PKTCTRL_PCRCEN		0x02
#define PKTCTRL_POVERRIDE	0x01

/* ENC28J60 Transmit Status Vector */
#define TSV_TXBYTECNT		0
#define TSV_TXCOLLISIONCNT	16
#define TSV_TXCRCERROR		20
#define TSV_TXLENCHKERROR	21
#define TSV_TXLENOUTOFRANGE	22
#define TSV_TXDONE		23
#define TSV_TXMULTICAST		24
#define TSV_TXBROADCAST		25
#define TSV_TXPACKETDEFER	26
#define TSV_TXEXDEFER		27
#define TSV_TXEXCOLLISION	28
#define TSV_TXLATECOLLISION	29
#define TSV_TXGIANT		30
#define TSV_TXUNDERRUN		31
#define TSV_TOTBYTETXONWIRE	32
#define TSV_TXCONTROLFRAME	48
#define TSV_TXPAUSEFRAME	49
#define TSV_BACKPRESSUREAPP	50
#define TSV_TXVLANTAGFRAME	51

#define TSV_SIZE		7
#define TSV_BYTEOF(x)		((x) / 8)
#define TSV_BITMASK(x)		(1 << ((x) % 8))
#define TSV_GETBIT(x, y)	(((x)[TSV_BYTEOF(y)] & TSV_BITMASK(y)) ? 1 : 0)

/* ENC28J60 Receive Status Vector */
#define RSV_RXLONGEVDROPEV	16
#define RSV_CARRIEREV		18
#define RSV_CRCERROR		20
#define RSV_LENCHECKERR		21
#define RSV_LENOUTOFRANGE	22
#define RSV_RXOK		23
#define RSV_RXMULTICAST		24
#define RSV_RXBROADCAST		25
#define RSV_DRIBBLENIBBLE	26
#define RSV_RXCONTROLFRAME	27
#define RSV_RXPAUSEFRAME	28
#define RSV_RXUNKNOWNOPCODE	29
#define RSV_RXTYPEVLAN		30

#define RSV_SIZE		6
#define RSV_BITMASK(x)		(1 << ((x) - 16))
#define RSV_GETBIT(x, y)	(((x) & RSV_BITMASK(y)) ? 1 : 0)


/* SPI operation codes */
#define ENC28J60_READ_CTRL_REG	0x00
#define ENC28J60_READ_BUF_MEM	0x3A
#define ENC28J60_WRITE_CTRL_REG 0x40
#define ENC28J60_WRITE_BUF_MEM	0x7A
#define ENC28J60_BIT_FIELD_SET	0x80
#define ENC28J60_BIT_FIELD_CLR	0xA0
#define ENC28J60_SOFT_RESET	0xFF


/* buffer boundaries applied to internal 8K ram
 * entire available packet buffer space is allocated.
 * Give TX buffer space for one full ethernet frame (~1500 bytes)
 * receive buffer gets the rest */
#define TXSTART_INIT		0x1A00
#define TXEND_INIT		0x1FFF

/* Put RX buffer at 0 as suggested by the Errata datasheet */
#define RXSTART_INIT		0x0000
#define RXEND_INIT		0x19FF

/* maximum ethernet frame length */
#define MAX_FRAMELEN		1518

/* Preferred half duplex: LEDA: Link status LEDB: Rx/Tx activity */
#define ENC28J60_LAMPS_MODE	0x3476

#endif
