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

#include <linux/version.h>	/* To get the current Linux version */
#include <linux/types.h>        /* To pick up the varions Linux types */
#include <linux/tty.h>          /* To pick up the various tty structs/defines */
#include <linux/interrupt.h>    /* For irqreturn_t type */

#include "dgap_types.h"         /* Additional types needed by the Digi header files */
#include "digi.h"               /* Digi specific ioctl header */
#include "dgap_kcompat.h"       /* Kernel 2.4/2.6 compat includes */
#include "dgap_sysfs.h"		/* Support for SYSFS */

/*************************************************************************
 *
 * Driver defines
 *
 *************************************************************************/

/*
 * Driver identification, error and debugging statments
 *
 * In theory, you can change all occurances of "digi" in the next
 * three lines, and the driver printk's will all automagically change.
 *
 * APR((fmt, args, ...));	Always prints message
 * DPR((fmt, args, ...));	Only prints if DGAP_TRACER is defined at
 *				  compile time and dgap_debug!=0
 */
#define	PROCSTR		"dgap"			/* /proc entries	 */
#define	DEVSTR		"/dev/dg/dgap"		/* /dev entries		 */
#define	DRVSTR		"dgap"			/* Driver name string 
						 * displayed by APR	 */
#define	APR(args)	do { PRINTF_TO_KMEM(args); printk(DRVSTR": "); printk args; \
			   } while (0)
#define	RAPR(args)	do { PRINTF_TO_KMEM(args); printk args; } while (0)

#define TRC_TO_CONSOLE 1

/*
 * Debugging levels can be set using debug insmod variable
 * They can also be compiled out completely.
 */

#define	DBG_INIT		(dgap_debug & 0x01)
#define	DBG_BASIC		(dgap_debug & 0x02)
#define	DBG_CORE		(dgap_debug & 0x04)

#define	DBG_OPEN		(dgap_debug & 0x08)
#define	DBG_CLOSE		(dgap_debug & 0x10)
#define	DBG_READ		(dgap_debug & 0x20)
#define	DBG_WRITE		(dgap_debug & 0x40)

#define	DBG_IOCTL		(dgap_debug & 0x80)

#define	DBG_PROC		(dgap_debug & 0x100)
#define	DBG_PARAM		(dgap_debug & 0x200)
#define	DBG_PSCAN		(dgap_debug & 0x400)
#define	DBG_EVENT		(dgap_debug & 0x800)

#define	DBG_DRAIN		(dgap_debug & 0x1000)
#define	DBG_CARR		(dgap_debug & 0x2000)

#define	DBG_MGMT		(dgap_debug & 0x4000)


#if defined(DGAP_TRACER)

# if defined(TRC_TO_KMEM)
/* Choose one: */
#  define TRC_ON_OVERFLOW_WRAP_AROUND
#  undef  TRC_ON_OVERFLOW_SHIFT_BUFFER
# endif //TRC_TO_KMEM

# define TRC_MAXMSG		1024
# define TRC_OVERFLOW		"(OVERFLOW)"
# define TRC_DTRC		"/usr/bin/dtrc"

#if defined TRC_TO_CONSOLE
#define PRINTF_TO_CONSOLE(args) { printk(DRVSTR": "); printk args; }
#else //!defined TRACE_TO_CONSOLE
#define PRINTF_TO_CONSOLE(args)
#endif

#if defined TRC_TO_KMEM
#define PRINTF_TO_KMEM(args) dgap_tracef args 
#else //!defined TRC_TO_KMEM
#define PRINTF_TO_KMEM(args)
#endif

#define	TRC(args)	{ PRINTF_TO_KMEM(args); PRINTF_TO_CONSOLE(args) }

# define DPR_INIT(ARGS)		if (DBG_INIT) TRC(ARGS)
# define DPR_BASIC(ARGS)	if (DBG_BASIC) TRC(ARGS)
# define DPR_CORE(ARGS)		if (DBG_CORE) TRC(ARGS)
# define DPR_OPEN(ARGS)		if (DBG_OPEN)  TRC(ARGS)
# define DPR_CLOSE(ARGS)	if (DBG_CLOSE)  TRC(ARGS)
# define DPR_READ(ARGS)		if (DBG_READ)  TRC(ARGS)
# define DPR_WRITE(ARGS)	if (DBG_WRITE) TRC(ARGS)
# define DPR_IOCTL(ARGS)	if (DBG_IOCTL) TRC(ARGS)
# define DPR_PROC(ARGS)		if (DBG_PROC)  TRC(ARGS)
# define DPR_PARAM(ARGS)	if (DBG_PARAM)  TRC(ARGS)
# define DPR_PSCAN(ARGS)	if (DBG_PSCAN)  TRC(ARGS)
# define DPR_EVENT(ARGS)	if (DBG_EVENT)  TRC(ARGS)
# define DPR_DRAIN(ARGS)	if (DBG_DRAIN)  TRC(ARGS)
# define DPR_CARR(ARGS)		if (DBG_CARR)  TRC(ARGS)
# define DPR_MGMT(ARGS)		if (DBG_MGMT)  TRC(ARGS)

# define DPR(ARGS)		if (dgap_debug) TRC(ARGS)
# define P(X)			dgap_tracef(#X "=%p\n", X)
# define X(X)			dgap_tracef(#X "=%x\n", X)

#else//!defined DGAP_TRACER

#define PRINTF_TO_KMEM(args)
# define TRC(ARGS)
# define DPR_INIT(ARGS)
# define DPR_BASIC(ARGS)
# define DPR_CORE(ARGS)
# define DPR_OPEN(ARGS)
# define DPR_CLOSE(ARGS)
# define DPR_READ(ARGS)
# define DPR_WRITE(ARGS)
# define DPR_IOCTL(ARGS)
# define DPR_PROC(ARGS)
# define DPR_PARAM(ARGS)
# define DPR_PSCAN(ARGS)
# define DPR_EVENT(ARGS)
# define DPR_DRAIN(ARGS)
# define DPR_CARR(ARGS)
# define DPR_MGMT(ARGS)

# define DPR(args)

#endif//DGAP_TRACER

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
 * as the default in tty_io.c with the same settings overriden as in serial.c
 *
 * In short, this should match the internal serial ports' defaults.
 */
#define	DEFAULT_IFLAGS	(ICRNL | IXON)
#define	DEFAULT_OFLAGS	(OPOST | ONLCR)
#define	DEFAULT_CFLAGS	(B9600 | CS8 | CREAD | HUPCL | CLOCAL)
#define	DEFAULT_LFLAGS	(ISIG | ICANON | ECHO | ECHOE | ECHOK | \
			ECHOCTL | ECHOKE | IEXTEN)

#ifndef _POSIX_VDISABLE
#define   _POSIX_VDISABLE '\0'
#endif

#define SNIFF_MAX	65536		/* Sniff buffer size (2^n) */
#define SNIFF_MASK	(SNIFF_MAX - 1)	/* Sniff wrap mask */

#define VPDSIZE (512)

/*
 * Lock function/defines.
 * Makes spotting lock/unlock locations easier.
 */
# define DGAP_SPINLOCK_INIT(x)		spin_lock_init(&(x))
# define DGAP_LOCK(x,y)			spin_lock_irqsave(&(x), y)
# define DGAP_UNLOCK(x,y)		spin_unlock_irqrestore(&(x), y)
# define DGAP_TRYLOCK(x,y)		spin_trylock(&(x))

/*
 * All the possible states the driver can be while being loaded.
 */
enum {
	DRIVER_INITIALIZED = 0,
	DRIVER_NEED_CONFIG_LOAD,
	DRIVER_REQUESTED_CONFIG,
	DRIVER_READY
};

/*
 * All the possible states the board can be while booting up.
 */
enum {
	BOARD_FAILED = 0,
	CONFIG_NOT_FOUND,
	BOARD_FOUND,
	NEED_RESET,
	FINISHED_RESET,
	NEED_CONFIG,
	FINISHED_CONFIG,
	NEED_DEVICE_CREATION,
	REQUESTED_DEVICE_CREATION,
	FINISHED_DEVICE_CREATION,
	NEED_BIOS_LOAD,
	REQUESTED_BIOS,
	WAIT_BIOS_LOAD,
	FINISHED_BIOS_LOAD,
	NEED_FEP_LOAD,
	REQUESTED_FEP,
	WAIT_FEP_LOAD,
	FINISHED_FEP_LOAD,
	NEED_PROC_CREATION,
	FINISHED_PROC_CREATION,
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

extern char *dgap_state_text[];
extern char *dgap_driver_state_text[];


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
struct macounter
{
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
struct board_t
{
	int		magic;		/* Board Magic number.  */
	int		boardnum;	/* Board number: 0-3 */
	int		firstminor;	/* First minor, e.g. 0, 30, 60 */

	int		type;		/* Type of board */
	char		*name;		/* Product Name */
	struct pci_dev	*pdev;		/* Pointer to the pci_dev struct */ 
	u16		vendor;		/* PCI vendor ID */
	u16		device;		/* PCI device ID */
	u16		subvendor;	/* PCI subsystem vendor ID */
	u16		subdevice;	/* PCI subsystem device ID */
	uchar		rev;		/* PCI revision ID */
	uint		pci_bus;	/* PCI bus value */
	uint		pci_slot;	/* PCI slot value */
	u16		maxports;	/* MAX ports this board can handle */
	uchar		vpd[VPDSIZE];	/* VPD of board, if found */
	u32		bd_flags;	/* Board flags */

	spinlock_t	bd_lock;	/* Used to protect board */

	u32		state;		/* State of card. */
	wait_queue_head_t state_wait;	/* Place to sleep on for state change */

	struct		tasklet_struct helper_tasklet; /* Poll helper tasklet */

	u32		wait_for_bios;
	u32		wait_for_fep;

	struct cnode *  bd_config;	/* Config of board */

	u16		nasync;		/* Number of ports on card */

	u32		use_interrupts;	/* Should we be interrupt driven? */
	ulong		irq;		/* Interrupt request number */
	ulong		intr_count;	/* Count of interrupts */
	u32		intr_used;	/* Non-zero if using interrupts */
	u32		intr_running;	/* Non-zero if FEP knows its doing interrupts */

	ulong		port;		/* Start of base io port of the card */
	ulong		port_end;	/* End of base io port of the card */
	ulong		membase;	/* Start of base memory of the card */
	ulong		membase_end;	/* End of base memory of the card */

	uchar 		*re_map_port;	/* Remapped io port of the card */
	uchar		*re_map_membase;/* Remapped memory of the card */

	uchar		runwait;	/* # Processes waiting for FEP  */
	uchar		inhibit_poller; /* Tells  the poller to leave us alone */

	struct channel_t *channels[MAXPORTS]; /* array of pointers to our channels. */

	struct tty_driver	*SerialDriver;
	char		SerialName[200];
	struct tty_driver	*PrintDriver;
	char		PrintName[200];

	u32		dgap_Major_Serial_Registered;
	u32		dgap_Major_TransparentPrint_Registered;

	u32		dgap_Serial_Major;
	u32		dgap_TransparentPrint_Major;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	u32		TtyRefCnt;
#endif

	struct bs_t	*bd_bs;			/* Base structure pointer       */

	char	*flipbuf;		/* Our flip buffer, alloced if board is found */
	char	*flipflagbuf;		/* Our flip flag buffer, alloced if board is found */

	u16		dpatype;	/* The board "type", as defined by DPA */
	u16		dpastatus;	/* The board "status", as defined by DPA */
	wait_queue_head_t kme_wait;	/* Needed for DPA support */

	u32		conc_dl_status;	/* Status of any pending conc download */
	/*
	 *	Mgmt data.
	 */
        char		*msgbuf_head;
        char		*msgbuf;
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
	u32	un_open_count;	/* Counter of opens to port		*/
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
 * Channel information structure.
 ************************************************************************/
struct channel_t {
	int magic;			/* Channel Magic Number		*/
	struct bs_t	*ch_bs;		/* Base structure pointer       */
	struct cm_t	*ch_cm;		/* Command queue pointer        */
	struct board_t *ch_bd;		/* Board structure pointer      */
	unsigned char *ch_vaddr;	/* FEP memory origin            */
	unsigned char *ch_taddr;	/* Write buffer origin          */
	unsigned char *ch_raddr;	/* Read buffer origin           */
	struct digi_t  ch_digi;		/* Transparent Print structure  */
	struct un_t ch_tun;		/* Terminal unit info           */
	struct un_t ch_pun;		/* Printer unit info            */

	spinlock_t	ch_lock;	/* provide for serialization */
	wait_queue_head_t ch_flags_wait;

	u32	pscan_state;
	uchar	pscan_savechar;

	u32 ch_portnum;			/* Port number, 0 offset.	*/
	u32 ch_open_count;		/* open count			*/
	u32	ch_flags;		/* Channel flags                */


	u32	ch_close_delay;		/* How long we should drop RTS/DTR for */

	u32	ch_cpstime;		/* Time for CPS calculations    */

	tcflag_t ch_c_iflag;		/* channel iflags               */
	tcflag_t ch_c_cflag;		/* channel cflags               */
	tcflag_t ch_c_oflag;		/* channel oflags               */
	tcflag_t ch_c_lflag;		/* channel lflags               */

	u16  ch_fepiflag;            /* FEP tty iflags               */
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

	uchar   ch_card;		/* Card channel is on           */
	uchar   ch_stopc;		/* Stop character               */
	uchar   ch_startc;		/* Start character              */

	uchar   ch_mostat;		/* FEP output modem status      */
	uchar   ch_mistat;		/* FEP input modem status       */
	uchar   ch_mforce;		/* Modem values to be forced    */
	uchar   ch_mval;		/* Force values                 */
	uchar   ch_fepstopc;		/* FEP stop character           */
	uchar   ch_fepstartc;		/* FEP start character          */

	uchar   ch_astopc;		/* Auxiliary Stop character     */
	uchar   ch_astartc;		/* Auxiliary Start character    */
	uchar   ch_fepastopc;		/* Auxiliary FEP stop char      */
	uchar   ch_fepastartc;		/* Auxiliary FEP start char     */

	uchar   ch_hflow;		/* FEP hardware handshake       */
	uchar   ch_dsr;			/* stores real dsr value        */
	uchar   ch_cd;			/* stores real cd value         */
	uchar   ch_tx_win;		/* channel tx buffer window     */
	uchar   ch_rx_win;		/* channel rx buffer window     */
	uint	ch_custom_speed;	/* Custom baud, if set		*/
	uint	ch_baud_info;		/* Current baud info for /proc output	*/
	ulong	ch_rxcount;		/* total of data received so far	*/
	ulong	ch_txcount;		/* total of data transmitted so far	*/
	ulong	ch_err_parity;		/* Count of parity errors on channel	*/
	ulong	ch_err_frame;		/* Count of framing errors on channel	*/
	ulong	ch_err_break;		/* Count of breaks on channel	*/
	ulong	ch_err_overrun;		/* Count of overruns on channel	*/

	uint ch_sniff_in;
	uint ch_sniff_out;
	char *ch_sniff_buf;		/* Sniff buffer for proc */
	ulong ch_sniff_flags;		/* Channel flags                */
	wait_queue_head_t ch_sniff_wait;
};


/*************************************************************************
 *
 * Prototypes for non-static functions used in more than one module
 *
 *************************************************************************/

extern int		dgap_ms_sleep(ulong ms);
extern void		*dgap_driver_kzmalloc(size_t size, int priority);
extern char		*dgap_ioctl_name(int cmd);
extern void		dgap_do_bios_load(struct board_t *brd, uchar __user *ubios, int len);
extern void		dgap_do_fep_load(struct board_t *brd, uchar __user *ufep, int len);
extern void		dgap_do_conc_load(struct board_t *brd, uchar *uaddr, int len);
extern void		dgap_do_config_load(uchar __user *uaddr, int len);
extern int		dgap_after_config_loaded(void);
extern int		dgap_finalize_board_init(struct board_t *brd);

/*
 * Our Global Variables.
 */
extern int		dgap_driver_state;	/* The state of the driver	*/
extern int		dgap_debug;		/* Debug variable		*/
extern int		dgap_rawreadok;		/* Set if user wants rawreads	*/
extern int		dgap_poll_tick;		/* Poll interval - 20 ms	*/
extern spinlock_t	dgap_global_lock;	/* Driver global spinlock	*/
extern uint		dgap_NumBoards;		/* Total number of boards	*/
extern struct board_t	*dgap_Board[MAXBOARDS];	/* Array of board structs	*/
extern ulong		dgap_poll_counter;	/* Times the poller has run	*/
extern char		*dgap_config_buf;	/* The config file buffer	*/
extern spinlock_t	dgap_dl_lock;		/* Downloader spinlock		*/
extern wait_queue_head_t dgap_dl_wait;		/* Wait queue for downloader	*/
extern int		dgap_dl_action;		/* Action flag for downloader	*/
extern int		dgap_registerttyswithsysfs; /* Should we register the	*/
						    /* ttys with sysfs or not	*/

/*
 * Global functions declared in dgap_fep5.c, but must be hidden from
 * user space programs.
 */
extern void	dgap_poll_tasklet(unsigned long data); 
extern void	dgap_cmdb(struct channel_t *ch, uchar cmd, uchar byte1, uchar byte2, uint ncmds);
extern void	dgap_cmdw(struct channel_t *ch, uchar cmd, u16 word, uint ncmds);
extern void	dgap_wmove(struct channel_t *ch, char *buf, uint cnt);
extern int	dgap_param(struct tty_struct *tty);
extern void	dgap_parity_scan(struct channel_t *ch, unsigned char *cbuf, unsigned char *fbuf, int *len);
extern uint	dgap_get_custom_baud(struct channel_t *ch);
extern void	dgap_firmware_reset_port(struct channel_t *ch);

#endif
