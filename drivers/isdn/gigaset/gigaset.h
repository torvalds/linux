/*
 * Siemens Gigaset 307x driver
 * Common header file for all connection variants
 *
 * Written by Stefan Eilers
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

/* define global prefix for pr_ macros in linux/kernel.h */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/spinlock.h>
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

#define GIG_TICK 100		/* in milliseconds */

/* timeout values (unit: 1 sec) */
#define INIT_TIMEOUT 1

/* timeout values (unit: 0.1 sec) */
#define RING_TIMEOUT 3		/* for additional parameters to RING */
#define BAS_TIMEOUT 20		/* for response to Base USB ops */
#define ATRDY_TIMEOUT 3		/* for HD_READY_SEND_ATDATA */

#define BAS_RETRY 3		/* max. retries for base USB ops */

#define MAXACT 3

extern int gigaset_debuglevel;	/* "needs" cast to (enum debuglevel) */

/* debug flags, combine by adding/bitwise OR */
enum debuglevel {
	DEBUG_INTR	  = 0x00008, /* interrupt processing */
	DEBUG_CMD	  = 0x00020, /* sent/received LL commands */
	DEBUG_STREAM	  = 0x00040, /* application data stream I/O events */
	DEBUG_STREAM_DUMP = 0x00080, /* application data stream content */
	DEBUG_LLDATA	  = 0x00100, /* sent/received LL data */
	DEBUG_DRIVER	  = 0x00400, /* driver structure */
	DEBUG_HDLC	  = 0x00800, /* M10x HDLC processing */
	DEBUG_WRITE	  = 0x01000, /* M105 data write */
	DEBUG_TRANSCMD	  = 0x02000, /* AT-COMMANDS+RESPONSES */
	DEBUG_MCMD	  = 0x04000, /* COMMANDS THAT ARE SENT VERY OFTEN */
	DEBUG_INIT	  = 0x08000, /* (de)allocation+initialization of data
					structures */
	DEBUG_SUSPEND	  = 0x10000, /* suspend/resume processing */
	DEBUG_OUTPUT	  = 0x20000, /* output to device */
	DEBUG_ISO	  = 0x40000, /* isochronous transfers */
	DEBUG_IF	  = 0x80000, /* character device operations */
	DEBUG_USBREQ	  = 0x100000, /* USB communication (except payload
					 data) */
	DEBUG_LOCKCMD	  = 0x200000, /* AT commands and responses when
					 MS_LOCKED */

	DEBUG_ANY	  = 0x3fffff, /* print message if any of the others is
					 activated */
};

#ifdef CONFIG_GIGASET_DEBUG

#define gig_dbg(level, format, arg...) \
	do { \
		if (unlikely(((enum debuglevel)gigaset_debuglevel) & (level))) \
			printk(KERN_DEBUG KBUILD_MODNAME ": " format "\n", \
			       ## arg); \
	} while (0)
#define DEBUG_DEFAULT (DEBUG_TRANSCMD | DEBUG_CMD | DEBUG_USBREQ)

#else

#define gig_dbg(level, format, arg...) do {} while (0)
#define DEBUG_DEFAULT 0

#endif

void gigaset_dbg_buffer(enum debuglevel level, const unsigned char *msg,
			size_t len, const unsigned char *buf);

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

/* number of B channels supported by base driver */
#define BAS_CHANNELS	2

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
#define AT_CLIP		7
/* total number */
#define AT_NUM		8

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

/* layer 2 protocols (AT^SBPR=...) */
#define L2_BITSYNC	0
#define L2_HDLC		1
#define L2_VOICE	2

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

extern struct reply_t gigaset_tab_cid[];
extern struct reply_t gigaset_tab_nocid[];

struct inbuf_t {
	unsigned char		*rcvbuf;	/* usb-gigaset receive buffer */
	struct bc_state		*bcs;
	struct cardstate	*cs;
	int			inputstate;
	int			head, tail;
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
 */
struct isowbuf_t {
	int		read;
	int		nextread;
	int		write;
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
 * - status: URB completion status
 */
struct isow_urbctx_t {
	struct urb *urb;
	struct bc_state *bcs;
	int limit;
	int status;
};

/* AT state structure
 * data associated with the state of an ISDN connection, whether or not
 * it is currently assigned a B channel
 */
struct at_state_t {
	struct list_head	list;
	int			waiting;
	int			getstring;
	unsigned		timer_index;
	unsigned long		timer_expires;
	int			timer_active;
	unsigned int		ConState;	/* State of connection */
	struct reply_t		*replystruct;
	int			cid;
	int			int_var[VAR_NUM];	/* see VAR_XXXX */
	char			*str_var[STR_NUM];	/* see STR_XXXX */
	unsigned		pending_commands;	/* see PC_XXXX */
	unsigned		seq_index;

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

	__u16 fcs;
	struct sk_buff *skb;
	int inputstate;			/* see INS_XXXX */

	int channel;

	struct cardstate *cs;

	unsigned chstate;		/* bitmap (CHS_*) */
	int ignore;
	unsigned proto2;		/* layer 2 protocol (L2_*) */
	char *commands[AT_NUM];		/* see AT_XXXX */

#ifdef CONFIG_GIGASET_DEBUG
	int emptycount;
#endif
	int busy;
	int use_count;

	/* private data of hardware drivers */
	union {
		struct ser_bc_state *ser;	/* serial hardware driver */
		struct usb_bc_state *usb;	/* usb hardware driver (m105) */
		struct bas_bc_state *bas;	/* usb hardware driver (base) */
	} hw;

	void *ap;			/* LL application structure */
};

struct cardstate {
	struct gigaset_driver *driver;
	unsigned minor_index;
	struct device *dev;
	struct device *tty_dev;
	unsigned flags;

	const struct gigaset_ops *ops;

	/* Stuff to handle communication */
	wait_queue_head_t waitqueue;
	int waiting;
	int mode;			/* see M_XXXX */
	int mstate;			/* Modem state: see MS_XXXX */
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

	unsigned running;		/* !=0 if events are handled */
	unsigned connected;		/* !=0 if hardware is connected */
	unsigned isdn_up;		/* !=0 after gigaset_isdn_start() */

	unsigned cidmode;

	int myid;			/* id for communication with LL */
	void *iif;			/* LL interface structure */
	unsigned short hw_hdr_len;	/* headroom needed in data skbs */

	struct reply_t *tabnocid;
	struct reply_t *tabcid;
	int cs_init;
	int ignoreframes;		/* frames to ignore after setting up the
					   B channel */
	struct mutex mutex;		/* locks this structure:
					 *   connected is not changed,
					 *   hardware_up is not changed,
					 *   MState is not changed to or from
					 *   MS_LOCKED */

	struct timer_list timer;
	int retry_count;
	int dle;			/* !=0 if modem commands/responses are
					   dle encoded */
	int cur_at_seq;			/* sequence of AT commands being
					   processed */
	int curchannel;			/* channel those commands are meant
					   for */
	int commands_pending;		/* flag(s) in xxx.commands_pending have
					   been set */
	struct tasklet_struct event_tasklet;
					/* tasklet for serializing AT commands.
					 * Scheduled
					 *   -> for modem reponses (and
					 *      incoming data for M10x)
					 *   -> on timeout
					 *   -> after setting bits in
					 *      xxx.at_state.pending_command
					 *      (e.g. command from LL) */
	struct tasklet_struct write_tasklet;
					/* tasklet for serial output
					 * (not used in base driver) */

	/* event queue */
	struct event_t events[MAX_EVENTS];
	unsigned ev_tail, ev_head;
	spinlock_t ev_lock;

	/* current modem response */
	unsigned char respdata[MAX_RESP_SIZE];
	unsigned cbytes;

	/* private data of hardware drivers */
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
	int		running;
	atomic_t	corrbytes;
	spinlock_t	isooutlock;
	struct isow_urbctx_t	isoouturbs[BAS_OUTURBS];
	struct isow_urbctx_t	*isooutdone, *isooutfree, *isooutovfl;
	struct isowbuf_t	*isooutbuf;
	unsigned numsub;		/* submitted URB counter
					   (for diagnostic messages only) */
	struct tasklet_struct	sent_tasklet;

	/* isochronous input state */
	spinlock_t isoinlock;
	struct urb *isoinurbs[BAS_INURBS];
	unsigned char isoinbuf[BAS_INBUFSIZE * BAS_INURBS];
	struct urb *isoindone;		/* completed isoc read URB */
	int isoinstatus;		/* status of completed URB */
	int loststatus;			/* status of dropped URB */
	unsigned isoinlost;		/* number of bytes lost */
	/* state of bit unstuffing algorithm
	   (in addition to BC_state.inputstate) */
	unsigned seqlen;		/* number of '1' bits not yet
					   unstuffed */
	unsigned inbyte, inbits;	/* collected bits for next byte */
	/* statistics */
	unsigned goodbytes;		/* bytes correctly received */
	unsigned alignerrs;		/* frames with incomplete byte at end */
	unsigned fcserrs;		/* FCS errors */
	unsigned frameerrs;		/* framing errors */
	unsigned giants;		/* long frames */
	unsigned runts;			/* short frames */
	unsigned aborts;		/* HDLC aborts */
	unsigned shared0s;		/* '0' bits shared between flags */
	unsigned stolen0s;		/* '0' stuff bits also serving as
					   leading flag bits */
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

	/* Called by gigaset_bchannel_down() for resetting bcs->hw.xxx */
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

	/* Called from LL interface to put an skb into the send-queue.
	 * After sending is completed, gigaset_skb_sent() must be called
	 * with the skb's link layer header preserved. */
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
#define DLE_FLAG	0x10

/* ===========================================================================
 *  Functions implemented in asyncdata.c
 */

/* Called from LL interface to put an skb into the send queue. */
int gigaset_m10x_send_skb(struct bc_state *bcs, struct sk_buff *skb);

/* Called from ev-layer.c to process a block of data
 * received through the common/control channel. */
void gigaset_m10x_input(struct inbuf_t *inbuf);

/* ===========================================================================
 *  Functions implemented in isocdata.c
 */

/* Called from LL interface to put an skb into the send queue. */
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
 *  Functions implemented in LL interface
 */

/* Called from common.c for setting up/shutting down with the ISDN subsystem */
int gigaset_isdn_register(struct cardstate *cs, const char *isdnid);
void gigaset_isdn_unregister(struct cardstate *cs);

/* Called from hardware module to indicate completion of an skb */
void gigaset_skb_sent(struct bc_state *bcs, struct sk_buff *skb);
void gigaset_skb_rcvd(struct bc_state *bcs, struct sk_buff *skb);
void gigaset_isdn_rcv_err(struct bc_state *bcs);

/* Called from common.c/ev-layer.c to indicate events relevant to the LL */
void gigaset_isdn_start(struct cardstate *cs);
void gigaset_isdn_stop(struct cardstate *cs);
int gigaset_isdn_icall(struct at_state_t *at_state);
void gigaset_isdn_connD(struct bc_state *bcs);
void gigaset_isdn_hupD(struct bc_state *bcs);
void gigaset_isdn_connB(struct bc_state *bcs);
void gigaset_isdn_hupB(struct bc_state *bcs);

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
void gigaset_init_dev_sysfs(struct cardstate *cs);
void gigaset_free_dev_sysfs(struct cardstate *cs);

/* ===========================================================================
 *  Functions implemented in common.c/gigaset.h
 */

void gigaset_bcs_reinit(struct bc_state *bcs);
void gigaset_at_init(struct at_state_t *at_state, struct bc_state *bcs,
		     struct cardstate *cs, int cid);
int gigaset_get_channel(struct bc_state *bcs);
struct bc_state *gigaset_get_free_channel(struct cardstate *cs);
void gigaset_free_channel(struct bc_state *bcs);
int gigaset_get_channels(struct cardstate *cs);
void gigaset_free_channels(struct cardstate *cs);
void gigaset_block_channels(struct cardstate *cs);

/* Allocate and initialize driver structure. */
struct gigaset_driver *gigaset_initdriver(unsigned minor, unsigned minors,
					  const char *procname,
					  const char *devname,
					  const struct gigaset_ops *ops,
					  struct module *owner);

/* Deallocate driver structure. */
void gigaset_freedriver(struct gigaset_driver *drv);
void gigaset_debugdrivers(void);
struct cardstate *gigaset_get_cs_by_tty(struct tty_struct *tty);
struct cardstate *gigaset_get_cs_by_id(int id);
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
int gigaset_shutdown(struct cardstate *cs);

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
	if (cs->running)
		tasklet_schedule(&cs->event_tasklet);
	spin_unlock_irqrestore(&cs->lock, flags);
}

/* Tell common.c that B channel has been closed. */
/* cs->lock must not be locked */
static inline void gigaset_bchannel_down(struct bc_state *bcs)
{
	gigaset_add_event(bcs->cs, &bcs->at_state, EV_BC_CLOSED, NULL, 0, NULL);

	gig_dbg(DEBUG_CMD, "scheduling BC_CLOSED");
	gigaset_schedule_event(bcs->cs);
}

/* Tell common.c that B channel has been opened. */
/* cs->lock must not be locked */
static inline void gigaset_bchannel_up(struct bc_state *bcs)
{
	gigaset_add_event(bcs->cs, &bcs->at_state, EV_BC_OPEN, NULL, 0, NULL);

	gig_dbg(DEBUG_CMD, "scheduling BC_OPEN");
	gigaset_schedule_event(bcs->cs);
}

/* handling routines for sk_buff */
/* ============================= */

/* append received bytes to inbuf */
int gigaset_fill_inbuf(struct inbuf_t *inbuf, const unsigned char *src,
		       unsigned numbytes);

/* ===========================================================================
 *  Functions implemented in interface.c
 */

/* initialize interface */
void gigaset_if_initdriver(struct gigaset_driver *drv, const char *procname,
			   const char *devname);
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
