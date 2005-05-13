/*
 *	Wavelan Pcmcia driver
 *
 *		Jean II - HPLB '96
 *
 * Reorganization and extension of the driver.
 * Original copyright follow. See wavelan_cs.h for details.
 *
 * This file contain the declarations of the Wavelan hardware. Note that
 * the Pcmcia Wavelan include a i82593 controller (see definitions in
 * file i82593.h).
 *
 * The main difference between the pcmcia hardware and the ISA one is
 * the Ethernet Controller (i82593 instead of i82586). The i82593 allow
 * only one send buffer. The PSA (Parameter Storage Area : EEprom for
 * permanent storage of various info) is memory mapped, but not the
 * MMI (Modem Management Interface).
 */

/*
 * Definitions for the AT&T GIS (formerly NCR) WaveLAN PCMCIA card: 
 *   An Ethernet-like radio transceiver controlled by an Intel 82593
 *   coprocessor.
 *
 *
 ****************************************************************************
 *   Copyright 1995
 *   Anthony D. Joseph
 *   Massachusetts Institute of Technology
 *
 *   Permission to use, copy, modify, and distribute this program
 *   for any purpose and without fee is hereby granted, provided
 *   that this copyright and permission notice appear on all copies
 *   and supporting documentation, the name of M.I.T. not be used
 *   in advertising or publicity pertaining to distribution of the
 *   program without specific prior permission, and notice be given
 *   in supporting documentation that copying and distribution is
 *   by permission of M.I.T.  M.I.T. makes no representations about
 *   the suitability of this software for any purpose.  It is pro-
 *   vided "as is" without express or implied warranty.         
 ****************************************************************************
 *
 *
 * Credits:
 *     Special thanks to Jan Hoogendoorn of AT&T GIS Utrecht for
 *       providing extremely useful information about WaveLAN PCMCIA hardware
 *
 *     This driver is based upon several other drivers, in particular:
 *       David Hinds' Linux driver for the PCMCIA 3c589 ethernet adapter
 *       Bruce Janson's Linux driver for the AT-bus WaveLAN adapter
 *	 Anders Klemets' PCMCIA WaveLAN adapter driver
 *       Robert Morris' BSDI driver for the PCMCIA WaveLAN adapter
 */

#ifndef _WAVELAN_CS_H
#define	_WAVELAN_CS_H

/************************** MAGIC NUMBERS ***************************/

/* The detection of the wavelan card is made by reading the MAC address
 * from the card and checking it. If you have a non AT&T product (OEM,
 * like DEC RoamAbout, or Digital Ocean, Epson, ...), you must modify this
 * part to accommodate your hardware...
 */
static const unsigned char	MAC_ADDRESSES[][3] =
{
  { 0x08, 0x00, 0x0E },		/* AT&T Wavelan (standard) & DEC RoamAbout */
  { 0x08, 0x00, 0x6A },		/* AT&T Wavelan (alternate) */
  { 0x00, 0x00, 0xE1 },		/* Hitachi Wavelan */
  { 0x00, 0x60, 0x1D }		/* Lucent Wavelan (another one) */
  /* Add your card here and send me the patch ! */
};

/*
 * Constants used to convert channels to frequencies
 */

/* Frequency available in the 2.0 modem, in units of 250 kHz
 * (as read in the offset register of the dac area).
 * Used to map channel numbers used by `wfreqsel' to frequencies
 */
static const short	channel_bands[] = { 0x30, 0x58, 0x64, 0x7A, 0x80, 0xA8,
				    0xD0, 0xF0, 0xF8, 0x150 };

/* Frequencies of the 1.0 modem (fixed frequencies).
 * Use to map the PSA `subband' to a frequency
 * Note : all frequencies apart from the first one need to be multiplied by 10
 */
static const int	fixed_bands[] = { 915e6, 2.425e8, 2.46e8, 2.484e8, 2.4305e8 };


/*************************** PC INTERFACE ****************************/

/* WaveLAN host interface definitions */

#define	LCCR(base)	(base)		/* LAN Controller Command Register */
#define	LCSR(base)	(base)		/* LAN Controller Status Register */
#define	HACR(base)	(base+0x1)	/* Host Adapter Command Register */
#define	HASR(base)	(base+0x1)	/* Host Adapter Status Register */
#define PIORL(base)	(base+0x2)	/* Program I/O Register Low */
#define RPLL(base)	(base+0x2)	/* Receive Pointer Latched Low */
#define PIORH(base)	(base+0x3)	/* Program I/O Register High */
#define RPLH(base)	(base+0x3)	/* Receive Pointer Latched High */
#define PIOP(base)	(base+0x4)	/* Program I/O Port */
#define MMR(base)	(base+0x6)	/* MMI Address Register */
#define MMD(base)	(base+0x7)	/* MMI Data Register */

/* Host Adaptor Command Register bit definitions */

#define HACR_LOF	  (1 << 3)	/* Lock Out Flag, toggle every 250ms */
#define HACR_PWR_STAT	  (1 << 4)	/* Power State, 1=active, 0=sleep */
#define HACR_TX_DMA_RESET (1 << 5)	/* Reset transmit DMA ptr on high */
#define HACR_RX_DMA_RESET (1 << 6)	/* Reset receive DMA ptr on high */
#define HACR_ROM_WEN	  (1 << 7)	/* EEPROM write enabled when true */

#define HACR_RESET              (HACR_TX_DMA_RESET | HACR_RX_DMA_RESET)
#define	HACR_DEFAULT		(HACR_PWR_STAT)

/* Host Adapter Status Register bit definitions */

#define HASR_MMI_BUSY	(1 << 2)	/* MMI is busy when true */
#define HASR_LOF	(1 << 3)	/* Lock out flag status */
#define HASR_NO_CLK	(1 << 4)	/* active when modem not connected */

/* Miscellaneous bit definitions */

#define PIORH_SEL_TX	(1 << 5)	/* PIOR points to 0=rx/1=tx buffer */
#define MMR_MMI_WR	(1 << 0)	/* Next MMI cycle is 0=read, 1=write */
#define PIORH_MASK	0x1f		/* only low 5 bits are significant */
#define RPLH_MASK	0x1f		/* only low 5 bits are significant */
#define MMI_ADDR_MASK	0x7e		/* Bits 1-6 of MMR are significant */

/* Attribute Memory map */

#define CIS_ADDR	0x0000		/* Card Information Status Register */
#define PSA_ADDR	0x0e00		/* Parameter Storage Area address */
#define EEPROM_ADDR	0x1000		/* EEPROM address (unused ?) */
#define COR_ADDR	0x4000		/* Configuration Option Register */

/* Configuration Option Register bit definitions */

#define COR_CONFIG	(1 << 0)	/* Config Index, 0 when unconfigured */
#define COR_SW_RESET	(1 << 7)	/* Software Reset on true */
#define COR_LEVEL_IRQ	(1 << 6)	/* Level IRQ */

/* Local Memory map */

#define RX_BASE		0x0000		/* Receive memory, 8 kB */
#define TX_BASE		0x2000		/* Transmit memory, 2 kB */
#define UNUSED_BASE	0x2800		/* Unused, 22 kB */
#define RX_SIZE		(TX_BASE-RX_BASE)	/* Size of receive area */
#define RX_SIZE_SHIFT	6		/* Bits to shift in stop register */

#define TRUE  1
#define FALSE 0

#define MOD_ENAL 1
#define MOD_PROM 2

/* Size of a MAC address */
#define WAVELAN_ADDR_SIZE	6

/* Maximum size of Wavelan packet */
#define WAVELAN_MTU	1500

#define	MAXDATAZ		(6 + 6 + 2 + WAVELAN_MTU)

/********************** PARAMETER STORAGE AREA **********************/

/*
 * Parameter Storage Area (PSA).
 */
typedef struct psa_t	psa_t;
struct psa_t
{
  /* For the PCMCIA Adapter, locations 0x00-0x0F are unused and fixed at 00 */
  unsigned char	psa_io_base_addr_1;	/* [0x00] Base address 1 ??? */
  unsigned char	psa_io_base_addr_2;	/* [0x01] Base address 2 */
  unsigned char	psa_io_base_addr_3;	/* [0x02] Base address 3 */
  unsigned char	psa_io_base_addr_4;	/* [0x03] Base address 4 */
  unsigned char	psa_rem_boot_addr_1;	/* [0x04] Remote Boot Address 1 */
  unsigned char	psa_rem_boot_addr_2;	/* [0x05] Remote Boot Address 2 */
  unsigned char	psa_rem_boot_addr_3;	/* [0x06] Remote Boot Address 3 */
  unsigned char	psa_holi_params;	/* [0x07] HOst Lan Interface (HOLI) Parameters */
  unsigned char	psa_int_req_no;		/* [0x08] Interrupt Request Line */
  unsigned char	psa_unused0[7];		/* [0x09-0x0F] unused */

  unsigned char	psa_univ_mac_addr[WAVELAN_ADDR_SIZE];	/* [0x10-0x15] Universal (factory) MAC Address */
  unsigned char	psa_local_mac_addr[WAVELAN_ADDR_SIZE];	/* [0x16-1B] Local MAC Address */
  unsigned char	psa_univ_local_sel;	/* [0x1C] Universal Local Selection */
#define		PSA_UNIVERSAL	0		/* Universal (factory) */
#define		PSA_LOCAL	1		/* Local */
  unsigned char	psa_comp_number;	/* [0x1D] Compatability Number: */
#define		PSA_COMP_PC_AT_915	0 	/* PC-AT 915 MHz	*/
#define		PSA_COMP_PC_MC_915	1 	/* PC-MC 915 MHz	*/
#define		PSA_COMP_PC_AT_2400	2 	/* PC-AT 2.4 GHz	*/
#define		PSA_COMP_PC_MC_2400	3 	/* PC-MC 2.4 GHz	*/
#define		PSA_COMP_PCMCIA_915	4 	/* PCMCIA 915 MHz or 2.0 */
  unsigned char	psa_thr_pre_set;	/* [0x1E] Modem Threshold Preset */
  unsigned char	psa_feature_select;	/* [0x1F] Call code required (1=on) */
#define		PSA_FEATURE_CALL_CODE	0x01 	/* Call code required (Japan) */
  unsigned char	psa_subband;		/* [0x20] Subband	*/
#define		PSA_SUBBAND_915		0	/* 915 MHz or 2.0 */
#define		PSA_SUBBAND_2425	1	/* 2425 MHz	*/
#define		PSA_SUBBAND_2460	2	/* 2460 MHz	*/
#define		PSA_SUBBAND_2484	3	/* 2484 MHz	*/
#define		PSA_SUBBAND_2430_5	4	/* 2430.5 MHz	*/
  unsigned char	psa_quality_thr;	/* [0x21] Modem Quality Threshold */
  unsigned char	psa_mod_delay;		/* [0x22] Modem Delay ??? (reserved) */
  unsigned char	psa_nwid[2];		/* [0x23-0x24] Network ID */
  unsigned char	psa_nwid_select;	/* [0x25] Network ID Select On Off */
  unsigned char	psa_encryption_select;	/* [0x26] Encryption On Off */
  unsigned char	psa_encryption_key[8];	/* [0x27-0x2E] Encryption Key */
  unsigned char	psa_databus_width;	/* [0x2F] AT bus width select 8/16 */
  unsigned char	psa_call_code[8];	/* [0x30-0x37] (Japan) Call Code */
  unsigned char	psa_nwid_prefix[2];	/* [0x38-0x39] Roaming domain */
  unsigned char	psa_reserved[2];	/* [0x3A-0x3B] Reserved - fixed 00 */
  unsigned char	psa_conf_status;	/* [0x3C] Conf Status, bit 0=1:config*/
  unsigned char	psa_crc[2];		/* [0x3D] CRC-16 over PSA */
  unsigned char	psa_crc_status;		/* [0x3F] CRC Valid Flag */
};

/* Size for structure checking (if padding is correct) */
#define	PSA_SIZE	64

/* Calculate offset of a field in the above structure
 * Warning : only even addresses are used */
#define	psaoff(p,f) 	((unsigned short) ((void *)(&((psa_t *) ((void *) NULL + (p)))->f) - (void *) NULL))

/******************** MODEM MANAGEMENT INTERFACE ********************/

/*
 * Modem Management Controller (MMC) write structure.
 */
typedef struct mmw_t	mmw_t;
struct mmw_t
{
  unsigned char	mmw_encr_key[8];	/* encryption key */
  unsigned char	mmw_encr_enable;	/* enable/disable encryption */
#define	MMW_ENCR_ENABLE_MODE	0x02	/* Mode of security option */
#define	MMW_ENCR_ENABLE_EN	0x01	/* Enable security option */
  unsigned char	mmw_unused0[1];		/* unused */
  unsigned char	mmw_des_io_invert;	/* Encryption option */
#define	MMW_DES_IO_INVERT_RES	0x0F	/* Reserved */
#define	MMW_DES_IO_INVERT_CTRL	0xF0	/* Control ??? (set to 0) */
  unsigned char	mmw_unused1[5];		/* unused */
  unsigned char	mmw_loopt_sel;		/* looptest selection */
#define	MMW_LOOPT_SEL_DIS_NWID	0x40	/* disable NWID filtering */
#define	MMW_LOOPT_SEL_INT	0x20	/* activate Attention Request */
#define	MMW_LOOPT_SEL_LS	0x10	/* looptest w/o collision avoidance */
#define MMW_LOOPT_SEL_LT3A	0x08	/* looptest 3a */
#define	MMW_LOOPT_SEL_LT3B	0x04	/* looptest 3b */
#define	MMW_LOOPT_SEL_LT3C	0x02	/* looptest 3c */
#define	MMW_LOOPT_SEL_LT3D	0x01	/* looptest 3d */
  unsigned char	mmw_jabber_enable;	/* jabber timer enable */
  /* Abort transmissions > 200 ms */
  unsigned char	mmw_freeze;		/* freeze / unfreeeze signal level */
  /* 0 : signal level & qual updated for every new message, 1 : frozen */
  unsigned char	mmw_anten_sel;		/* antenna selection */
#define MMW_ANTEN_SEL_SEL	0x01	/* direct antenna selection */
#define	MMW_ANTEN_SEL_ALG_EN	0x02	/* antenna selection algo. enable */
  unsigned char	mmw_ifs;		/* inter frame spacing */
  /* min time between transmission in bit periods (.5 us) - bit 0 ignored */
  unsigned char	mmw_mod_delay;	 	/* modem delay (synchro) */
  unsigned char	mmw_jam_time;		/* jamming time (after collision) */
  unsigned char	mmw_unused2[1];		/* unused */
  unsigned char	mmw_thr_pre_set;	/* level threshold preset */
  /* Discard all packet with signal < this value (4) */
  unsigned char	mmw_decay_prm;		/* decay parameters */
  unsigned char	mmw_decay_updat_prm;	/* decay update parameterz */
  unsigned char	mmw_quality_thr;	/* quality (z-quotient) threshold */
  /* Discard all packet with quality < this value (3) */
  unsigned char	mmw_netw_id_l;		/* NWID low order byte */
  unsigned char	mmw_netw_id_h;		/* NWID high order byte */
  /* Network ID or Domain : create virtual net on the air */

  /* 2.0 Hardware extension - frequency selection support */
  unsigned char	mmw_mode_select;	/* for analog tests (set to 0) */
  unsigned char	mmw_unused3[1];		/* unused */
  unsigned char	mmw_fee_ctrl;		/* frequency eeprom control */
#define	MMW_FEE_CTRL_PRE	0x10	/* Enable protected instructions */
#define	MMW_FEE_CTRL_DWLD	0x08	/* Download eeprom to mmc */
#define	MMW_FEE_CTRL_CMD	0x07	/* EEprom commands : */
#define	MMW_FEE_CTRL_READ	0x06	/* Read */
#define	MMW_FEE_CTRL_WREN	0x04	/* Write enable */
#define	MMW_FEE_CTRL_WRITE	0x05	/* Write data to address */
#define	MMW_FEE_CTRL_WRALL	0x04	/* Write data to all addresses */
#define	MMW_FEE_CTRL_WDS	0x04	/* Write disable */
#define	MMW_FEE_CTRL_PRREAD	0x16	/* Read addr from protect register */
#define	MMW_FEE_CTRL_PREN	0x14	/* Protect register enable */
#define	MMW_FEE_CTRL_PRCLEAR	0x17	/* Unprotect all registers */
#define	MMW_FEE_CTRL_PRWRITE	0x15	/* Write addr in protect register */
#define	MMW_FEE_CTRL_PRDS	0x14	/* Protect register disable */
  /* Never issue this command (PRDS) : it's irreversible !!! */

  unsigned char	mmw_fee_addr;		/* EEprom address */
#define	MMW_FEE_ADDR_CHANNEL	0xF0	/* Select the channel */
#define	MMW_FEE_ADDR_OFFSET	0x0F	/* Offset in channel data */
#define	MMW_FEE_ADDR_EN		0xC0	/* FEE_CTRL enable operations */
#define	MMW_FEE_ADDR_DS		0x00	/* FEE_CTRL disable operations */
#define	MMW_FEE_ADDR_ALL	0x40	/* FEE_CTRL all operations */
#define	MMW_FEE_ADDR_CLEAR	0xFF	/* FEE_CTRL clear operations */

  unsigned char	mmw_fee_data_l;		/* Write data to EEprom */
  unsigned char	mmw_fee_data_h;		/* high octet */
  unsigned char	mmw_ext_ant;		/* Setting for external antenna */
#define	MMW_EXT_ANT_EXTANT	0x01	/* Select external antenna */
#define	MMW_EXT_ANT_POL		0x02	/* Polarity of the antenna */
#define	MMW_EXT_ANT_INTERNAL	0x00	/* Internal antenna */
#define	MMW_EXT_ANT_EXTERNAL	0x03	/* External antenna */
#define	MMW_EXT_ANT_IQ_TEST	0x1C	/* IQ test pattern (set to 0) */
};

/* Size for structure checking (if padding is correct) */
#define	MMW_SIZE	37

/* Calculate offset of a field in the above structure */
#define	mmwoff(p,f) 	(unsigned short)((void *)(&((mmw_t *)((void *)0 + (p)))->f) - (void *)0)


/*
 * Modem Management Controller (MMC) read structure.
 */
typedef struct mmr_t	mmr_t;
struct mmr_t
{
  unsigned char	mmr_unused0[8];		/* unused */
  unsigned char	mmr_des_status;		/* encryption status */
  unsigned char	mmr_des_avail;		/* encryption available (0x55 read) */
#define	MMR_DES_AVAIL_DES	0x55		/* DES available */
#define	MMR_DES_AVAIL_AES	0x33		/* AES (AT&T) available */
  unsigned char	mmr_des_io_invert;	/* des I/O invert register */
  unsigned char	mmr_unused1[5];		/* unused */
  unsigned char	mmr_dce_status;		/* DCE status */
#define	MMR_DCE_STATUS_RX_BUSY		0x01	/* receiver busy */
#define	MMR_DCE_STATUS_LOOPT_IND	0x02	/* loop test indicated */
#define	MMR_DCE_STATUS_TX_BUSY		0x04	/* transmitter on */
#define	MMR_DCE_STATUS_JBR_EXPIRED	0x08	/* jabber timer expired */
#define MMR_DCE_STATUS			0x0F	/* mask to get the bits */
  unsigned char	mmr_dsp_id;		/* DSP id (AA = Daedalus rev A) */
  unsigned char	mmr_unused2[2];		/* unused */
  unsigned char	mmr_correct_nwid_l;	/* # of correct NWID's rxd (low) */
  unsigned char	mmr_correct_nwid_h;	/* # of correct NWID's rxd (high) */
  /* Warning : Read high order octet first !!! */
  unsigned char	mmr_wrong_nwid_l;	/* # of wrong NWID's rxd (low) */
  unsigned char	mmr_wrong_nwid_h;	/* # of wrong NWID's rxd (high) */
  unsigned char	mmr_thr_pre_set;	/* level threshold preset */
#define	MMR_THR_PRE_SET		0x3F		/* level threshold preset */
#define	MMR_THR_PRE_SET_CUR	0x80		/* Current signal above it */
  unsigned char	mmr_signal_lvl;		/* signal level */
#define	MMR_SIGNAL_LVL		0x3F		/* signal level */
#define	MMR_SIGNAL_LVL_VALID	0x80		/* Updated since last read */
  unsigned char	mmr_silence_lvl;	/* silence level (noise) */
#define	MMR_SILENCE_LVL		0x3F		/* silence level */
#define	MMR_SILENCE_LVL_VALID	0x80		/* Updated since last read */
  unsigned char	mmr_sgnl_qual;		/* signal quality */
#define	MMR_SGNL_QUAL		0x0F		/* signal quality */
#define	MMR_SGNL_QUAL_ANT	0x80		/* current antenna used */
  unsigned char	mmr_netw_id_l;		/* NWID low order byte ??? */
  unsigned char	mmr_unused3[3];		/* unused */

  /* 2.0 Hardware extension - frequency selection support */
  unsigned char	mmr_fee_status;		/* Status of frequency eeprom */
#define	MMR_FEE_STATUS_ID	0xF0		/* Modem revision id */
#define	MMR_FEE_STATUS_DWLD	0x08		/* Download in progress */
#define	MMR_FEE_STATUS_BUSY	0x04		/* EEprom busy */
  unsigned char	mmr_unused4[1];		/* unused */
  unsigned char	mmr_fee_data_l;		/* Read data from eeprom (low) */
  unsigned char	mmr_fee_data_h;		/* Read data from eeprom (high) */
};

/* Size for structure checking (if padding is correct) */
#define	MMR_SIZE	36

/* Calculate offset of a field in the above structure */
#define	mmroff(p,f) 	(unsigned short)((void *)(&((mmr_t *)((void *)0 + (p)))->f) - (void *)0)


/* Make the two above structures one */
typedef union mm_t
{
  struct mmw_t	w;	/* Write to the mmc */
  struct mmr_t	r;	/* Read from the mmc */
} mm_t;

#endif /* _WAVELAN_CS_H */
