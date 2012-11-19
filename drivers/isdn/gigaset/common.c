/*
 * Stuff used by all variants of the driver
 *
 * Copyright (c) 2001 by Stefan Eilers,
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
#include <linux/module.h>
#include <linux/moduleparam.h>

/* Version Information */
#define DRIVER_AUTHOR "Hansjoerg Lipp <hjlipp@web.de>, Tilman Schmidt <tilman@imap.cc>, Stefan Eilers"
#define DRIVER_DESC "Driver for Gigaset 307x"

#ifdef CONFIG_GIGASET_DEBUG
#define DRIVER_DESC_DEBUG " (debug build)"
#else
#define DRIVER_DESC_DEBUG ""
#endif

/* Module parameters */
int gigaset_debuglevel;
EXPORT_SYMBOL_GPL(gigaset_debuglevel);
module_param_named(debug, gigaset_debuglevel, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "debug level");

/* driver state flags */
#define VALID_MINOR	0x01
#define VALID_ID	0x02

/**
 * gigaset_dbg_buffer() - dump data in ASCII and hex for debugging
 * @level:	debugging level.
 * @msg:	message prefix.
 * @len:	number of bytes to dump.
 * @buf:	data to dump.
 *
 * If the current debugging level includes one of the bits set in @level,
 * @len bytes starting at @buf are logged to dmesg at KERN_DEBUG prio,
 * prefixed by the text @msg.
 */
void gigaset_dbg_buffer(enum debuglevel level, const unsigned char *msg,
			size_t len, const unsigned char *buf)
{
	unsigned char outbuf[80];
	unsigned char c;
	size_t space = sizeof outbuf - 1;
	unsigned char *out = outbuf;
	size_t numin = len;

	while (numin--) {
		c = *buf++;
		if (c == '~' || c == '^' || c == '\\') {
			if (!space--)
				break;
			*out++ = '\\';
		}
		if (c & 0x80) {
			if (!space--)
				break;
			*out++ = '~';
			c ^= 0x80;
		}
		if (c < 0x20 || c == 0x7f) {
			if (!space--)
				break;
			*out++ = '^';
			c ^= 0x40;
		}
		if (!space--)
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

	cs->control_state = TIOCM_RTS;

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
	r = setflags(cs, TIOCM_RTS | TIOCM_DTR, 800);
	if (r < 0)
		goto error;

	return 0;

error:
	dev_err(cs->dev, "error %d on setuartbits\n", -r);
	cs->control_state = TIOCM_RTS | TIOCM_DTR;
	cs->ops->set_modem_ctrl(cs, 0, TIOCM_RTS | TIOCM_DTR);

	return -1;
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

	gigaset_add_event(at_state->cs, at_state, EV_TIMEOUT, NULL,
			  at_state->timer_index, NULL);
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

	if (cs->running) {
		mod_timer(&cs->timer, jiffies + msecs_to_jiffies(GIG_TICK));
		if (timeout) {
			gig_dbg(DEBUG_EVENT, "scheduling timeout");
			tasklet_schedule(&cs->event_tasklet);
		}
	}

	spin_unlock_irqrestore(&cs->lock, flags);
}

int gigaset_get_channel(struct bc_state *bcs)
{
	unsigned long flags;

	spin_lock_irqsave(&bcs->cs->lock, flags);
	if (bcs->use_count || !try_module_get(bcs->cs->driver->owner)) {
		gig_dbg(DEBUG_CHANNEL, "could not allocate channel %d",
			bcs->channel);
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		return -EBUSY;
	}
	++bcs->use_count;
	bcs->busy = 1;
	gig_dbg(DEBUG_CHANNEL, "allocated channel %d", bcs->channel);
	spin_unlock_irqrestore(&bcs->cs->lock, flags);
	return 0;
}

struct bc_state *gigaset_get_free_channel(struct cardstate *cs)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&cs->lock, flags);
	if (!try_module_get(cs->driver->owner)) {
		gig_dbg(DEBUG_CHANNEL,
			"could not get module for allocating channel");
		spin_unlock_irqrestore(&cs->lock, flags);
		return NULL;
	}
	for (i = 0; i < cs->channels; ++i)
		if (!cs->bcs[i].use_count) {
			++cs->bcs[i].use_count;
			cs->bcs[i].busy = 1;
			spin_unlock_irqrestore(&cs->lock, flags);
			gig_dbg(DEBUG_CHANNEL, "allocated channel %d", i);
			return cs->bcs + i;
		}
	module_put(cs->driver->owner);
	spin_unlock_irqrestore(&cs->lock, flags);
	gig_dbg(DEBUG_CHANNEL, "no free channel");
	return NULL;
}

void gigaset_free_channel(struct bc_state *bcs)
{
	unsigned long flags;

	spin_lock_irqsave(&bcs->cs->lock, flags);
	if (!bcs->busy) {
		gig_dbg(DEBUG_CHANNEL, "could not free channel %d",
			bcs->channel);
		spin_unlock_irqrestore(&bcs->cs->lock, flags);
		return;
	}
	--bcs->use_count;
	bcs->busy = 0;
	module_put(bcs->cs->driver->owner);
	gig_dbg(DEBUG_CHANNEL, "freed channel %d", bcs->channel);
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
			gig_dbg(DEBUG_CHANNEL,
				"could not allocate all channels");
			return -EBUSY;
		}
	for (i = 0; i < cs->channels; ++i)
		++cs->bcs[i].use_count;
	spin_unlock_irqrestore(&cs->lock, flags);

	gig_dbg(DEBUG_CHANNEL, "allocated all channels");

	return 0;
}

void gigaset_free_channels(struct cardstate *cs)
{
	unsigned long flags;
	int i;

	gig_dbg(DEBUG_CHANNEL, "unblocking all channels");
	spin_lock_irqsave(&cs->lock, flags);
	for (i = 0; i < cs->channels; ++i)
		--cs->bcs[i].use_count;
	spin_unlock_irqrestore(&cs->lock, flags);
}

void gigaset_block_channels(struct cardstate *cs)
{
	unsigned long flags;
	int i;

	gig_dbg(DEBUG_CHANNEL, "blocking all channels");
	spin_lock_irqsave(&cs->lock, flags);
	for (i = 0; i < cs->channels; ++i)
		++cs->bcs[i].use_count;
	spin_unlock_irqrestore(&cs->lock, flags);
}

static void clear_events(struct cardstate *cs)
{
	struct event_t *ev;
	unsigned head, tail;
	unsigned long flags;

	spin_lock_irqsave(&cs->ev_lock, flags);

	head = cs->ev_head;
	tail = cs->ev_tail;

	while (tail != head) {
		ev = cs->events + head;
		kfree(ev->ptr);
		head = (head + 1) % MAX_EVENTS;
	}

	cs->ev_head = tail;

	spin_unlock_irqrestore(&cs->ev_lock, flags);
}

/**
 * gigaset_add_event() - add event to device event queue
 * @cs:		device descriptor structure.
 * @at_state:	connection state structure.
 * @type:	event type.
 * @ptr:	pointer parameter for event.
 * @parameter:	integer parameter for event.
 * @arg:	pointer parameter for event.
 *
 * Allocate an event queue entry from the device's event queue, and set it up
 * with the parameters given.
 *
 * Return value: added event
 */
struct event_t *gigaset_add_event(struct cardstate *cs,
				  struct at_state_t *at_state, int type,
				  void *ptr, int parameter, void *arg)
{
	unsigned long flags;
	unsigned next, tail;
	struct event_t *event = NULL;

	gig_dbg(DEBUG_EVENT, "queueing event %d", type);

	spin_lock_irqsave(&cs->ev_lock, flags);

	tail = cs->ev_tail;
	next = (tail + 1) % MAX_EVENTS;
	if (unlikely(next == cs->ev_head))
		dev_err(cs->dev, "event queue full\n");
	else {
		event = cs->events + tail;
		event->type = type;
		event->at_state = at_state;
		event->cid = -1;
		event->ptr = ptr;
		event->arg = arg;
		event->parameter = parameter;
		cs->ev_tail = next;
	}

	spin_unlock_irqrestore(&cs->ev_lock, flags);

	return event;
}
EXPORT_SYMBOL_GPL(gigaset_add_event);

static void clear_at_state(struct at_state_t *at_state)
{
	int i;

	for (i = 0; i < STR_NUM; ++i) {
		kfree(at_state->str_var[i]);
		at_state->str_var[i] = NULL;
	}
}

static void dealloc_temp_at_states(struct cardstate *cs)
{
	struct at_state_t *cur, *next;

	list_for_each_entry_safe(cur, next, &cs->temp_at_states, list) {
		list_del(&cur->list);
		clear_at_state(cur);
		kfree(cur);
	}
}

static void gigaset_freebcs(struct bc_state *bcs)
{
	int i;

	gig_dbg(DEBUG_INIT, "freeing bcs[%d]->hw", bcs->channel);
	bcs->cs->ops->freebcshw(bcs);

	gig_dbg(DEBUG_INIT, "clearing bcs[%d]->at_state", bcs->channel);
	clear_at_state(&bcs->at_state);
	gig_dbg(DEBUG_INIT, "freeing bcs[%d]->skb", bcs->channel);
	dev_kfree_skb(bcs->rx_skb);
	bcs->rx_skb = NULL;

	for (i = 0; i < AT_NUM; ++i) {
		kfree(bcs->commands[i]);
		bcs->commands[i] = NULL;
	}
}

static struct cardstate *alloc_cs(struct gigaset_driver *drv)
{
	unsigned long flags;
	unsigned i;
	struct cardstate *cs;
	struct cardstate *ret = NULL;

	spin_lock_irqsave(&drv->lock, flags);
	if (drv->blocked)
		goto exit;
	for (i = 0; i < drv->minors; ++i) {
		cs = drv->cs + i;
		if (!(cs->flags & VALID_MINOR)) {
			cs->flags = VALID_MINOR;
			ret = cs;
			break;
		}
	}
exit:
	spin_unlock_irqrestore(&drv->lock, flags);
	return ret;
}

static void free_cs(struct cardstate *cs)
{
	cs->flags = 0;
}

static void make_valid(struct cardstate *cs, unsigned mask)
{
	unsigned long flags;
	struct gigaset_driver *drv = cs->driver;
	spin_lock_irqsave(&drv->lock, flags);
	cs->flags |= mask;
	spin_unlock_irqrestore(&drv->lock, flags);
}

static void make_invalid(struct cardstate *cs, unsigned mask)
{
	unsigned long flags;
	struct gigaset_driver *drv = cs->driver;
	spin_lock_irqsave(&drv->lock, flags);
	cs->flags &= ~mask;
	spin_unlock_irqrestore(&drv->lock, flags);
}

/**
 * gigaset_freecs() - free all associated ressources of a device
 * @cs:		device descriptor structure.
 *
 * Stops all tasklets and timers, unregisters the device from all
 * subsystems it was registered to, deallocates the device structure
 * @cs and all structures referenced from it.
 * Operations on the device should be stopped before calling this.
 */
void gigaset_freecs(struct cardstate *cs)
{
	int i;
	unsigned long flags;

	if (!cs)
		return;

	mutex_lock(&cs->mutex);

	if (!cs->bcs)
		goto f_cs;
	if (!cs->inbuf)
		goto f_bcs;

	spin_lock_irqsave(&cs->lock, flags);
	cs->running = 0;
	spin_unlock_irqrestore(&cs->lock, flags); /* event handler and timer are
						     not rescheduled below */

	tasklet_kill(&cs->event_tasklet);
	del_timer_sync(&cs->timer);

	switch (cs->cs_init) {
	default:
		/* clear B channel structures */
		for (i = 0; i < cs->channels; ++i) {
			gig_dbg(DEBUG_INIT, "clearing bcs[%d]", i);
			gigaset_freebcs(cs->bcs + i);
		}

		/* clear device sysfs */
		gigaset_free_dev_sysfs(cs);

		gigaset_if_free(cs);

		gig_dbg(DEBUG_INIT, "clearing hw");
		cs->ops->freecshw(cs);

		/* fall through */
	case 2: /* error in initcshw */
		/* Deregister from LL */
		make_invalid(cs, VALID_ID);
		gigaset_isdn_unregdev(cs);

		/* fall through */
	case 1: /* error when registering to LL */
		gig_dbg(DEBUG_INIT, "clearing at_state");
		clear_at_state(&cs->at_state);
		dealloc_temp_at_states(cs);
		tty_port_destroy(&cs->port);

		/* fall through */
	case 0:	/* error in basic setup */
		clear_events(cs);
		gig_dbg(DEBUG_INIT, "freeing inbuf");
		kfree(cs->inbuf);
	}
f_bcs:	gig_dbg(DEBUG_INIT, "freeing bcs[]");
	kfree(cs->bcs);
f_cs:	gig_dbg(DEBUG_INIT, "freeing cs");
	mutex_unlock(&cs->mutex);
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
	at_state->timer_index = 0;
	at_state->seq_index = 0;
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


static void gigaset_inbuf_init(struct inbuf_t *inbuf, struct cardstate *cs)
/* inbuf->read must be allocated before! */
{
	inbuf->head = 0;
	inbuf->tail = 0;
	inbuf->cs = cs;
	inbuf->inputstate = INS_command;
}

/**
 * gigaset_fill_inbuf() - append received data to input buffer
 * @inbuf:	buffer structure.
 * @src:	received data.
 * @numbytes:	number of bytes received.
 *
 * Return value: !=0 if some data was appended
 */
int gigaset_fill_inbuf(struct inbuf_t *inbuf, const unsigned char *src,
		       unsigned numbytes)
{
	unsigned n, head, tail, bytesleft;

	gig_dbg(DEBUG_INTR, "received %u bytes", numbytes);

	if (!numbytes)
		return 0;

	bytesleft = numbytes;
	tail = inbuf->tail;
	head = inbuf->head;
	gig_dbg(DEBUG_INTR, "buffer state: %u -> %u", head, tail);

	while (bytesleft) {
		if (head > tail)
			n = head - 1 - tail;
		else if (head == 0)
			n = (RBUFSIZE - 1) - tail;
		else
			n = RBUFSIZE - tail;
		if (!n) {
			dev_err(inbuf->cs->dev,
				"buffer overflow (%u bytes lost)\n",
				bytesleft);
			break;
		}
		if (n > bytesleft)
			n = bytesleft;
		memcpy(inbuf->data + tail, src, n);
		bytesleft -= n;
		tail = (tail + n) % RBUFSIZE;
		src += n;
	}
	gig_dbg(DEBUG_INTR, "setting tail to %u", tail);
	inbuf->tail = tail;
	return numbytes != bytesleft;
}
EXPORT_SYMBOL_GPL(gigaset_fill_inbuf);

/* Initialize the b-channel structure */
static int gigaset_initbcs(struct bc_state *bcs, struct cardstate *cs,
			   int channel)
{
	int i;

	bcs->tx_skb = NULL;

	skb_queue_head_init(&bcs->squeue);

	bcs->corrupted = 0;
	bcs->trans_down = 0;
	bcs->trans_up = 0;

	gig_dbg(DEBUG_INIT, "setting up bcs[%d]->at_state", channel);
	gigaset_at_init(&bcs->at_state, bcs, cs, -1);

#ifdef CONFIG_GIGASET_DEBUG
	bcs->emptycount = 0;
#endif

	bcs->rx_bufsize = 0;
	bcs->rx_skb = NULL;
	bcs->rx_fcs = PPP_INITFCS;
	bcs->inputstate = 0;
	bcs->channel = channel;
	bcs->cs = cs;

	bcs->chstate = 0;
	bcs->use_count = 1;
	bcs->busy = 0;
	bcs->ignore = cs->ignoreframes;

	for (i = 0; i < AT_NUM; ++i)
		bcs->commands[i] = NULL;

	spin_lock_init(&bcs->aplock);
	bcs->ap = NULL;
	bcs->apconnstate = 0;

	gig_dbg(DEBUG_INIT, "  setting up bcs[%d]->hw", channel);
	return cs->ops->initbcshw(bcs);
}

/**
 * gigaset_initcs() - initialize device structure
 * @drv:	hardware driver the device belongs to
 * @channels:	number of B channels supported by device
 * @onechannel:	!=0 if B channel data and AT commands share one
 *		    communication channel (M10x),
 *		==0 if B channels have separate communication channels (base)
 * @ignoreframes:	number of frames to ignore after setting up B channel
 * @cidmode:	!=0: start in CallID mode
 * @modulename:	name of driver module for LL registration
 *
 * Allocate and initialize cardstate structure for Gigaset driver
 * Calls hardware dependent gigaset_initcshw() function
 * Calls B channel initialization function gigaset_initbcs() for each B channel
 *
 * Return value:
 *	pointer to cardstate structure
 */
struct cardstate *gigaset_initcs(struct gigaset_driver *drv, int channels,
				 int onechannel, int ignoreframes,
				 int cidmode, const char *modulename)
{
	struct cardstate *cs;
	unsigned long flags;
	int i;

	gig_dbg(DEBUG_INIT, "allocating cs");
	cs = alloc_cs(drv);
	if (!cs) {
		pr_err("maximum number of devices exceeded\n");
		return NULL;
	}

	gig_dbg(DEBUG_INIT, "allocating bcs[0..%d]", channels - 1);
	cs->bcs = kmalloc(channels * sizeof(struct bc_state), GFP_KERNEL);
	if (!cs->bcs) {
		pr_err("out of memory\n");
		goto error;
	}
	gig_dbg(DEBUG_INIT, "allocating inbuf");
	cs->inbuf = kmalloc(sizeof(struct inbuf_t), GFP_KERNEL);
	if (!cs->inbuf) {
		pr_err("out of memory\n");
		goto error;
	}

	cs->cs_init = 0;
	cs->channels = channels;
	cs->onechannel = onechannel;
	cs->ignoreframes = ignoreframes;
	INIT_LIST_HEAD(&cs->temp_at_states);
	cs->running = 0;
	init_timer(&cs->timer); /* clear next & prev */
	spin_lock_init(&cs->ev_lock);
	cs->ev_tail = 0;
	cs->ev_head = 0;

	tasklet_init(&cs->event_tasklet, gigaset_handle_event,
		     (unsigned long) cs);
	tty_port_init(&cs->port);
	cs->commands_pending = 0;
	cs->cur_at_seq = 0;
	cs->gotfwver = -1;
	cs->dev = NULL;
	cs->tty_dev = NULL;
	cs->cidmode = cidmode != 0;
	cs->tabnocid = gigaset_tab_nocid;
	cs->tabcid = gigaset_tab_cid;

	init_waitqueue_head(&cs->waitqueue);
	cs->waiting = 0;

	cs->mode = M_UNKNOWN;
	cs->mstate = MS_UNINITIALIZED;

	++cs->cs_init;

	gig_dbg(DEBUG_INIT, "setting up at_state");
	spin_lock_init(&cs->lock);
	gigaset_at_init(&cs->at_state, NULL, cs, 0);
	cs->dle = 0;
	cs->cbytes = 0;

	gig_dbg(DEBUG_INIT, "setting up inbuf");
	gigaset_inbuf_init(cs->inbuf, cs);

	cs->connected = 0;
	cs->isdn_up = 0;

	gig_dbg(DEBUG_INIT, "setting up cmdbuf");
	cs->cmdbuf = cs->lastcmdbuf = NULL;
	spin_lock_init(&cs->cmdlock);
	cs->curlen = 0;
	cs->cmdbytes = 0;

	gig_dbg(DEBUG_INIT, "setting up iif");
	if (gigaset_isdn_regdev(cs, modulename) < 0) {
		pr_err("error registering ISDN device\n");
		goto error;
	}

	make_valid(cs, VALID_ID);
	++cs->cs_init;
	gig_dbg(DEBUG_INIT, "setting up hw");
	if (cs->ops->initcshw(cs) < 0)
		goto error;

	++cs->cs_init;

	/* set up character device */
	gigaset_if_init(cs);

	/* set up device sysfs */
	gigaset_init_dev_sysfs(cs);

	/* set up channel data structures */
	for (i = 0; i < channels; ++i) {
		gig_dbg(DEBUG_INIT, "setting up bcs[%d]", i);
		if (gigaset_initbcs(cs->bcs + i, cs, i) < 0) {
			pr_err("could not allocate channel %d data\n", i);
			goto error;
		}
	}

	spin_lock_irqsave(&cs->lock, flags);
	cs->running = 1;
	spin_unlock_irqrestore(&cs->lock, flags);
	setup_timer(&cs->timer, timer_tick, (unsigned long) cs);
	cs->timer.expires = jiffies + msecs_to_jiffies(GIG_TICK);
	add_timer(&cs->timer);

	gig_dbg(DEBUG_INIT, "cs initialized");
	return cs;

error:
	gig_dbg(DEBUG_INIT, "failed");
	gigaset_freecs(cs);
	return NULL;
}
EXPORT_SYMBOL_GPL(gigaset_initcs);

/* ReInitialize the b-channel structure on hangup */
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

	bcs->rx_fcs = PPP_INITFCS;
	bcs->chstate = 0;

	bcs->ignore = cs->ignoreframes;
	dev_kfree_skb(bcs->rx_skb);
	bcs->rx_skb = NULL;

	cs->ops->reinitbcshw(bcs);
}

static void cleanup_cs(struct cardstate *cs)
{
	struct cmdbuf_t *cb, *tcb;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&cs->lock, flags);

	cs->mode = M_UNKNOWN;
	cs->mstate = MS_UNINITIALIZED;

	clear_at_state(&cs->at_state);
	dealloc_temp_at_states(cs);
	gigaset_at_init(&cs->at_state, NULL, cs, 0);

	cs->inbuf->inputstate = INS_command;
	cs->inbuf->head = 0;
	cs->inbuf->tail = 0;

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
	cs->commands_pending = 0;
	cs->cbytes = 0;

	spin_unlock_irqrestore(&cs->lock, flags);

	for (i = 0; i < cs->channels; ++i) {
		gigaset_freebcs(cs->bcs + i);
		if (gigaset_initbcs(cs->bcs + i, cs, i) < 0)
			pr_err("could not allocate channel %d data\n", i);
	}

	if (cs->waiting) {
		cs->cmd_result = -ENODEV;
		cs->waiting = 0;
		wake_up_interruptible(&cs->waitqueue);
	}
}


/**
 * gigaset_start() - start device operations
 * @cs:		device descriptor structure.
 *
 * Prepares the device for use by setting up communication parameters,
 * scheduling an EV_START event to initiate device initialization, and
 * waiting for completion of the initialization.
 *
 * Return value:
 *	0 on success, error code < 0 on failure
 */
int gigaset_start(struct cardstate *cs)
{
	unsigned long flags;

	if (mutex_lock_interruptible(&cs->mutex))
		return -EBUSY;

	spin_lock_irqsave(&cs->lock, flags);
	cs->connected = 1;
	spin_unlock_irqrestore(&cs->lock, flags);

	if (cs->mstate != MS_LOCKED) {
		cs->ops->set_modem_ctrl(cs, 0, TIOCM_DTR | TIOCM_RTS);
		cs->ops->baud_rate(cs, B115200);
		cs->ops->set_line_ctrl(cs, CS8);
		cs->control_state = TIOCM_DTR | TIOCM_RTS;
	}

	cs->waiting = 1;

	if (!gigaset_add_event(cs, &cs->at_state, EV_START, NULL, 0, NULL)) {
		cs->waiting = 0;
		goto error;
	}
	gigaset_schedule_event(cs);

	wait_event(cs->waitqueue, !cs->waiting);

	mutex_unlock(&cs->mutex);
	return 0;

error:
	mutex_unlock(&cs->mutex);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(gigaset_start);

/**
 * gigaset_shutdown() - shut down device operations
 * @cs:		device descriptor structure.
 *
 * Deactivates the device by scheduling an EV_SHUTDOWN event and
 * waiting for completion of the shutdown.
 *
 * Return value:
 *	0 - success, -ENODEV - error (no device associated)
 */
int gigaset_shutdown(struct cardstate *cs)
{
	mutex_lock(&cs->mutex);

	if (!(cs->flags & VALID_MINOR)) {
		mutex_unlock(&cs->mutex);
		return -ENODEV;
	}

	cs->waiting = 1;

	if (!gigaset_add_event(cs, &cs->at_state, EV_SHUTDOWN, NULL, 0, NULL))
		goto exit;
	gigaset_schedule_event(cs);

	wait_event(cs->waitqueue, !cs->waiting);

	cleanup_cs(cs);

exit:
	mutex_unlock(&cs->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(gigaset_shutdown);

/**
 * gigaset_stop() - stop device operations
 * @cs:		device descriptor structure.
 *
 * Stops operations on the device by scheduling an EV_STOP event and
 * waiting for completion of the shutdown.
 */
void gigaset_stop(struct cardstate *cs)
{
	mutex_lock(&cs->mutex);

	cs->waiting = 1;

	if (!gigaset_add_event(cs, &cs->at_state, EV_STOP, NULL, 0, NULL))
		goto exit;
	gigaset_schedule_event(cs);

	wait_event(cs->waitqueue, !cs->waiting);

	cleanup_cs(cs);

exit:
	mutex_unlock(&cs->mutex);
}
EXPORT_SYMBOL_GPL(gigaset_stop);

static LIST_HEAD(drivers);
static DEFINE_SPINLOCK(driver_lock);

struct cardstate *gigaset_get_cs_by_id(int id)
{
	unsigned long flags;
	struct cardstate *ret = NULL;
	struct cardstate *cs;
	struct gigaset_driver *drv;
	unsigned i;

	spin_lock_irqsave(&driver_lock, flags);
	list_for_each_entry(drv, &drivers, list) {
		spin_lock(&drv->lock);
		for (i = 0; i < drv->minors; ++i) {
			cs = drv->cs + i;
			if ((cs->flags & VALID_ID) && cs->myid == id) {
				ret = cs;
				break;
			}
		}
		spin_unlock(&drv->lock);
		if (ret)
			break;
	}
	spin_unlock_irqrestore(&driver_lock, flags);
	return ret;
}

static struct cardstate *gigaset_get_cs_by_minor(unsigned minor)
{
	unsigned long flags;
	struct cardstate *ret = NULL;
	struct gigaset_driver *drv;
	unsigned index;

	spin_lock_irqsave(&driver_lock, flags);
	list_for_each_entry(drv, &drivers, list) {
		if (minor < drv->minor || minor >= drv->minor + drv->minors)
			continue;
		index = minor - drv->minor;
		spin_lock(&drv->lock);
		if (drv->cs[index].flags & VALID_MINOR)
			ret = drv->cs + index;
		spin_unlock(&drv->lock);
		if (ret)
			break;
	}
	spin_unlock_irqrestore(&driver_lock, flags);
	return ret;
}

struct cardstate *gigaset_get_cs_by_tty(struct tty_struct *tty)
{
	return gigaset_get_cs_by_minor(tty->index + tty->driver->minor_start);
}

/**
 * gigaset_freedriver() - free all associated ressources of a driver
 * @drv:	driver descriptor structure.
 *
 * Unregisters the driver from the system and deallocates the driver
 * structure @drv and all structures referenced from it.
 * All devices should be shut down before calling this.
 */
void gigaset_freedriver(struct gigaset_driver *drv)
{
	unsigned long flags;

	spin_lock_irqsave(&driver_lock, flags);
	list_del(&drv->list);
	spin_unlock_irqrestore(&driver_lock, flags);

	gigaset_if_freedriver(drv);

	kfree(drv->cs);
	kfree(drv);
}
EXPORT_SYMBOL_GPL(gigaset_freedriver);

/**
 * gigaset_initdriver() - initialize driver structure
 * @minor:	First minor number
 * @minors:	Number of minors this driver can handle
 * @procname:	Name of the driver
 * @devname:	Name of the device files (prefix without minor number)
 *
 * Allocate and initialize gigaset_driver structure. Initialize interface.
 *
 * Return value:
 *	Pointer to the gigaset_driver structure on success, NULL on failure.
 */
struct gigaset_driver *gigaset_initdriver(unsigned minor, unsigned minors,
					  const char *procname,
					  const char *devname,
					  const struct gigaset_ops *ops,
					  struct module *owner)
{
	struct gigaset_driver *drv;
	unsigned long flags;
	unsigned i;

	drv = kmalloc(sizeof *drv, GFP_KERNEL);
	if (!drv)
		return NULL;

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
		goto error;

	for (i = 0; i < minors; ++i) {
		drv->cs[i].flags = 0;
		drv->cs[i].driver = drv;
		drv->cs[i].ops = drv->ops;
		drv->cs[i].minor_index = i;
		mutex_init(&drv->cs[i].mutex);
	}

	gigaset_if_initdriver(drv, procname, devname);

	spin_lock_irqsave(&driver_lock, flags);
	list_add(&drv->list, &drivers);
	spin_unlock_irqrestore(&driver_lock, flags);

	return drv;

error:
	kfree(drv);
	return NULL;
}
EXPORT_SYMBOL_GPL(gigaset_initdriver);

/**
 * gigaset_blockdriver() - block driver
 * @drv:	driver descriptor structure.
 *
 * Prevents the driver from attaching new devices, in preparation for
 * deregistration.
 */
void gigaset_blockdriver(struct gigaset_driver *drv)
{
	drv->blocked = 1;
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

	pr_info(DRIVER_DESC DRIVER_DESC_DEBUG "\n");
	gigaset_isdn_regdrv();
	return 0;
}

static void __exit gigaset_exit_module(void)
{
	gigaset_isdn_unregdrv();
}

module_init(gigaset_init_module);
module_exit(gigaset_exit_module);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

MODULE_LICENSE("GPL");
