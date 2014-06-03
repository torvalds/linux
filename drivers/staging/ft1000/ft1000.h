/*
 * Common structures and definitions for FT1000 Flarion Flash OFDM PCMCIA and
 * USB devices.
 *
 * Originally copyright (c) 2002 Flarion Technologies
 *
 */

#define DSPVERSZ	4
#define HWSERNUMSZ	16
#define SKUSZ		20
#define EUISZ		8
#define MODESZ		2
#define CALVERSZ	2
#define CALDATESZ	6

#define ELECTRABUZZ_ID	0	/* ASIC ID for Electrabuzz */
#define MAGNEMITE_ID	0x1a01	/* ASIC ID for Magnemite */

/* MEMORY MAP common to both ELECTRABUZZ and MAGNEMITE */
#define	FT1000_REG_DPRAM_ADDR	0x000E	/* DPADR - Dual Port Ram Indirect
					 * Address Register
					 */
#define	FT1000_REG_SUP_CTRL	0x0020	/* HCTR - Host Control Register */
#define	FT1000_REG_SUP_STAT	0x0022	/* HSTAT - Host Status Register */
#define	FT1000_REG_RESET	0x0024	/* HCTR - Host Control Register */
#define	FT1000_REG_SUP_ISR	0x0026	/* HISR - Host Interrupt Status
					 * Register
					 */
#define	FT1000_REG_SUP_IMASK	0x0028	/* HIMASK - Host Interrupt Mask */
#define	FT1000_REG_DOORBELL	0x002a	/* DBELL - Door Bell Register */
#define FT1000_REG_ASIC_ID	0x002e	/* ASICID - ASIC Identification
					 * Number
					 */

/* MEMORY MAP FOR ELECTRABUZZ ASIC */
#define FT1000_REG_UFIFO_STAT	0x0000	/* UFSR - Uplink FIFO status register */
#define FT1000_REG_UFIFO_BEG	0x0002	/* UFBR	- Uplink FIFO beginning
					 * register
					 */
#define	FT1000_REG_UFIFO_MID	0x0004	/* UFMR	- Uplink FIFO middle register */
#define	FT1000_REG_UFIFO_END	0x0006	/* UFER	- Uplink FIFO end register */
#define	FT1000_REG_DFIFO_STAT	0x0008	/* DFSR - Downlink FIFO status
					 * register
					 */
#define	FT1000_REG_DFIFO	0x000A	/* DFR - Downlink FIFO Register */
#define	FT1000_REG_DPRAM_DATA	0x000C	/* DPRAM - Dual Port Indirect
					 * Data Register
					 */
#define	FT1000_REG_WATERMARK	0x0010	/* WMARK - Watermark Register */

/* MEMORY MAP FOR MAGNEMITE */
#define FT1000_REG_MAG_UFDR	0x0000	/* UFDR - Uplink FIFO Data
					 * Register (32-bits)
					 */
#define FT1000_REG_MAG_UFDRL	0x0000	/* UFDRL - Uplink FIFO Data
					 * Register low-word (16-bits)
					 */
#define FT1000_REG_MAG_UFDRH	0x0002	/* UFDRH - Uplink FIFO Data Register
					 * high-word (16-bits)
					 */
#define FT1000_REG_MAG_UFER	0x0004	/* UFER - Uplink FIFO End Register */
#define FT1000_REG_MAG_UFSR	0x0006	/* UFSR - Uplink FIFO Status Register */
#define FT1000_REG_MAG_DFR	0x0008	/* DFR - Downlink FIFO Register
					 * (32-bits)
					 */
#define FT1000_REG_MAG_DFRL	0x0008	/* DFRL - Downlink FIFO Register
					 * low-word (16-bits)
					 */
#define FT1000_REG_MAG_DFRH	0x000a	/* DFRH - Downlink FIFO Register
					 * high-word (16-bits)
					 */
#define FT1000_REG_MAG_DFSR	0x000c	/* DFSR - Downlink FIFO Status
					 * Register
					 */
#define FT1000_REG_MAG_DPDATA	0x0010	/* DPDATA - Dual Port RAM Indirect
					 * Data Register (32-bits)
					 */
#define FT1000_REG_MAG_DPDATAL	0x0010	/* DPDATAL - Dual Port RAM Indirect
					 * Data Register low-word (16-bits)
					 */
#define FT1000_REG_MAG_DPDATAH	0x0012	/* DPDATAH - Dual Port RAM Indirect Data
					 * Register high-word (16-bits)
					 */
#define	FT1000_REG_MAG_WATERMARK 0x002c	/* WMARK - Watermark Register */
#define FT1000_REG_MAG_VERSION	0x0030	/* LLC Version */

/* Reserved Dual Port RAM offsets for Electrabuzz */
#define FT1000_DPRAM_TX_BASE	0x0002	/* Host to PC Card Messaging Area */
#define FT1000_DPRAM_RX_BASE	0x0800	/* PC Card to Host Messaging Area */
#define FT1000_FIFO_LEN		0x07FC	/* total length for DSP FIFO tracking */
#define FT1000_HI_HO		0x07FE	/* heartbeat with HI/HO */
#define FT1000_DSP_STATUS	0x0FFE	/* dsp status - non-zero is a request
					 * to reset dsp
					 */
#define FT1000_DSP_LED		0x0FFA	/* dsp led status for PAD device */
#define FT1000_DSP_CON_STATE	0x0FF8	/* DSP Connection Status Info */
#define FT1000_DPRAM_FEFE	0x0002	/* location for dsp ready indicator */
#define FT1000_DSP_TIMER0	0x1FF0	/* Timer Field from Basestation */
#define FT1000_DSP_TIMER1	0x1FF2	/* Timer Field from Basestation */
#define FT1000_DSP_TIMER2	0x1FF4	/* Timer Field from Basestation */
#define FT1000_DSP_TIMER3	0x1FF6	/* Timer Field from Basestation */

/* Reserved Dual Port RAM offsets for Magnemite */
#define FT1000_DPRAM_MAG_TX_BASE	0x0000	/* Host to PC Card
						 * Messaging Area
						 */
#define FT1000_DPRAM_MAG_RX_BASE	0x0200	/* PC Card to Host
						 * Messaging Area
						 */

#define FT1000_MAG_FIFO_LEN		0x1FF	/* total length for DSP
						 * FIFO tracking
						 */
#define FT1000_MAG_FIFO_LEN_INDX	0x1	/* low-word index */
#define FT1000_MAG_HI_HO		0x1FF	/* heartbeat with HI/HO */
#define FT1000_MAG_HI_HO_INDX		0x0	/* high-word index */
#define FT1000_MAG_DSP_LED		0x3FE	/* dsp led status for
						 * PAD device
						 */
#define FT1000_MAG_DSP_LED_INDX		0x0	/* dsp led status for
						 * PAD device
						 */
#define FT1000_MAG_DSP_CON_STATE	0x3FE	/* DSP Connection Status Info */
#define FT1000_MAG_DSP_CON_STATE_INDX	0x1	/* DSP Connection Status Info */
#define FT1000_MAG_DPRAM_FEFE		0x000	/* location for dsp ready
						 * indicator
						 */
#define FT1000_MAG_DPRAM_FEFE_INDX	0x0	/* location for dsp ready
						 * indicator
						 */
#define FT1000_MAG_DSP_TIMER0		0x3FC	/* Timer Field from
						 * Basestation
						 */
#define FT1000_MAG_DSP_TIMER0_INDX	0x1
#define FT1000_MAG_DSP_TIMER1		0x3FC	/* Timer Field from
						 * Basestation
						 */
#define FT1000_MAG_DSP_TIMER1_INDX	0x0
#define FT1000_MAG_DSP_TIMER2		0x3FD	/* Timer Field from
						 * Basestation
						 */
#define FT1000_MAG_DSP_TIMER2_INDX	0x1
#define FT1000_MAG_DSP_TIMER3		0x3FD	/* Timer Field from
						 * Basestation
						 */
#define FT1000_MAG_DSP_TIMER3_INDX	0x0
#define FT1000_MAG_TOTAL_LEN		0x200
#define FT1000_MAG_TOTAL_LEN_INDX	0x1
#define FT1000_MAG_PH_LEN		0x200
#define FT1000_MAG_PH_LEN_INDX		0x0
#define FT1000_MAG_PORT_ID		0x201
#define FT1000_MAG_PORT_ID_INDX		0x0

#define HOST_INTF_LE	0x0	/* Host interface little endian mode */
#define HOST_INTF_BE	0x1	/* Host interface big endian mode */

/* FT1000 to Host Doorbell assignments */
#define FT1000_DB_DPRAM_RX	0x0001	/* this value indicates that DSP
					 * has data for host in DPRAM
					 */
#define FT1000_DB_DNLD_RX	0x0002	/* Downloader handshake doorbell */
#define FT1000_ASIC_RESET_REQ	0x0004	/* DSP requesting host to
					 * reset the ASIC
					 */
#define FT1000_DSP_ASIC_RESET	0x0008	/* DSP indicating host that
					 * it will reset the ASIC
					 */
#define FT1000_DB_COND_RESET	0x0010	/* DSP request for a card reset. */

/* Host to FT1000 Doorbell assignments */
#define FT1000_DB_DPRAM_TX	0x0100	/* this value indicates that host
					 * has data for DSP in DPRAM.
					 */
#define FT1000_DB_DNLD_TX	0x0200	/* Downloader handshake doorbell */
#define FT1000_ASIC_RESET_DSP	0x0400	/* Responds to FT1000_ASIC_RESET_REQ */
#define FT1000_DB_HB		0x1000	/* Indicates that supervisor has a
					 * heartbeat message for DSP.
					 */

#define hi			0x6869	/* PC Card heartbeat values */
#define ho			0x686f	/* PC Card heartbeat values */

/* Magnemite specific defines */
#define hi_mag			0x6968	/* Byte swap hi to avoid
					 * additional system call
					 */
#define ho_mag			0x6f68	/* Byte swap ho to avoid
					 * additional system call
					 */

/* Bit field definitions for Host Interrupt Status Register */
/* Indicate the cause of an interrupt. */
#define ISR_EMPTY		0x00	/* no bits set */
#define ISR_DOORBELL_ACK	0x01	/* Doorbell acknowledge from DSP */
#define ISR_DOORBELL_PEND	0x02	/* Doorbell pending from DSP */
#define ISR_RCV			0x04	/* Packet available in Downlink FIFO */
#define ISR_WATERMARK		0x08	/* Watermark requirements satisfied */

/* Bit field definition for Host Interrupt Mask */
#define ISR_MASK_NONE		0x0000	/* no bits set */
#define ISR_MASK_DOORBELL_ACK	0x0001	/* Doorbell acknowledge mask */
#define ISR_MASK_DOORBELL_PEND	0x0002	/* Doorbell pending mask */
#define ISR_MASK_RCV		0x0004	/* Downlink Packet available mask */
#define ISR_MASK_WATERMARK	0x0008	/* Watermark interrupt mask */
#define ISR_MASK_ALL		0xffff	/* Mask all interrupts */
/* Default interrupt mask
 * (Enable Doorbell pending and Packet available interrupts)
 */
#define ISR_DEFAULT_MASK	0x7ff9

/* Bit field definition for Host Control Register */
#define DSP_RESET_BIT		0x0001	/* Bit field to control
					 * dsp reset state
					 */
					/* (0 = out of reset 1 = reset) */
#define ASIC_RESET_BIT		0x0002	/* Bit field to control
					 * ASIC reset state
					 */
					/* (0 = out of reset 1 = reset) */
#define DSP_UNENCRYPTED		0x0004
#define DSP_ENCRYPTED		0x0008
#define EFUSE_MEM_DISABLE	0x0040

/* Application specific IDs */
#define DSPID		0x20
#define HOSTID		0x10
#define DSPAIRID	0x90
#define DRIVERID	0x00
#define NETWORKID	0x20

/* Size of DPRAM Message */
#define MAX_CMD_SQSIZE	1780

#define ENET_MAX_SIZE		1514
#define ENET_HEADER_SIZE	14

#define SLOWQ_TYPE	0
#define FASTQ_TYPE	1

#define MAX_DSP_SESS_REC	1024

#define DSP_QID_OFFSET	4

/* Driver message types */
#define MEDIA_STATE		0x0010
#define TIME_UPDATE		0x0020
#define DSP_PROVISION		0x0030
#define DSP_INIT_MSG		0x0050
#define DSP_HIBERNATE		0x0060
#define DSP_STORE_INFO		0x0070
#define DSP_GET_INFO		0x0071
#define GET_DRV_ERR_RPT_MSG	0x0073
#define RSP_DRV_ERR_RPT_MSG	0x0074

/* Driver Error Messages for DSP */
#define DSP_HB_INFO		0x7ef0
#define DSP_FIFO_INFO		0x7ef1
#define DSP_CONDRESET_INFO	0x7ef2
#define DSP_CMDLEN_INFO		0x7ef3
#define DSP_CMDPHCKSUM_INFO	0x7ef4
#define DSP_PKTPHCKSUM_INFO	0x7ef5
#define DSP_PKTLEN_INFO		0x7ef6
#define DSP_USER_RESET		0x7ef7
#define FIFO_FLUSH_MAXLIMIT	0x7ef8
#define FIFO_FLUSH_BADCNT	0x7ef9
#define FIFO_ZERO_LEN		0x7efa

/* Pseudo Header structure */
struct pseudo_hdr {
	unsigned short	length;		/* length of msg body */
	unsigned char	source;		/* hardware source id */
					/*    Host = 0x10 */
					/*    Dsp  = 0x20 */
	unsigned char	destination;	/* hardware destination id
					 * (refer to source)
					 */
	unsigned char	portdest;	/* software destination port id */
					/*    Host = 0x00 */
					/*    Applicaton Broadcast = 0x10 */
					/*    Network Stack = 0x20 */
					/*    Dsp OAM = 0x80 */
					/*    Dsp Airlink = 0x90 */
					/*    Dsp Loader = 0xa0 */
					/*    Dsp MIP = 0xb0 */
	unsigned char	portsrc;	/* software source port id
					 * (refer to portdest)
					 */
	unsigned short	sh_str_id;	/* not used */
	unsigned char	control;	/* not used */
	unsigned char	rsvd1;
	unsigned char	seq_num;	/* message sequence number */
	unsigned char	rsvd2;
	unsigned short	qos_class;	/* not used */
	unsigned short	checksum;	/* pseudo header checksum */
} __packed;

struct drv_msg {
	struct pseudo_hdr pseudo;
	u16 type;
	u16 length;
	u8  data[0];
} __packed;

struct media_msg {
	struct pseudo_hdr pseudo;
	u16 type;
	u16 length;
	u16 state;
	u32 ip_addr;
	u32 net_mask;
	u32 gateway;
	u32 dns_1;
	u32 dns_2;
} __packed;

struct dsp_init_msg {
	struct pseudo_hdr pseudo;
	u16 type;
	u16 length;
	u8 DspVer[DSPVERSZ];		/* DSP version number */
	u8 HwSerNum[HWSERNUMSZ];	/* Hardware Serial Number */
	u8 Sku[SKUSZ];			/* SKU */
	u8 eui64[EUISZ];		/* EUI64 */
	u8 ProductMode[MODESZ];		/* Product Mode (Market/Production) */
	u8 RfCalVer[CALVERSZ];		/* Rf Calibration version */
	u8 RfCalDate[CALDATESZ];	/* Rf Calibration date */
} __packed;

struct prov_record {
	struct list_head list;
	u8 *pprov_data;
};

struct ft1000_info {
	void *priv;
	struct net_device_stats stats;
	u16 DrvErrNum;
	u16 AsicID;
	int CardReady;
	int registered;
	int mediastate;
	u8 squeseqnum;			/* sequence number on slow queue */
	spinlock_t dpram_lock;
	u16 fifo_cnt;
	u8 DspVer[DSPVERSZ];		/* DSP version number */
	u8 HwSerNum[HWSERNUMSZ];	/* Hardware Serial Number */
	u8 Sku[SKUSZ];			/* SKU */
	u8 eui64[EUISZ];		/* EUI64 */
	time_t ConTm;			/* Connection Time */
	u8 ProductMode[MODESZ];
	u8 RfCalVer[CALVERSZ];
	u8 RfCalDate[CALDATESZ];
	u16 DSP_TIME[4];
	u16 LedStat;
	u16 ConStat;
	u16 ProgConStat;
	struct list_head prov_list;
	u16 DSPInfoBlklen;
	int (*ft1000_reset)(void *);
	u16 DSPInfoBlk[MAX_DSP_SESS_REC];
	union {
		u16 Rec[MAX_DSP_SESS_REC];
		u32 MagRec[MAX_DSP_SESS_REC/2];
	} DSPSess;
	struct proc_dir_entry *ft1000_proc_dir;
	char netdevname[IFNAMSIZ];
};
