/*
 * Copyright 2003 Digi International (www.digi.com)
 *      Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	NOTE: THIS IS A SHARED HEADER. DO NOT CHANGE CODING STYLE!!!
 *
 *************************************************************************
 *
 * Driver includes
 *
 *************************************************************************/

#ifndef __DGAP_DRIVER_H
#define __DGAP_DRIVER_H

#include <linux/types.h>        /* To pick up the varions Linux types */
#include <linux/tty.h>          /* To pick up the various tty structs/defines */
#include <linux/interrupt.h>    /* For irqreturn_t type */

#ifndef TRUE
# define TRUE 1
#endif

#ifndef FALSE
# define FALSE 0
#endif

#if !defined(TTY_FLIPBUF_SIZE)
# define TTY_FLIPBUF_SIZE 512
#endif

/*************************************************************************
 *
 * Driver defines
 *
 *************************************************************************/

/*
 * Driver identification
 */
#define	DG_NAME		"dgap-1.3-16"
#define	DG_PART		"40002347_C"
#define	DRVSTR		"dgap"

/*
 * defines from dgap_pci.h
 */
#define PCIMAX 32			/* maximum number of PCI boards */

#define DIGI_VID		0x114F

#define PCI_DEV_EPC_DID		0x0002
#define PCI_DEV_XEM_DID		0x0004
#define PCI_DEV_XR_DID		0x0005
#define PCI_DEV_CX_DID		0x0006
#define PCI_DEV_XRJ_DID		0x0009	/* PLX-based Xr adapter */
#define PCI_DEV_XR_IBM_DID	0x0011	/* IBM 8-port Async Adapter */
#define PCI_DEV_XR_BULL_DID	0x0013	/* BULL 8-port Async Adapter */
#define PCI_DEV_XR_SAIP_DID	0x001c	/* SAIP card - Xr adapter */
#define PCI_DEV_XR_422_DID	0x0012	/* Xr-422 */
#define PCI_DEV_920_2_DID	0x0034	/* XR-Plus 920 K, 2 port */
#define PCI_DEV_920_4_DID	0x0026	/* XR-Plus 920 K, 4 port */
#define PCI_DEV_920_8_DID	0x0027	/* XR-Plus 920 K, 8 port */
#define PCI_DEV_EPCJ_DID	0x000a	/* PLX 9060 chip for PCI  */
#define PCI_DEV_CX_IBM_DID	0x001b	/* IBM 128-port Async Adapter */
#define PCI_DEV_920_8_HP_DID	0x0058	/* HP XR-Plus 920 K, 8 port */
#define PCI_DEV_XEM_HP_DID	0x0059  /* HP Xem PCI */

#define PCI_DEV_XEM_NAME	"AccelePort XEM"
#define PCI_DEV_CX_NAME		"AccelePort CX"
#define PCI_DEV_XR_NAME		"AccelePort Xr"
#define PCI_DEV_XRJ_NAME	"AccelePort Xr (PLX)"
#define PCI_DEV_XR_SAIP_NAME	"AccelePort Xr (SAIP)"
#define PCI_DEV_920_2_NAME	"AccelePort Xr920 2 port"
#define PCI_DEV_920_4_NAME	"AccelePort Xr920 4 port"
#define PCI_DEV_920_8_NAME	"AccelePort Xr920 8 port"
#define PCI_DEV_XR_422_NAME	"AccelePort Xr 422"
#define PCI_DEV_EPCJ_NAME	"AccelePort EPC (PLX)"
#define PCI_DEV_XR_BULL_NAME	"AccelePort Xr (BULL)"
#define PCI_DEV_XR_IBM_NAME	"AccelePort Xr (IBM)"
#define PCI_DEV_CX_IBM_NAME	"AccelePort CX (IBM)"
#define PCI_DEV_920_8_HP_NAME	"AccelePort Xr920 8 port (HP)"
#define PCI_DEV_XEM_HP_NAME	"AccelePort XEM (HP)"

/*
 * On the PCI boards, there is no IO space allocated
 * The I/O registers will be in the first 3 bytes of the
 * upper 2MB of the 4MB memory space.  The board memory
 * will be mapped into the low 2MB of the 4MB memory space
 */

/* Potential location of PCI Bios from E0000 to FFFFF*/
#define PCI_BIOS_SIZE		0x00020000

/* Size of Memory and I/O for PCI (4MB) */
#define PCI_RAM_SIZE		0x00400000

/* Size of Memory (2MB) */
#define PCI_MEM_SIZE		0x00200000

/* Max PCI Window Size (2MB) */
#define PCI_WIN_SIZE		0x00200000

#define PCI_WIN_SHIFT		21 /* 21 bits max */

/* Offset of I/0 in Memory (2MB) */
#define PCI_IO_OFFSET		0x00200000

/* Size of IO (2MB) */
#define PCI_IO_SIZE_DGAP	0x00200000

/* Number of boards we support at once. */
#define	MAXBOARDS	32
#define	MAXPORTS	224
#define MAXTTYNAMELEN	200

/* Our 3 magic numbers for our board, channel and unit structs */
#define DGAP_BOARD_MAGIC	0x5c6df104
#define DGAP_CHANNEL_MAGIC	0x6c6df104
#define DGAP_UNIT_MAGIC		0x7c6df104

/* Serial port types */
#define DGAP_SERIAL		0
#define DGAP_PRINT		1

#define	SERIAL_TYPE_NORMAL	1

/* 4 extra for alignment play space */
#define WRITEBUFLEN		((4096) + 4)
#define MYFLIPLEN		N_TTY_BUF_SIZE

#define SBREAK_TIME 0x25
#define U2BSIZE 0x400

#define dgap_jiffies_from_ms(a) (((a) * HZ) / 1000)

/*
 * Our major for the mgmt devices.
 *
 * We can use 22, because Digi was allocated 22 and 23 for the epca driver.
 * 22 has now become obsolete now that the "cu" devices have
 * been removed from 2.6.
 * Also, this *IS* the epca driver, just PCI only now.
 */
#ifndef DIGI_DGAP_MAJOR
# define DIGI_DGAP_MAJOR         22
#endif

/*
 * The parameters we use to define the periods of the moving averages.
 */
#define		MA_PERIOD	(HZ / 10)
#define		SMA_DUR		(1 * HZ)
#define		EMA_DUR		(1 * HZ)
#define		SMA_NPERIODS	(SMA_DUR / MA_PERIOD)
#define		EMA_NPERIODS	(EMA_DUR / MA_PERIOD)

/*
 * Define a local default termios struct. All ports will be created
 * with this termios initially.  This is the same structure that is defined
 * as the default in tty_io.c with the same settings overridden as in serial.c
 *
 * In short, this should match the internal serial ports' defaults.
 */
#define	DEFAULT_IFLAGS	(ICRNL | IXON)
#define	DEFAULT_OFLAGS	(OPOST | ONLCR)
#define	DEFAULT_CFLAGS	(B9600 | CS8 | CREAD | HUPCL | CLOCAL)
#define	DEFAULT_LFLAGS	(ISIG | ICANON | ECHO | ECHOE | ECHOK | \
			ECHOCTL | ECHOKE | IEXTEN)

#ifndef _POSIX_VDISABLE
#define _POSIX_VDISABLE ('\0')
#endif

#define SNIFF_MAX	65536		/* Sniff buffer size (2^n) */
#define SNIFF_MASK	(SNIFF_MAX - 1)	/* Sniff wrap mask */

#define VPDSIZE (512)

/************************************************************************
 *      FEP memory offsets
 ************************************************************************/
#define START           0x0004L         /* Execution start address      */

#define CMDBUF          0x0d10L         /* Command (cm_t) structure offset */
#define CMDSTART        0x0400L         /* Start of command buffer      */
#define CMDMAX          0x0800L         /* End of command buffer        */

#define EVBUF           0x0d18L         /* Event (ev_t) structure       */
#define EVSTART         0x0800L         /* Start of event buffer        */
#define EVMAX           0x0c00L         /* End of event buffer          */
#define FEP5_PLUS       0x0E40          /* ASCII '5' and ASCII 'A' is here  */
#define ECS_SEG         0x0E44          /* Segment of the extended      */
					/* channel structure            */
#define LINE_SPEED      0x10            /* Offset into ECS_SEG for line */
					/* speed if the fep has extended */
					/* capabilities                 */

/* BIOS MAGIC SPOTS */
#define ERROR           0x0C14L		/* BIOS error code              */
#define SEQUENCE	0x0C12L		/* BIOS sequence indicator      */
#define POSTAREA	0x0C00L		/* POST complete message area   */

/* FEP MAGIC SPOTS */
#define FEPSTAT         POSTAREA        /* OS here when FEP comes up    */
#define NCHAN           0x0C02L         /* number of ports FEP sees     */
#define PANIC           0x0C10L         /* PANIC area for FEP           */
#define KMEMEM          0x0C30L         /* Memory for KME use           */
#define CONFIG          0x0CD0L         /* Concentrator configuration info */
#define CONFIGSIZE      0x0030          /* configuration info size      */
#define DOWNREQ         0x0D00          /* Download request buffer pointer */

#define CHANBUF         0x1000L         /* Async channel (bs_t) structs */
#define FEPOSSIZE       0x1FFF          /* 8K FEPOS                     */

#define XEMPORTS    0xC02	/*
				 * Offset in board memory where FEP5 stores
				 * how many ports it has detected.
				 * NOTE: FEP5 reports 64 ports when the user
				 * has the cable in EBI OUT instead of EBI IN.
				 */

#define FEPCLR      0x00
#define FEPMEM      0x02
#define FEPRST      0x04
#define FEPINT      0x08
#define FEPMASK     0x0e
#define FEPWIN      0x80

#define LOWMEM      0x0100
#define HIGHMEM     0x7f00

#define FEPTIMEOUT 200000

#define ENABLE_INTR	0x0e04		/* Enable interrupts flag */
#define FEPPOLL_MIN	1		/* minimum of 1 millisecond */
#define FEPPOLL_MAX	20		/* maximum of 20 milliseconds */
#define FEPPOLL		0x0c26		/* Fep event poll interval */

#define	IALTPIN		0x0080		/* Input flag to swap DSR <-> DCD */

/************************************************************************
 * FEP supported functions
 ************************************************************************/
#define SRLOW		0xe0		/* Set receive low water	*/
#define SRHIGH		0xe1		/* Set receive high water	*/
#define FLUSHTX		0xe2		/* Flush transmit buffer	*/
#define PAUSETX		0xe3		/* Pause data transmission	*/
#define RESUMETX	0xe4		/* Resume data transmission	*/
#define SMINT		0xe5		/* Set Modem Interrupt		*/
#define SAFLOWC		0xe6		/* Set Aux. flow control chars	*/
#define SBREAK		0xe8		/* Send break			*/
#define SMODEM		0xe9		/* Set 8530 modem control lines	*/
#define SIFLAG		0xea		/* Set UNIX iflags		*/
#define SFLOWC		0xeb		/* Set flow control characters	*/
#define STLOW		0xec		/* Set transmit low water mark	*/
#define RPAUSE		0xee		/* Pause receive		*/
#define RRESUME		0xef		/* Resume receive		*/
#define CHRESET		0xf0		/* Reset Channel		*/
#define BUFSETALL	0xf2		/* Set Tx & Rx buffer size avail*/
#define SOFLAG		0xf3		/* Set UNIX oflags		*/
#define SHFLOW		0xf4		/* Set hardware handshake	*/
#define SCFLAG		0xf5		/* Set UNIX cflags		*/
#define SVNEXT		0xf6		/* Set VNEXT character		*/
#define SPINTFC		0xfc		/* Reserved			*/
#define SCOMMODE	0xfd		/* Set RS232/422 mode		*/

/************************************************************************
 *	Modes for SCOMMODE
 ************************************************************************/
#define MODE_232	0x00
#define MODE_422	0x01

/************************************************************************
 *      Event flags.
 ************************************************************************/
#define IFBREAK         0x01            /* Break received               */
#define IFTLW           0x02            /* Transmit low water           */
#define IFTEM           0x04            /* Transmitter empty            */
#define IFDATA          0x08            /* Receive data present         */
#define IFMODEM         0x20            /* Modem status change          */

/************************************************************************
 *      Modem flags
 ************************************************************************/
#       define  DM_RTS          0x02    /* Request to send              */
#       define  DM_CD           0x80    /* Carrier detect               */
#       define  DM_DSR          0x20    /* Data set ready               */
#       define  DM_CTS          0x10    /* Clear to send                */
#       define  DM_RI           0x40    /* Ring indicator               */
#       define  DM_DTR          0x01    /* Data terminal ready          */

/*
 * defines from dgap_conf.h
 */
#define NULLNODE 0		/* header node, not used */
#define BNODE 1			/* Board node */
#define LNODE 2			/* Line node */
#define CNODE 3			/* Concentrator node */
#define MNODE 4			/* EBI Module node */
#define TNODE 5			/* tty name prefix node */
#define	CUNODE 6		/* cu name prefix (non-SCO) */
#define PNODE 7			/* trans. print prefix node */
#define JNODE 8			/* maJor number node */
#define ANODE 9			/* altpin */
#define	TSNODE 10		/* tty structure size */
#define CSNODE 11		/* channel structure size */
#define BSNODE 12		/* board structure size */
#define USNODE 13		/* unit schedule structure size */
#define FSNODE 14		/* f2200 structure size */
#define VSNODE 15		/* size of VPIX structures */
#define INTRNODE 16		/* enable interrupt */

/* Enumeration of tokens */
#define	BEGIN	1
#define	END	2
#define	BOARD	10

#define EPCFS	11 /* start of EPC family definitions */
#define	ICX		11
#define	MCX		13
#define PCX	14
#define	IEPC	15
#define	EEPC	16
#define	MEPC	17
#define	IPCM	18
#define	EPCM	19
#define	MPCM	20
#define PEPC	21
#define PPCM	22
#ifdef CP
#define ICP     23
#define ECP     24
#define MCP     25
#endif
#define EPCFE	25 /* end of EPC family definitions */
#define	PC2E	26
#define	PC4E	27
#define	PC4E8K	28
#define	PC8E	29
#define	PC8E8K	30
#define	PC16E	31
#define MC2E8K  34
#define MC4E8K  35
#define MC8E8K  36

#define AVANFS	42	/* start of Avanstar family definitions */
#define A8P	42
#define A16P	43
#define AVANFE	43	/* end of Avanstar family definitions */

#define DA2000FS	44 /* start of AccelePort 2000 family definitions */
#define DA22		44 /* AccelePort 2002 */
#define DA24		45 /* AccelePort 2004 */
#define DA28		46 /* AccelePort 2008 */
#define DA216		47 /* AccelePort 2016 */
#define DAR4		48 /* AccelePort RAS 4 port */
#define DAR8		49 /* AccelePort RAS 8 port */
#define DDR24		50 /* DataFire RAS 24 port */
#define DDR30		51 /* DataFire RAS 30 port */
#define DDR48		52 /* DataFire RAS 48 port */
#define DDR60		53 /* DataFire RAS 60 port */
#define DA2000FE	53 /* end of AccelePort 2000/RAS family definitions */

#define PCXRFS	106	/* start of PCXR family definitions */
#define	APORT4	106
#define	APORT8	107
#define PAPORT4 108
#define PAPORT8 109
#define APORT4_920I	110
#define APORT8_920I	111
#define APORT4_920P	112
#define APORT8_920P	113
#define APORT2_920P 114
#define PCXRFE	117	/* end of PCXR family definitions */

#define	LINE	82
#ifdef T1
#define T1M	83
#define E1M	84
#endif
#define	CONC	64
#define	CX	65
#define	EPC	66
#define	MOD	67
#define	PORTS	68
#define METHOD	69
#define CUSTOM	70
#define BASIC	71
#define STATUS	72
#define MODEM	73
/* The following tokens can appear in multiple places */
#define	SPEED	74
#define	NPORTS	75
#define	ID	76
#define CABLE	77
#define CONNECT	78
#define	MEM	80
#define DPSZ	81

#define	TTYN	90
#define	CU	91
#define	PRINT	92
#define	XPRINT	93
#define CMAJOR   94
#define ALTPIN  95
#define STARTO 96
#define USEINTR  97
#define PCIINFO  98

#define	TTSIZ	100
#define	CHSIZ	101
#define BSSIZ	102
#define	UNTSIZ	103
#define	F2SIZ	104
#define	VPSIZ	105

#define	TOTAL_BOARD	2
#define	CURRENT_BRD	4
#define	BOARD_TYPE	6
#define	IO_ADDRESS	8
#define	MEM_ADDRESS	10

#define	FIELDS_PER_PAGE	18

#define TB_FIELD	1
#define CB_FIELD	3
#define BT_FIELD	5
#define IO_FIELD	7
#define ID_FIELD	8
#define ME_FIELD	9
#define TTY_FIELD	11
#define CU_FIELD	13
#define PR_FIELD	15
#define MPR_FIELD	17

#define	MAX_FIELD	512

#define	INIT		0
#define	NITEMS		128
#define MAX_ITEM	512

#define	DSCRINST	1
#define	DSCRNUM		3
#define	ALTPINQ		5
#define	SSAVE		7

#define	DSCR		"32"
#define	ONETONINE	"123456789"
#define	ALL		"1234567890"

/*
 * All the possible states the driver can be while being loaded.
 */
enum {
	DRIVER_INITIALIZED = 0,
	DRIVER_READY
};

/*
 * All the possible states the board can be while booting up.
 */
enum {
	BOARD_FAILED = 0,
	BOARD_READY
};

/*
 * All the possible states that a requested concentrator image can be in.
 */
enum {
	NO_PENDING_CONCENTRATOR_REQUESTS = 0,
	NEED_CONCENTRATOR,
	REQUESTED_CONCENTRATOR
};

/*
 * Modem line constants are defined as macros because DSR and
 * DCD are swapable using the ditty altpin option.
 */
#define D_CD(ch)        ch->ch_cd       /* Carrier detect       */
#define D_DSR(ch)       ch->ch_dsr      /* Data set ready       */
#define D_RTS(ch)       DM_RTS          /* Request to send      */
#define D_CTS(ch)       DM_CTS          /* Clear to send        */
#define D_RI(ch)        DM_RI           /* Ring indicator       */
#define D_DTR(ch)       DM_DTR          /* Data terminal ready  */

/*************************************************************************
 *
 * Structures and closely related defines.
 *
 *************************************************************************/

/*
 * A structure to hold a statistics counter.  We also
 * compute moving averages for this counter.
 */
struct macounter {
	u32		cnt;	/* Total count */
	ulong		accum;	/* Acuumulator per period */
	ulong		sma;	/* Simple moving average */
	ulong		ema;	/* Exponential moving average */
};

/************************************************************************
 * Device flag definitions for bd_flags.
 ************************************************************************/
#define	BD_FEP5PLUS	0x0001          /* Supports FEP5 Plus commands */
#define BD_HAS_VPD	0x0002		/* Board has VPD info available */

/*
 *	Per-board information
 */
struct board_t {
	int		magic;		/* Board Magic number.  */
	int		boardnum;	/* Board number: 0-3 */

	int		type;		/* Type of board */
	char		*name;		/* Product Name */
	struct pci_dev	*pdev;		/* Pointer to the pci_dev struct */
	u16		vendor;		/* PCI vendor ID */
	u16		device;		/* PCI device ID */
	u16		subvendor;	/* PCI subsystem vendor ID */
	u16		subdevice;	/* PCI subsystem device ID */
	u8		rev;		/* PCI revision ID */
	uint		pci_bus;	/* PCI bus value */
	uint		pci_slot;	/* PCI slot value */
	u16		maxports;	/* MAX ports this board can handle */
	u8		vpd[VPDSIZE];	/* VPD of board, if found */
	u32		bd_flags;	/* Board flags */

	spinlock_t	bd_lock;	/* Used to protect board */

	u32		state;		/* State of card. */
	wait_queue_head_t state_wait;	/* Place to sleep on for state change */

	struct		tasklet_struct helper_tasklet; /* Poll helper tasklet */

	u32		wait_for_bios;
	u32		wait_for_fep;

	struct cnode    *bd_config;	/* Config of board */

	u16		nasync;		/* Number of ports on card */

	ulong		irq;		/* Interrupt request number */
	ulong		intr_count;	/* Count of interrupts */
	u32		intr_used;	/* Non-zero if using interrupts */
	u32		intr_running;	/* Non-zero if FEP knows its doing */
					/* interrupts */

	ulong		port;		/* Start of base io port of the card */
	ulong		port_end;	/* End of base io port of the card */
	ulong		membase;	/* Start of base memory of the card */
	ulong		membase_end;	/* End of base memory of the card */

	u8 __iomem	*re_map_port;	/* Remapped io port of the card */
	u8 __iomem	*re_map_membase;/* Remapped memory of the card */

	u8		inhibit_poller; /* Tells the poller to leave us alone */

	struct channel_t *channels[MAXPORTS]; /* array of pointers to our */
					      /* channels.                */

	struct tty_driver	*serial_driver;
	struct tty_port *serial_ports;
	char		serial_name[200];
	struct tty_driver	*print_driver;
	struct tty_port *printer_ports;
	char		print_name[200];

	struct bs_t __iomem *bd_bs;	/* Base structure pointer         */

	char	*flipbuf;		/* Our flip buffer, alloced if    */
					/* board is found                 */
	char	*flipflagbuf;		/* Our flip flag buffer, alloced  */
					/* if board is found              */

	u16		dpatype;	/* The board "type", as defined   */
					/* by DPA                         */
	u16		dpastatus;	/* The board "status", as defined */
					/* by DPA                         */

	u32		conc_dl_status;	/* Status of any pending conc     */
					/* download                       */
};

/************************************************************************
 * Unit flag definitions for un_flags.
 ************************************************************************/
#define UN_ISOPEN	0x0001		/* Device is open		*/
#define UN_CLOSING	0x0002		/* Line is being closed		*/
#define UN_IMM		0x0004		/* Service immediately		*/
#define UN_BUSY		0x0008		/* Some work this channel	*/
#define UN_BREAKI	0x0010		/* Input break received		*/
#define UN_PWAIT	0x0020		/* Printer waiting for terminal	*/
#define UN_TIME		0x0040		/* Waiting on time		*/
#define UN_EMPTY	0x0080		/* Waiting output queue empty	*/
#define UN_LOW		0x0100		/* Waiting output low water mark*/
#define UN_EXCL_OPEN	0x0200		/* Open for exclusive use	*/
#define UN_WOPEN	0x0400		/* Device waiting for open	*/
#define UN_WIOCTL	0x0800		/* Device waiting for open	*/
#define UN_HANGUP	0x8000		/* Carrier lost			*/

struct device;

/************************************************************************
 * Structure for terminal or printer unit.
 ************************************************************************/
struct un_t {
	int	magic;		/* Unit Magic Number.			*/
	struct	channel_t *un_ch;
	u32	un_time;
	u32	un_type;
	int	un_open_count;	/* Counter of opens to port		*/
	struct tty_struct *un_tty;/* Pointer to unit tty structure	*/
	u32	un_flags;	/* Unit flags				*/
	wait_queue_head_t un_flags_wait; /* Place to sleep to wait on unit */
	u32	un_dev;		/* Minor device number			*/
	tcflag_t un_oflag;	/* oflags being done on board		*/
	tcflag_t un_lflag;	/* lflags being done on board		*/
	struct device *un_sysfs;
};

/************************************************************************
 * Device flag definitions for ch_flags.
 ************************************************************************/
#define CH_PRON         0x0001          /* Printer on string                */
#define CH_OUT          0x0002          /* Dial-out device open             */
#define CH_STOP         0x0004          /* Output is stopped                */
#define CH_STOPI        0x0008          /* Input is stopped                 */
#define CH_CD           0x0010          /* Carrier is present               */
#define CH_FCAR         0x0020          /* Carrier forced on                */

#define CH_RXBLOCK      0x0080          /* Enable rx blocked flag           */
#define CH_WLOW         0x0100          /* Term waiting low event           */
#define CH_WEMPTY       0x0200          /* Term waiting empty event         */
#define CH_RENABLE      0x0400          /* Buffer just emptied          */
#define CH_RACTIVE      0x0800          /* Process active in xxread()   */
#define CH_RWAIT        0x1000          /* Process waiting in xxread()  */
#define CH_BAUD0	0x2000		/* Used for checking B0 transitions */
#define CH_HANGUP       0x8000		/* Hangup received                  */

/*
 * Definitions for ch_sniff_flags
 */
#define SNIFF_OPEN	0x1
#define SNIFF_WAIT_DATA	0x2
#define SNIFF_WAIT_SPACE 0x4

/************************************************************************
 ***	Definitions for Digi ditty(1) command.
 ************************************************************************/

/************************************************************************
 * This module provides application access to special Digi
 * serial line enhancements which are not standard UNIX(tm) features.
 ************************************************************************/

#if !defined(TIOCMODG)

#define	TIOCMODG	(('d'<<8) | 250)	/* get modem ctrl state	*/
#define	TIOCMODS	(('d'<<8) | 251)	/* set modem ctrl state	*/

#ifndef TIOCM_LE
#define		TIOCM_LE	0x01		/* line enable		*/
#define		TIOCM_DTR	0x02		/* data terminal ready	*/
#define		TIOCM_RTS	0x04		/* request to send	*/
#define		TIOCM_ST	0x08		/* secondary transmit	*/
#define		TIOCM_SR	0x10		/* secondary receive	*/
#define		TIOCM_CTS	0x20		/* clear to send	*/
#define		TIOCM_CAR	0x40		/* carrier detect	*/
#define		TIOCM_RNG	0x80		/* ring	indicator	*/
#define		TIOCM_DSR	0x100		/* data set ready	*/
#define		TIOCM_RI	TIOCM_RNG	/* ring (alternate)	*/
#define		TIOCM_CD	TIOCM_CAR	/* carrier detect (alt)	*/
#endif

#endif

#if !defined(TIOCMSET)
#define	TIOCMSET	(('d'<<8) | 252)	/* set modem ctrl state	*/
#define	TIOCMGET	(('d'<<8) | 253)	/* set modem ctrl state	*/
#endif

#if !defined(TIOCMBIC)
#define	TIOCMBIC	(('d'<<8) | 254)	/* set modem ctrl state */
#define	TIOCMBIS	(('d'<<8) | 255)	/* set modem ctrl state */
#endif

#if !defined(TIOCSDTR)
#define	TIOCSDTR	(('e'<<8) | 0)		/* set DTR		*/
#define	TIOCCDTR	(('e'<<8) | 1)		/* clear DTR		*/
#endif

/************************************************************************
 * Ioctl command arguments for DIGI parameters.
 ************************************************************************/
#define DIGI_GETA	(('e'<<8) | 94)		/* Read params		*/

#define DIGI_SETA	(('e'<<8) | 95)		/* Set params		*/
#define DIGI_SETAW	(('e'<<8) | 96)		/* Drain & set params	*/
#define DIGI_SETAF	(('e'<<8) | 97)		/* Drain, flush & set params */

#define DIGI_KME	(('e'<<8) | 98)		/* Read/Write Host	*/
						/* Adapter Memory	*/

#define	DIGI_GETFLOW	(('e'<<8) | 99)		/* Get startc/stopc flow */
						/* control characters    */
#define	DIGI_SETFLOW	(('e'<<8) | 100)	/* Set startc/stopc flow */
						/* control characters	 */
#define	DIGI_GETAFLOW	(('e'<<8) | 101)	/* Get Aux. startc/stopc */
						/* flow control chars    */
#define	DIGI_SETAFLOW	(('e'<<8) | 102)	/* Set Aux. startc/stopc */
						/* flow control chars	 */

#define DIGI_GEDELAY	(('d'<<8) | 246)	/* Get edelay */
#define DIGI_SEDELAY	(('d'<<8) | 247)	/* Set edelay */

struct	digiflow_t {
	unsigned char	startc;			/* flow cntl start char	*/
	unsigned char	stopc;			/* flow cntl stop char	*/
};

#ifdef	FLOW_2200
#define	F2200_GETA	(('e'<<8) | 104)	/* Get 2x36 flow cntl flags */
#define	F2200_SETAW	(('e'<<8) | 105)	/* Set 2x36 flow cntl flags */
#define		F2200_MASK	0x03		/* 2200 flow cntl bit mask  */
#define		FCNTL_2200	0x01		/* 2x36 terminal flow cntl  */
#define		PCNTL_2200	0x02		/* 2x36 printer flow cntl   */
#define	F2200_XON	0xf8
#define	P2200_XON	0xf9
#define	F2200_XOFF	0xfa
#define	P2200_XOFF	0xfb

#define	FXOFF_MASK	0x03			/* 2200 flow status mask    */
#define	RCVD_FXOFF	0x01			/* 2x36 Terminal XOFF rcvd  */
#define	RCVD_PXOFF	0x02			/* 2x36 Printer XOFF rcvd   */
#endif

/************************************************************************
 * Values for digi_flags
 ************************************************************************/
#define DIGI_IXON	0x0001		/* Handle IXON in the FEP	*/
#define DIGI_FAST	0x0002		/* Fast baud rates		*/
#define RTSPACE		0x0004		/* RTS input flow control	*/
#define CTSPACE		0x0008		/* CTS output flow control	*/
#define DSRPACE		0x0010		/* DSR output flow control	*/
#define DCDPACE		0x0020		/* DCD output flow control	*/
#define DTRPACE		0x0040		/* DTR input flow control	*/
#define DIGI_COOK	0x0080		/* Cooked processing done in FEP */
#define DIGI_FORCEDCD	0x0100		/* Force carrier		*/
#define	DIGI_ALTPIN	0x0200		/* Alternate RJ-45 pin config	*/
#define	DIGI_AIXON	0x0400		/* Aux flow control in fep	*/
#define	DIGI_PRINTER	0x0800		/* Hold port open for flow cntrl*/
#define DIGI_PP_INPUT	0x1000		/* Change parallel port to input*/
#define DIGI_DTR_TOGGLE 0x2000		/* Support DTR Toggle		*/
#define	DIGI_422	0x4000		/* for 422/232 selectable panel */
#define DIGI_RTS_TOGGLE	0x8000		/* Support RTS Toggle		*/

/************************************************************************
 * These options are not supported on the comxi.
 ************************************************************************/
#define	DIGI_COMXI	(DIGI_FAST|DIGI_COOK|DSRPACE|DCDPACE|DTRPACE)

#define DIGI_PLEN	28		/* String length		*/
#define	DIGI_TSIZ	10		/* Terminal string len		*/

/************************************************************************
 * Structure used with ioctl commands for DIGI parameters.
 ************************************************************************/
struct digi_t {
	unsigned short	digi_flags;		/* Flags (see above)	*/
	unsigned short	digi_maxcps;		/* Max printer CPS	*/
	unsigned short	digi_maxchar;		/* Max chars in print queue */
	unsigned short	digi_bufsize;		/* Buffer size		*/
	unsigned char	digi_onlen;		/* Length of ON string	*/
	unsigned char	digi_offlen;		/* Length of OFF string	*/
	char		digi_onstr[DIGI_PLEN];	/* Printer on string	*/
	char		digi_offstr[DIGI_PLEN];	/* Printer off string	*/
	char		digi_term[DIGI_TSIZ];	/* terminal string	*/
};

/************************************************************************
 * KME definitions and structures.
 ************************************************************************/
#define	RW_IDLE		0	/* Operation complete			*/
#define	RW_READ		1	/* Read Concentrator Memory		*/
#define	RW_WRITE	2	/* Write Concentrator Memory		*/

struct rw_t {
	unsigned char	rw_req;		/* Request type			*/
	unsigned char	rw_board;	/* Host Adapter board number	*/
	unsigned char	rw_conc;	/* Concentrator number		*/
	unsigned char	rw_reserved;	/* Reserved for expansion	*/
	unsigned long	rw_addr;	/* Address in concentrator	*/
	unsigned short	rw_size;	/* Read/write request length	*/
	unsigned char	rw_data[128];	/* Data to read/write		*/
};

/************************************************************************
 * Structure to get driver status information
 ************************************************************************/
struct digi_dinfo {
	unsigned long	dinfo_nboards;		/* # boards configured	*/
	char		dinfo_reserved[12];	/* for future expansion */
	char		dinfo_version[16];	/* driver version       */
};

#define	DIGI_GETDD	(('d'<<8) | 248)	/* get driver info      */

/************************************************************************
 * Structure used with ioctl commands for per-board information
 *
 * physsize and memsize differ when board has "windowed" memory
 ************************************************************************/
struct digi_info {
	unsigned long	info_bdnum;		/* Board number (0 based)  */
	unsigned long	info_ioport;		/* io port address         */
	unsigned long	info_physaddr;		/* memory address          */
	unsigned long	info_physsize;		/* Size of host mem window */
	unsigned long	info_memsize;		/* Amount of dual-port mem */
						/* on board                */
	unsigned short	info_bdtype;		/* Board type              */
	unsigned short	info_nports;		/* number of ports         */
	char		info_bdstate;		/* board state             */
	char		info_reserved[7];	/* for future expansion    */
};

#define	DIGI_GETBD	(('d'<<8) | 249)	/* get board info          */

struct digi_stat {
	unsigned int	info_chan;		/* Channel number (0 based)  */
	unsigned int	info_brd;		/* Board number (0 based)  */
	unsigned long	info_cflag;		/* cflag for channel       */
	unsigned long	info_iflag;		/* iflag for channel       */
	unsigned long	info_oflag;		/* oflag for channel       */
	unsigned long	info_mstat;		/* mstat for channel       */
	unsigned long	info_tx_data;		/* tx_data for channel       */
	unsigned long	info_rx_data;		/* rx_data for channel       */
	unsigned long	info_hflow;		/* hflow for channel       */
	unsigned long	info_reserved[8];	/* for future expansion    */
};

#define	DIGI_GETSTAT	(('d'<<8) | 244)	/* get board info          */
/************************************************************************
 *
 * Structure used with ioctl commands for per-channel information
 *
 ************************************************************************/
struct digi_ch {
	unsigned long	info_bdnum;		/* Board number (0 based)  */
	unsigned long	info_channel;		/* Channel index number    */
	unsigned long	info_ch_cflag;		/* Channel cflag           */
	unsigned long	info_ch_iflag;		/* Channel iflag           */
	unsigned long	info_ch_oflag;		/* Channel oflag           */
	unsigned long	info_chsize;		/* Channel structure size  */
	unsigned long	info_sleep_stat;	/* sleep status		   */
	dev_t		info_dev;		/* device number	   */
	unsigned char	info_initstate;		/* Channel init state	   */
	unsigned char	info_running;		/* Channel running state   */
	long		reserved[8];		/* reserved for future use */
};

/*
* This structure is used with the DIGI_FEPCMD ioctl to
* tell the driver which port to send the command for.
*/
struct digi_cmd {
	int	cmd;
	int	word;
	int	ncmds;
	int	chan; /* channel index (zero based) */
	int	bdid; /* board index (zero based) */
};

/*
*  info_sleep_stat defines
*/
#define INFO_RUNWAIT	0x0001
#define INFO_WOPEN	0x0002
#define INFO_TTIOW	0x0004
#define INFO_CH_RWAIT	0x0008
#define INFO_CH_WEMPTY	0x0010
#define INFO_CH_WLOW	0x0020
#define INFO_XXBUF_BUSY 0x0040

#define	DIGI_GETCH	(('d'<<8) | 245)	/* get board info          */

/* Board type definitions */

#define	SUBTYPE		0007
#define	T_PCXI		0000
#define T_PCXM		0001
#define T_PCXE		0002
#define T_PCXR		0003
#define T_SP		0004
#define T_SP_PLUS	0005
#	define T_HERC	0000
#	define T_HOU	0001
#	define T_LON	0002
#	define T_CHA	0003
#define FAMILY		0070
#define T_COMXI		0000
#define T_PCXX		0010
#define T_CX		0020
#define T_EPC		0030
#define	T_PCLITE	0040
#define	T_SPXX		0050
#define	T_AVXX		0060
#define T_DXB		0070
#define T_A2K_4_8	0070
#define BUSTYPE		0700
#define T_ISABUS	0000
#define T_MCBUS		0100
#define	T_EISABUS	0200
#define	T_PCIBUS	0400

/* Board State Definitions */

#define	BD_RUNNING	0x0
#define	BD_REASON	0x7f
#define	BD_NOTFOUND	0x1
#define	BD_NOIOPORT	0x2
#define	BD_NOMEM	0x3
#define	BD_NOBIOS	0x4
#define	BD_NOFEP	0x5
#define	BD_FAILED	0x6
#define BD_ALLOCATED	0x7
#define BD_TRIBOOT	0x8
#define	BD_BADKME	0x80

#define DIGI_LOOPBACK	(('d'<<8) | 252)	/* Enable/disable UART  */
						/* internal loopback    */
#define DIGI_SPOLL	(('d'<<8) | 254)	/* change poller rate   */

#define DIGI_SETCUSTOMBAUD _IOW('e', 106, int)	/* Set integer baud rate */
#define DIGI_GETCUSTOMBAUD _IOR('e', 107, int)	/* Get integer baud rate */
#define DIGI_RESET_PORT	   (('e'<<8) | 93)	/* Reset port		 */

/************************************************************************
 * Channel information structure.
 ************************************************************************/
struct channel_t {
	int magic;			/* Channel Magic Number		*/
	struct bs_t __iomem *ch_bs;	/* Base structure pointer       */
	struct cm_t __iomem *ch_cm;	/* Command queue pointer        */
	struct board_t *ch_bd;		/* Board structure pointer      */
	u8 __iomem *ch_vaddr;		/* FEP memory origin            */
	u8 __iomem *ch_taddr;		/* Write buffer origin          */
	u8 __iomem *ch_raddr;		/* Read buffer origin           */
	struct digi_t  ch_digi;		/* Transparent Print structure  */
	struct un_t ch_tun;		/* Terminal unit info           */
	struct un_t ch_pun;		/* Printer unit info            */

	spinlock_t	ch_lock;	/* provide for serialization */
	wait_queue_head_t ch_flags_wait;

	u32	pscan_state;
	u8	pscan_savechar;

	u32 ch_portnum;			/* Port number, 0 offset.	*/
	u32 ch_open_count;		/* open count			*/
	u32	ch_flags;		/* Channel flags                */

	u32	ch_cpstime;		/* Time for CPS calculations    */

	tcflag_t ch_c_iflag;		/* channel iflags               */
	tcflag_t ch_c_cflag;		/* channel cflags               */
	tcflag_t ch_c_oflag;		/* channel oflags               */
	tcflag_t ch_c_lflag;		/* channel lflags               */

	u16  ch_fepiflag;		/* FEP tty iflags               */
	u16  ch_fepcflag;		/* FEP tty cflags               */
	u16  ch_fepoflag;		/* FEP tty oflags               */
	u16  ch_wopen;			/* Waiting for open process cnt */
	u16  ch_tstart;			/* Transmit buffer start        */
	u16  ch_tsize;			/* Transmit buffer size         */
	u16  ch_rstart;			/* Receive buffer start         */
	u16  ch_rsize;			/* Receive buffer size          */
	u16  ch_rdelay;			/* Receive delay time           */

	u16	ch_tlw;			/* Our currently set low water mark */

	u16  ch_cook;			/* Output character mask        */

	u8   ch_card;			/* Card channel is on           */
	u8   ch_stopc;			/* Stop character               */
	u8   ch_startc;			/* Start character              */

	u8   ch_mostat;			/* FEP output modem status      */
	u8   ch_mistat;			/* FEP input modem status       */
	u8   ch_mforce;			/* Modem values to be forced    */
	u8   ch_mval;			/* Force values                 */
	u8   ch_fepstopc;		/* FEP stop character           */
	u8   ch_fepstartc;		/* FEP start character          */

	u8   ch_astopc;			/* Auxiliary Stop character     */
	u8   ch_astartc;		/* Auxiliary Start character    */
	u8   ch_fepastopc;		/* Auxiliary FEP stop char      */
	u8   ch_fepastartc;		/* Auxiliary FEP start char     */

	u8   ch_hflow;			/* FEP hardware handshake       */
	u8   ch_dsr;			/* stores real dsr value        */
	u8   ch_cd;			/* stores real cd value         */
	u8   ch_tx_win;			/* channel tx buffer window     */
	u8   ch_rx_win;			/* channel rx buffer window     */
	uint	ch_custom_speed;	/* Custom baud, if set		*/
	uint	ch_baud_info;		/* Current baud info for /proc output */
	ulong	ch_rxcount;		/* total of data received so far      */
	ulong	ch_txcount;		/* total of data transmitted so far   */
	ulong	ch_err_parity;		/* Count of parity errors on channel  */
	ulong	ch_err_frame;		/* Count of framing errors on channel */
	ulong	ch_err_break;		/* Count of breaks on channel	*/
	ulong	ch_err_overrun;		/* Count of overruns on channel	*/
};

/************************************************************************
 * Command structure definition.
 ************************************************************************/
struct cm_t {
	unsigned short cm_head;		/* Command buffer head offset */
	unsigned short cm_tail;		/* Command buffer tail offset */
	unsigned short cm_start;	/* start offset of buffer     */
	unsigned short cm_max;		/* last offset of buffer      */
};

/************************************************************************
 * Event structure definition.
 ************************************************************************/
struct ev_t {
	unsigned short ev_head;		/* Command buffer head offset */
	unsigned short ev_tail;		/* Command buffer tail offset */
	unsigned short ev_start;	/* start offset of buffer     */
	unsigned short ev_max;		/* last offset of buffer      */
};

/************************************************************************
 * Download buffer structure.
 ************************************************************************/
struct downld_t {
	u8	dl_type;		/* Header                       */
	u8	dl_seq;			/* Download sequence            */
	ushort	dl_srev;		/* Software revision number     */
	ushort	dl_lrev;		/* Low revision number          */
	ushort	dl_hrev;		/* High revision number         */
	ushort	dl_seg;			/* Start segment address        */
	ushort	dl_size;		/* Number of bytes to download  */
	u8	dl_data[1024];		/* Download data                */
};

/************************************************************************
 * Per channel buffer structure
 ************************************************************************
 *              Base Structure Entries Usage Meanings to Host           *
 *                                                                      *
 *        W = read write        R = read only                           *
 *        C = changed by commands only                                  *
 *        U = unknown (may be changed w/o notice)                       *
 ************************************************************************/
struct bs_t {
	unsigned short  tp_jmp;		/* Transmit poll jump	 */
	unsigned short  tc_jmp;		/* Cooked procedure jump */
	unsigned short  ri_jmp;		/* Not currently used	 */
	unsigned short  rp_jmp;		/* Receive poll jump	 */

	unsigned short  tx_seg;		/* W Tx segment	 */
	unsigned short  tx_head;	/* W Tx buffer head offset */
	unsigned short  tx_tail;	/* R Tx buffer tail offset */
	unsigned short  tx_max;		/* W Tx buffer size - 1    */

	unsigned short  rx_seg;		/* W Rx segment	    */
	unsigned short  rx_head;	/* W Rx buffer head offset */
	unsigned short  rx_tail;	/* R Rx buffer tail offset */
	unsigned short  rx_max;		/* W Rx buffer size - 1    */

	unsigned short  tx_lw;		/* W Tx buffer low water mark */
	unsigned short  rx_lw;		/* W Rx buffer low water mark */
	unsigned short  rx_hw;		/* W Rx buffer high water mark*/
	unsigned short  incr;		/* W Increment to next channel*/

	unsigned short  fepdev;		/* U SCC device base address  */
	unsigned short  edelay;		/* W Exception delay          */
	unsigned short  blen;		/* W Break length             */
	unsigned short  btime;		/* U Break complete time      */

	unsigned short  iflag;		/* C UNIX input flags         */
	unsigned short  oflag;		/* C UNIX output flags        */
	unsigned short  cflag;		/* C UNIX control flags       */
	unsigned short  wfill[13];	/* U Reserved for expansion   */

	unsigned char   num;		/* U Channel number           */
	unsigned char   ract;		/* U Receiver active counter  */
	unsigned char   bstat;		/* U Break status bits        */
	unsigned char   tbusy;		/* W Transmit busy            */
	unsigned char   iempty;		/* W Transmit empty event     */
					/* enable                     */
	unsigned char   ilow;		/* W Transmit low-water event */
					/* enable                     */
	unsigned char   idata;		/* W Receive data interrupt   */
					/* enable                     */
	unsigned char   eflag;		/* U Host event flags         */

	unsigned char   tflag;		/* U Transmit flags           */
	unsigned char   rflag;		/* U Receive flags            */
	unsigned char   xmask;		/* U Transmit ready flags     */
	unsigned char   xval;		/* U Transmit ready value     */
	unsigned char   m_stat;		/* RC Modem status bits       */
	unsigned char   m_change;	/* U Modem bits which changed */
	unsigned char   m_int;		/* W Modem interrupt enable   */
					/* bits                       */
	unsigned char   m_last;		/* U Last modem status        */

	unsigned char   mtran;		/* C Unreported modem trans   */
	unsigned char   orun;		/* C Buffer overrun occurred  */
	unsigned char   astartc;	/* W Auxiliary Xon char       */
	unsigned char   astopc;		/* W Auxiliary Xoff char      */
	unsigned char   startc;		/* W Xon character            */
	unsigned char   stopc;		/* W Xoff character           */
	unsigned char   vnextc;		/* W Vnext character          */
	unsigned char   hflow;		/* C Software flow control    */

	unsigned char   fillc;		/* U Delay Fill character     */
	unsigned char   ochar;		/* U Saved output character   */
	unsigned char   omask;		/* U Output character mask    */

	unsigned char   bfill[13];	/* U Reserved for expansion   */

	unsigned char   scc[16];	/* U SCC registers            */
};

struct cnode {
	struct cnode *next;
	int type;
	int numbrd;

	union {
		struct {
			char  type;	/* Board Type           */
			long  addr;	/* Memory Address	*/
			char  *addrstr; /* Memory Address in string */
			long  pcibus;	/* PCI BUS		*/
			char  *pcibusstr; /* PCI BUS in string */
			long  pcislot;	/* PCI SLOT		*/
			char  *pcislotstr; /* PCI SLOT in string */
			long  nport;	/* Number of Ports	*/
			char  *id;	/* tty id		*/
			long  start;	/* start of tty counting */
			char  *method;  /* Install method       */
			char  v_addr;
			char  v_pcibus;
			char  v_pcislot;
			char  v_nport;
			char  v_id;
			char  v_start;
			char  v_method;
			char  line1;
			char  line2;
			char  conc1;   /* total concs in line1 */
			char  conc2;   /* total concs in line2 */
			char  module1; /* total modules for line1 */
			char  module2; /* total modules for line2 */
			char  *status; /* config status */
			char  *dimstatus;	 /* Y/N */
			int   status_index; /* field pointer */
		} board;

		struct {
			char  *cable;
			char  v_cable;
			long  speed;
			char  v_speed;
		} line;

		struct {
			char  type;
			char  *connect;
			long  speed;
			long  nport;
			char  *id;
			char  *idstr;
			long  start;
			char  v_connect;
			char  v_speed;
			char  v_nport;
			char  v_id;
			char  v_start;
		} conc;

		struct {
			char type;
			long nport;
			char *id;
			char *idstr;
			long start;
			char v_nport;
			char v_id;
			char v_start;
		} module;

		char *ttyname;
		char *cuname;
		char *printname;
		long majornumber;
		long altpin;
		long ttysize;
		long chsize;
		long bssize;
		long unsize;
		long f2size;
		long vpixsize;
		long useintr;
	} u;
};
#endif
