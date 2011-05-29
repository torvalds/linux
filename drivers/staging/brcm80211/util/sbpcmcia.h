/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_SBPCMCIA_H
#define	_SBPCMCIA_H

/* All the addresses that are offsets in attribute space are divided
 * by two to account for the fact that odd bytes are invalid in
 * attribute space and our read/write routines make the space appear
 * as if they didn't exist. Still we want to show the original numbers
 * as documented in the hnd_pcmcia core manual.
 */

/* PCMCIA Function Configuration Registers */
#define	PCMCIA_FCR		(0x700 / 2)

#define	FCR0_OFF		0
#define	FCR1_OFF		(0x40 / 2)
#define	FCR2_OFF		(0x80 / 2)
#define	FCR3_OFF		(0xc0 / 2)

#define	PCMCIA_FCR0		(0x700 / 2)
#define	PCMCIA_FCR1		(0x740 / 2)
#define	PCMCIA_FCR2		(0x780 / 2)
#define	PCMCIA_FCR3		(0x7c0 / 2)

/* Standard PCMCIA FCR registers */

#define	PCMCIA_COR		0

#define	COR_RST			0x80
#define	COR_LEV			0x40
#define	COR_IRQEN		0x04
#define	COR_BLREN		0x01
#define	COR_FUNEN		0x01

#define	PCICIA_FCSR		(2 / 2)
#define	PCICIA_PRR		(4 / 2)
#define	PCICIA_SCR		(6 / 2)
#define	PCICIA_ESR		(8 / 2)

#define PCM_MEMOFF		0x0000
#define F0_MEMOFF		0x1000
#define F1_MEMOFF		0x2000
#define F2_MEMOFF		0x3000
#define F3_MEMOFF		0x4000

/* Memory base in the function fcr's */
#define MEM_ADDR0		(0x728 / 2)
#define MEM_ADDR1		(0x72a / 2)
#define MEM_ADDR2		(0x72c / 2)

/* PCMCIA base plus Srom access in fcr0: */
#define PCMCIA_ADDR0		(0x072e / 2)
#define PCMCIA_ADDR1		(0x0730 / 2)
#define PCMCIA_ADDR2		(0x0732 / 2)

#define MEM_SEG			(0x0734 / 2)
#define SROM_CS			(0x0736 / 2)
#define SROM_DATAL		(0x0738 / 2)
#define SROM_DATAH		(0x073a / 2)
#define SROM_ADDRL		(0x073c / 2)
#define SROM_ADDRH		(0x073e / 2)
#define	SROM_INFO2		(0x0772 / 2)	/* Corerev >= 2 && <= 5 */
#define	SROM_INFO		(0x07be / 2)	/* Corerev >= 6 */

/*  Values for srom_cs: */
#define SROM_IDLE		0
#define SROM_WRITE		1
#define SROM_READ		2
#define SROM_WEN		4
#define SROM_WDS		7
#define SROM_DONE		8

/* Fields in srom_info: */
#define	SRI_SZ_MASK		0x03
#define	SRI_BLANK		0x04
#define	SRI_OTP			0x80

#if !defined(ESTA_POSTMOGRIFY_REMOVAL)
/* CIS stuff */

/* The CIS stops where the FCRs start */
#define	CIS_SIZE		PCMCIA_FCR

/* CIS tuple length field max */
#define CIS_TUPLE_LEN_MAX	0xff

/* Standard tuples we know about */

#define CISTPL_NULL			0x00
#define	CISTPL_VERS_1		0x15	/* CIS ver, manf, dev & ver strings */
#define	CISTPL_MANFID		0x20	/* Manufacturer and device id */
#define CISTPL_FUNCID		0x21	/* Function identification */
#define	CISTPL_FUNCE		0x22	/* Function extensions */
#define	CISTPL_CFTABLE		0x1b	/* Config table entry */
#define	CISTPL_END		0xff	/* End of the CIS tuple chain */

/* Function identifier provides context for the function extensions tuple */
#define CISTPL_FID_SDIO		0x0c	/* Extensions defined by SDIO spec */

/* Function extensions for LANs (assumed for extensions other than SDIO) */
#define	LAN_TECH		1	/* Technology type */
#define	LAN_SPEED		2	/* Raw bit rate */
#define	LAN_MEDIA		3	/* Transmission media */
#define	LAN_NID			4	/* Node identification (aka MAC addr) */
#define	LAN_CONN		5	/* Connector standard */

/* CFTable */
#define CFTABLE_REGWIN_2K	0x08	/* 2k reg windows size */
#define CFTABLE_REGWIN_4K	0x10	/* 4k reg windows size */
#define CFTABLE_REGWIN_8K	0x20	/* 8k reg windows size */

/* Vendor unique tuples are 0x80-0x8f. Within Broadcom we'll
 * take one for HNBU, and use "extensions" (a la FUNCE) within it.
 */

#define	CISTPL_BRCM_HNBU	0x80

/* Subtypes of BRCM_HNBU: */

#define HNBU_SROMREV		0x00	/* A byte with sromrev, 1 if not present */
#define HNBU_CHIPID		0x01	/* Two 16bit values: PCI vendor & device id */
#define HNBU_BOARDREV		0x02	/* One byte board revision */
#define HNBU_PAPARMS		0x03	/* PA parameters: 8 (sromrev == 1)
					 * or 9 (sromrev > 1) bytes
					 */
#define HNBU_OEM		0x04	/* Eight bytes OEM data (sromrev == 1) */
#define HNBU_CC			0x05	/* Default country code (sromrev == 1) */
#define	HNBU_AA			0x06	/* Antennas available */
#define	HNBU_AG			0x07	/* Antenna gain */
#define HNBU_BOARDFLAGS		0x08	/* board flags (2 or 4 bytes) */
#define HNBU_LEDS		0x09	/* LED set */
#define HNBU_CCODE		0x0a	/* Country code (2 bytes ascii + 1 byte cctl)
					 * in rev 2
					 */
#define HNBU_CCKPO		0x0b	/* 2 byte cck power offsets in rev 3 */
#define HNBU_OFDMPO		0x0c	/* 4 byte 11g ofdm power offsets in rev 3 */
#define HNBU_GPIOTIMER		0x0d	/* 2 bytes with on/off values in rev 3 */
#define HNBU_PAPARMS5G		0x0e	/* 5G PA params */
#define HNBU_ANT5G		0x0f	/* 4328 5G antennas available/gain */
#define HNBU_RDLID		0x10	/* 2 byte USB remote downloader (RDL) product Id */
#define HNBU_RSSISMBXA2G	0x11	/* 4328 2G RSSI mid pt sel & board switch arch,
					 * 2 bytes, rev 3.
					 */
#define HNBU_RSSISMBXA5G	0x12	/* 4328 5G RSSI mid pt sel & board switch arch,
					 * 2 bytes, rev 3.
					 */
#define HNBU_XTALFREQ		0x13	/* 4 byte Crystal frequency in kilohertz */
#define HNBU_TRI2G		0x14	/* 4328 2G TR isolation, 1 byte */
#define HNBU_TRI5G		0x15	/* 4328 5G TR isolation, 3 bytes */
#define HNBU_RXPO2G		0x16	/* 4328 2G RX power offset, 1 byte */
#define HNBU_RXPO5G		0x17	/* 4328 5G RX power offset, 1 byte */
#define HNBU_BOARDNUM		0x18	/* board serial number, independent of mac addr */
#define HNBU_MACADDR		0x19	/* mac addr override for the standard CIS LAN_NID */
#define HNBU_RDLSN		0x1a	/* 2 bytes; serial # advertised in USB descriptor */
#define HNBU_BOARDTYPE		0x1b	/* 2 bytes; boardtype */
#define HNBU_LEDDC		0x1c	/* 2 bytes; LED duty cycle */
#define HNBU_HNBUCIS		0x1d	/* what follows is proprietary HNBU CIS format */
#define HNBU_PAPARMS_SSLPNPHY	0x1e	/* SSLPNPHY PA params */
#define HNBU_RSSISMBXA2G_SSLPNPHY 0x1f	/* SSLPNPHY RSSI mid pt sel & board switch arch */
#define HNBU_RDLRNDIS		0x20	/* 1 byte; 1 = RDL advertises RNDIS config */
#define HNBU_CHAINSWITCH	0x21	/* 2 byte; txchain, rxchain */
#define HNBU_REGREV		0x22	/* 1 byte; */
#define HNBU_FEM		0x23	/* 2 or 4 byte: 11n frontend specification */
#define HNBU_PAPARMS_C0		0x24	/* 8 or 30 bytes: 11n pa paramater for chain 0 */
#define HNBU_PAPARMS_C1		0x25	/* 8 or 30 bytes: 11n pa paramater for chain 1 */
#define HNBU_PAPARMS_C2		0x26	/* 8 or 30 bytes: 11n pa paramater for chain 2 */
#define HNBU_PAPARMS_C3		0x27	/* 8 or 30 bytes: 11n pa paramater for chain 3 */
#define HNBU_PO_CCKOFDM		0x28	/* 6 or 18 bytes: cck2g/ofdm2g/ofdm5g power offset */
#define HNBU_PO_MCS2G		0x29	/* 8 bytes: mcs2g power offset */
#define HNBU_PO_MCS5GM		0x2a	/* 8 bytes: mcs5g mid band power offset */
#define HNBU_PO_MCS5GLH		0x2b	/* 16 bytes: mcs5g low-high band power offset */
#define HNBU_PO_CDD		0x2c	/* 2 bytes: cdd2g/5g power offset */
#define HNBU_PO_STBC		0x2d	/* 2 bytes: stbc2g/5g power offset */
#define HNBU_PO_40M		0x2e	/* 2 bytes: 40Mhz channel 2g/5g power offset */
#define HNBU_PO_40MDUP		0x2f	/* 2 bytes: 40Mhz channel dup 2g/5g power offset */

#define HNBU_RDLRWU		0x30	/* 1 byte; 1 = RDL advertises Remote Wake-up */
#define HNBU_WPS		0x31	/* 1 byte; GPIO pin for WPS button */
#define HNBU_USBFS		0x32	/* 1 byte; 1 = USB advertises FS mode only */
#define HNBU_BRMIN		0x33	/* 4 byte bootloader min resource mask */
#define HNBU_BRMAX		0x34	/* 4 byte bootloader max resource mask */
#define HNBU_PATCH		0x35	/* bootloader patch addr(2b) & data(4b) pair */
#define HNBU_CCKFILTTYPE	0x36	/* CCK digital filter selection options */
#define HNBU_OFDMPO5G		0x37	/* 4 * 3 = 12 byte 11a ofdm power offsets in rev 3 */

#define HNBU_USBEPNUM		0x40	/* USB endpoint numbers */
#define HNBU_SROM3SWRGN		0x80	/* 78 bytes; srom rev 3 s/w region without crc8
					 * plus extra info appended.
					 */
#define HNBU_RESERVED		0x81	/* Reserved for non-BRCM post-mfg additions */
#define HNBU_CUSTOM1		0x82	/* 4 byte; For non-BRCM post-mfg additions */
#define HNBU_CUSTOM2		0x83	/* Reserved; For non-BRCM post-mfg additions */
#endif				/* !defined(ESTA_POSTMOGRIFY_REMOVAL) */

/* sbtmstatelow */
#define SBTML_INT_ACK		0x40000	/* ack the sb interrupt */
#define SBTML_INT_EN		0x20000	/* enable sb interrupt */

/* sbtmstatehigh */
#define SBTMH_INT_STATUS	0x40000	/* sb interrupt status */

#endif				/* _SBPCMCIA_H */
