/*
 * Stuff used by all variants of the driver
 *
 * Copyright (c) 2001 by Stefan Eilers <Eilers.Stefan@epost.de>,
 *                       Hansjoerg Lipp <hjlipp@web.de>,
 *                       Tilman Schmidt <tilman@imap.cc>.
 *
 * =====================================================================
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 * =====================================================================
 */

#include "gigaset.h"
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

/* Version Information */
#define DRIVER_AUTHOR "Hansjoerg Lipp <hjlipp@web.de>, Tilman Schmidt <tilman@imap.cc>, Stefan Eilers <Eilers.Stefan@epost.de>"
#define DRIVER_DESC "Driver for Gigaset 307x"

/* Module parameters */
int gigaset_debuglevel = DEBUG_DEFAULT;
EXPORT_SYMBOL_GPL(gigaset_debuglevel);
module_param_named(debug, gigaset_debuglevel, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(debug, "debug level");

/*======================================================================
  Prototypes of internal functions
 */

static struct cardstate *alloc_cs(struct gigaset_driver *drv);
static void free_cs(struct cardstate *cs);
static void make_valid(struct cardstate *cs, unsigned mask);
static void make_invalid(struct cardstate *cs, unsigned mask);

#define VALID_MINOR	0x01
#define VALID_ID	0x02
#define ASSIGNED	0x04

/* bitwise byte inversion table */
__u8 gigaset_invtab[256] = {
	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
	0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
	0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
	0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
	0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
	0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
	0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
	0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
	0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
	0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
	0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
	0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
	0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
	0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
	0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
	0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
	0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
	0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
	0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
	0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
	0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
	0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
	0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
	0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
	0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
	0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
	0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
	0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
	0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
	0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
	0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
	0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff
};
EXPORT_SYMBOL_GPL(gigaset_invtab);

void gigaset_dbg_buffer(enum debuglevel level, const unsigned char *msg,
			size_t len, const unsigned char *buf, int from_user)
{
	unsigned char outbuf[80];
	unsigned char inbuf[80 - 1];
	unsigned char c;
	size_t numin;
	const unsigned char *in;
	size_t space = sizeof outbuf - 1;
	unsigned char *out = outbuf;

	if (!from_user) {
		in = buf;
		numin = len;
	} else {
		numin = len < sizeof inbuf ? len : sizeof inbuf;
		in = inbuf;
		if (copy_from_user(inbuf, (const unsigned char __user *) buf,
				   numin)) {
			gig_dbg(level, "%s (%u bytes) - copy_from_user failed",
				msg, (unsigned) len);
			return;
		}
	}

	while (numin-- > 0) {
		c = *buf++;
		if (c == '~' || c == '^' || c == '\\') {
			if (space-- <= 0)
				break;
			*out++ = '\\';
		}
		if (c & 0x80) {
			if (space-- <= 0)
				break;
			*out++ = '~';
			c ^= 0x80;
		}
		if (c < 0x20 || c == 0x7f) {
			if (space-- <= 0)
				break;
			*out++ = '^';
			c ^= 0x40;
		}
		if (space-- <= 0)
			break;
		*out++ = c;
	}
	*out = 0;

	gig_dbg(level, "%s (%u bytes): %s", msg, (unsigned) len, outbuf);
}
EXPORT_SYMBOL_GPL(gigaset_dbg_buffer);

static int setflags(struct cardstate *cs, unsigned flags, unsigned delay)
{
	int r;

	r = cs->ops->set_modem_ctrl(cs, cs->control_state, flags);
	cs->control_state = flags;
	if (r < 0)
		return r;

	if (delay) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(delay * HZ / 1000);
	}

	return 0;
}

int gigaset_enterconfigmode(struct cardstate *cs)
{
	int i, r;

	if (!atomic_read(&cs->connected)) {
		err("not connected!");
		return -1;
	}

	cs->control_state = TIOCM_RTS; //FIXME

	r = setflags(cs, TIOCM_DTR, 200);
	if (r < 0)
		goto error;
	r = setflags(cs, 0, 200);
	if (r < 0)
		goto error;
	for (i = 0; i < 5; ++i) {
		r = setflags(cs, TIOCM_RTS, 100);
		if (r < 0)
			goto error;
		r = setflags(cs, 0, 100);
		if (r < 0)
			goto error;
	}
	r = setflags(cs, TIOCM_RTS|TIOCM_DTR, 800);
	if (r < 0)
		goto error;

	return 0;

error:
	dev_err(cs->dev, "error %d on setuartbits\n", -r);
	cs->control_state = TIOCM_RTS|TIOCM_DTR; // FIXME is this a good value?
	cs->ops->set_modem_ctrl(cs, 0, TIOCM_RTS|TIOCM_DTR);

	return -1; //r
}

static int test_timeout(struct at_state_t *at_state)
{
	if (!at_state->timer_expires)
		return 0;

	if (--at_state->timer_expires) {
		gig_dbg(DEBUG_MCMD, "decreased timer of %p to %lu",
			at_state, at_state->timer_expires);
		return 0;
	}

	if (!gigaset_add_event(at_state->cs, at_state, EV_TIMEOUT, NULL,
			       atomic_read(&at_state->timer_index), NULL)) {
		//FIXME what should we do?
	}

	return 1;
}

static void timer_tick(unsigned long data)
{
	struct cardstate *cs = (struct cardstate *) data;
	unsigned long flags;
	unsigned channel;
	struct at_state_t *at_state;
	int timeout = 0;

	spin_lock_irqsave(&cs->lock, flags);

	for (channel = 0; channel < cs->channels; ++channel)
		if (test_timeout(&cs->bcs[channel].at_state))
			timeout = 1;

	if (test_timeout(&cs->at_state))
		timeout = 1;

	list_for_each_entry(at_state, &cs->temp_at_states, list)
		if (test_timeout(at_state))
			timeout = 1;

	if (atomic_read(&cs->running)) {
		mod_timer(&cs->timer, jiffies + msecs_to_jiffies(GIG_TICK));
		if (timeout) {
			gig_dbg(DEBUG_CMD, "scheduling timeout");
			tasklet_schedule(&cs->event_tasklet);
		}
	}

	spin_unlock_irqrestore(&cs->lock, flags);
}

int gigaset_get_channel(struct bc_state *bcs)
{
	unsigned long flags;

	spin_lock_irqsave(&bcs->cs->lock, flags);
	if (bcs->use_count) {
		gig_dbg(DEBUG_ANY, "could not allocate channel %d",
			bcs->channel);
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		return 0;
	}
	++bcs->use_count;
	bcs->busy = 1;
	gig_dbg(DEBUG_ANY, "allocated channel %d", bcs->channel);
	spin_unlock_irqrestore(&bcs->cs->lock, flags);
	return 1;
}

void gigaset_free_channel(struct bc_state *bcs)
{
	unsigned long flags;

	spin_lock_irqsave(&bcs->cs->lock, flags);
	if (!bcs->busy) {
		gig_dbg(DEBUG_ANY, "could not free channel %d", bcs->channel);
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		return;
	}
	--bcs->use_count;
	bcs->busy = 0;
	gig_dbg(DEBUG_ANY, "freed channel %d", bcs->channel);
	spin_unlock_irqrestore(&bcs->cs->lock, flags);
}

int gigaset_get_channels(struct cardstate *cs)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&cs->lock, flags);
	for (i = 0; i < cs->channels; ++i)
		if (cs->bcs[i].use_count) {
			spin_unlock_irqrestore(&cs->lock, flags);
			gig_dbg(DEBUG_ANY, "could not allocate all channels");
			return 0;
		}
	for (i = 0; i < cs->channels; ++i)
		++cs->bcs[i].use_count;
	spin_unlock_irqrestore(&cs->lock, flags);

	gig_dbg(DEBUG_ANY, "allocated all channels");

	return 1;
}

void gigaset_free_channels(struct cardstate *cs)
{
	unsigned long flags;
	int i;

	gig_dbg(DEBUG_ANY, "unblocking all channels");
	spin_lock_irqsave(&cs->lock, flags);
	for (i = 0; i < cs->channels; ++i)
		--cs->bcs[i].use_count;
	spin_unlock_irqrestore(&cs->lock, flags);
}

void gigaset_block_channels(struct cardstate *cs)
{
	unsigned long flags;
	int i;

	gig_dbg(DEBUG_ANY, "blocking all channels");
	spin_lock_irqsave(&cs->lock, flags);
	for (i = 0; i < cs->channels; ++i)
		++cs->bcs[i].use_count;
	spin_unlock_irqrestore(&cs->lock, flags);
}

static void clear_events(struct cardstate *cs)
{
	struct event_t *ev;
	unsigned head, tail;

	/* no locking needed (no reader/writer allowed) */

	head = atomic_read(&cs->ev_head);
	tail = atomic_read(&cs->ev_tail);

	while (tail != head) {
		ev = cs->events + head;
		kfree(ev->ptr);

		head = (head + 1) % MAX_EVENTS;
	}

	atomic_set(&cs->ev_head, tail);
}

struct event_t *gigaset_add_event(struct cardstate *cs,
				  struct at_state_t *at_state, int type,
				  void *ptr, int parameter, void *arg)
{
	unsigned long flags;
	unsigned next, tail;
	struct event_t *event = NULL;

	spin_lock_irqsave(&cs->ev_lock, flags);

	tail = atomic_read(&cs->ev_tail);
	next = (tail + 1) % MAX_EVENTS;
	if (unlikely(next == atomic_read(&cs->ev_head)))
		err("event queue full");
	else {
		event = cs->events + tail;
		event->type = type;
		event->at_state = at_state;
		event->cid = -1;
		event->ptr = ptr;
		event->arg = arg;
		event->parameter = parameter;
		atomic_set(&cs->ev_tail, next);
	}

	spin_unlock_irqrestore(&cs->ev_lock, flags);

	return event;
}
EXPORT_SYMBOL_GPL(gigaset_add_event);

static void free_strings(struct at_state_t *at_state)
{
	int i;

	for (i = 0; i < STR_NUM; ++i) {
		kfree(at_state->str_var[i]);
		at_state->str_var[i] = NULL;
	}
}

static void clear_at_state(struct at_state_t *at_state)
{
	free_strings(at_state);
}

static void dealloc_at_states(struct cardstate *cs)
{
	struct at_state_t *cur, *next;

	list_for_each_entry_safe(cur, next, &cs->temp_at_states, list) {
		list_del(&cur->list);
		free_strings(cur);
		kfree(cur);
	}
}

static void gigaset_freebcs(struct bc_state *bcs)
{
	int i;

	gig_dbg(DEBUG_INIT, "freeing bcs[%d]->hw", bcs->channel);
	if (!bcs->cs->ops->freebcshw(bcs)) {
		gig_dbg(DEBUG_INIT, "failed");
	}

	gig_dbg(DEBUG_INIT, "clearing bcs[%d]->at_state", bcs->channel);
	clear_at_state(&bcs->at_state);
	gig_dbg(DEBUG_INIT, "freeing bcs[%d]->skb", bcs->channel);

	if (bcs->skb)
		dev_kfree_skb(bcs->skb);
	for (i = 0; i < AT_NUM; ++i) {
		kfree(bcs->commands[i]);
		bcs->commands[i] = NULL;
	}
}

void gigaset_freecs(struct cardstate *cs)
{
	int i;
	unsigned long flags;

	if (!cs)
		return;

	down(&cs->sem);

	if (!cs->bcs)
		goto f_cs;
	if (!cs->inbuf)
		goto f_bcs;

	spin_lock_irqsave(&cs->lock, flags);
	atomic_set(&cs->running, 0);
	spin_unlock_irqrestore(&cs->lock, flags); /* event handler and timer are
						     not rescheduled below */

	tasklet_kill(&cs->event_tasklet);
	del_timer_sync(&cs->timer);

	switch (cs->cs_init) {
	default:
		gigaset_if_free(cs);

		gig_dbg(DEBUG_INIT, "clearing hw");
		cs->ops->freecshw(cs);

		//FIXME cmdbuf

		/* fall through */
	case 2: /* error in initcshw */
		/* Deregister from LL */
		make_invalid(cs, VALID_ID);
		gig_dbg(DEBUG_INIT, "clearing iif");
		gigaset_i4l_cmd(cs, ISDN_STAT_UNLOAD);

		/* fall through */
	case 1: /* error when regestering to LL */
		gig_dbg(DEBUG_INIT, "clearing at_state");
		clear_at_state(&cs->at_state);
		dealloc_at_states(cs);

		/* fall through */
	case 0: /* error in one call to initbcs */
		for (i = 0; i < cs->channels; ++i) {
			gig_dbg(DEBUG_INIT, "clearing bcs[%d]", i);
			gigaset_freebcs(cs->bcs + i);
		}

		clear_events(cs);
		gig_dbg(DEBUG_INIT, "freeing inbuf");
		kfree(cs->inbuf);
	}
f_bcs:	gig_dbg(DEBUG_INIT, "freeing bcs[]");
	kfree(cs->bcs);
f_cs:	gig_dbg(DEBUG_INIT, "freeing cs");
	up(&cs->sem);
	free_cs(cs);
}
EXPORT_SYMBOL_GPL(gigaset_freecs);

void gigaset_at_init(struct at_state_t *at_state, struct bc_state *bcs,
		     struct cardstate *cs, int cid)
{
	int i;

	INIT_LIST_HEAD(&at_state->list);
	at_state->waiting = 0;
	at_state->getstring = 0;
	at_state->pending_commands = 0;
	at_state->timer_expires = 0;
	at_state->timer_active = 0;
	atomic_set(&at_state->timer_index, 0);
	atomic_set(&at_state->seq_index, 0);
	at_state->ConState = 0;
	for (i = 0; i < STR_NUM; ++i)
		at_state->str_var[i] = NULL;
	at_state->int_var[VAR_ZDLE] = 0;
	at_state->int_var[VAR_ZCTP] = -1;
	at_state->int_var[VAR_ZSAU] = ZSAU_NULL;
	at_state->cs = cs;
	at_state->bcs = bcs;
	at_state->cid = cid;
	if (!cid)
		at_state->replystruct = cs->tabnocid;
	else
		at_state->replystruct = cs->tabcid;
}


static void gigaset_inbuf_init(struct inbuf_t *inbuf, struct bc_state *bcs,
			       struct cardstate *cs, int inputstate)
/* inbuf->read must be allocated before! */
{
	atomic_set(&inbuf->head, 0);
	atomic_set(&inbuf->tail, 0);
	inbuf->cs = cs;
	inbuf->bcs = bcs; /*base driver: NULL*/
	inbuf->rcvbuf = NULL; //FIXME
	inbuf->inputstate = inputstate;
}

/* Initialize the b-channel structure */
static struct bc_state *gigaset_initbcs(struct bc_state *bcs,
					struct cardstate *cs, int channel)
{
	int i;

	bcs->tx_skb = NULL; //FIXME -> hw part

	skb_queue_head_init(&bcs->squeue);

	bcs->corrupted = 0;
	bcs->trans_down = 0;
	bcs->trans_up = 0;

	gig_dbg(DEBUG_INIT, "setting up bcs[%d]->at_state", channel);
	gigaset_at_init(&bcs->at_state, bcs, cs, -1);

	bcs->rcvbytes = 0;

#ifdef CONFIG_GIGASET_DEBUG
	bcs->emptycount = 0;
#endif

	gig_dbg(DEBUG_INIT, "allocating bcs[%d]->skb", channel);
	bcs->fcs = PPP_INITFCS;
	bcs->inputstate = 0;
	if (cs->ignoreframes) {
		bcs->inputstate |= INS_skip_frame;
		bcs->skb = NULL;
	} else if ((bcs->skb = dev_alloc_skb(SBUFSIZE + HW_HDR_LEN)) != NULL)
		skb_reserve(bcs->skb, HW_HDR_LEN);
	else {
		dev_warn(cs->dev, "could not allocate skb\n");
		bcs->inputstate |= INS_skip_frame;
	}

	bcs->channel = channel;
	bcs->cs = cs;

	bcs->chstate = 0;
	bcs->use_count = 1;
	bcs->busy = 0;
	bcs->ignore = cs->ignoreframes;

	for (i = 0; i < AT_NUM; ++i)
		bcs->commands[i] = NULL;

	gig_dbg(DEBUG_INIT, "  setting up bcs[%d]->hw", channel);
	if (cs->ops->initbcshw(bcs))
		return bcs;

	gig_dbg(DEBUG_INIT, "  failed");

	gig_dbg(DEBUG_INIT, "  freeing bcs[%d]->skb", channel);
	if (bcs->skb)
		dev_kfree_skb(bcs->skb);

	return NULL;
}

/* gigaset_initcs
 * Allocate and initialize cardstate structure for Gigaset driver
 * Calls hardware dependent gigaset_initcshw() function
 * Calls B channel initialization function gigaset_initbcs() for each B channel
 * parameters:
 *	drv		hardware driver the device belongs to
 *	channels	number of B channels supported by device
 *	onechannel	!=0: B channel data and AT commands share one
 *			     communication channel
 *			==0: B channels have separate communication channels
 *	ignoreframes	number of frames to ignore after setting up B channel
 *	cidmode		!=0: start in CallID mode
 *	modulename	name of driver module (used for I4L registration)
 * return value:
 *	pointer to cardstate structure
 */
struct cardstate *gigaset_initcs(struct gigaset_driver *drv, int channels,
				 int onechannel, int ignoreframes,
				 int cidmode, const char *modulename)
{
	struct cardstate *cs = NULL;
	int i;

	gig_dbg(DEBUG_INIT, "allocating cs");
	cs = alloc_cs(drv);
	if (!cs)
		goto error;
	gig_dbg(DEBUG_INIT, "allocating bcs[0..%d]", channels - 1);
	cs->bcs = kmalloc(channels * sizeof(struct bc_state), GFP_KERNEL);
	if (!cs->bcs)
		goto error;
	gig_dbg(DEBUG_INIT, "allocating inbuf");
	cs->inbuf = kmalloc(sizeof(struct inbuf_t), GFP_KERNEL);
	if (!cs->inbuf)
		goto error;

	cs->cs_init = 0;
	cs->channels = channels;
	cs->onechannel = onechannel;
	cs->ignoreframes = ignoreframes;
	INIT_LIST_HEAD(&cs->temp_at_states);
	atomic_set(&cs->running, 0);
	init_timer(&cs->timer); /* clear next & prev */
	spin_lock_init(&cs->ev_lock);
	atomic_set(&cs->ev_tail, 0);
	atomic_set(&cs->ev_head, 0);
	init_MUTEX_LOCKED(&cs->sem);
	tasklet_init(&cs->event_tasklet, &gigaset_handle_event,
		     (unsigned long) cs);
	atomic_set(&cs->commands_pending, 0);
	cs->cur_at_seq = 0;
	cs->gotfwver = -1;
	cs->open_count = 0;
	cs->dev = NULL;
	cs->tty = NULL;
	atomic_set(&cs->cidmode, cidmode != 0);

	//if(onechannel) { //FIXME
		cs->tabnocid = gigaset_tab_nocid_m10x;
		cs->tabcid = gigaset_tab_cid_m10x;
	//} else {
	//	cs->tabnocid = gigaset_tab_nocid;
	//	cs->tabcid = gigaset_tab_cid;
	//}

	init_waitqueue_head(&cs->waitqueue);
	cs->waiting = 0;

	atomic_set(&cs->mode, M_UNKNOWN);
	atomic_set(&cs->mstate, MS_UNINITIALIZED);

	for (i = 0; i < channels; ++i) {
		gig_dbg(DEBUG_INIT, "setting up bcs[%d].read", i);
		if (!gigaset_initbcs(cs->bcs + i, cs, i))
			goto error;
	}

	++cs->cs_init;

	gig_dbg(DEBUG_INIT, "setting up at_state");
	spin_lock_init(&cs->lock);
	gigaset_at_init(&cs->at_state, NULL, cs, 0);
	cs->dle = 0;
	cs->cbytes = 0;

	gig_dbg(DEBUG_INIT, "setting up inbuf");
	if (onechannel) {			//FIXME distinction necessary?
		gigaset_inbuf_init(cs->inbuf, cs->bcs, cs, INS_command);
	} else
		gigaset_inbuf_init(cs->inbuf, NULL,    cs, INS_command);

	atomic_set(&cs->connected, 0);

	gig_dbg(DEBUG_INIT, "setting up cmdbuf");
	cs->cmdbuf = cs->lastcmdbuf = NULL;
	spin_lock_init(&cs->cmdlock);
	cs->curlen = 0;
	cs->cmdbytes = 0;

	gig_dbg(DEBUG_INIT, "setting up iif");
	if (!gigaset_register_to_LL(cs, modulename)) {
		err("register_isdn failed");
		goto error;
	}

	make_valid(cs, VALID_ID);
	++cs->cs_init;
	gig_dbg(DEBUG_INIT, "setting up hw");
	if (!cs->ops->initcshw(cs))
		goto error;

	++cs->cs_init;

	gigaset_if_init(cs);

	atomic_set(&cs->running, 1);
	setup_timer(&cs->timer, timer_tick, (unsigned long) cs);
	cs->timer.expires = jiffies + msecs_to_jiffies(GIG_TICK);
	/* FIXME: can jiffies increase too much until the timer is added?
	 * Same problem(?) with mod_timer() in timer_tick(). */
	add_timer(&cs->timer);

	gig_dbg(DEBUG_INIT, "cs initialized");
	up(&cs->sem);
	return cs;

error:	if (cs)
		up(&cs->sem);
	gig_dbg(DEBUG_INIT, "failed");
	gigaset_freecs(cs);
	return NULL;
}
EXPORT_SYMBOL_GPL(gigaset_initcs);

/* ReInitialize the b-channel structure */
/* e.g. called on hangup, disconnect */
void gigaset_bcs_reinit(struct bc_state *bcs)
{
	struct sk_buff *skb;
	struct cardstate *cs = bcs->cs;
	unsigned long flags;

	while ((skb = skb_dequeue(&bcs->squeue)) != NULL)
		dev_kfree_skb(skb);

	spin_lock_irqsave(&cs->lock, flags);
	clear_at_state(&bcs->at_state);
	bcs->at_state.ConState = 0;
	bcs->at_state.timer_active = 0;
	bcs->at_state.timer_expires = 0;
	bcs->at_state.cid = -1;			/* No CID defined */
	spin_unlock_irqrestore(&cs->lock, flags);

	bcs->inputstate = 0;

#ifdef CONFIG_GIGASET_DEBUG
	bcs->emptycount = 0;
#endif

	bcs->fcs = PPP_INITFCS;
	bcs->chstate = 0;

	bcs->ignore = cs->ignoreframes;
	if (bcs->ignore)
		bcs->inputstate |= INS_skip_frame;


	cs->ops->reinitbcshw(bcs);
}

static void cleanup_cs(struct cardstate *cs)
{
	struct cmdbuf_t *cb, *tcb;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&cs->lock, flags);

	atomic_set(&cs->mode, M_UNKNOWN);
	atomic_set(&cs->mstate, MS_UNINITIALIZED);

	clear_at_state(&cs->at_state);
	dealloc_at_states(cs);
	free_strings(&cs->at_state);
	gigaset_at_init(&cs->at_state, NULL, cs, 0);

	kfree(cs->inbuf->rcvbuf);
	cs->inbuf->rcvbuf = NULL;
	cs->inbuf->inputstate = INS_command;
	atomic_set(&cs->inbuf->head, 0);
	atomic_set(&cs->inbuf->tail, 0);

	cb = cs->cmdbuf;
	while (cb) {
		tcb = cb;
		cb = cb->next;
		kfree(tcb);
	}
	cs->cmdbuf = cs->lastcmdbuf = NULL;
	cs->curlen = 0;
	cs->cmdbytes = 0;
	cs->gotfwver = -1;
	cs->dle = 0;
	cs->cur_at_seq = 0;
	atomic_set(&cs->commands_pending, 0);
	cs->cbytes = 0;

	spin_unlock_irqrestore(&cs->lock, flags);

	for (i = 0; i < cs->channels; ++i) {
		gigaset_freebcs(cs->bcs + i);
		if (!gigaset_initbcs(cs->bcs + i, cs, i))
			break;			//FIXME error handling
	}

	if (cs->waiting) {
		cs->cmd_result = -ENODEV;
		cs->waiting = 0;
		wake_up_interruptible(&cs->waitqueue);
	}
}


int gigaset_start(struct cardstate *cs)
{
	if (down_interruptible(&cs->sem))
		return 0;

	atomic_set(&cs->connected, 1);

	if (atomic_read(&cs->mstate) != MS_LOCKED) {
		cs->ops->set_modem_ctrl(cs, 0, TIOCM_DTR|TIOCM_RTS);
		cs->ops->baud_rate(cs, B115200);
		cs->ops->set_line_ctrl(cs, CS8);
		cs->control_state = TIOCM_DTR|TIOCM_RTS;
	} else {
		//FIXME use some saved values?
	}

	cs->waiting = 1;

	if (!gigaset_add_event(cs, &cs->at_state, EV_START, NULL, 0, NULL)) {
		cs->waiting = 0;
		//FIXME what should we do?
		goto error;
	}

	gig_dbg(DEBUG_CMD, "scheduling START");
	gigaset_schedule_event(cs);

	wait_event(cs->waitqueue, !cs->waiting);

	/* set up device sysfs */
	gigaset_init_dev_sysfs(cs);

	up(&cs->sem);
	return 1;

error:
	up(&cs->sem);
	return 0;
}
EXPORT_SYMBOL_GPL(gigaset_start);

void gigaset_shutdown(struct cardstate *cs)
{
	down(&cs->sem);

	cs->waiting = 1;

	if (!gigaset_add_event(cs, &cs->at_state, EV_SHUTDOWN, NULL, 0, NULL)) {
		//FIXME what should we do?
		goto exit;
	}

	gig_dbg(DEBUG_CMD, "scheduling SHUTDOWN");
	gigaset_schedule_event(cs);

	if (wait_event_interruptible(cs->waitqueue, !cs->waiting)) {
		warn("%s: aborted", __func__);
		//FIXME
	}

	if (atomic_read(&cs->mstate) != MS_LOCKED) {
		//FIXME?
		//gigaset_baud_rate(cs, B115200);
		//gigaset_set_line_ctrl(cs, CS8);
		//gigaset_set_modem_ctrl(cs, TIOCM_DTR|TIOCM_RTS, 0);
		//cs->control_state = 0;
	} else {
		//FIXME use some saved values?
	}

	cleanup_cs(cs);

exit:
	up(&cs->sem);
}
EXPORT_SYMBOL_GPL(gigaset_shutdown);

void gigaset_stop(struct cardstate *cs)
{
	down(&cs->sem);

	/* clear device sysfs */
	gigaset_free_dev_sysfs(cs);

	atomic_set(&cs->connected, 0);

	cs->waiting = 1;

	if (!gigaset_add_event(cs, &cs->at_state, EV_STOP, NULL, 0, NULL)) {
		//FIXME what should we do?
		goto exit;
	}

	gig_dbg(DEBUG_CMD, "scheduling STOP");
	gigaset_schedule_event(cs);

	if (wait_event_interruptible(cs->waitqueue, !cs->waiting)) {
		warn("%s: aborted", __func__);
		//FIXME
	}

	/* Tell the LL that the device is not available .. */
	gigaset_i4l_cmd(cs, ISDN_STAT_STOP); // FIXME move to event layer?

	cleanup_cs(cs);

exit:
	up(&cs->sem);
}
EXPORT_SYMBOL_GPL(gigaset_stop);

static LIST_HEAD(drivers);
static spinlock_t driver_lock = SPIN_LOCK_UNLOCKED;

struct cardstate *gigaset_get_cs_by_id(int id)
{
	unsigned long flags;
	static struct cardstate *ret = NULL;
	static struct cardstate *cs;
	struct gigaset_driver *drv;
	unsigned i;

	spin_lock_irqsave(&driver_lock, flags);
	list_for_each_entry(drv, &drivers, list) {
		spin_lock(&drv->lock);
		for (i = 0; i < drv->minors; ++i) {
			if (drv->flags[i] & VALID_ID) {
				cs = drv->cs + i;
				if (cs->myid == id)
					ret = cs;
			}
			if (ret)
				break;
		}
		spin_unlock(&drv->lock);
		if (ret)
			break;
	}
	spin_unlock_irqrestore(&driver_lock, flags);
	return ret;
}

void gigaset_debugdrivers(void)
{
	unsigned long flags;
	static struct cardstate *cs;
	struct gigaset_driver *drv;
	unsigned i;

	spin_lock_irqsave(&driver_lock, flags);
	list_for_each_entry(drv, &drivers, list) {
		gig_dbg(DEBUG_DRIVER, "driver %p", drv);
		spin_lock(&drv->lock);
		for (i = 0; i < drv->minors; ++i) {
			gig_dbg(DEBUG_DRIVER, "  index %u", i);
			gig_dbg(DEBUG_DRIVER, "    flags 0x%02x",
				drv->flags[i]);
			cs = drv->cs + i;
			gig_dbg(DEBUG_DRIVER, "    cardstate %p", cs);
			gig_dbg(DEBUG_DRIVER, "    minor_index %u",
				cs->minor_index);
			gig_dbg(DEBUG_DRIVER, "    driver %p", cs->driver);
			gig_dbg(DEBUG_DRIVER, "    i4l id %d", cs->myid);
		}
		spin_unlock(&drv->lock);
	}
	spin_unlock_irqrestore(&driver_lock, flags);
}
EXPORT_SYMBOL_GPL(gigaset_debugdrivers);

struct cardstate *gigaset_get_cs_by_tty(struct tty_struct *tty)
{
	if (tty->index < 0 || tty->index >= tty->driver->num)
		return NULL;
	return gigaset_get_cs_by_minor(tty->index + tty->driver->minor_start);
}

struct cardstate *gigaset_get_cs_by_minor(unsigned minor)
{
	unsigned long flags;
	static struct cardstate *ret = NULL;
	struct gigaset_driver *drv;
	unsigned index;

	spin_lock_irqsave(&driver_lock, flags);
	list_for_each_entry(drv, &drivers, list) {
		if (minor < drv->minor || minor >= drv->minor + drv->minors)
			continue;
		index = minor - drv->minor;
		spin_lock(&drv->lock);
		if (drv->flags[index] & VALID_MINOR)
			ret = drv->cs + index;
		spin_unlock(&drv->lock);
		if (ret)
			break;
	}
	spin_unlock_irqrestore(&driver_lock, flags);
	return ret;
}

void gigaset_freedriver(struct gigaset_driver *drv)
{
	unsigned long flags;

	spin_lock_irqsave(&driver_lock, flags);
	list_del(&drv->list);
	spin_unlock_irqrestore(&driver_lock, flags);

	gigaset_if_freedriver(drv);
	module_put(drv->owner);

	kfree(drv->cs);
	kfree(drv->flags);
	kfree(drv);
}
EXPORT_SYMBOL_GPL(gigaset_freedriver);

/* gigaset_initdriver
 * Allocate and initialize gigaset_driver structure. Initialize interface.
 * parameters:
 *	minor		First minor number
 *	minors		Number of minors this driver can handle
 *	procname	Name of the driver
 *	devname		Name of the device files (prefix without minor number)
 *	devfsname	Devfs name of the device files without %d
 * return value:
 *	Pointer to the gigaset_driver structure on success, NULL on failure.
 */
struct gigaset_driver *gigaset_initdriver(unsigned minor, unsigned minors,
					  const char *procname,
					  const char *devname,
					  const char *devfsname,
					  const struct gigaset_ops *ops,
					  struct module *owner)
{
	struct gigaset_driver *drv;
	unsigned long flags;
	unsigned i;

	drv = kmalloc(sizeof *drv, GFP_KERNEL);
	if (!drv)
		return NULL;
	if (!try_module_get(owner))
		return NULL;

	drv->cs = NULL;
	drv->have_tty = 0;
	drv->minor = minor;
	drv->minors = minors;
	spin_lock_init(&drv->lock);
	drv->blocked = 0;
	drv->ops = ops;
	drv->owner = owner;
	INIT_LIST_HEAD(&drv->list);

	drv->cs = kmalloc(minors * sizeof *drv->cs, GFP_KERNEL);
	if (!drv->cs)
		goto out1;
	drv->flags = kmalloc(minors * sizeof *drv->flags, GFP_KERNEL);
	if (!drv->flags)
		goto out2;

	for (i = 0; i < minors; ++i) {
		drv->flags[i] = 0;
		drv->cs[i].driver = drv;
		drv->cs[i].ops = drv->ops;
		drv->cs[i].minor_index = i;
	}

	gigaset_if_initdriver(drv, procname, devname, devfsname);

	spin_lock_irqsave(&driver_lock, flags);
	list_add(&drv->list, &drivers);
	spin_unlock_irqrestore(&driver_lock, flags);

	return drv;

out2:
	kfree(drv->cs);
out1:
	kfree(drv);
	module_put(owner);
	return NULL;
}
EXPORT_SYMBOL_GPL(gigaset_initdriver);

static struct cardstate *alloc_cs(struct gigaset_driver *drv)
{
	unsigned long flags;
	unsigned i;
	static struct cardstate *ret = NULL;

	spin_lock_irqsave(&drv->lock, flags);
	for (i = 0; i < drv->minors; ++i) {
		if (!(drv->flags[i] & VALID_MINOR)) {
			drv->flags[i] = VALID_MINOR;
			ret = drv->cs + i;
		}
		if (ret)
			break;
	}
	spin_unlock_irqrestore(&drv->lock, flags);
	return ret;
}

static void free_cs(struct cardstate *cs)
{
	unsigned long flags;
	struct gigaset_driver *drv = cs->driver;
	spin_lock_irqsave(&drv->lock, flags);
	drv->flags[cs->minor_index] = 0;
	spin_unlock_irqrestore(&drv->lock, flags);
}

static void make_valid(struct cardstate *cs, unsigned mask)
{
	unsigned long flags;
	struct gigaset_driver *drv = cs->driver;
	spin_lock_irqsave(&drv->lock, flags);
	drv->flags[cs->minor_index] |= mask;
	spin_unlock_irqrestore(&drv->lock, flags);
}

static void make_invalid(struct cardstate *cs, unsigned mask)
{
	unsigned long flags;
	struct gigaset_driver *drv = cs->driver;
	spin_lock_irqsave(&drv->lock, flags);
	drv->flags[cs->minor_index] &= ~mask;
	spin_unlock_irqrestore(&drv->lock, flags);
}

/* For drivers without fixed assignment device<->cardstate (usb) */
struct cardstate *gigaset_getunassignedcs(struct gigaset_driver *drv)
{
	unsigned long flags;
	struct cardstate *cs = NULL;
	unsigned i;

	spin_lock_irqsave(&drv->lock, flags);
	if (drv->blocked)
		goto exit;
	for (i = 0; i < drv->minors; ++i) {
		if ((drv->flags[i] & VALID_MINOR) &&
		    !(drv->flags[i] & ASSIGNED)) {
			drv->flags[i] |= ASSIGNED;
			cs = drv->cs + i;
			break;
		}
	}
exit:
	spin_unlock_irqrestore(&drv->lock, flags);
	return cs;
}
EXPORT_SYMBOL_GPL(gigaset_getunassignedcs);

void gigaset_unassign(struct cardstate *cs)
{
	unsigned long flags;
	unsigned *minor_flags;
	struct gigaset_driver *drv;

	if (!cs)
		return;
	drv = cs->driver;
	spin_lock_irqsave(&drv->lock, flags);
	minor_flags = drv->flags + cs->minor_index;
	if (*minor_flags & VALID_MINOR)
		*minor_flags &= ~ASSIGNED;
	spin_unlock_irqrestore(&drv->lock, flags);
}
EXPORT_SYMBOL_GPL(gigaset_unassign);

void gigaset_blockdriver(struct gigaset_driver *drv)
{
	unsigned long flags;
	spin_lock_irqsave(&drv->lock, flags);
	drv->blocked = 1;
	spin_unlock_irqrestore(&drv->lock, flags);
}
EXPORT_SYMBOL_GPL(gigaset_blockdriver);

static int __init gigaset_init_module(void)
{
	/* in accordance with the principle of least astonishment,
	 * setting the 'debug' parameter to 1 activates a sensible
	 * set of default debug levels
	 */
	if (gigaset_debuglevel == 1)
		gigaset_debuglevel = DEBUG_DEFAULT;

	info(DRIVER_AUTHOR);
	info(DRIVER_DESC);
	return 0;
}

static void __exit gigaset_exit_module(void)
{
}

module_init(gigaset_init_module);
module_exit(gigaset_exit_module);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

MODULE_LICENSE("GPL");
