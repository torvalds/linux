/*
 * BCM43XX Sonics SiliconBackplane PCMCIA core hardware definitions.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: sbpcmcia.h 446298 2014-01-03 11:30:17Z $
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

#if !defined(LINUX_POSTMOGRIFY_REMOVAL)
/* CIS stuff */

/* The CIS stops where the FCRs start */
#define	CIS_SIZE		PCMCIA_FCR
#define CIS_SIZE_12K    1154    /* Maximum h/w + s/w sub region size for 12k OTP */

/* CIS tuple length field max */
#define CIS_TUPLE_LEN_MAX	0xff

/* Standard tuples we know about */

#define CISTPL_NULL			0x00
#define	CISTPL_VERS_1		0x15		/* CIS ver, manf, dev & ver strings */
#define	CISTPL_MANFID		0x20		/* Manufacturer and device id */
#define CISTPL_FUNCID		0x21		/* Function identification */
#define	CISTPL_FUNCE		0x22		/* Function extensions */
#define	CISTPL_CFTABLE		0x1b		/* Config table entry */
#define	CISTPL_END		0xff		/* End of the CIS tuple chain */

/* Function identifier provides context for the function extentions tuple */
#define CISTPL_FID_SDIO		0x0c		/* Extensions defined by SDIO spec */

/* Function extensions for LANs (assumed for extensions other than SDIO) */
#define	LAN_TECH		1		/* Technology type */
#define	LAN_SPEED		2		/* Raw bit rate */
#define	LAN_MEDIA		3		/* Transmission media */
#define	LAN_NID			4		/* Node identification (aka MAC addr) */
#define	LAN_CONN		5		/* Connector standard */


/* CFTable */
#define CFTABLE_REGWIN_2K	0x08		/* 2k reg windows size */
#define CFTABLE_REGWIN_4K	0x10		/* 4k reg windows size */
#define CFTABLE_REGWIN_8K	0x20		/* 8k reg windows size */

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
#define HNBU_RSSISMBXA2G_SSLPNPHY 0x1f /* SSLPNPHY RSSI mid pt sel & board switch arch */
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
#define HNBU_ELNA2G             0x38
#define HNBU_ELNA5G             0x39
#define HNBU_TEMPTHRESH 0x3A /* 2 bytes
					 * byte1 tempthresh
					 * byte2 period(msb 4 bits) | hysterisis(lsb 4 bits)
					 */
#define HNBU_UUID 0x3B /* 16 Bytes Hex */

#define HNBU_USBEPNUM		0x40	/* USB endpoint numbers */

/* POWER PER RATE for SROM V9 */
#define HNBU_CCKBW202GPO       0x41    /* 2 bytes each
					 * CCK Power offsets for 20 MHz rates (11, 5.5, 2, 1Mbps)
					 * cckbw202gpo cckbw20ul2gpo
					 */

#define HNBU_LEGOFDMBW202GPO    0x42    /* 4 bytes each
					 * OFDM power offsets for 20 MHz Legacy rates
					 * (54, 48, 36, 24, 18, 12, 9, 6 Mbps)
					 * legofdmbw202gpo  legofdmbw20ul2gpo
					 */

#define HNBU_LEGOFDMBW205GPO   0x43    /* 4 bytes each
					* 5G band: OFDM power offsets for 20 MHz Legacy rates
					* (54, 48, 36, 24, 18, 12, 9, 6 Mbps)
					* low subband : legofdmbw205glpo  legofdmbw20ul2glpo
					* mid subband :legofdmbw205gmpo  legofdmbw20ul2gmpo
					* high subband :legofdmbw205ghpo  legofdmbw20ul2ghpo
					*/

#define HNBU_MCS2GPO    0x44    /* 4 bytes each
				     * mcs 0-7  power-offset. LSB nibble: m0, MSB nibble: m7
				     * mcsbw202gpo  mcsbw20ul2gpo mcsbw402gpo
				     */
#define HNBU_MCS5GLPO    0x45    /* 4 bytes each
				     * 5G low subband mcs 0-7 power-offset.
				     * LSB nibble: m0, MSB nibble: m7
				     * mcsbw205glpo  mcsbw20ul5glpo mcsbw405glpo
				     */
#define HNBU_MCS5GMPO    0x46    /* 4 bytes each
				     * 5G mid subband mcs 0-7 power-offset.
				     * LSB nibble: m0, MSB nibble: m7
				     * mcsbw205gmpo  mcsbw20ul5gmpo mcsbw405gmpo
				     */
#define HNBU_MCS5GHPO    0x47    /* 4 bytes each
				     * 5G high subband mcs 0-7 power-offset.
				     * LSB nibble: m0, MSB nibble: m7
				     * mcsbw205ghpo  mcsbw20ul5ghpo mcsbw405ghpo
				     */
#define HNBU_MCS32PO	0x48	/*  2 bytes total
				 * mcs-32 power offset for each band/subband.
				 * LSB nibble: 2G band, MSB nibble:
				 * mcs322ghpo, mcs325gmpo, mcs325glpo, mcs322gpo
				 */
#define HNBU_LEG40DUPPO	0x49 /*  2 bytes total
				* Additional power offset for Legacy Dup40 transmissions.
				 * Applied in addition to legofdmbw20ulXpo, X=2g, 5gl, 5gm, or 5gh.
				 * LSB nibble: 2G band, MSB nibble: 5G band high subband.
				 * leg40dup5ghpo, leg40dup5gmpo, leg40dup5glpo, leg40dup2gpo
				 */

#define HNBU_PMUREGS	0x4a /* Variable length (5 bytes for each register)
				* The setting of the ChipCtrl, PLL, RegulatorCtrl, Up/Down Timer and
				* ResourceDependency Table registers.
				*/

#define HNBU_PATCH2		0x4b	/* bootloader TCAM patch addr(4b) & data(4b) pair .
				* This is required for socram rev 15 onwards.
				*/

#define HNBU_USBRDY		0x4c	/* Variable length (upto 5 bytes)
				* This is to indicate the USB/HSIC host controller
				* that the device is ready for enumeration.
				*/

#define HNBU_USBREGS	0x4d	/* Variable length
				* The setting of the devcontrol, HSICPhyCtrl1 and HSICPhyCtrl2
				* registers during the USB initialization.
				*/

#define HNBU_BLDR_TIMEOUT	0x4e	/* 2 bytes used for HSIC bootloader to reset chip
				* on connect timeout.
				* The Delay after USBConnect for timeout till dongle receives
				* get_descriptor request.
				*/
#define HNBU_USBFLAGS		0x4f
#define HNBU_PATCH_AUTOINC	0x50
#define HNBU_MDIO_REGLIST	0x51
#define HNBU_MDIOEX_REGLIST	0x52
/* Unified OTP: tupple to embed USB manfid inside SDIO CIS */
#define HNBU_UMANFID		0x53
#define HNBU_PUBKEY		0x54	/* 128 byte; publick key to validate downloaded FW */
#define HNBU_WOWLGPIO       0x55   /* 1 byte bit 7 initial polarity, bit 6..0 gpio pin */
#define HNBU_MUXENAB		0x56	/* 1 byte to enable mux options */
#define HNBU_GCI_CCR		0x57	/* GCI Chip control register */

#define HNBU_FEM_CFG		0x58	/* FEM config */
#define HNBU_ACPA_C0		0x59	/* ACPHY PA parameters: chain 0 */
#define HNBU_ACPA_C1		0x5a	/* ACPHY PA parameters: chain 1 */
#define HNBU_ACPA_C2		0x5b	/* ACPHY PA parameters: chain 2 */
#define HNBU_MEAS_PWR		0x5c
#define HNBU_PDOFF		0x5d
#define HNBU_ACPPR_2GPO		0x5e	/* ACPHY Power-per-rate 2gpo */
#define HNBU_ACPPR_5GPO		0x5f	/* ACPHY Power-per-rate 5gpo */
#define HNBU_ACPPR_SBPO		0x60	/* ACPHY Power-per-rate sbpo */
#define HNBU_NOISELVL		0x61
#define HNBU_RXGAIN_ERR		0x62
#define HNBU_AGBGA		0x63
#define HNBU_USBDESC_COMPOSITE	0x64    /* USB WLAN/BT composite descriptor */
#define HNBU_PATCH_AUTOINC8	0x65	/* Auto increment patch entry for 8 byte patching */
#define HNBU_PATCH8		0x66	/* Patch entry for 8 byte patching */
#define HNBU_ACRXGAINS_C0	0x67	/* ACPHY rxgains: chain 0 */
#define HNBU_ACRXGAINS_C1	0x68	/* ACPHY rxgains: chain 1 */
#define HNBU_ACRXGAINS_C2	0x69	/* ACPHY rxgains: chain 2 */
#define HNBU_TXDUTY		0x6a	/* Tx duty cycle for ACPHY 5g 40/80 Mhz */
#define HNBU_USBUTMI_CTL        0x6b    /* 2 byte USB UTMI/LDO Control */
#define HNBU_PDOFF_2G		0x6c
#define HNBU_USBSSPHY_UTMI_CTL0 0x6d    /* 4 byte USB SSPHY UTMI Control */
#define HNBU_USBSSPHY_UTMI_CTL1 0x6e    /* 4 byte USB SSPHY UTMI Control */
#define HNBU_USBSSPHY_UTMI_CTL2 0x6f    /* 4 byte USB SSPHY UTMI Control */
#define HNBU_USBSSPHY_SLEEP0    0x70    /* 2 byte USB SSPHY sleep */
#define HNBU_USBSSPHY_SLEEP1    0x71    /* 2 byte USB SSPHY sleep */
#define HNBU_USBSSPHY_SLEEP2    0x72    /* 2 byte USB SSPHY sleep */
#define HNBU_USBSSPHY_SLEEP3    0x73    /* 2 byte USB SSPHY sleep */
#define HNBU_USBSSPHY_MDIO		0x74	/* USB SSPHY INIT regs setting */
#define HNBU_USB30PHY_NOSS		0x75	/* USB30 NO Super Speed */
#define HNBU_USB30PHY_U1U2		0x76	/* USB30 PHY U1U2 Enable */
#define HNBU_USB30PHY_REGS		0x77	/* USB30 PHY REGs update */

#define HNBU_SROM3SWRGN		0x80	/* 78 bytes; srom rev 3 s/w region without crc8
					 * plus extra info appended.
					 */
#define HNBU_RESERVED		0x81	/* Reserved for non-BRCM post-mfg additions */
#define HNBU_CUSTOM1		0x82	/* 4 byte; For non-BRCM post-mfg additions */
#define HNBU_CUSTOM2		0x83	/* Reserved; For non-BRCM post-mfg additions */
#define HNBU_ACPAPARAM		0x84	/* ACPHY PAPARAM */
#define HNBU_ACPA_CCK		0x86	/* ACPHY PA trimming parameters: CCK */
#define HNBU_ACPA_40		0x87	/* ACPHY PA trimming parameters: 40 */
#define HNBU_ACPA_80		0x88	/* ACPHY PA trimming parameters: 80 */
#define HNBU_ACPA_4080		0x89	/* ACPHY PA trimming parameters: 40/80 */
#define HNBU_SUBBAND5GVER	0x8a	/* subband5gver */
#define HNBU_PAPARAMBWVER	0x8b	/* paparambwver */

#define HNBU_MCS5Gx1PO		0x8c
#define HNBU_ACPPR_SB8080_PO		0x8d


#endif /* !defined(LINUX_POSTMOGRIFY_REMOVAL) */

/* sbtmstatelow */
#define SBTML_INT_ACK		0x40000		/* ack the sb interrupt */
#define SBTML_INT_EN		0x20000		/* enable sb interrupt */

/* sbtmstatehigh */
#define SBTMH_INT_STATUS	0x40000		/* sb interrupt status */

#endif	/* _SBPCMCIA_H */
