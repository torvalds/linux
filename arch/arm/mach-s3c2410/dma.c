/* linux/arch/arm/mach-bast/dma.c
 *
 * (c) 2003-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 DMA core
 *
 * http://www.simtec.co.uk/products/EB2410ITX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  27-Feb-2005 BJD  Added kmem cache for dma descriptors
 *  18-Nov-2004 BJD  Removed error for loading onto stopped channel
 *  10-Nov-2004 BJD  Ensure all external symbols exported for modules
 *  10-Nov-2004 BJD  Use sys_device and sysdev_class for power management
 *  08-Aug-2004 BJD  Apply rmk's suggestions
 *  21-Jul-2004 BJD  Ported to linux 2.6
 *  12-Jul-2004 BJD  Finished re-write and change of API
 *  06-Jul-2004 BJD  Rewrote dma code to try and cope with various problems
 *  23-May-2003 BJD  Created file
 *  19-Aug-2003 BJD  Cleanup, header fix, added URL
 *
 * This file is based on the Sangwook Lee/Samsung patches, re-written due
 * to various ommisions from the code (such as flexible dma configuration)
 * for use with the BAST system board.
 *
 * The re-write is pretty much complete, and should be good enough for any
 * possible DMA function
 */

#include <linux/config.h>

#ifdef CONFIG_S3C2410_DMA_DEBUG
#define DEBUG
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/sysdev.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <asm/mach/dma.h>
#include <asm/arch/map.h>

/* io map for dma */
static void __iomem *dma_base;
static kmem_cache_t *dma_kmem;

/* dma channel state information */
s3c2410_dma_chan_t s3c2410_chans[S3C2410_DMA_CHANNELS];

/* debugging functions */

#define BUF_MAGIC (0xcafebabe)

#define dmawarn(fmt...) printk(KERN_DEBUG fmt)

#define dma_regaddr(chan, reg) ((chan)->regs + (reg))

#if 1
#define dma_wrreg(chan, reg, val) writel((val), (chan)->regs + (reg))
#else
static inline void
dma_wrreg(s3c2410_dma_chan_t *chan, int reg, unsigned long val)
{
	pr_debug("writing %08x to register %08x\n",(unsigned int)val,reg);
	writel(val, dma_regaddr(chan, reg));
}

#endif

#define dma_rdreg(chan, reg) readl((chan)->regs + (reg))

/* captured register state for debug */

struct s3c2410_dma_regstate {
	unsigned long         dcsrc;
	unsigned long         disrc;
	unsigned long         dstat;
	unsigned long         dcon;
	unsigned long         dmsktrig;
};

#ifdef CONFIG_S3C2410_DMA_DEBUG

/* dmadbg_showregs
 *
 * simple debug routine to print the current state of the dma registers
*/

static void
dmadbg_capture(s3c2410_dma_chan_t *chan, struct s3c2410_dma_regstate *regs)
{
	regs->dcsrc    = dma_rdreg(chan, S3C2410_DMA_DCSRC);
	regs->disrc    = dma_rdreg(chan, S3C2410_DMA_DISRC);
	regs->dstat    = dma_rdreg(chan, S3C2410_DMA_DSTAT);
	regs->dcon     = dma_rdreg(chan, S3C2410_DMA_DCON);
	regs->dmsktrig = dma_rdreg(chan, S3C2410_DMA_DMASKTRIG);
}

static void
dmadbg_showregs(const char *fname, int line, s3c2410_dma_chan_t *chan,
		 struct s3c2410_dma_regstate *regs)
{
	printk(KERN_DEBUG "dma%d: %s:%d: DCSRC=%08lx, DISRC=%08lx, DSTAT=%08lx DMT=%02lx, DCON=%08lx\n",
	       chan->number, fname, line,
	       regs->dcsrc, regs->disrc, regs->dstat, regs->dmsktrig,
	       regs->dcon);
}

static void
dmadbg_showchan(const char *fname, int line, s3c2410_dma_chan_t *chan)
{
	struct s3c2410_dma_regstate state;

	dmadbg_capture(chan, &state);

	printk(KERN_DEBUG "dma%d: %s:%d: ls=%d, cur=%p, %p %p\n",
	       chan->number, fname, line, chan->load_state,
	       chan->curr, chan->next, chan->end);

	dmadbg_showregs(fname, line, chan, &state);
}

#define dbg_showregs(chan) dmadbg_showregs(__FUNCTION__, __LINE__, (chan))
#define dbg_showchan(chan) dmadbg_showchan(__FUNCTION__, __LINE__, (chan))
#else
#define dbg_showregs(chan) do { } while(0)
#define dbg_showchan(chan) do { } while(0)
#endif /* CONFIG_S3C2410_DMA_DEBUG */

#define check_channel(chan) \
  do { if ((chan) >= S3C2410_DMA_CHANNELS) { \
    printk(KERN_ERR "%s: invalid channel %d\n", __FUNCTION__, (chan)); \
    return -EINVAL; \
  } } while(0)


/* s3c2410_dma_stats_timeout
 *
 * Update DMA stats from timeout info
*/

static void
s3c2410_dma_stats_timeout(s3c2410_dma_stats_t *stats, int val)
{
	if (stats == NULL)
		return;

	if (val > stats->timeout_longest)
		stats->timeout_longest = val;
	if (val < stats->timeout_shortest)
		stats->timeout_shortest = val;

	stats->timeout_avg += val;
}

/* s3c2410_dma_waitforload
 *
 * wait for the DMA engine to load a buffer, and update the state accordingly
*/

static int
s3c2410_dma_waitforload(s3c2410_dma_chan_t *chan, int line)
{
	int timeout = chan->load_timeout;
	int took;

	if (chan->load_state != S3C2410_DMALOAD_1LOADED) {
		printk(KERN_ERR "dma%d: s3c2410_dma_waitforload() called in loadstate %d from line %d\n", chan->number, chan->load_state, line);
		return 0;
	}

	if (chan->stats != NULL)
		chan->stats->loads++;

	while (--timeout > 0) {
		if ((dma_rdreg(chan, S3C2410_DMA_DSTAT) << (32-20)) != 0) {
			took = chan->load_timeout - timeout;

			s3c2410_dma_stats_timeout(chan->stats, took);

			switch (chan->load_state) {
			case S3C2410_DMALOAD_1LOADED:
				chan->load_state = S3C2410_DMALOAD_1RUNNING;
				break;

			default:
				printk(KERN_ERR "dma%d: unknown load_state in s3c2410_dma_waitforload() %d\n", chan->number, chan->load_state);
			}

			return 1;
		}
	}

	if (chan->stats != NULL) {
		chan->stats->timeout_failed++;
	}

	return 0;
}



/* s3c2410_dma_loadbuffer
 *
 * load a buffer, and update the channel state
*/

static inline int
s3c2410_dma_loadbuffer(s3c2410_dma_chan_t *chan,
		       s3c2410_dma_buf_t *buf)
{
	unsigned long reload;

	pr_debug("s3c2410_chan_loadbuffer: loading buff %p (0x%08lx,0x%06x)\n",
		 buf, (unsigned long)buf->data, buf->size);

	if (buf == NULL) {
		dmawarn("buffer is NULL\n");
		return -EINVAL;
	}

	/* check the state of the channel before we do anything */

	if (chan->load_state == S3C2410_DMALOAD_1LOADED) {
		dmawarn("load_state is S3C2410_DMALOAD_1LOADED\n");
	}

	if (chan->load_state == S3C2410_DMALOAD_1LOADED_1RUNNING) {
		dmawarn("state is S3C2410_DMALOAD_1LOADED_1RUNNING\n");
	}

	/* it would seem sensible if we are the last buffer to not bother
	 * with the auto-reload bit, so that the DMA engine will not try
	 * and load another transfer after this one has finished...
	 */
	if (chan->load_state == S3C2410_DMALOAD_NONE) {
		pr_debug("load_state is none, checking for noreload (next=%p)\n",
			 buf->next);
		reload = (buf->next == NULL) ? S3C2410_DCON_NORELOAD : 0;
	} else {
		pr_debug("load_state is %d => autoreload\n", chan->load_state);
		reload = S3C2410_DCON_AUTORELOAD;
	}

	writel(buf->data, chan->addr_reg);

	dma_wrreg(chan, S3C2410_DMA_DCON,
		  chan->dcon | reload | (buf->size/chan->xfer_unit));

	chan->next = buf->next;

	/* update the state of the channel */

	switch (chan->load_state) {
	case S3C2410_DMALOAD_NONE:
		chan->load_state = S3C2410_DMALOAD_1LOADED;
		break;

	case S3C2410_DMALOAD_1RUNNING:
		chan->load_state = S3C2410_DMALOAD_1LOADED_1RUNNING;
		break;

	default:
		dmawarn("dmaload: unknown state %d in loadbuffer\n",
			chan->load_state);
		break;
	}

	return 0;
}

/* s3c2410_dma_call_op
 *
 * small routine to call the op routine with the given op if it has been
 * registered
*/

static void
s3c2410_dma_call_op(s3c2410_dma_chan_t *chan, s3c2410_chan_op_t op)
{
	if (chan->op_fn != NULL) {
		(chan->op_fn)(chan, op);
	}
}

/* s3c2410_dma_buffdone
 *
 * small wrapper to check if callback routine needs to be called, and
 * if so, call it
*/

static inline void
s3c2410_dma_buffdone(s3c2410_dma_chan_t *chan, s3c2410_dma_buf_t *buf,
		     s3c2410_dma_buffresult_t result)
{
	pr_debug("callback_fn=%p, buf=%p, id=%p, size=%d, result=%d\n",
		 chan->callback_fn, buf, buf->id, buf->size, result);

	if (chan->callback_fn != NULL) {
		(chan->callback_fn)(chan, buf->id, buf->size, result);
	}
}

/* s3c2410_dma_start
 *
 * start a dma channel going
*/

static int s3c2410_dma_start(s3c2410_dma_chan_t *chan)
{
	unsigned long tmp;
	unsigned long flags;

	pr_debug("s3c2410_start_dma: channel=%d\n", chan->number);

	local_irq_save(flags);

	if (chan->state == S3C2410_DMA_RUNNING) {
		pr_debug("s3c2410_start_dma: already running (%d)\n", chan->state);
		local_irq_restore(flags);
		return 0;
	}

	chan->state = S3C2410_DMA_RUNNING;

	/* check wether there is anything to load, and if not, see
	 * if we can find anything to load
	 */

	if (chan->load_state == S3C2410_DMALOAD_NONE) {
		if (chan->next == NULL) {
			printk(KERN_ERR "dma%d: channel has nothing loaded\n",
			       chan->number);
			chan->state = S3C2410_DMA_IDLE;
			local_irq_restore(flags);
			return -EINVAL;
		}

		s3c2410_dma_loadbuffer(chan, chan->next);
	}

	dbg_showchan(chan);

	/* enable the channel */

	if (!chan->irq_enabled) {
		enable_irq(chan->irq);
		chan->irq_enabled = 1;
	}

	/* start the channel going */

	tmp = dma_rdreg(chan, S3C2410_DMA_DMASKTRIG);
	tmp &= ~S3C2410_DMASKTRIG_STOP;
	tmp |= S3C2410_DMASKTRIG_ON;
	dma_wrreg(chan, S3C2410_DMA_DMASKTRIG, tmp);

	pr_debug("wrote %08lx to DMASKTRIG\n", tmp);

#if 0
	/* the dma buffer loads should take care of clearing the AUTO
	 * reloading feature */
	tmp = dma_rdreg(chan, S3C2410_DMA_DCON);
	tmp &= ~S3C2410_DCON_NORELOAD;
	dma_wrreg(chan, S3C2410_DMA_DCON, tmp);
#endif

	s3c2410_dma_call_op(chan, S3C2410_DMAOP_START);

	dbg_showchan(chan);

	local_irq_restore(flags);
	return 0;
}

/* s3c2410_dma_canload
 *
 * work out if we can queue another buffer into the DMA engine
*/

static int
s3c2410_dma_canload(s3c2410_dma_chan_t *chan)
{
	if (chan->load_state == S3C2410_DMALOAD_NONE ||
	    chan->load_state == S3C2410_DMALOAD_1RUNNING)
		return 1;

	return 0;
}


/* s3c2410_dma_enqueue
 *
 * queue an given buffer for dma transfer.
 *
 * id         the device driver's id information for this buffer
 * data       the physical address of the buffer data
 * size       the size of the buffer in bytes
 *
 * If the channel is not running, then the flag S3C2410_DMAF_AUTOSTART
 * is checked, and if set, the channel is started. If this flag isn't set,
 * then an error will be returned.
 *
 * It is possible to queue more than one DMA buffer onto a channel at
 * once, and the code will deal with the re-loading of the next buffer
 * when necessary.
*/

int s3c2410_dma_enqueue(unsigned int channel, void *id,
			dma_addr_t data, int size)
{
	s3c2410_dma_chan_t *chan = &s3c2410_chans[channel];
	s3c2410_dma_buf_t *buf;
	unsigned long flags;

	check_channel(channel);

	pr_debug("%s: id=%p, data=%08x, size=%d\n",
		 __FUNCTION__, id, (unsigned int)data, size);

	buf = kmem_cache_alloc(dma_kmem, GFP_ATOMIC);
	if (buf == NULL) {
		pr_debug("%s: out of memory (%ld alloc)\n",
			 __FUNCTION__, sizeof(*buf));
		return -ENOMEM;
	}

	pr_debug("%s: new buffer %p\n", __FUNCTION__, buf);

	//dbg_showchan(chan);

	buf->next  = NULL;
	buf->data  = buf->ptr = data;
	buf->size  = size;
	buf->id    = id;
	buf->magic = BUF_MAGIC;

	local_irq_save(flags);

	if (chan->curr == NULL) {
		/* we've got nothing loaded... */
		pr_debug("%s: buffer %p queued onto empty channel\n",
			 __FUNCTION__, buf);

		chan->curr = buf;
		chan->end  = buf;
		chan->next = NULL;
	} else {
		pr_debug("dma%d: %s: buffer %p queued onto non-empty channel\n",
			 chan->number, __FUNCTION__, buf);

		if (chan->end == NULL)
			pr_debug("dma%d: %s: %p not empty, and chan->end==NULL?\n",
				 chan->number, __FUNCTION__, chan);

		chan->end->next = buf;
		chan->end = buf;
	}

	/* if necessary, update the next buffer field */
	if (chan->next == NULL)
		chan->next = buf;

	/* check to see if we can load a buffer */
	if (chan->state == S3C2410_DMA_RUNNING) {
		if (chan->load_state == S3C2410_DMALOAD_1LOADED && 1) {
			if (s3c2410_dma_waitforload(chan, __LINE__) == 0) {
				printk(KERN_ERR "dma%d: loadbuffer:"
				       "timeout loading buffer\n",
				       chan->number);
				dbg_showchan(chan);
				local_irq_restore(flags);
				return -EINVAL;
			}
		}

		while (s3c2410_dma_canload(chan) && chan->next != NULL) {
			s3c2410_dma_loadbuffer(chan, chan->next);
		}
	} else if (chan->state == S3C2410_DMA_IDLE) {
		if (chan->flags & S3C2410_DMAF_AUTOSTART) {
			s3c2410_dma_ctrl(chan->number, S3C2410_DMAOP_START);
		}
	}

	local_irq_restore(flags);
	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_enqueue);

static inline void
s3c2410_dma_freebuf(s3c2410_dma_buf_t *buf)
{
	int magicok = (buf->magic == BUF_MAGIC);

	buf->magic = -1;

	if (magicok) {
		kmem_cache_free(dma_kmem, buf);
	} else {
		printk("s3c2410_dma_freebuf: buff %p with bad magic\n", buf);
	}
}

/* s3c2410_dma_lastxfer
 *
 * called when the system is out of buffers, to ensure that the channel
 * is prepared for shutdown.
*/

static inline void
s3c2410_dma_lastxfer(s3c2410_dma_chan_t *chan)
{
	pr_debug("dma%d: s3c2410_dma_lastxfer: load_state %d\n",
		 chan->number, chan->load_state);

	switch (chan->load_state) {
	case S3C2410_DMALOAD_NONE:
		break;

	case S3C2410_DMALOAD_1LOADED:
		if (s3c2410_dma_waitforload(chan, __LINE__) == 0) {
				/* flag error? */
			printk(KERN_ERR "dma%d: timeout waiting for load\n",
			       chan->number);
			return;
		}
		break;

	default:
		pr_debug("dma%d: lastxfer: unhandled load_state %d with no next",
			 chan->number, chan->load_state);
		return;

	}

	/* hopefully this'll shut the damned thing up after the transfer... */
	dma_wrreg(chan, S3C2410_DMA_DCON, chan->dcon | S3C2410_DCON_NORELOAD);
}


#define dmadbg2(x...)

static irqreturn_t
s3c2410_dma_irq(int irq, void *devpw, struct pt_regs *regs)
{
	s3c2410_dma_chan_t *chan = (s3c2410_dma_chan_t *)devpw;
	s3c2410_dma_buf_t  *buf;

	buf = chan->curr;

	dbg_showchan(chan);

	/* modify the channel state */

	switch (chan->load_state) {
	case S3C2410_DMALOAD_1RUNNING:
		/* TODO - if we are running only one buffer, we probably
		 * want to reload here, and then worry about the buffer
		 * callback */

		chan->load_state = S3C2410_DMALOAD_NONE;
		break;

	case S3C2410_DMALOAD_1LOADED:
		/* iirc, we should go back to NONE loaded here, we
		 * had a buffer, and it was never verified as being
		 * loaded.
		 */

		chan->load_state = S3C2410_DMALOAD_NONE;
		break;

	case S3C2410_DMALOAD_1LOADED_1RUNNING:
		/* we'll worry about checking to see if another buffer is
		 * ready after we've called back the owner. This should
		 * ensure we do not wait around too long for the DMA
		 * engine to start the next transfer
		 */

		chan->load_state = S3C2410_DMALOAD_1LOADED;
		break;

	case S3C2410_DMALOAD_NONE:
		printk(KERN_ERR "dma%d: IRQ with no loaded buffer?\n",
		       chan->number);
		break;

	default:
		printk(KERN_ERR "dma%d: IRQ in invalid load_state %d\n",
		       chan->number, chan->load_state);
		break;
	}

	if (buf != NULL) {
		/* update the chain to make sure that if we load any more
		 * buffers when we call the callback function, things should
		 * work properly */

		chan->curr = buf->next;
		buf->next  = NULL;

		if (buf->magic != BUF_MAGIC) {
			printk(KERN_ERR "dma%d: %s: buf %p incorrect magic\n",
			       chan->number, __FUNCTION__, buf);
			return IRQ_HANDLED;
		}

		s3c2410_dma_buffdone(chan, buf, S3C2410_RES_OK);

		/* free resouces */
		s3c2410_dma_freebuf(buf);
	} else {
	}

	if (chan->next != NULL) {
		unsigned long flags;

		switch (chan->load_state) {
		case S3C2410_DMALOAD_1RUNNING:
			/* don't need to do anything for this state */
			break;

		case S3C2410_DMALOAD_NONE:
			/* can load buffer immediately */
			break;

		case S3C2410_DMALOAD_1LOADED:
			if (s3c2410_dma_waitforload(chan, __LINE__) == 0) {
				/* flag error? */
				printk(KERN_ERR "dma%d: timeout waiting for load\n",
				       chan->number);
				return IRQ_HANDLED;
			}

			break;

		case S3C2410_DMALOAD_1LOADED_1RUNNING:
			goto no_load;

		default:
			printk(KERN_ERR "dma%d: unknown load_state in irq, %d\n",
			       chan->number, chan->load_state);
			return IRQ_HANDLED;
		}

		local_irq_save(flags);
		s3c2410_dma_loadbuffer(chan, chan->next);
		local_irq_restore(flags);
	} else {
		s3c2410_dma_lastxfer(chan);

		/* see if we can stop this channel.. */
		if (chan->load_state == S3C2410_DMALOAD_NONE) {
			pr_debug("dma%d: end of transfer, stopping channel (%ld)\n",
				 chan->number, jiffies);
			s3c2410_dma_ctrl(chan->number, S3C2410_DMAOP_STOP);
		}
	}

 no_load:
	return IRQ_HANDLED;
}



/* s3c2410_request_dma
 *
 * get control of an dma channel
*/

int s3c2410_dma_request(unsigned int channel, s3c2410_dma_client_t *client,
			void *dev)
{
	s3c2410_dma_chan_t *chan = &s3c2410_chans[channel];
	unsigned long flags;
	int err;

	pr_debug("dma%d: s3c2410_request_dma: client=%s, dev=%p\n",
		 channel, client->name, dev);

	check_channel(channel);

	local_irq_save(flags);

	dbg_showchan(chan);

	if (chan->in_use) {
		if (client != chan->client) {
			printk(KERN_ERR "dma%d: already in use\n", channel);
			local_irq_restore(flags);
			return -EBUSY;
		} else {
			printk(KERN_ERR "dma%d: client already has channel\n", channel);
		}
	}

	chan->client = client;
	chan->in_use = 1;

	if (!chan->irq_claimed) {
		pr_debug("dma%d: %s : requesting irq %d\n",
			 channel, __FUNCTION__, chan->irq);

		err = request_irq(chan->irq, s3c2410_dma_irq, SA_INTERRUPT,
				  client->name, (void *)chan);

		if (err) {
			chan->in_use = 0;
			local_irq_restore(flags);

			printk(KERN_ERR "%s: cannot get IRQ %d for DMA %d\n",
			       client->name, chan->irq, chan->number);
			return err;
		}

		chan->irq_claimed = 1;
		chan->irq_enabled = 1;
	}

	local_irq_restore(flags);

	/* need to setup */

	pr_debug("%s: channel initialised, %p\n", __FUNCTION__, chan);

	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_request);

/* s3c2410_dma_free
 *
 * release the given channel back to the system, will stop and flush
 * any outstanding transfers, and ensure the channel is ready for the
 * next claimant.
 *
 * Note, although a warning is currently printed if the freeing client
 * info is not the same as the registrant's client info, the free is still
 * allowed to go through.
*/

int s3c2410_dma_free(dmach_t channel, s3c2410_dma_client_t *client)
{
	s3c2410_dma_chan_t *chan = &s3c2410_chans[channel];
	unsigned long flags;

	check_channel(channel);

	local_irq_save(flags);


	if (chan->client != client) {
		printk(KERN_WARNING "dma%d: possible free from different client (channel %p, passed %p)\n",
		       channel, chan->client, client);
	}

	/* sort out stopping and freeing the channel */

	if (chan->state != S3C2410_DMA_IDLE) {
		pr_debug("%s: need to stop dma channel %p\n",
		       __FUNCTION__, chan);

		/* possibly flush the channel */
		s3c2410_dma_ctrl(channel, S3C2410_DMAOP_STOP);
	}

	chan->client = NULL;
	chan->in_use = 0;

	if (chan->irq_claimed)
		free_irq(chan->irq, (void *)chan);
	chan->irq_claimed = 0;

	local_irq_restore(flags);

	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_free);

static int s3c2410_dma_dostop(s3c2410_dma_chan_t *chan)
{
	unsigned long tmp;
	unsigned long flags;

	pr_debug("%s:\n", __FUNCTION__);

	dbg_showchan(chan);

	local_irq_save(flags);

	s3c2410_dma_call_op(chan,  S3C2410_DMAOP_STOP);

	tmp = dma_rdreg(chan, S3C2410_DMA_DMASKTRIG);
	tmp |= S3C2410_DMASKTRIG_STOP;
	dma_wrreg(chan, S3C2410_DMA_DMASKTRIG, tmp);

#if 0
	/* should also clear interrupts, according to WinCE BSP */
	tmp = dma_rdreg(chan, S3C2410_DMA_DCON);
	tmp |= S3C2410_DCON_NORELOAD;
	dma_wrreg(chan, S3C2410_DMA_DCON, tmp);
#endif

	chan->state      = S3C2410_DMA_IDLE;
	chan->load_state = S3C2410_DMALOAD_NONE;

	local_irq_restore(flags);

	return 0;
}

/* s3c2410_dma_flush
 *
 * stop the channel, and remove all current and pending transfers
*/

static int s3c2410_dma_flush(s3c2410_dma_chan_t *chan)
{
	s3c2410_dma_buf_t *buf, *next;
	unsigned long flags;

	pr_debug("%s:\n", __FUNCTION__);

	local_irq_save(flags);

	if (chan->state != S3C2410_DMA_IDLE) {
		pr_debug("%s: stopping channel...\n", __FUNCTION__ );
		s3c2410_dma_ctrl(chan->number, S3C2410_DMAOP_STOP);
	}

	buf = chan->curr;
	if (buf == NULL)
		buf = chan->next;

	chan->curr = chan->next = chan->end = NULL;

	if (buf != NULL) {
		for ( ; buf != NULL; buf = next) {
			next = buf->next;

			pr_debug("%s: free buffer %p, next %p\n",
			       __FUNCTION__, buf, buf->next);

			s3c2410_dma_buffdone(chan, buf, S3C2410_RES_ABORT);
			s3c2410_dma_freebuf(buf);
		}
	}

	local_irq_restore(flags);

	return 0;
}


int
s3c2410_dma_ctrl(dmach_t channel, s3c2410_chan_op_t op)
{
	s3c2410_dma_chan_t *chan = &s3c2410_chans[channel];

	check_channel(channel);

	switch (op) {
	case S3C2410_DMAOP_START:
		return s3c2410_dma_start(chan);

	case S3C2410_DMAOP_STOP:
		return s3c2410_dma_dostop(chan);

	case S3C2410_DMAOP_PAUSE:
		return -ENOENT;

	case S3C2410_DMAOP_RESUME:
		return -ENOENT;

	case S3C2410_DMAOP_FLUSH:
		return s3c2410_dma_flush(chan);

	case S3C2410_DMAOP_TIMEOUT:
		return 0;

	}

	return -ENOENT;      /* unknown, don't bother */
}

EXPORT_SYMBOL(s3c2410_dma_ctrl);

/* DMA configuration for each channel
 *
 * DISRCC -> source of the DMA (AHB,APB)
 * DISRC  -> source address of the DMA
 * DIDSTC -> destination of the DMA (AHB,APD)
 * DIDST  -> destination address of the DMA
*/

/* s3c2410_dma_config
 *
 * xfersize:     size of unit in bytes (1,2,4)
 * dcon:         base value of the DCONx register
*/

int s3c2410_dma_config(dmach_t channel,
		       int xferunit,
		       int dcon)
{
	s3c2410_dma_chan_t *chan = &s3c2410_chans[channel];

	pr_debug("%s: chan=%d, xfer_unit=%d, dcon=%08x\n",
		 __FUNCTION__, channel, xferunit, dcon);

	check_channel(channel);

	switch (xferunit) {
	case 1:
		dcon |= S3C2410_DCON_BYTE;
		break;

	case 2:
		dcon |= S3C2410_DCON_HALFWORD;
		break;

	case 4:
		dcon |= S3C2410_DCON_WORD;
		break;

	default:
		pr_debug("%s: bad transfer size %d\n", __FUNCTION__, xferunit);
		return -EINVAL;
	}

	dcon |= S3C2410_DCON_HWTRIG;
	dcon |= S3C2410_DCON_INTREQ;

	pr_debug("%s: dcon now %08x\n", __FUNCTION__, dcon);

	chan->dcon = dcon;
	chan->xfer_unit = xferunit;

	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_config);

int s3c2410_dma_setflags(dmach_t channel, unsigned int flags)
{
	s3c2410_dma_chan_t *chan = &s3c2410_chans[channel];

	check_channel(channel);

	pr_debug("%s: chan=%p, flags=%08x\n", __FUNCTION__, chan, flags);

	chan->flags = flags;

	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_setflags);


/* do we need to protect the settings of the fields from
 * irq?
*/

int s3c2410_dma_set_opfn(dmach_t channel, s3c2410_dma_opfn_t rtn)
{
	s3c2410_dma_chan_t *chan = &s3c2410_chans[channel];

	check_channel(channel);

	pr_debug("%s: chan=%p, op rtn=%p\n", __FUNCTION__, chan, rtn);

	chan->op_fn = rtn;

	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_set_opfn);

int s3c2410_dma_set_buffdone_fn(dmach_t channel, s3c2410_dma_cbfn_t rtn)
{
	s3c2410_dma_chan_t *chan = &s3c2410_chans[channel];

	check_channel(channel);

	pr_debug("%s: chan=%p, callback rtn=%p\n", __FUNCTION__, chan, rtn);

	chan->callback_fn = rtn;

	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_set_buffdone_fn);

/* s3c2410_dma_devconfig
 *
 * configure the dma source/destination hardware type and address
 *
 * source:    S3C2410_DMASRC_HW: source is hardware
 *            S3C2410_DMASRC_MEM: source is memory
 *
 * hwcfg:     the value for xxxSTCn register,
 *            bit 0: 0=increment pointer, 1=leave pointer
 *            bit 1: 0=soucre is AHB, 1=soucre is APB
 *
 * devaddr:   physical address of the source
*/

int s3c2410_dma_devconfig(int channel,
			  s3c2410_dmasrc_t source,
			  int hwcfg,
			  unsigned long devaddr)
{
	s3c2410_dma_chan_t *chan = &s3c2410_chans[channel];

	check_channel(channel);

	pr_debug("%s: source=%d, hwcfg=%08x, devaddr=%08lx\n",
		 __FUNCTION__, (int)source, hwcfg, devaddr);

	chan->source = source;
	chan->dev_addr = devaddr;

	switch (source) {
	case S3C2410_DMASRC_HW:
		/* source is hardware */
		pr_debug("%s: hw source, devaddr=%08lx, hwcfg=%d\n",
			 __FUNCTION__, devaddr, hwcfg);
		dma_wrreg(chan, S3C2410_DMA_DISRCC, hwcfg & 3);
		dma_wrreg(chan, S3C2410_DMA_DISRC,  devaddr);
		dma_wrreg(chan, S3C2410_DMA_DIDSTC, (0<<1) | (0<<0));

		chan->addr_reg = dma_regaddr(chan, S3C2410_DMA_DIDST);
		return 0;

	case S3C2410_DMASRC_MEM:
		/* source is memory */
		pr_debug( "%s: mem source, devaddr=%08lx, hwcfg=%d\n",
			  __FUNCTION__, devaddr, hwcfg);
		dma_wrreg(chan, S3C2410_DMA_DISRCC, (0<<1) | (0<<0));
		dma_wrreg(chan, S3C2410_DMA_DIDST,  devaddr);
		dma_wrreg(chan, S3C2410_DMA_DIDSTC, hwcfg & 3);

		chan->addr_reg = dma_regaddr(chan, S3C2410_DMA_DISRC);
		return 0;
	}

	printk(KERN_ERR "dma%d: invalid source type (%d)\n", channel, source);
	return -EINVAL;
}

EXPORT_SYMBOL(s3c2410_dma_devconfig);

/* s3c2410_dma_getposition
 *
 * returns the current transfer points for the dma source and destination
*/

int s3c2410_dma_getposition(dmach_t channel, dma_addr_t *src, dma_addr_t *dst)
{
 	s3c2410_dma_chan_t *chan = &s3c2410_chans[channel];

 	check_channel(channel);

	if (src != NULL)
 		*src = dma_rdreg(chan, S3C2410_DMA_DCSRC);

 	if (dst != NULL)
 		*dst = dma_rdreg(chan, S3C2410_DMA_DCDST);

 	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_getposition);


/* system device class */

#ifdef CONFIG_PM

static int s3c2410_dma_suspend(struct sys_device *dev, pm_message_t state)
{
	s3c2410_dma_chan_t *cp = container_of(dev, s3c2410_dma_chan_t, dev);

	printk(KERN_DEBUG "suspending dma channel %d\n", cp->number);

	if (dma_rdreg(cp, S3C2410_DMA_DMASKTRIG) & S3C2410_DMASKTRIG_ON) {
		/* the dma channel is still working, which is probably
		 * a bad thing to do over suspend/resume. We stop the
		 * channel and assume that the client is either going to
		 * retry after resume, or that it is broken.
		 */

		printk(KERN_INFO "dma: stopping channel %d due to suspend\n",
		       cp->number);

		s3c2410_dma_dostop(cp);
	}

	return 0;
}

static int s3c2410_dma_resume(struct sys_device *dev)
{
	return 0;
}

#else
#define s3c2410_dma_suspend NULL
#define s3c2410_dma_resume  NULL
#endif /* CONFIG_PM */

static struct sysdev_class dma_sysclass = {
	set_kset_name("s3c24xx-dma"),
	.suspend	= s3c2410_dma_suspend,
	.resume		= s3c2410_dma_resume,
};

/* kmem cache implementation */

static void s3c2410_dma_cache_ctor(void *p, kmem_cache_t *c, unsigned long f)
{
	memset(p, 0, sizeof(s3c2410_dma_buf_t));
}


/* initialisation code */

static int __init s3c2410_init_dma(void)
{
	s3c2410_dma_chan_t *cp;
	int channel;
	int ret;

	printk("S3C2410 DMA Driver, (c) 2003-2004 Simtec Electronics\n");

	dma_base = ioremap(S3C2410_PA_DMA, 0x200);
	if (dma_base == NULL) {
		printk(KERN_ERR "dma failed to remap register block\n");
		return -ENOMEM;
	}

	ret = sysdev_class_register(&dma_sysclass);
	if (ret != 0) {
		printk(KERN_ERR "dma sysclass registration failed\n");
		goto err;
	}

	dma_kmem = kmem_cache_create("dma_desc", sizeof(s3c2410_dma_buf_t), 0,
				     SLAB_HWCACHE_ALIGN,
				     s3c2410_dma_cache_ctor, NULL);

	if (dma_kmem == NULL) {
		printk(KERN_ERR "dma failed to make kmem cache\n");
		ret = -ENOMEM;
		goto err;
	}

	for (channel = 0; channel < S3C2410_DMA_CHANNELS; channel++) {
		cp = &s3c2410_chans[channel];

		memset(cp, 0, sizeof(s3c2410_dma_chan_t));

		/* dma channel irqs are in order.. */
		cp->number = channel;
		cp->irq    = channel + IRQ_DMA0;
		cp->regs   = dma_base + (channel*0x40);

		/* point current stats somewhere */
		cp->stats  = &cp->stats_store;
		cp->stats_store.timeout_shortest = LONG_MAX;

		/* basic channel configuration */

		cp->load_timeout = 1<<18;

		/* register system device */

		cp->dev.cls = &dma_sysclass;
		cp->dev.id  = channel;
		ret = sysdev_register(&cp->dev);

		printk("DMA channel %d at %p, irq %d\n",
		       cp->number, cp->regs, cp->irq);
	}

	return 0;

 err:
	kmem_cache_destroy(dma_kmem);
	iounmap(dma_base);
	dma_base = NULL;
	return ret;
}

__initcall(s3c2410_init_dma);
