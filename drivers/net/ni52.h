/*
 * Intel i82586 Ethernet definitions
 *
 * This is an extension to the Linux operating system, and is covered by the
 * same GNU General Public License that covers that work.
 *
 * copyrights (c) 1994 by Michael Hipp (hippm@informatik.uni-tuebingen.de)
 *
 * I have done a look in the following sources:
 *   crynwr-packet-driver by Russ Nelson
 *   Garret A. Wollman's i82586-driver for BSD
 */


#define NI52_RESET     0  /* writing to this address, resets the i82586 */
#define NI52_ATTENTION 1  /* channel attention, kick the 586 */
#define NI52_TENA      3  /* 2-5 possibly wrong, Xmit enable */
#define NI52_TDIS      2  /* Xmit disable */
#define NI52_INTENA    5  /* Interrupt enable */
#define NI52_INTDIS    4  /* Interrupt disable */
#define NI52_MAGIC1    6  /* dunno exact function */
#define NI52_MAGIC2    7  /* dunno exact function */

#define NI52_MAGICVAL1 0x00  /* magic-values for ni5210 card */
#define NI52_MAGICVAL2 0x55

/*
 * where to find the System Configuration Pointer (SCP)
 */
#define SCP_DEFAULT_ADDRESS 0xfffff4


/*
 * System Configuration Pointer Struct
 */

struct scp_struct
{
	u16 zero_dum0;	/* has to be zero */
	u8 sysbus;	/* 0=16Bit,1=8Bit */
	u8 zero_dum1;	/* has to be zero for 586 */
	u8 zero_dum2;
	u8 zero_dum3;
	u32 iscp;		/* pointer to the iscp-block */
};


/*
 * Intermediate System Configuration Pointer (ISCP)
 */
struct iscp_struct
{
	u8 busy;          /* 586 clears after successful init */
	u8 zero_dummy;    /* has to be zero */
	u16 scb_offset;    /* pointeroffset to the scb_base */
	u32 scb_base;      /* base-address of all 16-bit offsets */
};

/*
 * System Control Block (SCB)
 */
struct scb_struct
{
	u8 rus;
	u8 cus;
	u8 cmd_ruc;        /* command word: RU part */
	u8 cmd_cuc;        /* command word: CU part & ACK */
	u16 cbl_offset;    /* pointeroffset, command block list */
	u16 rfa_offset;    /* pointeroffset, receive frame area */
	u16 crc_errs;      /* CRC-Error counter */
	u16 aln_errs;      /* alignmenterror counter */
	u16 rsc_errs;      /* Resourceerror counter */
	u16 ovrn_errs;     /* OVerrunerror counter */
};

/*
 * possible command values for the command word
 */
#define RUC_MASK	0x0070	/* mask for RU commands */
#define RUC_NOP		0x0000	/* NOP-command */
#define RUC_START	0x0010	/* start RU */
#define RUC_RESUME	0x0020	/* resume RU after suspend */
#define RUC_SUSPEND	0x0030	/* suspend RU */
#define RUC_ABORT	0x0040	/* abort receiver operation immediately */

#define CUC_MASK        0x07  /* mask for CU command */
#define CUC_NOP         0x00  /* NOP-command */
#define CUC_START       0x01  /* start execution of 1. cmd on the CBL */
#define CUC_RESUME      0x02  /* resume after suspend */
#define CUC_SUSPEND     0x03  /* Suspend CU */
#define CUC_ABORT       0x04  /* abort command operation immediately */

#define ACK_MASK        0xf0  /* mask for ACK command */
#define ACK_CX          0x80  /* acknowledges STAT_CX */
#define ACK_FR          0x40  /* ack. STAT_FR */
#define ACK_CNA         0x20  /* ack. STAT_CNA */
#define ACK_RNR         0x10  /* ack. STAT_RNR */

/*
 * possible status values for the status word
 */
#define STAT_MASK       0xf0  /* mask for cause of interrupt */
#define STAT_CX         0x80  /* CU finished cmd with its I bit set */
#define STAT_FR         0x40  /* RU finished receiving a frame */
#define STAT_CNA        0x20  /* CU left active state */
#define STAT_RNR        0x10  /* RU left ready state */

#define CU_STATUS       0x7   /* CU status, 0=idle */
#define CU_SUSPEND      0x1   /* CU is suspended */
#define CU_ACTIVE       0x2   /* CU is active */

#define RU_STATUS	0x70	/* RU status, 0=idle */
#define RU_SUSPEND	0x10	/* RU suspended */
#define RU_NOSPACE	0x20	/* RU no resources */
#define RU_READY	0x40	/* RU is ready */

/*
 * Receive Frame Descriptor (RFD)
 */
struct rfd_struct
{
	u8  stat_low;	/* status word */
	u8  stat_high;	/* status word */
	u8  rfd_sf;	/* 82596 mode only */
	u8  last;		/* Bit15,Last Frame on List / Bit14,suspend */
	u16 next;		/* linkoffset to next RFD */
	u16 rbd_offset;	/* pointeroffset to RBD-buffer */
	u8  dest[6];	/* ethernet-address, destination */
	u8  source[6];	/* ethernet-address, source */
	u16 length;	/* 802.3 frame-length */
	u16 zero_dummy;	/* dummy */
};

#define RFD_LAST     0x80	/* last: last rfd in the list */
#define RFD_SUSP     0x40	/* last: suspend RU after  */
#define RFD_COMPL    0x80
#define RFD_OK       0x20
#define RFD_BUSY     0x40
#define RFD_ERR_LEN  0x10     /* Length error (if enabled length-checking */
#define RFD_ERR_CRC  0x08     /* CRC error */
#define RFD_ERR_ALGN 0x04     /* Alignment error */
#define RFD_ERR_RNR  0x02     /* status: receiver out of resources */
#define RFD_ERR_OVR  0x01     /* DMA Overrun! */

#define RFD_ERR_FTS  0x0080	/* Frame to short */
#define RFD_ERR_NEOP 0x0040	/* No EOP flag (for bitstuffing only) */
#define RFD_ERR_TRUN 0x0020	/* (82596 only/SF mode) indicates truncated frame */
#define RFD_MATCHADD 0x0002     /* status: Destinationaddress !matches IA (only 82596) */
#define RFD_COLLDET  0x0001	/* Detected collision during reception */

/*
 * Receive Buffer Descriptor (RBD)
 */
struct rbd_struct
{
	u16 status;	/* status word,number of used bytes in buff */
	u16 next;		/* pointeroffset to next RBD */
	u32 buffer;	/* receive buffer address pointer */
	u16 size;		/* size of this buffer */
	u16 zero_dummy;    /* dummy */
};

#define RBD_LAST	0x8000	/* last buffer */
#define RBD_USED	0x4000	/* this buffer has data */
#define RBD_MASK	0x3fff	/* size-mask for length */

/*
 * Statusvalues for Commands/RFD
 */
#define STAT_COMPL   0x8000	/* status: frame/command is complete */
#define STAT_BUSY    0x4000	/* status: frame/command is busy */
#define STAT_OK      0x2000	/* status: frame/command is ok */

/*
 * Action-Commands
 */
#define CMD_NOP		0x0000	/* NOP */
#define CMD_IASETUP	0x0001	/* initial address setup command */
#define CMD_CONFIGURE	0x0002	/* configure command */
#define CMD_MCSETUP	0x0003	/* MC setup command */
#define CMD_XMIT	0x0004	/* transmit command */
#define CMD_TDR		0x0005	/* time domain reflectometer (TDR) command */
#define CMD_DUMP	0x0006	/* dump command */
#define CMD_DIAGNOSE	0x0007	/* diagnose command */

/*
 * Action command bits
 */
#define CMD_LAST	0x8000	/* indicates last command in the CBL */
#define CMD_SUSPEND	0x4000	/* suspend CU after this CB */
#define CMD_INT		0x2000	/* generate interrupt after execution */

/*
 * NOP - command
 */
struct nop_cmd_struct
{
	u16 cmd_status;	/* status of this command */
	u16 cmd_cmd;       /* the command itself (+bits) */
	u16 cmd_link;      /* offsetpointer to next command */
};

/*
 * IA Setup command
 */
struct iasetup_cmd_struct
{
	u16 cmd_status;
	u16 cmd_cmd;
	u16 cmd_link;
	u8  iaddr[6];
};

/*
 * Configure command
 */
struct configure_cmd_struct
{
	u16 cmd_status;
	u16 cmd_cmd;
	u16 cmd_link;
	u8  byte_cnt;   /* size of the config-cmd */
	u8  fifo;       /* fifo/recv monitor */
	u8  sav_bf;     /* save bad frames (bit7=1)*/
	u8  adr_len;    /* adr_len(0-2),al_loc(3),pream(4-5),loopbak(6-7)*/
	u8  priority;   /* lin_prio(0-2),exp_prio(4-6),bof_metd(7) */
	u8  ifs;        /* inter frame spacing */
	u8  time_low;   /* slot time low */
	u8  time_high;  /* slot time high(0-2) and max. retries(4-7) */
	u8  promisc;    /* promisc-mode(0) , et al (1-7) */
	u8  carr_coll;  /* carrier(0-3)/collision(4-7) stuff */
	u8  fram_len;   /* minimal frame len */
	u8  dummy;	     /* dummy */
};

/*
 * Multicast Setup command
 */
struct mcsetup_cmd_struct
{
	u16 cmd_status;
	u16 cmd_cmd;
	u16 cmd_link;
	u16 mc_cnt;		/* number of bytes in the MC-List */
	u8  mc_list[0][6];  	/* pointer to 6 bytes entries */
};

/*
 * DUMP command
 */
struct dump_cmd_struct
{
	u16 cmd_status;
	u16 cmd_cmd;
	u16 cmd_link;
	u16 dump_offset;    /* pointeroffset to DUMP space */
};

/*
 * transmit command
 */
struct transmit_cmd_struct
{
	u16 cmd_status;
	u16 cmd_cmd;
	u16 cmd_link;
	u16 tbd_offset;	/* pointeroffset to TBD */
	u8  dest[6];       /* destination address of the frame */
	u16 length;	/* user defined: 802.3 length / Ether type */
};

#define TCMD_ERRMASK     0x0fa0
#define TCMD_MAXCOLLMASK 0x000f
#define TCMD_MAXCOLL     0x0020
#define TCMD_HEARTBEAT   0x0040
#define TCMD_DEFERRED    0x0080
#define TCMD_UNDERRUN    0x0100
#define TCMD_LOSTCTS     0x0200
#define TCMD_NOCARRIER   0x0400
#define TCMD_LATECOLL    0x0800

struct tdr_cmd_struct
{
	u16 cmd_status;
	u16 cmd_cmd;
	u16 cmd_link;
	u16 status;
};

#define TDR_LNK_OK	0x8000	/* No link problem identified */
#define TDR_XCVR_PRB	0x4000	/* indicates a transceiver problem */
#define TDR_ET_OPN	0x2000	/* open, no correct termination */
#define TDR_ET_SRT	0x1000	/* TDR detected a short circuit */
#define TDR_TIMEMASK	0x07ff	/* mask for the time field */

/*
 * Transmit Buffer Descriptor (TBD)
 */
struct tbd_struct
{
	u16 size;		/* size + EOF-Flag(15) */
	u16 next;          /* pointeroffset to next TBD */
	u32 buffer;        /* pointer to buffer */
};

#define TBD_LAST 0x8000         /* EOF-Flag, indicates last buffer in list */




