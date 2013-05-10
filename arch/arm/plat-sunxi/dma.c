/*
 * arch/arm/plat-sunxi/dma/dma.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * huangxin <huangxin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <asm/memory.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <mach/hardware.h>
#include <plat/dma.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>

#include <mach/platform.h>
#include "dma_regs.h"
#if defined CONFIG_ARCH_SUN4I
#include "dma_route_sun4i.h"
#elif defined CONFIG_ARCH_SUN5I
#include "dma_route_sun5i.h"
#endif


#undef DEBUG

/* io map for dma */
static void __iomem *dma_base;
static struct kmem_cache *dma_kmem;

static int dma_channels;

static struct sw_dma_selection dma_sel;

/* dma channel state information */
struct sw_dma_chan sw_chans[SW_DMA_CHANNELS];

/* debugging functions */

#define BUF_MAGIC (0xcefabdba)

#define dmawarn(fmt...) printk(KERN_DEBUG fmt)

#define dma_regaddr(chan, reg) ((chan)->regs + (reg))

#define dma_wrreg(chan, reg, val) writel((val), (chan)->regs + (reg))

#define dma_rdreg(chan, reg) readl((chan)->regs + (reg))

/* captured register state for debug */

struct sw_dma_regstate {
	unsigned long         dirqen;		/* irq enable bits */
	unsigned long         dirqpd;		/* irq pending bits */
	unsigned long         dconf;		/* dma config bits */
	unsigned long         dsrc;			/* dma src (not shadow) */
	unsigned long         ddst;			/* dma dst (not shadow) */
	unsigned long         dcnt;			/* dma count (not shadow) */
};

#ifdef CONFIG_SW_DMA_DEBUG
static void
pr_debug_capture(struct sw_dma_chan *chan, struct sw_dma_regstate *regs)
{
	regs->dirqen  = readl(dma_base + SW_DMA_DIRQEN);
	regs->dirqpd  = readl(dma_base + SW_DMA_DIRQPD);
	regs->dsrc    = dma_rdreg(chan, SW_DMA_DSRC);
	regs->ddst    = dma_rdreg(chan, SW_DMA_DDST);
	regs->dcnt    = dma_rdreg(chan, SW_DMA_DCNT);
	regs->dconf   = dma_rdreg(chan, SW_DMA_DCONF);
}

static void
pr_debug_dumpregs(const char *fname, int line, struct sw_dma_chan *chan,
		 struct sw_dma_regstate *regs)
{
	pr_debug("dma%d: %s:%d: IRQEN=%08lx, IRQPD=%08lx, SRC=%08lx "
		        "DST=%08lx, DCNT=%lx, DCONF=%08lx\n",
	       chan->number, fname, line, regs->dirqen, regs->dirqpd,
			regs->dsrc, regs->ddst, regs->dcnt, regs->dconf
	       );
}

static void
pr_debug_showchan(const char *fname, int line, struct sw_dma_chan *chan)
{
	struct sw_dma_regstate state;
	struct sw_dma_buf  *buf;

	pr_debug_capture(chan, &state);

	pr_debug("dma%d: %s:%d: ls=%d, queue ->",
	       chan->number, fname, line, chan->load_state);
	for(buf=chan->curr; !!(buf); buf=buf->next)
		pr_debug(" %x", buf->data);
	pr_debug(" <-\n");

	pr_debug_dumpregs(fname, line, chan, &state);
}

static void
pr_debug_showregs(const char *fname, int line, struct sw_dma_chan *chan)
{
	struct sw_dma_regstate state;

	pr_debug_capture(chan, &state);
	pr_debug_dumpregs(fname, line, chan, &state);
}

#define dbg_showregs(chan) pr_debug_showregs(__func__, __LINE__, (chan))
#define dbg_showchan(chan) pr_debug_showchan(__func__, __LINE__, (chan))
#else
#define dbg_showregs(chan) do { } while(0)
#define dbg_showchan(chan) do { } while(0)
#endif /* CONFIG_SW_DMA_DEBUG */

static struct sw_dma_chan *dma_chan_map[DMACH_MAX];

/* lookup_dma_channel
 *
 * change the dma channel number given into a real dma channel id
*/

static struct sw_dma_chan *lookup_dma_channel(unsigned int channel)
{
	if (channel & DMACH_LOW_LEVEL)
		return &sw_chans[channel & ~DMACH_LOW_LEVEL];
	else
		return dma_chan_map[channel];
}

inline void DMA_COPY_HW_CONF(struct dma_hw_conf *to, struct dma_hw_conf *from)
{
	to->xfer_type    = from->xfer_type;
	to->drqsrc_type  = from->drqsrc_type;
	to->drqdst_type  = from->drqdst_type;
	to->address_type = from->address_type;
	to->dir          = from->dir;
	to->reload       = from->reload;
	to->hf_irq       = from->hf_irq;
	to->from         = from->from;
	to->to           = from->to;
	to->cmbk         = from->cmbk;
}

/* sw_dma_stats_timeout
 *
 * Update DMA stats from timeout info
*/

static void
sw_dma_stats_timeout(struct sw_dma_stats *stats, int val)
{
	if (stats == NULL)
		return;

	if (val > stats->timeout_longest)
		stats->timeout_longest = val;
	if (val < stats->timeout_shortest)
		stats->timeout_shortest = val;

	stats->timeout_avg += val;
}

/* sw_dma_waitforload
 *
 * wait for the DMA engine to load a buffer, and update the state accordingly
*/

static int sw_dma_waitforload(struct sw_dma_chan *chan, int line)
{
	int timeout = chan->load_timeout;
	int took;

	if (chan->load_state != SW_DMALOAD_1LOADED) {
		printk(KERN_ERR "dma%d: sw_dma_waitforload() called in loadstate %d from line %d\n", chan->number, chan->load_state, line);
		return 0;
	}

	if (chan->stats != NULL)
		chan->stats->loads++;

	while (--timeout > 0) {
		if (dma_rdreg(chan, SW_DMA_DCONF) & (1<<31) || /* runing */
			readl(dma_base + SW_DMA_DIRQPD) & (2 << (chan->number<<1)) /* pending */ ) {
			took = chan->load_timeout - timeout;

			sw_dma_stats_timeout(chan->stats, took);

			switch (chan->load_state) {
			case SW_DMALOAD_1LOADED:
				chan->load_state = SW_DMALOAD_1RUNNING;
				pr_debug("L%d(from %d), loadstate SW_DMALOAD_1LOADED -> SW_DMALOAD_1RUNNING\n", __LINE__, line);
				break;

			default:
				printk(KERN_ERR "dma%d: unknown load_state in sw_dma_waitforload() %d\n", chan->number, chan->load_state);
			}

			return 1;
		}
	}

	if (chan->stats != NULL) {
		chan->stats->timeout_failed++;
	}

	return 0;
}



/* sw_dma_loadbuffer
 *
 * load a buffer, and update the channel state
*/

static inline int sw_dma_loadbuffer(struct sw_dma_chan *chan, struct sw_dma_buf *buf)
{
	pr_debug("sw_chan_loadbuffer: loading buff %p (0x%08lx,0x%06x)\n",
		 buf, (unsigned long)buf->data, buf->size);

	if (buf == NULL) {
		dmawarn("buffer is NULL\n");
		return -EINVAL;
	}

	/* check the state of the channel before we do anything */

	if (chan->load_state == SW_DMALOAD_1LOADED) {
		dmawarn("load_state is SW_DMALOAD_1LOADED\n");
	}

	if (chan->load_state == SW_DMALOAD_1LOADED_1RUNNING) {
		dmawarn("state is SW_DMALOAD_1LOADED_1RUNNING\n");
	}

	if(chan->dcon & SW_NDMA_CONF_CONTI || chan->dcon & SW_DDMA_CONF_CONTI || chan->load_state == SW_DMALOAD_NONE){
		writel(__virt_to_bus(buf->data), chan->addr_reg);
		dma_wrreg(chan, SW_DMA_DCNT, buf->size);
	}

	chan->next = buf->next;

	/* update the state of the channel */

	switch (chan->load_state) {
	case SW_DMALOAD_NONE:
		pr_debug("L%d, loadstate SW_DMALOAD_NONE -> SW_DMALOAD_1LOADED\n", __LINE__);
		chan->load_state = SW_DMALOAD_1LOADED;
		break;

	case SW_DMALOAD_1RUNNING:
		pr_debug("L%d, loadstate SW_DMALOAD_1RUNNING -> SW_DMALOAD_1LOADED_1RUNNING\n", __LINE__);
		chan->load_state = SW_DMALOAD_1LOADED_1RUNNING;
		break;

	default:
		dmawarn("dmaload: unknown state %d in loadbuffer\n",
			chan->load_state);
		break;
	}

	return 0;
}

/* sw_dma_call_op
 *
 * small routine to call the op routine with the given op if it has been
 * registered
*/

static void sw_dma_call_op(struct sw_dma_chan *chan, enum sw_chan_op op)
{
	if (chan->op_fn != NULL) {
		(chan->op_fn)(chan, op);
	}
}

/* sw_dma_buffdone
 *
 * small wrapper to check if callback routine needs to be called, and
 * if so, call it
*/

static inline void sw_dma_buffdone(struct sw_dma_chan *chan, struct sw_dma_buf *buf,
		     enum sw_dma_buffresult result)
{
	if (chan->callback_fn != NULL) {
		(chan->callback_fn)(chan, buf->id, buf->size, result);
	}
}

static inline void sw_dma_halfdone(struct sw_dma_chan *chan, struct sw_dma_buf *buf,
		     enum sw_dma_buffresult result)
{
	if (chan->callback_hd != NULL) {
		(chan->callback_hd)(chan, buf->id, buf->size, result);
	}
}

/* sw_dma_start
 *
 * start a dma channel going
*/

static int sw_dma_start(struct sw_dma_chan *chan)
{
	unsigned long flags;

	//pr_debug("sw_start_dma: channel=%d\n", chan->number);

	local_irq_save(flags);

	if (chan->state == SW_DMA_RUNNING) {
		pr_debug("sw_start_dma: already running (%d)\n", chan->state);
		local_irq_restore(flags);
		return 0;
	}

	chan->state = SW_DMA_RUNNING;

	/* check wether there is anything to load, and if not, see
	 * if we can find anything to load
	 */

	if (chan->load_state == SW_DMALOAD_NONE) {
		if (chan->next == NULL) {
			printk(KERN_ERR "dma%d: channel has nothing loaded\n",
			       chan->number);
			chan->state = SW_DMA_IDLE;
			local_irq_restore(flags);
			return -EINVAL;
		}

		sw_dma_loadbuffer(chan, chan->next);
	}

	dbg_showchan(chan);

	//printk("[%s] dcon=0x%08x\n", __FUNCTION__, (unsigned int)chan->dcon);
	dma_wrreg(chan, SW_DMA_DCONF, SW_DCONF_LOADING | chan->dcon);


	sw_dma_call_op(chan, SW_DMAOP_START);

	dbg_showchan(chan);

	/* if we've only loaded one buffer onto the channel, then chec
	 * to see if we have another, and if so, try and load it so when
	 * the first buffer is finished, the new one will be loaded onto
	 * the channel */

	if (chan->next != NULL) {
		if (chan->load_state == SW_DMALOAD_1LOADED) {

			if (sw_dma_waitforload(chan, __LINE__) == 0) {
				pr_debug("%s: buff not yet loaded, no more todo\n",
					 __func__);
			} else {
		        pr_debug("L%d, loadstate %d -> SW_DMALOAD_1RUNNING\n", __LINE__, chan->load_state);
				chan->load_state = SW_DMALOAD_1RUNNING;
				sw_dma_loadbuffer(chan, chan->next);
			}

		} else if (chan->load_state == SW_DMALOAD_1RUNNING) {
			sw_dma_loadbuffer(chan, chan->next);
		}
	}


	local_irq_restore(flags);

	return 0;
}

/* sw_dma_canload
 *
 * work out if we can queue another buffer into the DMA engine
*/

static int
sw_dma_canload(struct sw_dma_chan *chan)
{
	if (chan->load_state == SW_DMALOAD_NONE ||
	    chan->load_state == SW_DMALOAD_1RUNNING)
		return 1;

	return 0;
}

/* sw_dma_enqueue
 *
 * queue an given buffer for dma transfer.
 *
 * id         the device driver's id information for this buffer
 * data       the physical address of the buffer data
 * size       the size of the buffer in bytes
 *
 * If the channel is not running, then the flag SW_DMAF_AUTOSTART
 * is checked, and if set, the channel is started. If this flag isn't set,
 * then an error will be returned.
 *
 * It is possible to queue more than one DMA buffer onto a channel at
 * once, and the code will deal with the re-loading of the next buffer
 * when necessary.
*/

int sw_dma_enqueue(unsigned int channel, void *id,
			dma_addr_t data, int size)
{
	struct sw_dma_chan *chan = lookup_dma_channel(channel);
	struct sw_dma_buf *buf;
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

	buf->next  = NULL;
	buf->id    = id;
	buf->magic = BUF_MAGIC;
	buf->data  = buf->ptr = data;
	buf->size  = size;

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
	if (chan->state == SW_DMA_RUNNING) {
		if (chan->load_state == SW_DMALOAD_1LOADED && 1) {
			if (sw_dma_waitforload(chan, __LINE__) == 0) {
				printk(KERN_ERR "dma%d: loadbuffer:"
				       "timeout loading buffer\n",
				       chan->number);
				dbg_showchan(chan);
				local_irq_restore(flags);
				return -EINVAL;
			}
		}

		while (sw_dma_canload(chan) && chan->next != NULL) {
			sw_dma_loadbuffer(chan, chan->next);
		}
	} else if (chan->state == SW_DMA_IDLE) {
		if (chan->flags & SW_DMAF_AUTOSTART) {
			sw_dma_ctrl(chan->number | DMACH_LOW_LEVEL,
					 SW_DMAOP_START);
		}
	}

	local_irq_restore(flags);
	return 0;
}

EXPORT_SYMBOL(sw_dma_enqueue);

static inline void
sw_dma_freebuf(struct sw_dma_buf *buf)
{
	int magicok = (buf->magic == BUF_MAGIC);

	buf->magic = -1;

	if (magicok) {
		kmem_cache_free(dma_kmem, buf);
	} else {
		printk("sw_dma_freebuf: buff %p with bad magic\n", buf);
	}
}

/* sw_dma_lastxfer
 *
 * called when the system is out of buffers, to ensure that the channel
 * is prepared for shutdown.
*/

static inline void
sw_dma_lastxfer(struct sw_dma_chan *chan)
{
	//printk("[%s] enter\n", __FUNCTION__);

	switch (chan->load_state) {
	case SW_DMALOAD_NONE:
		break;

	case SW_DMALOAD_1LOADED:
		if (sw_dma_waitforload(chan, __LINE__) == 0) {
				/* flag error? */
			printk(KERN_ERR "dma%d: timeout waiting for load (%s)\n",
			       chan->number, __func__);
			return;
		}
		break;

	case SW_DMALOAD_1LOADED_1RUNNING:
		/* I belive in this case we do not have anything to do
		 * until the next buffer comes along, and we turn off the
		 * reload */
		return;

	default:
		pr_debug("dma%d: lastxfer: unhandled load_state %d with no next\n",
			 chan->number, chan->load_state);
		return;

	}
}


#define pr_debug2(x...)

void exec_pending_chan(int chan_nr, unsigned long pend_bits)
{
	struct sw_dma_chan *chan;
	struct sw_dma_buf  *buf;
	unsigned long tmp;
	unsigned long flags;

	writel(pend_bits, dma_base + SW_DMA_DIRQPD);

	chan = &sw_chans[chan_nr];
	buf = chan->curr;

	/* Check me */
	if (chan->map == NULL) {
		pr_warning("Unexpected pending interrupt detected, pend_bits=0x%08x\n", (unsigned int)pend_bits);
		return;
	}

	tmp = chan->map->conf_ptr->hf_irq & (pend_bits >> (chan_nr << 1));
	if( tmp  & SW_DMA_IRQ_HALF ){
		if(chan->state != SW_DMA_IDLE)     //if dma is stopped by app, app may not want callback
			sw_dma_halfdone(chan, buf, SW_RES_OK);
	}
	if (!(tmp & SW_DMA_IRQ_FULL))
		return;

	dbg_showchan(chan);
	/* modify the channel state */
	switch (chan->load_state) {
	case SW_DMALOAD_1RUNNING:
		/* TODO - if we are running only one buffer, we probably
		 * want to reload here, and then worry about the buffer
		 * callback */

		pr_debug("L%d, loadstate SW_DMALOAD_1RUNNING -> SW_DMALOAD_NONE\n", __LINE__);
		chan->load_state = SW_DMALOAD_NONE;
		break;

	case SW_DMALOAD_1LOADED:
		/* iirc, we should go back to NONE loaded here, we
		 * had a buffer, and it was never verified as being
		 * loaded.
		 */

		pr_debug("L%d, loadstate SW_DMALOAD_1LOADED -> SW_DMALOAD_NONE\n", __LINE__);
		chan->load_state = SW_DMALOAD_NONE;
		break;

	case SW_DMALOAD_1LOADED_1RUNNING:
		/* we'll worry about checking to see if another buffer is
		 * ready after we've called back the owner. This should
		 * ensure we do not wait around too long for the DMA
		 * engine to start the next transfer
		 */

		pr_debug("L%d, loadstate SW_DMALOAD_1LOADED_1RUNNING -> SW_DMALOAD_1LOADED\n", __LINE__);
		chan->load_state = SW_DMALOAD_1LOADED;

		if(!(( chan->dcon & SW_NDMA_CONF_CONTI) || (chan->dcon & SW_DDMA_CONF_CONTI))){
			struct sw_dma_buf  *next = chan->curr->next;

			writel(__virt_to_bus(next->data), chan->addr_reg);
			dma_wrreg(chan, SW_DMA_DCNT, next->size);
			tmp = SW_DCONF_LOADING | chan->dcon;
			dma_wrreg(chan, SW_DMA_DCONF, tmp);
			tmp = dma_rdreg(chan, SW_DMA_DCONF);
			if (sw_dma_waitforload(chan, __LINE__) == 0) {
				printk(KERN_ERR "dma%d: timeout waiting for load (%s)\n",
				       chan->number, __func__);
				return;
			}
		}

		break;

	case SW_DMALOAD_NONE:
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
			return;
		}

		if(chan->state != SW_DMA_IDLE)     //if dma is stopped by app, app may not want callback
			sw_dma_buffdone(chan, buf, SW_RES_OK);

		/* free resouces */
		sw_dma_freebuf(buf);
		/* modify by yemao, 2011-07-28
		 * check load state after call dma callback, because some relative states may be changed
		 * in callback operation. if there is another buffer loaded in dma queue, run it and
		 * change relative state for next transfer.
		 * waitforload operation must follow dma loading to update dma load state
		 */
		if(chan->load_state == SW_DMALOAD_1LOADED && !((chan->dcon & SW_NDMA_CONF_CONTI)||(chan->dcon & SW_DDMA_CONF_CONTI))){
			writel(__virt_to_bus(chan->curr->data), chan->addr_reg);
			dma_wrreg(chan, SW_DMA_DCNT, chan->curr->size);
			tmp = SW_DCONF_LOADING | chan->dcon;
			dma_wrreg(chan, SW_DMA_DCONF, tmp);
			tmp = dma_rdreg(chan, SW_DMA_DCONF);
			if (sw_dma_waitforload(chan, __LINE__) == 0) {
				/* flag error? */
				printk(KERN_ERR "dma%d: timeout waiting for load (%s)\n",
				       chan->number, __func__);
				return;
			}
		}
	} else {
	}
	/* only reload if the channel is still running... our buffer done
	 * routine may have altered the state by requesting the dma channel
	 * to stop or shutdown... */

	/* todo: check that when the channel is shut-down from inside this
	 * function, we cope with unsetting reload, etc */

	if (chan->next != NULL && chan->state != SW_DMA_IDLE) {

		switch (chan->load_state) {
		case SW_DMALOAD_1RUNNING:
			/* don't need to do anything for this state */
			break;

		case SW_DMALOAD_NONE:
			/* can load buffer immediately */
			break;

		case SW_DMALOAD_1LOADED:
			break;

		case SW_DMALOAD_1LOADED_1RUNNING:
			return;

		default:
			printk(KERN_ERR "dma%d: unknown load_state in irq, %d\n",
			       chan->number, chan->load_state);
			return;
		}

		local_irq_save(flags);
		sw_dma_loadbuffer(chan, chan->next);
		local_irq_restore(flags);
	} else {
		sw_dma_lastxfer(chan);

		/* see if we can stop this channel.. */
		if (chan->load_state == SW_DMALOAD_NONE) {
			pr_debug("dma%d: end of transfer, stopping channel (%ld)\n",
				 chan->number, jiffies);
			sw_dma_ctrl(chan->number | DMACH_LOW_LEVEL,
					 SW_DMAOP_STOP);
		}
	}
}

static irqreturn_t
sw_dma_irq(int irq, void *dma_pending)
{
	unsigned long pend_reg;
	unsigned long pend_bits;
	int i;

	pr_debug("sw_dma_irq\n");

	pend_reg = readl(dma_base + SW_DMA_DIRQPD);

	for(i=0; i<16; i++){
		pend_bits = pend_reg & ( 3 <<  (i<<1) );
		if(pend_bits){
			exec_pending_chan(i, pend_bits);
		}
	}
	return IRQ_HANDLED;
}

/*
 * helper for dma pending check in irq disabled env.
 * it dose fully like the dma irq triggled.
 * mostly you can check if dma finished by using flags set within
 * bufferdone call back function.
 */
void poll_dma_pending(int chan_nr)
{
	unsigned long pend_bits;

	if (chan_nr & DMACH_LOW_LEVEL)
		chan_nr = chan_nr & ~DMACH_LOW_LEVEL;
	else
		chan_nr = (lookup_dma_channel(chan_nr))->number;

	pend_bits = readl(dma_base + SW_DMA_DIRQPD)  & (3 << (chan_nr << 1));
	if(pend_bits){
		exec_pending_chan(chan_nr, pend_bits);
	}
}
EXPORT_SYMBOL(poll_dma_pending);

static struct sw_dma_chan *sw_dma_map_channel(int channel);

/* sw_request_dma
 *
 * get control of an dma channel
*/

int sw_dma_request(unsigned int channel,
			struct sw_dma_client *client,
			void *dev)
{
	struct sw_dma_chan *chan;
	unsigned long flags, temp;

	pr_debug("dma%d: sw_request_dma: client=%s, dev=%p\n",
		 channel, client->name, dev);

	local_irq_save(flags);

	chan = sw_dma_map_channel(channel);
	if (chan == NULL) {
		local_irq_restore(flags);
		return -EBUSY;
	}

	dbg_showchan(chan);

	chan->client = client;
	chan->in_use = 1;

	temp = readl(dma_base + SW_DMA_DIRQPD);
	temp &= (3 << (chan->number<<1));
	writel(temp, dma_base + SW_DMA_DIRQPD);

	local_irq_restore(flags);

	chan->dev_id = dev;

	/* need to setup */

	pr_debug("%s: channel initialised, %p\n", __func__, chan);

	return chan->number | DMACH_LOW_LEVEL;
}

EXPORT_SYMBOL(sw_dma_request);

/* sw_dma_free
 *
 * release the given channel back to the system, will stop and flush
 * any outstanding transfers, and ensure the channel is ready for the
 * next claimant.
 *
 * Note, although a warning is currently printed if the freeing client
 * info is not the same as the registrant's client info, the free is still
 * allowed to go through.
*/

int sw_dma_free(unsigned int channel, struct sw_dma_client *client)
{
	struct sw_dma_chan *chan = lookup_dma_channel(channel);
	unsigned long flags;

	if (chan == NULL)
		return -EINVAL;

	local_irq_save(flags);

	if (chan->client != client) {
		printk(KERN_WARNING "dma%d: possible free from different client (channel %p, passed %p)\n",
		       channel, chan->client, client);
	}

	/* sort out stopping and freeing the channel */

	if (chan->state != SW_DMA_IDLE) {
		pr_debug("%s: need to stop dma channel %p\n",
		       __func__, chan);

		/* possibly flush the channel */
		sw_dma_ctrl(channel, SW_DMAOP_STOP);
	}

	chan->client = NULL;
	chan->in_use = 0;

	if (!(channel & DMACH_LOW_LEVEL))
		dma_chan_map[channel] = NULL;

	local_irq_restore(flags);

	return 0;
}

EXPORT_SYMBOL(sw_dma_free);

static int sw_dma_dostop(struct sw_dma_chan *chan)
{
	unsigned long flags;
	unsigned long tmp;

	pr_debug("%s:\n", __func__);

	dbg_showchan(chan);

	local_irq_save(flags);

	sw_dma_call_op(chan,  SW_DMAOP_STOP);

	tmp = dma_rdreg(chan, SW_DMA_DCONF);
	tmp &= ~SW_DCONF_LOADING;
	dma_wrreg(chan, SW_DMA_DCONF, tmp);

	/* should stop do this, or should we wait for flush? */
	chan->state      = SW_DMA_IDLE;
	pr_debug("L%d, loadstate %d -> SW_DMALOAD_NONE\n", __LINE__, chan->load_state);
	chan->load_state = SW_DMALOAD_NONE;

	local_irq_restore(flags);

	return 0;
}

static void sw_dma_waitforstop(struct sw_dma_chan *chan)
{
	unsigned long tmp;
	unsigned int timeout = 0x10000;

	while (timeout-- > 0) {
		tmp = dma_rdreg(chan, SW_DMA_DCONF);

		if (!(tmp & SW_DCONF_LOADING))
			return;
	}

	pr_debug("dma%d: failed to stop?\n", chan->number);
}


/* sw_dma_flush
 *
 * stop the channel, and remove all current and pending transfers
*/

static int sw_dma_flush(struct sw_dma_chan *chan)
{
	struct sw_dma_buf *buf, *next;
	unsigned long flags;

	pr_debug("%s: chan %p (%d)\n", __func__, chan, chan->number);

	dbg_showchan(chan);

	local_irq_save(flags);

	if (chan->state != SW_DMA_IDLE) {
		pr_debug("%s: stopping channel...\n", __func__ );
		sw_dma_ctrl(chan->number, SW_DMAOP_STOP);
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

			sw_dma_buffdone(chan, buf, SW_RES_ABORT);
			sw_dma_freebuf(buf);
		}
	}

	dbg_showregs(chan);

	sw_dma_waitforstop(chan);

	dbg_showregs(chan);

	local_irq_restore(flags);

	return 0;
}

static int sw_dma_started(struct sw_dma_chan *chan)
{
	unsigned long flags;

	local_irq_save(flags);

	dbg_showchan(chan);

	/* if we've only loaded one buffer onto the channel, then chec
	 * to see if we have another, and if so, try and load it so when
	 * the first buffer is finished, the new one will be loaded onto
	 * the channel */

	if (chan->next != NULL) {
		if (chan->load_state == SW_DMALOAD_1LOADED) {

			if (sw_dma_waitforload(chan, __LINE__) == 0) {
				pr_debug("%s: buff not yet loaded, no more todo\n",
					 __func__);
			} else {
	            pr_debug("L%d, loadstate %d -> SW_DMALOAD_NONE\n", __LINE__, chan->load_state);
				chan->load_state = SW_DMALOAD_1RUNNING;
				sw_dma_loadbuffer(chan, chan->next);
			}

		} else if (chan->load_state == SW_DMALOAD_1RUNNING) {
			sw_dma_loadbuffer(chan, chan->next);
		}
	}


	local_irq_restore(flags);

	return 0;

}

int sw_dma_ctrl(unsigned int channel, enum sw_chan_op op)
{
	struct sw_dma_chan *chan = lookup_dma_channel(channel);

	if (chan == NULL)
		return -EINVAL;

	switch (op) {
	case SW_DMAOP_START:
		return sw_dma_start(chan);

	case SW_DMAOP_STOP:
		return sw_dma_dostop(chan);

	case SW_DMAOP_PAUSE:
	case SW_DMAOP_RESUME:
		return -ENOENT;

	case SW_DMAOP_FLUSH:
		return sw_dma_flush(chan);

	case SW_DMAOP_STARTED:
		return sw_dma_started(chan);

	case SW_DMAOP_TIMEOUT:
		return 0;

	}

	return -ENOENT;      /* unknown, don't bother */
}

EXPORT_SYMBOL(sw_dma_ctrl);

/* DMA configuration for each channel
 *
 * DISRCC -> source of the DMA (AHB,APB)
 * DISRC  -> source address of the DMA
 * DIDSTC -> destination of the DMA (AHB,APD)
 * DIDST  -> destination address of the DMA
*/

/* sw_dma_config
 *
 * x:            transfer unit type
 * dir:          1 - to dev / 0 - from dev
 * dcon:         base value of the DCONx register
*/

int sw_dma_config(unsigned int channel, struct dma_hw_conf* user_conf)
{
	struct sw_dma_chan *chan = lookup_dma_channel(channel);
	struct dma_hw_conf* hw_conf;
	volatile unsigned long temp, dcon = 0;
	unsigned char drqdst, drqsrc;

	pr_debug("%s: chan=%p, user_conf=%p\n", __func__, chan, user_conf);

	if (chan == NULL)
		return -EINVAL;

	if(user_conf){
		pr_debug("%s: user_conf is used.\n", __func__);
		DMA_COPY_HW_CONF(&(chan->map->user_hw_conf), user_conf);
		hw_conf = chan->map->conf_ptr = &(chan->map->user_hw_conf);
	} else {
		hw_conf = chan->map->conf_ptr = (struct dma_hw_conf*)chan->map->default_hw_conf;
	}

	switch (hw_conf->dir) {
		case SW_DMA_RDEV:
			if(IS_DADECATE_DMA(chan)) {
				drqsrc = d_drqsrc_arr[hw_conf->drqsrc_type];
				drqdst = D_DRQDST_SDRAM;
			}
			else {

				drqsrc = n_drqsrc_arr[hw_conf->drqsrc_type];
				drqdst = N_DRQDST_SDRAM;
			}

			chan->addr_reg = dma_regaddr(chan, SW_DMA_DDST);
			dma_wrreg(chan, SW_DMA_DSRC,  hw_conf->from);
			break;

		case SW_DMA_WDEV:
			if(IS_DADECATE_DMA(chan)) {
				drqdst = d_drqdst_arr[hw_conf->drqdst_type];
				drqsrc = D_DRQSRC_SDRAM;
			}
			else {
				drqdst = n_drqdst_arr[hw_conf->drqdst_type];
				drqsrc = N_DRQSRC_SDRAM;
			}

			chan->addr_reg = dma_regaddr(chan, SW_DMA_DSRC);
			dma_wrreg(chan, SW_DMA_DDST,  hw_conf->to);
			break;

		case SW_DMA_M2M:
			if(IS_DADECATE_DMA(chan)) {
				drqdst = d_drqdst_arr[hw_conf->drqdst_type];
				drqsrc = D_DRQSRC_SDRAM;
			}
			else {
				drqdst = n_drqdst_arr[hw_conf->drqdst_type];
				drqsrc = N_DRQSRC_SDRAM;
			}

			chan->addr_reg = dma_regaddr(chan, SW_DMA_DSRC);
			dma_wrreg(chan, SW_DMA_DDST,  hw_conf->to);
			break;

		default:
			printk(KERN_ERR "dma %s: invalid r/w direction (%x)\n",chan->map->name, hw_conf->dir);
			return -EINVAL;
	}

	if (drqsrc == DRQ_INVALID || drqdst == DRQ_INVALID){
		printk(KERN_ERR "dma %s: invalid drq type\n",chan->map->name);
		return -EINVAL;
	}

	if(IS_DADECATE_DMA(chan))
		dcon |= drqsrc << D_DRQSRC_SHIFT | drqdst << D_DRQDST_SHIFT;
	else
		dcon |= drqsrc << N_DRQSRC_SHIFT | drqdst << N_DRQDST_SHIFT;

	dcon |= xfer_arr[hw_conf->xfer_type];
	dcon |= addrtype_arr[hw_conf->address_type];
	if(IS_DADECATE_DMA(chan)){
		dcon |= hw_conf->reload ? SW_DDMA_CONF_CONTI : 0;
	}
	else{
		dcon |= hw_conf->reload ? SW_NDMA_CONF_CONTI : 0;
	}
	dcon |= (1 << 15);   //backdoor: byte counter register shows the remain bytes for transfer
	chan->dcon = dcon;

	if( hw_conf->hf_irq < 2 ){
		printk(KERN_ERR "irq type is not suppoted yet.\n");
		return -EINVAL;
	}

	temp = readl(dma_base + SW_DMA_DIRQEN);
	temp &= ~(3 << (chan->number<<1));
	temp |= hw_conf->hf_irq << (chan->number<<1);
	writel(temp, dma_base + SW_DMA_DIRQEN);

	if( IS_DADECATE_DMA(chan)){
		dma_wrreg(chan, SW_DMA_DCMBK,  hw_conf->cmbk);
	}

	dbg_showchan(chan);

	return 0;
}

EXPORT_SYMBOL(sw_dma_config);

int sw_dma_setflags(unsigned int channel, unsigned int flags)
{
	struct sw_dma_chan *chan = lookup_dma_channel(channel);

	if (chan == NULL)
		return -EINVAL;

//	pr_debug("%s: chan=%p, flags=%08x\n", __func__, chan, flags);

	chan->flags = flags;

	return 0;
}

EXPORT_SYMBOL(sw_dma_setflags);


/* do we need to protect the settings of the fields from
 * irq?
*/

int sw_dma_set_opfn(unsigned int channel, sw_dma_opfn_t rtn)
{
	struct sw_dma_chan *chan = lookup_dma_channel(channel);

	if (chan == NULL)
		return -EINVAL;

	pr_debug("%s: chan=%p, op rtn=%p\n", __func__, chan, rtn);

	chan->op_fn = rtn;

	return 0;
}

EXPORT_SYMBOL(sw_dma_set_opfn);

int sw_dma_set_buffdone_fn(unsigned int channel, sw_dma_cbfn_t rtn)
{
	struct sw_dma_chan *chan = lookup_dma_channel(channel);

	if (chan == NULL)
		return -EINVAL;

	pr_debug("%s: chan=%p, callback rtn=%p\n", __func__, chan, rtn);

	chan->callback_fn = rtn;

	return 0;
}

EXPORT_SYMBOL(sw_dma_set_buffdone_fn);

int sw_dma_set_halfdone_fn(unsigned int channel, sw_dma_cbfn_t rtn)
{
	struct sw_dma_chan *chan = lookup_dma_channel(channel);

	if (chan == NULL)
		return -EINVAL;

	chan->callback_hd = rtn;

	return 0;
}

EXPORT_SYMBOL(sw_dma_set_halfdone_fn);

/* sw_dma_getposition
 *
 * returns the current transfer points for the dma source and destination
*/

int sw_dma_getposition(unsigned int channel, dma_addr_t *src, dma_addr_t *dst)
{
	struct sw_dma_chan *chan = lookup_dma_channel(channel);
	dma_addr_t s,d;

	if (chan == NULL)
		return -EINVAL;

	s = dma_rdreg(chan, SW_DMA_DSRC);
	d = dma_rdreg(chan, SW_DMA_DDST);

	if( chan->map->conf_ptr->dir == SW_DMA_RDEV ){
		*src = s;
		*dst = d;
	} else {
		*src = s;
		*dst = d;
	}

	return 0;
}

EXPORT_SYMBOL(sw_dma_getposition);

int sw_dma_getcurposition(unsigned int channel, dma_addr_t *src, dma_addr_t *dst)
{
	struct sw_dma_chan *chan = lookup_dma_channel(channel);
	//dma_addr_t s,d,count,countleft;
	dma_addr_t s,d,count;

	if (chan == NULL)
		return -EINVAL;

	s = dma_rdreg(chan, SW_DMA_DSRC);
	d = dma_rdreg(chan, SW_DMA_DDST);
	count = dma_rdreg(chan, SW_DMA_DCNT);

	/* FIXME: check */
#if 0
	temp = dma_rdreg(chan, SW_DMA_DCONF);
	temp |= (1<<15);
	dma_wrreg(chan, SW_DMA_DCONF, temp);
#endif

	//countleft = dma_rdreg(chan, SW_DMA_DCNT);

#if 0
	temp = dma_rdreg(chan, SW_DMA_DCONF);
	temp &= ~(1<<15);
	dma_wrreg(chan, SW_DMA_DCONF, temp);
#endif

        //printk("src = %x, count = %x , countleft = %x\n",s,count,countleft);
	//*src = s + (count - countleft);
	*src = s - count;
	*dst = d - count;


	return 0;
}

EXPORT_SYMBOL(sw_dma_getcurposition);

/* kmem cache implementation */

static void sw_dma_cache_ctor(void *p)
{
	memset(p, 0, sizeof(struct sw_dma_buf));
}

/* initialisation code */

int __devinit sw_dma_init(unsigned int channels, unsigned int irq,
			    unsigned int stride)
{
	struct sw_dma_chan *cp;
	int channel;
	int ret;

	printk("SOFTWINNER DMA Driver, (c) 2003-2004,2006 Simtec Electronics\n");

	dma_channels = channels;
	//dma_base = ioremap(SOFTWINNER_DMA_BASE, 4096);
	dma_base = (void __iomem *)SW_VA_DMAC_IO_BASE;
	dma_kmem = kmem_cache_create("dma_desc", sizeof(struct sw_dma_buf), 0,
				     SLAB_HWCACHE_ALIGN, sw_dma_cache_ctor);

	if (dma_kmem == NULL) {
		printk(KERN_ERR "dma failed to make kmem cache\n");
		ret = -ENOMEM;
		goto err2;
	}

	/* Disable & clear all interrupts */
	//writel(0x0, SW_VA_DMAC_IO_BASE);
	writel(0x0, dma_base);
	//writel(0xffffffff, SW_VA_DMAC_IO_BASE + 0x4);
	writel(0xffffffff, dma_base + 0x4);

	writel(1<<16, dma_base + 0x8);
	pr_debug("%s,%d,%x,%p\n",__func__,__LINE__,*(volatile int *)(dma_base + 0x8),dma_base + 0x8);
	for (channel = 0; channel < channels;  channel++) {
		cp = &sw_chans[channel];

		memset(cp, 0, sizeof(struct sw_dma_chan));

		cp->number = channel;

		if ((channel & 0xff) < 8) {
			cp->regs   = dma_base + 0x100 + (channel * stride);
		} else {
			cp->regs   = dma_base + 0x300 + ((channel - 8) * stride);
		}

		writel(0x0, cp->regs);
		writel(0x0, cp->regs + 0x4);
		writel(0x0, cp->regs + 0x8);
		writel(0x0, cp->regs + 0xc);

		dma_wrreg(cp, SW_DMA_DCONF, 0);

		/* point current stats somewhere */
		cp->stats  = &cp->stats_store;
		cp->stats_store.timeout_shortest = LONG_MAX;

		/* basic channel configuration */

		cp->load_timeout = 1<<18;
	}

	ret = request_irq(irq, sw_dma_irq, IRQF_DISABLED,
			  "dma_irq", dma_base + SW_DMA_DIRQPD);
	if(ret) {
		pr_err("Failed to require irq for DMA at %d\n", irq);
		goto err;
	}

	return 0;

 err:
	kmem_cache_destroy(dma_kmem);
 err2:
	dma_base = NULL;
	return ret;
}

int __devinit sw15_dma_init(void)
{
	return sw_dma_init(SW_DMA_CHANNELS, SW_INT_IRQNO_DMA, 0x20);
}

static inline int is_channel_valid(unsigned int channel)
{
	return (channel & DMA_CH_VALID);
}


/* sw_dma_map_channel()
 *
 * turn the virtual channel number into a real, and un-used hardware
 * channel.
 *
 * first, try the dma ordering given to us by either the relevant
 * dma code, or the board. Then just find the first usable free
 * channel
*/

static struct sw_dma_chan *sw_dma_map_channel(int channel)
{
	struct sw_dma_map *ch_map;
	struct sw_dma_chan *dmach;
	int ch;

	if (dma_sel.map == NULL || channel > dma_sel.map_size)
		return NULL;

	ch_map = dma_sel.map + channel;

	for (ch = 0; ch < dma_channels; ch++) {
		if (!is_channel_valid(ch_map->channels[ch]))
			continue;

		if (sw_chans[ch].in_use == 0) {
			break;
		}
	}

	if (ch >= dma_channels)
		return NULL;

	/* update our channel mapping */

	dmach = &sw_chans[ch];
	dmach->map = ch_map;
	dma_chan_map[channel] = dmach;

	return dmach;
}

static int sw_dma_check_entry(struct sw_dma_map *map, int ch)
{
	return 0;
}

int __devinit sw_dma_init_map(struct sw_dma_selection *sel)
{
	struct sw_dma_map *nmap;
	size_t map_sz = sizeof(*nmap) * sel->map_size;
	int ptr;

	nmap = kmalloc(map_sz, GFP_KERNEL);
	if (nmap == NULL)
		return -ENOMEM;

	memcpy(nmap, sel->map, map_sz);
	memcpy(&dma_sel, sel, sizeof(*sel));

	dma_sel.map = nmap;

	for (ptr = 0; ptr < sel->map_size; ptr++) {
		sw_dma_check_entry(nmap+ptr, ptr);
	}

	return 0;
}

static struct sw_dma_selection __refdata sw_dma_sel = {
	.dcon_mask	= 0xffffffff,
	.map		= sw_dma_mappings,
	.map_size	= ARRAY_SIZE(sw_dma_mappings),
};

static int __devinit sw_dmac_probe(struct platform_device *dev)
{
	int ret;

	sw15_dma_init();

	ret = sw_dma_init_map(&sw_dma_sel);

	if (ret) {
		early_printk("DMAC: failed to init map\n");
	} else {
		pr_info("Initialize DMAC OK\n");
	}

	return ret;
}
static int __devexit sw_dmac_remove(struct platform_device *dev)
{
        early_printk("[%s] enter\n", __FUNCTION__);
        return 0;
}
static int sw_dmac_suspend(struct platform_device *dev, pm_message_t state)
{
        early_printk("[%s] enter\n", __FUNCTION__);
        return 0;
}

static int sw_dmac_resume(struct platform_device *dev)
{
        early_printk("[%s] enter\n", __FUNCTION__);
        return 0;
}

static struct platform_driver sw_dmac_driver = {
        .probe          = sw_dmac_probe,
        .remove         = __devexit_p(sw_dmac_remove),
        .suspend        = sw_dmac_suspend,
        .resume         = sw_dmac_resume,
        .driver         = {
                .name   = "sw_dmac",
                .owner  = THIS_MODULE,
        },
};

static int __init sw_dma_drvinit(void)
{
        platform_driver_register(&sw_dmac_driver);
	return 0;
}

arch_initcall(sw_dma_drvinit);
