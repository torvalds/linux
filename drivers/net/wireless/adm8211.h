#ifndef ADM8211_H
#define ADM8211_H

/* ADM8211 Registers */

/* CR32 (SIG) signature */
#define ADM8211_SIG1		0x82011317 /* ADM8211A */
#define ADM8211_SIG2		0x82111317 /* ADM8211B/ADM8211C */

#define ADM8211_CSR_READ(r) ioread32(&priv->map->r)
#define ADM8211_CSR_WRITE(r, val) iowrite32((val), &priv->map->r)

/* CSR (Host Control and Status Registers) */
struct adm8211_csr {
	__le32 PAR;		/* 0x00 CSR0 */
	__le32 FRCTL;		/* 0x04 CSR0A */
	__le32 TDR;		/* 0x08 CSR1 */
	__le32 WTDP;		/* 0x0C CSR1A */
	__le32 RDR;		/* 0x10 CSR2 */
	__le32 WRDP;		/* 0x14 CSR2A */
	__le32 RDB;		/* 0x18 CSR3 */
	__le32 TDBH;		/* 0x1C CSR3A */
	__le32 TDBD;		/* 0x20 CSR4 */
	__le32 TDBP;		/* 0x24 CSR4A */
	__le32 STSR;		/* 0x28 CSR5 */
	__le32 TDBB;		/* 0x2C CSR5A */
	__le32 NAR;		/* 0x30 CSR6 */
	__le32 CSR6A;		/* reserved */
	__le32 IER;		/* 0x38 CSR7 */
	__le32 TKIPSCEP;	/* 0x3C CSR7A */
	__le32 LPC;		/* 0x40 CSR8 */
	__le32 CSR_TEST1;	/* 0x44 CSR8A */
	__le32 SPR;		/* 0x48 CSR9 */
	__le32 CSR_TEST0;	/* 0x4C CSR9A */
	__le32 WCSR;		/* 0x50 CSR10 */
	__le32 WPDR;		/* 0x54 CSR10A */
	__le32 GPTMR;		/* 0x58 CSR11 */
	__le32 GPIO;		/* 0x5C CSR11A */
	__le32 BBPCTL;		/* 0x60 CSR12 */
	__le32 SYNCTL;		/* 0x64 CSR12A */
	__le32 PLCPHD;		/* 0x68 CSR13 */
	__le32 MMIWA;		/* 0x6C CSR13A */
	__le32 MMIRD0;		/* 0x70 CSR14 */
	__le32 MMIRD1;		/* 0x74 CSR14A */
	__le32 TXBR;		/* 0x78 CSR15 */
	__le32 SYNDATA;		/* 0x7C CSR15A */
	__le32 ALCS;		/* 0x80 CSR16 */
	__le32 TOFS2;		/* 0x84 CSR17 */
	__le32 CMDR;		/* 0x88 CSR18 */
	__le32 PCIC;		/* 0x8C CSR19 */
	__le32 PMCSR;		/* 0x90 CSR20 */
	__le32 PAR0;		/* 0x94 CSR21 */
	__le32 PAR1;		/* 0x98 CSR22 */
	__le32 MAR0;		/* 0x9C CSR23 */
	__le32 MAR1;		/* 0xA0 CSR24 */
	__le32 ATIMDA0;		/* 0xA4 CSR25 */
	__le32 ABDA1;		/* 0xA8 CSR26 */
	__le32 BSSID0;		/* 0xAC CSR27 */
	__le32 TXLMT;		/* 0xB0 CSR28 */
	__le32 MIBCNT;		/* 0xB4 CSR29 */
	__le32 BCNT;		/* 0xB8 CSR30 */
	__le32 TSFTH;		/* 0xBC CSR31 */
	__le32 TSC;		/* 0xC0 CSR32 */
	__le32 SYNRF;		/* 0xC4 CSR33 */
	__le32 BPLI;		/* 0xC8 CSR34 */
	__le32 CAP0;		/* 0xCC CSR35 */
	__le32 CAP1;		/* 0xD0 CSR36 */
	__le32 RMD;		/* 0xD4 CSR37 */
	__le32 CFPP;		/* 0xD8 CSR38 */
	__le32 TOFS0;		/* 0xDC CSR39 */
	__le32 TOFS1;		/* 0xE0 CSR40 */
	__le32 IFST;		/* 0xE4 CSR41 */
	__le32 RSPT;		/* 0xE8 CSR42 */
	__le32 TSFTL;		/* 0xEC CSR43 */
	__le32 WEPCTL;		/* 0xF0 CSR44 */
	__le32 WESK;		/* 0xF4 CSR45 */
	__le32 WEPCNT;		/* 0xF8 CSR46 */
	__le32 MACTEST;		/* 0xFC CSR47 */
	__le32 FER;		/* 0x100 */
	__le32 FEMR;		/* 0x104 */
	__le32 FPSR;		/* 0x108 */
	__le32 FFER;		/* 0x10C */
} __attribute__ ((packed));

/* CSR0 - PAR (PCI Address Register) */
#define ADM8211_PAR_MWIE	(1 << 24)
#define ADM8211_PAR_MRLE	(1 << 23)
#define ADM8211_PAR_MRME	(1 << 21)
#define ADM8211_PAR_RAP		((1 << 18) | (1 << 17))
#define ADM8211_PAR_CAL		((1 << 15) | (1 << 14))
#define ADM8211_PAR_PBL		0x00003f00
#define ADM8211_PAR_BLE		(1 << 7)
#define ADM8211_PAR_DSL		0x0000007c
#define ADM8211_PAR_BAR		(1 << 1)
#define ADM8211_PAR_SWR		(1 << 0)

/* CSR1 - FRCTL (Frame Control Register) */
#define ADM8211_FRCTL_PWRMGT	(1 << 31)
#define ADM8211_FRCTL_MAXPSP	(1 << 27)
#define ADM8211_FRCTL_DRVPRSP	(1 << 26)
#define ADM8211_FRCTL_DRVBCON	(1 << 25)
#define ADM8211_FRCTL_AID	0x0000ffff
#define ADM8211_FRCTL_AID_ON	0x0000c000

/* CSR5 - STSR (Status Register) */
#define ADM8211_STSR_PCF	(1 << 31)
#define ADM8211_STSR_BCNTC	(1 << 30)
#define ADM8211_STSR_GPINT	(1 << 29)
#define ADM8211_STSR_LinkOff	(1 << 28)
#define ADM8211_STSR_ATIMTC	(1 << 27)
#define ADM8211_STSR_TSFTF	(1 << 26)
#define ADM8211_STSR_TSCZ	(1 << 25)
#define ADM8211_STSR_LinkOn	(1 << 24)
#define ADM8211_STSR_SQL	(1 << 23)
#define ADM8211_STSR_WEPTD	(1 << 22)
#define ADM8211_STSR_ATIME	(1 << 21)
#define ADM8211_STSR_TBTT	(1 << 20)
#define ADM8211_STSR_NISS	(1 << 16)
#define ADM8211_STSR_AISS	(1 << 15)
#define ADM8211_STSR_TEIS	(1 << 14)
#define ADM8211_STSR_FBE	(1 << 13)
#define ADM8211_STSR_REIS	(1 << 12)
#define ADM8211_STSR_GPTT	(1 << 11)
#define ADM8211_STSR_RPS	(1 << 8)
#define ADM8211_STSR_RDU	(1 << 7)
#define ADM8211_STSR_RCI	(1 << 6)
#define ADM8211_STSR_TUF	(1 << 5)
#define ADM8211_STSR_TRT	(1 << 4)
#define ADM8211_STSR_TLT	(1 << 3)
#define ADM8211_STSR_TDU	(1 << 2)
#define ADM8211_STSR_TPS	(1 << 1)
#define ADM8211_STSR_TCI	(1 << 0)

/* CSR6 - NAR (Network Access Register) */
#define ADM8211_NAR_TXCF	(1 << 31)
#define ADM8211_NAR_HF		(1 << 30)
#define ADM8211_NAR_UTR		(1 << 29)
#define ADM8211_NAR_SQ		(1 << 28)
#define ADM8211_NAR_CFP		(1 << 27)
#define ADM8211_NAR_SF		(1 << 21)
#define ADM8211_NAR_TR		((1 << 15) | (1 << 14))
#define ADM8211_NAR_ST		(1 << 13)
#define ADM8211_NAR_OM		((1 << 11) | (1 << 10))
#define ADM8211_NAR_MM		(1 << 7)
#define ADM8211_NAR_PR		(1 << 6)
#define ADM8211_NAR_EA		(1 << 5)
#define ADM8211_NAR_PB		(1 << 3)
#define ADM8211_NAR_STPDMA	(1 << 2)
#define ADM8211_NAR_SR		(1 << 1)
#define ADM8211_NAR_CTX		(1 << 0)

#define ADM8211_IDLE() 							   \
do { 									   \
	if (priv->nar & (ADM8211_NAR_SR | ADM8211_NAR_ST)) {		   \
		ADM8211_CSR_WRITE(NAR, priv->nar &			   \
				       ~(ADM8211_NAR_SR | ADM8211_NAR_ST));\
		ADM8211_CSR_READ(NAR);					   \
		msleep(20);						   \
	}								   \
} while (0)

#define ADM8211_IDLE_RX() 						\
do {									\
	if (priv->nar & ADM8211_NAR_SR) {				\
		ADM8211_CSR_WRITE(NAR, priv->nar & ~ADM8211_NAR_SR);	\
		ADM8211_CSR_READ(NAR);					\
		mdelay(20);						\
	}								\
} while (0)

#define ADM8211_RESTORE()					\
do {								\
	if (priv->nar & (ADM8211_NAR_SR | ADM8211_NAR_ST))	\
		ADM8211_CSR_WRITE(NAR, priv->nar);		\
} while (0)

/* CSR7 - IER (Interrupt Enable Register) */
#define ADM8211_IER_PCFIE	(1 << 31)
#define ADM8211_IER_BCNTCIE	(1 << 30)
#define ADM8211_IER_GPIE	(1 << 29)
#define ADM8211_IER_LinkOffIE	(1 << 28)
#define ADM8211_IER_ATIMTCIE	(1 << 27)
#define ADM8211_IER_TSFTFIE	(1 << 26)
#define ADM8211_IER_TSCZE	(1 << 25)
#define ADM8211_IER_LinkOnIE	(1 << 24)
#define ADM8211_IER_SQLIE	(1 << 23)
#define ADM8211_IER_WEPIE	(1 << 22)
#define ADM8211_IER_ATIMEIE	(1 << 21)
#define ADM8211_IER_TBTTIE	(1 << 20)
#define ADM8211_IER_NIE		(1 << 16)
#define ADM8211_IER_AIE		(1 << 15)
#define ADM8211_IER_TEIE	(1 << 14)
#define ADM8211_IER_FBEIE	(1 << 13)
#define ADM8211_IER_REIE	(1 << 12)
#define ADM8211_IER_GPTIE	(1 << 11)
#define ADM8211_IER_RSIE	(1 << 8)
#define ADM8211_IER_RUIE	(1 << 7)
#define ADM8211_IER_RCIE	(1 << 6)
#define ADM8211_IER_TUIE	(1 << 5)
#define ADM8211_IER_TRTIE	(1 << 4)
#define ADM8211_IER_TLTTIE	(1 << 3)
#define ADM8211_IER_TDUIE	(1 << 2)
#define ADM8211_IER_TPSIE	(1 << 1)
#define ADM8211_IER_TCIE	(1 << 0)

/* CSR9 - SPR (Serial Port Register) */
#define ADM8211_SPR_SRS		(1 << 11)
#define ADM8211_SPR_SDO		(1 << 3)
#define ADM8211_SPR_SDI		(1 << 2)
#define ADM8211_SPR_SCLK	(1 << 1)
#define ADM8211_SPR_SCS		(1 << 0)

/* CSR9A - CSR_TEST0 */
#define ADM8211_CSR_TEST0_EPNE	(1 << 18)
#define ADM8211_CSR_TEST0_EPSNM	(1 << 17)
#define ADM8211_CSR_TEST0_EPTYP	(1 << 16)
#define ADM8211_CSR_TEST0_EPRLD	(1 << 15)

/* CSR10 - WCSR (Wake-up Control/Status Register) */
#define ADM8211_WCSR_CRCT	(1 << 30)
#define ADM8211_WCSR_TSFTWE	(1 << 20)
#define ADM8211_WCSR_TIMWE	(1 << 19)
#define ADM8211_WCSR_ATIMWE	(1 << 18)
#define ADM8211_WCSR_KEYWE	(1 << 17)
#define ADM8211_WCSR_MPRE	(1 << 9)
#define ADM8211_WCSR_LSOE	(1 << 8)
#define ADM8211_WCSR_KEYUP	(1 << 6)
#define ADM8211_WCSR_TSFTW	(1 << 5)
#define ADM8211_WCSR_TIMW	(1 << 4)
#define ADM8211_WCSR_ATIMW	(1 << 3)
#define ADM8211_WCSR_MPR	(1 << 1)
#define ADM8211_WCSR_LSO	(1 << 0)

/* CSR11A - GPIO */
#define ADM8211_CSR_GPIO_EN5	(1 << 17)
#define ADM8211_CSR_GPIO_EN4	(1 << 16)
#define ADM8211_CSR_GPIO_EN3	(1 << 15)
#define ADM8211_CSR_GPIO_EN2	(1 << 14)
#define ADM8211_CSR_GPIO_EN1	(1 << 13)
#define ADM8211_CSR_GPIO_EN0	(1 << 12)
#define ADM8211_CSR_GPIO_O5	(1 << 11)
#define ADM8211_CSR_GPIO_O4	(1 << 10)
#define ADM8211_CSR_GPIO_O3	(1 << 9)
#define ADM8211_CSR_GPIO_O2	(1 << 8)
#define ADM8211_CSR_GPIO_O1	(1 << 7)
#define ADM8211_CSR_GPIO_O0	(1 << 6)
#define ADM8211_CSR_GPIO_IN	0x0000003f

/* CSR12 - BBPCTL (BBP Control port) */
#define ADM8211_BBPCTL_MMISEL	(1 << 31)
#define ADM8211_BBPCTL_SPICADD  (0x7F << 24)
#define ADM8211_BBPCTL_RF3000	(0x20 << 24)
#define ADM8211_BBPCTL_TXCE	(1 << 23)
#define ADM8211_BBPCTL_RXCE	(1 << 22)
#define ADM8211_BBPCTL_CCAP	(1 << 21)
#define ADM8211_BBPCTL_TYPE	0x001c0000
#define ADM8211_BBPCTL_WR	(1 << 17)
#define ADM8211_BBPCTL_RD	(1 << 16)
#define ADM8211_BBPCTL_ADDR	0x0000ff00
#define ADM8211_BBPCTL_DATA	0x000000ff

/* CSR12A - SYNCTL (Synthesizer Control port) */
#define ADM8211_SYNCTL_WR	(1 << 31)
#define ADM8211_SYNCTL_RD	(1 << 30)
#define ADM8211_SYNCTL_CS0	(1 << 29)
#define ADM8211_SYNCTL_CS1	(1 << 28)
#define ADM8211_SYNCTL_CAL	(1 << 27)
#define ADM8211_SYNCTL_SELCAL	(1 << 26)
#define ADM8211_SYNCTL_RFtype	((1 << 24) || (1 << 23) || (1 << 22))
#define ADM8211_SYNCTL_RFMD	(1 << 22)
#define ADM8211_SYNCTL_GENERAL	(0x7 << 22)
/* SYNCTL 21:0 Data (Si4126: 18-bit data, 4-bit address) */

/* CSR18 - CMDR (Command Register) */
#define ADM8211_CMDR_PM		(1 << 19)
#define ADM8211_CMDR_APM	(1 << 18)
#define ADM8211_CMDR_RTE	(1 << 4)
#define ADM8211_CMDR_DRT	((1 << 3) | (1 << 2))
#define ADM8211_CMDR_DRT_8DW	(0x0 << 2)
#define ADM8211_CMDR_DRT_16DW	(0x1 << 2)
#define ADM8211_CMDR_DRT_SF	(0x2 << 2)

/* CSR33 - SYNRF (SYNRF direct control) */
#define ADM8211_SYNRF_SELSYN	(1 << 31)
#define ADM8211_SYNRF_SELRF	(1 << 30)
#define ADM8211_SYNRF_LERF	(1 << 29)
#define ADM8211_SYNRF_LEIF	(1 << 28)
#define ADM8211_SYNRF_SYNCLK	(1 << 27)
#define ADM8211_SYNRF_SYNDATA	(1 << 26)
#define ADM8211_SYNRF_PE1	(1 << 25)
#define ADM8211_SYNRF_PE2	(1 << 24)
#define ADM8211_SYNRF_PA_PE	(1 << 23)
#define ADM8211_SYNRF_TR_SW	(1 << 22)
#define ADM8211_SYNRF_TR_SWN	(1 << 21)
#define ADM8211_SYNRF_RADIO	(1 << 20)
#define ADM8211_SYNRF_CAL_EN	(1 << 19)
#define ADM8211_SYNRF_PHYRST	(1 << 18)

#define ADM8211_SYNRF_IF_SELECT_0 	(1 << 31)
#define ADM8211_SYNRF_IF_SELECT_1 	((1 << 31) | (1 << 28))
#define ADM8211_SYNRF_WRITE_SYNDATA_0	(1 << 31)
#define ADM8211_SYNRF_WRITE_SYNDATA_1	((1 << 31) | (1 << 26))
#define ADM8211_SYNRF_WRITE_CLOCK_0	(1 << 31)
#define ADM8211_SYNRF_WRITE_CLOCK_1	((1 << 31) | (1 << 27))

/* CSR44 - WEPCTL (WEP Control) */
#define ADM8211_WEPCTL_WEPENABLE   (1 << 31)
#define ADM8211_WEPCTL_WPAENABLE   (1 << 30)
#define ADM8211_WEPCTL_CURRENT_TABLE (1 << 29)
#define ADM8211_WEPCTL_TABLE_WR	(1 << 28)
#define ADM8211_WEPCTL_TABLE_RD	(1 << 27)
#define ADM8211_WEPCTL_WEPRXBYP	(1 << 25)
#define ADM8211_WEPCTL_SEL_WEPTABLE (1 << 23)
#define ADM8211_WEPCTL_ADDR	(0x000001ff)

/* CSR45 - WESK (Data Entry for Share/Individual Key) */
#define ADM8211_WESK_DATA	(0x0000ffff)

/* FER (Function Event Register) */
#define ADM8211_FER_INTR_EV_ENT	(1 << 15)


/* Si4126 RF Synthesizer - Control Registers */
#define SI4126_MAIN_CONF	0
#define SI4126_PHASE_DET_GAIN	1
#define SI4126_POWERDOWN	2
#define SI4126_RF1_N_DIV	3 /* only Si4136 */
#define SI4126_RF2_N_DIV	4
#define SI4126_IF_N_DIV		5
#define SI4126_RF1_R_DIV	6 /* only Si4136 */
#define SI4126_RF2_R_DIV	7
#define SI4126_IF_R_DIV		8

/* Main Configuration */
#define SI4126_MAIN_XINDIV2	(1 << 6)
#define SI4126_MAIN_IFDIV	((1 << 11) | (1 << 10))
/* Powerdown */
#define SI4126_POWERDOWN_PDIB	(1 << 1)
#define SI4126_POWERDOWN_PDRB	(1 << 0)


/* RF3000 BBP - Control Port Registers */
/* 0x00 - reserved */
#define RF3000_MODEM_CTRL__RX_STATUS 0x01
#define RF3000_CCA_CTRL 0x02
#define RF3000_DIVERSITY__RSSI 0x03
#define RF3000_RX_SIGNAL_FIELD 0x04
#define RF3000_RX_LEN_MSB 0x05
#define RF3000_RX_LEN_LSB 0x06
#define RF3000_RX_SERVICE_FIELD 0x07
#define RF3000_TX_VAR_GAIN__TX_LEN_EXT 0x11
#define RF3000_TX_LEN_MSB 0x12
#define RF3000_TX_LEN_LSB 0x13
#define RF3000_LOW_GAIN_CALIB 0x14
#define RF3000_HIGH_GAIN_CALIB 0x15

/* ADM8211 revisions */
#define ADM8211_REV_AB 0x11
#define ADM8211_REV_AF 0x15
#define ADM8211_REV_BA 0x20
#define ADM8211_REV_CA 0x30

struct adm8211_desc {
	__le32 status;
	__le32 length;
	__le32 buffer1;
	__le32 buffer2;
};

#define RDES0_STATUS_OWN	(1 << 31)
#define RDES0_STATUS_ES		(1 << 30)
#define RDES0_STATUS_SQL	(1 << 29)
#define RDES0_STATUS_DE		(1 << 28)
#define RDES0_STATUS_FS		(1 << 27)
#define RDES0_STATUS_LS		(1 << 26)
#define RDES0_STATUS_PCF	(1 << 25)
#define RDES0_STATUS_SFDE	(1 << 24)
#define RDES0_STATUS_SIGE	(1 << 23)
#define RDES0_STATUS_CRC16E	(1 << 22)
#define RDES0_STATUS_RXTOE	(1 << 21)
#define RDES0_STATUS_CRC32E	(1 << 20)
#define RDES0_STATUS_ICVE	(1 << 19)
#define RDES0_STATUS_DA1	(1 << 17)
#define RDES0_STATUS_DA0	(1 << 16)
#define RDES0_STATUS_RXDR	((1 << 15) | (1 << 14) | (1 << 13) | (1 << 12))
#define RDES0_STATUS_FL		(0x00000fff)

#define RDES1_CONTROL_RER	(1 << 25)
#define RDES1_CONTROL_RCH	(1 << 24)
#define RDES1_CONTROL_RBS2	(0x00fff000)
#define RDES1_CONTROL_RBS1	(0x00000fff)

#define RDES1_STATUS_RSSI	(0x0000007f)


#define TDES0_CONTROL_OWN	(1 << 31)
#define TDES0_CONTROL_DONE	(1 << 30)
#define TDES0_CONTROL_TXDR	(0x0ff00000)

#define TDES0_STATUS_OWN	(1 << 31)
#define TDES0_STATUS_DONE	(1 << 30)
#define TDES0_STATUS_ES		(1 << 29)
#define TDES0_STATUS_TLT	(1 << 28)
#define TDES0_STATUS_TRT	(1 << 27)
#define TDES0_STATUS_TUF	(1 << 26)
#define TDES0_STATUS_TRO	(1 << 25)
#define TDES0_STATUS_SOFBR	(1 << 24)
#define TDES0_STATUS_ACR	(0x00000fff)

#define TDES1_CONTROL_IC	(1 << 31)
#define TDES1_CONTROL_LS	(1 << 30)
#define TDES1_CONTROL_FS	(1 << 29)
#define TDES1_CONTROL_TER	(1 << 25)
#define TDES1_CONTROL_TCH	(1 << 24)
#define TDES1_CONTROL_RBS2	(0x00fff000)
#define TDES1_CONTROL_RBS1	(0x00000fff)

/* SRAM offsets */
#define ADM8211_SRAM(x) (priv->pdev->revision < ADM8211_REV_BA ? \
        ADM8211_SRAM_A_ ## x : ADM8211_SRAM_B_ ## x)

#define ADM8211_SRAM_INDIV_KEY   0x0000
#define ADM8211_SRAM_A_SHARE_KEY 0x0160
#define ADM8211_SRAM_B_SHARE_KEY 0x00c0

#define ADM8211_SRAM_A_SSID      0x0180
#define ADM8211_SRAM_B_SSID      0x00d4
#define ADM8211_SRAM_SSID ADM8211_SRAM(SSID)

#define ADM8211_SRAM_A_SUPP_RATE 0x0191
#define ADM8211_SRAM_B_SUPP_RATE 0x00dd
#define ADM8211_SRAM_SUPP_RATE ADM8211_SRAM(SUPP_RATE)

#define ADM8211_SRAM_A_SIZE      0x0200
#define ADM8211_SRAM_B_SIZE      0x01c0
#define ADM8211_SRAM_SIZE ADM8211_SRAM(SIZE)

struct adm8211_rx_ring_info {
	struct sk_buff *skb;
	dma_addr_t mapping;
};

struct adm8211_tx_ring_info {
	struct sk_buff *skb;
	dma_addr_t mapping;
	size_t hdrlen;
};

#define PLCP_SIGNAL_1M		0x0a
#define PLCP_SIGNAL_2M		0x14
#define PLCP_SIGNAL_5M5		0x37
#define PLCP_SIGNAL_11M		0x6e

struct adm8211_tx_hdr {
	u8 da[6];
	u8 signal; /* PLCP signal / TX rate in 100 Kbps */
	u8 service;
	__le16 frame_body_size;
	__le16 frame_control;
	__le16 plcp_frag_tail_len;
	__le16 plcp_frag_head_len;
	__le16 dur_frag_tail;
	__le16 dur_frag_head;
	u8 addr4[6];

#define ADM8211_TXHDRCTL_SHORT_PREAMBLE		(1 <<  0)
#define ADM8211_TXHDRCTL_MORE_FRAG		(1 <<  1)
#define ADM8211_TXHDRCTL_MORE_DATA		(1 <<  2)
#define ADM8211_TXHDRCTL_FRAG_NO		(1 <<  3) /* ? */
#define ADM8211_TXHDRCTL_ENABLE_RTS		(1 <<  4)
#define ADM8211_TXHDRCTL_ENABLE_WEP_ENGINE	(1 <<  5)
#define ADM8211_TXHDRCTL_ENABLE_EXTEND_HEADER	(1 << 15) /* ? */
	__le16 header_control;
	__le16 frag;
	u8 reserved_0;
	u8 retry_limit;

	u32 wep2key0;
	u32 wep2key1;
	u32 wep2key2;
	u32 wep2key3;

	u8 keyid;
	u8 entry_control;	// huh??
	u16 reserved_1;
	u32 reserved_2;
} __attribute__ ((packed));


#define RX_COPY_BREAK 128
#define RX_PKT_SIZE 2500

struct adm8211_eeprom {
	__le16	signature;		/* 0x00 */
	u8	major_version;		/* 0x02 */
	u8	minor_version;		/* 0x03 */
	u8	reserved_1[4];		/* 0x04 */
	u8	hwaddr[6];		/* 0x08 */
	u8	reserved_2[8];		/* 0x1E */
	__le16	cr49;			/* 0x16 */
	u8	cr03;			/* 0x18 */
	u8	cr28;			/* 0x19 */
	u8	cr29;			/* 0x1A */
	u8	country_code;		/* 0x1B */

/* specific bbp types */
#define ADM8211_BBP_RFMD3000	0x00
#define ADM8211_BBP_RFMD3002	0x01
#define ADM8211_BBP_ADM8011	0x04
	u8	specific_bbptype;	/* 0x1C */
	u8	specific_rftype;	/* 0x1D */
	u8	reserved_3[2];		/* 0x1E */
	__le16	device_id;		/* 0x20 */
	__le16	vendor_id;		/* 0x22 */
	__le16	subsystem_id;		/* 0x24 */
	__le16	subsystem_vendor_id;	/* 0x26 */
	u8	maxlat;			/* 0x28 */
	u8	mingnt;			/* 0x29 */
	__le16	cis_pointer_low;	/* 0x2A */
	__le16	cis_pointer_high;	/* 0x2C */
	__le16	csr18;			/* 0x2E */
	u8	reserved_4[16];		/* 0x30 */
	u8	d1_pwrdara;		/* 0x40 */
	u8	d0_pwrdara;		/* 0x41 */
	u8	d3_pwrdara;		/* 0x42 */
	u8	d2_pwrdara;		/* 0x43 */
	u8	antenna_power[14];	/* 0x44 */
	__le16	cis_wordcnt;		/* 0x52 */
	u8	tx_power[14];		/* 0x54 */
	u8	lpf_cutoff[14];		/* 0x62 */
	u8	lnags_threshold[14];	/* 0x70 */
	__le16	checksum;		/* 0x7E */
	u8	cis_data[0];		/* 0x80, 384 bytes */
} __attribute__ ((packed));

struct adm8211_priv {
	struct pci_dev *pdev;
	spinlock_t lock;
	struct adm8211_csr __iomem *map;
	struct adm8211_desc *rx_ring;
	struct adm8211_desc *tx_ring;
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;
	struct adm8211_rx_ring_info *rx_buffers;
	struct adm8211_tx_ring_info *tx_buffers;
	unsigned int rx_ring_size, tx_ring_size;
	unsigned int cur_tx, dirty_tx, cur_rx;

	struct ieee80211_low_level_stats stats;
	struct ieee80211_supported_band band;
	struct ieee80211_channel channels[14];
	int mode;

	int channel;
	u8 bssid[ETH_ALEN];
	u8 ssid[32];
	size_t ssid_len;

	u8 soft_rx_crc;
	u8 retry_limit;

	u8 ant_power;
	u8 tx_power;
	u8 lpf_cutoff;
	u8 lnags_threshold;
	struct adm8211_eeprom *eeprom;
	size_t eeprom_len;

	u32 nar;

#define ADM8211_TYPE_INTERSIL	0x00
#define ADM8211_TYPE_RFMD	0x01
#define ADM8211_TYPE_MARVEL	0x02
#define ADM8211_TYPE_AIROHA	0x03
#define ADM8211_TYPE_ADMTEK     0x05
	unsigned int rf_type:3;
	unsigned int bbp_type:3;

	u8 specific_bbptype;
	enum {
		ADM8211_RFMD2948 = 0x0,
		ADM8211_RFMD2958 = 0x1,
		ADM8211_RFMD2958_RF3000_CONTROL_POWER = 0x2,
		ADM8211_MAX2820 = 0x8,
		ADM8211_AL2210L = 0xC,	/* Airoha */
	} transceiver_type;
};

struct ieee80211_chan_range {
	u8 min;
	u8 max;
};

static const struct ieee80211_chan_range cranges[] = {
	{1,  11},	/* FCC */
	{1,  11},	/* IC */
	{1,  13},	/* ETSI */
	{10, 11},	/* SPAIN */
	{10, 13},	/* FRANCE */
	{14, 14},	/* MMK */
	{1,  14},	/* MMK2 */
};

#endif /* ADM8211_H */
