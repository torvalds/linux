/*
 * Copyright (C) 2001 Troy D. Armstrong  IBM Corporation
 * Copyright (C) 2004-2005 Stephen Rothwell  IBM Corporation
 *
 * This modules exists as an interface between a Linux secondary partition
 * running on an iSeries and the primary partition's Virtual Service
 * Processor (VSP) object.  The VSP has final authority over powering on/off
 * all partitions in the iSeries.  It also provides miscellaneous low-level
 * machine facility type operations.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/bcd.h>
#include <linux/rtc.h>

#include <asm/time.h>
#include <asm/uaccess.h>
#include <asm/paca.h>
#include <asm/abs_addr.h>
#include <asm/iseries/vio.h>
#include <asm/iseries/mf.h>
#include <asm/iseries/hv_lp_config.h>
#include <asm/iseries/it_lp_queue.h>

#include "setup.h"

extern int piranha_simulator;

/*
 * This is the structure layout for the Machine Facilites LPAR event
 * flows.
 */
struct vsp_cmd_data {
	u64 token;
	u16 cmd;
	HvLpIndex lp_index;
	u8 result_code;
	u32 reserved;
	union {
		u64 state;	/* GetStateOut */
		u64 ipl_type;	/* GetIplTypeOut, Function02SelectIplTypeIn */
		u64 ipl_mode;	/* GetIplModeOut, Function02SelectIplModeIn */
		u64 page[4];	/* GetSrcHistoryIn */
		u64 flag;	/* GetAutoIplWhenPrimaryIplsOut,
				   SetAutoIplWhenPrimaryIplsIn,
				   WhiteButtonPowerOffIn,
				   Function08FastPowerOffIn,
				   IsSpcnRackPowerIncompleteOut */
		struct {
			u64 token;
			u64 address_type;
			u64 side;
			u32 length;
			u32 offset;
		} kern;		/* SetKernelImageIn, GetKernelImageIn,
				   SetKernelCmdLineIn, GetKernelCmdLineIn */
		u32 length_out;	/* GetKernelImageOut, GetKernelCmdLineOut */
		u8 reserved[80];
	} sub_data;
};

struct vsp_rsp_data {
	struct completion com;
	struct vsp_cmd_data *response;
};

struct alloc_data {
	u16 size;
	u16 type;
	u32 count;
	u16 reserved1;
	u8 reserved2;
	HvLpIndex target_lp;
};

struct ce_msg_data;

typedef void (*ce_msg_comp_hdlr)(void *token, struct ce_msg_data *vsp_cmd_rsp);

struct ce_msg_comp_data {
	ce_msg_comp_hdlr handler;
	void *token;
};

struct ce_msg_data {
	u8 ce_msg[12];
	char reserved[4];
	struct ce_msg_comp_data *completion;
};

struct io_mf_lp_event {
	struct HvLpEvent hp_lp_event;
	u16 subtype_result_code;
	u16 reserved1;
	u32 reserved2;
	union {
		struct alloc_data alloc;
		struct ce_msg_data ce_msg;
		struct vsp_cmd_data vsp_cmd;
	} data;
};

#define subtype_data(a, b, c, d)	\
		(((a) << 24) + ((b) << 16) + ((c) << 8) + (d))

/*
 * All outgoing event traffic is kept on a FIFO queue.  The first
 * pointer points to the one that is outstanding, and all new
 * requests get stuck on the end.  Also, we keep a certain number of
 * preallocated pending events so that we can operate very early in
 * the boot up sequence (before kmalloc is ready).
 */
struct pending_event {
	struct pending_event *next;
	struct io_mf_lp_event event;
	MFCompleteHandler hdlr;
	char dma_data[72];
	unsigned dma_data_length;
	unsigned remote_address;
};
static spinlock_t pending_event_spinlock;
static struct pending_event *pending_event_head;
static struct pending_event *pending_event_tail;
static struct pending_event *pending_event_avail;
static struct pending_event pending_event_prealloc[16];

/*
 * Put a pending event onto the available queue, so it can get reused.
 * Attention! You must have the pending_event_spinlock before calling!
 */
static void free_pending_event(struct pending_event *ev)
{
	if (ev != NULL) {
		ev->next = pending_event_avail;
		pending_event_avail = ev;
	}
}

/*
 * Enqueue the outbound event onto the stack.  If the queue was
 * empty to begin with, we must also issue it via the Hypervisor
 * interface.  There is a section of code below that will touch
 * the first stack pointer without the protection of the pending_event_spinlock.
 * This is OK, because we know that nobody else will be modifying
 * the first pointer when we do this.
 */
static int signal_event(struct pending_event *ev)
{
	int rc = 0;
	unsigned long flags;
	int go = 1;
	struct pending_event *ev1;
	HvLpEvent_Rc hv_rc;

	/* enqueue the event */
	if (ev != NULL) {
		ev->next = NULL;
		spin_lock_irqsave(&pending_event_spinlock, flags);
		if (pending_event_head == NULL)
			pending_event_head = ev;
		else {
			go = 0;
			pending_event_tail->next = ev;
		}
		pending_event_tail = ev;
		spin_unlock_irqrestore(&pending_event_spinlock, flags);
	}

	/* send the event */
	while (go) {
		go = 0;

		/* any DMA data to send beforehand? */
		if (pending_event_head->dma_data_length > 0)
			HvCallEvent_dmaToSp(pending_event_head->dma_data,
					pending_event_head->remote_address,
					pending_event_head->dma_data_length,
					HvLpDma_Direction_LocalToRemote);

		hv_rc = HvCallEvent_signalLpEvent(
				&pending_event_head->event.hp_lp_event);
		if (hv_rc != HvLpEvent_Rc_Good) {
			printk(KERN_ERR "mf.c: HvCallEvent_signalLpEvent() "
					"failed with %d\n", (int)hv_rc);

			spin_lock_irqsave(&pending_event_spinlock, flags);
			ev1 = pending_event_head;
			pending_event_head = pending_event_head->next;
			if (pending_event_head != NULL)
				go = 1;
			spin_unlock_irqrestore(&pending_event_spinlock, flags);

			if (ev1 == ev)
				rc = -EIO;
			else if (ev1->hdlr != NULL)
				(*ev1->hdlr)((void *)ev1->event.hp_lp_event.xCorrelationToken, -EIO);

			spin_lock_irqsave(&pending_event_spinlock, flags);
			free_pending_event(ev1);
			spin_unlock_irqrestore(&pending_event_spinlock, flags);
		}
	}

	return rc;
}

/*
 * Allocate a new pending_event structure, and initialize it.
 */
static struct pending_event *new_pending_event(void)
{
	struct pending_event *ev = NULL;
	HvLpIndex primary_lp = HvLpConfig_getPrimaryLpIndex();
	unsigned long flags;
	struct HvLpEvent *hev;

	spin_lock_irqsave(&pending_event_spinlock, flags);
	if (pending_event_avail != NULL) {
		ev = pending_event_avail;
		pending_event_avail = pending_event_avail->next;
	}
	spin_unlock_irqrestore(&pending_event_spinlock, flags);
	if (ev == NULL) {
		ev = kmalloc(sizeof(struct pending_event), GFP_ATOMIC);
		if (ev == NULL) {
			printk(KERN_ERR "mf.c: unable to kmalloc %ld bytes\n",
					sizeof(struct pending_event));
			return NULL;
		}
	}
	memset(ev, 0, sizeof(struct pending_event));
	hev = &ev->event.hp_lp_event;
	hev->flags = HV_LP_EVENT_VALID | HV_LP_EVENT_DO_ACK | HV_LP_EVENT_INT;
	hev->xType = HvLpEvent_Type_MachineFac;
	hev->xSourceLp = HvLpConfig_getLpIndex();
	hev->xTargetLp = primary_lp;
	hev->xSizeMinus1 = sizeof(ev->event) - 1;
	hev->xRc = HvLpEvent_Rc_Good;
	hev->xSourceInstanceId = HvCallEvent_getSourceLpInstanceId(primary_lp,
			HvLpEvent_Type_MachineFac);
	hev->xTargetInstanceId = HvCallEvent_getTargetLpInstanceId(primary_lp,
			HvLpEvent_Type_MachineFac);

	return ev;
}

static int signal_vsp_instruction(struct vsp_cmd_data *vsp_cmd)
{
	struct pending_event *ev = new_pending_event();
	int rc;
	struct vsp_rsp_data response;

	if (ev == NULL)
		return -ENOMEM;

	init_completion(&response.com);
	response.response = vsp_cmd;
	ev->event.hp_lp_event.xSubtype = 6;
	ev->event.hp_lp_event.x.xSubtypeData =
		subtype_data('M', 'F',  'V',  'I');
	ev->event.data.vsp_cmd.token = (u64)&response;
	ev->event.data.vsp_cmd.cmd = vsp_cmd->cmd;
	ev->event.data.vsp_cmd.lp_index = HvLpConfig_getLpIndex();
	ev->event.data.vsp_cmd.result_code = 0xFF;
	ev->event.data.vsp_cmd.reserved = 0;
	memcpy(&(ev->event.data.vsp_cmd.sub_data),
			&(vsp_cmd->sub_data), sizeof(vsp_cmd->sub_data));
	mb();

	rc = signal_event(ev);
	if (rc == 0)
		wait_for_completion(&response.com);
	return rc;
}


/*
 * Send a 12-byte CE message to the primary partition VSP object
 */
static int signal_ce_msg(char *ce_msg, struct ce_msg_comp_data *completion)
{
	struct pending_event *ev = new_pending_event();

	if (ev == NULL)
		return -ENOMEM;

	ev->event.hp_lp_event.xSubtype = 0;
	ev->event.hp_lp_event.x.xSubtypeData =
		subtype_data('M',  'F',  'C',  'E');
	memcpy(ev->event.data.ce_msg.ce_msg, ce_msg, 12);
	ev->event.data.ce_msg.completion = completion;
	return signal_event(ev);
}

/*
 * Send a 12-byte CE message (with no data) to the primary partition VSP object
 */
static int signal_ce_msg_simple(u8 ce_op, struct ce_msg_comp_data *completion)
{
	u8 ce_msg[12];

	memset(ce_msg, 0, sizeof(ce_msg));
	ce_msg[3] = ce_op;
	return signal_ce_msg(ce_msg, completion);
}

/*
 * Send a 12-byte CE message and DMA data to the primary partition VSP object
 */
static int dma_and_signal_ce_msg(char *ce_msg,
		struct ce_msg_comp_data *completion, void *dma_data,
		unsigned dma_data_length, unsigned remote_address)
{
	struct pending_event *ev = new_pending_event();

	if (ev == NULL)
		return -ENOMEM;

	ev->event.hp_lp_event.xSubtype = 0;
	ev->event.hp_lp_event.x.xSubtypeData =
		subtype_data('M', 'F', 'C', 'E');
	memcpy(ev->event.data.ce_msg.ce_msg, ce_msg, 12);
	ev->event.data.ce_msg.completion = completion;
	memcpy(ev->dma_data, dma_data, dma_data_length);
	ev->dma_data_length = dma_data_length;
	ev->remote_address = remote_address;
	return signal_event(ev);
}

/*
 * Initiate a nice (hopefully) shutdown of Linux.  We simply are
 * going to try and send the init process a SIGINT signal.  If
 * this fails (why?), we'll simply force it off in a not-so-nice
 * manner.
 */
static int shutdown(void)
{
	int rc = kill_proc(1, SIGINT, 1);

	if (rc) {
		printk(KERN_ALERT "mf.c: SIGINT to init failed (%d), "
				"hard shutdown commencing\n", rc);
		mf_power_off();
	} else
		printk(KERN_INFO "mf.c: init has been successfully notified "
				"to proceed with shutdown\n");
	return rc;
}

/*
 * The primary partition VSP object is sending us a new
 * event flow.  Handle it...
 */
static void handle_int(struct io_mf_lp_event *event)
{
	struct ce_msg_data *ce_msg_data;
	struct ce_msg_data *pce_msg_data;
	unsigned long flags;
	struct pending_event *pev;

	/* ack the interrupt */
	event->hp_lp_event.xRc = HvLpEvent_Rc_Good;
	HvCallEvent_ackLpEvent(&event->hp_lp_event);

	/* process interrupt */
	switch (event->hp_lp_event.xSubtype) {
	case 0:	/* CE message */
		ce_msg_data = &event->data.ce_msg;
		switch (ce_msg_data->ce_msg[3]) {
		case 0x5B:	/* power control notification */
			if ((ce_msg_data->ce_msg[5] & 0x20) != 0) {
				printk(KERN_INFO "mf.c: Commencing partition shutdown\n");
				if (shutdown() == 0)
					signal_ce_msg_simple(0xDB, NULL);
			}
			break;
		case 0xC0:	/* get time */
			spin_lock_irqsave(&pending_event_spinlock, flags);
			pev = pending_event_head;
			if (pev != NULL)
				pending_event_head = pending_event_head->next;
			spin_unlock_irqrestore(&pending_event_spinlock, flags);
			if (pev == NULL)
				break;
			pce_msg_data = &pev->event.data.ce_msg;
			if (pce_msg_data->ce_msg[3] != 0x40)
				break;
			if (pce_msg_data->completion != NULL) {
				ce_msg_comp_hdlr handler =
					pce_msg_data->completion->handler;
				void *token = pce_msg_data->completion->token;

				if (handler != NULL)
					(*handler)(token, ce_msg_data);
			}
			spin_lock_irqsave(&pending_event_spinlock, flags);
			free_pending_event(pev);
			spin_unlock_irqrestore(&pending_event_spinlock, flags);
			/* send next waiting event */
			if (pending_event_head != NULL)
				signal_event(NULL);
			break;
		}
		break;
	case 1:	/* IT sys shutdown */
		printk(KERN_INFO "mf.c: Commencing system shutdown\n");
		shutdown();
		break;
	}
}

/*
 * The primary partition VSP object is acknowledging the receipt
 * of a flow we sent to them.  If there are other flows queued
 * up, we must send another one now...
 */
static void handle_ack(struct io_mf_lp_event *event)
{
	unsigned long flags;
	struct pending_event *two = NULL;
	unsigned long free_it = 0;
	struct ce_msg_data *ce_msg_data;
	struct ce_msg_data *pce_msg_data;
	struct vsp_rsp_data *rsp;

	/* handle current event */
	if (pending_event_head == NULL) {
		printk(KERN_ERR "mf.c: stack empty for receiving ack\n");
		return;
	}

	switch (event->hp_lp_event.xSubtype) {
	case 0:     /* CE msg */
		ce_msg_data = &event->data.ce_msg;
		if (ce_msg_data->ce_msg[3] != 0x40) {
			free_it = 1;
			break;
		}
		if (ce_msg_data->ce_msg[2] == 0)
			break;
		free_it = 1;
		pce_msg_data = &pending_event_head->event.data.ce_msg;
		if (pce_msg_data->completion != NULL) {
			ce_msg_comp_hdlr handler =
				pce_msg_data->completion->handler;
			void *token = pce_msg_data->completion->token;

			if (handler != NULL)
				(*handler)(token, ce_msg_data);
		}
		break;
	case 4:	/* allocate */
	case 5:	/* deallocate */
		if (pending_event_head->hdlr != NULL)
			(*pending_event_head->hdlr)((void *)event->hp_lp_event.xCorrelationToken, event->data.alloc.count);
		free_it = 1;
		break;
	case 6:
		free_it = 1;
		rsp = (struct vsp_rsp_data *)event->data.vsp_cmd.token;
		if (rsp == NULL) {
			printk(KERN_ERR "mf.c: no rsp\n");
			break;
		}
		if (rsp->response != NULL)
			memcpy(rsp->response, &event->data.vsp_cmd,
					sizeof(event->data.vsp_cmd));
		complete(&rsp->com);
		break;
	}

	/* remove from queue */
	spin_lock_irqsave(&pending_event_spinlock, flags);
	if ((pending_event_head != NULL) && (free_it == 1)) {
		struct pending_event *oldHead = pending_event_head;

		pending_event_head = pending_event_head->next;
		two = pending_event_head;
		free_pending_event(oldHead);
	}
	spin_unlock_irqrestore(&pending_event_spinlock, flags);

	/* send next waiting event */
	if (two != NULL)
		signal_event(NULL);
}

/*
 * This is the generic event handler we are registering with
 * the Hypervisor.  Ensure the flows are for us, and then
 * parse it enough to know if it is an interrupt or an
 * acknowledge.
 */
static void hv_handler(struct HvLpEvent *event, struct pt_regs *regs)
{
	if ((event != NULL) && (event->xType == HvLpEvent_Type_MachineFac)) {
		if (hvlpevent_is_ack(event))
			handle_ack((struct io_mf_lp_event *)event);
		else
			handle_int((struct io_mf_lp_event *)event);
	} else
		printk(KERN_ERR "mf.c: alien event received\n");
}

/*
 * Global kernel interface to allocate and seed events into the
 * Hypervisor.
 */
void mf_allocate_lp_events(HvLpIndex target_lp, HvLpEvent_Type type,
		unsigned size, unsigned count, MFCompleteHandler hdlr,
		void *user_token)
{
	struct pending_event *ev = new_pending_event();
	int rc;

	if (ev == NULL) {
		rc = -ENOMEM;
	} else {
		ev->event.hp_lp_event.xSubtype = 4;
		ev->event.hp_lp_event.xCorrelationToken = (u64)user_token;
		ev->event.hp_lp_event.x.xSubtypeData =
			subtype_data('M', 'F', 'M', 'A');
		ev->event.data.alloc.target_lp = target_lp;
		ev->event.data.alloc.type = type;
		ev->event.data.alloc.size = size;
		ev->event.data.alloc.count = count;
		ev->hdlr = hdlr;
		rc = signal_event(ev);
	}
	if ((rc != 0) && (hdlr != NULL))
		(*hdlr)(user_token, rc);
}
EXPORT_SYMBOL(mf_allocate_lp_events);

/*
 * Global kernel interface to unseed and deallocate events already in
 * Hypervisor.
 */
void mf_deallocate_lp_events(HvLpIndex target_lp, HvLpEvent_Type type,
		unsigned count, MFCompleteHandler hdlr, void *user_token)
{
	struct pending_event *ev = new_pending_event();
	int rc;

	if (ev == NULL)
		rc = -ENOMEM;
	else {
		ev->event.hp_lp_event.xSubtype = 5;
		ev->event.hp_lp_event.xCorrelationToken = (u64)user_token;
		ev->event.hp_lp_event.x.xSubtypeData =
			subtype_data('M', 'F', 'M', 'D');
		ev->event.data.alloc.target_lp = target_lp;
		ev->event.data.alloc.type = type;
		ev->event.data.alloc.count = count;
		ev->hdlr = hdlr;
		rc = signal_event(ev);
	}
	if ((rc != 0) && (hdlr != NULL))
		(*hdlr)(user_token, rc);
}
EXPORT_SYMBOL(mf_deallocate_lp_events);

/*
 * Global kernel interface to tell the VSP object in the primary
 * partition to power this partition off.
 */
void mf_power_off(void)
{
	printk(KERN_INFO "mf.c: Down it goes...\n");
	signal_ce_msg_simple(0x4d, NULL);
	for (;;)
		;
}

/*
 * Global kernel interface to tell the VSP object in the primary
 * partition to reboot this partition.
 */
void mf_reboot(void)
{
	printk(KERN_INFO "mf.c: Preparing to bounce...\n");
	signal_ce_msg_simple(0x4e, NULL);
	for (;;)
		;
}

/*
 * Display a single word SRC onto the VSP control panel.
 */
void mf_display_src(u32 word)
{
	u8 ce[12];

	memset(ce, 0, sizeof(ce));
	ce[3] = 0x4a;
	ce[7] = 0x01;
	ce[8] = word >> 24;
	ce[9] = word >> 16;
	ce[10] = word >> 8;
	ce[11] = word;
	signal_ce_msg(ce, NULL);
}

/*
 * Display a single word SRC of the form "PROGXXXX" on the VSP control panel.
 */
void mf_display_progress(u16 value)
{
	u8 ce[12];
	u8 src[72];

	memcpy(ce, "\x00\x00\x04\x4A\x00\x00\x00\x48\x00\x00\x00\x00", 12);
	memcpy(src, "\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00PROGxxxx                        ",
		72);
	src[6] = value >> 8;
	src[7] = value & 255;
	src[44] = "0123456789ABCDEF"[(value >> 12) & 15];
	src[45] = "0123456789ABCDEF"[(value >> 8) & 15];
	src[46] = "0123456789ABCDEF"[(value >> 4) & 15];
	src[47] = "0123456789ABCDEF"[value & 15];
	dma_and_signal_ce_msg(ce, NULL, src, sizeof(src), 9 * 64 * 1024);
}

/*
 * Clear the VSP control panel.  Used to "erase" an SRC that was
 * previously displayed.
 */
void mf_clear_src(void)
{
	signal_ce_msg_simple(0x4b, NULL);
}

/*
 * Initialization code here.
 */
void mf_init(void)
{
	int i;

	/* initialize */
	spin_lock_init(&pending_event_spinlock);
	for (i = 0;
	     i < sizeof(pending_event_prealloc) / sizeof(*pending_event_prealloc);
	     ++i)
		free_pending_event(&pending_event_prealloc[i]);
	HvLpEvent_registerHandler(HvLpEvent_Type_MachineFac, &hv_handler);

	/* virtual continue ack */
	signal_ce_msg_simple(0x57, NULL);

	/* initialization complete */
	printk(KERN_NOTICE "mf.c: iSeries Linux LPAR Machine Facilities "
			"initialized\n");
}

struct rtc_time_data {
	struct completion com;
	struct ce_msg_data ce_msg;
	int rc;
};

static void get_rtc_time_complete(void *token, struct ce_msg_data *ce_msg)
{
	struct rtc_time_data *rtc = token;

	memcpy(&rtc->ce_msg, ce_msg, sizeof(rtc->ce_msg));
	rtc->rc = 0;
	complete(&rtc->com);
}

static int rtc_set_tm(int rc, u8 *ce_msg, struct rtc_time *tm)
{
	tm->tm_wday = 0;
	tm->tm_yday = 0;
	tm->tm_isdst = 0;
	if (rc) {
		tm->tm_sec = 0;
		tm->tm_min = 0;
		tm->tm_hour = 0;
		tm->tm_mday = 15;
		tm->tm_mon = 5;
		tm->tm_year = 52;
		return rc;
	}

	if ((ce_msg[2] == 0xa9) ||
	    (ce_msg[2] == 0xaf)) {
		/* TOD clock is not set */
		tm->tm_sec = 1;
		tm->tm_min = 1;
		tm->tm_hour = 1;
		tm->tm_mday = 10;
		tm->tm_mon = 8;
		tm->tm_year = 71;
		mf_set_rtc(tm);
	}
	{
		u8 year = ce_msg[5];
		u8 sec = ce_msg[6];
		u8 min = ce_msg[7];
		u8 hour = ce_msg[8];
		u8 day = ce_msg[10];
		u8 mon = ce_msg[11];

		BCD_TO_BIN(sec);
		BCD_TO_BIN(min);
		BCD_TO_BIN(hour);
		BCD_TO_BIN(day);
		BCD_TO_BIN(mon);
		BCD_TO_BIN(year);

		if (year <= 69)
			year += 100;

		tm->tm_sec = sec;
		tm->tm_min = min;
		tm->tm_hour = hour;
		tm->tm_mday = day;
		tm->tm_mon = mon;
		tm->tm_year = year;
	}

	return 0;
}

int mf_get_rtc(struct rtc_time *tm)
{
	struct ce_msg_comp_data ce_complete;
	struct rtc_time_data rtc_data;
	int rc;

	memset(&ce_complete, 0, sizeof(ce_complete));
	memset(&rtc_data, 0, sizeof(rtc_data));
	init_completion(&rtc_data.com);
	ce_complete.handler = &get_rtc_time_complete;
	ce_complete.token = &rtc_data;
	rc = signal_ce_msg_simple(0x40, &ce_complete);
	if (rc)
		return rc;
	wait_for_completion(&rtc_data.com);
	return rtc_set_tm(rtc_data.rc, rtc_data.ce_msg.ce_msg, tm);
}

struct boot_rtc_time_data {
	int busy;
	struct ce_msg_data ce_msg;
	int rc;
};

static void get_boot_rtc_time_complete(void *token, struct ce_msg_data *ce_msg)
{
	struct boot_rtc_time_data *rtc = token;

	memcpy(&rtc->ce_msg, ce_msg, sizeof(rtc->ce_msg));
	rtc->rc = 0;
	rtc->busy = 0;
}

int mf_get_boot_rtc(struct rtc_time *tm)
{
	struct ce_msg_comp_data ce_complete;
	struct boot_rtc_time_data rtc_data;
	int rc;

	memset(&ce_complete, 0, sizeof(ce_complete));
	memset(&rtc_data, 0, sizeof(rtc_data));
	rtc_data.busy = 1;
	ce_complete.handler = &get_boot_rtc_time_complete;
	ce_complete.token = &rtc_data;
	rc = signal_ce_msg_simple(0x40, &ce_complete);
	if (rc)
		return rc;
	/* We need to poll here as we are not yet taking interrupts */
	while (rtc_data.busy) {
		if (hvlpevent_is_pending())
			process_hvlpevents(NULL);
	}
	return rtc_set_tm(rtc_data.rc, rtc_data.ce_msg.ce_msg, tm);
}

int mf_set_rtc(struct rtc_time *tm)
{
	char ce_time[12];
	u8 day, mon, hour, min, sec, y1, y2;
	unsigned year;

	year = 1900 + tm->tm_year;
	y1 = year / 100;
	y2 = year % 100;

	sec = tm->tm_sec;
	min = tm->tm_min;
	hour = tm->tm_hour;
	day = tm->tm_mday;
	mon = tm->tm_mon + 1;

	BIN_TO_BCD(sec);
	BIN_TO_BCD(min);
	BIN_TO_BCD(hour);
	BIN_TO_BCD(mon);
	BIN_TO_BCD(day);
	BIN_TO_BCD(y1);
	BIN_TO_BCD(y2);

	memset(ce_time, 0, sizeof(ce_time));
	ce_time[3] = 0x41;
	ce_time[4] = y1;
	ce_time[5] = y2;
	ce_time[6] = sec;
	ce_time[7] = min;
	ce_time[8] = hour;
	ce_time[10] = day;
	ce_time[11] = mon;

	return signal_ce_msg(ce_time, NULL);
}

#ifdef CONFIG_PROC_FS

static int proc_mf_dump_cmdline(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len;
	char *p;
	struct vsp_cmd_data vsp_cmd;
	int rc;
	dma_addr_t dma_addr;

	/* The HV appears to return no more than 256 bytes of command line */
	if (off >= 256)
		return 0;
	if ((off + count) > 256)
		count = 256 - off;

	dma_addr = dma_map_single(iSeries_vio_dev, page, off + count,
			DMA_FROM_DEVICE);
	if (dma_mapping_error(dma_addr))
		return -ENOMEM;
	memset(page, 0, off + count);
	memset(&vsp_cmd, 0, sizeof(vsp_cmd));
	vsp_cmd.cmd = 33;
	vsp_cmd.sub_data.kern.token = dma_addr;
	vsp_cmd.sub_data.kern.address_type = HvLpDma_AddressType_TceIndex;
	vsp_cmd.sub_data.kern.side = (u64)data;
	vsp_cmd.sub_data.kern.length = off + count;
	mb();
	rc = signal_vsp_instruction(&vsp_cmd);
	dma_unmap_single(iSeries_vio_dev, dma_addr, off + count,
			DMA_FROM_DEVICE);
	if (rc)
		return rc;
	if (vsp_cmd.result_code != 0)
		return -ENOMEM;
	p = page;
	len = 0;
	while (len < (off + count)) {
		if ((*p == '\0') || (*p == '\n')) {
			if (*p == '\0')
				*p = '\n';
			p++;
			len++;
			*eof = 1;
			break;
		}
		p++;
		len++;
	}

	if (len < off) {
		*eof = 1;
		len = 0;
	}
	return len;
}

#if 0
static int mf_getVmlinuxChunk(char *buffer, int *size, int offset, u64 side)
{
	struct vsp_cmd_data vsp_cmd;
	int rc;
	int len = *size;
	dma_addr_t dma_addr;

	dma_addr = dma_map_single(iSeries_vio_dev, buffer, len,
			DMA_FROM_DEVICE);
	memset(buffer, 0, len);
	memset(&vsp_cmd, 0, sizeof(vsp_cmd));
	vsp_cmd.cmd = 32;
	vsp_cmd.sub_data.kern.token = dma_addr;
	vsp_cmd.sub_data.kern.address_type = HvLpDma_AddressType_TceIndex;
	vsp_cmd.sub_data.kern.side = side;
	vsp_cmd.sub_data.kern.offset = offset;
	vsp_cmd.sub_data.kern.length = len;
	mb();
	rc = signal_vsp_instruction(&vsp_cmd);
	if (rc == 0) {
		if (vsp_cmd.result_code == 0)
			*size = vsp_cmd.sub_data.length_out;
		else
			rc = -ENOMEM;
	}

	dma_unmap_single(iSeries_vio_dev, dma_addr, len, DMA_FROM_DEVICE);

	return rc;
}

static int proc_mf_dump_vmlinux(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int sizeToGet = count;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (mf_getVmlinuxChunk(page, &sizeToGet, off, (u64)data) == 0) {
		if (sizeToGet != 0) {
			*start = page + off;
			return sizeToGet;
		}
		*eof = 1;
		return 0;
	}
	*eof = 1;
	return 0;
}
#endif

static int proc_mf_dump_side(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len;
	char mf_current_side = ' ';
	struct vsp_cmd_data vsp_cmd;

	memset(&vsp_cmd, 0, sizeof(vsp_cmd));
	vsp_cmd.cmd = 2;
	vsp_cmd.sub_data.ipl_type = 0;
	mb();

	if (signal_vsp_instruction(&vsp_cmd) == 0) {
		if (vsp_cmd.result_code == 0) {
			switch (vsp_cmd.sub_data.ipl_type) {
			case 0:	mf_current_side = 'A';
				break;
			case 1:	mf_current_side = 'B';
				break;
			case 2:	mf_current_side = 'C';
				break;
			default:	mf_current_side = 'D';
				break;
			}
		}
	}

	len = sprintf(page, "%c\n", mf_current_side);

	if (len <= (off + count))
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static int proc_mf_change_side(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	char side;
	u64 newSide;
	struct vsp_cmd_data vsp_cmd;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (count == 0)
		return 0;

	if (get_user(side, buffer))
		return -EFAULT;

	switch (side) {
	case 'A':	newSide = 0;
			break;
	case 'B':	newSide = 1;
			break;
	case 'C':	newSide = 2;
			break;
	case 'D':	newSide = 3;
			break;
	default:
		printk(KERN_ERR "mf_proc.c: proc_mf_change_side: invalid side\n");
		return -EINVAL;
	}

	memset(&vsp_cmd, 0, sizeof(vsp_cmd));
	vsp_cmd.sub_data.ipl_type = newSide;
	vsp_cmd.cmd = 10;

	(void)signal_vsp_instruction(&vsp_cmd);

	return count;
}

#if 0
static void mf_getSrcHistory(char *buffer, int size)
{
	struct IplTypeReturnStuff return_stuff;
	struct pending_event *ev = new_pending_event();
	int rc = 0;
	char *pages[4];

	pages[0] = kmalloc(4096, GFP_ATOMIC);
	pages[1] = kmalloc(4096, GFP_ATOMIC);
	pages[2] = kmalloc(4096, GFP_ATOMIC);
	pages[3] = kmalloc(4096, GFP_ATOMIC);
	if ((ev == NULL) || (pages[0] == NULL) || (pages[1] == NULL)
			 || (pages[2] == NULL) || (pages[3] == NULL))
		return -ENOMEM;

	return_stuff.xType = 0;
	return_stuff.xRc = 0;
	return_stuff.xDone = 0;
	ev->event.hp_lp_event.xSubtype = 6;
	ev->event.hp_lp_event.x.xSubtypeData =
		subtype_data('M', 'F', 'V', 'I');
	ev->event.data.vsp_cmd.xEvent = &return_stuff;
	ev->event.data.vsp_cmd.cmd = 4;
	ev->event.data.vsp_cmd.lp_index = HvLpConfig_getLpIndex();
	ev->event.data.vsp_cmd.result_code = 0xFF;
	ev->event.data.vsp_cmd.reserved = 0;
	ev->event.data.vsp_cmd.sub_data.page[0] = iseries_hv_addr(pages[0]);
	ev->event.data.vsp_cmd.sub_data.page[1] = iseries_hv_addr(pages[1]);
	ev->event.data.vsp_cmd.sub_data.page[2] = iseries_hv_addr(pages[2]);
	ev->event.data.vsp_cmd.sub_data.page[3] = iseries_hv_addr(pages[3]);
	mb();
	if (signal_event(ev) != 0)
		return;

 	while (return_stuff.xDone != 1)
 		udelay(10);
 	if (return_stuff.xRc == 0)
 		memcpy(buffer, pages[0], size);
	kfree(pages[0]);
	kfree(pages[1]);
	kfree(pages[2]);
	kfree(pages[3]);
}
#endif

static int proc_mf_dump_src(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
#if 0
	int len;

	mf_getSrcHistory(page, count);
	len = count;
	len -= off;
	if (len < count) {
		*eof = 1;
		if (len <= 0)
			return 0;
	} else
		len = count;
	*start = page + off;
	return len;
#else
	return 0;
#endif
}

static int proc_mf_change_src(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	char stkbuf[10];

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if ((count < 4) && (count != 1)) {
		printk(KERN_ERR "mf_proc: invalid src\n");
		return -EINVAL;
	}

	if (count > (sizeof(stkbuf) - 1))
		count = sizeof(stkbuf) - 1;
	if (copy_from_user(stkbuf, buffer, count))
		return -EFAULT;

	if ((count == 1) && (*stkbuf == '\0'))
		mf_clear_src();
	else
		mf_display_src(*(u32 *)stkbuf);

	return count;
}

static int proc_mf_change_cmdline(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	struct vsp_cmd_data vsp_cmd;
	dma_addr_t dma_addr;
	char *page;
	int ret = -EACCES;

	if (!capable(CAP_SYS_ADMIN))
		goto out;

	dma_addr = 0;
	page = dma_alloc_coherent(iSeries_vio_dev, count, &dma_addr,
			GFP_ATOMIC);
	ret = -ENOMEM;
	if (page == NULL)
		goto out;

	ret = -EFAULT;
	if (copy_from_user(page, buffer, count))
		goto out_free;

	memset(&vsp_cmd, 0, sizeof(vsp_cmd));
	vsp_cmd.cmd = 31;
	vsp_cmd.sub_data.kern.token = dma_addr;
	vsp_cmd.sub_data.kern.address_type = HvLpDma_AddressType_TceIndex;
	vsp_cmd.sub_data.kern.side = (u64)data;
	vsp_cmd.sub_data.kern.length = count;
	mb();
	(void)signal_vsp_instruction(&vsp_cmd);
	ret = count;

out_free:
	dma_free_coherent(iSeries_vio_dev, count, page, dma_addr);
out:
	return ret;
}

static ssize_t proc_mf_change_vmlinux(struct file *file,
				      const char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct proc_dir_entry *dp = PDE(file->f_dentry->d_inode);
	ssize_t rc;
	dma_addr_t dma_addr;
	char *page;
	struct vsp_cmd_data vsp_cmd;

	rc = -EACCES;
	if (!capable(CAP_SYS_ADMIN))
		goto out;

	dma_addr = 0;
	page = dma_alloc_coherent(iSeries_vio_dev, count, &dma_addr,
			GFP_ATOMIC);
	rc = -ENOMEM;
	if (page == NULL) {
		printk(KERN_ERR "mf.c: couldn't allocate memory to set vmlinux chunk\n");
		goto out;
	}
	rc = -EFAULT;
	if (copy_from_user(page, buf, count))
		goto out_free;

	memset(&vsp_cmd, 0, sizeof(vsp_cmd));
	vsp_cmd.cmd = 30;
	vsp_cmd.sub_data.kern.token = dma_addr;
	vsp_cmd.sub_data.kern.address_type = HvLpDma_AddressType_TceIndex;
	vsp_cmd.sub_data.kern.side = (u64)dp->data;
	vsp_cmd.sub_data.kern.offset = *ppos;
	vsp_cmd.sub_data.kern.length = count;
	mb();
	rc = signal_vsp_instruction(&vsp_cmd);
	if (rc)
		goto out_free;
	rc = -ENOMEM;
	if (vsp_cmd.result_code != 0)
		goto out_free;

	*ppos += count;
	rc = count;
out_free:
	dma_free_coherent(iSeries_vio_dev, count, page, dma_addr);
out:
	return rc;
}

static struct file_operations proc_vmlinux_operations = {
	.write		= proc_mf_change_vmlinux,
};

static int __init mf_proc_init(void)
{
	struct proc_dir_entry *mf_proc_root;
	struct proc_dir_entry *ent;
	struct proc_dir_entry *mf;
	char name[2];
	int i;

	mf_proc_root = proc_mkdir("iSeries/mf", NULL);
	if (!mf_proc_root)
		return 1;

	name[1] = '\0';
	for (i = 0; i < 4; i++) {
		name[0] = 'A' + i;
		mf = proc_mkdir(name, mf_proc_root);
		if (!mf)
			return 1;

		ent = create_proc_entry("cmdline", S_IFREG|S_IRUSR|S_IWUSR, mf);
		if (!ent)
			return 1;
		ent->nlink = 1;
		ent->data = (void *)(long)i;
		ent->read_proc = proc_mf_dump_cmdline;
		ent->write_proc = proc_mf_change_cmdline;

		if (i == 3)	/* no vmlinux entry for 'D' */
			continue;

		ent = create_proc_entry("vmlinux", S_IFREG|S_IWUSR, mf);
		if (!ent)
			return 1;
		ent->nlink = 1;
		ent->data = (void *)(long)i;
		ent->proc_fops = &proc_vmlinux_operations;
	}

	ent = create_proc_entry("side", S_IFREG|S_IRUSR|S_IWUSR, mf_proc_root);
	if (!ent)
		return 1;
	ent->nlink = 1;
	ent->data = (void *)0;
	ent->read_proc = proc_mf_dump_side;
	ent->write_proc = proc_mf_change_side;

	ent = create_proc_entry("src", S_IFREG|S_IRUSR|S_IWUSR, mf_proc_root);
	if (!ent)
		return 1;
	ent->nlink = 1;
	ent->data = (void *)0;
	ent->read_proc = proc_mf_dump_src;
	ent->write_proc = proc_mf_change_src;

	return 0;
}

__initcall(mf_proc_init);

#endif /* CONFIG_PROC_FS */

/*
 * Get the RTC from the virtual service processor
 * This requires flowing LpEvents to the primary partition
 */
void iSeries_get_rtc_time(struct rtc_time *rtc_tm)
{
	if (piranha_simulator)
		return;

	mf_get_rtc(rtc_tm);
	rtc_tm->tm_mon--;
}

/*
 * Set the RTC in the virtual service processor
 * This requires flowing LpEvents to the primary partition
 */
int iSeries_set_rtc_time(struct rtc_time *tm)
{
	mf_set_rtc(tm);
	return 0;
}

unsigned long iSeries_get_boot_time(void)
{
	struct rtc_time tm;

	if (piranha_simulator)
		return 0;

	mf_get_boot_rtc(&tm);
	return mktime(tm.tm_year + 1900, tm.tm_mon, tm.tm_mday,
		      tm.tm_hour, tm.tm_min, tm.tm_sec);
}
