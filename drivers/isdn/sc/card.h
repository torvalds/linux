/* $Id: card.h,v 1.1.10.1 2001/09/23 22:24:59 kai Exp $
 *
 * Driver parameters for SpellCaster ISA ISDN adapters
 *
 * Copyright (C) 1996  SpellCaster Telecommunications Inc.
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For more information, please contact gpl-info@spellcast.com or write:
 *
 *     SpellCaster Telecommunications Inc.
 *     5621 Finch Avenue East, Unit #3
 *     Scarborough, Ontario  Canada
 *     M1B 2T9
 *     +1 (416) 297-8565
 *     +1 (416) 297-6433 Facsimile
 */

#ifndef CARD_H
#define CARD_H

/*
 * We need these if they're not already included
 */
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/isdnif.h>
#include <linux/irqreturn.h>
#include "message.h"
#include "scioc.h"

/*
 * Amount of time to wait for a reset to complete
 */
#define CHECKRESET_TIME		msecs_to_jiffies(4000)

/*
 * Amount of time between line status checks
 */
#define CHECKSTAT_TIME		msecs_to_jiffies(8000)

/*
 * The maximum amount of time to wait for a message response
 * to arrive. Use exclusively by send_and_receive
 */
#define SAR_TIMEOUT		msecs_to_jiffies(10000)

/*
 * Macro to determine is a card id is valid
 */
#define IS_VALID_CARD(x)	((x >= 0) && (x <= cinst))

/*
 * Per channel status and configuration
 */
typedef struct {
	int l2_proto;
	int l3_proto;
	char dn[50];
	unsigned long first_sendbuf;	/* Offset of first send buffer */
	unsigned int num_sendbufs;	/* Number of send buffers */
	unsigned int free_sendbufs;	/* Number of free sendbufs */
	unsigned int next_sendbuf;	/* Next sequential buffer */
	char eazlist[50];		/* Set with SETEAZ */
	char sillist[50];		/* Set with SETSIL */
	int eazclear;			/* Don't accept calls if TRUE */
} bchan;

/*
 * Everything you want to know about the adapter ...
 */
typedef struct {
	int model;
	int driverId;			/* LL Id */
	char devicename[20];		/* The device name */
	isdn_if *card;			/* ISDN4Linux structure */
	bchan *channel;			/* status of the B channels */
	char nChannels;			/* Number of channels */
	unsigned int interrupt;		/* Interrupt number */
	int iobase;			/* I/O Base address */
	int ioport[MAX_IO_REGS];	/* Index to I/O ports */
	int shmem_pgport;		/* port for the exp mem page reg. */
	int shmem_magic;		/* adapter magic number */
	unsigned int rambase;		/* Shared RAM base address */
	unsigned int ramsize;		/* Size of shared memory */
	RspMessage async_msg;		/* Async response message */
	int want_async_messages;	/* Snoop the Q ? */
	unsigned char seq_no;		/* Next send seq. number */
	struct timer_list reset_timer;	/* Check reset timer */
	struct timer_list stat_timer;	/* Check startproc timer */
	unsigned char nphystat;		/* Latest PhyStat info */
	unsigned char phystat;		/* Last PhyStat info */
	HWConfig_pl hwconfig;		/* Hardware config info */
	char load_ver[11];		/* CommManage Version string */
	char proc_ver[11];		/* CommEngine Version */
	int StartOnReset;		/* Indicates startproc after reset */
	int EngineUp;			/* Indicates CommEngine Up */
	int trace_mode;			/* Indicate if tracing is on */
	spinlock_t lock;		/* local lock */
} board;


extern board *sc_adapter[];
extern int cinst;

void memcpy_toshmem(int card, void *dest, const void *src, size_t n);
void memcpy_fromshmem(int card, void *dest, const void *src, size_t n);
int get_card_from_id(int driver);
int indicate_status(int card, int event, ulong Channel, char *Data);
irqreturn_t interrupt_handler(int interrupt, void *cardptr);
int sndpkt(int devId, int channel, struct sk_buff *data);
void rcvpkt(int card, RspMessage *rcvmsg);
int command(isdn_ctrl *cmd);
int reset(int card);
int startproc(int card);
int send_and_receive(int card, unsigned int procid, unsigned char type,
		     unsigned char class, unsigned char code,
		     unsigned char link, unsigned char data_len,
		     unsigned char *data,  RspMessage *mesgdata, int timeout);
void flushreadfifo (int card);
int sendmessage(int card, unsigned int procid, unsigned int type,
		unsigned int class, unsigned int code, unsigned int link,
		unsigned int data_len, unsigned int *data);
int receivemessage(int card, RspMessage *rspmsg);
int sc_ioctl(int card, scs_ioctl *data);
int setup_buffers(int card, int c);
void check_reset(unsigned long data);
void check_phystat(unsigned long data);

#endif /* CARD_H */
