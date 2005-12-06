/* $Id: isdnloop.h,v 1.5.6.3 2001/09/23 22:24:56 kai Exp $
 *
 * Loopback lowlevel module for testing of linklevel.
 *
 * Copyright 1997 by Fritz Elfert (fritz@isdn4linux.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef isdnloop_h
#define isdnloop_h

#define ISDNLOOP_IOCTL_DEBUGVAR  0
#define ISDNLOOP_IOCTL_ADDCARD   1
#define ISDNLOOP_IOCTL_LEASEDCFG 2
#define ISDNLOOP_IOCTL_STARTUP   3

/* Struct for adding new cards */
typedef struct isdnloop_cdef {
	char id1[10];
} isdnloop_cdef;

/* Struct for configuring cards */
typedef struct isdnloop_sdef {
	int ptype;
	char num[3][20];
} isdnloop_sdef;

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
#include <linux/isdnif.h>

#endif                          /* __KERNEL__ */

#define ISDNLOOP_FLAGS_B1ACTIVE 1	/* B-Channel-1 is open           */
#define ISDNLOOP_FLAGS_B2ACTIVE 2	/* B-Channel-2 is open           */
#define ISDNLOOP_FLAGS_RUNNING  4	/* Cards driver activated        */
#define ISDNLOOP_FLAGS_RBTIMER  8	/* scheduling of B-Channel-poll  */
#define ISDNLOOP_TIMER_BCREAD 1 /* B-Channel poll-cycle          */
#define ISDNLOOP_TIMER_DCREAD (HZ/2)	/* D-Channel poll-cycle          */
#define ISDNLOOP_TIMER_ALERTWAIT (10*HZ)	/* Alert timeout                 */
#define ISDNLOOP_MAX_SQUEUE 65536	/* Max. outstanding send-data    */
#define ISDNLOOP_BCH 2          /* channels per card             */

/*
 * Per card driver data
 */
typedef struct isdnloop_card {
	struct isdnloop_card *next;	/* Pointer to next device struct    */
	struct isdnloop_card
	*rcard[ISDNLOOP_BCH];   /* Pointer to 'remote' card         */
	int rch[ISDNLOOP_BCH];  /* 'remote' channel                 */
	int myid;               /* Driver-Nr. assigned by linklevel */
	int leased;             /* Flag: This Adapter is connected  */
	/*       to a leased line           */
	int sil[ISDNLOOP_BCH];  /* SI's to listen for               */
	char eazlist[ISDNLOOP_BCH][11];
	/* EAZ's to listen for              */
	char s0num[3][20];      /* 1TR6 base-number or MSN's        */
	unsigned short flags;   /* Statusflags                      */
	int ptype;              /* Protocol type (1TR6 or Euro)     */
	struct timer_list st_timer;	/* Timer for Status-Polls           */
	struct timer_list rb_timer;	/* Timer for B-Channel-Polls        */
	struct timer_list
	 c_timer[ISDNLOOP_BCH]; /* Timer for Alerting               */
	int l2_proto[ISDNLOOP_BCH];	/* Current layer-2-protocol         */
	isdn_if interface;      /* Interface to upper layer         */
	int iptr;               /* Index to imsg-buffer             */
	char imsg[60];          /* Internal buf for status-parsing  */
	int optr;               /* Index to omsg-buffer             */
	char omsg[60];          /* Internal buf for cmd-parsing     */
	char msg_buf[2048];     /* Buffer for status-messages       */
	char *msg_buf_write;    /* Writepointer for statusbuffer    */
	char *msg_buf_read;     /* Readpointer for statusbuffer     */
	char *msg_buf_end;      /* Pointer to end of statusbuffer   */
	int sndcount[ISDNLOOP_BCH];	/* Byte-counters for B-Ch.-send     */
	struct sk_buff_head
	 bqueue[ISDNLOOP_BCH];  /* B-Channel queues                 */
	struct sk_buff_head dqueue;	/* D-Channel queue                  */
} isdnloop_card;

/*
 * Main driver data
 */
#ifdef __KERNEL__
static isdnloop_card *cards = (isdnloop_card *) 0;
#endif                          /* __KERNEL__ */

/* Utility-Macros */

#define CID (card->interface.id)

#endif                          /* defined(__KERNEL__) || defined(__DEBUGVAR__) */
#endif                          /* isdnloop_h */
