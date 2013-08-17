/* linux/arch/arm/plat-s3c24xx/dma.c
 *
 * Copyright 2003-2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 DMA core
 *
 * http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/


#ifdef CONFIG_S3C2410_DMA_DEBUG
#define DEBUG
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/syscore_ops.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <mach/hardware.h>
#include <mach/dma.h>
#include <mach/map.h>

#include <plat/dma-s3c24xx.h>
#include <plat/regs-dma.h>

/* io map for dma */
static void __iomem *dma_base;
static struct kmem_cache *dma_kmem;

static int dma_channels;

static struct s3c24xx_dma_selection dma_sel;


/* debugging functions */

#define BUF_MAGIC (0xcafebabe)

#define dmawarn(fmt...) printk(KERN_DEBUG fmt)

#define dma_regaddr(chan, reg) ((chan)->regs + (reg))

#if 1
#define dma_wrreg(chan, reg, val) writel((val), (chan)->regs + (reg))
#else
static inline void
dma_wrreg(struct s3c2410_dma_chan *chan, int reg, unsigned long val)
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
dmadbg_capture(struct s3c2410_dma_chan *chan, struct s3c2410_dma_regstate *regs)
{
	regs->dcsrc    = dma_rdreg(chan, S3C2410_DMA_DCSRC);
	regs->disrc    = dma_rdreg(chan, S3C2410_DMA_DISRC);
	regs->dstat    = dma_rdreg(chan, S3C2410_DMA_DSTAT);
	regs->dcon     = dma_rdreg(chan, S3C2410_DMA_DCON);
	regs->dmsktrig = dma_rdreg(chan, S3C2410_DMA_DMASKTRIG);
}

static void
dmadbg_dumpregs(const char *fname, int line, struct s3c2410_dma_chan *chan,
		 struct s3c2410_dma_regstate *regs)
{
	printk(KERN_DEBUG "dma%d: %s:%d: DCSRC=%08lx, DISRC=%08lx, DSTAT=%08lx DMT=%02lx, DCON=%08lx\n",
	       chan->number, fname, line,
	       regs->dcsrc, regs->disrc, regs->dstat, regs->dmsktrig,
	       regs->dcon);
}

static void
dmadbg_showchan(const char *fname, int line, struct s3c2410_dma_chan *chan)
{
	struct s3c2410_dma_regstate state;

	dmadbg_capture(chan, &state);

	printk(KERN_DEBUG "dma%d: %s:%d: ls=%d, cur=%p, %p %p\n",
	       chan->number, fname, line, chan->load_state,
	       chan->curr, chan->next, chan->end);

	dmadbg_dumpregs(fname, line, chan, &state);
}

static void
dmadbg_showregs(const char *fname, int line, struct s3c2410_dma_chan *chan)
{
	struct s3c2410_dma_regstate state;

	dmadbg_capture(chan, &state);
	dmadbg_dumpregs(fname, line, chan, &state);
}

#define dbg_showregs(chan) dmadbg_showregs(__func__, __LINE__, (chan))
#define dbg_showchan(chan) dmadbg_showchan(__func__, __LINE__, (chan))
#else
#define dbg_showregs(chan) do { } while(0)
#define dbg_showchan(chan) do { } while(0)
#endif /* CONFIG_S3C2410_DMA_DEBUG */

/* s3c2410_dma_stats_timeout
 *
 * Update DMA stats from timeout info
*/

static void
s3c2410_dma_stats_timeout(struct s3c2410_dma_stats *stats, int val)
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
s3c2410_dma_waitforload(struct s3c2410_dma_chan *chan, int line)
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
s3c2410_dma_loadbuffer(struct s3c2410_dma_chan *chan,
		       struct s3c2410_dma_buf *buf)
{
	unsigned long reload;

	if (buf == NULL) {
		dmawarn("buffer is NULL\n");
		return -EINVAL;
	}

	pr_debug("s3c2410_chan_loadbuffer: loading buff %p (0x%08lx,0x%06x)\n",
		 buf, (unsigned long)buf->data, buf->size);

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
		//pr_debug("load_state is %d => autoreload\n", chan->load_state);
		reload = S3C2410_DCON_AUTORELOAD;
	}

	if ((buf->data & 0xf0000000) != 0x30000000) {
		dmawarn("dmaload: buffer is %p\n", (void *)buf->data);
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
s3c2410_dma_call_op(struct s3c2410_dma_chan *chan, enum s3c2410_chan_op op)
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
s3c2410_dma_buffdone(struct s3c2410_dma_chan *chan, struct s3c2410_dma_buf *buf,
		     enum s3c2410_dma_buffresult result)
{
#if 0
	pr_debug("callback_fn=%p, buf=%p, id=%p, size=%d, result=%d\n",
		 chan->callback_fn, buf, buf->id, buf->size, result);
#endif

	if (chan->callback_fn != NULL) {
		(chan->callback_fn)(chan, buf->id, buf->size, result);
	}
}

/* s3c2410_dma_start
 *
 * start a dma channel going
*/

static int s3c2410_dma_start(struct s3c2410_dma_chan *chan)
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

	pr_debug("dma%d: %08lx to DMASKTRIG\n", chan->number, tmp);

#if 0
	/* the dma buffer loads should take care of clearing the AUTO
	 * reloading feature */
	tmp = dma_rdreg(chan, S3C2410_DMA_DCON);
	tmp &= ~S3C2410_DCON_NORELOAD;
	dma_wrreg(chan, S3C2410_DMA_DCON, tmp);
#endif

	s3c2410_dma_call_op(chan, S3C2410_DMAOP_START);

	dbg_showchan(chan);

	/* if we've only loaded one buffer onto the channel, then chec
	 * to see if we have another, and if so, try and load it so when
	 * the first buffer is finished, the new one will be loaded onto
	 * the channel */

	if (chan->next != NULL) {
		if (chan->load_state == S3C2410_DMALOAD_1LOADED) {

			if (s3c2410_dma_waitforload(chan, __LINE__) == 0) {
				pr_debug("%s: buff not yet loaded, no more todo\n",
					 __func__);
			} else {
				chan->load_state = S3C2410_DMALOAD_1RUNNING;
				s3c2410_dma_loadbuffer(chan, chan->next);
			}

		} else if (chan->load_state == S3C2410_DMALOAD_1RUNNING) {
			s3c2410_dma_loadbuffer(chan, chan->next);
		}
	}


	local_irq_restore(flags);

	return 0;
}

/* s3c2410_dma_canload
 *
 * work out if we can queue another buffer into the DMA engine
*/

static int
s3c2410_dma_canload(struct s3c2410_dma_chan *chan)
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
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);
	struct s3c2410_dma_buf *buf;
	unsigned long flags;

	if (chan == NULL)
		return -EINVAL;

	pr_debug("%s: id=%p, data=%08x, size=%d\n",
		 __func__, id, (unsigned int)data, size);

	buf = kmem_cache_alloc(dma_kmem, GFP_ATOMIC);
	if (buf == NULL) {
		pr_debug("%s: out of memory (%ld alloc)\n",
			 __func__, (long)sizeof(*buf));
		return -ENOMEM;
	}

	//pr_debug("%s: new buffer %p\n", __func__, buf);
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
			 __func__, buf);

		chan->curr = buf;
		chan->end  = buf;
		chan->next = NULL;
	} else {
		pr_debug("dma%d: %s: buffer %p queued onto non-empty channel\n",
			 chan->number, __func__, buf);

		if (chan->end == NULL)
			pr_debug("dma%d: %s: %p not empty, and chan->end==NULL?\n",
				 chan->number, __func__, chan);

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
			s3c2410_dma_ctrl(chan->number | DMACH_LOW_LEVEL,
					 S3C2410_DMAOP_START);
		}
	}

	local_irq_restore(flags);
	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_enqueue);

static inline void
s3c2410_dma_freebuf(struct s3c2410_dma_buf *buf)
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
s3c2410_dma_lastxfer(struct s3c2410_dma_chan *chan)
{
#if 0
	pr_debug("dma%d: s3c2410_dma_lastxfer: load_state %d\n",
		 chan->number, chan->load_state);
#endif

	switch (chan->load_state) {
	case S3C2410_DMALOAD_NONE:
		break;

	case S3C2410_DMALOAD_1LOADED:
		if (s3c2410_dma_waitforload(chan, __LINE__) == 0) {
				/* flag error? */
			printk(KERN_ERR "dma%d: timeout waiting for load (%s)\n",
			       chan->number, __func__);
			return;
		}
		break;

	case S3C2410_DMALOAD_1LOADED_1RUNNING:
		/* I believe in this case we do not have anything to do
		 * until the next buffer comes along, and we turn off the
		 * reload */
		return;

	default:
		pr_debug("dma%d: lastxfer: unhandled load_state %d with no next\n",
			 chan->number, chan->load_state);
		return;

	}

	/* hopefully this'll shut the damned thing up after the transfer... */
	dma_wrreg(chan, S3C2410_DMA_DCON, chan->dcon | S3C2410_DCON_NORELOAD);
}


#define dmadbg2(x...)

static irqreturn_t
s3c2410_dma_irq(int irq, void *devpw)
{
	struct s3c2410_dma_chan *chan = (struct s3c2410_dma_chan *)devpw;
	struct s3c2410_dma_buf  *buf;

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
			       chan->number, __func__, buf);
			return IRQ_HANDLED;
		}

		s3c2410_dma_buffdone(chan, buf, S3C2410_RES_OK);

		/* free resouces */
		s3c2410_dma_freebuf(buf);
	} else {
	}

	/* only reload if the channel is still running... our buffer done
	 * routine may have altered the state by requesting the dma channel
	 * to stop or shutdown... */

	/* todo: check that when the channel is shut-down from inside this
	 * function, we cope with unsetting reload, etc */

	if (chan->next != NULL && chan->state != S3C2410_DMA_IDLE) {
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
				printk(KERN_ERR "dma%d: timeout waiting for load (%s)\n",
				       chan->number, __func__);
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
			s3c2410_dma_ctrl(chan->number | DMACH_LOW_LEVEL,
					 S3C2410_DMAOP_STOP);
		}
	}

 no_load:
	return IRQ_HANDLED;
}

static struct s3c2410_dma_chan *s3c2410_dma_map_channel(int channel);

/* s3c2410_request_dma
 *
 * get control of an dma channel
*/

int s3c2410_dma_request(enum dma_ch channel,
			struct s3c2410_dma_client *client,
			void *dev)
{
	struct s3c2410_dma_chan *chan;
	unsigned long flags;
	int err;

	pr_debug("dma%d: s3c2410_request_dma: client=%s, dev=%p\n",
		 channel, client->name, dev);

	local_irq_save(flags);

	chan = s3c2410_dma_map_channel(channel);
	if (chan == NULL) {
		local_irq_restore(flags);
		return -EBUSY;
	}

	dbg_showchan(chan);

	chan->client = client;
	chan->in_use = 1;

	if (!chan->irq_claimed) {
		pr_debug("dma%d: %s : requesting irq %d\n",
			 channel, __func__, chan->irq);

		chan->irq_claimed = 1;
		local_irq_restore(flags);

		err = request_irq(chan->irq, s3c2410_dma_irq, IRQF_DISABLED,
				  client->name, (void *)chan);

		local_irq_save(flags);

		if (err) {
			chan->in_use = 0;
			chan->irq_claimed = 0;
			local_irq_restore(flags);

			printk(KERN_ERR "%s: cannot get IRQ %d for DMA %d\n",
			       client->name, chan->irq, chan->number);
			return err;
		}

		chan->irq_enabled = 1;
	}

	local_irq_restore(flags);

	/* need to setup */

	pr_debug("%s: channel initialised, %p\n", __func__, chan);

	return chan->number | DMACH_LOW_LEVEL;
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

int s3c2410_dma_free(enum dma_ch channel, struct s3c2410_dma_client *client)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);
	unsigned long flags;

	if (chan == NULL)
		return -EINVAL;

	local_irq_save(flags);

	if (chan->client != client) {
		printk(KERN_WARNING "dma%d: possible free from different client (channel %p, passed %p)\n",
		       channel, chan->client, client);
	}

	/* sort out stopping and freeing the channel */

	if (chan->state != S3C2410_DMA_IDLE) {
		pr_debug("%s: need to stop dma channel %p\n",
		       __func__, chan);

		/* possibly flush the channel */
		s3c2410_dma_ctrl(channel, S3C2410_DMAOP_STOP);
	}

	chan->client = NULL;
	chan->in_use = 0;

	if (chan->irq_claimed)
		free_irq(chan->irq, (void *)chan);

	chan->irq_claimed = 0;

	if (!(channel & DMACH_LOW_LEVEL))
		s3c_dma_chan_map[channel] = NULL;

	local_irq_restore(flags);

	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_free);

static int s3c2410_dma_dostop(struct s3c2410_dma_chan *chan)
{
	unsigned long flags;
	unsigned long tmp;

	pr_debug("%s:\n", __func__);

	dbg_showchan(chan);

	local_irq_save(flags);

	s3c2410_dma_call_op(chan,  S3C2410_DMAOP_STOP);

	tmp = dma_rdreg(chan, S3C2410_DMA_DMASKTRIG);
	tmp |= S3C2410_DMASKTRIG_STOP;
	//tmp &= ~S3C2410_DMASKTRIG_ON;
	dma_wrreg(chan, S3C2410_DMA_DMASKTRIG, tmp);

#if 0
	/* should also clear interrupts, according to WinCE BSP */
	tmp = dma_rdreg(chan, S3C2410_DMA_DCON);
	tmp |= S3C2410_DCON_NORELOAD;
	dma_wrreg(chan, S3C2410_DMA_DCON, tmp);
#endif

	/* should stop do this, or should we wait for flush? */
	chan->state      = S3C2410_DMA_IDLE;
	chan->load_state = S3C2410_DMALOAD_NONE;

	local_irq_restore(flags);

	return 0;
}

static void s3c2410_dma_waitforstop(struct s3c2410_dma_chan *chan)
{
	unsigned long tmp;
	unsigned int timeout = 0x10000;

	while (timeout-- > 0) {
		tmp = dma_rdreg(chan, S3C2410_DMA_DMASKTRIG);

		if (!(tmp & S3C2410_DMASKTRIG_ON))
			return;
	}

	pr_debug("dma%d: failed to stop?\n", chan->number);
}


/* s3c2410_dma_flush
 *
 * stop the channel, and remove all current and pending transfers
*/

static int s3c2410_dma_flush(struct s3c2410_dma_chan *chan)
{
	struct s3c2410_dma_buf *buf, *next;
	unsigned long flags;

	pr_debug("%s: chan %p (%d)\n", __func__, chan, chan->number);

	dbg_showchan(chan);

	local_irq_save(flags);

	if (chan->state != S3C2410_DMA_IDLE) {
		pr_debug("%s: stopping channel...\n", __func__ );
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
			       __func__, buf, buf->next);

			s3c2410_dma_buffdone(chan, buf, S3C2410_RES_ABORT);
			s3c2410_dma_freebuf(buf);
		}
	}

	dbg_showregs(chan);

	s3c2410_dma_waitforstop(chan);

#if 0
	/* should also clear interrupts, according to WinCE BSP */
	{
		unsigned long tmp;

		tmp = dma_rdreg(chan, S3C2410_DMA_DCON);
		tmp |= S3C2410_DCON_NORELOAD;
		dma_wrreg(chan, S3C2410_DMA_DCON, tmp);
	}
#endif

	dbg_showregs(chan);

	local_irq_restore(flags);

	return 0;
}

static int s3c2410_dma_started(struct s3c2410_dma_chan *chan)
{
	unsigned long flags;

	local_irq_save(flags);

	dbg_showchan(chan);

	/* if we've only loaded one buffer onto the channel, then chec
	 * to see if we have another, and if so, try and load it so when
	 * the first buffer is finished, the new one will be loaded onto
	 * the channel */

	if (chan->next != NULL) {
		if (chan->load_state == S3C2410_DMALOAD_1LOADED) {

			if (s3c2410_dma_waitforload(chan, __LINE__) == 0) {
				pr_debug("%s: buff not yet loaded, no more todo\n",
					 __func__);
			} else {
				chan->load_state = S3C2410_DMALOAD_1RUNNING;
				s3c2410_dma_loadbuffer(chan, chan->next);
			}

		} else if (chan->load_state == S3C2410_DMALOAD_1RUNNING) {
			s3c2410_dma_loadbuffer(chan, chan->next);
		}
	}


	local_irq_restore(flags);

	return 0;

}

int
s3c2410_dma_ctrl(enum dma_ch channel, enum s3c2410_chan_op op)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);

	if (chan == NULL)
		return -EINVAL;

	switch (op) {
	case S3C2410_DMAOP_START:
		return s3c2410_dma_start(chan);

	case S3C2410_DMAOP_STOP:
		return s3c2410_dma_dostop(chan);

	case S3C2410_DMAOP_PAUSE:
	case S3C2410_DMAOP_RESUME:
		return -ENOENT;

	case S3C2410_DMAOP_FLUSH:
		return s3c2410_dma_flush(chan);

	case S3C2410_DMAOP_STARTED:
		return s3c2410_dma_started(chan);

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
*/

int s3c2410_dma_config(enum dma_ch channel,
		       int xferunit)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);
	unsigned int dcon;

	pr_debug("%s: chan=%d, xfer_unit=%d\n", __func__, channel, xferunit);

	if (chan == NULL)
		return -EINVAL;

	dcon = chan->dcon & dma_sel.dcon_mask;
	pr_debug("%s: dcon is %08x\n", __func__, dcon);

	switch (chan->req_ch) {
	case DMACH_I2S_IN:
	case DMACH_I2S_OUT:
	case DMACH_PCM_IN:
	case DMACH_PCM_OUT:
	case DMACH_MIC_IN:
	default:
		dcon |= S3C2410_DCON_HANDSHAKE;
		dcon |= S3C2410_DCON_SYNC_PCLK;
		break;

	case DMACH_SDI:
		/* note, ensure if need HANDSHAKE or not */
		dcon |= S3C2410_DCON_SYNC_PCLK;
		break;

	case DMACH_XD0:
	case DMACH_XD1:
		dcon |= S3C2410_DCON_HANDSHAKE;
		dcon |= S3C2410_DCON_SYNC_HCLK;
		break;
	}

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
		pr_debug("%s: bad transfer size %d\n", __func__, xferunit);
		return -EINVAL;
	}

	dcon |= S3C2410_DCON_HWTRIG;
	dcon |= S3C2410_DCON_INTREQ;

	pr_debug("%s: dcon now %08x\n", __func__, dcon);

	chan->dcon = dcon;
	chan->xfer_unit = xferunit;

	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_config);


/* s3c2410_dma_devconfig
 *
 * configure the dma source/destination hardware type and address
 *
 * source:    DMA_FROM_DEVICE: source is hardware
 *            DMA_TO_DEVICE: source is memory
 *
 * devaddr:   physical address of the source
*/

int s3c2410_dma_devconfig(enum dma_ch channel,
			  enum dma_data_direction source,
			  unsigned long devaddr)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);
	unsigned int hwcfg;

	if (chan == NULL)
		return -EINVAL;

	pr_debug("%s: source=%d, devaddr=%08lx\n",
		 __func__, (int)source, devaddr);

	chan->source = source;
	chan->dev_addr = devaddr;

	switch (chan->req_ch) {
	case DMACH_XD0:
	case DMACH_XD1:
		hwcfg = 0; /* AHB */
		break;

	default:
		hwcfg = S3C2410_DISRCC_APB;
	}

	/* always assume our peripheral desintation is a fixed
	 * address in memory. */
	 hwcfg |= S3C2410_DISRCC_INC;

	switch (source) {
	case DMA_FROM_DEVICE:
		/* source is hardware */
		pr_debug("%s: hw source, devaddr=%08lx, hwcfg=%d\n",
			 __func__, devaddr, hwcfg);
		dma_wrreg(chan, S3C2410_DMA_DISRCC, hwcfg & 3);
		dma_wrreg(chan, S3C2410_DMA_DISRC,  devaddr);
		dma_wrreg(chan, S3C2410_DMA_DIDSTC, (0<<1) | (0<<0));

		chan->addr_reg = dma_regaddr(chan, S3C2410_DMA_DIDST);
		break;

	case DMA_TO_DEVICE:
		/* source is memory */
		pr_debug("%s: mem source, devaddr=%08lx, hwcfg=%d\n",
			 __func__, devaddr, hwcfg);
		dma_wrreg(chan, S3C2410_DMA_DISRCC, (0<<1) | (0<<0));
		dma_wrreg(chan, S3C2410_DMA_DIDST,  devaddr);
		dma_wrreg(chan, S3C2410_DMA_DIDSTC, hwcfg & 3);

		chan->addr_reg = dma_regaddr(chan, S3C2410_DMA_DISRC);
		break;

	default:
		printk(KERN_ERR "dma%d: invalid source type (%d)\n",
		       channel, source);

		return -EINVAL;
	}

	if (dma_sel.direction != NULL)
		(dma_sel.direction)(chan, chan->map, source);

	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_devconfig);

/* s3c2410_dma_getposition
 *
 * returns the current transfer points for the dma source and destination
*/

int s3c2410_dma_getposition(enum dma_ch channel, dma_addr_t *src, dma_addr_t *dst)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);

	if (chan == NULL)
		return -EINVAL;

	if (src != NULL)
 		*src = dma_rdreg(chan, S3C2410_DMA_DCSRC);

 	if (dst != NULL)
 		*dst = dma_rdreg(chan, S3C2410_DMA_DCDST);

 	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_getposition);

/* system core operations */

#ifdef CONFIG_PM

static void s3c2410_dma_suspend_chan(struct s3c2410_dma_chan *cp)
{
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
}

static int s3c2410_dma_suspend(void)
{
	struct s3c2410_dma_chan *cp = s3c2410_chans;
	int channel;

	for (channel = 0; channel < dma_channels; cp++, channel++)
		s3c2410_dma_suspend_chan(cp);

	return 0;
}

static void s3c2410_dma_resume_chan(struct s3c2410_dma_chan *cp)
{
	unsigned int no = cp->number | DMACH_LOW_LEVEL;

	/* restore channel's hardware configuration */

	if (!cp->in_use)
		return;

	printk(KERN_INFO "dma%d: restoring configuration\n", cp->number);

	s3c2410_dma_config(no, cp->xfer_unit);
	s3c2410_dma_devconfig(no, cp->source, cp->dev_addr);

	/* re-select the dma source for this channel */

	if (cp->map != NULL)
		dma_sel.select(cp, cp->map);
}

static void s3c2410_dma_resume(void)
{
	struct s3c2410_dma_chan *cp = s3c2410_chans + dma_channels - 1;
	int channel;

	for (channel = dma_channels - 1; channel >= 0; cp--, channel--)
		s3c2410_dma_resume_chan(cp);
}

#else
#define s3c2410_dma_suspend NULL
#define s3c2410_dma_resume  NULL
#endif /* CONFIG_PM */

struct syscore_ops dma_syscore_ops = {
	.suspend	= s3c2410_dma_suspend,
	.resume		= s3c2410_dma_resume,
};

/* kmem cache implementation */

static void s3c2410_dma_cache_ctor(void *p)
{
	memset(p, 0, sizeof(struct s3c2410_dma_buf));
}

/* initialisation code */

static int __init s3c24xx_dma_syscore_init(void)
{
	register_syscore_ops(&dma_syscore_ops);

	return 0;
}

late_initcall(s3c24xx_dma_syscore_init);

int __init s3c24xx_dma_init(unsigned int channels, unsigned int irq,
			    unsigned int stride)
{
	struct s3c2410_dma_chan *cp;
	int channel;
	int ret;

	printk("S3C24XX DMA Driver, Copyright 2003-2006 Simtec Electronics\n");

	dma_channels = channels;

	dma_base = ioremap(S3C24XX_PA_DMA, stride * channels);
	if (dma_base == NULL) {
		printk(KERN_ERR "dma failed to remap register block\n");
		return -ENOMEM;
	}

	dma_kmem = kmem_cache_create("dma_desc",
				     sizeof(struct s3c2410_dma_buf), 0,
				     SLAB_HWCACHE_ALIGN,
				     s3c2410_dma_cache_ctor);

	if (dma_kmem == NULL) {
		printk(KERN_ERR "dma failed to make kmem cache\n");
		ret = -ENOMEM;
		goto err;
	}

	for (channel = 0; channel < channels;  channel++) {
		cp = &s3c2410_chans[channel];

		memset(cp, 0, sizeof(struct s3c2410_dma_chan));

		/* dma channel irqs are in order.. */
		cp->number = channel;
		cp->irq    = channel + irq;
		cp->regs   = dma_base + (channel * stride);

		/* point current stats somewhere */
		cp->stats  = &cp->stats_store;
		cp->stats_store.timeout_shortest = LONG_MAX;

		/* basic channel configuration */

		cp->load_timeout = 1<<18;

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

int __init s3c2410_dma_init(void)
{
	return s3c24xx_dma_init(4, IRQ_DMA0, 0x40);
}

static inline int is_channel_valid(unsigned int channel)
{
	return (channel & DMA_CH_VALID);
}

static struct s3c24xx_dma_order *dma_order;


/* s3c2410_dma_map_channel()
 *
 * turn the virtual channel number into a real, and un-used hardware
 * channel.
 *
 * first, try the dma ordering given to us by either the relevant
 * dma code, or the board. Then just find the first usable free
 * channel
*/

static struct s3c2410_dma_chan *s3c2410_dma_map_channel(int channel)
{
	struct s3c24xx_dma_order_ch *ord = NULL;
	struct s3c24xx_dma_map *ch_map;
	struct s3c2410_dma_chan *dmach;
	int ch;

	if (dma_sel.map == NULL || channel > dma_sel.map_size)
		return NULL;

	ch_map = dma_sel.map + channel;

	/* first, try the board mapping */

	if (dma_order) {
		ord = &dma_order->channels[channel];

		for (ch = 0; ch < dma_channels; ch++) {
			int tmp;
			if (!is_channel_valid(ord->list[ch]))
				continue;

			tmp = ord->list[ch] & ~DMA_CH_VALID;
			if (s3c2410_chans[tmp].in_use == 0) {
				ch = tmp;
				goto found;
			}
		}

		if (ord->flags & DMA_CH_NEVER)
			return NULL;
	}

	/* second, search the channel map for first free */

	for (ch = 0; ch < dma_channels; ch++) {
		if (!is_channel_valid(ch_map->channels[ch]))
			continue;

		if (s3c2410_chans[ch].in_use == 0) {
			printk("mapped channel %d to %d\n", channel, ch);
			break;
		}
	}

	if (ch >= dma_channels)
		return NULL;

	/* update our channel mapping */

 found:
	dmach = &s3c2410_chans[ch];
	dmach->map = ch_map;
	dmach->req_ch = channel;
	s3c_dma_chan_map[channel] = dmach;

	/* select the channel */

	(dma_sel.select)(dmach, ch_map);

	return dmach;
}

static int s3c24xx_dma_check_entry(struct s3c24xx_dma_map *map, int ch)
{
	return 0;
}

int __init s3c24xx_dma_init_map(struct s3c24xx_dma_selection *sel)
{
	struct s3c24xx_dma_map *nmap;
	size_t map_sz = sizeof(*nmap) * sel->map_size;
	int ptr;

	nmap = kmemdup(sel->map, map_sz, GFP_KERNEL);
	if (nmap == NULL)
		return -ENOMEM;

	memcpy(&dma_sel, sel, sizeof(*sel));

	dma_sel.map = nmap;

	for (ptr = 0; ptr < sel->map_size; ptr++)
		s3c24xx_dma_check_entry(nmap+ptr, ptr);

	return 0;
}

int __init s3c24xx_dma_order_set(struct s3c24xx_dma_order *ord)
{
	struct s3c24xx_dma_order *nord = dma_order;

	if (nord == NULL)
		nord = kmalloc(sizeof(struct s3c24xx_dma_order), GFP_KERNEL);

	if (nord == NULL) {
		printk(KERN_ERR "no memory to store dma channel order\n");
		return -ENOMEM;
	}

	dma_order = nord;
	memcpy(nord, ord, sizeof(struct s3c24xx_dma_order));
	return 0;
}
