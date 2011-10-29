#ifndef _LMC_VAR_H_
#define _LMC_VAR_H_

 /*
  * Copyright (c) 1997-2000 LAN Media Corporation (LMC)
  * All rights reserved.  www.lanmedia.com
  *
  * This code is written by:
  * Andrew Stanley-Jones (asj@cban.com)
  * Rob Braun (bbraun@vix.com),
  * Michael Graff (explorer@vix.com) and
  * Matt Thomas (matt@3am-software.com).
  *
  * This software may be used and distributed according to the terms
  * of the GNU General Public License version 2, incorporated herein by reference.
  */

#include <linux/timer.h>

/*
 * basic definitions used in lmc include files
 */

typedef struct lmc___softc lmc_softc_t;
typedef struct lmc___media lmc_media_t;
typedef struct lmc___ctl lmc_ctl_t;

#define lmc_csrptr_t    unsigned long

#define LMC_REG_RANGE 0x80

#define LMC_PRINTF_FMT  "%s"
#define LMC_PRINTF_ARGS	(sc->lmc_device->name)

#define TX_TIMEOUT (2*HZ)

#define LMC_TXDESCS            32
#define LMC_RXDESCS            32

#define LMC_LINK_UP            1
#define LMC_LINK_DOWN          0

/* These macros for generic read and write to and from the dec chip */
#define LMC_CSR_READ(sc, csr) \
	inl((sc)->lmc_csrs.csr)
#define LMC_CSR_WRITE(sc, reg, val) \
	outl((val), (sc)->lmc_csrs.reg)

//#ifdef _LINUX_DELAY_H
//	#define SLOW_DOWN_IO udelay(2);
//	#undef __SLOW_DOWN_IO
//	#define __SLOW_DOWN_IO udelay(2);
//#endif

#define DELAY(n) SLOW_DOWN_IO

#define lmc_delay() inl(sc->lmc_csrs.csr_9)

/* This macro sync's up with the mii so that reads and writes can take place */
#define LMC_MII_SYNC(sc) do {int n=32; while( n >= 0 ) { \
                LMC_CSR_WRITE((sc), csr_9, 0x20000); \
		lmc_delay(); \
		LMC_CSR_WRITE((sc), csr_9, 0x30000); \
                lmc_delay(); \
		n--; }} while(0)

struct lmc_regfile_t {
    lmc_csrptr_t csr_busmode;                  /* CSR0 */
    lmc_csrptr_t csr_txpoll;                   /* CSR1 */
    lmc_csrptr_t csr_rxpoll;                   /* CSR2 */
    lmc_csrptr_t csr_rxlist;                   /* CSR3 */
    lmc_csrptr_t csr_txlist;                   /* CSR4 */
    lmc_csrptr_t csr_status;                   /* CSR5 */
    lmc_csrptr_t csr_command;                  /* CSR6 */
    lmc_csrptr_t csr_intr;                     /* CSR7 */
    lmc_csrptr_t csr_missed_frames;            /* CSR8 */
    lmc_csrptr_t csr_9;                        /* CSR9 */
    lmc_csrptr_t csr_10;                       /* CSR10 */
    lmc_csrptr_t csr_11;                       /* CSR11 */
    lmc_csrptr_t csr_12;                       /* CSR12 */
    lmc_csrptr_t csr_13;                       /* CSR13 */
    lmc_csrptr_t csr_14;                       /* CSR14 */
    lmc_csrptr_t csr_15;                       /* CSR15 */
};

#define csr_enetrom             csr_9   /* 21040 */
#define csr_reserved            csr_10  /* 21040 */
#define csr_full_duplex         csr_11  /* 21040 */
#define csr_bootrom             csr_10  /* 21041/21140A/?? */
#define csr_gp                  csr_12  /* 21140* */
#define csr_watchdog            csr_15  /* 21140* */
#define csr_gp_timer            csr_11  /* 21041/21140* */
#define csr_srom_mii            csr_9   /* 21041/21140* */
#define csr_sia_status          csr_12  /* 2104x */
#define csr_sia_connectivity    csr_13  /* 2104x */
#define csr_sia_tx_rx           csr_14  /* 2104x */
#define csr_sia_general         csr_15  /* 2104x */

/* tulip length/control transmit descriptor definitions
 *  used to define bits in the second tulip_desc_t field (length)
 *  for the transmit descriptor -baz */

#define LMC_TDES_FIRST_BUFFER_SIZE       ((u32)(0x000007FF))
#define LMC_TDES_SECOND_BUFFER_SIZE      ((u32)(0x003FF800))
#define LMC_TDES_HASH_FILTERING          ((u32)(0x00400000))
#define LMC_TDES_DISABLE_PADDING         ((u32)(0x00800000))
#define LMC_TDES_SECOND_ADDR_CHAINED     ((u32)(0x01000000))
#define LMC_TDES_END_OF_RING             ((u32)(0x02000000))
#define LMC_TDES_ADD_CRC_DISABLE         ((u32)(0x04000000))
#define LMC_TDES_SETUP_PACKET            ((u32)(0x08000000))
#define LMC_TDES_INVERSE_FILTERING       ((u32)(0x10000000))
#define LMC_TDES_FIRST_SEGMENT           ((u32)(0x20000000))
#define LMC_TDES_LAST_SEGMENT            ((u32)(0x40000000))
#define LMC_TDES_INTERRUPT_ON_COMPLETION ((u32)(0x80000000))

#define TDES_SECOND_BUFFER_SIZE_BIT_NUMBER  11
#define TDES_COLLISION_COUNT_BIT_NUMBER     3

/* Constants for the RCV descriptor RDES */

#define LMC_RDES_OVERFLOW             ((u32)(0x00000001))
#define LMC_RDES_CRC_ERROR            ((u32)(0x00000002))
#define LMC_RDES_DRIBBLING_BIT        ((u32)(0x00000004))
#define LMC_RDES_REPORT_ON_MII_ERR    ((u32)(0x00000008))
#define LMC_RDES_RCV_WATCHDOG_TIMEOUT ((u32)(0x00000010))
#define LMC_RDES_FRAME_TYPE           ((u32)(0x00000020))
#define LMC_RDES_COLLISION_SEEN       ((u32)(0x00000040))
#define LMC_RDES_FRAME_TOO_LONG       ((u32)(0x00000080))
#define LMC_RDES_LAST_DESCRIPTOR      ((u32)(0x00000100))
#define LMC_RDES_FIRST_DESCRIPTOR     ((u32)(0x00000200))
#define LMC_RDES_MULTICAST_FRAME      ((u32)(0x00000400))
#define LMC_RDES_RUNT_FRAME           ((u32)(0x00000800))
#define LMC_RDES_DATA_TYPE            ((u32)(0x00003000))
#define LMC_RDES_LENGTH_ERROR         ((u32)(0x00004000))
#define LMC_RDES_ERROR_SUMMARY        ((u32)(0x00008000))
#define LMC_RDES_FRAME_LENGTH         ((u32)(0x3FFF0000))
#define LMC_RDES_OWN_BIT              ((u32)(0x80000000))

#define RDES_FRAME_LENGTH_BIT_NUMBER       16

#define LMC_RDES_ERROR_MASK ( (u32)( \
	  LMC_RDES_OVERFLOW \
	| LMC_RDES_DRIBBLING_BIT \
	| LMC_RDES_REPORT_ON_MII_ERR \
        | LMC_RDES_COLLISION_SEEN ) )


/*
 * Ioctl info
 */

typedef struct {
	u32	n;
	u32	m;
	u32	v;
	u32	x;
	u32	r;
	u32	f;
	u32	exact;
} lmc_av9110_t;

/*
 * Common structure passed to the ioctl code.
 */
struct lmc___ctl {
	u32	cardtype;
	u32	clock_source;		/* HSSI, T1 */
	u32	clock_rate;		/* T1 */
	u32	crc_length;
	u32	cable_length;		/* DS3 */
	u32	scrambler_onoff;	/* DS3 */
	u32	cable_type;		/* T1 */
	u32	keepalive_onoff;	/* protocol */
	u32	ticks;			/* ticks/sec */
	union {
		lmc_av9110_t	ssi;
	} cardspec;
	u32       circuit_type;   /* T1 or E1 */
};


/*
 * Careful, look at the data sheet, there's more to this
 * structure than meets the eye.  It should probably be:
 *
 * struct tulip_desc_t {
 *         u8  own:1;
 *         u32 status:31;
 *         u32 control:10;
 *         u32 buffer1;
 *         u32 buffer2;
 * };
 * You could also expand status control to provide more bit information
 */

struct tulip_desc_t {
	s32 status;
	s32 length;
	u32 buffer1;
	u32 buffer2;
};

/*
 * media independent methods to check on media status, link, light LEDs,
 * etc.
 */
struct lmc___media {
	void	(* init)(lmc_softc_t * const);
	void	(* defaults)(lmc_softc_t * const);
	void	(* set_status)(lmc_softc_t * const, lmc_ctl_t *);
	void	(* set_clock_source)(lmc_softc_t * const, int);
	void	(* set_speed)(lmc_softc_t * const, lmc_ctl_t *);
	void	(* set_cable_length)(lmc_softc_t * const, int);
	void	(* set_scrambler)(lmc_softc_t * const, int);
	int	(* get_link_status)(lmc_softc_t * const);
	void	(* set_link_status)(lmc_softc_t * const, int);
	void	(* set_crc_length)(lmc_softc_t * const, int);
        void    (* set_circuit_type)(lmc_softc_t * const, int);
        void	(* watchdog)(lmc_softc_t * const);
};


#define STATCHECK     0xBEEFCAFE

struct lmc_extra_statistics
{
	u32       version_size;
	u32       lmc_cardtype;

	u32       tx_ProcTimeout;
	u32       tx_IntTimeout;
	u32       tx_NoCompleteCnt;
	u32       tx_MaxXmtsB4Int;
	u32       tx_TimeoutCnt;
	u32       tx_OutOfSyncPtr;
	u32       tx_tbusy0;
	u32       tx_tbusy1;
	u32       tx_tbusy_calls;
	u32       resetCount;
	u32       lmc_txfull;
	u32       tbusy;
	u32       dirtyTx;
	u32       lmc_next_tx;
	u32       otherTypeCnt;
	u32       lastType;
	u32       lastTypeOK;
	u32       txLoopCnt;
	u32       usedXmtDescripCnt;
	u32       txIndexCnt;
	u32       rxIntLoopCnt;

	u32       rx_SmallPktCnt;
	u32       rx_BadPktSurgeCnt;
	u32       rx_BuffAllocErr;
	u32       tx_lossOfClockCnt;

	/* T1 error counters */
	u32       framingBitErrorCount;
	u32       lineCodeViolationCount;

	u32       lossOfFrameCount;
	u32       changeOfFrameAlignmentCount;
	u32       severelyErroredFrameCount;

	u32       check;
};

typedef struct lmc_xinfo {
	u32       Magic0;                         /* BEEFCAFE */

	u32       PciCardType;
	u32       PciSlotNumber;          /* PCI slot number       */

	u16	       DriverMajorVersion;
	u16	       DriverMinorVersion;
	u16	       DriverSubVersion;

	u16	       XilinxRevisionNumber;
	u16	       MaxFrameSize;

	u16     	  t1_alarm1_status;
	u16       	t1_alarm2_status;

	int             link_status;
	u32       mii_reg16;

	u32       Magic1;                         /* DEADBEEF */
} LMC_XINFO;


/*
 * forward decl
 */
struct lmc___softc {
	char                   *name;
	u8			board_idx;
	struct lmc_extra_statistics extra_stats;
	struct net_device      *lmc_device;

	int                     hang, rxdesc, bad_packet, some_counter;
	u32  	         	txgo;
	struct lmc_regfile_t	lmc_csrs;
	volatile u32		lmc_txtick;
	volatile u32		lmc_rxtick;
	u32			lmc_flags;
	u32			lmc_intrmask;	/* our copy of csr_intr */
	u32			lmc_cmdmode;	/* our copy of csr_cmdmode */
	u32			lmc_busmode;	/* our copy of csr_busmode */
	u32			lmc_gpio_io;	/* state of in/out settings */
	u32			lmc_gpio;	/* state of outputs */
	struct sk_buff*		lmc_txq[LMC_TXDESCS];
	struct sk_buff*		lmc_rxq[LMC_RXDESCS];
	volatile
	struct tulip_desc_t	lmc_rxring[LMC_RXDESCS];
	volatile
	struct tulip_desc_t	lmc_txring[LMC_TXDESCS];
	unsigned int		lmc_next_rx, lmc_next_tx;
	volatile
	unsigned int		lmc_taint_tx, lmc_taint_rx;
	int			lmc_tx_start, lmc_txfull;
	int			lmc_txbusy;
	u16			lmc_miireg16;
	int			lmc_ok;
	int			last_link_status;
	int			lmc_cardtype;
	u32               	last_frameerr;
	lmc_media_t	       *lmc_media;
	struct timer_list	timer;
	lmc_ctl_t		ictl;
	u32			TxDescriptControlInit;

	int                     tx_TimeoutInd; /* additional driver state */
	int                     tx_TimeoutDisplay;
	unsigned int		lastlmc_taint_tx;
	int                     lasttx_packets;
	u32			tx_clockState;
	u32			lmc_crcSize;
	LMC_XINFO		lmc_xinfo;
	char                    lmc_yel, lmc_blue, lmc_red; /* for T1 and DS3 */
	char                    lmc_timing; /* for HSSI and SSI */
	int                     got_irq;

	char                    last_led_err[4];

	u32                     last_int;
	u32                     num_int;

	spinlock_t              lmc_lock;
	u16			if_type;       /* HDLC/PPP or NET */

	/* Failure cases */
	u8			failed_ring;
	u8			failed_recv_alloc;

	/* Structure check */
	u32                     check;
};

#define LMC_PCI_TIME 1
#define LMC_EXT_TIME 0

#define PKT_BUF_SZ              1542  /* was 1536 */

/* CSR5 settings */
#define TIMER_INT     0x00000800
#define TP_LINK_FAIL  0x00001000
#define TP_LINK_PASS  0x00000010
#define NORMAL_INT    0x00010000
#define ABNORMAL_INT  0x00008000
#define RX_JABBER_INT 0x00000200
#define RX_DIED       0x00000100
#define RX_NOBUFF     0x00000080
#define RX_INT        0x00000040
#define TX_FIFO_UNDER 0x00000020
#define TX_JABBER     0x00000008
#define TX_NOBUFF     0x00000004
#define TX_DIED       0x00000002
#define TX_INT        0x00000001

/* CSR6 settings */
#define OPERATION_MODE  0x00000200 /* Full Duplex      */
#define PROMISC_MODE    0x00000040 /* Promiscuous Mode */
#define RECEIVE_ALL     0x40000000 /* Receive All      */
#define PASS_BAD_FRAMES 0x00000008 /* Pass Bad Frames  */

/* Dec control registers  CSR6 as well */
#define LMC_DEC_ST 0x00002000
#define LMC_DEC_SR 0x00000002

/* CSR15 settings */
#define RECV_WATCHDOG_DISABLE 0x00000010
#define JABBER_DISABLE        0x00000001

/* More settings */
/*
 * aSR6 -- Command (Operation Mode) Register
 */
#define TULIP_CMD_RECEIVEALL    0x40000000L /* (RW)  Receivel all frames? */
#define TULIP_CMD_MUSTBEONE     0x02000000L /* (RW)  Must Be One (21140) */
#define TULIP_CMD_TXTHRSHLDCTL  0x00400000L /* (RW)  Transmit Threshold Mode (21140) */
#define TULIP_CMD_STOREFWD      0x00200000L /* (RW)  Store and Forward (21140) */
#define TULIP_CMD_NOHEARTBEAT   0x00080000L /* (RW)  No Heartbeat (21140) */
#define TULIP_CMD_PORTSELECT    0x00040000L /* (RW)  Post Select (100Mb) (21140) */
#define TULIP_CMD_FULLDUPLEX    0x00000200L /* (RW)  Full Duplex Mode */
#define TULIP_CMD_OPERMODE      0x00000C00L /* (RW)  Operating Mode */
#define TULIP_CMD_PROMISCUOUS   0x00000041L /* (RW)  Promiscuous Mode */
#define TULIP_CMD_PASSBADPKT    0x00000008L /* (RW)  Pass Bad Frames */
#define TULIP_CMD_THRESHOLDCTL  0x0000C000L /* (RW)  Threshold Control */

#define TULIP_GP_PINSET         0x00000100L
#define TULIP_BUSMODE_SWRESET   0x00000001L
#define TULIP_WATCHDOG_TXDISABLE 0x00000001L
#define TULIP_WATCHDOG_RXDISABLE 0x00000010L

#define TULIP_STS_NORMALINTR    0x00010000L /* (RW)  Normal Interrupt */
#define TULIP_STS_ABNRMLINTR    0x00008000L /* (RW)  Abnormal Interrupt */
#define TULIP_STS_ERI           0x00004000L /* (RW)  Early Receive Interrupt */
#define TULIP_STS_SYSERROR      0x00002000L /* (RW)  System Error */
#define TULIP_STS_GTE           0x00000800L /* (RW)  General Pupose Timer Exp */
#define TULIP_STS_ETI           0x00000400L /* (RW)  Early Transmit Interrupt */
#define TULIP_STS_RXWT          0x00000200L /* (RW)  Receiver Watchdog Timeout */
#define TULIP_STS_RXSTOPPED     0x00000100L /* (RW)  Receiver Process Stopped */
#define TULIP_STS_RXNOBUF       0x00000080L /* (RW)  Receive Buf Unavail */
#define TULIP_STS_RXINTR        0x00000040L /* (RW)  Receive Interrupt */
#define TULIP_STS_TXUNDERFLOW   0x00000020L /* (RW)  Transmit Underflow */
#define TULIP_STS_TXJABER       0x00000008L /* (RW)  Jabber timeout */
#define TULIP_STS_TXNOBUF       0x00000004L
#define TULIP_STS_TXSTOPPED     0x00000002L /* (RW)  Transmit Process Stopped */
#define TULIP_STS_TXINTR        0x00000001L /* (RW)  Transmit Interrupt */

#define TULIP_STS_RXS_STOPPED   0x00000000L /*        000 - Stopped */

#define TULIP_STS_RXSTOPPED     0x00000100L             /* (RW)  Receive Process Stopped */
#define TULIP_STS_RXNOBUF       0x00000080L

#define TULIP_CMD_TXRUN         0x00002000L /* (RW)  Start/Stop Transmitter */
#define TULIP_CMD_RXRUN         0x00000002L /* (RW)  Start/Stop Receive Filtering */
#define TULIP_DSTS_TxDEFERRED   0x00000001      /* Initially Deferred */
#define TULIP_DSTS_OWNER        0x80000000      /* Owner (1 = 21040) */
#define TULIP_DSTS_RxMIIERR     0x00000008
#define LMC_DSTS_ERRSUM         (TULIP_DSTS_RxMIIERR)

#define TULIP_DEFAULT_INTR_MASK  (TULIP_STS_NORMALINTR \
  | TULIP_STS_RXINTR       \
  | TULIP_STS_TXINTR     \
  | TULIP_STS_ABNRMLINTR \
  | TULIP_STS_SYSERROR   \
  | TULIP_STS_TXSTOPPED  \
  | TULIP_STS_TXUNDERFLOW\
  | TULIP_STS_RXSTOPPED )

#define DESC_OWNED_BY_SYSTEM   ((u32)(0x00000000))
#define DESC_OWNED_BY_DC21X4   ((u32)(0x80000000))

#ifndef TULIP_CMD_RECEIVEALL
#define TULIP_CMD_RECEIVEALL 0x40000000L
#endif

/* Adapter module number */
#define LMC_ADAP_HSSI           2
#define LMC_ADAP_DS3            3
#define LMC_ADAP_SSI            4
#define LMC_ADAP_T1             5

#define LMC_MTU 1500

#define LMC_CRC_LEN_16 2  /* 16-bit CRC */
#define LMC_CRC_LEN_32 4

#endif /* _LMC_VAR_H_ */
