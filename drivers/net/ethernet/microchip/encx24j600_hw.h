/* SPDX-License-Identifier: GPL-2.0 */
/*
 * encx24j600_hw.h: Register definitions
 *
 */

#ifndef _ENCX24J600_HW_H
#define _ENCX24J600_HW_H

struct encx24j600_context {
	struct spi_device *spi;
	struct regmap *regmap;
	struct regmap *phymap;
	struct mutex mutex; /* mutex to protect access to regmap */
	int bank;
};

int devm_regmap_init_encx24j600(struct device *dev,
				struct encx24j600_context *ctx);

/* Single-byte instructions */
#define BANK_SELECT(bank) (0xC0 | ((bank & (BANK_MASK >> BANK_SHIFT)) << 1))
#define B0SEL 0xC0		/* Bank 0 Select */
#define B1SEL 0xC2		/* Bank 1 Select */
#define B2SEL 0xC4		/* Bank 2 Select */
#define B3SEL 0xC6		/* Bank 3 Select */
#define SETETHRST 0xCA		/* System Reset */
#define FCDISABLE 0xE0		/* Flow Control Disable */
#define FCSINGLE 0xE2		/* Flow Control Single */
#define FCMULTIPLE 0xE4		/* Flow Control Multiple */
#define FCCLEAR 0xE6		/* Flow Control Clear */
#define SETPKTDEC 0xCC		/* Decrement Packet Counter */
#define DMASTOP 0xD2		/* DMA Stop */
#define DMACKSUM 0xD8		/* DMA Start Checksum */
#define DMACKSUMS 0xDA		/* DMA Start Checksum with Seed */
#define DMACOPY 0xDC		/* DMA Start Copy */
#define DMACOPYS 0xDE		/* DMA Start Copy and Checksum with Seed */
#define SETTXRTS 0xD4		/* Request Packet Transmission */
#define ENABLERX 0xE8		/* Enable RX */
#define DISABLERX 0xEA		/* Disable RX */
#define SETEIE 0xEC		/* Enable Interrupts */
#define CLREIE 0xEE		/* Disable Interrupts */

/* Two byte instructions */
#define RBSEL 0xC8		/* Read Bank Select */

/* Three byte instructions */
#define WGPRDPT 0x60		/* Write EGPRDPT */
#define RGPRDPT 0x62		/* Read EGPRDPT */
#define WRXRDPT 0x64		/* Write ERXRDPT */
#define RRXRDPT 0x66		/* Read ERXRDPT */
#define WUDARDPT 0x68		/* Write EUDARDPT */
#define RUDARDPT 0x6A		/* Read EUDARDPT */
#define WGPWRPT 0x6C		/* Write EGPWRPT */
#define RGPWRPT 0x6E		/* Read EGPWRPT */
#define WRXWRPT 0x70		/* Write ERXWRPT */
#define RRXWRPT 0x72		/* Read ERXWRPT */
#define WUDAWRPT 0x74		/* Write EUDAWRPT */
#define RUDAWRPT 0x76		/* Read EUDAWRPT */

/* n byte instructions */
#define RCRCODE 0x00
#define WCRCODE 0x40
#define BFSCODE 0x80
#define BFCCODE 0xA0
#define RCR(addr) (RCRCODE | (addr & ADDR_MASK)) /* Read Control Register */
#define WCR(addr) (WCRCODE | (addr & ADDR_MASK)) /* Write Control Register */
#define RCRU 0x20		/* Read Control Register Unbanked */
#define WCRU 0x22		/* Write Control Register Unbanked */
#define BFS(addr) (BFSCODE | (addr & ADDR_MASK)) /* Bit Field Set */
#define BFC(addr) (BFCCODE | (addr & ADDR_MASK)) /* Bit Field Clear */
#define BFSU 0x24		/* Bit Field Set Unbanked */
#define BFCU 0x26		/* Bit Field Clear Unbanked */
#define RGPDATA 0x28		/* Read EGPDATA */
#define WGPDATA 0x2A		/* Write EGPDATA */
#define RRXDATA 0x2C		/* Read ERXDATA */
#define WRXDATA 0x2E		/* Write ERXDATA */
#define RUDADATA 0x30		/* Read EUDADATA */
#define WUDADATA 0x32		/* Write EUDADATA */

#define SFR_REG_COUNT	0xA0

/* ENC424J600 Control Registers
 * Control register definitions are a combination of address
 * and bank number
 * - Register address (bits 0-4)
 * - Bank number (bits 5-6)
 */
#define ADDR_MASK 0x1F
#define BANK_MASK 0x60
#define BANK_SHIFT 5

/* All-bank registers */
#define EUDAST 0x16
#define EUDAND 0x18
#define ESTAT 0x1A
#define EIR 0x1C
#define ECON1 0x1E

/* Bank 0 registers */
#define ETXST (0x00 | 0x00)
#define ETXLEN (0x02 | 0x00)
#define ERXST (0x04 | 0x00)
#define ERXTAIL (0x06 | 0x00)
#define ERXHEAD (0x08 | 0x00)
#define EDMAST (0x0A | 0x00)
#define EDMALEN (0x0C | 0x00)
#define EDMADST (0x0E | 0x00)
#define EDMACS (0x10 | 0x00)
#define ETXSTAT (0x12 | 0x00)
#define ETXWIRE (0x14 | 0x00)

/* Bank 1 registers */
#define EHT1 (0x00 | 0x20)
#define EHT2 (0x02 | 0x20)
#define EHT3 (0x04 | 0x20)
#define EHT4 (0x06 | 0x20)
#define EPMM1 (0x08 | 0x20)
#define EPMM2 (0x0A | 0x20)
#define EPMM3 (0x0C | 0x20)
#define EPMM4 (0x0E | 0x20)
#define EPMCS (0x10 | 0x20)
#define EPMO (0x12 | 0x20)
#define ERXFCON (0x14 | 0x20)

/* Bank 2 registers */
#define MACON1 (0x00 | 0x40)
#define MACON2 (0x02 | 0x40)
#define MABBIPG (0x04 | 0x40)
#define MAIPG (0x06 | 0x40)
#define MACLCON (0x08 | 0x40)
#define MAMXFL (0x0A | 0x40)
#define MICMD (0x12 | 0x40)
#define MIREGADR (0x14 | 0x40)

/* Bank 3 registers */
#define MAADR3 (0x00 | 0x60)
#define MAADR2 (0x02 | 0x60)
#define MAADR1 (0x04 | 0x60)
#define MIWR (0x06 | 0x60)
#define MIRD (0x08 | 0x60)
#define MISTAT (0x0A | 0x60)
#define EPAUS (0x0C | 0x60)
#define ECON2 (0x0E | 0x60)
#define ERXWM (0x10 | 0x60)
#define EIE (0x12 | 0x60)
#define EIDLED (0x14 | 0x60)

/* Unbanked registers */
#define EGPDATA (0x00 | 0x80)
#define ERXDATA (0x02 | 0x80)
#define EUDADATA (0x04 | 0x80)
#define EGPRDPT (0x06 | 0x80)
#define EGPWRPT (0x08 | 0x80)
#define ERXRDPT (0x0A | 0x80)
#define ERXWRPT (0x0C | 0x80)
#define EUDARDPT (0x0E | 0x80)
#define EUDAWRPT (0x10 | 0x80)


/* Register bit definitions */
/* ESTAT */
#define INT (1 << 15)
#define FCIDLE (1 << 14)
#define RXBUSY (1 << 13)
#define CLKRDY (1 << 12)
#define PHYDPX (1 << 10)
#define PHYLNK (1 << 8)

/* EIR */
#define CRYPTEN (1 << 15)
#define MODEXIF (1 << 14)
#define HASHIF (1 << 13)
#define AESIF (1 << 12)
#define LINKIF (1 << 11)
#define PKTIF (1 << 6)
#define DMAIF (1 << 5)
#define TXIF (1 << 3)
#define TXABTIF (1 << 2)
#define RXABTIF (1 << 1)
#define PCFULIF (1 << 0)

/* ECON1 */
#define MODEXST (1 << 15)
#define HASHEN (1 << 14)
#define HASHOP (1 << 13)
#define HASHLST (1 << 12)
#define AESST (1 << 11)
#define AESOP1 (1 << 10)
#define AESOP0 (1 << 9)
#define PKTDEC (1 << 8)
#define FCOP1 (1 << 7)
#define FCOP0 (1 << 6)
#define DMAST (1 << 5)
#define DMACPY (1 << 4)
#define DMACSSD (1 << 3)
#define DMANOCS (1 << 2)
#define TXRTS (1 << 1)
#define RXEN (1 << 0)

/* ETXSTAT */
#define LATECOL (1 << 10)
#define MAXCOL (1 << 9)
#define EXDEFER (1 << 8)
#define ETXSTATL_DEFER (1 << 7)
#define CRCBAD (1 << 4)
#define COLCNT_MASK 0xF

/* ERXFCON */
#define HTEN (1 << 15)
#define MPEN (1 << 14)
#define NOTPM (1 << 12)
#define PMEN3 (1 << 11)
#define PMEN2 (1 << 10)
#define PMEN1 (1 << 9)
#define PMEN0 (1 << 8)
#define CRCEEN (1 << 7)
#define CRCEN (1 << 6)
#define RUNTEEN (1 << 5)
#define RUNTEN (1 << 4)
#define UCEN (1 << 3)
#define NOTMEEN (1 << 2)
#define MCEN (1 << 1)
#define BCEN (1 << 0)

/* MACON1 */
#define LOOPBK (1 << 4)
#define RXPAUS (1 << 2)
#define PASSALL (1 << 1)

/* MACON2 */
#define MACON2_DEFER (1 << 14)
#define BPEN (1 << 13)
#define NOBKOFF (1 << 12)
#define PADCFG2 (1 << 7)
#define PADCFG1 (1 << 6)
#define PADCFG0 (1 << 5)
#define TXCRCEN (1 << 4)
#define PHDREN (1 << 3)
#define HFRMEN (1 << 2)
#define MACON2_RSV1 (1 << 1)
#define FULDPX (1 << 0)

/* MAIPG */
/* value of the high byte is given by the reserved bits,
 * value of the low byte is recomended setting of the
 * IPG parameter.
 */
#define MAIPGH_VAL 0x0C
#define MAIPGL_VAL 0x12

/* MIREGADRH */
#define MIREGADR_VAL (1 << 8)

/* MIREGADRL */
#define PHREG_MASK 0x1F

/* MICMD */
#define MIISCAN (1 << 1)
#define MIIRD (1 << 0)

/* MISTAT */
#define NVALID (1 << 2)
#define SCAN (1 << 1)
#define BUSY (1 << 0)

/* ECON2 */
#define ETHEN (1 << 15)
#define STRCH (1 << 14)
#define TXMAC (1 << 13)
#define SHA1MD5 (1 << 12)
#define COCON3 (1 << 11)
#define COCON2 (1 << 10)
#define COCON1 (1 << 9)
#define COCON0 (1 << 8)
#define AUTOFC (1 << 7)
#define TXRST (1 << 6)
#define RXRST (1 << 5)
#define ETHRST (1 << 4)
#define MODLEN1 (1 << 3)
#define MODLEN0 (1 << 2)
#define AESLEN1 (1 << 1)
#define AESLEN0 (1 << 0)

/* EIE */
#define INTIE (1 << 15)
#define MODEXIE (1 << 14)
#define HASHIE (1 << 13)
#define AESIE (1 << 12)
#define LINKIE (1 << 11)
#define PKTIE (1 << 6)
#define DMAIE (1 << 5)
#define TXIE (1 << 3)
#define TXABTIE (1 << 2)
#define RXABTIE (1 << 1)
#define PCFULIE (1 << 0)

/* EIDLED */
#define LACFG3 (1 << 15)
#define LACFG2 (1 << 14)
#define LACFG1 (1 << 13)
#define LACFG0 (1 << 12)
#define LBCFG3 (1 << 11)
#define LBCFG2 (1 << 10)
#define LBCFG1 (1 << 9)
#define LBCFG0 (1 << 8)
#define DEVID_SHIFT 5
#define DEVID_MASK (0x7 << DEVID_SHIFT)
#define REVID_SHIFT 0
#define REVID_MASK (0x1F << REVID_SHIFT)

/* PHY registers */
#define PHCON1 0x00
#define PHSTAT1 0x01
#define PHANA 0x04
#define PHANLPA 0x05
#define PHANE 0x06
#define PHCON2 0x11
#define PHSTAT2 0x1B
#define PHSTAT3 0x1F

/* PHCON1 */
#define PRST (1 << 15)
#define PLOOPBK (1 << 14)
#define SPD100 (1 << 13)
#define ANEN (1 << 12)
#define PSLEEP (1 << 11)
#define RENEG (1 << 9)
#define PFULDPX (1 << 8)

/* PHSTAT1 */
#define FULL100 (1 << 14)
#define HALF100 (1 << 13)
#define FULL10 (1 << 12)
#define HALF10 (1 << 11)
#define ANDONE (1 << 5)
#define LRFAULT (1 << 4)
#define ANABLE (1 << 3)
#define LLSTAT (1 << 2)
#define EXTREGS (1 << 0)

/* PHSTAT2 */
#define PLRITY (1 << 4)

/* PHSTAT3 */
#define PHY3SPD100 (1 << 3)
#define PHY3DPX (1 << 4)
#define SPDDPX_SHIFT 2
#define SPDDPX_MASK (0x7 << SPDDPX_SHIFT)

/* PHANA */
/* Default value for PHY initialization*/
#define PHANA_DEFAULT 0x05E1

/* PHANE */
#define PDFLT (1 << 4)
#define LPARCD (1 << 1)
#define LPANABL (1 << 0)

#define EUDAST_TEST_VAL 0x1234

#define TSV_SIZE 7

#define ENCX24J600_DEV_ID 0x1

/* Configuration */

/* Led is on when the link is present and driven low
 * temporarily when packet is TX'd or RX'd
 */
#define LED_A_SETTINGS 0xC

/* Led is on if the link is in 100 Mbps mode */
#define LED_B_SETTINGS 0x8

/* maximum ethernet frame length
 * Currently not used as a limit anywhere
 * (we're using the "huge frame enable" feature of
 * enc424j600).
 */
#define MAX_FRAMELEN 1518

/* Size in bytes of the receive buffer in enc424j600.
 * Must be word aligned (even).
 */
#define RX_BUFFER_SIZE (15 * MAX_FRAMELEN)

/* Start of the general purpose area in sram */
#define SRAM_GP_START 0x0

/* SRAM size */
#define SRAM_SIZE 0x6000

/* Start of the receive buffer */
#define ERXST_VAL (SRAM_SIZE - RX_BUFFER_SIZE)

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

#define RSV_RUNTFILTERMATCH	31
#define RSV_NOTMEFILTERMATCH	32
#define RSV_HASHFILTERMATCH	33
#define RSV_MAGICPKTFILTERMATCH	34
#define RSV_PTRNMTCHFILTERMATCH	35
#define RSV_UNICASTFILTERMATCH	36

#define RSV_SIZE		8
#define RSV_BITMASK(x)		(1 << ((x) - 16))
#define RSV_GETBIT(x, y)	(((x) & RSV_BITMASK(y)) ? 1 : 0)

struct rsv {
	u16 next_packet;
	u16 len;
	u32 rxstat;
};

/* Put RX buffer at 0 as suggested by the Errata datasheet */

#define RXSTART_INIT		ERXST_VAL
#define RXEND_INIT		0x5FFF

int regmap_encx24j600_spi_write(void *context, u8 reg, const u8 *data,
				size_t count);
int regmap_encx24j600_spi_read(void *context, u8 reg, u8 *data, size_t count);


#endif
