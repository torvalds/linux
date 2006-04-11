/*
 * Siemens Gigaset 307x driver
 * Common header file for all connection variants
 *
 * Written by Stefan Eilers <Eilers.Stefan@epost.de>
 *        and Hansjoerg Lipp <hjlipp@web.de>
 *
 * =====================================================================
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 * =====================================================================
 */

#ifndef GIGASET_H
#define GIGASET_H

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/isdnif.h>
#include <linux/usb.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ppp_defs.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/list.h>
#include <asm/atomic.h>

#define GIG_VERSION {0,5,0,0}
#define GIG_COMPAT  {0,4,0,0}

#define MAX_REC_PARAMS 10	/* Max. number of params in response string */
#define MAX_RESP_SIZE 512	/* Max. size of a response string */
#define HW_HDR_LEN 2		/* Header size used to store ack info */

#define MAX_EVENTS 64		/* size of event queue */

#define RBUFSIZE 8192
#define SBUFSIZE 4096		/* sk_buff payload size */

#define TRANSBUFSIZE 768	/* bytes per skb for transparent receive */
#define MAX_BUF_SIZE (SBUFSIZE - 2)	/* Max. size of a data packet from LL */

/* compile time options */
#define GIG_MAJOR 0

#define GIG_MAYINITONDIAL
#define GIG_RETRYCID
#define GIG_X75

#define MAX_TIMER_INDEX 1000
#define MAX_SEQ_INDEX   1000

#define GIG_TICK (HZ / 10)

/* timeout values (unit: 1 sec) */
#define INIT_TIMEOUT 1

/* timeout values (unit: 0.1 sec) */
#define RING_TIMEOUT 3		/* for additional parameters to RING */
#define BAS_TIMEOUT 20		/* for response to Base USB ops */
#define ATRDY_TIMEOUT 3		/* for HD_READY_SEND_ATDATA */

#define BAS_RETRY 3		/* max. retries for base USB ops */

#define MAXACT 3

#define IFNULL(a) \
	if (unlikely(!(a)))

#define IFNULLRET(a) \
	if (unlikely(!(a))) { \
		err("%s==NULL at %s:%d!", #a, __FILE__, __LINE__); \
		return; \
	}

#define IFNULLRETVAL(a,b) \
	if (unlikely(!(a))) { \
		err("%s==NULL at %s:%d!", #a, __FILE__, __LINE__); \
		return (b); \
	}

#define IFNULLCONT(a) \
	if (unlikely(!(a))) { \
		err("%s==NULL at %s:%d!", #a, __FILE__, __LINE__); \
		continue; \
	}

#define IFNULLGOTO(a,b) \
	if (unlikely(!(a))) { \
		err("%s==NULL at %s:%d!", #a, __FILE__, __LINE__); \
		goto b; \
	}

extern int gigaset_debuglevel;	/* "needs" cast to (enum debuglevel) */

/* any combination of these can be given with the 'debug=' parameter to insmod,
 * e.g. 'insmod usb_gigaset.o debug=0x2c' will set DEBUG_OPEN, DEBUG_CMD and
 * DEBUG_INTR.
 */
enum debuglevel { /* up to 24 bits (atomic_t) */
	DEBUG_REG	  = 0x0002,/* serial port I/O register operations */
	DEBUG_OPEN	  = 0x0004, /* open/close serial port */
	DEBUG_INTR	  = 0x0008, /* interrupt processing */
	DEBUG_INTR_DUMP   = 0x0010, /* Activating hexdump debug output on
				       interrupt requests, not available as
				       run-time option */
	DEBUG_CMD	  = 0x00020, /* sent/received LL commands */
	DEBUG_STREAM	  = 0x00040, /* application data stream I/O events */
	DEBUG_STREAM_DUMP = 0x00080, /* application data stream content */
	DEBUG_LLDATA	  = 0x00100, /* sent/received LL data */
	DEBUG_INTR_0	  = 0x00200, /* serial port interrupt processing */
	DEBUG_DRIVER	  = 0x00400, /* driver structure */
	DEBUG_HDLC	  = 0x00800, /* M10x HDLC processing */
	DEBUG_WRITE	  = 0x01000, /* M105 data write */
	DEBUG_TRANSCMD    = 0x02000, /* AT-COMMANDS+RESPONSES */
	DEBUG_MCMD        = 0x04000, /* COMMANDS THAT ARE SENT VERY OFTEN */
	DEBUG_INIT	  = 0x08000, /* (de)allocation+initialization of data
					structures */
	DEBUG_LOCK	  = 0x10000, /* semaphore operations */
	DEBUG_OUTPUT	  = 0x20000, /* output to device */
	DEBUG_ISO         = 0x40000, /* isochronous transfers */
	DEBUG_IF	  = 0x80000, /* character device operations */
	DEBUG_USBREQ	  = 0x100000, /* USB communication (except payload
					 data) */
	DEBUG_LOCKCMD     = 0x200000, /* AT commands and responses when
					 MS_LOCKED */

	DEBUG_ANY	  = 0x3fffff, /* print message if any of the others is
					 activated */
};

#ifdef CONFIG_GIGASET_DEBUG
#define DEBUG_DEFAULT (DEBUG_INIT | DEBUG_TRANSCMD | DEBUG_CMD | DEBUG_USBREQ)
#else
#define DEBUG_DEFAULT 0
#endif

/* redefine syslog macros to prepend module name instead of entire
 * source path */
#undef info
#define info(format, arg...) \
	printk(KERN_INFO "%s: " format "\n", \
	       THIS_MODULE ? THIS_MODULE->name : "gigaset_hw" , ## arg)

#undef notice
#define notice(format, arg...) \
	printk(KERN_NOTICE "%s: " format "\n", \
	       THIS_MODULE ? THIS_MODULE->name : "gigaset_hw" , ## arg)

#undef warn
#define warn(format, arg...) \
	printk(KERN_WARNING "%s: " format "\n", \
	       THIS_MODULE ? THIS_MODULE->name : "gigaset_hw" , ## arg)

#undef err
#define err(format, arg...) \
	printk(KERN_ERR "%s: " format "\n", \
	       THIS_MODULE ? THIS_MODULE->name : "gigaset_hw" , ## arg)

#undef dbg
#ifdef CONFIG_GIGASET_DEBUG
#define dbg(level, format, arg...) \
	do { \
		if (unlikely(((enum debuglevel)gigaset_debuglevel) & (level))) \
			printk(KERN_DEBUG "%s: " format "\n", \
			       THIS_MODULE ? THIS_MODULE->name : "gigaset_hw" \
			       , ## arg); \
	} while (0)
#else
#define dbg(level, format, arg...) do {} while (0)
#endif

void gigaset_dbg_buffer(enum debuglevel level, const unsigned char *msg,
                        size_t len, const unsigned char *buf, int from_user);

/* connection state */
#define ZSAU_NONE			0
#define ZSAU_DISCONNECT_IND		4
#define ZSAU_OUTGOING_CALL_PROCEEDING	1
#define ZSAU_PROCEEDING			1
#define ZSAU_CALL_DELIVERED		2
#define ZSAU_ACTIVE			3
#define ZSAU_NULL			5
#define ZSAU_DISCONNECT_REQ		6
#define ZSAU_UNKNOWN			-1

/* USB control transfer requests */
#define OUT_VENDOR_REQ	(USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT)
#define IN_VENDOR_REQ	(USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT)

/* int-in-events 3070 */
#define HD_B1_FLOW_CONTROL		0x80
#define HD_B2_FLOW_CONTROL		0x81
#define HD_RECEIVEATDATA_ACK		(0x35)		// 3070
						// att: HD_RECEIVE>>AT<<DATA_ACK
#define HD_READY_SEND_ATDATA		(0x36)		// 3070
#define HD_OPEN_ATCHANNEL_ACK		(0x37)		// 3070
#define HD_CLOSE_ATCHANNEL_ACK		(0x38)		// 3070
#define HD_DEVICE_INIT_OK		(0x11)		// ISurf USB + 3070
#define HD_OPEN_B1CHANNEL_ACK		(0x51)		// ISurf USB + 3070
#define HD_OPEN_B2CHANNEL_ACK		(0x52)		// ISurf USB + 3070
#define HD_CLOSE_B1CHANNEL_ACK		(0x53)		// ISurf USB + 3070
#define HD_CLOSE_B2CHANNEL_ACK		(0x54)		// ISurf USB + 3070
// 	 Powermangment
#define HD_SUSPEND_END			(0x61)		// ISurf USB
//   Configuration
#define HD_RESET_INTERRUPT_PIPE_ACK	(0xFF)		// ISurf USB + 3070

/* control requests 3070 */
#define	HD_OPEN_B1CHANNEL		(0x23)		// ISurf USB + 3070
#define	HD_CLOSE_B1CHANNEL		(0x24)		// ISurf USB + 3070
#define	HD_OPEN_B2CHANNEL		(0x25)		// ISurf USB + 3070
#define	HD_CLOSE_B2CHANNEL		(0x26)		// ISurf USB + 3070
#define HD_RESET_INTERRUPT_PIPE		(0x27)		// ISurf USB + 3070
#define	HD_DEVICE_INIT_ACK		(0x34)		// ISurf USB + 3070
#define	HD_WRITE_ATMESSAGE		(0x12)		// 3070
#define	HD_READ_ATMESSAGE		(0x13)		// 3070
#define	HD_OPEN_ATCHANNEL		(0x28)		// 3070
#define	HD_CLOSE_ATCHANNEL		(0x29)		// 3070

/* USB frames for isochronous transfer */
#define BAS_FRAMETIME	1	/* number of milliseconds between frames */
#define BAS_NUMFRAMES	8	/* number of frames per URB */
#define BAS_MAXFRAME	16	/* allocated bytes per frame */
#define BAS_NORMFRAME	8	/* send size without flow control */
#define BAS_HIGHFRAME	10	/* "    "    with positive flow control */
#define BAS_LOWFRAME	5	/* "    "    with negative flow control */
#define BAS_CORRFRAMES	4	/* flow control multiplicator */

#define BAS_INBUFSIZE	(BAS_MAXFRAME * BAS_NUMFRAMES)
					/* size of isoc in buf per URB */
#define BAS_OUTBUFSIZE	4096		/* size of common isoc out buffer */
#define BAS_OUTBUFPAD	BAS_MAXFRAME	/* size of pad area for isoc out buf */

#define BAS_INURBS	3
#define BAS_OUTURBS	3

/* variable commands in struct bc_state */
#define AT_ISO		0
#define AT_DIAL		1
#define AT_MSN		2
#define AT_BC		3
#define AT_PROTO	4
#define AT_TYPE		5
#define AT_HLC		6
#define AT_NUM		7

/* variables in struct at_state_t */
#define VAR_ZSAU	0
#define VAR_ZDLE	1
#define VAR_ZVLS	2
#define VAR_ZCTP	3
#define VAR_NUM		4

#define STR_NMBR	0
#define STR_ZCPN	1
#define STR_ZCON	2
#define STR_ZBC		3
#define STR_ZHLC	4
#define STR_NUM		5

#define EV_TIMEOUT	-105
#define EV_IF_VER	-106
#define EV_PROC_CIDMODE	-107
#define EV_SHUTDOWN	-108
#define EV_START	-110
#define EV_STOP		-111
#define EV_IF_LOCK	-112
#define EV_PROTO_L2	-113
#define EV_ACCEPT	-114
#define EV_DIAL		-115
#define EV_HUP		-116
#define EV_BC_OPEN	-117
#define EV_BC_CLOSED	-118

/* input state */
#define INS_command	0x0001
#define INS_DLE_char	0x0002
#define INS_byte_stuff	0x0004
#define INS_have_data	0x0008
#define INS_skip_frame	0x0010
#define INS_DLE_command	0x0020
#define INS_flag_hunt	0x0040

/* channel state */
#define CHS_D_UP	0x01
#define CHS_B_UP	0x02
#define CHS_NOTIFY_LL	0x04

#define ICALL_REJECT	0
#define ICALL_ACCEPT	1
#define ICALL_IGNORE	2

/* device state */
#define MS_UNINITIALIZED	0
#define MS_INIT			1
#define MS_LOCKED		2
#define MS_SHUTDOWN		3
#define MS_RECOVER		4
#define MS_READY		5

/* mode */
#define M_UNKNOWN	0
#define M_CONFIG	1
#define M_UNIMODEM	2
#define M_CID		3

/* start mode */
#define SM_LOCKED	0
#define SM_ISDN		1 /* default */

struct gigaset_ops;
struct gigaset_driver;

struct usb_cardstate;
struct ser_cardstate;
struct bas_cardstate;

struct bc_state;
struct usb_bc_state;
struct ser_bc_state;
struct bas_bc_state;

struct reply_t {
	int	resp_code;	/* RSP_XXXX */
	int	min_ConState;	/* <0 => ignore */
	int	max_ConState;	/* <0 => ignore */
	int	parameter;	/* e.g. ZSAU_XXXX <0: ignore*/
	int	new_ConState;	/* <0 => ignore */
	int	timeout;	/* >0 => *HZ; <=0 => TOUT_XXXX*/
	int	action[MAXACT];	/* ACT_XXXX */
	char	*command;	/* NULL==none */
};

extern struct reply_t gigaset_tab_cid_m10x[];
extern struct reply_t gigaset_tab_nocid_m10x[];

struct inbuf_t {
	unsigned char		*rcvbuf;	/* usb-gigaset receive buffer */
	struct bc_state		*bcs;
	struct cardstate	*cs;
	int			inputstate;
	atomic_t		head, tail;
	unsigned char		data[RBUFSIZE];
};

/* isochronous write buffer structure
 * circular buffer with pad area for extraction of complete USB frames
 * - data[read..nextread-1] is valid data already submitted to the USB subsystem
 * - data[nextread..write-1] is valid data yet to be sent
 * - data[write] is the next byte to write to
 *   - in byte-oriented L2 procotols, it is completely free
 *   - in bit-oriented L2 procotols, it may contain a partial byte of valid data
 * - data[write+1..read-1] is free
 * - wbits is the number of valid data bits in data[write], starting at the LSB
 * - writesem is the semaphore for writing to the buffer:
 *   if writesem <= 0, data[write..read-1] is currently being written to
 * - idle contains the byte value to repeat when the end of valid data is
 *   reached; if nextread==write (buffer contains no data to send), either the
 *   BAS_OUTBUFPAD bytes immediately before data[write] (if
 *   write>=BAS_OUTBUFPAD) or those of the pad area (if write<BAS_OUTBUFPAD)
 *   are also filled with that value
 * - optionally, the following statistics on the buffer's usage can be
 *   collected:
 *   maxfill:    maximum number of bytes occupied
 *   idlefills:  number of times a frame of idle bytes is prepared
 *   emptygets:  number of times the buffer was empty when a data frame was
 *               requested
 *   backtoback: number of times two data packets were entered into the buffer
 *               without intervening idle flags
 *   nakedback:  set if no idle flags have been inserted since the last data
 *               packet
 */
struct isowbuf_t {
	atomic_t	read;
	atomic_t	nextread;
	atomic_t	write;
	atomic_t	writesem;
	int		wbits;
	unsigned char	data[BAS_OUTBUFSIZE + BAS_OUTBUFPAD];
	unsigned char	idle;
};

/* isochronous write URB context structure
 * data to be stored along with the URB and retrieved when it is returned
 * as completed by the USB subsystem
 * - urb: pointer to the URB itself
 * - bcs: pointer to the B Channel control structure
 * - limit: end of write buffer area covered by this URB
 */
struct isow_urbctx_t {
	struct urb *urb;
	struct bc_state *bcs;
	int limit;
};

/* AT state structure
 * data associated with the state of an ISDN connection, whether or not
 * it is currently assigned a B channel
 */
struct at_state_t {
	struct list_head	list;
	int			waiting;
	int			getstring;
	atomic_t		timer_index;
	unsigned long		timer_expires;
	int			timer_active;
	unsigned int		ConState;	/* State of connection */
	struct reply_t		*replystruct;
	int			cid;
	int			int_var[VAR_NUM];	/* see VAR_XXXX */
	char			*str_var[STR_NUM];	/* see STR_XXXX */
	unsigned		pending_commands;	/* see PC_XXXX */
	atomic_t		seq_index;

	struct cardstate	*cs;
	struct bc_state		*bcs;
};

struct resp_type_t {
	unsigned char	*response;
	int		resp_code;	/* RSP_XXXX */
	int		type;		/* RT_XXXX */
};

struct event_t {
	int type;
	void *ptr, *arg;
	int parameter;
	int cid;
	struct at_state_t *at_state;
};

/* This buffer holds all information about the used B-Channel */
struct bc_state {
	struct sk_buff *tx_skb;		/* Current transfer buffer to modem */
	struct sk_buff_head squeue;	/* B-Channel send Queue */

	/* Variables for debugging .. */
	int corrupted;			/* Counter for corrupted packages */
	int trans_down;			/* Counter of packages (downstream) */
	int trans_up;			/* Counter of packages (upstream) */

	struct at_state_t at_state;
	unsigned long rcvbytes;

	__u16 fcs;
	struct sk_buff *skb;
	int inputstate;			/* see INS_XXXX */

	int channel;

	struct cardstate *cs;

	unsigned chstate;		/* bitmap (CHS_*) */
	int ignore;
	unsigned proto2;		/* Layer 2 protocol (ISDN_PROTO_L2_*) */
	char *commands[AT_NUM];		/* see AT_XXXX */

#ifdef CONFIG_GIGASET_DEBUG
	int emptycount;
#endif
	int busy;
	int use_count;

	/* hardware drivers */
	union {
		struct ser_bc_state *ser;	/* serial hardware driver */
		struct usb_bc_state *usb;	/* usb hardware driver (m105) */
		struct bas_bc_state *bas;	/* usb hardware driver (base) */
	} hw;
};

struct cardstate {
	struct gigaset_driver *driver;
	unsigned minor_index;

	const struct gigaset_ops *ops;

	/* Stuff to handle communication */
	wait_queue_head_t waitqueue;
	int waiting;
	atomic_t mode;			/* see M_XXXX */
	atomic_t mstate;		/* Modem state: see MS_XXXX */
					/* only changed by the event layer */
	int cmd_result;

	int channels;
	struct bc_state *bcs;		/* Array of struct bc_state */

	int onechannel;			/* data and commands transmitted in one
					   stream (M10x) */

	spinlock_t lock;
	struct at_state_t at_state;	/* at_state_t for cid == 0 */
	struct list_head temp_at_states;/* list of temporary "struct
					   at_state_t"s without B channel */

	struct inbuf_t *inbuf;

	struct cmdbuf_t *cmdbuf, *lastcmdbuf;
	spinlock_t cmdlock;
	unsigned curlen, cmdbytes;

	unsigned open_count;
	struct tty_struct *tty;
	struct tasklet_struct if_wake_tasklet;
	unsigned control_state;

	unsigned fwver[4];
	int gotfwver;

	atomic_t running;		/* !=0 if events are handled */
	atomic_t connected;		/* !=0 if hardware is connected */

	atomic_t cidmode;

	int myid;			/* id for communication with LL */
	isdn_if iif;

	struct reply_t *tabnocid;
	struct reply_t *tabcid;
	int cs_init;
	int ignoreframes;		/* frames to ignore after setting up the
					   B channel */
	struct semaphore sem;		/* locks this structure: */
					/*   connected is not changed, */
					/*   hardware_up is not changed, */
					/*   MState is not changed to or from
					     MS_LOCKED */

	struct timer_list timer;
	int retry_count;
	int dle;			/* !=0 if modem commands/responses are
					   dle encoded */
	int cur_at_seq;			/* sequence of AT commands being
					   processed */
	int curchannel;			/* channel, those commands are meant
					   for */
	atomic_t commands_pending;	/* flag(s) in xxx.commands_pending have
					   been set */
	struct tasklet_struct event_tasklet;
					/* tasklet for serializing AT commands.
					 * Scheduled
					 *   -> for modem reponses (and
					 *      incomming data for M10x)
					 *   -> on timeout
					 *   -> after setting bits in
					 *      xxx.at_state.pending_command
					 *      (e.g. command from LL) */
	struct tasklet_struct write_tasklet;
					/* tasklet for serial output
					 * (not used in base driver) */

	/* event queue */
	struct event_t events[MAX_EVENTS];
	atomic_t ev_tail, ev_head;
	spinlock_t ev_lock;

	/* current modem response */
	unsigned char respdata[MAX_RESP_SIZE];
	unsigned cbytes;

	/* hardware drivers */
	union {
		struct usb_cardstate *usb; /* USB hardware driver (m105) */
		struct ser_cardstate *ser; /* serial hardware driver */
		struct bas_cardstate *bas; /* USB hardware driver (base) */
	} hw;
};

struct gigaset_driver {
	struct list_head list;
	spinlock_t lock;		/* locks minor tables and blocked */
	struct tty_driver *tty;
	unsigned have_tty;
	unsigned minor;
	unsigned minors;
	struct cardstate *cs;
	unsigned *flags;
	int blocked;

	const struct gigaset_ops *ops;
	struct module *owner;
};

struct cmdbuf_t {
	struct cmdbuf_t *next, *prev;
	int len, offset;
	struct tasklet_struct *wake_tasklet;
	unsigned char buf[0];
};

struct bas_bc_state {
	/* isochronous output state */
	atomic_t	running;
	atomic_t	corrbytes;
	spinlock_t	isooutlock;
	struct isow_urbctx_t	isoouturbs[BAS_OUTURBS];
	struct isow_urbctx_t	*isooutdone, *isooutfree, *isooutovfl;
	struct isowbuf_t	*isooutbuf;
	unsigned numsub;			/* submitted URB counter (for
						   diagnostic messages only) */
	struct tasklet_struct	sent_tasklet;

	/* isochronous input state */
	spinlock_t isoinlock;
	struct urb *isoinurbs[BAS_INURBS];
	unsigned char isoinbuf[BAS_INBUFSIZE * BAS_INURBS];
	struct urb *isoindone;	                /* completed isoc read URB */
	int loststatus;				/* status of dropped URB */
	unsigned isoinlost;			/* number of bytes lost */
	/* state of bit unstuffing algorithm (in addition to
	   BC_state.inputstate) */
	unsigned seqlen;			/* number of '1' bits not yet
						   unstuffed */
	unsigned inbyte, inbits;		/* collected bits for next byte
						*/
	/* statistics */
	unsigned goodbytes;			/* bytes correctly received */
	unsigned alignerrs;			/* frames with incomplete byte
						   at end */
	unsigned fcserrs;			/* FCS errors */
	unsigned frameerrs;			/* framing errors */
	unsigned giants;			/* long frames */
	unsigned runts;				/* short frames */
	unsigned aborts;			/* HDLC aborts */
	unsigned shared0s;			/* '0' bits shared between flags
						*/
	unsigned stolen0s;			/* '0' stuff bits also serving
						   as leading flag bits */
	struct tasklet_struct rcvd_tasklet;
};

struct gigaset_ops {
	/* Called from ev-layer.c/interface.c for sending AT commands to the
	   device */
	int (*write_cmd)(struct cardstate *cs,
	                 const unsigned char *buf, int len,
	                 struct tasklet_struct *wake_tasklet);

	/* Called from interface.c for additional device control */
	int (*write_room)(struct cardstate *cs);
	int (*chars_in_buffer)(struct cardstate *cs);
	int (*brkchars)(struct cardstate *cs, const unsigned char buf[6]);

	/* Called from ev-layer.c after setting up connection
	 * Should call gigaset_bchannel_up(), when finished. */
	int (*init_bchannel)(struct bc_state *bcs);

	/* Called from ev-layer.c after hanging up
	 * Should call gigaset_bchannel_down(), when finished. */
	int (*close_bchannel)(struct bc_state *bcs);

	/* Called by gigaset_initcs() for setting up bcs->hw.xxx */
	int (*initbcshw)(struct bc_state *bcs);

	/* Called by gigaset_freecs() for freeing bcs->hw.xxx */
	int (*freebcshw)(struct bc_state *bcs);

	/* Called by gigaset_stop() or gigaset_bchannel_down() for resetting
	   bcs->hw.xxx */
	void (*reinitbcshw)(struct bc_state *bcs);

	/* Called by gigaset_initcs() for setting up cs->hw.xxx */
	int (*initcshw)(struct cardstate *cs);

	/* Called by gigaset_freecs() for freeing cs->hw.xxx */
	void (*freecshw)(struct cardstate *cs);

	/* Called from common.c/interface.c for additional serial port
	   control */
	int (*set_modem_ctrl)(struct cardstate *cs, unsigned old_state,
			      unsigned new_state);
	int (*baud_rate)(struct cardstate *cs, unsigned cflag);
	int (*set_line_ctrl)(struct cardstate *cs, unsigned cflag);

	/* Called from i4l.c to put an skb into the send-queue. */
	int (*send_skb)(struct bc_state *bcs, struct sk_buff *skb);

	/* Called from ev-layer.c to process a block of data
	 * received through the common/control channel. */
	void (*handle_input)(struct inbuf_t *inbuf);

};

/* = Common structures and definitions ======================================= */

/* Parser states for DLE-Event:
 * <DLE-EVENT>: <DLE_FLAG> "X" <EVENT> <DLE_FLAG> "."
 * <DLE_FLAG>:  0x10
 * <EVENT>:     ((a-z)* | (A-Z)* | (0-10)*)+
 */
#define DLE_FLAG       0x10

/* ===========================================================================
 *  Functions implemented in asyncdata.c
 */

/* Called from i4l.c to put an skb into the send-queue.
 * After sending gigaset_skb_sent() should be called. */
int gigaset_m10x_send_skb(struct bc_state *bcs, struct sk_buff *skb);

/* Called from ev-layer.c to process a block of data
 * received through the common/control channel. */
void gigaset_m10x_input(struct inbuf_t *inbuf);

/* ===========================================================================
 *  Functions implemented in isocdata.c
 */

/* Called from i4l.c to put an skb into the send-queue.
 * After sending gigaset_skb_sent() should be called. */
int gigaset_isoc_send_skb(struct bc_state *bcs, struct sk_buff *skb);

/* Called from ev-layer.c to process a block of data
 * received through the common/control channel. */
void gigaset_isoc_input(struct inbuf_t *inbuf);

/* Called from bas-gigaset.c to process a block of data
 * received through the isochronous channel */
void gigaset_isoc_receive(unsigned char *src, unsigned count,
			  struct bc_state *bcs);

/* Called from bas-gigaset.c to put a block of data
 * into the isochronous output buffer */
int gigaset_isoc_buildframe(struct bc_state *bcs, unsigned char *in, int len);

/* Called from bas-gigaset.c to initialize the isochronous output buffer */
void gigaset_isowbuf_init(struct isowbuf_t *iwb, unsigned char idle);

/* Called from bas-gigaset.c to retrieve a block of bytes for sending */
int gigaset_isowbuf_getbytes(struct isowbuf_t *iwb, int size);

/* ===========================================================================
 *  Functions implemented in i4l.c/gigaset.h
 */

/* Called by gigaset_initcs() for setting up with the isdn4linux subsystem */
int gigaset_register_to_LL(struct cardstate *cs, const char *isdnid);

/* Called from xxx-gigaset.c to indicate completion of sending an skb */
void gigaset_skb_sent(struct bc_state *bcs, struct sk_buff *skb);

/* Called from common.c/ev-layer.c to indicate events relevant to the LL */
int gigaset_isdn_icall(struct at_state_t *at_state);
int gigaset_isdn_setup_accept(struct at_state_t *at_state);
int gigaset_isdn_setup_dial(struct at_state_t *at_state, void *data);

void gigaset_i4l_cmd(struct cardstate *cs, int cmd);
void gigaset_i4l_channel_cmd(struct bc_state *bcs, int cmd);


static inline void gigaset_isdn_rcv_err(struct bc_state *bcs)
{
	isdn_ctrl response;

	/* error -> LL */
	dbg(DEBUG_CMD, "sending L1ERR");
	response.driver = bcs->cs->myid;
	response.command = ISDN_STAT_L1ERR;
	response.arg = bcs->channel;
	response.parm.errcode = ISDN_STAT_L1ERR_RECV;
	bcs->cs->iif.statcallb(&response);
}

/* ===========================================================================
 *  Functions implemented in ev-layer.c
 */

/* tasklet called from common.c to process queued events */
void gigaset_handle_event(unsigned long data);

/* called from isocdata.c / asyncdata.c
 * when a complete modem response line has been received */
void gigaset_handle_modem_response(struct cardstate *cs);

/* ===========================================================================
 *  Functions implemented in proc.c
 */

/* initialize sysfs for device */
void gigaset_init_dev_sysfs(struct usb_interface *interface);
void gigaset_free_dev_sysfs(struct usb_interface *interface);

/* ===========================================================================
 *  Functions implemented in common.c/gigaset.h
 */

void gigaset_bcs_reinit(struct bc_state *bcs);
void gigaset_at_init(struct at_state_t *at_state, struct bc_state *bcs,
                     struct cardstate *cs, int cid);
int gigaset_get_channel(struct bc_state *bcs);
void gigaset_free_channel(struct bc_state *bcs);
int gigaset_get_channels(struct cardstate *cs);
void gigaset_free_channels(struct cardstate *cs);
void gigaset_block_channels(struct cardstate *cs);

/* Allocate and initialize driver structure. */
struct gigaset_driver *gigaset_initdriver(unsigned minor, unsigned minors,
                                          const char *procname,
                                          const char *devname,
                                          const char *devfsname,
                                          const struct gigaset_ops *ops,
                                          struct module *owner);

/* Deallocate driver structure. */
void gigaset_freedriver(struct gigaset_driver *drv);
void gigaset_debugdrivers(void);
struct cardstate *gigaset_get_cs_by_minor(unsigned minor);
struct cardstate *gigaset_get_cs_by_tty(struct tty_struct *tty);
struct cardstate *gigaset_get_cs_by_id(int id);

/* For drivers without fixed assignment device<->cardstate (usb) */
struct cardstate *gigaset_getunassignedcs(struct gigaset_driver *drv);
void gigaset_unassign(struct cardstate *cs);
void gigaset_blockdriver(struct gigaset_driver *drv);

/* Allocate and initialize card state. Calls hardware dependent
   gigaset_init[b]cs(). */
struct cardstate *gigaset_initcs(struct gigaset_driver *drv, int channels,
				 int onechannel, int ignoreframes,
				 int cidmode, const char *modulename);

/* Free card state. Calls hardware dependent gigaset_free[b]cs(). */
void gigaset_freecs(struct cardstate *cs);

/* Tell common.c that hardware and driver are ready. */
int gigaset_start(struct cardstate *cs);

/* Tell common.c that the device is not present any more. */
void gigaset_stop(struct cardstate *cs);

/* Tell common.c that the driver is being unloaded. */
void gigaset_shutdown(struct cardstate *cs);

/* Tell common.c that an skb has been sent. */
void gigaset_skb_sent(struct bc_state *bcs, struct sk_buff *skb);

/* Append event to the queue.
 * Returns NULL on failure or a pointer to the event on success.
 * ptr must be kmalloc()ed (and not be freed by the caller).
 */
struct event_t *gigaset_add_event(struct cardstate *cs,
                                  struct at_state_t *at_state, int type,
                                  void *ptr, int parameter, void *arg);

/* Called on CONFIG1 command from frontend. */
int gigaset_enterconfigmode(struct cardstate *cs); //0: success <0: errorcode

/* cs->lock must not be locked */
static inline void gigaset_schedule_event(struct cardstate *cs)
{
	unsigned long flags;
	spin_lock_irqsave(&cs->lock, flags);
	if (atomic_read(&cs->running))
		tasklet_schedule(&cs->event_tasklet);
	spin_unlock_irqrestore(&cs->lock, flags);
}

/* Tell common.c that B channel has been closed. */
/* cs->lock must not be locked */
static inline void gigaset_bchannel_down(struct bc_state *bcs)
{
	gigaset_add_event(bcs->cs, &bcs->at_state, EV_BC_CLOSED, NULL, 0, NULL);

	dbg(DEBUG_CMD, "scheduling BC_CLOSED");
	gigaset_schedule_event(bcs->cs);
}

/* Tell common.c that B channel has been opened. */
/* cs->lock must not be locked */
static inline void gigaset_bchannel_up(struct bc_state *bcs)
{
	gigaset_add_event(bcs->cs, &bcs->at_state, EV_BC_OPEN, NULL, 0, NULL);

	dbg(DEBUG_CMD, "scheduling BC_OPEN");
	gigaset_schedule_event(bcs->cs);
}

/* handling routines for sk_buff */
/* ============================= */

/* private version of __skb_put()
 * append 'len' bytes to the content of 'skb', already knowing that the
 * existing buffer can accomodate them
 * returns a pointer to the location where the new bytes should be copied to
 * This function does not take any locks so it must be called with the
 * appropriate locks held only.
 */
static inline unsigned char *gigaset_skb_put_quick(struct sk_buff *skb,
                                                   unsigned int len)
{
	unsigned char *tmp = skb->tail;
	/*SKB_LINEAR_ASSERT(skb);*/		/* not needed here */
	skb->tail += len;
	skb->len += len;
	return tmp;
}

/* pass received skb to LL
 * Warning: skb must not be accessed anymore!
 */
static inline void gigaset_rcv_skb(struct sk_buff *skb,
                                   struct cardstate *cs,
                                   struct bc_state *bcs)
{
	cs->iif.rcvcallb_skb(cs->myid, bcs->channel, skb);
	bcs->trans_down++;
}

/* handle reception of corrupted skb
 * Warning: skb must not be accessed anymore!
 */
static inline void gigaset_rcv_error(struct sk_buff *procskb,
                                     struct cardstate *cs,
                                     struct bc_state *bcs)
{
	if (procskb)
		dev_kfree_skb(procskb);

	if (bcs->ignore)
		--bcs->ignore;
	else {
		++bcs->corrupted;
		gigaset_isdn_rcv_err(bcs);
	}
}


/* bitwise byte inversion table */
extern __u8 gigaset_invtab[];	/* in common.c */


/* append received bytes to inbuf */
static inline int gigaset_fill_inbuf(struct inbuf_t *inbuf,
                                     const unsigned char *src,
                                     unsigned numbytes)
{
	unsigned n, head, tail, bytesleft;

	dbg(DEBUG_INTR, "received %u bytes", numbytes);

	if (!numbytes)
		return 0;

	bytesleft = numbytes;
	tail = atomic_read(&inbuf->tail);
	head = atomic_read(&inbuf->head);
	dbg(DEBUG_INTR, "buffer state: %u -> %u", head, tail);

	while (bytesleft) {
		if (head > tail)
			n = head - 1 - tail;
		else if (head == 0)
			n = (RBUFSIZE-1) - tail;
		else
			n = RBUFSIZE - tail;
		if (!n) {
			err("buffer overflow (%u bytes lost)", bytesleft);
			break;
		}
		if (n > bytesleft)
			n = bytesleft;
		memcpy(inbuf->data + tail, src, n);
		bytesleft -= n;
		tail = (tail + n) % RBUFSIZE;
		src += n;
	}
	dbg(DEBUG_INTR, "setting tail to %u", tail);
	atomic_set(&inbuf->tail, tail);
	return numbytes != bytesleft;
}

/* ===========================================================================
 *  Functions implemented in interface.c
 */

/* initialize interface */
void gigaset_if_initdriver(struct gigaset_driver *drv, const char *procname,
                           const char *devname, const char *devfsname);
/* release interface */
void gigaset_if_freedriver(struct gigaset_driver *drv);
/* add minor */
void gigaset_if_init(struct cardstate *cs);
/* remove minor */
void gigaset_if_free(struct cardstate *cs);
/* device received data */
void gigaset_if_receive(struct cardstate *cs,
                        unsigned char *buffer, size_t len);

#endif
