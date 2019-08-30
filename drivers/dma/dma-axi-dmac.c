// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Analog Devices AXI-DMAC core
 *
 * Copyright 2013-2015 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/dma/axi-dmac.h>

#include "dmaengine.h"
#include "virt-dma.h"

/*
 * The AXI-DMAC is a soft IP core that is used in FPGA designs. The core has
 * various instantiation parameters which decided the exact feature set support
 * by the core.
 *
 * Each channel of the core has a source interface and a destination interface.
 * The number of channels and the type of the channel interfaces is selected at
 * configuration time. A interface can either be a connected to a central memory
 * interconnect, which allows access to system memory, or it can be connected to
 * a dedicated bus which is directly connected to a data port on a peripheral.
 * Given that those are configuration options of the core that are selected when
 * it is instantiated this means that they can not be changed by software at
 * runtime. By extension this means that each channel is uni-directional. It can
 * either be device to memory or memory to device, but not both. Also since the
 * device side is a dedicated data bus only connected to a single peripheral
 * there is no address than can or needs to be configured for the device side.
 */

#define AXI_DMAC_REG_IRQ_MASK		0x80
#define AXI_DMAC_REG_IRQ_PENDING	0x84
#define AXI_DMAC_REG_IRQ_SOURCE		0x88

#define AXI_DMAC_REG_CTRL		0x400
#define AXI_DMAC_REG_TRANSFER_ID	0x404
#define AXI_DMAC_REG_START_TRANSFER	0x408
#define AXI_DMAC_REG_FLAGS		0x40c
#define AXI_DMAC_REG_DEST_ADDRESS	0x410
#define AXI_DMAC_REG_SRC_ADDRESS	0x414
#define AXI_DMAC_REG_X_LENGTH		0x418
#define AXI_DMAC_REG_Y_LENGTH		0x41c
#define AXI_DMAC_REG_DEST_STRIDE	0x420
#define AXI_DMAC_REG_SRC_STRIDE		0x424
#define AXI_DMAC_REG_TRANSFER_DONE	0x428
#define AXI_DMAC_REG_ACTIVE_TRANSFER_ID 0x42c
#define AXI_DMAC_REG_STATUS		0x430
#define AXI_DMAC_REG_CURRENT_SRC_ADDR	0x434
#define AXI_DMAC_REG_CURRENT_DEST_ADDR	0x438

#define AXI_DMAC_CTRL_ENABLE		BIT(0)
#define AXI_DMAC_CTRL_PAUSE		BIT(1)

#define AXI_DMAC_IRQ_SOT		BIT(0)
#define AXI_DMAC_IRQ_EOT		BIT(1)

#define AXI_DMAC_FLAG_CYCLIC		BIT(0)

/* The maximum ID allocated by the hardware is 31 */
#define AXI_DMAC_SG_UNUSED 32U

struct axi_dmac_sg {
	dma_addr_t src_addr;
	dma_addr_t dest_addr;
	unsigned int x_len;
	unsigned int y_len;
	unsigned int dest_stride;
	unsigned int src_stride;
	unsigned int id;
	bool schedule_when_free;
};

struct axi_dmac_desc {
	struct virt_dma_desc vdesc;
	bool cyclic;

	unsigned int num_submitted;
	unsigned int num_completed;
	unsigned int num_sgs;
	struct axi_dmac_sg sg[];
};

struct axi_dmac_chan {
	struct virt_dma_chan vchan;

	struct axi_dmac_desc *next_desc;
	struct list_head active_descs;
	enum dma_transfer_direction direction;

	unsigned int src_width;
	unsigned int dest_width;
	unsigned int src_type;
	unsigned int dest_type;

	unsigned int max_length;
	unsigned int align_mask;

	bool hw_cyclic;
	bool hw_2d;
};

struct axi_dmac {
	void __iomem *base;
	int irq;

	struct clk *clk;

	struct dma_device dma_dev;
	struct axi_dmac_chan chan;

	struct device_dma_parameters dma_parms;
};

static struct axi_dmac *chan_to_axi_dmac(struct axi_dmac_chan *chan)
{
	return container_of(chan->vchan.chan.device, struct axi_dmac,
		dma_dev);
}

static struct axi_dmac_chan *to_axi_dmac_chan(struct dma_chan *c)
{
	return container_of(c, struct axi_dmac_chan, vchan.chan);
}

static struct axi_dmac_desc *to_axi_dmac_desc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct axi_dmac_desc, vdesc);
}

static void axi_dmac_write(struct axi_dmac *axi_dmac, unsigned int reg,
	unsigned int val)
{
	writel(val, axi_dmac->base + reg);
}

static int axi_dmac_read(struct axi_dmac *axi_dmac, unsigned int reg)
{
	return readl(axi_dmac->base + reg);
}

static int axi_dmac_src_is_mem(struct axi_dmac_chan *chan)
{
	return chan->src_type == AXI_DMAC_BUS_TYPE_AXI_MM;
}

static int axi_dmac_dest_is_mem(struct axi_dmac_chan *chan)
{
	return chan->dest_type == AXI_DMAC_BUS_TYPE_AXI_MM;
}

static bool axi_dmac_check_len(struct axi_dmac_chan *chan, unsigned int len)
{
	if (len == 0)
		return false;
	if ((len & chan->align_mask) != 0) /* Not aligned */
		return false;
	return true;
}

static bool axi_dmac_check_addr(struct axi_dmac_chan *chan, dma_addr_t addr)
{
	if ((addr & chan->align_mask) != 0) /* Not aligned */
		return false;
	return true;
}

static void axi_dmac_start_transfer(struct axi_dmac_chan *chan)
{
	struct axi_dmac *dmac = chan_to_axi_dmac(chan);
	struct virt_dma_desc *vdesc;
	struct axi_dmac_desc *desc;
	struct axi_dmac_sg *sg;
	unsigned int flags = 0;
	unsigned int val;

	val = axi_dmac_read(dmac, AXI_DMAC_REG_START_TRANSFER);
	if (val) /* Queue is full, wait for the next SOT IRQ */
		return;

	desc = chan->next_desc;

	if (!desc) {
		vdesc = vchan_next_desc(&chan->vchan);
		if (!vdesc)
			return;
		list_move_tail(&vdesc->node, &chan->active_descs);
		desc = to_axi_dmac_desc(vdesc);
	}
	sg = &desc->sg[desc->num_submitted];

	/* Already queued in cyclic mode. Wait for it to finish */
	if (sg->id != AXI_DMAC_SG_UNUSED) {
		sg->schedule_when_free = true;
		return;
	}

	desc->num_submitted++;
	if (desc->num_submitted == desc->num_sgs) {
		if (desc->cyclic)
			desc->num_submitted = 0; /* Start again */
		else
			chan->next_desc = NULL;
	} else {
		chan->next_desc = desc;
	}

	sg->id = axi_dmac_read(dmac, AXI_DMAC_REG_TRANSFER_ID);

	if (axi_dmac_dest_is_mem(chan)) {
		axi_dmac_write(dmac, AXI_DMAC_REG_DEST_ADDRESS, sg->dest_addr);
		axi_dmac_write(dmac, AXI_DMAC_REG_DEST_STRIDE, sg->dest_stride);
	}

	if (axi_dmac_src_is_mem(chan)) {
		axi_dmac_write(dmac, AXI_DMAC_REG_SRC_ADDRESS, sg->src_addr);
		axi_dmac_write(dmac, AXI_DMAC_REG_SRC_STRIDE, sg->src_stride);
	}

	/*
	 * If the hardware supports cyclic transfers and there is no callback to
	 * call and only a single segment, enable hw cyclic mode to avoid
	 * unnecessary interrupts.
	 */
	if (chan->hw_cyclic && desc->cyclic && !desc->vdesc.tx.callback &&
		desc->num_sgs == 1)
		flags |= AXI_DMAC_FLAG_CYCLIC;

	axi_dmac_write(dmac, AXI_DMAC_REG_X_LENGTH, sg->x_len - 1);
	axi_dmac_write(dmac, AXI_DMAC_REG_Y_LENGTH, sg->y_len - 1);
	axi_dmac_write(dmac, AXI_DMAC_REG_FLAGS, flags);
	axi_dmac_write(dmac, AXI_DMAC_REG_START_TRANSFER, 1);
}

static struct axi_dmac_desc *axi_dmac_active_desc(struct axi_dmac_chan *chan)
{
	return list_first_entry_or_null(&chan->active_descs,
		struct axi_dmac_desc, vdesc.node);
}

static bool axi_dmac_transfer_done(struct axi_dmac_chan *chan,
	unsigned int completed_transfers)
{
	struct axi_dmac_desc *active;
	struct axi_dmac_sg *sg;
	bool start_next = false;

	active = axi_dmac_active_desc(chan);
	if (!active)
		return false;

	do {
		sg = &active->sg[active->num_completed];
		if (sg->id == AXI_DMAC_SG_UNUSED) /* Not yet submitted */
			break;
		if (!(BIT(sg->id) & completed_transfers))
			break;
		active->num_completed++;
		sg->id = AXI_DMAC_SG_UNUSED;
		if (sg->schedule_when_free) {
			sg->schedule_when_free = false;
			start_next = true;
		}

		if (active->cyclic)
			vchan_cyclic_callback(&active->vdesc);

		if (active->num_completed == active->num_sgs) {
			if (active->cyclic) {
				active->num_completed = 0; /* wrap around */
			} else {
				list_del(&active->vdesc.node);
				vchan_cookie_complete(&active->vdesc);
				active = axi_dmac_active_desc(chan);
			}
		}
	} while (active);

	return start_next;
}

static irqreturn_t axi_dmac_interrupt_handler(int irq, void *devid)
{
	struct axi_dmac *dmac = devid;
	unsigned int pending;
	bool start_next = false;

	pending = axi_dmac_read(dmac, AXI_DMAC_REG_IRQ_PENDING);
	if (!pending)
		return IRQ_NONE;

	axi_dmac_write(dmac, AXI_DMAC_REG_IRQ_PENDING, pending);

	spin_lock(&dmac->chan.vchan.lock);
	/* One or more transfers have finished */
	if (pending & AXI_DMAC_IRQ_EOT) {
		unsigned int completed;

		completed = axi_dmac_read(dmac, AXI_DMAC_REG_TRANSFER_DONE);
		start_next = axi_dmac_transfer_done(&dmac->chan, completed);
	}
	/* Space has become available in the descriptor queue */
	if ((pending & AXI_DMAC_IRQ_SOT) || start_next)
		axi_dmac_start_transfer(&dmac->chan);
	spin_unlock(&dmac->chan.vchan.lock);

	return IRQ_HANDLED;
}

static int axi_dmac_terminate_all(struct dma_chan *c)
{
	struct axi_dmac_chan *chan = to_axi_dmac_chan(c);
	struct axi_dmac *dmac = chan_to_axi_dmac(chan);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&chan->vchan.lock, flags);
	axi_dmac_write(dmac, AXI_DMAC_REG_CTRL, 0);
	chan->next_desc = NULL;
	vchan_get_all_descriptors(&chan->vchan, &head);
	list_splice_tail_init(&chan->active_descs, &head);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);

	vchan_dma_desc_free_list(&chan->vchan, &head);

	return 0;
}

static void axi_dmac_synchronize(struct dma_chan *c)
{
	struct axi_dmac_chan *chan = to_axi_dmac_chan(c);

	vchan_synchronize(&chan->vchan);
}

static void axi_dmac_issue_pending(struct dma_chan *c)
{
	struct axi_dmac_chan *chan = to_axi_dmac_chan(c);
	struct axi_dmac *dmac = chan_to_axi_dmac(chan);
	unsigned long flags;

	axi_dmac_write(dmac, AXI_DMAC_REG_CTRL, AXI_DMAC_CTRL_ENABLE);

	spin_lock_irqsave(&chan->vchan.lock, flags);
	if (vchan_issue_pending(&chan->vchan))
		axi_dmac_start_transfer(chan);
	spin_unlock_irqrestore(&chan->vchan.lock, flags);
}

static struct axi_dmac_desc *axi_dmac_alloc_desc(unsigned int num_sgs)
{
	struct axi_dmac_desc *desc;
	unsigned int i;

	desc = kzalloc(struct_size(desc, sg, num_sgs), GFP_NOWAIT);
	if (!desc)
		return NULL;

	for (i = 0; i < num_sgs; i++)
		desc->sg[i].id = AXI_DMAC_SG_UNUSED;

	desc->num_sgs = num_sgs;

	return desc;
}

static struct axi_dmac_sg *axi_dmac_fill_linear_sg(struct axi_dmac_chan *chan,
	enum dma_transfer_direction direction, dma_addr_t addr,
	unsigned int num_periods, unsigned int period_len,
	struct axi_dmac_sg *sg)
{
	unsigned int num_segments, i;
	unsigned int segment_size;
	unsigned int len;

	/* Split into multiple equally sized segments if necessary */
	num_segments = DIV_ROUND_UP(period_len, chan->max_length);
	segment_size = DIV_ROUND_UP(period_len, num_segments);
	/* Take care of alignment */
	segment_size = ((segment_size - 1) | chan->align_mask) + 1;

	for (i = 0; i < num_periods; i++) {
		len = period_len;

		while (len > segment_size) {
			if (direction == DMA_DEV_TO_MEM)
				sg->dest_addr = addr;
			else
				sg->src_addr = addr;
			sg->x_len = segment_size;
			sg->y_len = 1;
			sg++;
			addr += segment_size;
			len -= segment_size;
		}

		if (direction == DMA_DEV_TO_MEM)
			sg->dest_addr = addr;
		else
			sg->src_addr = addr;
		sg->x_len = len;
		sg->y_len = 1;
		sg++;
		addr += len;
	}

	return sg;
}

static struct dma_async_tx_descriptor *axi_dmac_prep_slave_sg(
	struct dma_chan *c, struct scatterlist *sgl,
	unsigned int sg_len, enum dma_transfer_direction direction,
	unsigned long flags, void *context)
{
	struct axi_dmac_chan *chan = to_axi_dmac_chan(c);
	struct axi_dmac_desc *desc;
	struct axi_dmac_sg *dsg;
	struct scatterlist *sg;
	unsigned int num_sgs;
	unsigned int i;

	if (direction != chan->direction)
		return NULL;

	num_sgs = 0;
	for_each_sg(sgl, sg, sg_len, i)
		num_sgs += DIV_ROUND_UP(sg_dma_len(sg), chan->max_length);

	desc = axi_dmac_alloc_desc(num_sgs);
	if (!desc)
		return NULL;

	dsg = desc->sg;

	for_each_sg(sgl, sg, sg_len, i) {
		if (!axi_dmac_check_addr(chan, sg_dma_address(sg)) ||
		    !axi_dmac_check_len(chan, sg_dma_len(sg))) {
			kfree(desc);
			return NULL;
		}

		dsg = axi_dmac_fill_linear_sg(chan, direction, sg_dma_address(sg), 1,
			sg_dma_len(sg), dsg);
	}

	desc->cyclic = false;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);
}

static struct dma_async_tx_descriptor *axi_dmac_prep_dma_cyclic(
	struct dma_chan *c, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction direction,
	unsigned long flags)
{
	struct axi_dmac_chan *chan = to_axi_dmac_chan(c);
	struct axi_dmac_desc *desc;
	unsigned int num_periods, num_segments;

	if (direction != chan->direction)
		return NULL;

	if (!axi_dmac_check_len(chan, buf_len) ||
	    !axi_dmac_check_addr(chan, buf_addr))
		return NULL;

	if (period_len == 0 || buf_len % period_len)
		return NULL;

	num_periods = buf_len / period_len;
	num_segments = DIV_ROUND_UP(period_len, chan->max_length);

	desc = axi_dmac_alloc_desc(num_periods * num_segments);
	if (!desc)
		return NULL;

	axi_dmac_fill_linear_sg(chan, direction, buf_addr, num_periods,
		period_len, desc->sg);

	desc->cyclic = true;

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);
}

static struct dma_async_tx_descriptor *axi_dmac_prep_interleaved(
	struct dma_chan *c, struct dma_interleaved_template *xt,
	unsigned long flags)
{
	struct axi_dmac_chan *chan = to_axi_dmac_chan(c);
	struct axi_dmac_desc *desc;
	size_t dst_icg, src_icg;

	if (xt->frame_size != 1)
		return NULL;

	if (xt->dir != chan->direction)
		return NULL;

	if (axi_dmac_src_is_mem(chan)) {
		if (!xt->src_inc || !axi_dmac_check_addr(chan, xt->src_start))
			return NULL;
	}

	if (axi_dmac_dest_is_mem(chan)) {
		if (!xt->dst_inc || !axi_dmac_check_addr(chan, xt->dst_start))
			return NULL;
	}

	dst_icg = dmaengine_get_dst_icg(xt, &xt->sgl[0]);
	src_icg = dmaengine_get_src_icg(xt, &xt->sgl[0]);

	if (chan->hw_2d) {
		if (!axi_dmac_check_len(chan, xt->sgl[0].size) ||
		    xt->numf == 0)
			return NULL;
		if (xt->sgl[0].size + dst_icg > chan->max_length ||
		    xt->sgl[0].size + src_icg > chan->max_length)
			return NULL;
	} else {
		if (dst_icg != 0 || src_icg != 0)
			return NULL;
		if (chan->max_length / xt->sgl[0].size < xt->numf)
			return NULL;
		if (!axi_dmac_check_len(chan, xt->sgl[0].size * xt->numf))
			return NULL;
	}

	desc = axi_dmac_alloc_desc(1);
	if (!desc)
		return NULL;

	if (axi_dmac_src_is_mem(chan)) {
		desc->sg[0].src_addr = xt->src_start;
		desc->sg[0].src_stride = xt->sgl[0].size + src_icg;
	}

	if (axi_dmac_dest_is_mem(chan)) {
		desc->sg[0].dest_addr = xt->dst_start;
		desc->sg[0].dest_stride = xt->sgl[0].size + dst_icg;
	}

	if (chan->hw_2d) {
		desc->sg[0].x_len = xt->sgl[0].size;
		desc->sg[0].y_len = xt->numf;
	} else {
		desc->sg[0].x_len = xt->sgl[0].size * xt->numf;
		desc->sg[0].y_len = 1;
	}

	return vchan_tx_prep(&chan->vchan, &desc->vdesc, flags);
}

static void axi_dmac_free_chan_resources(struct dma_chan *c)
{
	vchan_free_chan_resources(to_virt_chan(c));
}

static void axi_dmac_desc_free(struct virt_dma_desc *vdesc)
{
	kfree(container_of(vdesc, struct axi_dmac_desc, vdesc));
}

/*
 * The configuration stored in the devicetree matches the configuration
 * parameters of the peripheral instance and allows the driver to know which
 * features are implemented and how it should behave.
 */
static int axi_dmac_parse_chan_dt(struct device_node *of_chan,
	struct axi_dmac_chan *chan)
{
	u32 val;
	int ret;

	ret = of_property_read_u32(of_chan, "reg", &val);
	if (ret)
		return ret;

	/* We only support 1 channel for now */
	if (val != 0)
		return -EINVAL;

	ret = of_property_read_u32(of_chan, "adi,source-bus-type", &val);
	if (ret)
		return ret;
	if (val > AXI_DMAC_BUS_TYPE_FIFO)
		return -EINVAL;
	chan->src_type = val;

	ret = of_property_read_u32(of_chan, "adi,destination-bus-type", &val);
	if (ret)
		return ret;
	if (val > AXI_DMAC_BUS_TYPE_FIFO)
		return -EINVAL;
	chan->dest_type = val;

	ret = of_property_read_u32(of_chan, "adi,source-bus-width", &val);
	if (ret)
		return ret;
	chan->src_width = val / 8;

	ret = of_property_read_u32(of_chan, "adi,destination-bus-width", &val);
	if (ret)
		return ret;
	chan->dest_width = val / 8;

	chan->align_mask = max(chan->dest_width, chan->src_width) - 1;

	if (axi_dmac_dest_is_mem(chan) && axi_dmac_src_is_mem(chan))
		chan->direction = DMA_MEM_TO_MEM;
	else if (!axi_dmac_dest_is_mem(chan) && axi_dmac_src_is_mem(chan))
		chan->direction = DMA_MEM_TO_DEV;
	else if (axi_dmac_dest_is_mem(chan) && !axi_dmac_src_is_mem(chan))
		chan->direction = DMA_DEV_TO_MEM;
	else
		chan->direction = DMA_DEV_TO_DEV;

	return 0;
}

static void axi_dmac_detect_caps(struct axi_dmac *dmac)
{
	struct axi_dmac_chan *chan = &dmac->chan;

	axi_dmac_write(dmac, AXI_DMAC_REG_FLAGS, AXI_DMAC_FLAG_CYCLIC);
	if (axi_dmac_read(dmac, AXI_DMAC_REG_FLAGS) == AXI_DMAC_FLAG_CYCLIC)
		chan->hw_cyclic = true;

	axi_dmac_write(dmac, AXI_DMAC_REG_Y_LENGTH, 1);
	if (axi_dmac_read(dmac, AXI_DMAC_REG_Y_LENGTH) == 1)
		chan->hw_2d = true;

	axi_dmac_write(dmac, AXI_DMAC_REG_X_LENGTH, 0xffffffff);
	chan->max_length = axi_dmac_read(dmac, AXI_DMAC_REG_X_LENGTH);
	if (chan->max_length != UINT_MAX)
		chan->max_length++;
}

static int axi_dmac_probe(struct platform_device *pdev)
{
	struct device_node *of_channels, *of_chan;
	struct dma_device *dma_dev;
	struct axi_dmac *dmac;
	struct resource *res;
	int ret;

	dmac = devm_kzalloc(&pdev->dev, sizeof(*dmac), GFP_KERNEL);
	if (!dmac)
		return -ENOMEM;

	dmac->irq = platform_get_irq(pdev, 0);
	if (dmac->irq < 0)
		return dmac->irq;
	if (dmac->irq == 0)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dmac->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dmac->base))
		return PTR_ERR(dmac->base);

	dmac->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dmac->clk))
		return PTR_ERR(dmac->clk);

	INIT_LIST_HEAD(&dmac->chan.active_descs);

	of_channels = of_get_child_by_name(pdev->dev.of_node, "adi,channels");
	if (of_channels == NULL)
		return -ENODEV;

	for_each_child_of_node(of_channels, of_chan) {
		ret = axi_dmac_parse_chan_dt(of_chan, &dmac->chan);
		if (ret) {
			of_node_put(of_chan);
			of_node_put(of_channels);
			return -EINVAL;
		}
	}
	of_node_put(of_channels);

	pdev->dev.dma_parms = &dmac->dma_parms;
	dma_set_max_seg_size(&pdev->dev, UINT_MAX);

	dma_dev = &dmac->dma_dev;
	dma_cap_set(DMA_SLAVE, dma_dev->cap_mask);
	dma_cap_set(DMA_CYCLIC, dma_dev->cap_mask);
	dma_cap_set(DMA_INTERLEAVE, dma_dev->cap_mask);
	dma_dev->device_free_chan_resources = axi_dmac_free_chan_resources;
	dma_dev->device_tx_status = dma_cookie_status;
	dma_dev->device_issue_pending = axi_dmac_issue_pending;
	dma_dev->device_prep_slave_sg = axi_dmac_prep_slave_sg;
	dma_dev->device_prep_dma_cyclic = axi_dmac_prep_dma_cyclic;
	dma_dev->device_prep_interleaved_dma = axi_dmac_prep_interleaved;
	dma_dev->device_terminate_all = axi_dmac_terminate_all;
	dma_dev->device_synchronize = axi_dmac_synchronize;
	dma_dev->dev = &pdev->dev;
	dma_dev->chancnt = 1;
	dma_dev->src_addr_widths = BIT(dmac->chan.src_width);
	dma_dev->dst_addr_widths = BIT(dmac->chan.dest_width);
	dma_dev->directions = BIT(dmac->chan.direction);
	dma_dev->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	INIT_LIST_HEAD(&dma_dev->channels);

	dmac->chan.vchan.desc_free = axi_dmac_desc_free;
	vchan_init(&dmac->chan.vchan, dma_dev);

	ret = clk_prepare_enable(dmac->clk);
	if (ret < 0)
		return ret;

	axi_dmac_detect_caps(dmac);

	axi_dmac_write(dmac, AXI_DMAC_REG_IRQ_MASK, 0x00);

	ret = dma_async_device_register(dma_dev);
	if (ret)
		goto err_clk_disable;

	ret = of_dma_controller_register(pdev->dev.of_node,
		of_dma_xlate_by_chan_id, dma_dev);
	if (ret)
		goto err_unregister_device;

	ret = request_irq(dmac->irq, axi_dmac_interrupt_handler, IRQF_SHARED,
		dev_name(&pdev->dev), dmac);
	if (ret)
		goto err_unregister_of;

	platform_set_drvdata(pdev, dmac);

	return 0;

err_unregister_of:
	of_dma_controller_free(pdev->dev.of_node);
err_unregister_device:
	dma_async_device_unregister(&dmac->dma_dev);
err_clk_disable:
	clk_disable_unprepare(dmac->clk);

	return ret;
}

static int axi_dmac_remove(struct platform_device *pdev)
{
	struct axi_dmac *dmac = platform_get_drvdata(pdev);

	of_dma_controller_free(pdev->dev.of_node);
	free_irq(dmac->irq, dmac);
	tasklet_kill(&dmac->chan.vchan.task);
	dma_async_device_unregister(&dmac->dma_dev);
	clk_disable_unprepare(dmac->clk);

	return 0;
}

static const struct of_device_id axi_dmac_of_match_table[] = {
	{ .compatible = "adi,axi-dmac-1.00.a" },
	{ },
};
MODULE_DEVICE_TABLE(of, axi_dmac_of_match_table);

static struct platform_driver axi_dmac_driver = {
	.driver = {
		.name = "dma-axi-dmac",
		.of_match_table = axi_dmac_of_match_table,
	},
	.probe = axi_dmac_probe,
	.remove = axi_dmac_remove,
};
module_platform_driver(axi_dmac_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("DMA controller driver for the AXI-DMAC controller");
MODULE_LICENSE("GPL v2");
