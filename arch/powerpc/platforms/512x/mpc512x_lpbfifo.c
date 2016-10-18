/*
 * The driver for Freescale MPC512x LocalPlus Bus FIFO
 * (called SCLPC in the Reference Manual).
 *
 * Copyright (C) 2013-2015 Alexander Popov <alex.popov@linux.com>.
 *
 * This file is released under the GPLv2.
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/mpc5121.h>
#include <asm/io.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>

#define DRV_NAME "mpc512x_lpbfifo"

struct cs_range {
	u32 csnum;
	u32 base; /* must be zero */
	u32 addr;
	u32 size;
};

static struct lpbfifo_data {
	spinlock_t lock; /* for protecting lpbfifo_data */
	phys_addr_t regs_phys;
	resource_size_t regs_size;
	struct mpc512x_lpbfifo __iomem *regs;
	int irq;
	struct cs_range *cs_ranges;
	size_t cs_n;
	struct dma_chan *chan;
	struct mpc512x_lpbfifo_request *req;
	dma_addr_t ram_bus_addr;
	bool wait_lpbfifo_irq;
	bool wait_lpbfifo_callback;
} lpbfifo;

/*
 * A data transfer from RAM to some device on LPB is finished
 * when both mpc512x_lpbfifo_irq() and mpc512x_lpbfifo_callback()
 * have been called. We execute the callback registered in
 * mpc512x_lpbfifo_request just after that.
 * But for a data transfer from some device on LPB to RAM we don't enable
 * LPBFIFO interrupt because clearing MPC512X_SCLPC_SUCCESS interrupt flag
 * automatically disables LPBFIFO reading request to the DMA controller
 * and the data transfer hangs. So the callback registered in
 * mpc512x_lpbfifo_request is executed at the end of mpc512x_lpbfifo_callback().
 */

/*
 * mpc512x_lpbfifo_irq - IRQ handler for LPB FIFO
 */
static irqreturn_t mpc512x_lpbfifo_irq(int irq, void *param)
{
	struct device *dev = (struct device *)param;
	struct mpc512x_lpbfifo_request *req = NULL;
	unsigned long flags;
	u32 status;

	spin_lock_irqsave(&lpbfifo.lock, flags);

	if (!lpbfifo.regs)
		goto end;

	req = lpbfifo.req;
	if (!req || req->dir == MPC512X_LPBFIFO_REQ_DIR_READ) {
		dev_err(dev, "bogus LPBFIFO IRQ\n");
		goto end;
	}

	status = in_be32(&lpbfifo.regs->status);
	if (status != MPC512X_SCLPC_SUCCESS) {
		dev_err(dev, "DMA transfer from RAM to peripheral failed\n");
		out_be32(&lpbfifo.regs->enable,
				MPC512X_SCLPC_RESET | MPC512X_SCLPC_FIFO_RESET);
		goto end;
	}
	/* Clear the interrupt flag */
	out_be32(&lpbfifo.regs->status, MPC512X_SCLPC_SUCCESS);

	lpbfifo.wait_lpbfifo_irq = false;

	if (lpbfifo.wait_lpbfifo_callback)
		goto end;

	/* Transfer is finished, set the FIFO as idle */
	lpbfifo.req = NULL;

	spin_unlock_irqrestore(&lpbfifo.lock, flags);

	if (req->callback)
		req->callback(req);

	return IRQ_HANDLED;

 end:
	spin_unlock_irqrestore(&lpbfifo.lock, flags);
	return IRQ_HANDLED;
}

/*
 * mpc512x_lpbfifo_callback is called by DMA driver when
 * DMA transaction is finished.
 */
static void mpc512x_lpbfifo_callback(void *param)
{
	unsigned long flags;
	struct mpc512x_lpbfifo_request *req = NULL;
	enum dma_data_direction dir;

	spin_lock_irqsave(&lpbfifo.lock, flags);

	if (!lpbfifo.regs) {
		spin_unlock_irqrestore(&lpbfifo.lock, flags);
		return;
	}

	req = lpbfifo.req;
	if (!req) {
		pr_err("bogus LPBFIFO callback\n");
		spin_unlock_irqrestore(&lpbfifo.lock, flags);
		return;
	}

	/* Release the mapping */
	if (req->dir == MPC512X_LPBFIFO_REQ_DIR_WRITE)
		dir = DMA_TO_DEVICE;
	else
		dir = DMA_FROM_DEVICE;
	dma_unmap_single(lpbfifo.chan->device->dev,
			lpbfifo.ram_bus_addr, req->size, dir);

	lpbfifo.wait_lpbfifo_callback = false;

	if (!lpbfifo.wait_lpbfifo_irq) {
		/* Transfer is finished, set the FIFO as idle */
		lpbfifo.req = NULL;

		spin_unlock_irqrestore(&lpbfifo.lock, flags);

		if (req->callback)
			req->callback(req);
	} else {
		spin_unlock_irqrestore(&lpbfifo.lock, flags);
	}
}

static int mpc512x_lpbfifo_kick(void)
{
	u32 bits;
	bool no_incr = false;
	u32 bpt = 32; /* max bytes per LPBFIFO transaction involving DMA */
	u32 cs = 0;
	size_t i;
	struct dma_device *dma_dev = NULL;
	struct scatterlist sg;
	enum dma_data_direction dir;
	struct dma_slave_config dma_conf = {};
	struct dma_async_tx_descriptor *dma_tx = NULL;
	dma_cookie_t cookie;
	int ret;

	/*
	 * 1. Fit the requirements:
	 * - the packet size must be a multiple of 4 since FIFO Data Word
	 *    Register allows only full-word access according the Reference
	 *    Manual;
	 * - the physical address of the device on LPB and the packet size
	 *    must be aligned on BPT (bytes per transaction) or 8-bytes
	 *    boundary according the Reference Manual;
	 * - but we choose DMA maxburst equal (or very close to) BPT to prevent
	 *    DMA controller from overtaking FIFO and causing FIFO underflow
	 *    error. So we force the packet size to be aligned on BPT boundary
	 *    not to confuse DMA driver which requires the packet size to be
	 *    aligned on maxburst boundary;
	 * - BPT should be set to the LPB device port size for operation with
	 *    disabled auto-incrementing according Reference Manual.
	 */
	if (lpbfifo.req->size == 0 || !IS_ALIGNED(lpbfifo.req->size, 4))
		return -EINVAL;

	if (lpbfifo.req->portsize != LPB_DEV_PORTSIZE_UNDEFINED) {
		bpt = lpbfifo.req->portsize;
		no_incr = true;
	}

	while (bpt > 1) {
		if (IS_ALIGNED(lpbfifo.req->dev_phys_addr, min(bpt, 0x8u)) &&
					IS_ALIGNED(lpbfifo.req->size, bpt)) {
			break;
		}

		if (no_incr)
			return -EINVAL;

		bpt >>= 1;
	}
	dma_conf.dst_maxburst = max(bpt, 0x4u) / 4;
	dma_conf.src_maxburst = max(bpt, 0x4u) / 4;

	for (i = 0; i < lpbfifo.cs_n; i++) {
		phys_addr_t cs_start = lpbfifo.cs_ranges[i].addr;
		phys_addr_t cs_end = cs_start + lpbfifo.cs_ranges[i].size;
		phys_addr_t access_start = lpbfifo.req->dev_phys_addr;
		phys_addr_t access_end = access_start + lpbfifo.req->size;

		if (access_start >= cs_start && access_end <= cs_end) {
			cs = lpbfifo.cs_ranges[i].csnum;
			break;
		}
	}
	if (i == lpbfifo.cs_n)
		return -EFAULT;

	/* 2. Prepare DMA */
	dma_dev = lpbfifo.chan->device;

	if (lpbfifo.req->dir == MPC512X_LPBFIFO_REQ_DIR_WRITE) {
		dir = DMA_TO_DEVICE;
		dma_conf.direction = DMA_MEM_TO_DEV;
		dma_conf.dst_addr = lpbfifo.regs_phys +
				offsetof(struct mpc512x_lpbfifo, data_word);
	} else {
		dir = DMA_FROM_DEVICE;
		dma_conf.direction = DMA_DEV_TO_MEM;
		dma_conf.src_addr = lpbfifo.regs_phys +
				offsetof(struct mpc512x_lpbfifo, data_word);
	}
	dma_conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dma_conf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

	/* Make DMA channel work with LPB FIFO data register */
	if (dma_dev->device_config(lpbfifo.chan, &dma_conf)) {
		ret = -EINVAL;
		goto err_dma_prep;
	}

	sg_init_table(&sg, 1);

	sg_dma_address(&sg) = dma_map_single(dma_dev->dev,
			lpbfifo.req->ram_virt_addr, lpbfifo.req->size, dir);
	if (dma_mapping_error(dma_dev->dev, sg_dma_address(&sg)))
		return -EFAULT;

	lpbfifo.ram_bus_addr = sg_dma_address(&sg); /* For freeing later */

	sg_dma_len(&sg) = lpbfifo.req->size;

	dma_tx = dmaengine_prep_slave_sg(lpbfifo.chan, &sg,
						1, dma_conf.direction, 0);
	if (!dma_tx) {
		ret = -ENOSPC;
		goto err_dma_prep;
	}
	dma_tx->callback = mpc512x_lpbfifo_callback;
	dma_tx->callback_param = NULL;

	/* 3. Prepare FIFO */
	out_be32(&lpbfifo.regs->enable,
				MPC512X_SCLPC_RESET | MPC512X_SCLPC_FIFO_RESET);
	out_be32(&lpbfifo.regs->enable, 0x0);

	/*
	 * Configure the watermarks for write operation (RAM->DMA->FIFO->dev):
	 * - high watermark 7 words according the Reference Manual,
	 * - low watermark 512 bytes (half of the FIFO).
	 * These watermarks don't work for read operation since the
	 * MPC512X_SCLPC_FLUSH bit is set (according the Reference Manual).
	 */
	out_be32(&lpbfifo.regs->fifo_ctrl, MPC512X_SCLPC_FIFO_CTRL(0x7));
	out_be32(&lpbfifo.regs->fifo_alarm, MPC512X_SCLPC_FIFO_ALARM(0x200));

	/*
	 * Start address is a physical address of the region which belongs
	 * to the device on the LocalPlus Bus
	 */
	out_be32(&lpbfifo.regs->start_addr, lpbfifo.req->dev_phys_addr);

	/*
	 * Configure chip select, transfer direction, address increment option
	 * and bytes per transaction option
	 */
	bits = MPC512X_SCLPC_CS(cs);
	if (lpbfifo.req->dir == MPC512X_LPBFIFO_REQ_DIR_READ)
		bits |= MPC512X_SCLPC_READ | MPC512X_SCLPC_FLUSH;
	if (no_incr)
		bits |= MPC512X_SCLPC_DAI;
	bits |= MPC512X_SCLPC_BPT(bpt);
	out_be32(&lpbfifo.regs->ctrl, bits);

	/* Unmask irqs */
	bits = MPC512X_SCLPC_ENABLE | MPC512X_SCLPC_ABORT_INT_ENABLE;
	if (lpbfifo.req->dir == MPC512X_LPBFIFO_REQ_DIR_WRITE)
		bits |= MPC512X_SCLPC_NORM_INT_ENABLE;
	else
		lpbfifo.wait_lpbfifo_irq = false;

	out_be32(&lpbfifo.regs->enable, bits);

	/* 4. Set packet size and kick FIFO off */
	bits = lpbfifo.req->size | MPC512X_SCLPC_START;
	out_be32(&lpbfifo.regs->pkt_size, bits);

	/* 5. Finally kick DMA off */
	cookie = dma_tx->tx_submit(dma_tx);
	if (dma_submit_error(cookie)) {
		ret = -ENOSPC;
		goto err_dma_submit;
	}

	return 0;

 err_dma_submit:
	out_be32(&lpbfifo.regs->enable,
				MPC512X_SCLPC_RESET | MPC512X_SCLPC_FIFO_RESET);
 err_dma_prep:
	dma_unmap_single(dma_dev->dev, sg_dma_address(&sg),
						lpbfifo.req->size, dir);
	return ret;
}

static int mpc512x_lpbfifo_submit_locked(struct mpc512x_lpbfifo_request *req)
{
	int ret = 0;

	if (!lpbfifo.regs)
		return -ENODEV;

	/* Check whether a transfer is in progress */
	if (lpbfifo.req)
		return -EBUSY;

	lpbfifo.wait_lpbfifo_irq = true;
	lpbfifo.wait_lpbfifo_callback = true;
	lpbfifo.req = req;

	ret = mpc512x_lpbfifo_kick();
	if (ret != 0)
		lpbfifo.req = NULL; /* Set the FIFO as idle */

	return ret;
}

int mpc512x_lpbfifo_submit(struct mpc512x_lpbfifo_request *req)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&lpbfifo.lock, flags);
	ret = mpc512x_lpbfifo_submit_locked(req);
	spin_unlock_irqrestore(&lpbfifo.lock, flags);

	return ret;
}
EXPORT_SYMBOL(mpc512x_lpbfifo_submit);

/*
 * LPBFIFO driver uses "ranges" property of "localbus" device tree node
 * for being able to determine the chip select number of a client device
 * ordering a DMA transfer.
 */
static int get_cs_ranges(struct device *dev)
{
	int ret = -ENODEV;
	struct device_node *lb_node;
	const u32 *addr_cells_p;
	const u32 *size_cells_p;
	int proplen;
	size_t i;

	lb_node = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-localbus");
	if (!lb_node)
		return ret;

	/*
	 * The node defined as compatible with 'fsl,mpc5121-localbus'
	 * should have two address cells and one size cell.
	 * Every item of its ranges property should consist of:
	 * - the first address cell which is the chipselect number;
	 * - the second address cell which is the offset in the chipselect,
	 *    must be zero.
	 * - CPU address of the beginning of an access window;
	 * - the only size cell which is the size of an access window.
	 */
	addr_cells_p = of_get_property(lb_node, "#address-cells", NULL);
	size_cells_p = of_get_property(lb_node, "#size-cells", NULL);
	if (addr_cells_p == NULL || *addr_cells_p != 2 ||
				size_cells_p == NULL ||	*size_cells_p != 1) {
		goto end;
	}

	proplen = of_property_count_u32_elems(lb_node, "ranges");
	if (proplen <= 0 || proplen % 4 != 0)
		goto end;

	lpbfifo.cs_n = proplen / 4;
	lpbfifo.cs_ranges = devm_kcalloc(dev, lpbfifo.cs_n,
					sizeof(struct cs_range), GFP_KERNEL);
	if (!lpbfifo.cs_ranges)
		goto end;

	if (of_property_read_u32_array(lb_node, "ranges",
				(u32 *)lpbfifo.cs_ranges, proplen) != 0) {
		goto end;
	}

	for (i = 0; i < lpbfifo.cs_n; i++) {
		if (lpbfifo.cs_ranges[i].base != 0)
			goto end;
	}

	ret = 0;

 end:
	of_node_put(lb_node);
	return ret;
}

static int mpc512x_lpbfifo_probe(struct platform_device *pdev)
{
	struct resource r;
	int ret = 0;

	memset(&lpbfifo, 0, sizeof(struct lpbfifo_data));
	spin_lock_init(&lpbfifo.lock);

	lpbfifo.chan = dma_request_slave_channel(&pdev->dev, "rx-tx");
	if (lpbfifo.chan == NULL)
		return -EPROBE_DEFER;

	if (of_address_to_resource(pdev->dev.of_node, 0, &r) != 0) {
		dev_err(&pdev->dev, "bad 'reg' in 'sclpc' device tree node\n");
		ret = -ENODEV;
		goto err0;
	}

	lpbfifo.regs_phys = r.start;
	lpbfifo.regs_size = resource_size(&r);

	if (!devm_request_mem_region(&pdev->dev, lpbfifo.regs_phys,
					lpbfifo.regs_size, DRV_NAME)) {
		dev_err(&pdev->dev, "unable to request region\n");
		ret = -EBUSY;
		goto err0;
	}

	lpbfifo.regs = devm_ioremap(&pdev->dev,
					lpbfifo.regs_phys, lpbfifo.regs_size);
	if (!lpbfifo.regs) {
		dev_err(&pdev->dev, "mapping registers failed\n");
		ret = -ENOMEM;
		goto err0;
	}

	out_be32(&lpbfifo.regs->enable,
				MPC512X_SCLPC_RESET | MPC512X_SCLPC_FIFO_RESET);

	if (get_cs_ranges(&pdev->dev) != 0) {
		dev_err(&pdev->dev, "bad '/localbus' device tree node\n");
		ret = -ENODEV;
		goto err0;
	}

	lpbfifo.irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (!lpbfifo.irq) {
		dev_err(&pdev->dev, "mapping irq failed\n");
		ret = -ENODEV;
		goto err0;
	}

	if (request_irq(lpbfifo.irq, mpc512x_lpbfifo_irq, 0,
						DRV_NAME, &pdev->dev) != 0) {
		dev_err(&pdev->dev, "requesting irq failed\n");
		ret = -ENODEV;
		goto err1;
	}

	dev_info(&pdev->dev, "probe succeeded\n");
	return 0;

 err1:
	irq_dispose_mapping(lpbfifo.irq);
 err0:
	dma_release_channel(lpbfifo.chan);
	return ret;
}

static int mpc512x_lpbfifo_remove(struct platform_device *pdev)
{
	unsigned long flags;
	struct dma_device *dma_dev = lpbfifo.chan->device;
	struct mpc512x_lpbfifo __iomem *regs = NULL;

	spin_lock_irqsave(&lpbfifo.lock, flags);
	regs = lpbfifo.regs;
	lpbfifo.regs = NULL;
	spin_unlock_irqrestore(&lpbfifo.lock, flags);

	dma_dev->device_terminate_all(lpbfifo.chan);
	out_be32(&regs->enable, MPC512X_SCLPC_RESET | MPC512X_SCLPC_FIFO_RESET);

	free_irq(lpbfifo.irq, &pdev->dev);
	irq_dispose_mapping(lpbfifo.irq);
	dma_release_channel(lpbfifo.chan);

	return 0;
}

static const struct of_device_id mpc512x_lpbfifo_match[] = {
	{ .compatible = "fsl,mpc512x-lpbfifo", },
	{},
};
MODULE_DEVICE_TABLE(of, mpc512x_lpbfifo_match);

static struct platform_driver mpc512x_lpbfifo_driver = {
	.probe = mpc512x_lpbfifo_probe,
	.remove = mpc512x_lpbfifo_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = mpc512x_lpbfifo_match,
	},
};

module_platform_driver(mpc512x_lpbfifo_driver);

MODULE_AUTHOR("Alexander Popov <alex.popov@linux.com>");
MODULE_DESCRIPTION("MPC512x LocalPlus Bus FIFO device driver");
MODULE_LICENSE("GPL v2");
