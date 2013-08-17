/* $Id: icn.h,v 1.30.6.5 2001/09/23 22:24:55 kai Exp $
 *
 * ISDN lowlevel-module for the ICN active ISDN-Card.
 *
 * Copyright 1994 by Fritz Elfert (fritz@isdn4linux.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef icn_h
#define icn_h

#define ICN_IOCTL_SETMMIO   0
#define ICN_IOCTL_GETMMIO   1
#define ICN_IOCTL_SETPORT   2
#define ICN_IOCTL_GETPORT   3
#define ICN_IOCTL_LOADBOOT  4
#define ICN_IOCTL_LOADPROTO 5
#define ICN_IOCTL_LEASEDCFG 6
#define ICN_IOCTL_GETDOUBLE 7
#define ICN_IOCTL_DEBUGVAR  8
#define ICN_IOCTL_ADDCARD   9

/* Struct for adding new cards */
typedef struct icn_cdef {
	int port;
	char id1[10];
	char id2[10];
} icn_cdef;

#if defined(__KERNEL__) || defined(__DEBUGVAR__)

#ifdef __KERNEL__
/* Kernel includes */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/isdnif.h>

#endif                          /* __KERNEL__ */

/* some useful macros for debugging */
#ifdef ICN_DEBUG_PORT
#define OUTB_P(v, p) {printk(KERN_DEBUG "icn: outb_p(0x%02x,0x%03x)\n", v, p); outb_p(v, p);}
#else
#define OUTB_P outb
#endif

/* Defaults for Port-Address and shared-memory */
#define ICN_BASEADDR 0x320
#define ICN_PORTLEN (0x04)
#define ICN_MEMADDR 0x0d0000

#define ICN_FLAGS_B1ACTIVE 1    /* B-Channel-1 is open                     */
#define ICN_FLAGS_B2ACTIVE 2    /* B-Channel-2 is open                     */
#define ICN_FLAGS_RUNNING  4    /* Cards driver activated                  */
#define ICN_FLAGS_RBTIMER  8    /* cyclic scheduling of B-Channel-poll     */

#define ICN_BOOT_TIMEOUT1  1000 /* Delay for Boot-download (msecs)         */

#define ICN_TIMER_BCREAD (HZ / 100)	/* B-Channel poll-cycle                    */
#define ICN_TIMER_DCREAD (HZ / 2) /* D-Channel poll-cycle                    */

#define ICN_CODE_STAGE1 4096    /* Size of bootcode                        */
#define ICN_CODE_STAGE2 65536   /* Size of protocol-code                   */

#define ICN_MAX_SQUEUE 8000     /* Max. outstanding send-data (2* hw-buf.) */
#define ICN_FRAGSIZE (250)      /* Max. size of send-fragments             */
#define ICN_BCH 2               /* Number of supported channels per card   */

/* type-definitions for accessing the mmap-io-areas */

#define SHM_DCTL_OFFSET (0)     /* Offset to data-controlstructures in shm */
#define SHM_CCTL_OFFSET (0x1d2) /* Offset to comm-controlstructures in shm */
#define SHM_CBUF_OFFSET (0x200) /* Offset to comm-buffers in shm           */
#define SHM_DBUF_OFFSET (0x2000)	/* Offset to data-buffers in shm           */

/*
 * Layout of card's data buffers
 */
typedef struct {
	unsigned char length;   /* Bytecount of fragment (max 250)    */
	unsigned char endflag;  /* 0=last frag., 0xff=frag. continued */
	unsigned char data[ICN_FRAGSIZE];	/* The data                           */
	/* Fill to 256 bytes */
	char unused[0x100 - ICN_FRAGSIZE - 2];
} frag_buf;

/*
 * Layout of card's shared memory
 */
typedef union {
	struct {
		unsigned char scns;	/* Index to free SendFrag.             */
		unsigned char scnr;	/* Index to active SendFrag   READONLY */
		unsigned char ecns;	/* Index to free RcvFrag.     READONLY */
		unsigned char ecnr;	/* Index to valid RcvFrag              */
		char unused[6];
		unsigned short fuell1;	/* Internal Buf Bytecount              */
	} data_control;
	struct {
		char unused[SHM_CCTL_OFFSET];
		unsigned char iopc_i;	/* Read-Ptr Status-Queue      READONLY */
		unsigned char iopc_o;	/* Write-Ptr Status-Queue              */
		unsigned char pcio_i;	/* Write-Ptr Command-Queue             */
		unsigned char pcio_o;	/* Read-Ptr Command Queue     READONLY */
	} comm_control;
	struct {
		char unused[SHM_CBUF_OFFSET];
		unsigned char pcio_buf[0x100];	/* Ring-Buffer Command-Queue           */
		unsigned char iopc_buf[0x100];	/* Ring-Buffer Status-Queue            */
	} comm_buffers;
	struct {
		char unused[SHM_DBUF_OFFSET];
		frag_buf receive_buf[0x10];
		frag_buf send_buf[0x10];
	} data_buffers;
} icn_shmem;

/*
 * Per card driver data
 */
typedef struct icn_card {
	struct icn_card *next;  /* Pointer to next device struct    */
	struct icn_card *other; /* Pointer to other card for ICN4B  */
	unsigned short port;    /* Base-port-address                */
	int myid;               /* Driver-Nr. assigned by linklevel */
	int rvalid;             /* IO-portregion has been requested */
	int leased;             /* Flag: This Adapter is connected  */
				/*       to a leased line           */
	unsigned short flags;   /* Statusflags                      */
	int doubleS0;           /* Flag: ICN4B                      */
	int secondhalf;         /* Flag: Second half of a doubleS0  */
	int fw_rev;             /* Firmware revision loaded         */
	int ptype;              /* Protocol type (1TR6 or Euro)     */
	struct timer_list st_timer;   /* Timer for Status-Polls     */
	struct timer_list rb_timer;   /* Timer for B-Channel-Polls  */
	u_char rcvbuf[ICN_BCH][4096]; /* B-Channel-Receive-Buffers  */
	int rcvidx[ICN_BCH];    /* Index for above buffers          */
	int l2_proto[ICN_BCH];  /* Current layer-2-protocol         */
	isdn_if interface;      /* Interface to upper layer         */
	int iptr;               /* Index to imsg-buffer             */
	char imsg[60];          /* Internal buf for status-parsing  */
	char msg_buf[2048];     /* Buffer for status-messages       */
	char *msg_buf_write;    /* Writepointer for statusbuffer    */
	char *msg_buf_read;     /* Readpointer for statusbuffer     */
	char *msg_buf_end;      /* Pointer to end of statusbuffer   */
	int sndcount[ICN_BCH];  /* Byte-counters for B-Ch.-send     */
	int xlen[ICN_BCH];      /* Byte-counters/Flags for sent-ACK */
	struct sk_buff *xskb[ICN_BCH]; /* Current transmitted skb   */
	struct sk_buff_head spqueue[ICN_BCH];  /* Sendqueue         */
	char regname[35];       /* Name used for request_region     */
	u_char xmit_lock[ICN_BCH]; /* Semaphore for pollbchan_send()*/
	spinlock_t lock;        /* protect critical operations      */
} icn_card;

/*
 * Main driver data
 */
typedef struct icn_dev {
	spinlock_t devlock;     /* spinlock to protect this struct  */
	unsigned long memaddr;	/* Address of memory mapped buffers */
	icn_shmem __iomem *shmem;       /* Pointer to memory-mapped-buffers */
	int mvalid;             /* IO-shmem has been requested      */
	int channel;            /* Currently mapped channel         */
	struct icn_card *mcard; /* Currently mapped card            */
	int chanlock;           /* Semaphore for channel-mapping    */
	int firstload;          /* Flag: firmware never loaded      */
} icn_dev;

typedef icn_dev *icn_devptr;

#ifdef __KERNEL__

static icn_card *cards = (icn_card *) 0;
static u_char chan2bank[] =
{0, 4, 8, 12};                  /* for icn_map_channel() */

static icn_dev dev;

#endif                          /* __KERNEL__ */

/* Utility-Macros */

/* Macros for accessing ports */
#define ICN_CFG    (card->port)
#define ICN_MAPRAM (card->port + 1)
#define ICN_RUN    (card->port + 2)
#define ICN_BANK   (card->port + 3)

/* Return true, if there is a free transmit-buffer */
#define sbfree (((readb(&dev.shmem->data_control.scns) + 1) & 0xf) !=	\
		readb(&dev.shmem->data_control.scnr))

/* Switch to next transmit-buffer */
#define sbnext (writeb((readb(&dev.shmem->data_control.scns) + 1) & 0xf,	\
		       &dev.shmem->data_control.scns))

/* Shortcuts for transmit-buffer-access */
#define sbuf_n dev.shmem->data_control.scns
#define sbuf_d dev.shmem->data_buffers.send_buf[readb(&sbuf_n)].data
#define sbuf_l dev.shmem->data_buffers.send_buf[readb(&sbuf_n)].length
#define sbuf_f dev.shmem->data_buffers.send_buf[readb(&sbuf_n)].endflag

/* Return true, if there is receive-data is available */
#define rbavl  (readb(&dev.shmem->data_control.ecnr) != \
		readb(&dev.shmem->data_control.ecns))

/* Switch to next receive-buffer */
#define rbnext (writeb((readb(&dev.shmem->data_control.ecnr) + 1) & 0xf,	\
		       &dev.shmem->data_control.ecnr))

/* Shortcuts for receive-buffer-access */
#define rbuf_n dev.shmem->data_control.ecnr
#define rbuf_d dev.shmem->data_buffers.receive_buf[readb(&rbuf_n)].data
#define rbuf_l dev.shmem->data_buffers.receive_buf[readb(&rbuf_n)].length
#define rbuf_f dev.shmem->data_buffers.receive_buf[readb(&rbuf_n)].endflag

/* Shortcuts for command-buffer-access */
#define cmd_o (dev.shmem->comm_control.pcio_o)
#define cmd_i (dev.shmem->comm_control.pcio_i)

/* Return free space in command-buffer */
#define cmd_free ((readb(&cmd_i) >= readb(&cmd_o)) ?		\
		  0x100 - readb(&cmd_i) + readb(&cmd_o) :	\
		  readb(&cmd_o) - readb(&cmd_i))

/* Shortcuts for message-buffer-access */
#define msg_o (dev.shmem->comm_control.iopc_o)
#define msg_i (dev.shmem->comm_control.iopc_i)

/* Return length of Message, if avail. */
#define msg_avail ((readb(&msg_o) > readb(&msg_i)) ?		\
		   0x100 - readb(&msg_o) + readb(&msg_i) :	\
		   readb(&msg_i) - readb(&msg_o))

#define CID (card->interface.id)

#endif                          /* defined(__KERNEL__) || defined(__DEBUGVAR__) */
#endif                          /* icn_h */
