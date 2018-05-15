/* $Id: capidrv.c,v 1.1.2.2 2004/01/12 23:17:24 keil Exp $
 *
 * ISDN4Linux Driver, using capi20 interface (kernelcapi)
 *
 * Copyright 1997 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/skbuff.h>
#include <linux/isdn.h>
#include <linux/isdnif.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/moduleparam.h>

#include <linux/isdn/capiutil.h>
#include <linux/isdn/capicmd.h>
#include "capidrv.h"

static int debugmode = 0;

MODULE_DESCRIPTION("CAPI4Linux: Interface to ISDN4Linux");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");
module_param(debugmode, uint, S_IRUGO | S_IWUSR);

/* -------- type definitions ----------------------------------------- */


struct capidrv_contr {

	struct capidrv_contr *next;
	struct module *owner;
	u32 contrnr;
	char name[20];

	/*
	 * for isdn4linux
	 */
	isdn_if interface;
	int myid;

	/*
	 * LISTEN state
	 */
	int state;
	u32 cipmask;
	u32 cipmask2;
	struct timer_list listentimer;

	/*
	 * ID of capi message sent
	 */
	u16 msgid;

	/*
	 * B-Channels
	 */
	int nbchan;
	struct capidrv_bchan {
		struct capidrv_contr *contr;
		u8 msn[ISDN_MSNLEN];
		int l2;
		int l3;
		u8 num[ISDN_MSNLEN];
		u8 mynum[ISDN_MSNLEN];
		int si1;
		int si2;
		int incoming;
		int disconnecting;
		struct capidrv_plci {
			struct capidrv_plci *next;
			u32 plci;
			u32 ncci;	/* ncci for CONNECT_ACTIVE_IND */
			u16 msgid;	/* to identfy CONNECT_CONF */
			int chan;
			int state;
			int leasedline;
			struct capidrv_ncci {
				struct capidrv_ncci *next;
				struct capidrv_plci *plcip;
				u32 ncci;
				u16 msgid;	/* to identfy CONNECT_B3_CONF */
				int chan;
				int state;
				int oldstate;
				/* */
				u16 datahandle;
				struct ncci_datahandle_queue {
					struct ncci_datahandle_queue *next;
					u16                         datahandle;
					int                           len;
				} *ackqueue;
			} *ncci_list;
		} *plcip;
		struct capidrv_ncci *nccip;
	} *bchans;

	struct capidrv_plci *plci_list;

	/* for q931 data */
	u8  q931_buf[4096];
	u8 *q931_read;
	u8 *q931_write;
	u8 *q931_end;
};


struct capidrv_data {
	struct capi20_appl ap;
	int ncontr;
	struct capidrv_contr *contr_list;
};

typedef struct capidrv_plci capidrv_plci;
typedef struct capidrv_ncci capidrv_ncci;
typedef struct capidrv_contr capidrv_contr;
typedef struct capidrv_data capidrv_data;
typedef struct capidrv_bchan capidrv_bchan;

/* -------- data definitions ----------------------------------------- */

static capidrv_data global;
static DEFINE_SPINLOCK(global_lock);

static void handle_dtrace_data(capidrv_contr *card,
			       int send, int level2, u8 *data, u16 len);

/* -------- convert functions ---------------------------------------- */

static inline u32 b1prot(int l2, int l3)
{
	switch (l2) {
	case ISDN_PROTO_L2_X75I:
	case ISDN_PROTO_L2_X75UI:
	case ISDN_PROTO_L2_X75BUI:
		return 0;
	case ISDN_PROTO_L2_HDLC:
	default:
		return 0;
	case ISDN_PROTO_L2_TRANS:
		return 1;
	case ISDN_PROTO_L2_V11096:
	case ISDN_PROTO_L2_V11019:
	case ISDN_PROTO_L2_V11038:
		return 2;
	case ISDN_PROTO_L2_FAX:
		return 4;
	case ISDN_PROTO_L2_MODEM:
		return 8;
	}
}

static inline u32 b2prot(int l2, int l3)
{
	switch (l2) {
	case ISDN_PROTO_L2_X75I:
	case ISDN_PROTO_L2_X75UI:
	case ISDN_PROTO_L2_X75BUI:
	default:
		return 0;
	case ISDN_PROTO_L2_HDLC:
	case ISDN_PROTO_L2_TRANS:
	case ISDN_PROTO_L2_V11096:
	case ISDN_PROTO_L2_V11019:
	case ISDN_PROTO_L2_V11038:
	case ISDN_PROTO_L2_MODEM:
		return 1;
	case ISDN_PROTO_L2_FAX:
		return 4;
	}
}

static inline u32 b3prot(int l2, int l3)
{
	switch (l2) {
	case ISDN_PROTO_L2_X75I:
	case ISDN_PROTO_L2_X75UI:
	case ISDN_PROTO_L2_X75BUI:
	case ISDN_PROTO_L2_HDLC:
	case ISDN_PROTO_L2_TRANS:
	case ISDN_PROTO_L2_V11096:
	case ISDN_PROTO_L2_V11019:
	case ISDN_PROTO_L2_V11038:
	case ISDN_PROTO_L2_MODEM:
	default:
		return 0;
	case ISDN_PROTO_L2_FAX:
		return 4;
	}
}

static _cstruct b1config_async_v110(u16 rate)
{
	/* CAPI-Spec "B1 Configuration" */
	static unsigned char buf[9];
	buf[0] = 8; /* len */
	/* maximum bitrate */
	buf[1] = rate & 0xff; buf[2] = (rate >> 8) & 0xff;
	buf[3] = 8; buf[4] = 0; /* 8 bits per character */
	buf[5] = 0; buf[6] = 0; /* parity none */
	buf[7] = 0; buf[8] = 0; /* 1 stop bit */
	return buf;
}

static _cstruct b1config(int l2, int l3)
{
	switch (l2) {
	case ISDN_PROTO_L2_X75I:
	case ISDN_PROTO_L2_X75UI:
	case ISDN_PROTO_L2_X75BUI:
	case ISDN_PROTO_L2_HDLC:
	case ISDN_PROTO_L2_TRANS:
	default:
		return NULL;
	case ISDN_PROTO_L2_V11096:
		return b1config_async_v110(9600);
	case ISDN_PROTO_L2_V11019:
		return b1config_async_v110(19200);
	case ISDN_PROTO_L2_V11038:
		return b1config_async_v110(38400);
	}
}

static inline u16 si2cip(u8 si1, u8 si2)
{
	static const u8 cip[17][5] =
		{
			/*  0  1  2  3  4  */
			{0, 0, 0, 0, 0},	/*0 */
			{16, 16, 4, 26, 16},	/*1 */
			{17, 17, 17, 4, 4},	/*2 */
			{2, 2, 2, 2, 2},	/*3 */
			{18, 18, 18, 18, 18},	/*4 */
			{2, 2, 2, 2, 2},	/*5 */
			{0, 0, 0, 0, 0},	/*6 */
			{2, 2, 2, 2, 2},	/*7 */
			{2, 2, 2, 2, 2},	/*8 */
			{21, 21, 21, 21, 21},	/*9 */
			{19, 19, 19, 19, 19},	/*10 */
			{0, 0, 0, 0, 0},	/*11 */
			{0, 0, 0, 0, 0},	/*12 */
			{0, 0, 0, 0, 0},	/*13 */
			{0, 0, 0, 0, 0},	/*14 */
			{22, 22, 22, 22, 22},	/*15 */
			{27, 27, 27, 28, 27}	/*16 */
		};
	if (si1 > 16)
		si1 = 0;
	if (si2 > 4)
		si2 = 0;

	return (u16) cip[si1][si2];
}

static inline u8 cip2si1(u16 cipval)
{
	static const u8 si[32] =
		{7, 1, 7, 7, 1, 1, 7, 7,	/*0-7 */
		 7, 1, 0, 0, 0, 0, 0, 0,	/*8-15 */
		 1, 2, 4, 10, 9, 9, 15, 7,	/*16-23 */
		 7, 7, 1, 16, 16, 0, 0, 0};	/*24-31 */

	if (cipval > 31)
		cipval = 0;	/* .... */
	return si[cipval];
}

static inline u8 cip2si2(u16 cipval)
{
	static const u8 si[32] =
		{0, 0, 0, 0, 2, 3, 0, 0,	/*0-7 */
		 0, 3, 0, 0, 0, 0, 0, 0,	/*8-15 */
		 1, 2, 0, 0, 9, 0, 0, 0,	/*16-23 */
		 0, 0, 3, 2, 3, 0, 0, 0};	/*24-31 */

	if (cipval > 31)
		cipval = 0;	/* .... */
	return si[cipval];
}


/* -------- controller management ------------------------------------- */

static inline capidrv_contr *findcontrbydriverid(int driverid)
{
	unsigned long flags;
	capidrv_contr *p;

	spin_lock_irqsave(&global_lock, flags);
	for (p = global.contr_list; p; p = p->next)
		if (p->myid == driverid)
			break;
	spin_unlock_irqrestore(&global_lock, flags);
	return p;
}

static capidrv_contr *findcontrbynumber(u32 contr)
{
	unsigned long flags;
	capidrv_contr *p = global.contr_list;

	spin_lock_irqsave(&global_lock, flags);
	for (p = global.contr_list; p; p = p->next)
		if (p->contrnr == contr)
			break;
	spin_unlock_irqrestore(&global_lock, flags);
	return p;
}


/* -------- plci management ------------------------------------------ */

static capidrv_plci *new_plci(capidrv_contr *card, int chan)
{
	capidrv_plci *plcip;

	plcip = kzalloc(sizeof(capidrv_plci), GFP_ATOMIC);

	if (plcip == NULL)
		return NULL;

	plcip->state = ST_PLCI_NONE;
	plcip->plci = 0;
	plcip->msgid = 0;
	plcip->chan = chan;
	plcip->next = card->plci_list;
	card->plci_list = plcip;
	card->bchans[chan].plcip = plcip;

	return plcip;
}

static capidrv_plci *find_plci_by_plci(capidrv_contr *card, u32 plci)
{
	capidrv_plci *p;
	for (p = card->plci_list; p; p = p->next)
		if (p->plci == plci)
			return p;
	return NULL;
}

static capidrv_plci *find_plci_by_msgid(capidrv_contr *card, u16 msgid)
{
	capidrv_plci *p;
	for (p = card->plci_list; p; p = p->next)
		if (p->msgid == msgid)
			return p;
	return NULL;
}

static capidrv_plci *find_plci_by_ncci(capidrv_contr *card, u32 ncci)
{
	capidrv_plci *p;
	for (p = card->plci_list; p; p = p->next)
		if (p->plci == (ncci & 0xffff))
			return p;
	return NULL;
}

static void free_plci(capidrv_contr *card, capidrv_plci *plcip)
{
	capidrv_plci **pp;

	for (pp = &card->plci_list; *pp; pp = &(*pp)->next) {
		if (*pp == plcip) {
			*pp = (*pp)->next;
			card->bchans[plcip->chan].plcip = NULL;
			card->bchans[plcip->chan].disconnecting = 0;
			card->bchans[plcip->chan].incoming = 0;
			kfree(plcip);
			return;
		}
	}
	printk(KERN_ERR "capidrv-%d: free_plci %p (0x%x) not found, Huh?\n",
	       card->contrnr, plcip, plcip->plci);
}

/* -------- ncci management ------------------------------------------ */

static inline capidrv_ncci *new_ncci(capidrv_contr *card,
				     capidrv_plci *plcip,
				     u32 ncci)
{
	capidrv_ncci *nccip;

	nccip = kzalloc(sizeof(capidrv_ncci), GFP_ATOMIC);

	if (nccip == NULL)
		return NULL;

	nccip->ncci = ncci;
	nccip->state = ST_NCCI_NONE;
	nccip->plcip = plcip;
	nccip->chan = plcip->chan;
	nccip->datahandle = 0;

	nccip->next = plcip->ncci_list;
	plcip->ncci_list = nccip;

	card->bchans[plcip->chan].nccip = nccip;

	return nccip;
}

static inline capidrv_ncci *find_ncci(capidrv_contr *card, u32 ncci)
{
	capidrv_plci *plcip;
	capidrv_ncci *p;

	if ((plcip = find_plci_by_ncci(card, ncci)) == NULL)
		return NULL;

	for (p = plcip->ncci_list; p; p = p->next)
		if (p->ncci == ncci)
			return p;
	return NULL;
}

static inline capidrv_ncci *find_ncci_by_msgid(capidrv_contr *card,
					       u32 ncci, u16 msgid)
{
	capidrv_plci *plcip;
	capidrv_ncci *p;

	if ((plcip = find_plci_by_ncci(card, ncci)) == NULL)
		return NULL;

	for (p = plcip->ncci_list; p; p = p->next)
		if (p->msgid == msgid)
			return p;
	return NULL;
}

static void free_ncci(capidrv_contr *card, struct capidrv_ncci *nccip)
{
	struct capidrv_ncci **pp;

	for (pp = &(nccip->plcip->ncci_list); *pp; pp = &(*pp)->next) {
		if (*pp == nccip) {
			*pp = (*pp)->next;
			break;
		}
	}
	card->bchans[nccip->chan].nccip = NULL;
	kfree(nccip);
}

static int capidrv_add_ack(struct capidrv_ncci *nccip,
			   u16 datahandle, int len)
{
	struct ncci_datahandle_queue *n, **pp;

	n = kmalloc(sizeof(struct ncci_datahandle_queue), GFP_ATOMIC);
	if (!n) {
		printk(KERN_ERR "capidrv: kmalloc ncci_datahandle failed\n");
		return -1;
	}
	n->next = NULL;
	n->datahandle = datahandle;
	n->len = len;
	for (pp = &nccip->ackqueue; *pp; pp = &(*pp)->next);
	*pp = n;
	return 0;
}

static int capidrv_del_ack(struct capidrv_ncci *nccip, u16 datahandle)
{
	struct ncci_datahandle_queue **pp, *p;
	int len;

	for (pp = &nccip->ackqueue; *pp; pp = &(*pp)->next) {
		if ((*pp)->datahandle == datahandle) {
			p = *pp;
			len = p->len;
			*pp = (*pp)->next;
			kfree(p);
			return len;
		}
	}
	return -1;
}

/* -------- convert and send capi message ---------------------------- */

static void send_message(capidrv_contr *card, _cmsg *cmsg)
{
	struct sk_buff *skb;
	size_t len;

	if (capi_cmsg2message(cmsg, cmsg->buf)) {
		printk(KERN_ERR "capidrv::send_message: parser failure\n");
		return;
	}
	len = CAPIMSG_LEN(cmsg->buf);
	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_ERR "capidrv::send_message: can't allocate mem\n");
		return;
	}
	skb_put_data(skb, cmsg->buf, len);
	if (capi20_put_message(&global.ap, skb) != CAPI_NOERROR)
		kfree_skb(skb);
}

/* -------- state machine -------------------------------------------- */

struct listenstatechange {
	int actstate;
	int nextstate;
	int event;
};

static struct listenstatechange listentable[] =
{
	{ST_LISTEN_NONE, ST_LISTEN_WAIT_CONF, EV_LISTEN_REQ},
	{ST_LISTEN_ACTIVE, ST_LISTEN_ACTIVE_WAIT_CONF, EV_LISTEN_REQ},
	{ST_LISTEN_WAIT_CONF, ST_LISTEN_NONE, EV_LISTEN_CONF_ERROR},
	{ST_LISTEN_ACTIVE_WAIT_CONF, ST_LISTEN_ACTIVE, EV_LISTEN_CONF_ERROR},
	{ST_LISTEN_WAIT_CONF, ST_LISTEN_NONE, EV_LISTEN_CONF_EMPTY},
	{ST_LISTEN_ACTIVE_WAIT_CONF, ST_LISTEN_NONE, EV_LISTEN_CONF_EMPTY},
	{ST_LISTEN_WAIT_CONF, ST_LISTEN_ACTIVE, EV_LISTEN_CONF_OK},
	{ST_LISTEN_ACTIVE_WAIT_CONF, ST_LISTEN_ACTIVE, EV_LISTEN_CONF_OK},
	{},
};

static void listen_change_state(capidrv_contr *card, int event)
{
	struct listenstatechange *p = listentable;
	while (p->event) {
		if (card->state == p->actstate && p->event == event) {
			if (debugmode)
				printk(KERN_DEBUG "capidrv-%d: listen_change_state %d -> %d\n",
				       card->contrnr, card->state, p->nextstate);
			card->state = p->nextstate;
			return;
		}
		p++;
	}
	printk(KERN_ERR "capidrv-%d: listen_change_state state=%d event=%d ????\n",
	       card->contrnr, card->state, event);

}

/* ------------------------------------------------------------------ */

static void p0(capidrv_contr *card, capidrv_plci *plci)
{
	isdn_ctrl cmd;

	card->bchans[plci->chan].contr = NULL;
	cmd.command = ISDN_STAT_DHUP;
	cmd.driver = card->myid;
	cmd.arg = plci->chan;
	card->interface.statcallb(&cmd);
	free_plci(card, plci);
}

/* ------------------------------------------------------------------ */

struct plcistatechange {
	int actstate;
	int nextstate;
	int event;
	void (*changefunc)(capidrv_contr *card, capidrv_plci *plci);
};

static struct plcistatechange plcitable[] =
{
	/* P-0 */
	{ST_PLCI_NONE, ST_PLCI_OUTGOING, EV_PLCI_CONNECT_REQ, NULL},
	{ST_PLCI_NONE, ST_PLCI_ALLOCATED, EV_PLCI_FACILITY_IND_UP, NULL},
	{ST_PLCI_NONE, ST_PLCI_INCOMING, EV_PLCI_CONNECT_IND, NULL},
	{ST_PLCI_NONE, ST_PLCI_RESUMEING, EV_PLCI_RESUME_REQ, NULL},
	/* P-0.1 */
	{ST_PLCI_OUTGOING, ST_PLCI_NONE, EV_PLCI_CONNECT_CONF_ERROR, p0},
	{ST_PLCI_OUTGOING, ST_PLCI_ALLOCATED, EV_PLCI_CONNECT_CONF_OK, NULL},
	/* P-1 */
	{ST_PLCI_ALLOCATED, ST_PLCI_ACTIVE, EV_PLCI_CONNECT_ACTIVE_IND, NULL},
	{ST_PLCI_ALLOCATED, ST_PLCI_DISCONNECTING, EV_PLCI_DISCONNECT_REQ, NULL},
	{ST_PLCI_ALLOCATED, ST_PLCI_DISCONNECTING, EV_PLCI_FACILITY_IND_DOWN, NULL},
	{ST_PLCI_ALLOCATED, ST_PLCI_DISCONNECTED, EV_PLCI_DISCONNECT_IND, NULL},
	/* P-ACT */
	{ST_PLCI_ACTIVE, ST_PLCI_DISCONNECTING, EV_PLCI_DISCONNECT_REQ, NULL},
	{ST_PLCI_ACTIVE, ST_PLCI_DISCONNECTING, EV_PLCI_FACILITY_IND_DOWN, NULL},
	{ST_PLCI_ACTIVE, ST_PLCI_DISCONNECTED, EV_PLCI_DISCONNECT_IND, NULL},
	{ST_PLCI_ACTIVE, ST_PLCI_HELD, EV_PLCI_HOLD_IND, NULL},
	{ST_PLCI_ACTIVE, ST_PLCI_DISCONNECTING, EV_PLCI_SUSPEND_IND, NULL},
	/* P-2 */
	{ST_PLCI_INCOMING, ST_PLCI_DISCONNECTING, EV_PLCI_CONNECT_REJECT, NULL},
	{ST_PLCI_INCOMING, ST_PLCI_FACILITY_IND, EV_PLCI_FACILITY_IND_UP, NULL},
	{ST_PLCI_INCOMING, ST_PLCI_ACCEPTING, EV_PLCI_CONNECT_RESP, NULL},
	{ST_PLCI_INCOMING, ST_PLCI_DISCONNECTING, EV_PLCI_DISCONNECT_REQ, NULL},
	{ST_PLCI_INCOMING, ST_PLCI_DISCONNECTING, EV_PLCI_FACILITY_IND_DOWN, NULL},
	{ST_PLCI_INCOMING, ST_PLCI_DISCONNECTED, EV_PLCI_DISCONNECT_IND, NULL},
	{ST_PLCI_INCOMING, ST_PLCI_DISCONNECTING, EV_PLCI_CD_IND, NULL},
	/* P-3 */
	{ST_PLCI_FACILITY_IND, ST_PLCI_DISCONNECTING, EV_PLCI_CONNECT_REJECT, NULL},
	{ST_PLCI_FACILITY_IND, ST_PLCI_ACCEPTING, EV_PLCI_CONNECT_ACTIVE_IND, NULL},
	{ST_PLCI_FACILITY_IND, ST_PLCI_DISCONNECTING, EV_PLCI_DISCONNECT_REQ, NULL},
	{ST_PLCI_FACILITY_IND, ST_PLCI_DISCONNECTING, EV_PLCI_FACILITY_IND_DOWN, NULL},
	{ST_PLCI_FACILITY_IND, ST_PLCI_DISCONNECTED, EV_PLCI_DISCONNECT_IND, NULL},
	/* P-4 */
	{ST_PLCI_ACCEPTING, ST_PLCI_ACTIVE, EV_PLCI_CONNECT_ACTIVE_IND, NULL},
	{ST_PLCI_ACCEPTING, ST_PLCI_DISCONNECTING, EV_PLCI_DISCONNECT_REQ, NULL},
	{ST_PLCI_ACCEPTING, ST_PLCI_DISCONNECTING, EV_PLCI_FACILITY_IND_DOWN, NULL},
	{ST_PLCI_ACCEPTING, ST_PLCI_DISCONNECTED, EV_PLCI_DISCONNECT_IND, NULL},
	/* P-5 */
	{ST_PLCI_DISCONNECTING, ST_PLCI_DISCONNECTED, EV_PLCI_DISCONNECT_IND, NULL},
	/* P-6 */
	{ST_PLCI_DISCONNECTED, ST_PLCI_NONE, EV_PLCI_DISCONNECT_RESP, p0},
	/* P-0.Res */
	{ST_PLCI_RESUMEING, ST_PLCI_NONE, EV_PLCI_RESUME_CONF_ERROR, p0},
	{ST_PLCI_RESUMEING, ST_PLCI_RESUME, EV_PLCI_RESUME_CONF_OK, NULL},
	/* P-RES */
	{ST_PLCI_RESUME, ST_PLCI_ACTIVE, EV_PLCI_RESUME_IND, NULL},
	/* P-HELD */
	{ST_PLCI_HELD, ST_PLCI_ACTIVE, EV_PLCI_RETRIEVE_IND, NULL},
	{},
};

static void plci_change_state(capidrv_contr *card, capidrv_plci *plci, int event)
{
	struct plcistatechange *p = plcitable;
	while (p->event) {
		if (plci->state == p->actstate && p->event == event) {
			if (debugmode)
				printk(KERN_DEBUG "capidrv-%d: plci_change_state:0x%x %d -> %d\n",
				       card->contrnr, plci->plci, plci->state, p->nextstate);
			plci->state = p->nextstate;
			if (p->changefunc)
				p->changefunc(card, plci);
			return;
		}
		p++;
	}
	printk(KERN_ERR "capidrv-%d: plci_change_state:0x%x state=%d event=%d ????\n",
	       card->contrnr, plci->plci, plci->state, event);
}

/* ------------------------------------------------------------------ */

static _cmsg cmsg;

static void n0(capidrv_contr *card, capidrv_ncci *ncci)
{
	isdn_ctrl cmd;

	capi_fill_DISCONNECT_REQ(&cmsg,
				 global.ap.applid,
				 card->msgid++,
				 ncci->plcip->plci,
				 NULL,	/* BChannelinformation */
				 NULL,	/* Keypadfacility */
				 NULL,	/* Useruserdata */   /* $$$$ */
				 NULL	/* Facilitydataarray */
		);
	plci_change_state(card, ncci->plcip, EV_PLCI_DISCONNECT_REQ);
	send_message(card, &cmsg);

	cmd.command = ISDN_STAT_BHUP;
	cmd.driver = card->myid;
	cmd.arg = ncci->chan;
	card->interface.statcallb(&cmd);
	free_ncci(card, ncci);
}

/* ------------------------------------------------------------------ */

struct nccistatechange {
	int actstate;
	int nextstate;
	int event;
	void (*changefunc)(capidrv_contr *card, capidrv_ncci *ncci);
};

static struct nccistatechange nccitable[] =
{
	/* N-0 */
	{ST_NCCI_NONE, ST_NCCI_OUTGOING, EV_NCCI_CONNECT_B3_REQ, NULL},
	{ST_NCCI_NONE, ST_NCCI_INCOMING, EV_NCCI_CONNECT_B3_IND, NULL},
	/* N-0.1 */
	{ST_NCCI_OUTGOING, ST_NCCI_ALLOCATED, EV_NCCI_CONNECT_B3_CONF_OK, NULL},
	{ST_NCCI_OUTGOING, ST_NCCI_NONE, EV_NCCI_CONNECT_B3_CONF_ERROR, n0},
	/* N-1 */
	{ST_NCCI_INCOMING, ST_NCCI_DISCONNECTING, EV_NCCI_CONNECT_B3_REJECT, NULL},
	{ST_NCCI_INCOMING, ST_NCCI_ALLOCATED, EV_NCCI_CONNECT_B3_RESP, NULL},
	{ST_NCCI_INCOMING, ST_NCCI_DISCONNECTED, EV_NCCI_DISCONNECT_B3_IND, NULL},
	{ST_NCCI_INCOMING, ST_NCCI_DISCONNECTING, EV_NCCI_DISCONNECT_B3_REQ, NULL},
	/* N-2 */
	{ST_NCCI_ALLOCATED, ST_NCCI_ACTIVE, EV_NCCI_CONNECT_B3_ACTIVE_IND, NULL},
	{ST_NCCI_ALLOCATED, ST_NCCI_DISCONNECTED, EV_NCCI_DISCONNECT_B3_IND, NULL},
	{ST_NCCI_ALLOCATED, ST_NCCI_DISCONNECTING, EV_NCCI_DISCONNECT_B3_REQ, NULL},
	/* N-ACT */
	{ST_NCCI_ACTIVE, ST_NCCI_ACTIVE, EV_NCCI_RESET_B3_IND, NULL},
	{ST_NCCI_ACTIVE, ST_NCCI_RESETING, EV_NCCI_RESET_B3_REQ, NULL},
	{ST_NCCI_ACTIVE, ST_NCCI_DISCONNECTED, EV_NCCI_DISCONNECT_B3_IND, NULL},
	{ST_NCCI_ACTIVE, ST_NCCI_DISCONNECTING, EV_NCCI_DISCONNECT_B3_REQ, NULL},
	/* N-3 */
	{ST_NCCI_RESETING, ST_NCCI_ACTIVE, EV_NCCI_RESET_B3_IND, NULL},
	{ST_NCCI_RESETING, ST_NCCI_DISCONNECTED, EV_NCCI_DISCONNECT_B3_IND, NULL},
	{ST_NCCI_RESETING, ST_NCCI_DISCONNECTING, EV_NCCI_DISCONNECT_B3_REQ, NULL},
	/* N-4 */
	{ST_NCCI_DISCONNECTING, ST_NCCI_DISCONNECTED, EV_NCCI_DISCONNECT_B3_IND, NULL},
	{ST_NCCI_DISCONNECTING, ST_NCCI_PREVIOUS, EV_NCCI_DISCONNECT_B3_CONF_ERROR, NULL},
	/* N-5 */
	{ST_NCCI_DISCONNECTED, ST_NCCI_NONE, EV_NCCI_DISCONNECT_B3_RESP, n0},
	{},
};

static void ncci_change_state(capidrv_contr *card, capidrv_ncci *ncci, int event)
{
	struct nccistatechange *p = nccitable;
	while (p->event) {
		if (ncci->state == p->actstate && p->event == event) {
			if (debugmode)
				printk(KERN_DEBUG "capidrv-%d: ncci_change_state:0x%x %d -> %d\n",
				       card->contrnr, ncci->ncci, ncci->state, p->nextstate);
			if (p->nextstate == ST_NCCI_PREVIOUS) {
				ncci->state = ncci->oldstate;
				ncci->oldstate = p->actstate;
			} else {
				ncci->oldstate = p->actstate;
				ncci->state = p->nextstate;
			}
			if (p->changefunc)
				p->changefunc(card, ncci);
			return;
		}
		p++;
	}
	printk(KERN_ERR "capidrv-%d: ncci_change_state:0x%x state=%d event=%d ????\n",
	       card->contrnr, ncci->ncci, ncci->state, event);
}

/* ------------------------------------------------------------------- */

static inline int new_bchan(capidrv_contr *card)
{
	int i;
	for (i = 0; i < card->nbchan; i++) {
		if (card->bchans[i].plcip == NULL) {
			card->bchans[i].disconnecting = 0;
			return i;
		}
	}
	return -1;
}

/* ------------------------------------------------------------------- */
static char *capi_info2str(u16 reason)
{
#ifndef CONFIG_ISDN_CAPI_CAPIDRV_VERBOSE
	return "..";
#else
	switch (reason) {

/*-- informative values (corresponding message was processed) -----*/
	case 0x0001:
		return "NCPI not supported by current protocol, NCPI ignored";
	case 0x0002:
		return "Flags not supported by current protocol, flags ignored";
	case 0x0003:
		return "Alert already sent by another application";

/*-- error information concerning CAPI_REGISTER -----*/
	case 0x1001:
		return "Too many applications";
	case 0x1002:
		return "Logical block size too small, must be at least 128 Bytes";
	case 0x1003:
		return "Buffer exceeds 64 kByte";
	case 0x1004:
		return "Message buffer size too small, must be at least 1024 Bytes";
	case 0x1005:
		return "Max. number of logical connections not supported";
	case 0x1006:
		return "Reserved";
	case 0x1007:
		return "The message could not be accepted because of an internal busy condition";
	case 0x1008:
		return "OS resource error (no memory ?)";
	case 0x1009:
		return "CAPI not installed";
	case 0x100A:
		return "Controller does not support external equipment";
	case 0x100B:
		return "Controller does only support external equipment";

/*-- error information concerning message exchange functions -----*/
	case 0x1101:
		return "Illegal application number";
	case 0x1102:
		return "Illegal command or subcommand or message length less than 12 bytes";
	case 0x1103:
		return "The message could not be accepted because of a queue full condition !! The error code does not imply that CAPI cannot receive messages directed to another controller, PLCI or NCCI";
	case 0x1104:
		return "Queue is empty";
	case 0x1105:
		return "Queue overflow, a message was lost !! This indicates a configuration error. The only recovery from this error is to perform a CAPI_RELEASE";
	case 0x1106:
		return "Unknown notification parameter";
	case 0x1107:
		return "The Message could not be accepted because of an internal busy condition";
	case 0x1108:
		return "OS Resource error (no memory ?)";
	case 0x1109:
		return "CAPI not installed";
	case 0x110A:
		return "Controller does not support external equipment";
	case 0x110B:
		return "Controller does only support external equipment";

/*-- error information concerning resource / coding problems -----*/
	case 0x2001:
		return "Message not supported in current state";
	case 0x2002:
		return "Illegal Controller / PLCI / NCCI";
	case 0x2003:
		return "Out of PLCI";
	case 0x2004:
		return "Out of NCCI";
	case 0x2005:
		return "Out of LISTEN";
	case 0x2006:
		return "Out of FAX resources (protocol T.30)";
	case 0x2007:
		return "Illegal message parameter coding";

/*-- error information concerning requested services  -----*/
	case 0x3001:
		return "B1 protocol not supported";
	case 0x3002:
		return "B2 protocol not supported";
	case 0x3003:
		return "B3 protocol not supported";
	case 0x3004:
		return "B1 protocol parameter not supported";
	case 0x3005:
		return "B2 protocol parameter not supported";
	case 0x3006:
		return "B3 protocol parameter not supported";
	case 0x3007:
		return "B protocol combination not supported";
	case 0x3008:
		return "NCPI not supported";
	case 0x3009:
		return "CIP Value unknown";
	case 0x300A:
		return "Flags not supported (reserved bits)";
	case 0x300B:
		return "Facility not supported";
	case 0x300C:
		return "Data length not supported by current protocol";
	case 0x300D:
		return "Reset procedure not supported by current protocol";

/*-- informations about the clearing of a physical connection -----*/
	case 0x3301:
		return "Protocol error layer 1 (broken line or B-channel removed by signalling protocol)";
	case 0x3302:
		return "Protocol error layer 2";
	case 0x3303:
		return "Protocol error layer 3";
	case 0x3304:
		return "Another application got that call";
/*-- T.30 specific reasons -----*/
	case 0x3311:
		return "Connecting not successful (remote station is no FAX G3 machine)";
	case 0x3312:
		return "Connecting not successful (training error)";
	case 0x3313:
		return "Disconnected before transfer (remote station does not support transfer mode, e.g. resolution)";
	case 0x3314:
		return "Disconnected during transfer (remote abort)";
	case 0x3315:
		return "Disconnected during transfer (remote procedure error, e.g. unsuccessful repetition of T.30 commands)";
	case 0x3316:
		return "Disconnected during transfer (local tx data underrun)";
	case 0x3317:
		return "Disconnected during transfer (local rx data overflow)";
	case 0x3318:
		return "Disconnected during transfer (local abort)";
	case 0x3319:
		return "Illegal parameter coding (e.g. SFF coding error)";

/*-- disconnect causes from the network according to ETS 300 102-1/Q.931 -----*/
	case 0x3481: return "Unallocated (unassigned) number";
	case 0x3482: return "No route to specified transit network";
	case 0x3483: return "No route to destination";
	case 0x3486: return "Channel unacceptable";
	case 0x3487:
		return "Call awarded and being delivered in an established channel";
	case 0x3490: return "Normal call clearing";
	case 0x3491: return "User busy";
	case 0x3492: return "No user responding";
	case 0x3493: return "No answer from user (user alerted)";
	case 0x3495: return "Call rejected";
	case 0x3496: return "Number changed";
	case 0x349A: return "Non-selected user clearing";
	case 0x349B: return "Destination out of order";
	case 0x349C: return "Invalid number format";
	case 0x349D: return "Facility rejected";
	case 0x349E: return "Response to STATUS ENQUIRY";
	case 0x349F: return "Normal, unspecified";
	case 0x34A2: return "No circuit / channel available";
	case 0x34A6: return "Network out of order";
	case 0x34A9: return "Temporary failure";
	case 0x34AA: return "Switching equipment congestion";
	case 0x34AB: return "Access information discarded";
	case 0x34AC: return "Requested circuit / channel not available";
	case 0x34AF: return "Resources unavailable, unspecified";
	case 0x34B1: return "Quality of service unavailable";
	case 0x34B2: return "Requested facility not subscribed";
	case 0x34B9: return "Bearer capability not authorized";
	case 0x34BA: return "Bearer capability not presently available";
	case 0x34BF: return "Service or option not available, unspecified";
	case 0x34C1: return "Bearer capability not implemented";
	case 0x34C2: return "Channel type not implemented";
	case 0x34C5: return "Requested facility not implemented";
	case 0x34C6: return "Only restricted digital information bearer capability is available";
	case 0x34CF: return "Service or option not implemented, unspecified";
	case 0x34D1: return "Invalid call reference value";
	case 0x34D2: return "Identified channel does not exist";
	case 0x34D3: return "A suspended call exists, but this call identity does not";
	case 0x34D4: return "Call identity in use";
	case 0x34D5: return "No call suspended";
	case 0x34D6: return "Call having the requested call identity has been cleared";
	case 0x34D8: return "Incompatible destination";
	case 0x34DB: return "Invalid transit network selection";
	case 0x34DF: return "Invalid message, unspecified";
	case 0x34E0: return "Mandatory information element is missing";
	case 0x34E1: return "Message type non-existent or not implemented";
	case 0x34E2: return "Message not compatible with call state or message type non-existent or not implemented";
	case 0x34E3: return "Information element non-existent or not implemented";
	case 0x34E4: return "Invalid information element contents";
	case 0x34E5: return "Message not compatible with call state";
	case 0x34E6: return "Recovery on timer expiry";
	case 0x34EF: return "Protocol error, unspecified";
	case 0x34FF: return "Interworking, unspecified";

	default: return "No additional information";
	}
#endif
}

static void handle_controller(_cmsg *cmsg)
{
	capidrv_contr *card = findcontrbynumber(cmsg->adr.adrController & 0x7f);

	if (!card) {
		printk(KERN_ERR "capidrv: %s from unknown controller 0x%x\n",
		       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
		       cmsg->adr.adrController & 0x7f);
		return;
	}
	switch (CAPICMD(cmsg->Command, cmsg->Subcommand)) {

	case CAPI_LISTEN_CONF:	/* Controller */
		if (debugmode)
			printk(KERN_DEBUG "capidrv-%d: listenconf Info=0x%4x (%s) cipmask=0x%x\n",
			       card->contrnr, cmsg->Info, capi_info2str(cmsg->Info), card->cipmask);
		if (cmsg->Info) {
			listen_change_state(card, EV_LISTEN_CONF_ERROR);
		} else if (card->cipmask == 0) {
			listen_change_state(card, EV_LISTEN_CONF_EMPTY);
		} else {
			listen_change_state(card, EV_LISTEN_CONF_OK);
		}
		break;

	case CAPI_MANUFACTURER_IND:	/* Controller */
		if (cmsg->ManuID == 0x214D5641
		    && cmsg->Class == 0
		    && cmsg->Function == 1) {
			u8  *data = cmsg->ManuData + 3;
			u16  len = cmsg->ManuData[0];
			u16 layer;
			int direction;
			if (len == 255) {
				len = (cmsg->ManuData[1] | (cmsg->ManuData[2] << 8));
				data += 2;
			}
			len -= 2;
			layer = ((*(data - 1)) << 8) | *(data - 2);
			if (layer & 0x300)
				direction = (layer & 0x200) ? 0 : 1;
			else direction = (layer & 0x800) ? 0 : 1;
			if (layer & 0x0C00) {
				if ((layer & 0xff) == 0x80) {
					handle_dtrace_data(card, direction, 1, data, len);
					break;
				}
			} else if ((layer & 0xff) < 0x80) {
				handle_dtrace_data(card, direction, 0, data, len);
				break;
			}
			printk(KERN_INFO "capidrv-%d: %s from controller 0x%x layer 0x%x, ignored\n",
			       card->contrnr,
			       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
			       cmsg->adr.adrController, layer);
			break;
		}
		goto ignored;
	case CAPI_MANUFACTURER_CONF:	/* Controller */
		if (cmsg->ManuID == 0x214D5641) {
			char *s = NULL;
			switch (cmsg->Class) {
			case 0: break;
			case 1: s = "unknown class"; break;
			case 2: s = "unknown function"; break;
			default: s = "unknown error"; break;
			}
			if (s)
				printk(KERN_INFO "capidrv-%d: %s from controller 0x%x function %d: %s\n",
				       card->contrnr,
				       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
				       cmsg->adr.adrController,
				       cmsg->Function, s);
			break;
		}
		goto ignored;
	case CAPI_FACILITY_IND:	/* Controller/plci/ncci */
		goto ignored;
	case CAPI_FACILITY_CONF:	/* Controller/plci/ncci */
		goto ignored;
	case CAPI_INFO_IND:	/* Controller/plci */
		goto ignored;
	case CAPI_INFO_CONF:	/* Controller/plci */
		goto ignored;

	default:
		printk(KERN_ERR "capidrv-%d: got %s from controller 0x%x ???",
		       card->contrnr,
		       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
		       cmsg->adr.adrController);
	}
	return;

ignored:
	printk(KERN_INFO "capidrv-%d: %s from controller 0x%x ignored\n",
	       card->contrnr,
	       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
	       cmsg->adr.adrController);
}

static void handle_incoming_call(capidrv_contr *card, _cmsg *cmsg)
{
	capidrv_plci *plcip;
	capidrv_bchan *bchan;
	isdn_ctrl cmd;
	int chan;

	if ((chan = new_bchan(card)) == -1) {
		printk(KERN_ERR "capidrv-%d: incoming call on not existing bchan ?\n", card->contrnr);
		return;
	}
	bchan = &card->bchans[chan];
	if ((plcip = new_plci(card, chan)) == NULL) {
		printk(KERN_ERR "capidrv-%d: incoming call: no memory, sorry.\n", card->contrnr);
		return;
	}
	bchan->incoming = 1;
	plcip->plci = cmsg->adr.adrPLCI;
	plci_change_state(card, plcip, EV_PLCI_CONNECT_IND);

	cmd.command = ISDN_STAT_ICALL;
	cmd.driver = card->myid;
	cmd.arg = chan;
	memset(&cmd.parm.setup, 0, sizeof(cmd.parm.setup));
	strncpy(cmd.parm.setup.phone,
		cmsg->CallingPartyNumber + 3,
		cmsg->CallingPartyNumber[0] - 2);
	strncpy(cmd.parm.setup.eazmsn,
		cmsg->CalledPartyNumber + 2,
		cmsg->CalledPartyNumber[0] - 1);
	cmd.parm.setup.si1 = cip2si1(cmsg->CIPValue);
	cmd.parm.setup.si2 = cip2si2(cmsg->CIPValue);
	cmd.parm.setup.plan = cmsg->CallingPartyNumber[1];
	cmd.parm.setup.screen = cmsg->CallingPartyNumber[2];

	printk(KERN_INFO "capidrv-%d: incoming call %s,%d,%d,%s\n",
	       card->contrnr,
	       cmd.parm.setup.phone,
	       cmd.parm.setup.si1,
	       cmd.parm.setup.si2,
	       cmd.parm.setup.eazmsn);

	if (cmd.parm.setup.si1 == 1 && cmd.parm.setup.si2 != 0) {
		printk(KERN_INFO "capidrv-%d: patching si2=%d to 0 for VBOX\n",
		       card->contrnr,
		       cmd.parm.setup.si2);
		cmd.parm.setup.si2 = 0;
	}

	switch (card->interface.statcallb(&cmd)) {
	case 0:
	case 3:
		/* No device matching this call.
		 * and isdn_common.c has send a HANGUP command
		 * which is ignored in state ST_PLCI_INCOMING,
		 * so we send RESP to ignore the call
		 */
		capi_cmsg_answer(cmsg);
		cmsg->Reject = 1;	/* ignore */
		plci_change_state(card, plcip, EV_PLCI_CONNECT_REJECT);
		send_message(card, cmsg);
		printk(KERN_INFO "capidrv-%d: incoming call %s,%d,%d,%s ignored\n",
		       card->contrnr,
		       cmd.parm.setup.phone,
		       cmd.parm.setup.si1,
		       cmd.parm.setup.si2,
		       cmd.parm.setup.eazmsn);
		break;
	case 1:
		/* At least one device matching this call (RING on ttyI)
		 * HL-driver may send ALERTING on the D-channel in this
		 * case.
		 * really means: RING on ttyI or a net interface
		 * accepted this call already.
		 *
		 * If the call was accepted, state has already changed,
		 * and CONNECT_RESP already sent.
		 */
		if (plcip->state == ST_PLCI_INCOMING) {
			printk(KERN_INFO "capidrv-%d: incoming call %s,%d,%d,%s tty alerting\n",
			       card->contrnr,
			       cmd.parm.setup.phone,
			       cmd.parm.setup.si1,
			       cmd.parm.setup.si2,
			       cmd.parm.setup.eazmsn);
			capi_fill_ALERT_REQ(cmsg,
					    global.ap.applid,
					    card->msgid++,
					    plcip->plci,	/* adr */
					    NULL,/* BChannelinformation */
					    NULL,/* Keypadfacility */
					    NULL,/* Useruserdata */
					    NULL /* Facilitydataarray */
				);
			plcip->msgid = cmsg->Messagenumber;
			send_message(card, cmsg);
		} else {
			printk(KERN_INFO "capidrv-%d: incoming call %s,%d,%d,%s on netdev\n",
			       card->contrnr,
			       cmd.parm.setup.phone,
			       cmd.parm.setup.si1,
			       cmd.parm.setup.si2,
			       cmd.parm.setup.eazmsn);
		}
		break;

	case 2:		/* Call will be rejected. */
		capi_cmsg_answer(cmsg);
		cmsg->Reject = 2;	/* reject call, normal call clearing */
		plci_change_state(card, plcip, EV_PLCI_CONNECT_REJECT);
		send_message(card, cmsg);
		break;

	default:
		/* An error happened. (Invalid parameters for example.) */
		capi_cmsg_answer(cmsg);
		cmsg->Reject = 8;	/* reject call,
					   destination out of order */
		plci_change_state(card, plcip, EV_PLCI_CONNECT_REJECT);
		send_message(card, cmsg);
		break;
	}
	return;
}

static void handle_plci(_cmsg *cmsg)
{
	capidrv_contr *card = findcontrbynumber(cmsg->adr.adrController & 0x7f);
	capidrv_plci *plcip;
	isdn_ctrl cmd;
	_cdebbuf *cdb;

	if (!card) {
		printk(KERN_ERR "capidrv: %s from unknown controller 0x%x\n",
		       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
		       cmsg->adr.adrController & 0x7f);
		return;
	}
	switch (CAPICMD(cmsg->Command, cmsg->Subcommand)) {

	case CAPI_DISCONNECT_IND:	/* plci */
		if (cmsg->Reason) {
			printk(KERN_INFO "capidrv-%d: %s reason 0x%x (%s) for plci 0x%x\n",
			       card->contrnr,
			       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
			       cmsg->Reason, capi_info2str(cmsg->Reason), cmsg->adr.adrPLCI);
		}
		if (!(plcip = find_plci_by_plci(card, cmsg->adr.adrPLCI))) {
			capi_cmsg_answer(cmsg);
			send_message(card, cmsg);
			goto notfound;
		}
		card->bchans[plcip->chan].disconnecting = 1;
		plci_change_state(card, plcip, EV_PLCI_DISCONNECT_IND);
		capi_cmsg_answer(cmsg);
		plci_change_state(card, plcip, EV_PLCI_DISCONNECT_RESP);
		send_message(card, cmsg);
		break;

	case CAPI_DISCONNECT_CONF:	/* plci */
		if (cmsg->Info) {
			printk(KERN_INFO "capidrv-%d: %s info 0x%x (%s) for plci 0x%x\n",
			       card->contrnr,
			       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
			       cmsg->Info, capi_info2str(cmsg->Info),
			       cmsg->adr.adrPLCI);
		}
		if (!(plcip = find_plci_by_plci(card, cmsg->adr.adrPLCI)))
			goto notfound;

		card->bchans[plcip->chan].disconnecting = 1;
		break;

	case CAPI_ALERT_CONF:	/* plci */
		if (cmsg->Info) {
			printk(KERN_INFO "capidrv-%d: %s info 0x%x (%s) for plci 0x%x\n",
			       card->contrnr,
			       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
			       cmsg->Info, capi_info2str(cmsg->Info),
			       cmsg->adr.adrPLCI);
		}
		break;

	case CAPI_CONNECT_IND:	/* plci */
		handle_incoming_call(card, cmsg);
		break;

	case CAPI_CONNECT_CONF:	/* plci */
		if (cmsg->Info) {
			printk(KERN_INFO "capidrv-%d: %s info 0x%x (%s) for plci 0x%x\n",
			       card->contrnr,
			       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
			       cmsg->Info, capi_info2str(cmsg->Info),
			       cmsg->adr.adrPLCI);
		}
		if (!(plcip = find_plci_by_msgid(card, cmsg->Messagenumber)))
			goto notfound;

		plcip->plci = cmsg->adr.adrPLCI;
		if (cmsg->Info) {
			plci_change_state(card, plcip, EV_PLCI_CONNECT_CONF_ERROR);
		} else {
			plci_change_state(card, plcip, EV_PLCI_CONNECT_CONF_OK);
		}
		break;

	case CAPI_CONNECT_ACTIVE_IND:	/* plci */

		if (!(plcip = find_plci_by_plci(card, cmsg->adr.adrPLCI)))
			goto notfound;

		if (card->bchans[plcip->chan].incoming) {
			capi_cmsg_answer(cmsg);
			plci_change_state(card, plcip, EV_PLCI_CONNECT_ACTIVE_IND);
			send_message(card, cmsg);
		} else {
			capidrv_ncci *nccip;
			capi_cmsg_answer(cmsg);
			send_message(card, cmsg);

			nccip = new_ncci(card, plcip, cmsg->adr.adrPLCI);

			if (!nccip) {
				printk(KERN_ERR "capidrv-%d: no mem for ncci, sorry\n", card->contrnr);
				break;	/* $$$$ */
			}
			capi_fill_CONNECT_B3_REQ(cmsg,
						 global.ap.applid,
						 card->msgid++,
						 plcip->plci,	/* adr */
						 NULL	/* NCPI */
				);
			nccip->msgid = cmsg->Messagenumber;
			plci_change_state(card, plcip,
					  EV_PLCI_CONNECT_ACTIVE_IND);
			ncci_change_state(card, nccip, EV_NCCI_CONNECT_B3_REQ);
			send_message(card, cmsg);
			cmd.command = ISDN_STAT_DCONN;
			cmd.driver = card->myid;
			cmd.arg = plcip->chan;
			card->interface.statcallb(&cmd);
		}
		break;

	case CAPI_INFO_IND:	/* Controller/plci */

		if (!(plcip = find_plci_by_plci(card, cmsg->adr.adrPLCI)))
			goto notfound;

		if (cmsg->InfoNumber == 0x4000) {
			if (cmsg->InfoElement[0] == 4) {
				cmd.command = ISDN_STAT_CINF;
				cmd.driver = card->myid;
				cmd.arg = plcip->chan;
				sprintf(cmd.parm.num, "%lu",
					(unsigned long)
					((u32) cmsg->InfoElement[1]
					 | ((u32) (cmsg->InfoElement[2]) << 8)
					 | ((u32) (cmsg->InfoElement[3]) << 16)
					 | ((u32) (cmsg->InfoElement[4]) << 24)));
				card->interface.statcallb(&cmd);
				break;
			}
		}
		cdb = capi_cmsg2str(cmsg);
		if (cdb) {
			printk(KERN_WARNING "capidrv-%d: %s\n",
			       card->contrnr, cdb->buf);
			cdebbuf_free(cdb);
		} else
			printk(KERN_WARNING "capidrv-%d: CAPI_INFO_IND InfoNumber %x not handled\n",
			       card->contrnr, cmsg->InfoNumber);

		break;

	case CAPI_CONNECT_ACTIVE_CONF:		/* plci */
		goto ignored;
	case CAPI_SELECT_B_PROTOCOL_CONF:	/* plci */
		goto ignored;
	case CAPI_FACILITY_IND:	/* Controller/plci/ncci */
		goto ignored;
	case CAPI_FACILITY_CONF:	/* Controller/plci/ncci */
		goto ignored;

	case CAPI_INFO_CONF:	/* Controller/plci */
		goto ignored;

	default:
		printk(KERN_ERR "capidrv-%d: got %s for plci 0x%x ???",
		       card->contrnr,
		       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
		       cmsg->adr.adrPLCI);
	}
	return;
ignored:
	printk(KERN_INFO "capidrv-%d: %s for plci 0x%x ignored\n",
	       card->contrnr,
	       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
	       cmsg->adr.adrPLCI);
	return;
notfound:
	printk(KERN_ERR "capidrv-%d: %s: plci 0x%x not found\n",
	       card->contrnr,
	       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
	       cmsg->adr.adrPLCI);
	return;
}

static void handle_ncci(_cmsg *cmsg)
{
	capidrv_contr *card = findcontrbynumber(cmsg->adr.adrController & 0x7f);
	capidrv_plci *plcip;
	capidrv_ncci *nccip;
	isdn_ctrl cmd;
	int len;

	if (!card) {
		printk(KERN_ERR "capidrv: %s from unknown controller 0x%x\n",
		       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
		       cmsg->adr.adrController & 0x7f);
		return;
	}
	switch (CAPICMD(cmsg->Command, cmsg->Subcommand)) {

	case CAPI_CONNECT_B3_ACTIVE_IND:	/* ncci */
		if (!(nccip = find_ncci(card, cmsg->adr.adrNCCI)))
			goto notfound;

		capi_cmsg_answer(cmsg);
		ncci_change_state(card, nccip, EV_NCCI_CONNECT_B3_ACTIVE_IND);
		send_message(card, cmsg);

		cmd.command = ISDN_STAT_BCONN;
		cmd.driver = card->myid;
		cmd.arg = nccip->chan;
		card->interface.statcallb(&cmd);

		printk(KERN_INFO "capidrv-%d: chan %d up with ncci 0x%x\n",
		       card->contrnr, nccip->chan, nccip->ncci);
		break;

	case CAPI_CONNECT_B3_ACTIVE_CONF:	/* ncci */
		goto ignored;

	case CAPI_CONNECT_B3_IND:	/* ncci */

		plcip = find_plci_by_ncci(card, cmsg->adr.adrNCCI);
		if (plcip) {
			nccip = new_ncci(card, plcip, cmsg->adr.adrNCCI);
			if (nccip) {
				ncci_change_state(card, nccip, EV_NCCI_CONNECT_B3_IND);
				capi_fill_CONNECT_B3_RESP(cmsg,
							  global.ap.applid,
							  card->msgid++,
							  nccip->ncci,	/* adr */
							  0,	/* Reject */
							  NULL	/* NCPI */
					);
				ncci_change_state(card, nccip, EV_NCCI_CONNECT_B3_RESP);
				send_message(card, cmsg);
				break;
			}
			printk(KERN_ERR "capidrv-%d: no mem for ncci, sorry\n",							card->contrnr);
		} else {
			printk(KERN_ERR "capidrv-%d: %s: plci for ncci 0x%x not found\n",
			       card->contrnr,
			       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
			       cmsg->adr.adrNCCI);
		}
		capi_fill_CONNECT_B3_RESP(cmsg,
					  global.ap.applid,
					  card->msgid++,
					  cmsg->adr.adrNCCI,
					  2,	/* Reject */
					  NULL	/* NCPI */
			);
		send_message(card, cmsg);
		break;

	case CAPI_CONNECT_B3_CONF:	/* ncci */

		if (!(nccip = find_ncci_by_msgid(card,
						 cmsg->adr.adrNCCI,
						 cmsg->Messagenumber)))
			goto notfound;

		nccip->ncci = cmsg->adr.adrNCCI;
		if (cmsg->Info) {
			printk(KERN_INFO "capidrv-%d: %s info 0x%x (%s) for ncci 0x%x\n",
			       card->contrnr,
			       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
			       cmsg->Info, capi_info2str(cmsg->Info),
			       cmsg->adr.adrNCCI);
		}

		if (cmsg->Info)
			ncci_change_state(card, nccip, EV_NCCI_CONNECT_B3_CONF_ERROR);
		else
			ncci_change_state(card, nccip, EV_NCCI_CONNECT_B3_CONF_OK);
		break;

	case CAPI_CONNECT_B3_T90_ACTIVE_IND:	/* ncci */
		capi_cmsg_answer(cmsg);
		send_message(card, cmsg);
		break;

	case CAPI_DATA_B3_IND:	/* ncci */
		/* handled in handle_data() */
		goto ignored;

	case CAPI_DATA_B3_CONF:	/* ncci */
		if (cmsg->Info) {
			printk(KERN_WARNING "CAPI_DATA_B3_CONF: Info %x - %s\n",
			       cmsg->Info, capi_info2str(cmsg->Info));
		}
		if (!(nccip = find_ncci(card, cmsg->adr.adrNCCI)))
			goto notfound;

		len = capidrv_del_ack(nccip, cmsg->DataHandle);
		if (len < 0)
			break;
		cmd.command = ISDN_STAT_BSENT;
		cmd.driver = card->myid;
		cmd.arg = nccip->chan;
		cmd.parm.length = len;
		card->interface.statcallb(&cmd);
		break;

	case CAPI_DISCONNECT_B3_IND:	/* ncci */
		if (!(nccip = find_ncci(card, cmsg->adr.adrNCCI)))
			goto notfound;

		card->bchans[nccip->chan].disconnecting = 1;
		ncci_change_state(card, nccip, EV_NCCI_DISCONNECT_B3_IND);
		capi_cmsg_answer(cmsg);
		ncci_change_state(card, nccip, EV_NCCI_DISCONNECT_B3_RESP);
		send_message(card, cmsg);
		break;

	case CAPI_DISCONNECT_B3_CONF:	/* ncci */
		if (!(nccip = find_ncci(card, cmsg->adr.adrNCCI)))
			goto notfound;
		if (cmsg->Info) {
			printk(KERN_INFO "capidrv-%d: %s info 0x%x (%s) for ncci 0x%x\n",
			       card->contrnr,
			       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
			       cmsg->Info, capi_info2str(cmsg->Info),
			       cmsg->adr.adrNCCI);
			ncci_change_state(card, nccip, EV_NCCI_DISCONNECT_B3_CONF_ERROR);
		}
		break;

	case CAPI_RESET_B3_IND:	/* ncci */
		if (!(nccip = find_ncci(card, cmsg->adr.adrNCCI)))
			goto notfound;
		ncci_change_state(card, nccip, EV_NCCI_RESET_B3_IND);
		capi_cmsg_answer(cmsg);
		send_message(card, cmsg);
		break;

	case CAPI_RESET_B3_CONF:	/* ncci */
		goto ignored;	/* $$$$ */

	case CAPI_FACILITY_IND:	/* Controller/plci/ncci */
		goto ignored;
	case CAPI_FACILITY_CONF:	/* Controller/plci/ncci */
		goto ignored;

	default:
		printk(KERN_ERR "capidrv-%d: got %s for ncci 0x%x ???",
		       card->contrnr,
		       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
		       cmsg->adr.adrNCCI);
	}
	return;
ignored:
	printk(KERN_INFO "capidrv-%d: %s for ncci 0x%x ignored\n",
	       card->contrnr,
	       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
	       cmsg->adr.adrNCCI);
	return;
notfound:
	printk(KERN_ERR "capidrv-%d: %s: ncci 0x%x not found\n",
	       card->contrnr,
	       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
	       cmsg->adr.adrNCCI);
}


static void handle_data(_cmsg *cmsg, struct sk_buff *skb)
{
	capidrv_contr *card = findcontrbynumber(cmsg->adr.adrController & 0x7f);
	capidrv_ncci *nccip;

	if (!card) {
		printk(KERN_ERR "capidrv: %s from unknown controller 0x%x\n",
		       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
		       cmsg->adr.adrController & 0x7f);
		kfree_skb(skb);
		return;
	}
	if (!(nccip = find_ncci(card, cmsg->adr.adrNCCI))) {
		printk(KERN_ERR "capidrv-%d: %s: ncci 0x%x not found\n",
		       card->contrnr,
		       capi_cmd2str(cmsg->Command, cmsg->Subcommand),
		       cmsg->adr.adrNCCI);
		kfree_skb(skb);
		return;
	}
	(void) skb_pull(skb, CAPIMSG_LEN(skb->data));
	card->interface.rcvcallb_skb(card->myid, nccip->chan, skb);
	capi_cmsg_answer(cmsg);
	send_message(card, cmsg);
}

static _cmsg s_cmsg;

static void capidrv_recv_message(struct capi20_appl *ap, struct sk_buff *skb)
{
	if (capi_message2cmsg(&s_cmsg, skb->data)) {
		printk(KERN_ERR "capidrv: applid=%d: received invalid message\n",
		       ap->applid);
		kfree_skb(skb);
		return;
	}
	if (debugmode > 3) {
		_cdebbuf *cdb = capi_cmsg2str(&s_cmsg);

		if (cdb) {
			printk(KERN_DEBUG "%s: applid=%d %s\n", __func__,
			       ap->applid, cdb->buf);
			cdebbuf_free(cdb);
		} else
			printk(KERN_DEBUG "%s: applid=%d %s not traced\n",
			       __func__, ap->applid,
			       capi_cmd2str(s_cmsg.Command, s_cmsg.Subcommand));
	}
	if (s_cmsg.Command == CAPI_DATA_B3
	    && s_cmsg.Subcommand == CAPI_IND) {
		handle_data(&s_cmsg, skb);
		return;
	}
	if ((s_cmsg.adr.adrController & 0xffffff00) == 0)
		handle_controller(&s_cmsg);
	else if ((s_cmsg.adr.adrPLCI & 0xffff0000) == 0)
		handle_plci(&s_cmsg);
	else
		handle_ncci(&s_cmsg);
	/*
	 * data of skb used in s_cmsg,
	 * free data when s_cmsg is not used again
	 * thanks to Lars Heete <hel@admin.de>
	 */
	kfree_skb(skb);
}

/* ------------------------------------------------------------------- */

#define PUTBYTE_TO_STATUS(card, byte)				\
	do {							\
		*(card)->q931_write++ = (byte);			\
		if ((card)->q931_write > (card)->q931_end)	\
			(card)->q931_write = (card)->q931_buf;	\
	} while (0)

static void handle_dtrace_data(capidrv_contr *card,
			       int send, int level2, u8 *data, u16 len)
{
	u8 *p, *end;
	isdn_ctrl cmd;

	if (!len) {
		printk(KERN_DEBUG "capidrv-%d: avmb1_q931_data: len == %d\n",
		       card->contrnr, len);
		return;
	}

	if (level2) {
		PUTBYTE_TO_STATUS(card, 'D');
		PUTBYTE_TO_STATUS(card, '2');
		PUTBYTE_TO_STATUS(card, send ? '>' : '<');
		PUTBYTE_TO_STATUS(card, ':');
	} else {
		PUTBYTE_TO_STATUS(card, 'D');
		PUTBYTE_TO_STATUS(card, '3');
		PUTBYTE_TO_STATUS(card, send ? '>' : '<');
		PUTBYTE_TO_STATUS(card, ':');
	}

	for (p = data, end = data + len; p < end; p++) {
		PUTBYTE_TO_STATUS(card, ' ');
		PUTBYTE_TO_STATUS(card, hex_asc_hi(*p));
		PUTBYTE_TO_STATUS(card, hex_asc_lo(*p));
	}
	PUTBYTE_TO_STATUS(card, '\n');

	cmd.command = ISDN_STAT_STAVAIL;
	cmd.driver = card->myid;
	cmd.arg = len * 3 + 5;
	card->interface.statcallb(&cmd);
}

/* ------------------------------------------------------------------- */

static _cmsg cmdcmsg;

static int capidrv_ioctl(isdn_ctrl *c, capidrv_contr *card)
{
	switch (c->arg) {
	case 1:
		debugmode = (int)(*((unsigned int *)c->parm.num));
		printk(KERN_DEBUG "capidrv-%d: debugmode=%d\n",
		       card->contrnr, debugmode);
		return 0;
	default:
		printk(KERN_DEBUG "capidrv-%d: capidrv_ioctl(%ld) called ??\n",
		       card->contrnr, c->arg);
		return -EINVAL;
	}
	return -EINVAL;
}

/*
 * Handle leased lines (CAPI-Bundling)
 */

struct internal_bchannelinfo {
	unsigned short channelalloc;
	unsigned short operation;
	unsigned char  cmask[31];
};

static int decodeFVteln(char *teln, unsigned long *bmaskp, int *activep)
{
	unsigned long bmask = 0;
	int active = !0;
	char *s;
	int i;

	if (strncmp(teln, "FV:", 3) != 0)
		return 1;
	s = teln + 3;
	while (*s && *s == ' ') s++;
	if (!*s) return -2;
	if (*s == 'p' || *s == 'P') {
		active = 0;
		s++;
	}
	if (*s == 'a' || *s == 'A') {
		active = !0;
		s++;
	}
	while (*s) {
		int digit1 = 0;
		int digit2 = 0;
		char *endp;

		digit1 = simple_strtoul(s, &endp, 10);
		if (s == endp)
			return -3;
		s = endp;

		if (digit1 <= 0 || digit1 > 30) return -4;
		if (*s == 0 || *s == ',' || *s == ' ') {
			bmask |= (1 << digit1);
			digit1 = 0;
			if (*s) s++;
			continue;
		}
		if (*s != '-') return -5;
		s++;

		digit2 = simple_strtoul(s, &endp, 10);
		if (s == endp)
			return -3;
		s = endp;

		if (digit2 <= 0 || digit2 > 30) return -4;
		if (*s == 0 || *s == ',' || *s == ' ') {
			if (digit1 > digit2)
				for (i = digit2; i <= digit1; i++)
					bmask |= (1 << i);
			else
				for (i = digit1; i <= digit2; i++)
					bmask |= (1 << i);
			digit1 = digit2 = 0;
			if (*s) s++;
			continue;
		}
		return -6;
	}
	if (activep) *activep = active;
	if (bmaskp) *bmaskp = bmask;
	return 0;
}

static int FVteln2capi20(char *teln, u8 AdditionalInfo[1 + 2 + 2 + 31])
{
	unsigned long bmask;
	int active;
	int rc, i;

	rc = decodeFVteln(teln, &bmask, &active);
	if (rc) return rc;
	/* Length */
	AdditionalInfo[0] = 2 + 2 + 31;
	/* Channel: 3 => use channel allocation */
	AdditionalInfo[1] = 3; AdditionalInfo[2] = 0;
	/* Operation: 0 => DTE mode, 1 => DCE mode */
	if (active) {
		AdditionalInfo[3] = 0; AdditionalInfo[4] = 0;
	} else {
		AdditionalInfo[3] = 1; AdditionalInfo[4] = 0;
	}
	/* Channel mask array */
	AdditionalInfo[5] = 0; /* no D-Channel */
	for (i = 1; i <= 30; i++)
		AdditionalInfo[5 + i] = (bmask & (1 << i)) ? 0xff : 0;
	return 0;
}

static int capidrv_command(isdn_ctrl *c, capidrv_contr *card)
{
	isdn_ctrl cmd;
	struct capidrv_bchan *bchan;
	struct capidrv_plci *plcip;
	u8 AdditionalInfo[1 + 2 + 2 + 31];
	int rc, isleasedline = 0;

	if (c->command == ISDN_CMD_IOCTL)
		return capidrv_ioctl(c, card);

	switch (c->command) {
	case ISDN_CMD_DIAL: {
		u8 calling[ISDN_MSNLEN + 3];
		u8 called[ISDN_MSNLEN + 2];

		if (debugmode)
			printk(KERN_DEBUG "capidrv-%d: ISDN_CMD_DIAL(ch=%ld,\"%s,%d,%d,%s\")\n",
			       card->contrnr,
			       c->arg,
			       c->parm.setup.phone,
			       c->parm.setup.si1,
			       c->parm.setup.si2,
			       c->parm.setup.eazmsn);

		bchan = &card->bchans[c->arg % card->nbchan];

		if (bchan->plcip) {
			printk(KERN_ERR "capidrv-%d: dail ch=%ld,\"%s,%d,%d,%s\" in use (plci=0x%x)\n",
			       card->contrnr,
			       c->arg,
			       c->parm.setup.phone,
			       c->parm.setup.si1,
			       c->parm.setup.si2,
			       c->parm.setup.eazmsn,
			       bchan->plcip->plci);
			return 0;
		}
		bchan->si1 = c->parm.setup.si1;
		bchan->si2 = c->parm.setup.si2;

		strncpy(bchan->num, c->parm.setup.phone, sizeof(bchan->num));
		strncpy(bchan->mynum, c->parm.setup.eazmsn, sizeof(bchan->mynum));
		rc = FVteln2capi20(bchan->num, AdditionalInfo);
		isleasedline = (rc == 0);
		if (rc < 0)
			printk(KERN_ERR "capidrv-%d: WARNING: invalid leased linedefinition \"%s\"\n", card->contrnr, bchan->num);

		if (isleasedline) {
			calling[0] = 0;
			called[0] = 0;
			if (debugmode)
				printk(KERN_DEBUG "capidrv-%d: connecting leased line\n", card->contrnr);
		} else {
			calling[0] = strlen(bchan->mynum) + 2;
			calling[1] = 0;
			calling[2] = 0x80;
			strncpy(calling + 3, bchan->mynum, ISDN_MSNLEN);
			called[0] = strlen(bchan->num) + 1;
			called[1] = 0x80;
			strncpy(called + 2, bchan->num, ISDN_MSNLEN);
		}

		capi_fill_CONNECT_REQ(&cmdcmsg,
				      global.ap.applid,
				      card->msgid++,
				      card->contrnr,	/* adr */
				      si2cip(bchan->si1, bchan->si2),	/* cipvalue */
				      called,	/* CalledPartyNumber */
				      calling,	/* CallingPartyNumber */
				      NULL,	/* CalledPartySubaddress */
				      NULL,	/* CallingPartySubaddress */
				      b1prot(bchan->l2, bchan->l3),	/* B1protocol */
				      b2prot(bchan->l2, bchan->l3),	/* B2protocol */
				      b3prot(bchan->l2, bchan->l3),	/* B3protocol */
				      b1config(bchan->l2, bchan->l3),	/* B1configuration */
				      NULL,	/* B2configuration */
				      NULL,	/* B3configuration */
				      NULL,	/* BC */
				      NULL,	/* LLC */
				      NULL,	/* HLC */
				      /* BChannelinformation */
				      isleasedline ? AdditionalInfo : NULL,
				      NULL,	/* Keypadfacility */
				      NULL,	/* Useruserdata */
				      NULL	/* Facilitydataarray */
			);
		if ((plcip = new_plci(card, (c->arg % card->nbchan))) == NULL) {
			cmd.command = ISDN_STAT_DHUP;
			cmd.driver = card->myid;
			cmd.arg = (c->arg % card->nbchan);
			card->interface.statcallb(&cmd);
			return -1;
		}
		plcip->msgid = cmdcmsg.Messagenumber;
		plcip->leasedline = isleasedline;
		plci_change_state(card, plcip, EV_PLCI_CONNECT_REQ);
		send_message(card, &cmdcmsg);
		return 0;
	}

	case ISDN_CMD_ACCEPTD:

		bchan = &card->bchans[c->arg % card->nbchan];
		if (debugmode)
			printk(KERN_DEBUG "capidrv-%d: ISDN_CMD_ACCEPTD(ch=%ld) l2=%d l3=%d\n",
			       card->contrnr,
			       c->arg, bchan->l2, bchan->l3);

		capi_fill_CONNECT_RESP(&cmdcmsg,
				       global.ap.applid,
				       card->msgid++,
				       bchan->plcip->plci,	/* adr */
				       0,	/* Reject */
				       b1prot(bchan->l2, bchan->l3),	/* B1protocol */
				       b2prot(bchan->l2, bchan->l3),	/* B2protocol */
				       b3prot(bchan->l2, bchan->l3),	/* B3protocol */
				       b1config(bchan->l2, bchan->l3),	/* B1configuration */
				       NULL,	/* B2configuration */
				       NULL,	/* B3configuration */
				       NULL,	/* ConnectedNumber */
				       NULL,	/* ConnectedSubaddress */
				       NULL,	/* LLC */
				       NULL,	/* BChannelinformation */
				       NULL,	/* Keypadfacility */
				       NULL,	/* Useruserdata */
				       NULL	/* Facilitydataarray */
			);
		if (capi_cmsg2message(&cmdcmsg, cmdcmsg.buf)) {
			printk(KERN_ERR "capidrv-%d: capidrv_command: parser failure\n",
			       card->contrnr);
			return -EINVAL;
		}
		plci_change_state(card, bchan->plcip, EV_PLCI_CONNECT_RESP);
		send_message(card, &cmdcmsg);
		return 0;

	case ISDN_CMD_ACCEPTB:
		if (debugmode)
			printk(KERN_DEBUG "capidrv-%d: ISDN_CMD_ACCEPTB(ch=%ld)\n",
			       card->contrnr,
			       c->arg);
		return -ENOSYS;

	case ISDN_CMD_HANGUP:
		if (debugmode)
			printk(KERN_DEBUG "capidrv-%d: ISDN_CMD_HANGUP(ch=%ld)\n",
			       card->contrnr,
			       c->arg);
		bchan = &card->bchans[c->arg % card->nbchan];

		if (bchan->disconnecting) {
			if (debugmode)
				printk(KERN_DEBUG "capidrv-%d: chan %ld already disconnecting ...\n",
				       card->contrnr,
				       c->arg);
			return 0;
		}
		if (bchan->nccip) {
			bchan->disconnecting = 1;
			capi_fill_DISCONNECT_B3_REQ(&cmdcmsg,
						    global.ap.applid,
						    card->msgid++,
						    bchan->nccip->ncci,
						    NULL	/* NCPI */
				);
			ncci_change_state(card, bchan->nccip, EV_NCCI_DISCONNECT_B3_REQ);
			send_message(card, &cmdcmsg);
			return 0;
		} else if (bchan->plcip) {
			if (bchan->plcip->state == ST_PLCI_INCOMING) {
				/*
				 * just ignore, we a called from
				 * isdn_status_callback(),
				 * which will return 0 or 2, this is handled
				 * by the CONNECT_IND handler
				 */
				bchan->disconnecting = 1;
				return 0;
			} else if (bchan->plcip->plci) {
				bchan->disconnecting = 1;
				capi_fill_DISCONNECT_REQ(&cmdcmsg,
							 global.ap.applid,
							 card->msgid++,
							 bchan->plcip->plci,
							 NULL,	/* BChannelinformation */
							 NULL,	/* Keypadfacility */
							 NULL,	/* Useruserdata */
							 NULL	/* Facilitydataarray */
					);
				plci_change_state(card, bchan->plcip, EV_PLCI_DISCONNECT_REQ);
				send_message(card, &cmdcmsg);
				return 0;
			} else {
				printk(KERN_ERR "capidrv-%d: chan %ld disconnect request while waiting for CONNECT_CONF\n",
				       card->contrnr,
				       c->arg);
				return -EINVAL;
			}
		}
		printk(KERN_ERR "capidrv-%d: chan %ld disconnect request on free channel\n",
		       card->contrnr,
		       c->arg);
		return -EINVAL;
/* ready */

	case ISDN_CMD_SETL2:
		if (debugmode)
			printk(KERN_DEBUG "capidrv-%d: set L2 on chan %ld to %ld\n",
			       card->contrnr,
			       (c->arg & 0xff), (c->arg >> 8));
		bchan = &card->bchans[(c->arg & 0xff) % card->nbchan];
		bchan->l2 = (c->arg >> 8);
		return 0;

	case ISDN_CMD_SETL3:
		if (debugmode)
			printk(KERN_DEBUG "capidrv-%d: set L3 on chan %ld to %ld\n",
			       card->contrnr,
			       (c->arg & 0xff), (c->arg >> 8));
		bchan = &card->bchans[(c->arg & 0xff) % card->nbchan];
		bchan->l3 = (c->arg >> 8);
		return 0;

	case ISDN_CMD_SETEAZ:
		if (debugmode)
			printk(KERN_DEBUG "capidrv-%d: set EAZ \"%s\" on chan %ld\n",
			       card->contrnr,
			       c->parm.num, c->arg);
		bchan = &card->bchans[c->arg % card->nbchan];
		strncpy(bchan->msn, c->parm.num, ISDN_MSNLEN);
		return 0;

	case ISDN_CMD_CLREAZ:
		if (debugmode)
			printk(KERN_DEBUG "capidrv-%d: clearing EAZ on chan %ld\n",
			       card->contrnr, c->arg);
		bchan = &card->bchans[c->arg % card->nbchan];
		bchan->msn[0] = 0;
		return 0;

	default:
		printk(KERN_ERR "capidrv-%d: ISDN_CMD_%d, Huh?\n",
		       card->contrnr, c->command);
		return -EINVAL;
	}
	return 0;
}

static int if_command(isdn_ctrl *c)
{
	capidrv_contr *card = findcontrbydriverid(c->driver);

	if (card)
		return capidrv_command(c, card);

	printk(KERN_ERR
	       "capidrv: if_command %d called with invalid driverId %d!\n",
	       c->command, c->driver);
	return -ENODEV;
}

static _cmsg sendcmsg;

static int if_sendbuf(int id, int channel, int doack, struct sk_buff *skb)
{
	capidrv_contr *card = findcontrbydriverid(id);
	capidrv_bchan *bchan;
	capidrv_ncci *nccip;
	int len = skb->len;
	int msglen;
	u16 errcode;
	u16 datahandle;
	u32 data;

	if (!card) {
		printk(KERN_ERR "capidrv: if_sendbuf called with invalid driverId %d!\n",
		       id);
		return 0;
	}
	if (debugmode > 4)
		printk(KERN_DEBUG "capidrv-%d: sendbuf len=%d skb=%p doack=%d\n",
		       card->contrnr, len, skb, doack);
	bchan = &card->bchans[channel % card->nbchan];
	nccip = bchan->nccip;
	if (!nccip || nccip->state != ST_NCCI_ACTIVE) {
		printk(KERN_ERR "capidrv-%d: if_sendbuf: %s:%d: chan not up!\n",
		       card->contrnr, card->name, channel);
		return 0;
	}
	datahandle = nccip->datahandle;

	/*
	 * Here we copy pointer skb->data into the 32-bit 'Data' field.
	 * The 'Data' field is not used in practice in linux kernel
	 * (neither in 32 or 64 bit), but should have some value,
	 * since a CAPI message trace will display it.
	 *
	 * The correct value in the 32 bit case is the address of the
	 * data, in 64 bit it makes no sense, we use 0 there.
	 */

#ifdef CONFIG_64BIT
	data = 0;
#else
	data = (unsigned long) skb->data;
#endif

	capi_fill_DATA_B3_REQ(&sendcmsg, global.ap.applid, card->msgid++,
			      nccip->ncci,	/* adr */
			      data,		/* Data */
			      skb->len,		/* DataLength */
			      datahandle,	/* DataHandle */
			      0	/* Flags */
		);

	if (capidrv_add_ack(nccip, datahandle, doack ? (int)skb->len : -1) < 0)
		return 0;

	if (capi_cmsg2message(&sendcmsg, sendcmsg.buf)) {
		printk(KERN_ERR "capidrv-%d: if_sendbuf: parser failure\n",
		       card->contrnr);
		return -EINVAL;
	}
	msglen = CAPIMSG_LEN(sendcmsg.buf);
	if (skb_headroom(skb) < msglen) {
		struct sk_buff *nskb = skb_realloc_headroom(skb, msglen);
		if (!nskb) {
			printk(KERN_ERR "capidrv-%d: if_sendbuf: no memory\n",
			       card->contrnr);
			(void)capidrv_del_ack(nccip, datahandle);
			return 0;
		}
		printk(KERN_DEBUG "capidrv-%d: only %d bytes headroom, need %d\n",
		       card->contrnr, skb_headroom(skb), msglen);
		memcpy(skb_push(nskb, msglen), sendcmsg.buf, msglen);
		errcode = capi20_put_message(&global.ap, nskb);
		if (errcode == CAPI_NOERROR) {
			dev_kfree_skb(skb);
			nccip->datahandle++;
			return len;
		}
		if (debugmode > 3)
			printk(KERN_DEBUG "capidrv-%d: sendbuf putmsg ret(%x) - %s\n",
			       card->contrnr, errcode, capi_info2str(errcode));
		(void)capidrv_del_ack(nccip, datahandle);
		dev_kfree_skb(nskb);
		return errcode == CAPI_SENDQUEUEFULL ? 0 : -1;
	} else {
		memcpy(skb_push(skb, msglen), sendcmsg.buf, msglen);
		errcode = capi20_put_message(&global.ap, skb);
		if (errcode == CAPI_NOERROR) {
			nccip->datahandle++;
			return len;
		}
		if (debugmode > 3)
			printk(KERN_DEBUG "capidrv-%d: sendbuf putmsg ret(%x) - %s\n",
			       card->contrnr, errcode, capi_info2str(errcode));
		skb_pull(skb, msglen);
		(void)capidrv_del_ack(nccip, datahandle);
		return errcode == CAPI_SENDQUEUEFULL ? 0 : -1;
	}
}

static int if_readstat(u8 __user *buf, int len, int id, int channel)
{
	capidrv_contr *card = findcontrbydriverid(id);
	int count;
	u8 __user *p;

	if (!card) {
		printk(KERN_ERR "capidrv: if_readstat called with invalid driverId %d!\n",
		       id);
		return -ENODEV;
	}

	for (p = buf, count = 0; count < len; p++, count++) {
		if (put_user(*card->q931_read++, p))
			return -EFAULT;
		if (card->q931_read > card->q931_end)
			card->q931_read = card->q931_buf;
	}
	return count;

}

static void enable_dchannel_trace(capidrv_contr *card)
{
	u8 manufacturer[CAPI_MANUFACTURER_LEN];
	capi_version version;
	u16 contr = card->contrnr;
	u16 errcode;
	u16 avmversion[3];

	errcode = capi20_get_manufacturer(contr, manufacturer);
	if (errcode != CAPI_NOERROR) {
		printk(KERN_ERR "%s: can't get manufacturer (0x%x)\n",
		       card->name, errcode);
		return;
	}
	if (strstr(manufacturer, "AVM") == NULL) {
		printk(KERN_ERR "%s: not from AVM, no d-channel trace possible (%s)\n",
		       card->name, manufacturer);
		return;
	}
	errcode = capi20_get_version(contr, &version);
	if (errcode != CAPI_NOERROR) {
		printk(KERN_ERR "%s: can't get version (0x%x)\n",
		       card->name, errcode);
		return;
	}
	avmversion[0] = (version.majormanuversion >> 4) & 0x0f;
	avmversion[1] = (version.majormanuversion << 4) & 0xf0;
	avmversion[1] |= (version.minormanuversion >> 4) & 0x0f;
	avmversion[2] |= version.minormanuversion & 0x0f;

	if (avmversion[0] > 3 || (avmversion[0] == 3 && avmversion[1] > 5)) {
		printk(KERN_INFO "%s: D2 trace enabled\n", card->name);
		capi_fill_MANUFACTURER_REQ(&cmdcmsg, global.ap.applid,
					   card->msgid++,
					   contr,
					   0x214D5641,  /* ManuID */
					   0,           /* Class */
					   1,           /* Function */
					   (_cstruct)"\004\200\014\000\000");
	} else {
		printk(KERN_INFO "%s: D3 trace enabled\n", card->name);
		capi_fill_MANUFACTURER_REQ(&cmdcmsg, global.ap.applid,
					   card->msgid++,
					   contr,
					   0x214D5641,  /* ManuID */
					   0,           /* Class */
					   1,           /* Function */
					   (_cstruct)"\004\002\003\000\000");
	}
	send_message(card, &cmdcmsg);
}


static void send_listen(capidrv_contr *card)
{
	capi_fill_LISTEN_REQ(&cmdcmsg, global.ap.applid,
			     card->msgid++,
			     card->contrnr, /* controller */
			     1 << 6,	/* Infomask */
			     card->cipmask,
			     card->cipmask2,
			     NULL, NULL);
	listen_change_state(card, EV_LISTEN_REQ);
	send_message(card, &cmdcmsg);
}

static void listentimerfunc(struct timer_list *t)
{
	capidrv_contr *card = from_timer(card, t, listentimer);
	if (card->state != ST_LISTEN_NONE && card->state != ST_LISTEN_ACTIVE)
		printk(KERN_ERR "%s: controller dead ??\n", card->name);
	send_listen(card);
	mod_timer(&card->listentimer, jiffies + 60 * HZ);
}


static int capidrv_addcontr(u16 contr, struct capi_profile *profp)
{
	capidrv_contr *card;
	unsigned long flags;
	isdn_ctrl cmd;
	char id[20];
	int i;

	sprintf(id, "capidrv-%d", contr);
	if (!try_module_get(THIS_MODULE)) {
		printk(KERN_WARNING "capidrv: (%s) Could not reserve module\n", id);
		return -1;
	}
	if (!(card = kzalloc(sizeof(capidrv_contr), GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "capidrv: (%s) Could not allocate contr-struct.\n", id);
		return -1;
	}
	card->owner = THIS_MODULE;
	timer_setup(&card->listentimer, listentimerfunc, 0);
	strcpy(card->name, id);
	card->contrnr = contr;
	card->nbchan = profp->nbchannel;
	card->bchans = kmalloc(sizeof(capidrv_bchan) * card->nbchan, GFP_ATOMIC);
	if (!card->bchans) {
		printk(KERN_WARNING
		       "capidrv: (%s) Could not allocate bchan-structs.\n", id);
		module_put(card->owner);
		kfree(card);
		return -1;
	}
	card->interface.channels = profp->nbchannel;
	card->interface.maxbufsize = 2048;
	card->interface.command = if_command;
	card->interface.writebuf_skb = if_sendbuf;
	card->interface.writecmd = NULL;
	card->interface.readstat = if_readstat;
	card->interface.features =
		ISDN_FEATURE_L2_HDLC |
		ISDN_FEATURE_L2_TRANS |
		ISDN_FEATURE_L3_TRANS |
		ISDN_FEATURE_P_UNKNOWN |
		ISDN_FEATURE_L2_X75I |
		ISDN_FEATURE_L2_X75UI |
		ISDN_FEATURE_L2_X75BUI;
	if (profp->support1 & (1 << 2))
		card->interface.features |=
			ISDN_FEATURE_L2_V11096 |
			ISDN_FEATURE_L2_V11019 |
			ISDN_FEATURE_L2_V11038;
	if (profp->support1 & (1 << 8))
		card->interface.features |= ISDN_FEATURE_L2_MODEM;
	card->interface.hl_hdrlen = 22; /* len of DATA_B3_REQ */
	strncpy(card->interface.id, id, sizeof(card->interface.id) - 1);


	card->q931_read = card->q931_buf;
	card->q931_write = card->q931_buf;
	card->q931_end = card->q931_buf + sizeof(card->q931_buf) - 1;

	if (!register_isdn(&card->interface)) {
		printk(KERN_ERR "capidrv: Unable to register contr %s\n", id);
		kfree(card->bchans);
		module_put(card->owner);
		kfree(card);
		return -1;
	}
	card->myid = card->interface.channels;
	memset(card->bchans, 0, sizeof(capidrv_bchan) * card->nbchan);
	for (i = 0; i < card->nbchan; i++) {
		card->bchans[i].contr = card;
	}

	spin_lock_irqsave(&global_lock, flags);
	card->next = global.contr_list;
	global.contr_list = card;
	global.ncontr++;
	spin_unlock_irqrestore(&global_lock, flags);

	cmd.command = ISDN_STAT_RUN;
	cmd.driver = card->myid;
	card->interface.statcallb(&cmd);

	card->cipmask = 0x1FFF03FF;	/* any */
	card->cipmask2 = 0;

	send_listen(card);
	mod_timer(&card->listentimer, jiffies + 60 * HZ);

	printk(KERN_INFO "%s: now up (%d B channels)\n",
	       card->name, card->nbchan);

	enable_dchannel_trace(card);

	return 0;
}

static int capidrv_delcontr(u16 contr)
{
	capidrv_contr **pp, *card;
	unsigned long flags;
	isdn_ctrl cmd;

	spin_lock_irqsave(&global_lock, flags);
	for (card = global.contr_list; card; card = card->next) {
		if (card->contrnr == contr)
			break;
	}
	if (!card) {
		spin_unlock_irqrestore(&global_lock, flags);
		printk(KERN_ERR "capidrv: delcontr: no contr %u\n", contr);
		return -1;
	}

	/* FIXME: maybe a race condition the card should be removed
	 * here from global list /kkeil
	 */
	spin_unlock_irqrestore(&global_lock, flags);

	del_timer(&card->listentimer);

	if (debugmode)
		printk(KERN_DEBUG "capidrv-%d: id=%d unloading\n",
		       card->contrnr, card->myid);

	cmd.command = ISDN_STAT_STOP;
	cmd.driver = card->myid;
	card->interface.statcallb(&cmd);

	while (card->nbchan) {

		cmd.command = ISDN_STAT_DISCH;
		cmd.driver = card->myid;
		cmd.arg = card->nbchan - 1;
		cmd.parm.num[0] = 0;
		if (debugmode)
			printk(KERN_DEBUG "capidrv-%d: id=%d disable chan=%ld\n",
			       card->contrnr, card->myid, cmd.arg);
		card->interface.statcallb(&cmd);

		if (card->bchans[card->nbchan - 1].nccip)
			free_ncci(card, card->bchans[card->nbchan - 1].nccip);
		if (card->bchans[card->nbchan - 1].plcip)
			free_plci(card, card->bchans[card->nbchan - 1].plcip);
		if (card->plci_list)
			printk(KERN_ERR "capidrv: bug in free_plci()\n");
		card->nbchan--;
	}
	kfree(card->bchans);
	card->bchans = NULL;

	if (debugmode)
		printk(KERN_DEBUG "capidrv-%d: id=%d isdn unload\n",
		       card->contrnr, card->myid);

	cmd.command = ISDN_STAT_UNLOAD;
	cmd.driver = card->myid;
	card->interface.statcallb(&cmd);

	if (debugmode)
		printk(KERN_DEBUG "capidrv-%d: id=%d remove contr from list\n",
		       card->contrnr, card->myid);

	spin_lock_irqsave(&global_lock, flags);
	for (pp = &global.contr_list; *pp; pp = &(*pp)->next) {
		if (*pp == card) {
			*pp = (*pp)->next;
			card->next = NULL;
			global.ncontr--;
			break;
		}
	}
	spin_unlock_irqrestore(&global_lock, flags);

	module_put(card->owner);
	printk(KERN_INFO "%s: now down.\n", card->name);
	kfree(card);
	return 0;
}


static int
lower_callback(struct notifier_block *nb, unsigned long val, void *v)
{
	capi_profile profile;
	u32 contr = (long)v;

	switch (val) {
	case CAPICTR_UP:
		printk(KERN_INFO "capidrv: controller %hu up\n", contr);
		if (capi20_get_profile(contr, &profile) == CAPI_NOERROR)
			(void) capidrv_addcontr(contr, &profile);
		break;
	case CAPICTR_DOWN:
		printk(KERN_INFO "capidrv: controller %hu down\n", contr);
		(void) capidrv_delcontr(contr);
		break;
	}
	return NOTIFY_OK;
}

/*
 * /proc/capi/capidrv:
 * nrecvctlpkt nrecvdatapkt nsendctlpkt nsenddatapkt
 */
static int capidrv_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lu %lu %lu %lu\n",
		   global.ap.nrecvctlpkt,
		   global.ap.nrecvdatapkt,
		   global.ap.nsentctlpkt,
		   global.ap.nsentdatapkt);
	return 0;
}

static void __init proc_init(void)
{
	proc_create_single("capi/capidrv", 0, NULL, capidrv_proc_show);
}

static void __exit proc_exit(void)
{
	remove_proc_entry("capi/capidrv", NULL);
}

static struct notifier_block capictr_nb = {
	.notifier_call = lower_callback,
};

static int __init capidrv_init(void)
{
	capi_profile profile;
	u32 ncontr, contr;
	u16 errcode;

	global.ap.rparam.level3cnt = -2;  /* number of bchannels twice */
	global.ap.rparam.datablkcnt = 16;
	global.ap.rparam.datablklen = 2048;

	global.ap.recv_message = capidrv_recv_message;
	errcode = capi20_register(&global.ap);
	if (errcode) {
		return -EIO;
	}

	register_capictr_notifier(&capictr_nb);

	errcode = capi20_get_profile(0, &profile);
	if (errcode != CAPI_NOERROR) {
		unregister_capictr_notifier(&capictr_nb);
		capi20_release(&global.ap);
		return -EIO;
	}

	ncontr = profile.ncontroller;
	for (contr = 1; contr <= ncontr; contr++) {
		errcode = capi20_get_profile(contr, &profile);
		if (errcode != CAPI_NOERROR)
			continue;
		(void) capidrv_addcontr(contr, &profile);
	}
	proc_init();

	return 0;
}

static void __exit capidrv_exit(void)
{
	unregister_capictr_notifier(&capictr_nb);
	capi20_release(&global.ap);

	proc_exit();
}

module_init(capidrv_init);
module_exit(capidrv_exit);
