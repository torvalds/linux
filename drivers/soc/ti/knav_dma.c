// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Texas Instruments Incorporated
 * Authors:	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *		Sandeep Nair <sandeep_n@ti.com>
 *		Cyril Chemparathy <cyril@ti.com>
 */

#include <linux/io.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/dma-direction.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/of_dma.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/soc/ti/knav_dma.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#define REG_MASK		0xffffffff

#define DMA_LOOPBACK		BIT(31)
#define DMA_ENABLE		BIT(31)
#define DMA_TEARDOWN		BIT(30)

#define DMA_TX_FILT_PSWORDS	BIT(29)
#define DMA_TX_FILT_EINFO	BIT(30)
#define DMA_TX_PRIO_SHIFT	0
#define DMA_RX_PRIO_SHIFT	16
#define DMA_PRIO_MASK		GENMASK(3, 0)
#define DMA_PRIO_DEFAULT	0
#define DMA_RX_TIMEOUT_DEFAULT	17500 /* cycles */
#define DMA_RX_TIMEOUT_MASK	GENMASK(16, 0)
#define DMA_RX_TIMEOUT_SHIFT	0

#define CHAN_HAS_EPIB		BIT(30)
#define CHAN_HAS_PSINFO		BIT(29)
#define CHAN_ERR_RETRY		BIT(28)
#define CHAN_PSINFO_AT_SOP	BIT(25)
#define CHAN_SOP_OFF_SHIFT	16
#define CHAN_SOP_OFF_MASK	GENMASK(9, 0)
#define DESC_TYPE_SHIFT		26
#define DESC_TYPE_MASK		GENMASK(2, 0)

/*
 * QMGR & QNUM together make up 14 bits with QMGR as the 2 MSb's in the logical
 * navigator cloud mapping scheme.
 * using the 14bit physical queue numbers directly maps into this scheme.
 */
#define CHAN_QNUM_MASK		GENMASK(14, 0)
#define DMA_MAX_QMS		4
#define DMA_TIMEOUT		1	/* msecs */
#define DMA_INVALID_ID		0xffff

struct reg_global {
	u32	revision;
	u32	perf_control;
	u32	emulation_control;
	u32	priority_control;
	u32	qm_base_address[DMA_MAX_QMS];
};

struct reg_chan {
	u32	control;
	u32	mode;
	u32	__rsvd[6];
};

struct reg_tx_sched {
	u32	prio;
};

struct reg_rx_flow {
	u32	control;
	u32	tags;
	u32	tag_sel;
	u32	fdq_sel[2];
	u32	thresh[3];
};

struct knav_dma_pool_device {
	struct device			*dev;
	struct list_head		list;
};

struct knav_dma_device {
	bool				loopback, enable_all;
	unsigned			tx_priority, rx_priority, rx_timeout;
	unsigned			logical_queue_managers;
	unsigned			qm_base_address[DMA_MAX_QMS];
	struct reg_global __iomem	*reg_global;
	struct reg_chan __iomem		*reg_tx_chan;
	struct reg_rx_flow __iomem	*reg_rx_flow;
	struct reg_chan __iomem		*reg_rx_chan;
	struct reg_tx_sched __iomem	*reg_tx_sched;
	unsigned			max_rx_chan, max_tx_chan;
	unsigned			max_rx_flow;
	char				name[32];
	atomic_t			ref_count;
	struct list_head		list;
	struct list_head		chan_list;
	spinlock_t			lock;
};

struct knav_dma_chan {
	enum dma_transfer_direction	direction;
	struct knav_dma_device		*dma;
	atomic_t			ref_count;

	/* registers */
	struct reg_chan __iomem		*reg_chan;
	struct reg_tx_sched __iomem	*reg_tx_sched;
	struct reg_rx_flow __iomem	*reg_rx_flow;

	/* configuration stuff */
	unsigned			channel, flow;
	struct knav_dma_cfg		cfg;
	struct list_head		list;
	spinlock_t			lock;
};

#define chan_number(ch)	((ch->direction == DMA_MEM_TO_DEV) ? \
			ch->channel : ch->flow)

static struct knav_dma_pool_device *kdev;

static bool device_ready;
bool knav_dma_device_ready(void)
{
	return device_ready;
}
EXPORT_SYMBOL_GPL(knav_dma_device_ready);

static bool check_config(struct knav_dma_chan *chan, struct knav_dma_cfg *cfg)
{
	if (!memcmp(&chan->cfg, cfg, sizeof(*cfg)))
		return true;
	else
		return false;
}

static int chan_start(struct knav_dma_chan *chan,
			struct knav_dma_cfg *cfg)
{
	u32 v = 0;

	spin_lock(&chan->lock);
	if ((chan->direction == DMA_MEM_TO_DEV) && chan->reg_chan) {
		if (cfg->u.tx.filt_pswords)
			v |= DMA_TX_FILT_PSWORDS;
		if (cfg->u.tx.filt_einfo)
			v |= DMA_TX_FILT_EINFO;
		writel_relaxed(v, &chan->reg_chan->mode);
		writel_relaxed(DMA_ENABLE, &chan->reg_chan->control);
	}

	if (chan->reg_tx_sched)
		writel_relaxed(cfg->u.tx.priority, &chan->reg_tx_sched->prio);

	if (chan->reg_rx_flow) {
		v = 0;

		if (cfg->u.rx.einfo_present)
			v |= CHAN_HAS_EPIB;
		if (cfg->u.rx.psinfo_present)
			v |= CHAN_HAS_PSINFO;
		if (cfg->u.rx.err_mode == DMA_RETRY)
			v |= CHAN_ERR_RETRY;
		v |= (cfg->u.rx.desc_type & DESC_TYPE_MASK) << DESC_TYPE_SHIFT;
		if (cfg->u.rx.psinfo_at_sop)
			v |= CHAN_PSINFO_AT_SOP;
		v |= (cfg->u.rx.sop_offset & CHAN_SOP_OFF_MASK)
			<< CHAN_SOP_OFF_SHIFT;
		v |= cfg->u.rx.dst_q & CHAN_QNUM_MASK;

		writel_relaxed(v, &chan->reg_rx_flow->control);
		writel_relaxed(0, &chan->reg_rx_flow->tags);
		writel_relaxed(0, &chan->reg_rx_flow->tag_sel);

		v =  cfg->u.rx.fdq[0] << 16;
		v |=  cfg->u.rx.fdq[1] & CHAN_QNUM_MASK;
		writel_relaxed(v, &chan->reg_rx_flow->fdq_sel[0]);

		v =  cfg->u.rx.fdq[2] << 16;
		v |=  cfg->u.rx.fdq[3] & CHAN_QNUM_MASK;
		writel_relaxed(v, &chan->reg_rx_flow->fdq_sel[1]);

		writel_relaxed(0, &chan->reg_rx_flow->thresh[0]);
		writel_relaxed(0, &chan->reg_rx_flow->thresh[1]);
		writel_relaxed(0, &chan->reg_rx_flow->thresh[2]);
	}

	/* Keep a copy of the cfg */
	memcpy(&chan->cfg, cfg, sizeof(*cfg));
	spin_unlock(&chan->lock);

	return 0;
}

static int chan_teardown(struct knav_dma_chan *chan)
{
	unsigned long end, value;

	if (!chan->reg_chan)
		return 0;

	/* indicate teardown */
	writel_relaxed(DMA_TEARDOWN, &chan->reg_chan->control);

	/* wait for the dma to shut itself down */
	end = jiffies + msecs_to_jiffies(DMA_TIMEOUT);
	do {
		value = readl_relaxed(&chan->reg_chan->control);
		if ((value & DMA_ENABLE) == 0)
			break;
	} while (time_after(end, jiffies));

	if (readl_relaxed(&chan->reg_chan->control) & DMA_ENABLE) {
		dev_err(kdev->dev, "timeout waiting for teardown\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void chan_stop(struct knav_dma_chan *chan)
{
	spin_lock(&chan->lock);
	if (chan->reg_rx_flow) {
		/* first detach fdqs, starve out the flow */
		writel_relaxed(0, &chan->reg_rx_flow->fdq_sel[0]);
		writel_relaxed(0, &chan->reg_rx_flow->fdq_sel[1]);
		writel_relaxed(0, &chan->reg_rx_flow->thresh[0]);
		writel_relaxed(0, &chan->reg_rx_flow->thresh[1]);
		writel_relaxed(0, &chan->reg_rx_flow->thresh[2]);
	}

	/* teardown the dma channel */
	chan_teardown(chan);

	/* then disconnect the completion side */
	if (chan->reg_rx_flow) {
		writel_relaxed(0, &chan->reg_rx_flow->control);
		writel_relaxed(0, &chan->reg_rx_flow->tags);
		writel_relaxed(0, &chan->reg_rx_flow->tag_sel);
	}

	memset(&chan->cfg, 0, sizeof(struct knav_dma_cfg));
	spin_unlock(&chan->lock);

	dev_dbg(kdev->dev, "channel stopped\n");
}

static void dma_hw_enable_all(struct knav_dma_device *dma)
{
	int i;

	for (i = 0; i < dma->max_tx_chan; i++) {
		writel_relaxed(0, &dma->reg_tx_chan[i].mode);
		writel_relaxed(DMA_ENABLE, &dma->reg_tx_chan[i].control);
	}
}


static void knav_dma_hw_init(struct knav_dma_device *dma)
{
	unsigned v;
	int i;

	spin_lock(&dma->lock);
	v  = dma->loopback ? DMA_LOOPBACK : 0;
	writel_relaxed(v, &dma->reg_global->emulation_control);

	v = readl_relaxed(&dma->reg_global->perf_control);
	v |= ((dma->rx_timeout & DMA_RX_TIMEOUT_MASK) << DMA_RX_TIMEOUT_SHIFT);
	writel_relaxed(v, &dma->reg_global->perf_control);

	v = ((dma->tx_priority << DMA_TX_PRIO_SHIFT) |
	     (dma->rx_priority << DMA_RX_PRIO_SHIFT));

	writel_relaxed(v, &dma->reg_global->priority_control);

	/* Always enable all Rx channels. Rx paths are managed using flows */
	for (i = 0; i < dma->max_rx_chan; i++)
		writel_relaxed(DMA_ENABLE, &dma->reg_rx_chan[i].control);

	for (i = 0; i < dma->logical_queue_managers; i++)
		writel_relaxed(dma->qm_base_address[i],
			       &dma->reg_global->qm_base_address[i]);
	spin_unlock(&dma->lock);
}

static void knav_dma_hw_destroy(struct knav_dma_device *dma)
{
	int i;
	unsigned v;

	spin_lock(&dma->lock);
	v = ~DMA_ENABLE & REG_MASK;

	for (i = 0; i < dma->max_rx_chan; i++)
		writel_relaxed(v, &dma->reg_rx_chan[i].control);

	for (i = 0; i < dma->max_tx_chan; i++)
		writel_relaxed(v, &dma->reg_tx_chan[i].control);
	spin_unlock(&dma->lock);
}

static void dma_debug_show_channels(struct seq_file *s,
					struct knav_dma_chan *chan)
{
	int i;

	seq_printf(s, "\t%s %d:\t",
		((chan->direction == DMA_MEM_TO_DEV) ? "tx chan" : "rx flow"),
		chan_number(chan));

	if (chan->direction == DMA_MEM_TO_DEV) {
		seq_printf(s, "einfo - %d, pswords - %d, priority - %d\n",
			chan->cfg.u.tx.filt_einfo,
			chan->cfg.u.tx.filt_pswords,
			chan->cfg.u.tx.priority);
	} else {
		seq_printf(s, "einfo - %d, psinfo - %d, desc_type - %d\n",
			chan->cfg.u.rx.einfo_present,
			chan->cfg.u.rx.psinfo_present,
			chan->cfg.u.rx.desc_type);
		seq_printf(s, "\t\t\tdst_q: [%d], thresh: %d fdq: ",
			chan->cfg.u.rx.dst_q,
			chan->cfg.u.rx.thresh);
		for (i = 0; i < KNAV_DMA_FDQ_PER_CHAN; i++)
			seq_printf(s, "[%d]", chan->cfg.u.rx.fdq[i]);
		seq_printf(s, "\n");
	}
}

static void dma_debug_show_devices(struct seq_file *s,
					struct knav_dma_device *dma)
{
	struct knav_dma_chan *chan;

	list_for_each_entry(chan, &dma->chan_list, list) {
		if (atomic_read(&chan->ref_count))
			dma_debug_show_channels(s, chan);
	}
}

static int knav_dma_debug_show(struct seq_file *s, void *v)
{
	struct knav_dma_device *dma;

	list_for_each_entry(dma, &kdev->list, list) {
		if (atomic_read(&dma->ref_count)) {
			seq_printf(s, "%s : max_tx_chan: (%d), max_rx_flows: (%d)\n",
			dma->name, dma->max_tx_chan, dma->max_rx_flow);
			dma_debug_show_devices(s, dma);
		}
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(knav_dma_debug);

static int of_channel_match_helper(struct device_node *np, const char *name,
					const char **dma_instance)
{
	struct of_phandle_args args;
	struct device_node *dma_node;
	int index;

	dma_node = of_parse_phandle(np, "ti,navigator-dmas", 0);
	if (!dma_node)
		return -ENODEV;

	*dma_instance = dma_node->name;
	index = of_property_match_string(np, "ti,navigator-dma-names", name);
	if (index < 0) {
		dev_err(kdev->dev, "No 'ti,navigator-dma-names' property\n");
		return -ENODEV;
	}

	if (of_parse_phandle_with_fixed_args(np, "ti,navigator-dmas",
					1, index, &args)) {
		dev_err(kdev->dev, "Missing the phandle args name %s\n", name);
		return -ENODEV;
	}

	if (args.args[0] < 0) {
		dev_err(kdev->dev, "Missing args for %s\n", name);
		return -ENODEV;
	}

	return args.args[0];
}

/**
 * knav_dma_open_channel() - try to setup an exclusive slave channel
 * @dev:	pointer to client device structure
 * @name:	slave channel name
 * @config:	dma configuration parameters
 *
 * Returns pointer to appropriate DMA channel on success or error.
 */
void *knav_dma_open_channel(struct device *dev, const char *name,
					struct knav_dma_cfg *config)
{
	struct knav_dma_device *dma = NULL, *iter1;
	struct knav_dma_chan *chan = NULL, *iter2;
	int chan_num = -1;
	const char *instance;

	if (!kdev) {
		pr_err("keystone-navigator-dma driver not registered\n");
		return (void *)-EINVAL;
	}

	chan_num = of_channel_match_helper(dev->of_node, name, &instance);
	if (chan_num < 0) {
		dev_err(kdev->dev, "No DMA instance with name %s\n", name);
		return (void *)-EINVAL;
	}

	dev_dbg(kdev->dev, "initializing %s channel %d from DMA %s\n",
		  config->direction == DMA_MEM_TO_DEV ? "transmit" :
		  config->direction == DMA_DEV_TO_MEM ? "receive"  :
		  "unknown", chan_num, instance);

	if (config->direction != DMA_MEM_TO_DEV &&
	    config->direction != DMA_DEV_TO_MEM) {
		dev_err(kdev->dev, "bad direction\n");
		return (void *)-EINVAL;
	}

	/* Look for correct dma instance */
	list_for_each_entry(iter1, &kdev->list, list) {
		if (!strcmp(iter1->name, instance)) {
			dma = iter1;
			break;
		}
	}
	if (!dma) {
		dev_err(kdev->dev, "No DMA instance with name %s\n", instance);
		return (void *)-EINVAL;
	}

	/* Look for correct dma channel from dma instance */
	list_for_each_entry(iter2, &dma->chan_list, list) {
		if (config->direction == DMA_MEM_TO_DEV) {
			if (iter2->channel == chan_num) {
				chan = iter2;
				break;
			}
		} else {
			if (iter2->flow == chan_num) {
				chan = iter2;
				break;
			}
		}
	}
	if (!chan) {
		dev_err(kdev->dev, "channel %d is not in DMA %s\n",
				chan_num, instance);
		return (void *)-EINVAL;
	}

	if (atomic_read(&chan->ref_count) >= 1) {
		if (!check_config(chan, config)) {
			dev_err(kdev->dev, "channel %d config miss-match\n",
				chan_num);
			return (void *)-EINVAL;
		}
	}

	if (atomic_inc_return(&chan->dma->ref_count) <= 1)
		knav_dma_hw_init(chan->dma);

	if (atomic_inc_return(&chan->ref_count) <= 1)
		chan_start(chan, config);

	dev_dbg(kdev->dev, "channel %d opened from DMA %s\n",
				chan_num, instance);

	return chan;
}
EXPORT_SYMBOL_GPL(knav_dma_open_channel);

/**
 * knav_dma_close_channel()	- Destroy a dma channel
 *
 * @channel:	dma channel handle
 *
 */
void knav_dma_close_channel(void *channel)
{
	struct knav_dma_chan *chan = channel;

	if (!kdev) {
		pr_err("keystone-navigator-dma driver not registered\n");
		return;
	}

	if (atomic_dec_return(&chan->ref_count) <= 0)
		chan_stop(chan);

	if (atomic_dec_return(&chan->dma->ref_count) <= 0)
		knav_dma_hw_destroy(chan->dma);

	dev_dbg(kdev->dev, "channel %d or flow %d closed from DMA %s\n",
			chan->channel, chan->flow, chan->dma->name);
}
EXPORT_SYMBOL_GPL(knav_dma_close_channel);

static void __iomem *pktdma_get_regs(struct knav_dma_device *dma,
				struct device_node *node,
				unsigned index, resource_size_t *_size)
{
	struct device *dev = kdev->dev;
	struct resource res;
	void __iomem *regs;
	int ret;

	ret = of_address_to_resource(node, index, &res);
	if (ret) {
		dev_err(dev, "Can't translate of node(%pOFn) address for index(%d)\n",
			node, index);
		return ERR_PTR(ret);
	}

	regs = devm_ioremap_resource(kdev->dev, &res);
	if (IS_ERR(regs))
		dev_err(dev, "Failed to map register base for index(%d) node(%pOFn)\n",
			index, node);
	if (_size)
		*_size = resource_size(&res);

	return regs;
}

static int pktdma_init_rx_chan(struct knav_dma_chan *chan, u32 flow)
{
	struct knav_dma_device *dma = chan->dma;

	chan->flow = flow;
	chan->reg_rx_flow = dma->reg_rx_flow + flow;
	chan->channel = DMA_INVALID_ID;
	dev_dbg(kdev->dev, "rx flow(%d) (%p)\n", chan->flow, chan->reg_rx_flow);

	return 0;
}

static int pktdma_init_tx_chan(struct knav_dma_chan *chan, u32 channel)
{
	struct knav_dma_device *dma = chan->dma;

	chan->channel = channel;
	chan->reg_chan = dma->reg_tx_chan + channel;
	chan->reg_tx_sched = dma->reg_tx_sched + channel;
	chan->flow = DMA_INVALID_ID;
	dev_dbg(kdev->dev, "tx channel(%d) (%p)\n", chan->channel, chan->reg_chan);

	return 0;
}

static int pktdma_init_chan(struct knav_dma_device *dma,
				enum dma_transfer_direction dir,
				unsigned chan_num)
{
	struct device *dev = kdev->dev;
	struct knav_dma_chan *chan;
	int ret = -EINVAL;

	chan = devm_kzalloc(dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	INIT_LIST_HEAD(&chan->list);
	chan->dma	= dma;
	chan->direction	= DMA_TRANS_NONE;
	atomic_set(&chan->ref_count, 0);
	spin_lock_init(&chan->lock);

	if (dir == DMA_MEM_TO_DEV) {
		chan->direction = dir;
		ret = pktdma_init_tx_chan(chan, chan_num);
	} else if (dir == DMA_DEV_TO_MEM) {
		chan->direction = dir;
		ret = pktdma_init_rx_chan(chan, chan_num);
	} else {
		dev_err(dev, "channel(%d) direction unknown\n", chan_num);
	}

	list_add_tail(&chan->list, &dma->chan_list);

	return ret;
}

static int dma_init(struct device_node *cloud, struct device_node *dma_node)
{
	unsigned max_tx_chan, max_rx_chan, max_rx_flow, max_tx_sched;
	struct device_node *node = dma_node;
	struct knav_dma_device *dma;
	int ret, len, num_chan = 0;
	resource_size_t size;
	u32 timeout;
	u32 i;

	dma = devm_kzalloc(kdev->dev, sizeof(*dma), GFP_KERNEL);
	if (!dma) {
		dev_err(kdev->dev, "could not allocate driver mem\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&dma->list);
	INIT_LIST_HEAD(&dma->chan_list);

	if (!of_find_property(cloud, "ti,navigator-cloud-address", &len)) {
		dev_err(kdev->dev, "unspecified navigator cloud addresses\n");
		return -ENODEV;
	}

	dma->logical_queue_managers = len / sizeof(u32);
	if (dma->logical_queue_managers > DMA_MAX_QMS) {
		dev_warn(kdev->dev, "too many queue mgrs(>%d) rest ignored\n",
			 dma->logical_queue_managers);
		dma->logical_queue_managers = DMA_MAX_QMS;
	}

	ret = of_property_read_u32_array(cloud, "ti,navigator-cloud-address",
					dma->qm_base_address,
					dma->logical_queue_managers);
	if (ret) {
		dev_err(kdev->dev, "invalid navigator cloud addresses\n");
		return -ENODEV;
	}

	dma->reg_global	 = pktdma_get_regs(dma, node, 0, &size);
	if (IS_ERR(dma->reg_global))
		return PTR_ERR(dma->reg_global);
	if (size < sizeof(struct reg_global)) {
		dev_err(kdev->dev, "bad size %pa for global regs\n", &size);
		return -ENODEV;
	}

	dma->reg_tx_chan = pktdma_get_regs(dma, node, 1, &size);
	if (IS_ERR(dma->reg_tx_chan))
		return PTR_ERR(dma->reg_tx_chan);

	max_tx_chan = size / sizeof(struct reg_chan);
	dma->reg_rx_chan = pktdma_get_regs(dma, node, 2, &size);
	if (IS_ERR(dma->reg_rx_chan))
		return PTR_ERR(dma->reg_rx_chan);

	max_rx_chan = size / sizeof(struct reg_chan);
	dma->reg_tx_sched = pktdma_get_regs(dma, node, 3, &size);
	if (IS_ERR(dma->reg_tx_sched))
		return PTR_ERR(dma->reg_tx_sched);

	max_tx_sched = size / sizeof(struct reg_tx_sched);
	dma->reg_rx_flow = pktdma_get_regs(dma, node, 4, &size);
	if (IS_ERR(dma->reg_rx_flow))
		return PTR_ERR(dma->reg_rx_flow);

	max_rx_flow = size / sizeof(struct reg_rx_flow);
	dma->rx_priority = DMA_PRIO_DEFAULT;
	dma->tx_priority = DMA_PRIO_DEFAULT;

	dma->enable_all	= of_property_read_bool(node, "ti,enable-all");
	dma->loopback	= of_property_read_bool(node, "ti,loop-back");

	ret = of_property_read_u32(node, "ti,rx-retry-timeout", &timeout);
	if (ret < 0) {
		dev_dbg(kdev->dev, "unspecified rx timeout using value %d\n",
			DMA_RX_TIMEOUT_DEFAULT);
		timeout = DMA_RX_TIMEOUT_DEFAULT;
	}

	dma->rx_timeout = timeout;
	dma->max_rx_chan = max_rx_chan;
	dma->max_rx_flow = max_rx_flow;
	dma->max_tx_chan = min(max_tx_chan, max_tx_sched);
	atomic_set(&dma->ref_count, 0);
	strcpy(dma->name, node->name);
	spin_lock_init(&dma->lock);

	for (i = 0; i < dma->max_tx_chan; i++) {
		if (pktdma_init_chan(dma, DMA_MEM_TO_DEV, i) >= 0)
			num_chan++;
	}

	for (i = 0; i < dma->max_rx_flow; i++) {
		if (pktdma_init_chan(dma, DMA_DEV_TO_MEM, i) >= 0)
			num_chan++;
	}

	list_add_tail(&dma->list, &kdev->list);

	/*
	 * For DSP software usecases or userpace transport software, setup all
	 * the DMA hardware resources.
	 */
	if (dma->enable_all) {
		atomic_inc(&dma->ref_count);
		knav_dma_hw_init(dma);
		dma_hw_enable_all(dma);
	}

	dev_info(kdev->dev, "DMA %s registered %d logical channels, flows %d, tx chans: %d, rx chans: %d%s\n",
		dma->name, num_chan, dma->max_rx_flow,
		dma->max_tx_chan, dma->max_rx_chan,
		dma->loopback ? ", loopback" : "");

	return 0;
}

static int knav_dma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *child;
	int ret = 0;

	if (!node) {
		dev_err(&pdev->dev, "could not find device info\n");
		return -EINVAL;
	}

	kdev = devm_kzalloc(dev,
			sizeof(struct knav_dma_pool_device), GFP_KERNEL);
	if (!kdev) {
		dev_err(dev, "could not allocate driver mem\n");
		return -ENOMEM;
	}

	kdev->dev = dev;
	INIT_LIST_HEAD(&kdev->list);

	pm_runtime_enable(kdev->dev);
	ret = pm_runtime_resume_and_get(kdev->dev);
	if (ret < 0) {
		dev_err(kdev->dev, "unable to enable pktdma, err %d\n", ret);
		goto err_pm_disable;
	}

	/* Initialise all packet dmas */
	for_each_child_of_node(node, child) {
		ret = dma_init(node, child);
		if (ret) {
			of_node_put(child);
			dev_err(&pdev->dev, "init failed with %d\n", ret);
			break;
		}
	}

	if (list_empty(&kdev->list)) {
		dev_err(dev, "no valid dma instance\n");
		ret = -ENODEV;
		goto err_put_sync;
	}

	debugfs_create_file("knav_dma", S_IFREG | S_IRUGO, NULL, NULL,
			    &knav_dma_debug_fops);

	device_ready = true;
	return ret;

err_put_sync:
	pm_runtime_put_sync(kdev->dev);
err_pm_disable:
	pm_runtime_disable(kdev->dev);

	return ret;
}

static void knav_dma_remove(struct platform_device *pdev)
{
	struct knav_dma_device *dma;

	list_for_each_entry(dma, &kdev->list, list) {
		if (atomic_dec_return(&dma->ref_count) == 0)
			knav_dma_hw_destroy(dma);
	}

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

static struct of_device_id of_match[] = {
	{ .compatible = "ti,keystone-navigator-dma", },
	{},
};

MODULE_DEVICE_TABLE(of, of_match);

static struct platform_driver knav_dma_driver = {
	.probe	= knav_dma_probe,
	.remove_new = knav_dma_remove,
	.driver = {
		.name		= "keystone-navigator-dma",
		.of_match_table	= of_match,
	},
};
module_platform_driver(knav_dma_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI Keystone Navigator Packet DMA driver");
MODULE_AUTHOR("Sandeep Nair <sandeep_n@ti.com>");
MODULE_AUTHOR("Santosh Shilimkar <santosh.shilimkar@ti.com>");
