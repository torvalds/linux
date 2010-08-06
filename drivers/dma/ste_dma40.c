/*
 * driver/dma/ste_dma40.c
 *
 * Copyright (C) ST-Ericsson 2007-2010
 * License terms: GNU General Public License (GPL) version 2
 * Author: Per Friden <per.friden@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include <plat/ste_dma40.h>

#include "ste_dma40_ll.h"

#define D40_NAME "dma40"

#define D40_PHY_CHAN -1

/* For masking out/in 2 bit channel positions */
#define D40_CHAN_POS(chan)  (2 * (chan / 2))
#define D40_CHAN_POS_MASK(chan) (0x3 << D40_CHAN_POS(chan))

/* Maximum iterations taken before giving up suspending a channel */
#define D40_SUSPEND_MAX_IT 500

#define D40_ALLOC_FREE		(1 << 31)
#define D40_ALLOC_PHY		(1 << 30)
#define D40_ALLOC_LOG_FREE	0

/* The number of free d40_desc to keep in memory before starting
 * to kfree() them */
#define D40_DESC_CACHE_SIZE 50

/* Hardware designer of the block */
#define D40_PERIPHID2_DESIGNER 0x8

/**
 * enum 40_command - The different commands and/or statuses.
 *
 * @D40_DMA_STOP: DMA channel command STOP or status STOPPED,
 * @D40_DMA_RUN: The DMA channel is RUNNING of the command RUN.
 * @D40_DMA_SUSPEND_REQ: Request the DMA to SUSPEND as soon as possible.
 * @D40_DMA_SUSPENDED: The DMA channel is SUSPENDED.
 */
enum d40_command {
	D40_DMA_STOP		= 0,
	D40_DMA_RUN		= 1,
	D40_DMA_SUSPEND_REQ	= 2,
	D40_DMA_SUSPENDED	= 3
};

/**
 * struct d40_lli_pool - Structure for keeping LLIs in memory
 *
 * @base: Pointer to memory area when the pre_alloc_lli's are not large
 * enough, IE bigger than the most common case, 1 dst and 1 src. NULL if
 * pre_alloc_lli is used.
 * @size: The size in bytes of the memory at base or the size of pre_alloc_lli.
 * @pre_alloc_lli: Pre allocated area for the most common case of transfers,
 * one buffer to one buffer.
 */
struct d40_lli_pool {
	void	*base;
	int	size;
	/* Space for dst and src, plus an extra for padding */
	u8	pre_alloc_lli[3 * sizeof(struct d40_phy_lli)];
};

/**
 * struct d40_desc - A descriptor is one DMA job.
 *
 * @lli_phy: LLI settings for physical channel. Both src and dst=
 * points into the lli_pool, to base if lli_len > 1 or to pre_alloc_lli if
 * lli_len equals one.
 * @lli_log: Same as above but for logical channels.
 * @lli_pool: The pool with two entries pre-allocated.
 * @lli_len: Number of LLI's in lli_pool
 * @lli_tcount: Number of LLIs processed in the transfer. When equals lli_len
 * then this transfer job is done.
 * @txd: DMA engine struct. Used for among other things for communication
 * during a transfer.
 * @node: List entry.
 * @dir: The transfer direction of this job.
 * @is_in_client_list: true if the client owns this descriptor.
 *
 * This descriptor is used for both logical and physical transfers.
 */

struct d40_desc {
	/* LLI physical */
	struct d40_phy_lli_bidir	 lli_phy;
	/* LLI logical */
	struct d40_log_lli_bidir	 lli_log;

	struct d40_lli_pool		 lli_pool;
	u32				 lli_len;
	u32				 lli_tcount;

	struct dma_async_tx_descriptor	 txd;
	struct list_head		 node;

	enum dma_data_direction		 dir;
	bool				 is_in_client_list;
};

/**
 * struct d40_lcla_pool - LCLA pool settings and data.
 *
 * @base: The virtual address of LCLA.
 * @phy: Physical base address of LCLA.
 * @base_size: size of lcla.
 * @lock: Lock to protect the content in this struct.
 * @alloc_map: Mapping between physical channel and LCLA entries.
 * @num_blocks: The number of entries of alloc_map. Equals to the
 * number of physical channels.
 */
struct d40_lcla_pool {
	void		*base;
	dma_addr_t	 phy;
	resource_size_t  base_size;
	spinlock_t	 lock;
	u32		*alloc_map;
	int		 num_blocks;
};

/**
 * struct d40_phy_res - struct for handling eventlines mapped to physical
 * channels.
 *
 * @lock: A lock protection this entity.
 * @num: The physical channel number of this entity.
 * @allocated_src: Bit mapped to show which src event line's are mapped to
 * this physical channel. Can also be free or physically allocated.
 * @allocated_dst: Same as for src but is dst.
 * allocated_dst and allocated_src uses the D40_ALLOC* defines as well as
 * event line number. Both allocated_src and allocated_dst can not be
 * allocated to a physical channel, since the interrupt handler has then
 * no way of figure out which one the interrupt belongs to.
 */
struct d40_phy_res {
	spinlock_t lock;
	int	   num;
	u32	   allocated_src;
	u32	   allocated_dst;
};

struct d40_base;

/**
 * struct d40_chan - Struct that describes a channel.
 *
 * @lock: A spinlock to protect this struct.
 * @log_num: The logical number, if any of this channel.
 * @completed: Starts with 1, after first interrupt it is set to dma engine's
 * current cookie.
 * @pending_tx: The number of pending transfers. Used between interrupt handler
 * and tasklet.
 * @busy: Set to true when transfer is ongoing on this channel.
 * @phy_chan: Pointer to physical channel which this instance runs on.
 * @chan: DMA engine handle.
 * @tasklet: Tasklet that gets scheduled from interrupt context to complete a
 * transfer and call client callback.
 * @client: Cliented owned descriptor list.
 * @active: Active descriptor.
 * @queue: Queued jobs.
 * @free: List of free descripts, ready to be reused.
 * @free_len: Number of descriptors in the free list.
 * @dma_cfg: The client configuration of this dma channel.
 * @base: Pointer to the device instance struct.
 * @src_def_cfg: Default cfg register setting for src.
 * @dst_def_cfg: Default cfg register setting for dst.
 * @log_def: Default logical channel settings.
 * @lcla: Space for one dst src pair for logical channel transfers.
 * @lcpa: Pointer to dst and src lcpa settings.
 *
 * This struct can either "be" a logical or a physical channel.
 */
struct d40_chan {
	spinlock_t			 lock;
	int				 log_num;
	/* ID of the most recent completed transfer */
	int				 completed;
	int				 pending_tx;
	bool				 busy;
	struct d40_phy_res		*phy_chan;
	struct dma_chan			 chan;
	struct tasklet_struct		 tasklet;
	struct list_head		 client;
	struct list_head		 active;
	struct list_head		 queue;
	struct list_head		 free;
	int				 free_len;
	struct stedma40_chan_cfg	 dma_cfg;
	struct d40_base			*base;
	/* Default register configurations */
	u32				 src_def_cfg;
	u32				 dst_def_cfg;
	struct d40_def_lcsp		 log_def;
	struct d40_lcla_elem		 lcla;
	struct d40_log_lli_full		*lcpa;
};

/**
 * struct d40_base - The big global struct, one for each probe'd instance.
 *
 * @interrupt_lock: Lock used to make sure one interrupt is handle a time.
 * @execmd_lock: Lock for execute command usage since several channels share
 * the same physical register.
 * @dev: The device structure.
 * @virtbase: The virtual base address of the DMA's register.
 * @clk: Pointer to the DMA clock structure.
 * @phy_start: Physical memory start of the DMA registers.
 * @phy_size: Size of the DMA register map.
 * @irq: The IRQ number.
 * @num_phy_chans: The number of physical channels. Read from HW. This
 * is the number of available channels for this driver, not counting "Secure
 * mode" allocated physical channels.
 * @num_log_chans: The number of logical channels. Calculated from
 * num_phy_chans.
 * @dma_both: dma_device channels that can do both memcpy and slave transfers.
 * @dma_slave: dma_device channels that can do only do slave transfers.
 * @dma_memcpy: dma_device channels that can do only do memcpy transfers.
 * @phy_chans: Room for all possible physical channels in system.
 * @log_chans: Room for all possible logical channels in system.
 * @lookup_log_chans: Used to map interrupt number to logical channel. Points
 * to log_chans entries.
 * @lookup_phy_chans: Used to map interrupt number to physical channel. Points
 * to phy_chans entries.
 * @plat_data: Pointer to provided platform_data which is the driver
 * configuration.
 * @phy_res: Vector containing all physical channels.
 * @lcla_pool: lcla pool settings and data.
 * @lcpa_base: The virtual mapped address of LCPA.
 * @phy_lcpa: The physical address of the LCPA.
 * @lcpa_size: The size of the LCPA area.
 */
struct d40_base {
	spinlock_t			 interrupt_lock;
	spinlock_t			 execmd_lock;
	struct device			 *dev;
	void __iomem			 *virtbase;
	struct clk			 *clk;
	phys_addr_t			  phy_start;
	resource_size_t			  phy_size;
	int				  irq;
	int				  num_phy_chans;
	int				  num_log_chans;
	struct dma_device		  dma_both;
	struct dma_device		  dma_slave;
	struct dma_device		  dma_memcpy;
	struct d40_chan			 *phy_chans;
	struct d40_chan			 *log_chans;
	struct d40_chan			**lookup_log_chans;
	struct d40_chan			**lookup_phy_chans;
	struct stedma40_platform_data	 *plat_data;
	/* Physical half channels */
	struct d40_phy_res		 *phy_res;
	struct d40_lcla_pool		  lcla_pool;
	void				 *lcpa_base;
	dma_addr_t			  phy_lcpa;
	resource_size_t			  lcpa_size;
};

/**
 * struct d40_interrupt_lookup - lookup table for interrupt handler
 *
 * @src: Interrupt mask register.
 * @clr: Interrupt clear register.
 * @is_error: true if this is an error interrupt.
 * @offset: start delta in the lookup_log_chans in d40_base. If equals to
 * D40_PHY_CHAN, the lookup_phy_chans shall be used instead.
 */
struct d40_interrupt_lookup {
	u32 src;
	u32 clr;
	bool is_error;
	int offset;
};

/**
 * struct d40_reg_val - simple lookup struct
 *
 * @reg: The register.
 * @val: The value that belongs to the register in reg.
 */
struct d40_reg_val {
	unsigned int reg;
	unsigned int val;
};

static int d40_pool_lli_alloc(struct d40_desc *d40d,
			      int lli_len, bool is_log)
{
	u32 align;
	void *base;

	if (is_log)
		align = sizeof(struct d40_log_lli);
	else
		align = sizeof(struct d40_phy_lli);

	if (lli_len == 1) {
		base = d40d->lli_pool.pre_alloc_lli;
		d40d->lli_pool.size = sizeof(d40d->lli_pool.pre_alloc_lli);
		d40d->lli_pool.base = NULL;
	} else {
		d40d->lli_pool.size = ALIGN(lli_len * 2 * align, align);

		base = kmalloc(d40d->lli_pool.size + align, GFP_NOWAIT);
		d40d->lli_pool.base = base;

		if (d40d->lli_pool.base == NULL)
			return -ENOMEM;
	}

	if (is_log) {
		d40d->lli_log.src = PTR_ALIGN((struct d40_log_lli *) base,
					      align);
		d40d->lli_log.dst = PTR_ALIGN(d40d->lli_log.src + lli_len,
					      align);
	} else {
		d40d->lli_phy.src = PTR_ALIGN((struct d40_phy_lli *)base,
					      align);
		d40d->lli_phy.dst = PTR_ALIGN(d40d->lli_phy.src + lli_len,
					      align);

		d40d->lli_phy.src_addr = virt_to_phys(d40d->lli_phy.src);
		d40d->lli_phy.dst_addr = virt_to_phys(d40d->lli_phy.dst);
	}

	return 0;
}

static void d40_pool_lli_free(struct d40_desc *d40d)
{
	kfree(d40d->lli_pool.base);
	d40d->lli_pool.base = NULL;
	d40d->lli_pool.size = 0;
	d40d->lli_log.src = NULL;
	d40d->lli_log.dst = NULL;
	d40d->lli_phy.src = NULL;
	d40d->lli_phy.dst = NULL;
	d40d->lli_phy.src_addr = 0;
	d40d->lli_phy.dst_addr = 0;
}

static dma_cookie_t d40_assign_cookie(struct d40_chan *d40c,
				      struct d40_desc *desc)
{
	dma_cookie_t cookie = d40c->chan.cookie;

	if (++cookie < 0)
		cookie = 1;

	d40c->chan.cookie = cookie;
	desc->txd.cookie = cookie;

	return cookie;
}

static void d40_desc_reset(struct d40_desc *d40d)
{
	d40d->lli_tcount = 0;
}

static void d40_desc_remove(struct d40_desc *d40d)
{
	list_del(&d40d->node);
}

static struct d40_desc *d40_desc_get(struct d40_chan *d40c)
{
	struct d40_desc *desc;
	struct d40_desc *d;
	struct d40_desc *_d;

	if (!list_empty(&d40c->client)) {
		list_for_each_entry_safe(d, _d, &d40c->client, node)
			if (async_tx_test_ack(&d->txd)) {
				d40_pool_lli_free(d);
				d40_desc_remove(d);
				desc = d;
				goto out;
			}
	}

	if (list_empty(&d40c->free)) {
		/* Alloc new desc because we're out of used ones */
		desc = kzalloc(sizeof(struct d40_desc), GFP_NOWAIT);
		if (desc == NULL)
			goto out;
		INIT_LIST_HEAD(&desc->node);
	} else {
		/* Reuse an old desc. */
		desc = list_first_entry(&d40c->free,
					struct d40_desc,
					node);
		list_del(&desc->node);
		d40c->free_len--;
	}
out:
	return desc;
}

static void d40_desc_free(struct d40_chan *d40c, struct d40_desc *d40d)
{
	if (d40c->free_len < D40_DESC_CACHE_SIZE) {
		list_add_tail(&d40d->node, &d40c->free);
		d40c->free_len++;
	} else
		kfree(d40d);
}

static void d40_desc_submit(struct d40_chan *d40c, struct d40_desc *desc)
{
	list_add_tail(&desc->node, &d40c->active);
}

static struct d40_desc *d40_first_active_get(struct d40_chan *d40c)
{
	struct d40_desc *d;

	if (list_empty(&d40c->active))
		return NULL;

	d = list_first_entry(&d40c->active,
			     struct d40_desc,
			     node);
	return d;
}

static void d40_desc_queue(struct d40_chan *d40c, struct d40_desc *desc)
{
	list_add_tail(&desc->node, &d40c->queue);
}

static struct d40_desc *d40_first_queued(struct d40_chan *d40c)
{
	struct d40_desc *d;

	if (list_empty(&d40c->queue))
		return NULL;

	d = list_first_entry(&d40c->queue,
			     struct d40_desc,
			     node);
	return d;
}

/* Support functions for logical channels */

static int d40_lcla_id_get(struct d40_chan *d40c,
			   struct d40_lcla_pool *pool)
{
	int src_id = 0;
	int dst_id = 0;
	struct d40_log_lli *lcla_lidx_base =
		pool->base + d40c->phy_chan->num * 1024;
	int i;
	int lli_per_log = d40c->base->plat_data->llis_per_log;

	if (d40c->lcla.src_id >= 0 && d40c->lcla.dst_id >= 0)
		return 0;

	if (pool->num_blocks > 32)
		return -EINVAL;

	spin_lock(&pool->lock);

	for (i = 0; i < pool->num_blocks; i++) {
		if (!(pool->alloc_map[d40c->phy_chan->num] & (0x1 << i))) {
			pool->alloc_map[d40c->phy_chan->num] |= (0x1 << i);
			break;
		}
	}
	src_id = i;
	if (src_id >= pool->num_blocks)
		goto err;

	for (; i < pool->num_blocks; i++) {
		if (!(pool->alloc_map[d40c->phy_chan->num] & (0x1 << i))) {
			pool->alloc_map[d40c->phy_chan->num] |= (0x1 << i);
			break;
		}
	}

	dst_id = i;
	if (dst_id == src_id)
		goto err;

	d40c->lcla.src_id = src_id;
	d40c->lcla.dst_id = dst_id;
	d40c->lcla.dst = lcla_lidx_base + dst_id * lli_per_log + 1;
	d40c->lcla.src = lcla_lidx_base + src_id * lli_per_log + 1;


	spin_unlock(&pool->lock);
	return 0;
err:
	spin_unlock(&pool->lock);
	return -EINVAL;
}

static void d40_lcla_id_put(struct d40_chan *d40c,
			    struct d40_lcla_pool *pool,
			    int id)
{
	if (id < 0)
		return;

	d40c->lcla.src_id = -1;
	d40c->lcla.dst_id = -1;

	spin_lock(&pool->lock);
	pool->alloc_map[d40c->phy_chan->num] &= (~(0x1 << id));
	spin_unlock(&pool->lock);
}

static int d40_channel_execute_command(struct d40_chan *d40c,
				       enum d40_command command)
{
	int status, i;
	void __iomem *active_reg;
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&d40c->base->execmd_lock, flags);

	if (d40c->phy_chan->num % 2 == 0)
		active_reg = d40c->base->virtbase + D40_DREG_ACTIVE;
	else
		active_reg = d40c->base->virtbase + D40_DREG_ACTIVO;

	if (command == D40_DMA_SUSPEND_REQ) {
		status = (readl(active_reg) &
			  D40_CHAN_POS_MASK(d40c->phy_chan->num)) >>
			D40_CHAN_POS(d40c->phy_chan->num);

		if (status == D40_DMA_SUSPENDED || status == D40_DMA_STOP)
			goto done;
	}

	writel(command << D40_CHAN_POS(d40c->phy_chan->num), active_reg);

	if (command == D40_DMA_SUSPEND_REQ) {

		for (i = 0 ; i < D40_SUSPEND_MAX_IT; i++) {
			status = (readl(active_reg) &
				  D40_CHAN_POS_MASK(d40c->phy_chan->num)) >>
				D40_CHAN_POS(d40c->phy_chan->num);

			cpu_relax();
			/*
			 * Reduce the number of bus accesses while
			 * waiting for the DMA to suspend.
			 */
			udelay(3);

			if (status == D40_DMA_STOP ||
			    status == D40_DMA_SUSPENDED)
				break;
		}

		if (i == D40_SUSPEND_MAX_IT) {
			dev_err(&d40c->chan.dev->device,
				"[%s]: unable to suspend the chl %d (log: %d) status %x\n",
				__func__, d40c->phy_chan->num, d40c->log_num,
				status);
			dump_stack();
			ret = -EBUSY;
		}

	}
done:
	spin_unlock_irqrestore(&d40c->base->execmd_lock, flags);
	return ret;
}

static void d40_term_all(struct d40_chan *d40c)
{
	struct d40_desc *d40d;
	struct d40_desc *d;
	struct d40_desc *_d;

	/* Release active descriptors */
	while ((d40d = d40_first_active_get(d40c))) {
		d40_desc_remove(d40d);

		/* Return desc to free-list */
		d40_desc_free(d40c, d40d);
	}

	/* Release queued descriptors waiting for transfer */
	while ((d40d = d40_first_queued(d40c))) {
		d40_desc_remove(d40d);

		/* Return desc to free-list */
		d40_desc_free(d40c, d40d);
	}

	/* Release client owned descriptors */
	if (!list_empty(&d40c->client))
		list_for_each_entry_safe(d, _d, &d40c->client, node) {
			d40_pool_lli_free(d);
			d40_desc_remove(d);
			/* Return desc to free-list */
			d40_desc_free(d40c, d40d);
		}

	d40_lcla_id_put(d40c, &d40c->base->lcla_pool,
			d40c->lcla.src_id);
	d40_lcla_id_put(d40c, &d40c->base->lcla_pool,
			d40c->lcla.dst_id);

	d40c->pending_tx = 0;
	d40c->busy = false;
}

static void d40_config_set_event(struct d40_chan *d40c, bool do_enable)
{
	u32 val;
	unsigned long flags;

	if (do_enable)
		val = D40_ACTIVATE_EVENTLINE;
	else
		val = D40_DEACTIVATE_EVENTLINE;

	spin_lock_irqsave(&d40c->phy_chan->lock, flags);

	/* Enable event line connected to device (or memcpy) */
	if ((d40c->dma_cfg.dir ==  STEDMA40_PERIPH_TO_MEM) ||
	    (d40c->dma_cfg.dir == STEDMA40_PERIPH_TO_PERIPH)) {
		u32 event = D40_TYPE_TO_EVENT(d40c->dma_cfg.src_dev_type);

		writel((val << D40_EVENTLINE_POS(event)) |
		       ~D40_EVENTLINE_MASK(event),
		       d40c->base->virtbase + D40_DREG_PCBASE +
		       d40c->phy_chan->num * D40_DREG_PCDELTA +
		       D40_CHAN_REG_SSLNK);
	}
	if (d40c->dma_cfg.dir !=  STEDMA40_PERIPH_TO_MEM) {
		u32 event = D40_TYPE_TO_EVENT(d40c->dma_cfg.dst_dev_type);

		writel((val << D40_EVENTLINE_POS(event)) |
		       ~D40_EVENTLINE_MASK(event),
		       d40c->base->virtbase + D40_DREG_PCBASE +
		       d40c->phy_chan->num * D40_DREG_PCDELTA +
		       D40_CHAN_REG_SDLNK);
	}

	spin_unlock_irqrestore(&d40c->phy_chan->lock, flags);
}

static u32 d40_chan_has_events(struct d40_chan *d40c)
{
	u32 val = 0;

	/* If SSLNK or SDLNK is zero all events are disabled */
	if ((d40c->dma_cfg.dir ==  STEDMA40_PERIPH_TO_MEM) ||
	    (d40c->dma_cfg.dir == STEDMA40_PERIPH_TO_PERIPH))
		val = readl(d40c->base->virtbase + D40_DREG_PCBASE +
			    d40c->phy_chan->num * D40_DREG_PCDELTA +
			    D40_CHAN_REG_SSLNK);

	if (d40c->dma_cfg.dir !=  STEDMA40_PERIPH_TO_MEM)
		val = readl(d40c->base->virtbase + D40_DREG_PCBASE +
			    d40c->phy_chan->num * D40_DREG_PCDELTA +
			    D40_CHAN_REG_SDLNK);
	return val;
}

static void d40_config_enable_lidx(struct d40_chan *d40c)
{
	/* Set LIDX for lcla */
	writel((d40c->phy_chan->num << D40_SREG_ELEM_LOG_LIDX_POS) &
	       D40_SREG_ELEM_LOG_LIDX_MASK,
	       d40c->base->virtbase + D40_DREG_PCBASE +
	       d40c->phy_chan->num * D40_DREG_PCDELTA + D40_CHAN_REG_SDELT);

	writel((d40c->phy_chan->num << D40_SREG_ELEM_LOG_LIDX_POS) &
	       D40_SREG_ELEM_LOG_LIDX_MASK,
	       d40c->base->virtbase + D40_DREG_PCBASE +
	       d40c->phy_chan->num * D40_DREG_PCDELTA + D40_CHAN_REG_SSELT);
}

static int d40_config_write(struct d40_chan *d40c)
{
	u32 addr_base;
	u32 var;
	int res;

	res = d40_channel_execute_command(d40c, D40_DMA_SUSPEND_REQ);
	if (res)
		return res;

	/* Odd addresses are even addresses + 4 */
	addr_base = (d40c->phy_chan->num % 2) * 4;
	/* Setup channel mode to logical or physical */
	var = ((u32)(d40c->log_num != D40_PHY_CHAN) + 1) <<
		D40_CHAN_POS(d40c->phy_chan->num);
	writel(var, d40c->base->virtbase + D40_DREG_PRMSE + addr_base);

	/* Setup operational mode option register */
	var = ((d40c->dma_cfg.channel_type >> STEDMA40_INFO_CH_MODE_OPT_POS) &
	       0x3) << D40_CHAN_POS(d40c->phy_chan->num);

	writel(var, d40c->base->virtbase + D40_DREG_PRMOE + addr_base);

	if (d40c->log_num != D40_PHY_CHAN) {
		/* Set default config for CFG reg */
		writel(d40c->src_def_cfg,
		       d40c->base->virtbase + D40_DREG_PCBASE +
		       d40c->phy_chan->num * D40_DREG_PCDELTA +
		       D40_CHAN_REG_SSCFG);
		writel(d40c->dst_def_cfg,
		       d40c->base->virtbase + D40_DREG_PCBASE +
		       d40c->phy_chan->num * D40_DREG_PCDELTA +
		       D40_CHAN_REG_SDCFG);

		d40_config_enable_lidx(d40c);
	}
	return res;
}

static void d40_desc_load(struct d40_chan *d40c, struct d40_desc *d40d)
{

	if (d40d->lli_phy.dst && d40d->lli_phy.src) {
		d40_phy_lli_write(d40c->base->virtbase,
				  d40c->phy_chan->num,
				  d40d->lli_phy.dst,
				  d40d->lli_phy.src);
		d40d->lli_tcount = d40d->lli_len;
	} else if (d40d->lli_log.dst && d40d->lli_log.src) {
		u32 lli_len;
		struct d40_log_lli *src = d40d->lli_log.src;
		struct d40_log_lli *dst = d40d->lli_log.dst;

		src += d40d->lli_tcount;
		dst += d40d->lli_tcount;

		if (d40d->lli_len <= d40c->base->plat_data->llis_per_log)
			lli_len = d40d->lli_len;
		else
			lli_len = d40c->base->plat_data->llis_per_log;
		d40d->lli_tcount += lli_len;
		d40_log_lli_write(d40c->lcpa, d40c->lcla.src,
				  d40c->lcla.dst,
				  dst, src,
				  d40c->base->plat_data->llis_per_log);
	}
}

static dma_cookie_t d40_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct d40_chan *d40c = container_of(tx->chan,
					     struct d40_chan,
					     chan);
	struct d40_desc *d40d = container_of(tx, struct d40_desc, txd);
	unsigned long flags;

	spin_lock_irqsave(&d40c->lock, flags);

	tx->cookie = d40_assign_cookie(d40c, d40d);

	d40_desc_queue(d40c, d40d);

	spin_unlock_irqrestore(&d40c->lock, flags);

	return tx->cookie;
}

static int d40_start(struct d40_chan *d40c)
{
	int err;

	if (d40c->log_num != D40_PHY_CHAN) {
		err = d40_channel_execute_command(d40c, D40_DMA_SUSPEND_REQ);
		if (err)
			return err;
		d40_config_set_event(d40c, true);
	}

	err = d40_channel_execute_command(d40c, D40_DMA_RUN);

	return err;
}

static struct d40_desc *d40_queue_start(struct d40_chan *d40c)
{
	struct d40_desc *d40d;
	int err;

	/* Start queued jobs, if any */
	d40d = d40_first_queued(d40c);

	if (d40d != NULL) {
		d40c->busy = true;

		/* Remove from queue */
		d40_desc_remove(d40d);

		/* Add to active queue */
		d40_desc_submit(d40c, d40d);

		/* Initiate DMA job */
		d40_desc_load(d40c, d40d);

		/* Start dma job */
		err = d40_start(d40c);

		if (err)
			return NULL;
	}

	return d40d;
}

/* called from interrupt context */
static void dma_tc_handle(struct d40_chan *d40c)
{
	struct d40_desc *d40d;

	if (!d40c->phy_chan)
		return;

	/* Get first active entry from list */
	d40d = d40_first_active_get(d40c);

	if (d40d == NULL)
		return;

	if (d40d->lli_tcount < d40d->lli_len) {

		d40_desc_load(d40c, d40d);
		/* Start dma job */
		(void) d40_start(d40c);
		return;
	}

	if (d40_queue_start(d40c) == NULL)
		d40c->busy = false;

	d40c->pending_tx++;
	tasklet_schedule(&d40c->tasklet);

}

static void dma_tasklet(unsigned long data)
{
	struct d40_chan *d40c = (struct d40_chan *) data;
	struct d40_desc *d40d_fin;
	unsigned long flags;
	dma_async_tx_callback callback;
	void *callback_param;

	spin_lock_irqsave(&d40c->lock, flags);

	/* Get first active entry from list */
	d40d_fin = d40_first_active_get(d40c);

	if (d40d_fin == NULL)
		goto err;

	d40c->completed = d40d_fin->txd.cookie;

	/*
	 * If terminating a channel pending_tx is set to zero.
	 * This prevents any finished active jobs to return to the client.
	 */
	if (d40c->pending_tx == 0) {
		spin_unlock_irqrestore(&d40c->lock, flags);
		return;
	}

	/* Callback to client */
	callback = d40d_fin->txd.callback;
	callback_param = d40d_fin->txd.callback_param;

	if (async_tx_test_ack(&d40d_fin->txd)) {
		d40_pool_lli_free(d40d_fin);
		d40_desc_remove(d40d_fin);
		/* Return desc to free-list */
		d40_desc_free(d40c, d40d_fin);
	} else {
		d40_desc_reset(d40d_fin);
		if (!d40d_fin->is_in_client_list) {
			d40_desc_remove(d40d_fin);
			list_add_tail(&d40d_fin->node, &d40c->client);
			d40d_fin->is_in_client_list = true;
		}
	}

	d40c->pending_tx--;

	if (d40c->pending_tx)
		tasklet_schedule(&d40c->tasklet);

	spin_unlock_irqrestore(&d40c->lock, flags);

	if (callback)
		callback(callback_param);

	return;

 err:
	/* Rescue manouver if receiving double interrupts */
	if (d40c->pending_tx > 0)
		d40c->pending_tx--;
	spin_unlock_irqrestore(&d40c->lock, flags);
}

static irqreturn_t d40_handle_interrupt(int irq, void *data)
{
	static const struct d40_interrupt_lookup il[] = {
		{D40_DREG_LCTIS0, D40_DREG_LCICR0, false,  0},
		{D40_DREG_LCTIS1, D40_DREG_LCICR1, false, 32},
		{D40_DREG_LCTIS2, D40_DREG_LCICR2, false, 64},
		{D40_DREG_LCTIS3, D40_DREG_LCICR3, false, 96},
		{D40_DREG_LCEIS0, D40_DREG_LCICR0, true,   0},
		{D40_DREG_LCEIS1, D40_DREG_LCICR1, true,  32},
		{D40_DREG_LCEIS2, D40_DREG_LCICR2, true,  64},
		{D40_DREG_LCEIS3, D40_DREG_LCICR3, true,  96},
		{D40_DREG_PCTIS,  D40_DREG_PCICR,  false, D40_PHY_CHAN},
		{D40_DREG_PCEIS,  D40_DREG_PCICR,  true,  D40_PHY_CHAN},
	};

	int i;
	u32 regs[ARRAY_SIZE(il)];
	u32 tmp;
	u32 idx;
	u32 row;
	long chan = -1;
	struct d40_chan *d40c;
	unsigned long flags;
	struct d40_base *base = data;

	spin_lock_irqsave(&base->interrupt_lock, flags);

	/* Read interrupt status of both logical and physical channels */
	for (i = 0; i < ARRAY_SIZE(il); i++)
		regs[i] = readl(base->virtbase + il[i].src);

	for (;;) {

		chan = find_next_bit((unsigned long *)regs,
				     BITS_PER_LONG * ARRAY_SIZE(il), chan + 1);

		/* No more set bits found? */
		if (chan == BITS_PER_LONG * ARRAY_SIZE(il))
			break;

		row = chan / BITS_PER_LONG;
		idx = chan & (BITS_PER_LONG - 1);

		/* ACK interrupt */
		tmp = readl(base->virtbase + il[row].clr);
		tmp |= 1 << idx;
		writel(tmp, base->virtbase + il[row].clr);

		if (il[row].offset == D40_PHY_CHAN)
			d40c = base->lookup_phy_chans[idx];
		else
			d40c = base->lookup_log_chans[il[row].offset + idx];
		spin_lock(&d40c->lock);

		if (!il[row].is_error)
			dma_tc_handle(d40c);
		else
			dev_err(base->dev, "[%s] IRQ chan: %ld offset %d idx %d\n",
				__func__, chan, il[row].offset, idx);

		spin_unlock(&d40c->lock);
	}

	spin_unlock_irqrestore(&base->interrupt_lock, flags);

	return IRQ_HANDLED;
}


static int d40_validate_conf(struct d40_chan *d40c,
			     struct stedma40_chan_cfg *conf)
{
	int res = 0;
	u32 dst_event_group = D40_TYPE_TO_GROUP(conf->dst_dev_type);
	u32 src_event_group = D40_TYPE_TO_GROUP(conf->src_dev_type);
	bool is_log = (conf->channel_type & STEDMA40_CHANNEL_IN_OPER_MODE)
		== STEDMA40_CHANNEL_IN_LOG_MODE;

	if (d40c->dma_cfg.dir == STEDMA40_MEM_TO_PERIPH &&
	    dst_event_group == STEDMA40_DEV_DST_MEMORY) {
		dev_err(&d40c->chan.dev->device, "[%s] Invalid dst\n",
			__func__);
		res = -EINVAL;
	}

	if (d40c->dma_cfg.dir == STEDMA40_PERIPH_TO_MEM &&
	    src_event_group == STEDMA40_DEV_SRC_MEMORY) {
		dev_err(&d40c->chan.dev->device, "[%s] Invalid src\n",
			__func__);
		res = -EINVAL;
	}

	if (src_event_group == STEDMA40_DEV_SRC_MEMORY &&
	    dst_event_group == STEDMA40_DEV_DST_MEMORY && is_log) {
		dev_err(&d40c->chan.dev->device,
			"[%s] No event line\n", __func__);
		res = -EINVAL;
	}

	if (conf->dir == STEDMA40_PERIPH_TO_PERIPH &&
	    (src_event_group != dst_event_group)) {
		dev_err(&d40c->chan.dev->device,
			"[%s] Invalid event group\n", __func__);
		res = -EINVAL;
	}

	if (conf->dir == STEDMA40_PERIPH_TO_PERIPH) {
		/*
		 * DMAC HW supports it. Will be added to this driver,
		 * in case any dma client requires it.
		 */
		dev_err(&d40c->chan.dev->device,
			"[%s] periph to periph not supported\n",
			__func__);
		res = -EINVAL;
	}

	return res;
}

static bool d40_alloc_mask_set(struct d40_phy_res *phy, bool is_src,
			       int log_event_line, bool is_log)
{
	unsigned long flags;
	spin_lock_irqsave(&phy->lock, flags);
	if (!is_log) {
		/* Physical interrupts are masked per physical full channel */
		if (phy->allocated_src == D40_ALLOC_FREE &&
		    phy->allocated_dst == D40_ALLOC_FREE) {
			phy->allocated_dst = D40_ALLOC_PHY;
			phy->allocated_src = D40_ALLOC_PHY;
			goto found;
		} else
			goto not_found;
	}

	/* Logical channel */
	if (is_src) {
		if (phy->allocated_src == D40_ALLOC_PHY)
			goto not_found;

		if (phy->allocated_src == D40_ALLOC_FREE)
			phy->allocated_src = D40_ALLOC_LOG_FREE;

		if (!(phy->allocated_src & (1 << log_event_line))) {
			phy->allocated_src |= 1 << log_event_line;
			goto found;
		} else
			goto not_found;
	} else {
		if (phy->allocated_dst == D40_ALLOC_PHY)
			goto not_found;

		if (phy->allocated_dst == D40_ALLOC_FREE)
			phy->allocated_dst = D40_ALLOC_LOG_FREE;

		if (!(phy->allocated_dst & (1 << log_event_line))) {
			phy->allocated_dst |= 1 << log_event_line;
			goto found;
		} else
			goto not_found;
	}

not_found:
	spin_unlock_irqrestore(&phy->lock, flags);
	return false;
found:
	spin_unlock_irqrestore(&phy->lock, flags);
	return true;
}

static bool d40_alloc_mask_free(struct d40_phy_res *phy, bool is_src,
			       int log_event_line)
{
	unsigned long flags;
	bool is_free = false;

	spin_lock_irqsave(&phy->lock, flags);
	if (!log_event_line) {
		/* Physical interrupts are masked per physical full channel */
		phy->allocated_dst = D40_ALLOC_FREE;
		phy->allocated_src = D40_ALLOC_FREE;
		is_free = true;
		goto out;
	}

	/* Logical channel */
	if (is_src) {
		phy->allocated_src &= ~(1 << log_event_line);
		if (phy->allocated_src == D40_ALLOC_LOG_FREE)
			phy->allocated_src = D40_ALLOC_FREE;
	} else {
		phy->allocated_dst &= ~(1 << log_event_line);
		if (phy->allocated_dst == D40_ALLOC_LOG_FREE)
			phy->allocated_dst = D40_ALLOC_FREE;
	}

	is_free = ((phy->allocated_src | phy->allocated_dst) ==
		   D40_ALLOC_FREE);

out:
	spin_unlock_irqrestore(&phy->lock, flags);

	return is_free;
}

static int d40_allocate_channel(struct d40_chan *d40c)
{
	int dev_type;
	int event_group;
	int event_line;
	struct d40_phy_res *phys;
	int i;
	int j;
	int log_num;
	bool is_src;
	bool is_log = (d40c->dma_cfg.channel_type & STEDMA40_CHANNEL_IN_OPER_MODE)
		== STEDMA40_CHANNEL_IN_LOG_MODE;


	phys = d40c->base->phy_res;

	if (d40c->dma_cfg.dir == STEDMA40_PERIPH_TO_MEM) {
		dev_type = d40c->dma_cfg.src_dev_type;
		log_num = 2 * dev_type;
		is_src = true;
	} else if (d40c->dma_cfg.dir == STEDMA40_MEM_TO_PERIPH ||
		   d40c->dma_cfg.dir == STEDMA40_MEM_TO_MEM) {
		/* dst event lines are used for logical memcpy */
		dev_type = d40c->dma_cfg.dst_dev_type;
		log_num = 2 * dev_type + 1;
		is_src = false;
	} else
		return -EINVAL;

	event_group = D40_TYPE_TO_GROUP(dev_type);
	event_line = D40_TYPE_TO_EVENT(dev_type);

	if (!is_log) {
		if (d40c->dma_cfg.dir == STEDMA40_MEM_TO_MEM) {
			/* Find physical half channel */
			for (i = 0; i < d40c->base->num_phy_chans; i++) {

				if (d40_alloc_mask_set(&phys[i], is_src,
						       0, is_log))
					goto found_phy;
			}
		} else
			for (j = 0; j < d40c->base->num_phy_chans; j += 8) {
				int phy_num = j  + event_group * 2;
				for (i = phy_num; i < phy_num + 2; i++) {
					if (d40_alloc_mask_set(&phys[i], is_src,
							       0, is_log))
						goto found_phy;
				}
			}
		return -EINVAL;
found_phy:
		d40c->phy_chan = &phys[i];
		d40c->log_num = D40_PHY_CHAN;
		goto out;
	}
	if (dev_type == -1)
		return -EINVAL;

	/* Find logical channel */
	for (j = 0; j < d40c->base->num_phy_chans; j += 8) {
		int phy_num = j + event_group * 2;
		/*
		 * Spread logical channels across all available physical rather
		 * than pack every logical channel at the first available phy
		 * channels.
		 */
		if (is_src) {
			for (i = phy_num; i < phy_num + 2; i++) {
				if (d40_alloc_mask_set(&phys[i], is_src,
						       event_line, is_log))
					goto found_log;
			}
		} else {
			for (i = phy_num + 1; i >= phy_num; i--) {
				if (d40_alloc_mask_set(&phys[i], is_src,
						       event_line, is_log))
					goto found_log;
			}
		}
	}
	return -EINVAL;

found_log:
	d40c->phy_chan = &phys[i];
	d40c->log_num = log_num;
out:

	if (is_log)
		d40c->base->lookup_log_chans[d40c->log_num] = d40c;
	else
		d40c->base->lookup_phy_chans[d40c->phy_chan->num] = d40c;

	return 0;

}

static int d40_config_chan(struct d40_chan *d40c,
			   struct stedma40_chan_cfg *info)
{

	/* Fill in basic CFG register values */
	d40_phy_cfg(&d40c->dma_cfg, &d40c->src_def_cfg,
		    &d40c->dst_def_cfg, d40c->log_num != D40_PHY_CHAN);

	if (d40c->log_num != D40_PHY_CHAN) {
		d40_log_cfg(&d40c->dma_cfg,
			    &d40c->log_def.lcsp1, &d40c->log_def.lcsp3);

		if (d40c->dma_cfg.dir == STEDMA40_PERIPH_TO_MEM)
			d40c->lcpa = d40c->base->lcpa_base +
				d40c->dma_cfg.src_dev_type * 32;
		else
			d40c->lcpa = d40c->base->lcpa_base +
				d40c->dma_cfg.dst_dev_type * 32 + 16;
	}

	/* Write channel configuration to the DMA */
	return d40_config_write(d40c);
}

static int d40_config_memcpy(struct d40_chan *d40c)
{
	dma_cap_mask_t cap = d40c->chan.device->cap_mask;

	if (dma_has_cap(DMA_MEMCPY, cap) && !dma_has_cap(DMA_SLAVE, cap)) {
		d40c->dma_cfg = *d40c->base->plat_data->memcpy_conf_log;
		d40c->dma_cfg.src_dev_type = STEDMA40_DEV_SRC_MEMORY;
		d40c->dma_cfg.dst_dev_type = d40c->base->plat_data->
			memcpy[d40c->chan.chan_id];

	} else if (dma_has_cap(DMA_MEMCPY, cap) &&
		   dma_has_cap(DMA_SLAVE, cap)) {
		d40c->dma_cfg = *d40c->base->plat_data->memcpy_conf_phy;
	} else {
		dev_err(&d40c->chan.dev->device, "[%s] No memcpy\n",
			__func__);
		return -EINVAL;
	}

	return 0;
}


static int d40_free_dma(struct d40_chan *d40c)
{

	int res = 0;
	u32 event, dir;
	struct d40_phy_res *phy = d40c->phy_chan;
	bool is_src;

	/* Terminate all queued and active transfers */
	d40_term_all(d40c);

	if (phy == NULL) {
		dev_err(&d40c->chan.dev->device, "[%s] phy == null\n",
			__func__);
		return -EINVAL;
	}

	if (phy->allocated_src == D40_ALLOC_FREE &&
	    phy->allocated_dst == D40_ALLOC_FREE) {
		dev_err(&d40c->chan.dev->device, "[%s] channel already free\n",
			__func__);
		return -EINVAL;
	}


	res = d40_channel_execute_command(d40c, D40_DMA_SUSPEND_REQ);
	if (res) {
		dev_err(&d40c->chan.dev->device, "[%s] suspend\n",
			__func__);
		return res;
	}

	if (d40c->dma_cfg.dir == STEDMA40_MEM_TO_PERIPH ||
	    d40c->dma_cfg.dir == STEDMA40_MEM_TO_MEM) {
		event = D40_TYPE_TO_EVENT(d40c->dma_cfg.dst_dev_type);
		dir = D40_CHAN_REG_SDLNK;
		is_src = false;
	} else if (d40c->dma_cfg.dir == STEDMA40_PERIPH_TO_MEM) {
		event = D40_TYPE_TO_EVENT(d40c->dma_cfg.src_dev_type);
		dir = D40_CHAN_REG_SSLNK;
		is_src = true;
	} else {
		dev_err(&d40c->chan.dev->device,
			"[%s] Unknown direction\n", __func__);
		return -EINVAL;
	}

	if (d40c->log_num != D40_PHY_CHAN) {
		/*
		 * Release logical channel, deactivate the event line during
		 * the time physical res is suspended.
		 */
		writel((D40_DEACTIVATE_EVENTLINE << D40_EVENTLINE_POS(event)) &
		       D40_EVENTLINE_MASK(event),
		       d40c->base->virtbase + D40_DREG_PCBASE +
		       phy->num * D40_DREG_PCDELTA + dir);

		d40c->base->lookup_log_chans[d40c->log_num] = NULL;

		/*
		 * Check if there are more logical allocation
		 * on this phy channel.
		 */
		if (!d40_alloc_mask_free(phy, is_src, event)) {
			/* Resume the other logical channels if any */
			if (d40_chan_has_events(d40c)) {
				res = d40_channel_execute_command(d40c,
								  D40_DMA_RUN);
				if (res) {
					dev_err(&d40c->chan.dev->device,
						"[%s] Executing RUN command\n",
						__func__);
					return res;
				}
			}
			return 0;
		}
	} else
		d40_alloc_mask_free(phy, is_src, 0);

	/* Release physical channel */
	res = d40_channel_execute_command(d40c, D40_DMA_STOP);
	if (res) {
		dev_err(&d40c->chan.dev->device,
			"[%s] Failed to stop channel\n", __func__);
		return res;
	}
	d40c->phy_chan = NULL;
	/* Invalidate channel type */
	d40c->dma_cfg.channel_type = 0;
	d40c->base->lookup_phy_chans[phy->num] = NULL;

	return 0;


}

static int d40_pause(struct dma_chan *chan)
{
	struct d40_chan *d40c =
		container_of(chan, struct d40_chan, chan);
	int res;

	unsigned long flags;

	spin_lock_irqsave(&d40c->lock, flags);

	res = d40_channel_execute_command(d40c, D40_DMA_SUSPEND_REQ);
	if (res == 0) {
		if (d40c->log_num != D40_PHY_CHAN) {
			d40_config_set_event(d40c, false);
			/* Resume the other logical channels if any */
			if (d40_chan_has_events(d40c))
				res = d40_channel_execute_command(d40c,
								  D40_DMA_RUN);
		}
	}

	spin_unlock_irqrestore(&d40c->lock, flags);
	return res;
}

static bool d40_is_paused(struct d40_chan *d40c)
{
	bool is_paused = false;
	unsigned long flags;
	void __iomem *active_reg;
	u32 status;
	u32 event;
	int res;

	spin_lock_irqsave(&d40c->lock, flags);

	if (d40c->log_num == D40_PHY_CHAN) {
		if (d40c->phy_chan->num % 2 == 0)
			active_reg = d40c->base->virtbase + D40_DREG_ACTIVE;
		else
			active_reg = d40c->base->virtbase + D40_DREG_ACTIVO;

		status = (readl(active_reg) &
			  D40_CHAN_POS_MASK(d40c->phy_chan->num)) >>
			D40_CHAN_POS(d40c->phy_chan->num);
		if (status == D40_DMA_SUSPENDED || status == D40_DMA_STOP)
			is_paused = true;

		goto _exit;
	}

	res = d40_channel_execute_command(d40c, D40_DMA_SUSPEND_REQ);
	if (res != 0)
		goto _exit;

	if (d40c->dma_cfg.dir == STEDMA40_MEM_TO_PERIPH ||
	    d40c->dma_cfg.dir == STEDMA40_MEM_TO_MEM)
		event = D40_TYPE_TO_EVENT(d40c->dma_cfg.dst_dev_type);
	else if (d40c->dma_cfg.dir == STEDMA40_PERIPH_TO_MEM)
		event = D40_TYPE_TO_EVENT(d40c->dma_cfg.src_dev_type);
	else {
		dev_err(&d40c->chan.dev->device,
			"[%s] Unknown direction\n", __func__);
		goto _exit;
	}
	status = d40_chan_has_events(d40c);
	status = (status & D40_EVENTLINE_MASK(event)) >>
		D40_EVENTLINE_POS(event);

	if (status != D40_DMA_RUN)
		is_paused = true;

	/* Resume the other logical channels if any */
	if (d40_chan_has_events(d40c))
		res = d40_channel_execute_command(d40c,
						  D40_DMA_RUN);

_exit:
	spin_unlock_irqrestore(&d40c->lock, flags);
	return is_paused;

}


static bool d40_tx_is_linked(struct d40_chan *d40c)
{
	bool is_link;

	if (d40c->log_num != D40_PHY_CHAN)
		is_link = readl(&d40c->lcpa->lcsp3) &  D40_MEM_LCSP3_DLOS_MASK;
	else
		is_link = readl(d40c->base->virtbase + D40_DREG_PCBASE +
				d40c->phy_chan->num * D40_DREG_PCDELTA +
				D40_CHAN_REG_SDLNK) &
			D40_SREG_LNK_PHYS_LNK_MASK;
	return is_link;
}

static u32 d40_residue(struct d40_chan *d40c)
{
	u32 num_elt;

	if (d40c->log_num != D40_PHY_CHAN)
		num_elt = (readl(&d40c->lcpa->lcsp2) &  D40_MEM_LCSP2_ECNT_MASK)
			>> D40_MEM_LCSP2_ECNT_POS;
	else
		num_elt = (readl(d40c->base->virtbase + D40_DREG_PCBASE +
				 d40c->phy_chan->num * D40_DREG_PCDELTA +
				 D40_CHAN_REG_SDELT) &
			   D40_SREG_ELEM_PHY_ECNT_MASK) >> D40_SREG_ELEM_PHY_ECNT_POS;
	return num_elt * (1 << d40c->dma_cfg.dst_info.data_width);
}

static int d40_resume(struct dma_chan *chan)
{
	struct d40_chan *d40c =
		container_of(chan, struct d40_chan, chan);
	int res = 0;
	unsigned long flags;

	spin_lock_irqsave(&d40c->lock, flags);

	if (d40c->log_num != D40_PHY_CHAN) {
		res = d40_channel_execute_command(d40c, D40_DMA_SUSPEND_REQ);
		if (res)
			goto out;

		/* If bytes left to transfer or linked tx resume job */
		if (d40_residue(d40c) || d40_tx_is_linked(d40c)) {
			d40_config_set_event(d40c, true);
			res = d40_channel_execute_command(d40c, D40_DMA_RUN);
		}
	} else if (d40_residue(d40c) || d40_tx_is_linked(d40c))
		res = d40_channel_execute_command(d40c, D40_DMA_RUN);

out:
	spin_unlock_irqrestore(&d40c->lock, flags);
	return res;
}

static u32 stedma40_residue(struct dma_chan *chan)
{
	struct d40_chan *d40c =
		container_of(chan, struct d40_chan, chan);
	u32 bytes_left;
	unsigned long flags;

	spin_lock_irqsave(&d40c->lock, flags);
	bytes_left = d40_residue(d40c);
	spin_unlock_irqrestore(&d40c->lock, flags);

	return bytes_left;
}

/* Public DMA functions in addition to the DMA engine framework */

int stedma40_set_psize(struct dma_chan *chan,
		       int src_psize,
		       int dst_psize)
{
	struct d40_chan *d40c =
		container_of(chan, struct d40_chan, chan);
	unsigned long flags;

	spin_lock_irqsave(&d40c->lock, flags);

	if (d40c->log_num != D40_PHY_CHAN) {
		d40c->log_def.lcsp1 &= ~D40_MEM_LCSP1_SCFG_PSIZE_MASK;
		d40c->log_def.lcsp3 &= ~D40_MEM_LCSP1_SCFG_PSIZE_MASK;
		d40c->log_def.lcsp1 |= src_psize << D40_MEM_LCSP1_SCFG_PSIZE_POS;
		d40c->log_def.lcsp3 |= dst_psize << D40_MEM_LCSP1_SCFG_PSIZE_POS;
		goto out;
	}

	if (src_psize == STEDMA40_PSIZE_PHY_1)
		d40c->src_def_cfg &= ~(1 << D40_SREG_CFG_PHY_PEN_POS);
	else {
		d40c->src_def_cfg |= 1 << D40_SREG_CFG_PHY_PEN_POS;
		d40c->src_def_cfg &= ~(STEDMA40_PSIZE_PHY_16 <<
				       D40_SREG_CFG_PSIZE_POS);
		d40c->src_def_cfg |= src_psize << D40_SREG_CFG_PSIZE_POS;
	}

	if (dst_psize == STEDMA40_PSIZE_PHY_1)
		d40c->dst_def_cfg &= ~(1 << D40_SREG_CFG_PHY_PEN_POS);
	else {
		d40c->dst_def_cfg |= 1 << D40_SREG_CFG_PHY_PEN_POS;
		d40c->dst_def_cfg &= ~(STEDMA40_PSIZE_PHY_16 <<
				       D40_SREG_CFG_PSIZE_POS);
		d40c->dst_def_cfg |= dst_psize << D40_SREG_CFG_PSIZE_POS;
	}
out:
	spin_unlock_irqrestore(&d40c->lock, flags);
	return 0;
}
EXPORT_SYMBOL(stedma40_set_psize);

struct dma_async_tx_descriptor *stedma40_memcpy_sg(struct dma_chan *chan,
						   struct scatterlist *sgl_dst,
						   struct scatterlist *sgl_src,
						   unsigned int sgl_len,
						   unsigned long flags)
{
	int res;
	struct d40_desc *d40d;
	struct d40_chan *d40c = container_of(chan, struct d40_chan,
					     chan);
	unsigned long flg;
	int lli_max = d40c->base->plat_data->llis_per_log;


	spin_lock_irqsave(&d40c->lock, flg);
	d40d = d40_desc_get(d40c);

	if (d40d == NULL)
		goto err;

	memset(d40d, 0, sizeof(struct d40_desc));
	d40d->lli_len = sgl_len;

	d40d->txd.flags = flags;

	if (d40c->log_num != D40_PHY_CHAN) {
		if (sgl_len > 1)
			/*
			 * Check if there is space available in lcla. If not,
			 * split list into 1-length and run only in lcpa
			 * space.
			 */
			if (d40_lcla_id_get(d40c,
					    &d40c->base->lcla_pool) != 0)
				lli_max = 1;

		if (d40_pool_lli_alloc(d40d, sgl_len, true) < 0) {
			dev_err(&d40c->chan.dev->device,
				"[%s] Out of memory\n", __func__);
			goto err;
		}

		(void) d40_log_sg_to_lli(d40c->lcla.src_id,
					 sgl_src,
					 sgl_len,
					 d40d->lli_log.src,
					 d40c->log_def.lcsp1,
					 d40c->dma_cfg.src_info.data_width,
					 flags & DMA_PREP_INTERRUPT, lli_max,
					 d40c->base->plat_data->llis_per_log);

		(void) d40_log_sg_to_lli(d40c->lcla.dst_id,
					 sgl_dst,
					 sgl_len,
					 d40d->lli_log.dst,
					 d40c->log_def.lcsp3,
					 d40c->dma_cfg.dst_info.data_width,
					 flags & DMA_PREP_INTERRUPT, lli_max,
					 d40c->base->plat_data->llis_per_log);


	} else {
		if (d40_pool_lli_alloc(d40d, sgl_len, false) < 0) {
			dev_err(&d40c->chan.dev->device,
				"[%s] Out of memory\n", __func__);
			goto err;
		}

		res = d40_phy_sg_to_lli(sgl_src,
					sgl_len,
					0,
					d40d->lli_phy.src,
					d40d->lli_phy.src_addr,
					d40c->src_def_cfg,
					d40c->dma_cfg.src_info.data_width,
					d40c->dma_cfg.src_info.psize,
					true);

		if (res < 0)
			goto err;

		res = d40_phy_sg_to_lli(sgl_dst,
					sgl_len,
					0,
					d40d->lli_phy.dst,
					d40d->lli_phy.dst_addr,
					d40c->dst_def_cfg,
					d40c->dma_cfg.dst_info.data_width,
					d40c->dma_cfg.dst_info.psize,
					true);

		if (res < 0)
			goto err;

		(void) dma_map_single(d40c->base->dev, d40d->lli_phy.src,
				      d40d->lli_pool.size, DMA_TO_DEVICE);
	}

	dma_async_tx_descriptor_init(&d40d->txd, chan);

	d40d->txd.tx_submit = d40_tx_submit;

	spin_unlock_irqrestore(&d40c->lock, flg);

	return &d40d->txd;
err:
	spin_unlock_irqrestore(&d40c->lock, flg);
	return NULL;
}
EXPORT_SYMBOL(stedma40_memcpy_sg);

bool stedma40_filter(struct dma_chan *chan, void *data)
{
	struct stedma40_chan_cfg *info = data;
	struct d40_chan *d40c =
		container_of(chan, struct d40_chan, chan);
	int err;

	if (data) {
		err = d40_validate_conf(d40c, info);
		if (!err)
			d40c->dma_cfg = *info;
	} else
		err = d40_config_memcpy(d40c);

	return err == 0;
}
EXPORT_SYMBOL(stedma40_filter);

/* DMA ENGINE functions */
static int d40_alloc_chan_resources(struct dma_chan *chan)
{
	int err;
	unsigned long flags;
	struct d40_chan *d40c =
		container_of(chan, struct d40_chan, chan);

	spin_lock_irqsave(&d40c->lock, flags);

	d40c->completed = chan->cookie = 1;

	/*
	 * If no dma configuration is set (channel_type == 0)
	 * use default configuration
	 */
	if (d40c->dma_cfg.channel_type == 0) {
		err = d40_config_memcpy(d40c);
		if (err)
			goto err_alloc;
	}

	err = d40_allocate_channel(d40c);
	if (err) {
		dev_err(&d40c->chan.dev->device,
			"[%s] Failed to allocate channel\n", __func__);
		goto err_alloc;
	}

	err = d40_config_chan(d40c, &d40c->dma_cfg);
	if (err) {
		dev_err(&d40c->chan.dev->device,
			"[%s] Failed to configure channel\n",
			__func__);
		goto err_config;
	}

	spin_unlock_irqrestore(&d40c->lock, flags);
	return 0;

 err_config:
	(void) d40_free_dma(d40c);
 err_alloc:
	spin_unlock_irqrestore(&d40c->lock, flags);
	dev_err(&d40c->chan.dev->device,
		"[%s] Channel allocation failed\n", __func__);
	return -EINVAL;
}

static void d40_free_chan_resources(struct dma_chan *chan)
{
	struct d40_chan *d40c =
		container_of(chan, struct d40_chan, chan);
	int err;
	unsigned long flags;

	spin_lock_irqsave(&d40c->lock, flags);

	err = d40_free_dma(d40c);

	if (err)
		dev_err(&d40c->chan.dev->device,
			"[%s] Failed to free channel\n", __func__);
	spin_unlock_irqrestore(&d40c->lock, flags);
}

static struct dma_async_tx_descriptor *d40_prep_memcpy(struct dma_chan *chan,
						       dma_addr_t dst,
						       dma_addr_t src,
						       size_t size,
						       unsigned long flags)
{
	struct d40_desc *d40d;
	struct d40_chan *d40c = container_of(chan, struct d40_chan,
					     chan);
	unsigned long flg;
	int err = 0;

	spin_lock_irqsave(&d40c->lock, flg);
	d40d = d40_desc_get(d40c);

	if (d40d == NULL) {
		dev_err(&d40c->chan.dev->device,
			"[%s] Descriptor is NULL\n", __func__);
		goto err;
	}

	memset(d40d, 0, sizeof(struct d40_desc));

	d40d->txd.flags = flags;

	dma_async_tx_descriptor_init(&d40d->txd, chan);

	d40d->txd.tx_submit = d40_tx_submit;

	if (d40c->log_num != D40_PHY_CHAN) {

		if (d40_pool_lli_alloc(d40d, 1, true) < 0) {
			dev_err(&d40c->chan.dev->device,
				"[%s] Out of memory\n", __func__);
			goto err;
		}
		d40d->lli_len = 1;

		d40_log_fill_lli(d40d->lli_log.src,
				 src,
				 size,
				 0,
				 d40c->log_def.lcsp1,
				 d40c->dma_cfg.src_info.data_width,
				 true, true);

		d40_log_fill_lli(d40d->lli_log.dst,
				 dst,
				 size,
				 0,
				 d40c->log_def.lcsp3,
				 d40c->dma_cfg.dst_info.data_width,
				 true, true);

	} else {

		if (d40_pool_lli_alloc(d40d, 1, false) < 0) {
			dev_err(&d40c->chan.dev->device,
				"[%s] Out of memory\n", __func__);
			goto err;
		}

		err = d40_phy_fill_lli(d40d->lli_phy.src,
				       src,
				       size,
				       d40c->dma_cfg.src_info.psize,
				       0,
				       d40c->src_def_cfg,
				       true,
				       d40c->dma_cfg.src_info.data_width,
				       false);
		if (err)
			goto err_fill_lli;

		err = d40_phy_fill_lli(d40d->lli_phy.dst,
				       dst,
				       size,
				       d40c->dma_cfg.dst_info.psize,
				       0,
				       d40c->dst_def_cfg,
				       true,
				       d40c->dma_cfg.dst_info.data_width,
				       false);

		if (err)
			goto err_fill_lli;

		(void) dma_map_single(d40c->base->dev, d40d->lli_phy.src,
				      d40d->lli_pool.size, DMA_TO_DEVICE);
	}

	spin_unlock_irqrestore(&d40c->lock, flg);
	return &d40d->txd;

err_fill_lli:
	dev_err(&d40c->chan.dev->device,
		"[%s] Failed filling in PHY LLI\n", __func__);
	d40_pool_lli_free(d40d);
err:
	spin_unlock_irqrestore(&d40c->lock, flg);
	return NULL;
}

static int d40_prep_slave_sg_log(struct d40_desc *d40d,
				 struct d40_chan *d40c,
				 struct scatterlist *sgl,
				 unsigned int sg_len,
				 enum dma_data_direction direction,
				 unsigned long flags)
{
	dma_addr_t dev_addr = 0;
	int total_size;
	int lli_max = d40c->base->plat_data->llis_per_log;

	if (d40_pool_lli_alloc(d40d, sg_len, true) < 0) {
		dev_err(&d40c->chan.dev->device,
			"[%s] Out of memory\n", __func__);
		return -ENOMEM;
	}

	d40d->lli_len = sg_len;
	d40d->lli_tcount = 0;

	if (sg_len > 1)
		/*
		 * Check if there is space available in lcla.
		 * If not, split list into 1-length and run only
		 * in lcpa space.
		 */
		if (d40_lcla_id_get(d40c, &d40c->base->lcla_pool) != 0)
			lli_max = 1;

	if (direction == DMA_FROM_DEVICE) {
		dev_addr = d40c->base->plat_data->dev_rx[d40c->dma_cfg.src_dev_type];
		total_size = d40_log_sg_to_dev(&d40c->lcla,
					       sgl, sg_len,
					       &d40d->lli_log,
					       &d40c->log_def,
					       d40c->dma_cfg.src_info.data_width,
					       d40c->dma_cfg.dst_info.data_width,
					       direction,
					       flags & DMA_PREP_INTERRUPT,
					       dev_addr, lli_max,
					       d40c->base->plat_data->llis_per_log);
	} else if (direction == DMA_TO_DEVICE) {
		dev_addr = d40c->base->plat_data->dev_tx[d40c->dma_cfg.dst_dev_type];
		total_size = d40_log_sg_to_dev(&d40c->lcla,
					       sgl, sg_len,
					       &d40d->lli_log,
					       &d40c->log_def,
					       d40c->dma_cfg.src_info.data_width,
					       d40c->dma_cfg.dst_info.data_width,
					       direction,
					       flags & DMA_PREP_INTERRUPT,
					       dev_addr, lli_max,
					       d40c->base->plat_data->llis_per_log);
	} else
		return -EINVAL;
	if (total_size < 0)
		return -EINVAL;

	return 0;
}

static int d40_prep_slave_sg_phy(struct d40_desc *d40d,
				 struct d40_chan *d40c,
				 struct scatterlist *sgl,
				 unsigned int sgl_len,
				 enum dma_data_direction direction,
				 unsigned long flags)
{
	dma_addr_t src_dev_addr;
	dma_addr_t dst_dev_addr;
	int res;

	if (d40_pool_lli_alloc(d40d, sgl_len, false) < 0) {
		dev_err(&d40c->chan.dev->device,
			"[%s] Out of memory\n", __func__);
		return -ENOMEM;
	}

	d40d->lli_len = sgl_len;
	d40d->lli_tcount = 0;

	if (direction == DMA_FROM_DEVICE) {
		dst_dev_addr = 0;
		src_dev_addr = d40c->base->plat_data->dev_rx[d40c->dma_cfg.src_dev_type];
	} else if (direction == DMA_TO_DEVICE) {
		dst_dev_addr = d40c->base->plat_data->dev_tx[d40c->dma_cfg.dst_dev_type];
		src_dev_addr = 0;
	} else
		return -EINVAL;

	res = d40_phy_sg_to_lli(sgl,
				sgl_len,
				src_dev_addr,
				d40d->lli_phy.src,
				d40d->lli_phy.src_addr,
				d40c->src_def_cfg,
				d40c->dma_cfg.src_info.data_width,
				d40c->dma_cfg.src_info.psize,
				true);
	if (res < 0)
		return res;

	res = d40_phy_sg_to_lli(sgl,
				sgl_len,
				dst_dev_addr,
				d40d->lli_phy.dst,
				d40d->lli_phy.dst_addr,
				d40c->dst_def_cfg,
				d40c->dma_cfg.dst_info.data_width,
				d40c->dma_cfg.dst_info.psize,
				 true);
	if (res < 0)
		return res;

	(void) dma_map_single(d40c->base->dev, d40d->lli_phy.src,
			      d40d->lli_pool.size, DMA_TO_DEVICE);
	return 0;
}

static struct dma_async_tx_descriptor *d40_prep_slave_sg(struct dma_chan *chan,
							 struct scatterlist *sgl,
							 unsigned int sg_len,
							 enum dma_data_direction direction,
							 unsigned long flags)
{
	struct d40_desc *d40d;
	struct d40_chan *d40c = container_of(chan, struct d40_chan,
					     chan);
	unsigned long flg;
	int err;

	if (d40c->dma_cfg.pre_transfer)
		d40c->dma_cfg.pre_transfer(chan,
					   d40c->dma_cfg.pre_transfer_data,
					   sg_dma_len(sgl));

	spin_lock_irqsave(&d40c->lock, flg);
	d40d = d40_desc_get(d40c);
	spin_unlock_irqrestore(&d40c->lock, flg);

	if (d40d == NULL)
		return NULL;

	memset(d40d, 0, sizeof(struct d40_desc));

	if (d40c->log_num != D40_PHY_CHAN)
		err = d40_prep_slave_sg_log(d40d, d40c, sgl, sg_len,
					    direction, flags);
	else
		err = d40_prep_slave_sg_phy(d40d, d40c, sgl, sg_len,
					    direction, flags);
	if (err) {
		dev_err(&d40c->chan.dev->device,
			"[%s] Failed to prepare %s slave sg job: %d\n",
			__func__,
			d40c->log_num != D40_PHY_CHAN ? "log" : "phy", err);
		return NULL;
	}

	d40d->txd.flags = flags;

	dma_async_tx_descriptor_init(&d40d->txd, chan);

	d40d->txd.tx_submit = d40_tx_submit;

	return &d40d->txd;
}

static enum dma_status d40_tx_status(struct dma_chan *chan,
				     dma_cookie_t cookie,
				     struct dma_tx_state *txstate)
{
	struct d40_chan *d40c = container_of(chan, struct d40_chan, chan);
	dma_cookie_t last_used;
	dma_cookie_t last_complete;
	int ret;

	last_complete = d40c->completed;
	last_used = chan->cookie;

	if (d40_is_paused(d40c))
		ret = DMA_PAUSED;
	else
		ret = dma_async_is_complete(cookie, last_complete, last_used);

	dma_set_tx_state(txstate, last_complete, last_used,
			 stedma40_residue(chan));

	return ret;
}

static void d40_issue_pending(struct dma_chan *chan)
{
	struct d40_chan *d40c = container_of(chan, struct d40_chan, chan);
	unsigned long flags;

	spin_lock_irqsave(&d40c->lock, flags);

	/* Busy means that pending jobs are already being processed */
	if (!d40c->busy)
		(void) d40_queue_start(d40c);

	spin_unlock_irqrestore(&d40c->lock, flags);
}

static int d40_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
		       unsigned long arg)
{
	unsigned long flags;
	struct d40_chan *d40c = container_of(chan, struct d40_chan, chan);

	switch (cmd) {
	case DMA_TERMINATE_ALL:
		spin_lock_irqsave(&d40c->lock, flags);
		d40_term_all(d40c);
		spin_unlock_irqrestore(&d40c->lock, flags);
		return 0;
	case DMA_PAUSE:
		return d40_pause(chan);
	case DMA_RESUME:
		return d40_resume(chan);
	}

	/* Other commands are unimplemented */
	return -ENXIO;
}

/* Initialization functions */

static void __init d40_chan_init(struct d40_base *base, struct dma_device *dma,
				 struct d40_chan *chans, int offset,
				 int num_chans)
{
	int i = 0;
	struct d40_chan *d40c;

	INIT_LIST_HEAD(&dma->channels);

	for (i = offset; i < offset + num_chans; i++) {
		d40c = &chans[i];
		d40c->base = base;
		d40c->chan.device = dma;

		/* Invalidate lcla element */
		d40c->lcla.src_id = -1;
		d40c->lcla.dst_id = -1;

		spin_lock_init(&d40c->lock);

		d40c->log_num = D40_PHY_CHAN;

		INIT_LIST_HEAD(&d40c->free);
		INIT_LIST_HEAD(&d40c->active);
		INIT_LIST_HEAD(&d40c->queue);
		INIT_LIST_HEAD(&d40c->client);

		d40c->free_len = 0;

		tasklet_init(&d40c->tasklet, dma_tasklet,
			     (unsigned long) d40c);

		list_add_tail(&d40c->chan.device_node,
			      &dma->channels);
	}
}

static int __init d40_dmaengine_init(struct d40_base *base,
				     int num_reserved_chans)
{
	int err ;

	d40_chan_init(base, &base->dma_slave, base->log_chans,
		      0, base->num_log_chans);

	dma_cap_zero(base->dma_slave.cap_mask);
	dma_cap_set(DMA_SLAVE, base->dma_slave.cap_mask);

	base->dma_slave.device_alloc_chan_resources = d40_alloc_chan_resources;
	base->dma_slave.device_free_chan_resources = d40_free_chan_resources;
	base->dma_slave.device_prep_dma_memcpy = d40_prep_memcpy;
	base->dma_slave.device_prep_slave_sg = d40_prep_slave_sg;
	base->dma_slave.device_tx_status = d40_tx_status;
	base->dma_slave.device_issue_pending = d40_issue_pending;
	base->dma_slave.device_control = d40_control;
	base->dma_slave.dev = base->dev;

	err = dma_async_device_register(&base->dma_slave);

	if (err) {
		dev_err(base->dev,
			"[%s] Failed to register slave channels\n",
			__func__);
		goto failure1;
	}

	d40_chan_init(base, &base->dma_memcpy, base->log_chans,
		      base->num_log_chans, base->plat_data->memcpy_len);

	dma_cap_zero(base->dma_memcpy.cap_mask);
	dma_cap_set(DMA_MEMCPY, base->dma_memcpy.cap_mask);

	base->dma_memcpy.device_alloc_chan_resources = d40_alloc_chan_resources;
	base->dma_memcpy.device_free_chan_resources = d40_free_chan_resources;
	base->dma_memcpy.device_prep_dma_memcpy = d40_prep_memcpy;
	base->dma_memcpy.device_prep_slave_sg = d40_prep_slave_sg;
	base->dma_memcpy.device_tx_status = d40_tx_status;
	base->dma_memcpy.device_issue_pending = d40_issue_pending;
	base->dma_memcpy.device_control = d40_control;
	base->dma_memcpy.dev = base->dev;
	/*
	 * This controller can only access address at even
	 * 32bit boundaries, i.e. 2^2
	 */
	base->dma_memcpy.copy_align = 2;

	err = dma_async_device_register(&base->dma_memcpy);

	if (err) {
		dev_err(base->dev,
			"[%s] Failed to regsiter memcpy only channels\n",
			__func__);
		goto failure2;
	}

	d40_chan_init(base, &base->dma_both, base->phy_chans,
		      0, num_reserved_chans);

	dma_cap_zero(base->dma_both.cap_mask);
	dma_cap_set(DMA_SLAVE, base->dma_both.cap_mask);
	dma_cap_set(DMA_MEMCPY, base->dma_both.cap_mask);

	base->dma_both.device_alloc_chan_resources = d40_alloc_chan_resources;
	base->dma_both.device_free_chan_resources = d40_free_chan_resources;
	base->dma_both.device_prep_dma_memcpy = d40_prep_memcpy;
	base->dma_both.device_prep_slave_sg = d40_prep_slave_sg;
	base->dma_both.device_tx_status = d40_tx_status;
	base->dma_both.device_issue_pending = d40_issue_pending;
	base->dma_both.device_control = d40_control;
	base->dma_both.dev = base->dev;
	base->dma_both.copy_align = 2;
	err = dma_async_device_register(&base->dma_both);

	if (err) {
		dev_err(base->dev,
			"[%s] Failed to register logical and physical capable channels\n",
			__func__);
		goto failure3;
	}
	return 0;
failure3:
	dma_async_device_unregister(&base->dma_memcpy);
failure2:
	dma_async_device_unregister(&base->dma_slave);
failure1:
	return err;
}

/* Initialization functions. */

static int __init d40_phy_res_init(struct d40_base *base)
{
	int i;
	int num_phy_chans_avail = 0;
	u32 val[2];
	int odd_even_bit = -2;

	val[0] = readl(base->virtbase + D40_DREG_PRSME);
	val[1] = readl(base->virtbase + D40_DREG_PRSMO);

	for (i = 0; i < base->num_phy_chans; i++) {
		base->phy_res[i].num = i;
		odd_even_bit += 2 * ((i % 2) == 0);
		if (((val[i % 2] >> odd_even_bit) & 3) == 1) {
			/* Mark security only channels as occupied */
			base->phy_res[i].allocated_src = D40_ALLOC_PHY;
			base->phy_res[i].allocated_dst = D40_ALLOC_PHY;
		} else {
			base->phy_res[i].allocated_src = D40_ALLOC_FREE;
			base->phy_res[i].allocated_dst = D40_ALLOC_FREE;
			num_phy_chans_avail++;
		}
		spin_lock_init(&base->phy_res[i].lock);
	}
	dev_info(base->dev, "%d of %d physical DMA channels available\n",
		 num_phy_chans_avail, base->num_phy_chans);

	/* Verify settings extended vs standard */
	val[0] = readl(base->virtbase + D40_DREG_PRTYP);

	for (i = 0; i < base->num_phy_chans; i++) {

		if (base->phy_res[i].allocated_src == D40_ALLOC_FREE &&
		    (val[0] & 0x3) != 1)
			dev_info(base->dev,
				 "[%s] INFO: channel %d is misconfigured (%d)\n",
				 __func__, i, val[0] & 0x3);

		val[0] = val[0] >> 2;
	}

	return num_phy_chans_avail;
}

static struct d40_base * __init d40_hw_detect_init(struct platform_device *pdev)
{
	static const struct d40_reg_val dma_id_regs[] = {
		/* Peripheral Id */
		{ .reg = D40_DREG_PERIPHID0, .val = 0x0040},
		{ .reg = D40_DREG_PERIPHID1, .val = 0x0000},
		/*
		 * D40_DREG_PERIPHID2 Depends on HW revision:
		 *  MOP500/HREF ED has 0x0008,
		 *  ? has 0x0018,
		 *  HREF V1 has 0x0028
		 */
		{ .reg = D40_DREG_PERIPHID3, .val = 0x0000},

		/* PCell Id */
		{ .reg = D40_DREG_CELLID0, .val = 0x000d},
		{ .reg = D40_DREG_CELLID1, .val = 0x00f0},
		{ .reg = D40_DREG_CELLID2, .val = 0x0005},
		{ .reg = D40_DREG_CELLID3, .val = 0x00b1}
	};
	struct stedma40_platform_data *plat_data;
	struct clk *clk = NULL;
	void __iomem *virtbase = NULL;
	struct resource *res = NULL;
	struct d40_base *base = NULL;
	int num_log_chans = 0;
	int num_phy_chans;
	int i;

	clk = clk_get(&pdev->dev, NULL);

	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "[%s] No matching clock found\n",
			__func__);
		goto failure;
	}

	clk_enable(clk);

	/* Get IO for DMAC base address */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "base");
	if (!res)
		goto failure;

	if (request_mem_region(res->start, resource_size(res),
			       D40_NAME " I/O base") == NULL)
		goto failure;

	virtbase = ioremap(res->start, resource_size(res));
	if (!virtbase)
		goto failure;

	/* HW version check */
	for (i = 0; i < ARRAY_SIZE(dma_id_regs); i++) {
		if (dma_id_regs[i].val !=
		    readl(virtbase + dma_id_regs[i].reg)) {
			dev_err(&pdev->dev,
				"[%s] Unknown hardware! Expected 0x%x at 0x%x but got 0x%x\n",
				__func__,
				dma_id_regs[i].val,
				dma_id_regs[i].reg,
				readl(virtbase + dma_id_regs[i].reg));
			goto failure;
		}
	}

	i = readl(virtbase + D40_DREG_PERIPHID2);

	if ((i & 0xf) != D40_PERIPHID2_DESIGNER) {
		dev_err(&pdev->dev,
			"[%s] Unknown designer! Got %x wanted %x\n",
			__func__, i & 0xf, D40_PERIPHID2_DESIGNER);
		goto failure;
	}

	/* The number of physical channels on this HW */
	num_phy_chans = 4 * (readl(virtbase + D40_DREG_ICFG) & 0x7) + 4;

	dev_info(&pdev->dev, "hardware revision: %d @ 0x%x\n",
		 (i >> 4) & 0xf, res->start);

	plat_data = pdev->dev.platform_data;

	/* Count the number of logical channels in use */
	for (i = 0; i < plat_data->dev_len; i++)
		if (plat_data->dev_rx[i] != 0)
			num_log_chans++;

	for (i = 0; i < plat_data->dev_len; i++)
		if (plat_data->dev_tx[i] != 0)
			num_log_chans++;

	base = kzalloc(ALIGN(sizeof(struct d40_base), 4) +
		       (num_phy_chans + num_log_chans + plat_data->memcpy_len) *
		       sizeof(struct d40_chan), GFP_KERNEL);

	if (base == NULL) {
		dev_err(&pdev->dev, "[%s] Out of memory\n", __func__);
		goto failure;
	}

	base->clk = clk;
	base->num_phy_chans = num_phy_chans;
	base->num_log_chans = num_log_chans;
	base->phy_start = res->start;
	base->phy_size = resource_size(res);
	base->virtbase = virtbase;
	base->plat_data = plat_data;
	base->dev = &pdev->dev;
	base->phy_chans = ((void *)base) + ALIGN(sizeof(struct d40_base), 4);
	base->log_chans = &base->phy_chans[num_phy_chans];

	base->phy_res = kzalloc(num_phy_chans * sizeof(struct d40_phy_res),
				GFP_KERNEL);
	if (!base->phy_res)
		goto failure;

	base->lookup_phy_chans = kzalloc(num_phy_chans *
					 sizeof(struct d40_chan *),
					 GFP_KERNEL);
	if (!base->lookup_phy_chans)
		goto failure;

	if (num_log_chans + plat_data->memcpy_len) {
		/*
		 * The max number of logical channels are event lines for all
		 * src devices and dst devices
		 */
		base->lookup_log_chans = kzalloc(plat_data->dev_len * 2 *
						 sizeof(struct d40_chan *),
						 GFP_KERNEL);
		if (!base->lookup_log_chans)
			goto failure;
	}
	base->lcla_pool.alloc_map = kzalloc(num_phy_chans * sizeof(u32),
					    GFP_KERNEL);
	if (!base->lcla_pool.alloc_map)
		goto failure;

	return base;

failure:
	if (clk) {
		clk_disable(clk);
		clk_put(clk);
	}
	if (virtbase)
		iounmap(virtbase);
	if (res)
		release_mem_region(res->start,
				   resource_size(res));
	if (virtbase)
		iounmap(virtbase);

	if (base) {
		kfree(base->lcla_pool.alloc_map);
		kfree(base->lookup_log_chans);
		kfree(base->lookup_phy_chans);
		kfree(base->phy_res);
		kfree(base);
	}

	return NULL;
}

static void __init d40_hw_init(struct d40_base *base)
{

	static const struct d40_reg_val dma_init_reg[] = {
		/* Clock every part of the DMA block from start */
		{ .reg = D40_DREG_GCC,    .val = 0x0000ff01},

		/* Interrupts on all logical channels */
		{ .reg = D40_DREG_LCMIS0, .val = 0xFFFFFFFF},
		{ .reg = D40_DREG_LCMIS1, .val = 0xFFFFFFFF},
		{ .reg = D40_DREG_LCMIS2, .val = 0xFFFFFFFF},
		{ .reg = D40_DREG_LCMIS3, .val = 0xFFFFFFFF},
		{ .reg = D40_DREG_LCICR0, .val = 0xFFFFFFFF},
		{ .reg = D40_DREG_LCICR1, .val = 0xFFFFFFFF},
		{ .reg = D40_DREG_LCICR2, .val = 0xFFFFFFFF},
		{ .reg = D40_DREG_LCICR3, .val = 0xFFFFFFFF},
		{ .reg = D40_DREG_LCTIS0, .val = 0xFFFFFFFF},
		{ .reg = D40_DREG_LCTIS1, .val = 0xFFFFFFFF},
		{ .reg = D40_DREG_LCTIS2, .val = 0xFFFFFFFF},
		{ .reg = D40_DREG_LCTIS3, .val = 0xFFFFFFFF}
	};
	int i;
	u32 prmseo[2] = {0, 0};
	u32 activeo[2] = {0xFFFFFFFF, 0xFFFFFFFF};
	u32 pcmis = 0;
	u32 pcicr = 0;

	for (i = 0; i < ARRAY_SIZE(dma_init_reg); i++)
		writel(dma_init_reg[i].val,
		       base->virtbase + dma_init_reg[i].reg);

	/* Configure all our dma channels to default settings */
	for (i = 0; i < base->num_phy_chans; i++) {

		activeo[i % 2] = activeo[i % 2] << 2;

		if (base->phy_res[base->num_phy_chans - i - 1].allocated_src
		    == D40_ALLOC_PHY) {
			activeo[i % 2] |= 3;
			continue;
		}

		/* Enable interrupt # */
		pcmis = (pcmis << 1) | 1;

		/* Clear interrupt # */
		pcicr = (pcicr << 1) | 1;

		/* Set channel to physical mode */
		prmseo[i % 2] = prmseo[i % 2] << 2;
		prmseo[i % 2] |= 1;

	}

	writel(prmseo[1], base->virtbase + D40_DREG_PRMSE);
	writel(prmseo[0], base->virtbase + D40_DREG_PRMSO);
	writel(activeo[1], base->virtbase + D40_DREG_ACTIVE);
	writel(activeo[0], base->virtbase + D40_DREG_ACTIVO);

	/* Write which interrupt to enable */
	writel(pcmis, base->virtbase + D40_DREG_PCMIS);

	/* Write which interrupt to clear */
	writel(pcicr, base->virtbase + D40_DREG_PCICR);

}

static int __init d40_probe(struct platform_device *pdev)
{
	int err;
	int ret = -ENOENT;
	struct d40_base *base;
	struct resource *res = NULL;
	int num_reserved_chans;
	u32 val;

	base = d40_hw_detect_init(pdev);

	if (!base)
		goto failure;

	num_reserved_chans = d40_phy_res_init(base);

	platform_set_drvdata(pdev, base);

	spin_lock_init(&base->interrupt_lock);
	spin_lock_init(&base->execmd_lock);

	/* Get IO for logical channel parameter address */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lcpa");
	if (!res) {
		ret = -ENOENT;
		dev_err(&pdev->dev,
			"[%s] No \"lcpa\" memory resource\n",
			__func__);
		goto failure;
	}
	base->lcpa_size = resource_size(res);
	base->phy_lcpa = res->start;

	if (request_mem_region(res->start, resource_size(res),
			       D40_NAME " I/O lcpa") == NULL) {
		ret = -EBUSY;
		dev_err(&pdev->dev,
			"[%s] Failed to request LCPA region 0x%x-0x%x\n",
			__func__, res->start, res->end);
		goto failure;
	}

	/* We make use of ESRAM memory for this. */
	val = readl(base->virtbase + D40_DREG_LCPA);
	if (res->start != val && val != 0) {
		dev_warn(&pdev->dev,
			 "[%s] Mismatch LCPA dma 0x%x, def 0x%x\n",
			 __func__, val, res->start);
	} else
		writel(res->start, base->virtbase + D40_DREG_LCPA);

	base->lcpa_base = ioremap(res->start, resource_size(res));
	if (!base->lcpa_base) {
		ret = -ENOMEM;
		dev_err(&pdev->dev,
			"[%s] Failed to ioremap LCPA region\n",
			__func__);
		goto failure;
	}
	/* Get IO for logical channel link address */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lcla");
	if (!res) {
		ret = -ENOENT;
		dev_err(&pdev->dev,
			"[%s] No \"lcla\" resource defined\n",
			__func__);
		goto failure;
	}

	base->lcla_pool.base_size = resource_size(res);
	base->lcla_pool.phy = res->start;

	if (request_mem_region(res->start, resource_size(res),
			       D40_NAME " I/O lcla") == NULL) {
		ret = -EBUSY;
		dev_err(&pdev->dev,
			"[%s] Failed to request LCLA region 0x%x-0x%x\n",
			__func__, res->start, res->end);
		goto failure;
	}
	val = readl(base->virtbase + D40_DREG_LCLA);
	if (res->start != val && val != 0) {
		dev_warn(&pdev->dev,
			 "[%s] Mismatch LCLA dma 0x%x, def 0x%x\n",
			 __func__, val, res->start);
	} else
		writel(res->start, base->virtbase + D40_DREG_LCLA);

	base->lcla_pool.base = ioremap(res->start, resource_size(res));
	if (!base->lcla_pool.base) {
		ret = -ENOMEM;
		dev_err(&pdev->dev,
			"[%s] Failed to ioremap LCLA 0x%x-0x%x\n",
			__func__, res->start, res->end);
		goto failure;
	}

	spin_lock_init(&base->lcla_pool.lock);

	base->lcla_pool.num_blocks = base->num_phy_chans;

	base->irq = platform_get_irq(pdev, 0);

	ret = request_irq(base->irq, d40_handle_interrupt, 0, D40_NAME, base);

	if (ret) {
		dev_err(&pdev->dev, "[%s] No IRQ defined\n", __func__);
		goto failure;
	}

	err = d40_dmaengine_init(base, num_reserved_chans);
	if (err)
		goto failure;

	d40_hw_init(base);

	dev_info(base->dev, "initialized\n");
	return 0;

failure:
	if (base) {
		if (base->virtbase)
			iounmap(base->virtbase);
		if (base->lcla_pool.phy)
			release_mem_region(base->lcla_pool.phy,
					   base->lcla_pool.base_size);
		if (base->phy_lcpa)
			release_mem_region(base->phy_lcpa,
					   base->lcpa_size);
		if (base->phy_start)
			release_mem_region(base->phy_start,
					   base->phy_size);
		if (base->clk) {
			clk_disable(base->clk);
			clk_put(base->clk);
		}

		kfree(base->lcla_pool.alloc_map);
		kfree(base->lookup_log_chans);
		kfree(base->lookup_phy_chans);
		kfree(base->phy_res);
		kfree(base);
	}

	dev_err(&pdev->dev, "[%s] probe failed\n", __func__);
	return ret;
}

static struct platform_driver d40_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name  = D40_NAME,
	},
};

int __init stedma40_init(void)
{
	return platform_driver_probe(&d40_driver, d40_probe);
}
arch_initcall(stedma40_init);
