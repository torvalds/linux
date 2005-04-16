/*
 *	WaveLAN ISA driver
 *
 *		Jean II - HPLB '96
 *
 * Reorganisation and extension of the driver.
 * Original copyright follows. See wavelan.p.h for details.
 *
 * This file contains the declarations for the WaveLAN hardware. Note that
 * the WaveLAN ISA includes a i82586 controller (see definitions in
 * file i82586.h).
 *
 * The main difference between the ISA hardware and the PCMCIA one is
 * the Ethernet controller (i82586 instead of i82593).
 * The i82586 allows multiple transmit buffers.  The PSA needs to be accessed
 * through the host interface.
 */

#ifndef _WAVELAN_H
#define	_WAVELAN_H

/************************** MAGIC NUMBERS ***************************/

/* Detection of the WaveLAN card is done by reading the MAC
 * address from the card and checking it.  If you have a non-AT&T
 * product (OEM, like DEC RoamAbout, Digital Ocean, or Epson),
 * you might need to modify this part to accommodate your hardware.
 */
static const char	MAC_ADDRESSES[][3] =
{
  { 0x08, 0x00, 0x0E },		/* AT&T WaveLAN (standard) & DEC RoamAbout */
  { 0x08, 0x00, 0x6A },		/* AT&T WaveLAN (alternate) */
  { 0x00, 0x00, 0xE1 },		/* Hitachi Wavelan */
  { 0x00, 0x60, 0x1D }		/* Lucent Wavelan (another one) */
  /* Add your card here and send me the patch! */
};

#define WAVELAN_ADDR_SIZE	6	/* Size of a MAC address */

#define WAVELAN_MTU		1500	/* Maximum size of WaveLAN packet */

#define	MAXDATAZ		(WAVELAN_ADDR_SIZE + WAVELAN_ADDR_SIZE + 2 + WAVELAN_MTU)

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

/*
 * Host Adaptor structure.
 * (base is board port address).
 */
typedef union hacs_u	hacs_u;
union hacs_u
{
	unsigned short	hu_command;		/* Command register */
#define		HACR_RESET		0x0001	/* Reset board */
#define		HACR_CA			0x0002	/* Set Channel Attention for 82586 */
#define		HACR_16BITS		0x0004	/* 16-bit operation (0 => 8bits) */
#define		HACR_OUT0		0x0008	/* General purpose output pin 0 */
						/* not used - must be 1 */
#define		HACR_OUT1		0x0010	/* General purpose output pin 1 */
						/* not used - must be 1 */
#define		HACR_82586_INT_ENABLE	0x0020	/* Enable 82586 interrupts */
#define		HACR_MMC_INT_ENABLE	0x0040	/* Enable MMC interrupts */
#define		HACR_INTR_CLR_ENABLE	0x0080	/* Enable interrupt status read/clear */
	unsigned short	hu_status;		/* Status Register */
#define		HASR_82586_INTR		0x0001	/* Interrupt request from 82586 */
#define		HASR_MMC_INTR		0x0002	/* Interrupt request from MMC */
#define		HASR_MMC_BUSY		0x0004	/* MMC busy indication */
#define		HASR_PSA_BUSY		0x0008	/* LAN parameter storage area busy */
};

typedef struct ha_t	ha_t;
struct ha_t
{
	hacs_u		ha_cs;		/* Command and status registers */
#define 		ha_command	ha_cs.hu_command
#define 		ha_status	ha_cs.hu_status
	unsigned short	ha_mmcr;	/* Modem Management Ctrl Register */
	unsigned short	ha_pior0;	/* Program I/O Address Register Port 0 */
	unsigned short	ha_piop0;	/* Program I/O Port 0 */
	unsigned short	ha_pior1;	/* Program I/O Address Register Port 1 */
	unsigned short	ha_piop1;	/* Program I/O Port 1 */
	unsigned short	ha_pior2;	/* Program I/O Address Register Port 2 */
	unsigned short	ha_piop2;	/* Program I/O Port 2 */
};

#define HA_SIZE		16

#define	hoff(p,f) 	(unsigned short)((void *)(&((ha_t *)((void *)0 + (p)))->f) - (void *)0)
#define	HACR(p)		hoff(p, ha_command)
#define	HASR(p)		hoff(p, ha_status)
#define	MMCR(p)		hoff(p, ha_mmcr)
#define	PIOR0(p)	hoff(p, ha_pior0)
#define	PIOP0(p)	hoff(p, ha_piop0)
#define	PIOR1(p)	hoff(p, ha_pior1)
#define	PIOP1(p)	hoff(p, ha_piop1)
#define	PIOR2(p)	hoff(p, ha_pior2)
#define	PIOP2(p)	hoff(p, ha_piop2)

/*
 * Program I/O Mode Register values.
 */
#define STATIC_PIO		0	/* Mode 1: static mode */
					/* RAM access ??? */
#define AUTOINCR_PIO		1	/* Mode 2: auto increment mode */
					/* RAM access ??? */
#define AUTODECR_PIO		2	/* Mode 3: auto decrement mode */
					/* RAM access ??? */
#define PARAM_ACCESS_PIO	3	/* Mode 4: LAN parameter access mode */
					/* Parameter access. */
#define PIO_MASK		3	/* register mask */
#define PIOM(cmd,piono)		((u_short)cmd << 10 << (piono * 2))

#define	HACR_DEFAULT		(HACR_OUT0 | HACR_OUT1 | HACR_16BITS | PIOM(STATIC_PIO, 0) | PIOM(AUTOINCR_PIO, 1) | PIOM(PARAM_ACCESS_PIO, 2))
#define	HACR_INTRON		(HACR_82586_INT_ENABLE | HACR_MMC_INT_ENABLE | HACR_INTR_CLR_ENABLE)

/************************** MEMORY LAYOUT **************************/

/*
 * Onboard 64 k RAM layout.
 * (Offsets from 0x0000.)
 */
#define OFFSET_RU		0x0000		/* 75% memory */
#define OFFSET_CU		0xC000		/* 25% memory */
#define OFFSET_SCB		(OFFSET_ISCP - sizeof(scb_t))
#define OFFSET_ISCP		(OFFSET_SCP - sizeof(iscp_t))
#define OFFSET_SCP		I82586_SCP_ADDR

#define	RXBLOCKZ		(sizeof(fd_t) + sizeof(rbd_t) + MAXDATAZ)
#define	TXBLOCKZ		(sizeof(ac_tx_t) + sizeof(ac_nop_t) + sizeof(tbd_t) + MAXDATAZ)

#define	NRXBLOCKS		((OFFSET_CU - OFFSET_RU) / RXBLOCKZ)
#define	NTXBLOCKS		((OFFSET_SCB - OFFSET_CU) / TXBLOCKZ)

/********************** PARAMETER STORAGE AREA **********************/

/*
 * Parameter Storage Area (PSA).
 */
typedef struct psa_t	psa_t;
struct psa_t
{
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
  unsigned char	psa_comp_number;	/* [0x1D] Compatibility Number:	 */
#define		PSA_COMP_PC_AT_915	0 	/* PC-AT 915 MHz 	 */
#define		PSA_COMP_PC_MC_915	1 	/* PC-MC 915 MHz	 */
#define		PSA_COMP_PC_AT_2400	2 	/* PC-AT 2.4 GHz	 */
#define		PSA_COMP_PC_MC_2400	3 	/* PC-MC 2.4 GHz 	 */
#define		PSA_COMP_PCMCIA_915	4 	/* PCMCIA 915 MHz or 2.0 */
  unsigned char	psa_thr_pre_set;	/* [0x1E] Modem Threshold Preset */
  unsigned char	psa_feature_select;	/* [0x1F] Call code required (1=on) */
#define		PSA_FEATURE_CALL_CODE	0x01 	/* Call code required (Japan) */
  unsigned char	psa_subband;		/* [0x20] Subband	  */
#define		PSA_SUBBAND_915		0	/* 915 MHz or 2.0 */
#define		PSA_SUBBAND_2425	1	/* 2425 MHz	  */
#define		PSA_SUBBAND_2460	2	/* 2460 MHz	  */
#define		PSA_SUBBAND_2484	3	/* 2484 MHz	  */
#define		PSA_SUBBAND_2430_5	4	/* 2430.5 MHz	  */
  unsigned char	psa_quality_thr;	/* [0x21] Modem Quality Threshold */
  unsigned char	psa_mod_delay;		/* [0x22] Modem Delay (?) (reserved) */
  unsigned char	psa_nwid[2];		/* [0x23-0x24] Network ID */
  unsigned char	psa_nwid_select;	/* [0x25] Network ID Select On/Off */
  unsigned char	psa_encryption_select;	/* [0x26] Encryption On/Off */
  unsigned char	psa_encryption_key[8];	/* [0x27-0x2E] Encryption Key */
  unsigned char	psa_databus_width;	/* [0x2F] AT bus width select 8/16 */
  unsigned char	psa_call_code[8];	/* [0x30-0x37] (Japan) Call Code */
  unsigned char	psa_nwid_prefix[2];	/* [0x38-0x39] Roaming domain */
  unsigned char	psa_reserved[2];	/* [0x3A-0x3B] Reserved - fixed 00 */
  unsigned char	psa_conf_status;	/* [0x3C] Conf Status, bit 0=1:config*/
  unsigned char	psa_crc[2];		/* [0x3D] CRC-16 over PSA */
  unsigned char	psa_crc_status;		/* [0x3F] CRC Valid Flag */
};

#define	PSA_SIZE	64

/* Calculate offset of a field in the above structure.
 * Warning:  only even addresses are used. */
#define	psaoff(p,f) 	((unsigned short) ((void *)(&((psa_t *) ((void *) NULL + (p)))->f) - (void *) NULL))

/******************** MODEM MANAGEMENT INTERFACE ********************/

/*
 * Modem Management Controller (MMC) write structure.
 */
typedef struct mmw_t	mmw_t;
struct mmw_t
{
  unsigned char	mmw_encr_key[8];	/* encryption key */
  unsigned char	mmw_encr_enable;	/* Enable or disable encryption. */
#define	MMW_ENCR_ENABLE_MODE	0x02	/* mode of security option */
#define	MMW_ENCR_ENABLE_EN	0x01	/* Enable security option. */
  unsigned char	mmw_unused0[1];		/* unused */
  unsigned char	mmw_des_io_invert;	/* encryption option */
#define	MMW_DES_IO_INVERT_RES	0x0F	/* reserved */
#define	MMW_DES_IO_INVERT_CTRL	0xF0	/* control (?) (set to 0) */
  unsigned char	mmw_unused1[5];		/* unused */
  unsigned char	mmw_loopt_sel;		/* looptest selection */
#define	MMW_LOOPT_SEL_DIS_NWID	0x40	/* Disable NWID filtering. */
#define	MMW_LOOPT_SEL_INT	0x20	/* Activate Attention Request. */
#define	MMW_LOOPT_SEL_LS	0x10	/* looptest, no collision avoidance */
#define MMW_LOOPT_SEL_LT3A	0x08	/* looptest 3a */
#define	MMW_LOOPT_SEL_LT3B	0x04	/* looptest 3b */
#define	MMW_LOOPT_SEL_LT3C	0x02	/* looptest 3c */
#define	MMW_LOOPT_SEL_LT3D	0x01	/* looptest 3d */
  unsigned char	mmw_jabber_enable;	/* jabber timer enable */
  /* Abort transmissions > 200 ms */
  unsigned char	mmw_freeze;		/* freeze or unfreeze signal level */
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
  unsigned char	mmw_decay_updat_prm;	/* decay update parameters */
  unsigned char	mmw_quality_thr;	/* quality (z-quotient) threshold */
  /* Discard all packet with quality < this value (3) */
  unsigned char	mmw_netw_id_l;		/* NWID low order byte */
  unsigned char	mmw_netw_id_h;		/* NWID high order byte */
  /* Network ID or Domain : create virtual net on the air */

  /* 2.0 Hardware extension - frequency selection support */
  unsigned char	mmw_mode_select;	/* for analog tests (set to 0) */
  unsigned char	mmw_unused3[1];		/* unused */
  unsigned char	mmw_fee_ctrl;		/* frequency EEPROM control */
#define	MMW_FEE_CTRL_PRE	0x10	/* Enable protected instructions. */
#define	MMW_FEE_CTRL_DWLD	0x08	/* Download EEPROM to mmc. */
#define	MMW_FEE_CTRL_CMD	0x07	/* EEPROM commands:  */
#define	MMW_FEE_CTRL_READ	0x06	/* Read */
#define	MMW_FEE_CTRL_WREN	0x04	/* Write enable */
#define	MMW_FEE_CTRL_WRITE	0x05	/* Write data to address. */
#define	MMW_FEE_CTRL_WRALL	0x04	/* Write data to all addresses. */
#define	MMW_FEE_CTRL_WDS	0x04	/* Write disable */
#define	MMW_FEE_CTRL_PRREAD	0x16	/* Read addr from protect register */
#define	MMW_FEE_CTRL_PREN	0x14	/* Protect register enable */
#define	MMW_FEE_CTRL_PRCLEAR	0x17	/* Unprotect all registers. */
#define	MMW_FEE_CTRL_PRWRITE	0x15	/* Write address in protect register */
#define	MMW_FEE_CTRL_PRDS	0x14	/* Protect register disable */
  /* Never issue the PRDS command:  it's irreversible! */

  unsigned char	mmw_fee_addr;		/* EEPROM address */
#define	MMW_FEE_ADDR_CHANNEL	0xF0	/* Select the channel. */
#define	MMW_FEE_ADDR_OFFSET	0x0F	/* Offset in channel data */
#define	MMW_FEE_ADDR_EN		0xC0	/* FEE_CTRL enable operations */
#define	MMW_FEE_ADDR_DS		0x00	/* FEE_CTRL disable operations */
#define	MMW_FEE_ADDR_ALL	0x40	/* FEE_CTRL all operations */
#define	MMW_FEE_ADDR_CLEAR	0xFF	/* FEE_CTRL clear operations */

  unsigned char	mmw_fee_data_l;		/* Write data to EEPROM. */
  unsigned char	mmw_fee_data_h;		/* high octet */
  unsigned char	mmw_ext_ant;		/* Setting for external antenna */
#define	MMW_EXT_ANT_EXTANT	0x01	/* Select external antenna */
#define	MMW_EXT_ANT_POL		0x02	/* Polarity of the antenna */
#define	MMW_EXT_ANT_INTERNAL	0x00	/* Internal antenna */
#define	MMW_EXT_ANT_EXTERNAL	0x03	/* External antenna */
#define	MMW_EXT_ANT_IQ_TEST	0x1C	/* IQ test pattern (set to 0) */
};

#define	MMW_SIZE	37

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
  unsigned char	mmr_dsp_id;		/* DSP ID (AA = Daedalus rev A) */
  unsigned char	mmr_unused2[2];		/* unused */
  unsigned char	mmr_correct_nwid_l;	/* # of correct NWIDs rxd (low) */
  unsigned char	mmr_correct_nwid_h;	/* # of correct NWIDs rxd (high) */
  /* Warning:  read high-order octet first! */
  unsigned char	mmr_wrong_nwid_l;	/* # of wrong NWIDs rxd (low) */
  unsigned char	mmr_wrong_nwid_h;	/* # of wrong NWIDs rxd (high) */
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
  unsigned char	mmr_netw_id_l;		/* NWID low order byte (?) */
  unsigned char	mmr_unused3[3];		/* unused */

  /* 2.0 Hardware extension - frequency selection support */
  unsigned char	mmr_fee_status;		/* Status of frequency EEPROM */
#define	MMR_FEE_STATUS_ID	0xF0		/* Modem revision ID */
#define	MMR_FEE_STATUS_DWLD	0x08		/* Download in progress */
#define	MMR_FEE_STATUS_BUSY	0x04		/* EEPROM busy */
  unsigned char	mmr_unused4[1];		/* unused */
  unsigned char	mmr_fee_data_l;		/* Read data from EEPROM (low) */
  unsigned char	mmr_fee_data_h;		/* Read data from EEPROM (high) */
};

#define	MMR_SIZE	36

#define	mmroff(p,f) 	(unsigned short)((void *)(&((mmr_t *)((void *)0 + (p)))->f) - (void *)0)

/* Make the two above structures one */
typedef union mm_t
{
  struct mmw_t	w;	/* Write to the mmc */
  struct mmr_t	r;	/* Read from the mmc */
} mm_t;

#endif /* _WAVELAN_H */

/*
 * This software may only be used and distributed
 * according to the terms of the GNU General Public License.
 *
 * For more details, see wavelan.c.
 */
