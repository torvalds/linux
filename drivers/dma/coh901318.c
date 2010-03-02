/*
 * driver/dma/coh901318.c
 *
 * Copyright (C) 2007-2009 ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 * DMA driver for COH 901 318
 * Author: Per Friden <per.friden@stericsson.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/fs.h> /* everything... */
#include <linux/slab.h> /* kmalloc() */
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <mach/coh901318.h>

#include "coh901318_lli.h"

#define COHC_2_DEV(cohc) (&cohc->chan.dev->device)

#ifdef VERBOSE_DEBUG
#define COH_DBG(x) ({ if (1) x; 0; })
#else
#define COH_DBG(x) ({ if (0) x; 0; })
#endif

struct coh901318_desc {
	struct dma_async_tx_descriptor desc;
	struct list_head node;
	struct scatterlist *sg;
	unsigned int sg_len;
	struct coh901318_lli *data;
	enum dma_data_direction dir;
	int pending_irqs;
	unsigned long flags;
};

struct coh901318_base {
	struct device *dev;
	void __iomem *virtbase;
	struct coh901318_pool pool;
	struct powersave pm;
	struct dma_device dma_slave;
	struct dma_device dma_memcpy;
	struct coh901318_chan *chans;
	struct coh901318_platform *platform;
};

struct coh901318_chan {
	spinlock_t lock;
	int allocated;
	int completed;
	int id;
	int stopped;

	struct work_struct free_work;
	struct dma_chan chan;

	struct tasklet_struct tasklet;

	struct list_head active;
	struct list_head queue;
	struct list_head free;

	unsigned long nbr_active_done;
	unsigned long busy;
	int pending_irqs;

	struct coh901318_base *base;
};

static void coh901318_list_print(struct coh901318_chan *cohc,
				 struct coh901318_lli *lli)
{
	struct coh901318_lli *l = lli;
	int i = 0;

	while (l) {
		dev_vdbg(COHC_2_DEV(cohc), "i %d, lli %p, ctrl 0x%x, src 0x%x"
			 ", dst 0x%x, link 0x%x virt_link_addr 0x%p\n",
			 i, l, l->control, l->src_addr, l->dst_addr,
			 l->link_addr, l->virt_link_addr);
		i++;
		l = l->virt_link_addr;
	}
}

#ifdef CONFIG_DEBUG_FS

#define COH901318_DEBUGFS_ASSIGN(x, y) (x = y)

static struct coh901318_base *debugfs_dma_base;
static struct dentry *dma_dentry;

static int coh901318_debugfs_open(struct inode *inode, struct file *file)
{

	file->private_data = inode->i_private;
	return 0;
}

static int coh901318_debugfs_read(struct file *file, char __user *buf,
				  size_t count, loff_t *f_pos)
{
	u64 started_channels = debugfs_dma_base->pm.started_channels;
	int pool_count = debugfs_dma_base->pool.debugfs_pool_counter;
	int i;
	int ret = 0;
	char *dev_buf;
	char *tmp;
	int dev_size;

	dev_buf = kmalloc(4*1024, GFP_KERNEL);
	if (dev_buf == NULL)
		goto err_kmalloc;
	tmp = dev_buf;

	tmp += sprintf(tmp, "DMA -- enabled dma channels\n");

	for (i = 0; i < debugfs_dma_base->platform->max_channels; i++)
		if (started_channels & (1 << i))
			tmp += sprintf(tmp, "channel %d\n", i);

	tmp += sprintf(tmp, "Pool alloc nbr %d\n", pool_count);
	dev_size = tmp  - dev_buf;

	/* No more to read if offset != 0 */
	if (*f_pos > dev_size)
		goto out;

	if (count > dev_size - *f_pos)
		count = dev_size - *f_pos;

	if (copy_to_user(buf, dev_buf + *f_pos, count))
		ret = -EINVAL;
	ret = count;
	*f_pos += count;

 out:
	kfree(dev_buf);
	return ret;

 err_kmalloc:
	return 0;
}

static const struct file_operations coh901318_debugfs_status_operations = {
	.owner		= THIS_MODULE,
	.open		= coh901318_debugfs_open,
	.read		= coh901318_debugfs_read,
};


static int __init init_coh901318_debugfs(void)
{

	dma_dentry = debugfs_create_dir("dma", NULL);

	(void) debugfs_create_file("status",
				   S_IFREG | S_IRUGO,
				   dma_dentry, NULL,
				   &coh901318_debugfs_status_operations);
	return 0;
}

static void __exit exit_coh901318_debugfs(void)
{
	debugfs_remove_recursive(dma_dentry);
}

module_init(init_coh901318_debugfs);
module_exit(exit_coh901318_debugfs);
#else

#define COH901318_DEBUGFS_ASSIGN(x, y)

#endif /* CONFIG_DEBUG_FS */

static inline struct coh901318_chan *to_coh901318_chan(struct dma_chan *chan)
{
	return container_of(chan, struct coh901318_chan, chan);
}

static inline dma_addr_t
cohc_dev_addr(struct coh901318_chan *cohc)
{
	return cohc->base->platform->chan_conf[cohc->id].dev_addr;
}

static inline const struct coh901318_params *
cohc_chan_param(struct coh901318_chan *cohc)
{
	return &cohc->base->platform->chan_conf[cohc->id].param;
}

static inline const struct coh_dma_channel *
cohc_chan_conf(struct coh901318_chan *cohc)
{
	return &cohc->base->platform->chan_conf[cohc->id];
}

static void enable_powersave(struct coh901318_chan *cohc)
{
	unsigned long flags;
	struct powersave *pm = &cohc->base->pm;

	spin_lock_irqsave(&pm->lock, flags);

	pm->started_channels &= ~(1ULL << cohc->id);

	if (!pm->started_channels) {
		/* DMA no longer intends to access memory */
		cohc->base->platform->access_memory_state(cohc->base->dev,
							  false);
	}

	spin_unlock_irqrestore(&pm->lock, flags);
}
static void disable_powersave(struct coh901318_chan *cohc)
{
	unsigned long flags;
	struct powersave *pm = &cohc->base->pm;

	spin_lock_irqsave(&pm->lock, flags);

	if (!pm->started_channels) {
		/* DMA intends to access memory */
		cohc->base->platform->access_memory_state(cohc->base->dev,
							  true);
	}

	pm->started_channels |= (1ULL << cohc->id);

	spin_unlock_irqrestore(&pm->lock, flags);
}

static inline int coh901318_set_ctrl(struct coh901318_chan *cohc, u32 control)
{
	int channel = cohc->id;
	void __iomem *virtbase = cohc->base->virtbase;

	writel(control,
	       virtbase + COH901318_CX_CTRL +
	       COH901318_CX_CTRL_SPACING * channel);
	return 0;
}

static inline int coh901318_set_conf(struct coh901318_chan *cohc, u32 conf)
{
	int channel = cohc->id;
	void __iomem *virtbase = cohc->base->virtbase;

	writel(conf,
	       virtbase + COH901318_CX_CFG +
	       COH901318_CX_CFG_SPACING*channel);
	return 0;
}


static int coh901318_start(struct coh901318_chan *cohc)
{
	u32 val;
	int channel = cohc->id;
	void __iomem *virtbase = cohc->base->virtbase;

	disable_powersave(cohc);

	val = readl(virtbase + COH901318_CX_CFG +
		    COH901318_CX_CFG_SPACING * channel);

	/* Enable channel */
	val |= COH901318_CX_CFG_CH_ENABLE;
	writel(val, virtbase + COH901318_CX_CFG +
	       COH901318_CX_CFG_SPACING * channel);

	return 0;
}

static int coh901318_prep_linked_list(struct coh901318_chan *cohc,
				      struct coh901318_lli *data)
{
	int channel = cohc->id;
	void __iomem *virtbase = cohc->base->virtbase;

	BUG_ON(readl(virtbase + COH901318_CX_STAT +
		     COH901318_CX_STAT_SPACING*channel) &
	       COH901318_CX_STAT_ACTIVE);

	writel(data->src_addr,
	       virtbase + COH901318_CX_SRC_ADDR +
	       COH901318_CX_SRC_ADDR_SPACING * channel);

	writel(data->dst_addr, virtbase +
	       COH901318_CX_DST_ADDR +
	       COH901318_CX_DST_ADDR_SPACING * channel);

	writel(data->link_addr, virtbase + COH901318_CX_LNK_ADDR +
	       COH901318_CX_LNK_ADDR_SPACING * channel);

	writel(data->control, virtbase + COH901318_CX_CTRL +
	       COH901318_CX_CTRL_SPACING * channel);

	return 0;
}
static dma_cookie_t
coh901318_assign_cookie(struct coh901318_chan *cohc,
			struct coh901318_desc *cohd)
{
	dma_cookie_t cookie = cohc->chan.cookie;

	if (++cookie < 0)
		cookie = 1;

	cohc->chan.cookie = cookie;
	cohd->desc.cookie = cookie;

	return cookie;
}

static struct coh901318_desc *
coh901318_desc_get(struct coh901318_chan *cohc)
{
	struct coh901318_desc *desc;

	if (list_empty(&cohc->free)) {
		/* alloc new desc because we're out of used ones
		 * TODO: alloc a pile of descs instead of just one,
		 * avoid many small allocations.
		 */
		desc = kmalloc(sizeof(struct coh901318_desc), GFP_NOWAIT);
		if (desc == NULL)
			goto out;
		INIT_LIST_HEAD(&desc->node);
	} else {
		/* Reuse an old desc. */
		desc = list_first_entry(&cohc->free,
					struct coh901318_desc,
					node);
		list_del(&desc->node);
	}

 out:
	return desc;
}

static void
coh901318_desc_free(struct coh901318_chan *cohc, struct coh901318_desc *cohd)
{
	list_add_tail(&cohd->node, &cohc->free);
}

/* call with irq lock held */
static void
coh901318_desc_submit(struct coh901318_chan *cohc, struct coh901318_desc *desc)
{
	list_add_tail(&desc->node, &cohc->active);

	BUG_ON(cohc->pending_irqs != 0);

	cohc->pending_irqs = desc->pending_irqs;
}

static struct coh901318_desc *
coh901318_first_active_get(struct coh901318_chan *cohc)
{
	struct coh901318_desc *d;

	if (list_empty(&cohc->active))
		return NULL;

	d = list_first_entry(&cohc->active,
			     struct coh901318_desc,
			     node);
	return d;
}

static void
coh901318_desc_remove(struct coh901318_desc *cohd)
{
	list_del(&cohd->node);
}

static void
coh901318_desc_queue(struct coh901318_chan *cohc, struct coh901318_desc *desc)
{
	list_add_tail(&desc->node, &cohc->queue);
}

static struct coh901318_desc *
coh901318_first_queued(struct coh901318_chan *cohc)
{
	struct coh901318_desc *d;

	if (list_empty(&cohc->queue))
		return NULL;

	d = list_first_entry(&cohc->queue,
			     struct coh901318_desc,
			     node);
	return d;
}

/*
 * DMA start/stop controls
 */
u32 coh901318_get_bytes_left(struct dma_chan *chan)
{
	unsigned long flags;
	u32 ret;
	struct coh901318_chan *cohc = to_coh901318_chan(chan);

	spin_lock_irqsave(&cohc->lock, flags);

	/* Read transfer count value */
	ret = readl(cohc->base->virtbase +
		    COH901318_CX_CTRL+COH901318_CX_CTRL_SPACING *
		    cohc->id) & COH901318_CX_CTRL_TC_VALUE_MASK;

	spin_unlock_irqrestore(&cohc->lock, flags);

	return ret;
}
EXPORT_SYMBOL(coh901318_get_bytes_left);


/* Stops a transfer without losing data. Enables power save.
   Use this function in conjunction with coh901318_continue(..)
*/
void coh901318_stop(struct dma_chan *chan)
{
	u32 val;
	unsigned long flags;
	struct coh901318_chan *cohc = to_coh901318_chan(chan);
	int channel = cohc->id;
	void __iomem *virtbase = cohc->base->virtbase;

	spin_lock_irqsave(&cohc->lock, flags);

	/* Disable channel in HW */
	val = readl(virtbase + COH901318_CX_CFG +
		    COH901318_CX_CFG_SPACING * channel);

	/* Stopping infinit transfer */
	if ((val & COH901318_CX_CTRL_TC_ENABLE) == 0 &&
	    (val & COH901318_CX_CFG_CH_ENABLE))
		cohc->stopped = 1;


	val &= ~COH901318_CX_CFG_CH_ENABLE;
	/* Enable twice, HW bug work around */
	writel(val, virtbase + COH901318_CX_CFG +
	       COH901318_CX_CFG_SPACING * channel);
	writel(val, virtbase + COH901318_CX_CFG +
	       COH901318_CX_CFG_SPACING * channel);

	/* Spin-wait for it to actually go inactive */
	while (readl(virtbase + COH901318_CX_STAT+COH901318_CX_STAT_SPACING *
		     channel) & COH901318_CX_STAT_ACTIVE)
		cpu_relax();

	/* Check if we stopped an active job */
	if ((readl(virtbase + COH901318_CX_CTRL+COH901318_CX_CTRL_SPACING *
		   channel) & COH901318_CX_CTRL_TC_VALUE_MASK) > 0)
		cohc->stopped = 1;

	enable_powersave(cohc);

	spin_unlock_irqrestore(&cohc->lock, flags);
}
EXPORT_SYMBOL(coh901318_stop);

/* Continues a transfer that has been stopped via 300_dma_stop(..).
   Power save is handled.
*/
void coh901318_continue(struct dma_chan *chan)
{
	u32 val;
	unsigned long flags;
	struct coh901318_chan *cohc = to_coh901318_chan(chan);
	int channel = cohc->id;

	spin_lock_irqsave(&cohc->lock, flags);

	disable_powersave(cohc);

	if (cohc->stopped) {
		/* Enable channel in HW */
		val = readl(cohc->base->virtbase + COH901318_CX_CFG +
			    COH901318_CX_CFG_SPACING * channel);

		val |= COH901318_CX_CFG_CH_ENABLE;

		writel(val, cohc->base->virtbase + COH901318_CX_CFG +
		       COH901318_CX_CFG_SPACING*channel);

		cohc->stopped = 0;
	}

	spin_unlock_irqrestore(&cohc->lock, flags);
}
EXPORT_SYMBOL(coh901318_continue);

bool coh901318_filter_id(struct dma_chan *chan, void *chan_id)
{
	unsigned int ch_nr = (unsigned int) chan_id;

	if (ch_nr == to_coh901318_chan(chan)->id)
		return true;

	return false;
}
EXPORT_SYMBOL(coh901318_filter_id);

/*
 * DMA channel allocation
 */
static int coh901318_config(struct coh901318_chan *cohc,
			    struct coh901318_params *param)
{
	unsigned long flags;
	const struct coh901318_params *p;
	int channel = cohc->id;
	void __iomem *virtbase = cohc->base->virtbase;

	spin_lock_irqsave(&cohc->lock, flags);

	if (param)
		p = param;
	else
		p = &cohc->base->platform->chan_conf[channel].param;

	/* Clear any pending BE or TC interrupt */
	if (channel < 32) {
		writel(1 << channel, virtbase + COH901318_BE_INT_CLEAR1);
		writel(1 << channel, virtbase + COH901318_TC_INT_CLEAR1);
	} else {
		writel(1 << (channel - 32), virtbase +
		       COH901318_BE_INT_CLEAR2);
		writel(1 << (channel - 32), virtbase +
		       COH901318_TC_INT_CLEAR2);
	}

	coh901318_set_conf(cohc, p->config);
	coh901318_set_ctrl(cohc, p->ctrl_lli_last);

	spin_unlock_irqrestore(&cohc->lock, flags);

	return 0;
}

/* must lock when calling this function
 * start queued jobs, if any
 * TODO: start all queued jobs in one go
 *
 * Returns descriptor if queued job is started otherwise NULL.
 * If the queue is empty NULL is returned.
 */
static struct coh901318_desc *coh901318_queue_start(struct coh901318_chan *cohc)
{
	struct coh901318_desc *cohd_que;

	/* start queued jobs, if any
	 * TODO: transmit all queued jobs in one go
	 */
	cohd_que = coh901318_first_queued(cohc);

	if (cohd_que != NULL) {
		/* Remove from queue */
		coh901318_desc_remove(cohd_que);
		/* initiate DMA job */
		cohc->busy = 1;

		coh901318_desc_submit(cohc, cohd_que);

		coh901318_prep_linked_list(cohc, cohd_que->data);

		/* start dma job */
		coh901318_start(cohc);

	}

	return cohd_que;
}

/*
 * This tasklet is called from the interrupt handler to
 * handle each descriptor (DMA job) that is sent to a channel.
 */
static void dma_tasklet(unsigned long data)
{
	struct coh901318_chan *cohc = (struct coh901318_chan *) data;
	struct coh901318_desc *cohd_fin;
	unsigned long flags;
	dma_async_tx_callback callback;
	void *callback_param;

	dev_vdbg(COHC_2_DEV(cohc), "[%s] chan_id %d"
		 " nbr_active_done %ld\n", __func__,
		 cohc->id, cohc->nbr_active_done);

	spin_lock_irqsave(&cohc->lock, flags);

	/* get first active descriptor entry from list */
	cohd_fin = coh901318_first_active_get(cohc);

	BUG_ON(cohd_fin->pending_irqs == 0);

	if (cohd_fin == NULL)
		goto err;

	cohd_fin->pending_irqs--;
	cohc->completed = cohd_fin->desc.cookie;

	if (cohc->nbr_active_done == 0)
		return;

	if (!cohd_fin->pending_irqs) {
		/* release the lli allocation*/
		coh901318_lli_free(&cohc->base->pool, &cohd_fin->data);
	}

	dev_vdbg(COHC_2_DEV(cohc), "[%s] chan_id %d pending_irqs %d"
		 " nbr_active_done %ld\n", __func__,
		 cohc->id, cohc->pending_irqs, cohc->nbr_active_done);

	/* callback to client */
	callback = cohd_fin->desc.callback;
	callback_param = cohd_fin->desc.callback_param;

	if (!cohd_fin->pending_irqs) {
		coh901318_desc_remove(cohd_fin);

		/* return desc to free-list */
		coh901318_desc_free(cohc, cohd_fin);
	}

	/*
	 * If another interrupt fired while the tasklet was scheduling,
	 * we don't get called twice, so we have this number of active
	 * counter that keep track of the number of IRQs expected to
	 * be handled for this channel. If there happen to be more than
	 * one IRQ to be ack:ed, we simply schedule this tasklet again.
	 */
	if (cohc->nbr_active_done)
		cohc->nbr_active_done--;

	if (cohc->nbr_active_done) {
		dev_dbg(COHC_2_DEV(cohc), "scheduling tasklet again, new IRQs "
			"came in while we were scheduling this tasklet\n");
		if (cohc_chan_conf(cohc)->priority_high)
			tasklet_hi_schedule(&cohc->tasklet);
		else
			tasklet_schedule(&cohc->tasklet);
	}
	spin_unlock_irqrestore(&cohc->lock, flags);

	if (callback)
		callback(callback_param);

	return;

 err:
	spin_unlock_irqrestore(&cohc->lock, flags);
	dev_err(COHC_2_DEV(cohc), "[%s] No active dma desc\n", __func__);
}


/* called from interrupt context */
static void dma_tc_handle(struct coh901318_chan *cohc)
{
	BUG_ON(!cohc->allocated && (list_empty(&cohc->active) ||
				    list_empty(&cohc->queue)));

	if (!cohc->allocated)
		return;

	BUG_ON(cohc->pending_irqs == 0);

	cohc->pending_irqs--;
	cohc->nbr_active_done++;

	if (cohc->pending_irqs == 0 && coh901318_queue_start(cohc) == NULL)
		cohc->busy = 0;

	BUG_ON(list_empty(&cohc->active));

	if (cohc_chan_conf(cohc)->priority_high)
		tasklet_hi_schedule(&cohc->tasklet);
	else
		tasklet_schedule(&cohc->tasklet);
}


static irqreturn_t dma_irq_handler(int irq, void *dev_id)
{
	u32 status1;
	u32 status2;
	int i;
	int ch;
	struct coh901318_base *base  = dev_id;
	struct coh901318_chan *cohc;
	void __iomem *virtbase = base->virtbase;

	status1 = readl(virtbase + COH901318_INT_STATUS1);
	status2 = readl(virtbase + COH901318_INT_STATUS2);

	if (unlikely(status1 == 0 && status2 == 0)) {
		dev_warn(base->dev, "spurious DMA IRQ from no channel!\n");
		return IRQ_HANDLED;
	}

	/* TODO: consider handle IRQ in tasklet here to
	 *       minimize interrupt latency */

	/* Check the first 32 DMA channels for IRQ */
	while (status1) {
		/* Find first bit set, return as a number. */
		i = ffs(status1) - 1;
		ch = i;

		cohc = &base->chans[ch];
		spin_lock(&cohc->lock);

		/* Mask off this bit */
		status1 &= ~(1 << i);
		/* Check the individual channel bits */
		if (test_bit(i, virtbase + COH901318_BE_INT_STATUS1)) {
			dev_crit(COHC_2_DEV(cohc),
				 "DMA bus error on channel %d!\n", ch);
			BUG_ON(1);
			/* Clear BE interrupt */
			__set_bit(i, virtbase + COH901318_BE_INT_CLEAR1);
		} else {
			/* Caused by TC, really? */
			if (unlikely(!test_bit(i, virtbase +
					       COH901318_TC_INT_STATUS1))) {
				dev_warn(COHC_2_DEV(cohc),
					 "ignoring interrupt not caused by terminal count on channel %d\n", ch);
				/* Clear TC interrupt */
				BUG_ON(1);
				__set_bit(i, virtbase + COH901318_TC_INT_CLEAR1);
			} else {
				/* Enable powersave if transfer has finished */
				if (!(readl(virtbase + COH901318_CX_STAT +
					    COH901318_CX_STAT_SPACING*ch) &
				      COH901318_CX_STAT_ENABLED)) {
					enable_powersave(cohc);
				}

				/* Must clear TC interrupt before calling
				 * dma_tc_handle
				 * in case tc_handle initate a new dma job
				 */
				__set_bit(i, virtbase + COH901318_TC_INT_CLEAR1);

				dma_tc_handle(cohc);
			}
		}
		spin_unlock(&cohc->lock);
	}

	/* Check the remaining 32 DMA channels for IRQ */
	while (status2) {
		/* Find first bit set, return as a number. */
		i = ffs(status2) - 1;
		ch = i + 32;
		cohc = &base->chans[ch];
		spin_lock(&cohc->lock);

		/* Mask off this bit */
		status2 &= ~(1 << i);
		/* Check the individual channel bits */
		if (test_bit(i, virtbase + COH901318_BE_INT_STATUS2)) {
			dev_crit(COHC_2_DEV(cohc),
				 "DMA bus error on channel %d!\n", ch);
			/* Clear BE interrupt */
			BUG_ON(1);
			__set_bit(i, virtbase + COH901318_BE_INT_CLEAR2);
		} else {
			/* Caused by TC, really? */
			if (unlikely(!test_bit(i, virtbase +
					       COH901318_TC_INT_STATUS2))) {
				dev_warn(COHC_2_DEV(cohc),
					 "ignoring interrupt not caused by terminal count on channel %d\n", ch);
				/* Clear TC interrupt */
				__set_bit(i, virtbase + COH901318_TC_INT_CLEAR2);
				BUG_ON(1);
			} else {
				/* Enable powersave if transfer has finished */
				if (!(readl(virtbase + COH901318_CX_STAT +
					    COH901318_CX_STAT_SPACING*ch) &
				      COH901318_CX_STAT_ENABLED)) {
					enable_powersave(cohc);
				}
				/* Must clear TC interrupt before calling
				 * dma_tc_handle
				 * in case tc_handle initate a new dma job
				 */
				__set_bit(i, virtbase + COH901318_TC_INT_CLEAR2);

				dma_tc_handle(cohc);
			}
		}
		spin_unlock(&cohc->lock);
	}

	return IRQ_HANDLED;
}

static int coh901318_alloc_chan_resources(struct dma_chan *chan)
{
	struct coh901318_chan	*cohc = to_coh901318_chan(chan);

	dev_vdbg(COHC_2_DEV(cohc), "[%s] DMA channel %d\n",
		 __func__, cohc->id);

	if (chan->client_count > 1)
		return -EBUSY;

	coh901318_config(cohc, NULL);

	cohc->allocated = 1;
	cohc->completed = chan->cookie = 1;

	return 1;
}

static void
coh901318_free_chan_resources(struct dma_chan *chan)
{
	struct coh901318_chan	*cohc = to_coh901318_chan(chan);
	int channel = cohc->id;
	unsigned long flags;

	spin_lock_irqsave(&cohc->lock, flags);

	/* Disable HW */
	writel(0x00000000U, cohc->base->virtbase + COH901318_CX_CFG +
	       COH901318_CX_CFG_SPACING*channel);
	writel(0x00000000U, cohc->base->virtbase + COH901318_CX_CTRL +
	       COH901318_CX_CTRL_SPACING*channel);

	cohc->allocated = 0;

	spin_unlock_irqrestore(&cohc->lock, flags);

	chan->device->device_terminate_all(chan);
}


static dma_cookie_t
coh901318_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct coh901318_desc *cohd = container_of(tx, struct coh901318_desc,
						   desc);
	struct coh901318_chan *cohc = to_coh901318_chan(tx->chan);
	unsigned long flags;

	spin_lock_irqsave(&cohc->lock, flags);

	tx->cookie = coh901318_assign_cookie(cohc, cohd);

	coh901318_desc_queue(cohc, cohd);

	spin_unlock_irqrestore(&cohc->lock, flags);

	return tx->cookie;
}

static struct dma_async_tx_descriptor *
coh901318_prep_memcpy(struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		      size_t size, unsigned long flags)
{
	struct coh901318_lli *data;
	struct coh901318_desc *cohd;
	unsigned long flg;
	struct coh901318_chan *cohc = to_coh901318_chan(chan);
	int lli_len;
	u32 ctrl_last = cohc_chan_param(cohc)->ctrl_lli_last;

	spin_lock_irqsave(&cohc->lock, flg);

	dev_vdbg(COHC_2_DEV(cohc),
		 "[%s] channel %d src 0x%x dest 0x%x size %d\n",
		 __func__, cohc->id, src, dest, size);

	if (flags & DMA_PREP_INTERRUPT)
		/* Trigger interrupt after last lli */
		ctrl_last |= COH901318_CX_CTRL_TC_IRQ_ENABLE;

	lli_len = size >> MAX_DMA_PACKET_SIZE_SHIFT;
	if ((lli_len << MAX_DMA_PACKET_SIZE_SHIFT) < size)
		lli_len++;

	data = coh901318_lli_alloc(&cohc->base->pool, lli_len);

	if (data == NULL)
		goto err;

	cohd = coh901318_desc_get(cohc);
	cohd->sg = NULL;
	cohd->sg_len = 0;
	cohd->data = data;

	cohd->pending_irqs =
		coh901318_lli_fill_memcpy(
				&cohc->base->pool, data, src, size, dest,
				cohc_chan_param(cohc)->ctrl_lli_chained,
				ctrl_last);
	cohd->flags = flags;

	COH_DBG(coh901318_list_print(cohc, data));

	dma_async_tx_descriptor_init(&cohd->desc, chan);

	cohd->desc.tx_submit = coh901318_tx_submit;

	spin_unlock_irqrestore(&cohc->lock, flg);

	return &cohd->desc;
 err:
	spin_unlock_irqrestore(&cohc->lock, flg);
	return NULL;
}

static struct dma_async_tx_descriptor *
coh901318_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
			unsigned int sg_len, enum dma_data_direction direction,
			unsigned long flags)
{
	struct coh901318_chan *cohc = to_coh901318_chan(chan);
	struct coh901318_lli *data;
	struct coh901318_desc *cohd;
	struct scatterlist *sg;
	int len = 0;
	int size;
	int i;
	u32 ctrl_chained = cohc_chan_param(cohc)->ctrl_lli_chained;
	u32 ctrl = cohc_chan_param(cohc)->ctrl_lli;
	u32 ctrl_last = cohc_chan_param(cohc)->ctrl_lli_last;
	unsigned long flg;

	if (!sgl)
		goto out;
	if (sgl->length == 0)
		goto out;

	spin_lock_irqsave(&cohc->lock, flg);

	dev_vdbg(COHC_2_DEV(cohc), "[%s] sg_len %d dir %d\n",
		 __func__, sg_len, direction);

	if (flags & DMA_PREP_INTERRUPT)
		/* Trigger interrupt after last lli */
		ctrl_last |= COH901318_CX_CTRL_TC_IRQ_ENABLE;

	cohd = coh901318_desc_get(cohc);
	cohd->sg = NULL;
	cohd->sg_len = 0;
	cohd->dir = direction;

	if (direction == DMA_TO_DEVICE) {
		u32 tx_flags = COH901318_CX_CTRL_PRDD_SOURCE |
			COH901318_CX_CTRL_SRC_ADDR_INC_ENABLE;

		ctrl_chained |= tx_flags;
		ctrl_last |= tx_flags;
		ctrl |= tx_flags;
	} else if (direction == DMA_FROM_DEVICE) {
		u32 rx_flags = COH901318_CX_CTRL_PRDD_DEST |
			COH901318_CX_CTRL_DST_ADDR_INC_ENABLE;

		ctrl_chained |= rx_flags;
		ctrl_last |= rx_flags;
		ctrl |= rx_flags;
	} else
		goto err_direction;

	dma_async_tx_descriptor_init(&cohd->desc, chan);

	cohd->desc.tx_submit = coh901318_tx_submit;


	/* The dma only supports transmitting packages up to
	 * MAX_DMA_PACKET_SIZE. Calculate to total number of
	 * dma elemts required to send the entire sg list
	 */
	for_each_sg(sgl, sg, sg_len, i) {
		unsigned int factor;
		size = sg_dma_len(sg);

		if (size <= MAX_DMA_PACKET_SIZE) {
			len++;
			continue;
		}

		factor = size >> MAX_DMA_PACKET_SIZE_SHIFT;
		if ((factor << MAX_DMA_PACKET_SIZE_SHIFT) < size)
			factor++;

		len += factor;
	}

	pr_debug("Allocate %d lli:s for this transfer\n", len);
	data = coh901318_lli_alloc(&cohc->base->pool, len);

	if (data == NULL)
		goto err_dma_alloc;

	/* initiate allocated data list */
	cohd->pending_irqs =
		coh901318_lli_fill_sg(&cohc->base->pool, data, sgl, sg_len,
				      cohc_dev_addr(cohc),
				      ctrl_chained,
				      ctrl,
				      ctrl_last,
				      direction, COH901318_CX_CTRL_TC_IRQ_ENABLE);
	cohd->data = data;

	cohd->flags = flags;

	COH_DBG(coh901318_list_print(cohc, data));

	spin_unlock_irqrestore(&cohc->lock, flg);

	return &cohd->desc;
 err_dma_alloc:
 err_direction:
	coh901318_desc_remove(cohd);
	coh901318_desc_free(cohc, cohd);
	spin_unlock_irqrestore(&cohc->lock, flg);
 out:
	return NULL;
}

static enum dma_status
coh901318_is_tx_complete(struct dma_chan *chan,
			 dma_cookie_t cookie, dma_cookie_t *done,
			 dma_cookie_t *used)
{
	struct coh901318_chan *cohc = to_coh901318_chan(chan);
	dma_cookie_t last_used;
	dma_cookie_t last_complete;
	int ret;

	last_complete = cohc->completed;
	last_used = chan->cookie;

	ret = dma_async_is_complete(cookie, last_complete, last_used);

	if (done)
		*done = last_complete;
	if (used)
		*used = last_used;

	return ret;
}

static void
coh901318_issue_pending(struct dma_chan *chan)
{
	struct coh901318_chan *cohc = to_coh901318_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&cohc->lock, flags);

	/* Busy means that pending jobs are already being processed */
	if (!cohc->busy)
		coh901318_queue_start(cohc);

	spin_unlock_irqrestore(&cohc->lock, flags);
}

static void
coh901318_terminate_all(struct dma_chan *chan)
{
	unsigned long flags;
	struct coh901318_chan *cohc = to_coh901318_chan(chan);
	struct coh901318_desc *cohd;
	void __iomem *virtbase = cohc->base->virtbase;

	coh901318_stop(chan);

	spin_lock_irqsave(&cohc->lock, flags);

	/* Clear any pending BE or TC interrupt */
	if (cohc->id < 32) {
		writel(1 << cohc->id, virtbase + COH901318_BE_INT_CLEAR1);
		writel(1 << cohc->id, virtbase + COH901318_TC_INT_CLEAR1);
	} else {
		writel(1 << (cohc->id - 32), virtbase +
		       COH901318_BE_INT_CLEAR2);
		writel(1 << (cohc->id - 32), virtbase +
		       COH901318_TC_INT_CLEAR2);
	}

	enable_powersave(cohc);

	while ((cohd = coh901318_first_active_get(cohc))) {
		/* release the lli allocation*/
		coh901318_lli_free(&cohc->base->pool, &cohd->data);

		/* return desc to free-list */
		coh901318_desc_remove(cohd);
		coh901318_desc_free(cohc, cohd);
	}

	while ((cohd = coh901318_first_queued(cohc))) {
		/* release the lli allocation*/
		coh901318_lli_free(&cohc->base->pool, &cohd->data);

		/* return desc to free-list */
		coh901318_desc_remove(cohd);
		coh901318_desc_free(cohc, cohd);
	}


	cohc->nbr_active_done = 0;
	cohc->busy = 0;
	cohc->pending_irqs = 0;

	spin_unlock_irqrestore(&cohc->lock, flags);
}
void coh901318_base_init(struct dma_device *dma, const int *pick_chans,
			 struct coh901318_base *base)
{
	int chans_i;
	int i = 0;
	struct coh901318_chan *cohc;

	INIT_LIST_HEAD(&dma->channels);

	for (chans_i = 0; pick_chans[chans_i] != -1; chans_i += 2) {
		for (i = pick_chans[chans_i]; i <= pick_chans[chans_i+1]; i++) {
			cohc = &base->chans[i];

			cohc->base = base;
			cohc->chan.device = dma;
			cohc->id = i;

			/* TODO: do we really need this lock if only one
			 * client is connected to each channel?
			 */

			spin_lock_init(&cohc->lock);

			cohc->pending_irqs = 0;
			cohc->nbr_active_done = 0;
			cohc->busy = 0;
			INIT_LIST_HEAD(&cohc->free);
			INIT_LIST_HEAD(&cohc->active);
			INIT_LIST_HEAD(&cohc->queue);

			tasklet_init(&cohc->tasklet, dma_tasklet,
				     (unsigned long) cohc);

			list_add_tail(&cohc->chan.device_node,
				      &dma->channels);
		}
	}
}

static int __init coh901318_probe(struct platform_device *pdev)
{
	int err = 0;
	struct coh901318_platform *pdata;
	struct coh901318_base *base;
	int irq;
	struct resource *io;

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!io)
		goto err_get_resource;

	/* Map DMA controller registers to virtual memory */
	if (request_mem_region(io->start,
			       resource_size(io),
			       pdev->dev.driver->name) == NULL) {
		err = -EBUSY;
		goto err_request_mem;
	}

	pdata = pdev->dev.platform_data;
	if (!pdata)
		goto err_no_platformdata;

	base = kmalloc(ALIGN(sizeof(struct coh901318_base), 4) +
		       pdata->max_channels *
		       sizeof(struct coh901318_chan),
		       GFP_KERNEL);
	if (!base)
		goto err_alloc_coh_dma_channels;

	base->chans = ((void *)base) + ALIGN(sizeof(struct coh901318_base), 4);

	base->virtbase = ioremap(io->start, resource_size(io));
	if (!base->virtbase) {
		err = -ENOMEM;
		goto err_no_ioremap;
	}

	base->dev = &pdev->dev;
	base->platform = pdata;
	spin_lock_init(&base->pm.lock);
	base->pm.started_channels = 0;

	COH901318_DEBUGFS_ASSIGN(debugfs_dma_base, base);

	platform_set_drvdata(pdev, base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		goto err_no_irq;

	err = request_irq(irq, dma_irq_handler, IRQF_DISABLED,
			  "coh901318", base);
	if (err) {
		dev_crit(&pdev->dev,
			 "Cannot allocate IRQ for DMA controller!\n");
		goto err_request_irq;
	}

	err = coh901318_pool_create(&base->pool, &pdev->dev,
				    sizeof(struct coh901318_lli),
				    32);
	if (err)
		goto err_pool_create;

	/* init channels for device transfers */
	coh901318_base_init(&base->dma_slave,  base->platform->chans_slave,
			    base);

	dma_cap_zero(base->dma_slave.cap_mask);
	dma_cap_set(DMA_SLAVE, base->dma_slave.cap_mask);

	base->dma_slave.device_alloc_chan_resources = coh901318_alloc_chan_resources;
	base->dma_slave.device_free_chan_resources = coh901318_free_chan_resources;
	base->dma_slave.device_prep_slave_sg = coh901318_prep_slave_sg;
	base->dma_slave.device_is_tx_complete = coh901318_is_tx_complete;
	base->dma_slave.device_issue_pending = coh901318_issue_pending;
	base->dma_slave.device_terminate_all = coh901318_terminate_all;
	base->dma_slave.dev = &pdev->dev;

	err = dma_async_device_register(&base->dma_slave);

	if (err)
		goto err_register_slave;

	/* init channels for memcpy */
	coh901318_base_init(&base->dma_memcpy, base->platform->chans_memcpy,
			    base);

	dma_cap_zero(base->dma_memcpy.cap_mask);
	dma_cap_set(DMA_MEMCPY, base->dma_memcpy.cap_mask);

	base->dma_memcpy.device_alloc_chan_resources = coh901318_alloc_chan_resources;
	base->dma_memcpy.device_free_chan_resources = coh901318_free_chan_resources;
	base->dma_memcpy.device_prep_dma_memcpy = coh901318_prep_memcpy;
	base->dma_memcpy.device_is_tx_complete = coh901318_is_tx_complete;
	base->dma_memcpy.device_issue_pending = coh901318_issue_pending;
	base->dma_memcpy.device_terminate_all = coh901318_terminate_all;
	base->dma_memcpy.dev = &pdev->dev;
	err = dma_async_device_register(&base->dma_memcpy);

	if (err)
		goto err_register_memcpy;

	dev_info(&pdev->dev, "Initialized COH901318 DMA on virtual base 0x%08x\n",
		(u32) base->virtbase);

	return err;

 err_register_memcpy:
	dma_async_device_unregister(&base->dma_slave);
 err_register_slave:
	coh901318_pool_destroy(&base->pool);
 err_pool_create:
	free_irq(platform_get_irq(pdev, 0), base);
 err_request_irq:
 err_no_irq:
	iounmap(base->virtbase);
 err_no_ioremap:
	kfree(base);
 err_alloc_coh_dma_channels:
 err_no_platformdata:
	release_mem_region(pdev->resource->start,
			   resource_size(pdev->resource));
 err_request_mem:
 err_get_resource:
	return err;
}

static int __exit coh901318_remove(struct platform_device *pdev)
{
	struct coh901318_base *base = platform_get_drvdata(pdev);

	dma_async_device_unregister(&base->dma_memcpy);
	dma_async_device_unregister(&base->dma_slave);
	coh901318_pool_destroy(&base->pool);
	free_irq(platform_get_irq(pdev, 0), base);
	iounmap(base->virtbase);
	kfree(base);
	release_mem_region(pdev->resource->start,
			   resource_size(pdev->resource));
	return 0;
}


static struct platform_driver coh901318_driver = {
	.remove = __exit_p(coh901318_remove),
	.driver = {
		.name	= "coh901318",
	},
};

int __init coh901318_init(void)
{
	return platform_driver_probe(&coh901318_driver, coh901318_probe);
}
subsys_initcall(coh901318_init);

void __exit coh901318_exit(void)
{
	platform_driver_unregister(&coh901318_driver);
}
module_exit(coh901318_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Per Friden");
