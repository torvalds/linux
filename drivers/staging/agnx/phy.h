#ifndef AGNX_PHY_H_
#define AGNX_PHY_H_

#include "agnx.h"

/* Transmission Managment Registers */
#define AGNX_TXM_BASE		0x0000
#define AGNX_TXM_CTL		0x0000	/* control register */
#define AGNX_TXM_ETMF		0x0004 /* enable transmission management functions */
#define AGNX_TXM_TXTEMP		0x0008 /* transmission template */
#define AGNX_TXM_RETRYSTAID	0x000c /* Retry Station ID */
#define AGNX_TXM_TIMESTAMPLO		0x0010	/* Timestamp Lo */
#define AGNX_TXM_TIMESTAMPHI		0x0014	/* Timestamp Hi */
#define AGNX_TXM_TXDELAY	0x0018  /* tx delay */
#define AGNX_TXM_TBTTLO		0x0020	/* tbtt Lo */
#define AGNX_TXM_TBTTHI		0x0024	/* tbtt Hi */
#define AGNX_TXM_BEAINTER	0x0028 /* Beacon Interval */
#define AGNX_TXM_NAV		0x0030 /* NAV */
#define AGNX_TXM_CFPMDV		0x0034 /* CFP MDV */
#define AGNX_TXM_CFPERCNT	0x0038 /* CFP period count */
#define AGNX_TXM_PROBDELAY	0x003c /* probe delay */
#define AGNX_TXM_LISINTERCNT	0x0040 /* listen interval count */
#define AGNX_TXM_DTIMPERICNT	0x004c /* DTIM period count */

#define AGNX_TXM_BEACON_CTL	0x005c /* beacon control */

#define AGNX_TXM_SCHEMPCNT	0x007c /* schedule empty count */
#define AGNX_TXM_MAXTIMOUT	0x0084 /* max timeout exceed count */
#define AGNX_TXM_MAXCFPTIM	0x0088 /* max CF poll timeout count */
#define AGNX_TXM_MAXRXTIME	0x008c /* max RX timeout count */
#define AGNX_TXM_MAXACKTIM	0x0090	/* max ACK timeout count */
#define AGNX_TXM_DIF01		0x00a0 /* DIF 0-1 */
#define AGNX_TXM_DIF23		0x00a4 /* DIF 2-3 */
#define AGNX_TXM_DIF45		0x00a8 /* DIF 4-5 */
#define AGNX_TXM_DIF67		0x00ac /* DIF 6-7 */
#define AGNX_TXM_SIFSPIFS	0x00b0 /* SIFS/PIFS */
#define AGNX_TXM_TIFSEIFS	0x00b4 /* TIFS/EIFS */
#define AGNX_TXM_MAXCCACNTSLOT	0x00b8 /* max CCA count slot */
#define AGNX_TXM_SLOTLIMIT	0x00bc /* slot limit/1 msec limit */
#define AGNX_TXM_CFPOLLRXTIM	0x00f0 /* CF poll RX timeout count */
#define AGNX_TXM_CFACKT11B	0x00f4 /* CF ack timeout limit for 11b */
#define AGNX_TXM_CW0		0x0100 /* CW 0 */
#define AGNX_TXM_SLBEALIM0	0x0108 /* short/long beacon limit 0 */
#define AGNX_TXM_CW1		0x0120 /* CW 1 */
#define AGNX_TXM_SLBEALIM1	0x0128 /* short/long beacon limit 1 */
#define AGNX_TXM_CW2		0x0140 /* CW 2 */
#define AGNX_TXM_SLBEALIM2	0x0148 /* short/long beacon limit 2 */
#define AGNX_TXM_CW3		0x0160 /* CW 3 */
#define AGNX_TXM_SLBEALIM3	0x0168 /* short/long beacon limit 3 */
#define AGNX_TXM_CW4		0x0180 /* CW 4 */
#define AGNX_TXM_SLBEALIM4	0x0188 /* short/long beacon limit 4 */
#define AGNX_TXM_CW5		0x01a0 /* CW 5 */
#define AGNX_TXM_SLBEALIM5	0x01a8 /* short/long beacon limit 5 */
#define AGNX_TXM_CW6		0x01c0 /* CW 6 */
#define AGNX_TXM_SLBEALIM6	0x01c8 /* short/long beacon limit 6 */
#define AGNX_TXM_CW7		0x01e0 /* CW 7 */
#define AGNX_TXM_SLBEALIM7	0x01e8 /* short/long beacon limit 7 */
#define AGNX_TXM_BEACONTEMP     0x1000	/* beacon template */
#define AGNX_TXM_STAPOWTEMP	0x1a00 /*  Station Power Template */

/* Receive Management Control Registers */
#define AGNX_RXM_BASE		0x2000
#define AGNX_RXM_REQRATE	0x2000	/* requested rate */
#define AGNX_RXM_MACHI		0x2004	/* first 4 bytes of mac address */
#define AGNX_RXM_MACLO		0x2008	/* last 2 bytes of mac address */
#define AGNX_RXM_BSSIDHI	0x200c	/* bssid hi */
#define AGNX_RXM_BSSIDLO	0x2010	/* bssid lo */
#define AGNX_RXM_HASH_CMD_FLAG	0x2014	/* Flags for the RX Hash Command Default:0 */
#define AGNX_RXM_HASH_CMD_HIGH	0x2018	/* The High half of the Hash Command */
#define AGNX_RXM_HASH_CMD_LOW	0x201c	/* The Low half of the Hash Command */
#define AGNX_RXM_ROUTAB		0x2020	/* routing table */
#define		ROUTAB_SUBTYPE_SHIFT	24
#define		ROUTAB_TYPE_SHIFT	28
#define		ROUTAB_STATUS_SHIFT	30
#define		ROUTAB_RW_SHIFT		31
#define		ROUTAB_ROUTE_DROP	0xf00000 /* Drop */
#define		ROUTAB_ROUTE_CPU	0x400000 /* CPU */
#define		ROUTAB_ROUTE_ENCRY	0x500800 /* Encryption */
#define		ROUTAB_ROUTE_RFP	0x800000 /* RFP */

#define		ROUTAB_TYPE_MANAG	0x0 /* Management */
#define		ROUTAB_TYPE_CTL		0x1 /* Control */
#define		ROUTAB_TYPE_DATA	0x2 /* Data */

#define		ROUTAB_SUBTYPE_DATA		0x0
#define		ROUTAB_SUBTYPE_DATAACK		0x1
#define		ROUTAB_SUBTYPE_DATAPOLL		0x2
#define		ROUTAB_SUBTYPE_DATAPOLLACK	0x3
#define		ROUTAB_SUBTYPE_NULL		0x4 /* NULL */
#define		ROUTAB_SUBTYPE_NULLACK		0x5
#define		ROUTAB_SUBTYPE_NULLPOLL		0x6
#define		ROUTAB_SUBTYPE_NULLPOLLACK	0x7
#define		ROUTAB_SUBTYPE_QOSDATA		0x8 /* QOS DATA */
#define		ROUTAB_SUBTYPE_QOSDATAACK	0x9
#define		ROUTAB_SUBTYPE_QOSDATAPOLL	0xa
#define		ROUTAB_SUBTYPE_QOSDATAACKPOLL	0xb
#define		ROUTAB_SUBTYPE_QOSNULL		0xc
#define		ROUTAB_SUBTYPE_QOSNULLACK	0xd
#define		ROUTAB_SUBTYPE_QOSNULLPOLL	0xe
#define		ROUTAB_SUBTYPE_QOSNULLPOLLACK	0xf
#define AGNX_RXM_DELAY11	   0x2024	/* delay 11(AB) */
#define AGNX_RXM_SOF_CNT	   0x2028	/* SOF Count */
#define AGNX_RXM_FRAG_CNT	   0x202c	/* Fragment Count*/
#define AGNX_RXM_FCS_CNT	   0x2030	/* FCS Count */
#define AGNX_RXM_BSSID_MISS_CNT	   0x2034	/* BSSID Miss Count */
#define AGNX_RXM_PDU_ERR_CNT	   0x2038	/* PDU Error Count */
#define AGNX_RXM_DEST_MISS_CNT	   0x203C	/* Destination Miss Count */
#define AGNX_RXM_DROP_CNT	   0x2040	/* Drop Count */
#define AGNX_RXM_ABORT_CNT	   0x2044	/* Abort Count */
#define AGNX_RXM_RELAY_CNT	   0x2048	/* Relay Count */
#define AGNX_RXM_HASH_MISS_CNT	   0x204c	/* Hash Miss Count */
#define AGNX_RXM_SA_HI		   0x2050	/* Address of received packet Hi */
#define AGNX_RXM_SA_LO		   0x2054	/* Address of received packet Lo */
#define AGNX_RXM_HASH_DUMP_LST	   0x2100	/* Contains Hash Data */
#define AGNX_RXM_HASH_DUMP_MST	   0x2104	/* Contains Hash Data */
#define AGNX_RXM_HASH_DUMP_DATA    0x2108	/* The Station ID to dump */


/* Encryption Managment */
#define AGNX_ENCRY_BASE		0x2400
#define AGNX_ENCRY_WEPKEY0	0x2440 /* wep key #0 */
#define AGNX_ENCRY_WEPKEY1	0x2444 /* wep key #1 */
#define AGNX_ENCRY_WEPKEY2	0x2448 /* wep key #2 */
#define AGNX_ENCRY_WEPKEY3	0x244c /* wep key #3 */
#define AGNX_ENCRY_CCMRECTL	0x2460 /* ccm replay control */


/* Band Management Registers */
#define AGNX_BM_BASE		0x2c00
#define AGNX_BM_BMCTL		0x2c00  /* band management control */
#define AGNX_BM_TXWADDR		0x2c18  /* tx workqueue address start */
#define AGNX_BM_TXTOPEER	0x2c24	/* transmit to peers */
#define AGNX_BM_FPLHP		0x2c2c  /* free pool list head pointer */
#define AGNX_BM_FPLTP		0x2c30  /* free pool list tail pointer */
#define AGNX_BM_FPCNT		0x2c34  /* free pool count */
#define AGNX_BM_CIPDUWCNT	0x2c38  /* card interface pdu workqueue count */
#define AGNX_BM_SPPDUWCNT	0x2c3c  /* sp pdu workqueue count */
#define AGNX_BM_RFPPDUWCNT	0x2c40  /* rfp pdu workqueue count */
#define AGNX_BM_RHPPDUWCNT	0x2c44  /* rhp pdu workqueue count */
#define AGNX_BM_CIWQCTL		0x2c48 /* Card Interface WorkQueue Control */
#define AGNX_BM_CPUTXWCTL	0x2c50  /* cpu tx workqueue control */
#define AGNX_BM_CPURXWCTL	0x2c58  /* cpu rx workqueue control */
#define AGNX_BM_CPULWCTL	0x2c60 /* cpu low workqueue control */
#define AGNX_BM_CPUHWCTL	0x2c68 /* cpu high workqueue control */
#define AGNX_BM_SPTXWCTL	0x2c70 /* sp tx workqueue control */
#define AGNX_BM_SPRXWCTL	0x2c78 /* sp rx workqueue control */
#define AGNX_BM_RFPWCTL		0x2c80 /* RFP workqueue control */
#define AGNX_BM_MTSM		0x2c90 /* Multicast Transmit Station Mask */

/* Card Interface Registers (32bits) */
#define AGNX_CIR_BASE		0x3000
#define AGNX_CIR_BLKCTL		0x3000	/* block control*/
#define		AGNX_STAT_TX	0x1
#define		AGNX_STAT_RX	0x2
#define		AGNX_STAT_X	0x4
/* Below two interrupt flags will be set by our but not CPU or the card */
#define		AGNX_STAT_TXD	0x10
#define		AGNX_STAT_TXM	0x20

#define AGNX_CIR_ADDRWIN	0x3004	/* Addressable Windows*/
#define AGNX_CIR_ENDIAN		0x3008  /* card endianness */
#define AGNX_CIR_SERIALITF	0x3020	/* serial interface */
#define AGNX_CIR_RXCFG		0x3040	/* receive config */
#define		ENABLE_RX_INTERRUPT 0x20
#define		RX_CACHE_LINE	    0x8
/* the RX fragment length */
#define		FRAG_LEN_256	0x0 /* 256B */
#define		FRAG_LEN_512	0x1
#define		FRAG_LEN_1024	0x2
#define		FRAG_LEN_2048	0x3
#define		FRAG_BE		0x10
#define AGNX_CIR_RXCTL		0x3050	/* receive control */
/* memory address, chipside */
#define AGNX_CIR_RXCMSTART	0x3054	/* receive client memory start */
#define AGNX_CIR_RXCMEND	0x3058	/* receive client memory end */
/* memory address, pci */
#define AGNX_CIR_RXHOSTADDR	0x3060	/* receive hostside address */
/* memory address, chipside */
#define AGNX_CIR_RXCLIADDR	0x3064	/* receive clientside address */
#define AGNX_CIR_RXDMACTL	0x3068	/* receive dma control */
#define AGNX_CIR_TXCFG		0x3080	/* transmit config */
#define AGNX_CIR_TXMCTL		0x3090 /* Transmit Management Control */
#define		ENABLE_TX_INTERRUPT 0x20
#define		TX_CACHE_LINE	    0x8
#define AGNX_CIR_TXMSTART	0x3094 /* Transmit Management Start */
#define AGNX_CIR_TXMEND		0x3098 /* Transmit Management End */
#define AGNX_CIR_TXDCTL		0x30a0	/* transmit data control */
/* memeory address, chipset */
#define AGNX_CIR_TXDSTART	0x30a4	/* transmit data start */
#define AGNX_CIR_TXDEND		0x30a8	/* transmit data end */
#define AGNX_CIR_TXMHADDR	0x30b0 /* Transmit Management Hostside Address */
#define AGNX_CIR_TXMCADDR	0x30b4 /* Transmit Management Clientside Address */
#define AGNX_CIR_TXDMACTL	0x30b8	/* transmit dma control */


/* Power Managment Unit */
#define AGNX_PM_BASE		0x3c00
#define AGNX_PM_PMCTL		0x3c00	/* PM Control*/
#define AGNX_PM_MACMSW		0x3c08 /* MAC Manual Slow Work Enable */
#define AGNX_PM_RFCTL		0x3c0c /* RF Control */
#define AGNX_PM_PHYMW		0x3c14	/* Phy Mannal Work */
#define AGNX_PM_SOFTRST		0x3c18	/* PMU Soft Reset */
#define AGNX_PM_PLLCTL		0x3c1c	/* PMU PLL control*/
#define AGNX_PM_TESTPHY		0x3c24 /* PMU Test Phy */


/* Interrupt Control interface */
#define AGNX_INT_BASE		0x4000
#define AGNX_INT_STAT		0x4000	/* interrupt status */
#define AGNX_INT_MASK		0x400c	/* interrupt mask */
/* FIXME */
#define		IRQ_TX_BEACON	0x1	/* TX Beacon */
#define		IRQ_TX_RETRY	0x8	/* TX Retry Interrupt */
#define		IRQ_TX_ACTIVITY	0x10	/* TX Activity */
#define		IRQ_RX_ACTIVITY	0x20	/* RX Activity */
/* FIXME I guess that instead RX a none exist staion's packet or
   the station hasn't been init */
#define		IRQ_RX_X	0x40
#define		IRQ_RX_Y	0x80	/* RX ? */
#define		IRQ_RX_HASHHIT	0x100	/* RX Hash Hit */
#define		IRQ_RX_FRAME	0x200	/* RX Frame */
#define		IRQ_ERR_INT	0x400	/* Error Interrupt */
#define		IRQ_TX_QUE_FULL	0x800	/* TX Workqueue Full */
#define		IRQ_BANDMAN_ERR	0x10000	/* Bandwidth Management Error */
#define		IRQ_TX_DISABLE	0x20000	/* TX Disable */
#define		IRQ_RX_IVASESKEY 0x80000 /* RX Invalid Session Key */
#define		IRQ_RX_KEYIDMIS	0x100000 /* RX key ID Mismatch */
#define		IRQ_REP_THHIT	0x200000 /* Replay Threshold Hit */
#define		IRQ_TIMER1	0x4000000 /* Timer1 */
#define		IRQ_TIMER_CNT	0x10000000 /* Timer Count */
#define		IRQ_PHY_FASTINT 0x20000000 /* Phy Fast Interrupt */
#define		IRQ_PHY_SLOWINT	0x40000000 /* Phy Slow Interrupt */
#define		IRQ_OTHER	0x80000000 /* Unknow interrupt */
#define		AGNX_IRQ_ALL   	0xffffffff

/* System Interface */
#define AGNX_SYSITF_BASE	0x4400
#define AGNX_SYSITF_SYSMODE	0x4400	/* system mode */
#define AGNX_SYSITF_GPIOIN	0x4410 /* GPIO In */
/* PIN lines for leds? */
#define AGNX_SYSITF_GPIOUT	0x4414	/* GPIO Out */

/* Timer Control */
#define AGNX_TIMCTL_TIMER1	0x4800 /* Timer 1 */
#define AGNX_TIMCTL_TIM1CTL	0x4808 /* Timer 1 Control */


/* Antenna Calibration Interface */
#define AGNX_ACI_BASE		0x5000
#define AGNX_ACI_MODE		0x5000 /* Mode */
#define AGNX_ACI_MEASURE	0x5004 /* Measure */
#define AGNX_ACI_SELCHAIN	0x5008 /* Select Chain */
#define AGNX_ACI_LEN		0x500c /* Length */
#define AGNX_ACI_TIMER1		0x5018 /* Timer 1 */
#define AGNX_ACI_TIMER2		0x501c /* Timer 2 */
#define AGNX_ACI_OFFSET		0x5020 /* Offset */
#define AGNX_ACI_STATUS		0x5030 /* Status */
#define		CALI_IDLE	0x0
#define		CALI_DONE	0x1
#define		CALI_BUSY	0x2
#define		CALI_ERR	0x3
#define AGNX_ACI_AICCHA0OVE	0x5034 /* AIC Channel 0 Override */
#define AGNX_ACI_AICCHA1OVE	0x5038 /* AIC Channel 1 Override */

/* Gain Control Registers */
#define AGNX_GCR_BASE		0x9000
/* threshold of primary antenna */
#define AGNX_GCR_THD0A		0x9000	/* threshold? D0 A */
/* low threshold of primary antenna */
#define AGNX_GCR_THD0AL		0x9004	/* threshold? D0 A low */
/* threshold of secondary antenna */
#define AGNX_GCR_THD0B		0x9008	/* threshold? D0_B */
#define AGNX_GCR_DUNSAT		0x900c /* d unsaturated */
#define AGNX_GCR_DSAT		0x9010 /* d saturated */
#define AGNX_GCR_DFIRCAL	0x9014 /* D Fir/Cal */
#define AGNX_GCR_DGCTL11A	0x9018 /* d gain control 11a */
#define AGNX_GCR_DGCTL11B	0x901c /* d gain control 11b */
/* strength of gain */
#define AGNX_GCR_GAININIT	0x9020	/* gain initialization */
#define AGNX_GCR_THNOSIG	0x9024 /* threhold no signal */
#define AGNX_GCR_COARSTEP	0x9028 /* coarse stepping */
#define AGNX_GCR_SIFST11A	0x902c /* sifx time 11a */
#define AGNX_GCR_SIFST11B	0x9030 /* sifx time 11b */
#define AGNX_GCR_CWDETEC	0x9034 /* cw detection */
#define AGNX_GCR_0X38		0x9038 /* ???? */
#define AGNX_GCR_BOACT		0x903c	/* BO Active */
#define AGNX_GCR_BOINACT	0x9040	/* BO Inactive */
#define AGNX_GCR_BODYNA		0x9044	/* BO dynamic */
/* 802.11 mode(a,b,g) */
#define AGNX_GCR_DISCOVMOD	0x9048	/* discovery mode */
#define AGNX_GCR_NLISTANT	0x904c	/* number of listening antenna */
#define AGNX_GCR_NACTIANT	0x9050	/* number of active antenna */
#define AGNX_GCR_NMEASANT	0x9054	/* number of measuring antenna */
#define AGNX_GCR_NCAPTANT	0x9058	/* number of capture antenna */
#define AGNX_GCR_THCAP11A	0x905c /* threshold capture 11a */
#define AGNX_GCR_THCAP11B	0x9060 /* threshold capture 11b */
#define AGNX_GCR_THCAPRX11A	0x9064 /* threshold capture rx 11a */
#define AGNX_GCR_THCAPRX11B	0x9068 /* threshold capture rx 11b */
#define AGNX_GCR_THLEVDRO	0x906c /* threshold level drop */
#define AGNX_GCR_GAINSET0	0x9070 /* Gainset 0 */
#define AGNX_GCR_GAINSET1	0x9074 /* Gainset 1 */
#define AGNX_GCR_GAINSET2	0x9078 /* Gainset 2 */
#define AGNX_GCR_MAXRXTIME11A	0x907c /* maximum rx time 11a */
#define AGNX_GCR_MAXRXTIME11B	0x9080 /* maximum rx time 11b */
#define AGNX_GCR_CORRTIME	0x9084 /* correction time */
/* reset the subsystem, 0 = disable, 1 = enable */
#define AGNX_GCR_RSTGCTL	0x9088	/* reset gain control */
/* channel receiving */
#define AGNX_GCR_RXCHANEL	0x908c	/* receive channel */
#define AGNX_GCR_NOISE0		0x9090 /* Noise 0 */
#define AGNX_GCR_NOISE1		0x9094 /* Noise 1 */
#define AGNX_GCR_NOISE2		0x9098 /* Noise 2 */
#define AGNX_GCR_SIGHTH		0x909c	/* Signal High Threshold */
#define AGNX_GCR_SIGLTH		0x90a0	/* Signal Low Threshold */
#define AGNX_GCR_CORRDROP	0x90a4 /* correction drop */
/* threshold of tertiay antenna */
#define AGNX_GCR_THCD		0x90a8	/* threshold? CD */
#define AGNX_GCR_THCS		0x90ac	/* threshold? CS */
#define AGNX_GCR_MAXPOWDIFF	0x90b8 /* maximum power difference */
#define AGNX_GCR_TRACNT4	0x90ec /* Transition Count 4 */
#define AGNX_GCR_TRACNT5      	0x90f0	/* transition count 5 */
#define AGNX_GCR_TRACNT6       	0x90f4	/* transition count 6 */
#define AGNX_GCR_TRACNT7       	0x90f8	/* transition coutn 7 */
#define AGNX_GCR_TESTBUS	0x911c /* test bus */
#define AGNX_GCR_CHAINNUM	0x9120 /* Number of Chains */
#define AGNX_GCR_ANTCFG		0x9124	/* Antenna Config */
#define AGNX_GCR_THJUMP		0x912c /* threhold jump */
#define AGNX_GCR_THPOWER	0x9130 /* threshold power */
#define AGNX_GCR_THPOWCLIP	0x9134 /* threshold power clip*/
#define AGNX_GCR_FORCECTLCLK	0x9138 /* Force Gain Control Clock */
#define AGNX_GCR_GAINSETWRITE	0x913c /* Gainset Write */
#define AGNX_GCR_THD0BTFEST	0x9140	/* threshold d0 b tf estimate */
#define AGNX_GCR_THRX11BPOWMIN	0x9144	/* threshold rx 11b power minimum */
#define AGNX_GCR_0X14c		0x914c /* ?? */
#define AGNX_GCR_0X150		0x9150 /* ?? */
#define AGNX_GCR_RXOVERIDE	0x9194	/* recieve override */
#define AGNX_GCR_WATCHDOG	0x91b0	/* watchdog timeout */


/* Spi Interface */
#define AGNX_SPI_BASE		0xdc00
#define AGNX_SPI_CFG		0xdc00 /* spi configuration */
/* Only accept 16 bits */
#define AGNX_SPI_WMSW		0xdc04	/* write most significant word */
/* Only accept 16 bits */
#define AGNX_SPI_WLSW		0xdc08	/* write least significant word */
#define AGNX_SPI_CTL		0xdc0c	/* spi control */
#define AGNX_SPI_RMSW		0xdc10 /* read most significant word */
#define AGNX_SPI_RLSW		0xdc14 /* read least significant word */
/* SPI Control Mask */
#define		SPI_READ_CTL		0x4000 /* read control */
#define		SPI_BUSY_CTL		0x8000 /* busy control */
/* RF and synth chips in spi */
#define		RF_CHIP0	0x400
#define		RF_CHIP1	0x800
#define		RF_CHIP2	0x1000
#define		SYNTH_CHIP	0x2000

/* Unknown register */
#define AGNX_UNKNOWN_BASE	0x7800

/* FIXME MonitorGain */
#define AGNX_MONGCR_BASE	0x12000

/* Gain Table */
#define AGNX_GAIN_TABLE		0x12400

/* The initial FIR coefficient table */
#define AGNX_FIR_BASE		0x19804

#define AGNX_ENGINE_LOOKUP_TBL	0x800

/* eeprom commands */
#define EEPROM_CMD_NULL		0x0 /* NULL */
#define EEPROM_CMD_WRITE	0x2 /* write */
#define EEPROM_CMD_READ		0x3 /* read */
#define EEPROM_CMD_STATUSREAD	0x5 /* status register read */
#define EEPROM_CMD_WRITEENABLE	0x6 /* write enable */
#define EEPROM_CMD_CONFIGURE	0x7 /* configure */

#define EEPROM_DATAFORCOFIGURE	0x6 /* ??? */

/* eeprom address */
#define EEPROM_ADDR_SUBVID	0x0 /* Sub Vendor ID */
#define EEPROM_ADDR_SUBSID	0x2 /* Sub System ID */
#define EEPROM_ADDR_MACADDR	0x146 /* MAC Address */
#define EEPROM_ADDR_LOTYPE	0x14f /* LO type */

struct agnx_eeprom {
	u8 data;	/* date */
	u16 address;	/* address in EEPROM */
	u8 cmd;		/* command, unknown, status */
}  __attribute__((__packed__));

#define AGNX_EEPROM_COMMAND_SHIFT	5
#define AGNX_EEPROM_COMMAND_STAT	0x01

void disable_receiver(struct agnx_priv *priv);
void enable_receiver(struct agnx_priv *priv);
u8 read_from_eeprom(struct agnx_priv *priv, u16 address);
void agnx_hw_init(struct agnx_priv *priv);
int agnx_hw_reset(struct agnx_priv *priv);
int agnx_set_ssid(struct agnx_priv *priv, u8 *ssid, size_t ssid_len);
void agnx_set_bssid(struct agnx_priv *priv, const u8 *bssid);
void enable_power_saving(struct agnx_priv *priv);
void disable_power_saving(struct agnx_priv *priv);
void calibrate_antenna_period(unsigned long data);

#endif /* AGNX_PHY_H_ */
