// SPDX-License-Identifier: GPL-2.0
/*
 * Microsemi Switchtec(tm) PCIe Management Driver
 * Copyright (c) 2019, Logan Gunthorpe <logang@deltatee.com>
 * Copyright (c) 2019, GigaIO Networks, Inc
 */

#include "dmaengine.h"

#include <linux/circ_buf.h>
#include <linux/dmaengine.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pci.h>

MODULE_DESCRIPTION("PLX ExpressLane PEX PCI Switch DMA Engine");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Logan Gunthorpe");

#define PLX_REG_DESC_RING_ADDR			0x214
#define PLX_REG_DESC_RING_ADDR_HI		0x218
#define PLX_REG_DESC_RING_NEXT_ADDR		0x21C
#define PLX_REG_DESC_RING_COUNT			0x220
#define PLX_REG_DESC_RING_LAST_ADDR		0x224
#define PLX_REG_DESC_RING_LAST_SIZE		0x228
#define PLX_REG_PREF_LIMIT			0x234
#define PLX_REG_CTRL				0x238
#define PLX_REG_CTRL2				0x23A
#define PLX_REG_INTR_CTRL			0x23C
#define PLX_REG_INTR_STATUS			0x23E

#define PLX_REG_PREF_LIMIT_PREF_FOUR		8

#define PLX_REG_CTRL_GRACEFUL_PAUSE		BIT(0)
#define PLX_REG_CTRL_ABORT			BIT(1)
#define PLX_REG_CTRL_WRITE_BACK_EN		BIT(2)
#define PLX_REG_CTRL_START			BIT(3)
#define PLX_REG_CTRL_RING_STOP_MODE		BIT(4)
#define PLX_REG_CTRL_DESC_MODE_BLOCK		(0 << 5)
#define PLX_REG_CTRL_DESC_MODE_ON_CHIP		(1 << 5)
#define PLX_REG_CTRL_DESC_MODE_OFF_CHIP		(2 << 5)
#define PLX_REG_CTRL_DESC_INVALID		BIT(8)
#define PLX_REG_CTRL_GRACEFUL_PAUSE_DONE	BIT(9)
#define PLX_REG_CTRL_ABORT_DONE			BIT(10)
#define PLX_REG_CTRL_IMM_PAUSE_DONE		BIT(12)
#define PLX_REG_CTRL_IN_PROGRESS		BIT(30)

#define PLX_REG_CTRL_RESET_VAL	(PLX_REG_CTRL_DESC_INVALID | \
				 PLX_REG_CTRL_GRACEFUL_PAUSE_DONE | \
				 PLX_REG_CTRL_ABORT_DONE | \
				 PLX_REG_CTRL_IMM_PAUSE_DONE)

#define PLX_REG_CTRL_START_VAL	(PLX_REG_CTRL_WRITE_BACK_EN | \
				 PLX_REG_CTRL_DESC_MODE_OFF_CHIP | \
				 PLX_REG_CTRL_START | \
				 PLX_REG_CTRL_RESET_VAL)

#define PLX_REG_CTRL2_MAX_TXFR_SIZE_64B		0
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_128B	1
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_256B	2
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_512B	3
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_1KB		4
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_2KB		5
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_4B		7

#define PLX_REG_INTR_CRTL_ERROR_EN		BIT(0)
#define PLX_REG_INTR_CRTL_INV_DESC_EN		BIT(1)
#define PLX_REG_INTR_CRTL_ABORT_DONE_EN		BIT(3)
#define PLX_REG_INTR_CRTL_PAUSE_DONE_EN		BIT(4)
#define PLX_REG_INTR_CRTL_IMM_PAUSE_DONE_EN	BIT(5)

#define PLX_REG_INTR_STATUS_ERROR		BIT(0)
#define PLX_REG_INTR_STATUS_INV_DESC		BIT(1)
#define PLX_REG_INTR_STATUS_DESC_DONE		BIT(2)
#define PLX_REG_INTR_CRTL_ABORT_DONE		BIT(3)

struct plx_dma_hw_std_desc {
	__le32 flags_and_size;
	__le16 dst_addr_hi;
	__le16 src_addr_hi;
	__le32 dst_addr_lo;
	__le32 src_addr_lo;
};

#define PLX_DESC_SIZE_MASK		0x7ffffff
#define PLX_DESC_FLAG_VALID		BIT(31)
#define PLX_DESC_FLAG_INT_WHEN_DONE	BIT(30)

#define PLX_DESC_WB_SUCCESS		BIT(30)
#define PLX_DESC_WB_RD_FAIL		BIT(29)
#define PLX_DESC_WB_WR_FAIL		BIT(28)

#define PLX_DMA_RING_COUNT		2048

struct plx_dma_desc {
	struct dma_async_tx_descriptor txd;
	struct plx_dma_hw_std_desc *hw;
	u32 orig_size;
};

struct plx_dma_dev {
	struct dma_device dma_dev;
	struct dma_chan dma_chan;
	struct pci_dev __rcu *pdev;
	void __iomem *bar;
	struct tasklet_struct desc_task;

	spinlock_t ring_lock;
	bool ring_active;
	int head;
	int tail;
	struct plx_dma_hw_std_desc *hw_ring;
	dma_addr_t hw_ring_dma;
	struct plx_dma_desc **desc_ring;
};

static struct plx_dma_dev *chan_to_plx_dma_dev(struct dma_chan *c)
{
	return container_of(c, struct plx_dma_dev, dma_chan);
}

static struct plx_dma_desc *to_plx_desc(struct dma_async_tx_descriptor *txd)
{
	return container_of(txd, struct plx_dma_desc, txd);
}

static struct plx_dma_desc *plx_dma_get_desc(struct plx_dma_dev *plxdev, int i)
{
	return plxdev->desc_ring[i & (PLX_DMA_RING_COUNT - 1)];
}

static void plx_dma_process_desc(struct plx_dma_dev *plxdev)
{
	struct dmaengine_result res;
	struct plx_dma_desc *desc;
	u32 flags;

	spin_lock(&plxdev->ring_lock);

	while (plxdev->tail != plxdev->head) {
		desc = plx_dma_get_desc(plxdev, plxdev->tail);

		flags = le32_to_cpu(READ_ONCE(desc->hw->flags_and_size));

		if (flags & PLX_DESC_FLAG_VALID)
			break;

		res.residue = desc->orig_size - (flags & PLX_DESC_SIZE_MASK);

		if (flags & PLX_DESC_WB_SUCCESS)
			res.result = DMA_TRANS_NOERROR;
		else if (flags & PLX_DESC_WB_WR_FAIL)
			res.result = DMA_TRANS_WRITE_FAILED;
		else
			res.result = DMA_TRANS_READ_FAILED;

		dma_cookie_complete(&desc->txd);
		dma_descriptor_unmap(&desc->txd);
		dmaengine_desc_get_callback_invoke(&desc->txd, &res);
		desc->txd.callback = NULL;
		desc->txd.callback_result = NULL;

		plxdev->tail++;
	}

	spin_unlock(&plxdev->ring_lock);
}

static void plx_dma_abort_desc(struct plx_dma_dev *plxdev)
{
	struct dmaengine_result res;
	struct plx_dma_desc *desc;

	plx_dma_process_desc(plxdev);

	spin_lock_bh(&plxdev->ring_lock);

	while (plxdev->tail != plxdev->head) {
		desc = plx_dma_get_desc(plxdev, plxdev->tail);

		res.residue = desc->orig_size;
		res.result = DMA_TRANS_ABORTED;

		dma_cookie_complete(&desc->txd);
		dma_descriptor_unmap(&desc->txd);
		dmaengine_desc_get_callback_invoke(&desc->txd, &res);
		desc->txd.callback = NULL;
		desc->txd.callback_result = NULL;

		plxdev->tail++;
	}

	spin_unlock_bh(&plxdev->ring_lock);
}

static void __plx_dma_stop(struct plx_dma_dev *plxdev)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	u32 val;

	val = readl(plxdev->bar + PLX_REG_CTRL);
	if (!(val & ~PLX_REG_CTRL_GRACEFUL_PAUSE))
		return;

	writel(PLX_REG_CTRL_RESET_VAL | PLX_REG_CTRL_GRACEFUL_PAUSE,
	       plxdev->bar + PLX_REG_CTRL);

	while (!time_after(jiffies, timeout)) {
		val = readl(plxdev->bar + PLX_REG_CTRL);
		if (val & PLX_REG_CTRL_GRACEFUL_PAUSE_DONE)
			break;

		cpu_relax();
	}

	if (!(val & PLX_REG_CTRL_GRACEFUL_PAUSE_DONE))
		dev_err(plxdev->dma_dev.dev,
			"Timeout waiting for graceful pause!\n");

	writel(PLX_REG_CTRL_RESET_VAL | PLX_REG_CTRL_GRACEFUL_PAUSE,
	       plxdev->bar + PLX_REG_CTRL);

	writel(0, plxdev->bar + PLX_REG_DESC_RING_COUNT);
	writel(0, plxdev->bar + PLX_REG_DESC_RING_ADDR);
	writel(0, plxdev->bar + PLX_REG_DESC_RING_ADDR_HI);
	writel(0, plxdev->bar + PLX_REG_DESC_RING_NEXT_ADDR);
}

static void plx_dma_stop(struct plx_dma_dev *plxdev)
{
	rcu_read_lock();
	if (!rcu_dereference(plxdev->pdev)) {
		rcu_read_unlock();
		return;
	}

	__plx_dma_stop(plxdev);

	rcu_read_unlock();
}

static void plx_dma_desc_task(struct tasklet_struct *t)
{
	struct plx_dma_dev *plxdev = from_tasklet(plxdev, t, desc_task);

	plx_dma_process_desc(plxdev);
}

static struct dma_async_tx_descriptor *plx_dma_prep_memcpy(struct dma_chan *c,
		dma_addr_t dma_dst, dma_addr_t dma_src, size_t len,
		unsigned long flags)
	__acquires(plxdev->ring_lock)
{
	struct plx_dma_dev *plxdev = chan_to_plx_dma_dev(c);
	struct plx_dma_desc *plxdesc;

	spin_lock_bh(&plxdev->ring_lock);
	if (!plxdev->ring_active)
		goto err_unlock;

	if (!CIRC_SPACE(plxdev->head, plxdev->tail, PLX_DMA_RING_COUNT))
		goto err_unlock;

	if (len > PLX_DESC_SIZE_MASK)
		goto err_unlock;

	plxdesc = plx_dma_get_desc(plxdev, plxdev->head);
	plxdev->head++;

	plxdesc->hw->dst_addr_lo = cpu_to_le32(lower_32_bits(dma_dst));
	plxdesc->hw->dst_addr_hi = cpu_to_le16(upper_32_bits(dma_dst));
	plxdesc->hw->src_addr_lo = cpu_to_le32(lower_32_bits(dma_src));
	plxdesc->hw->src_addr_hi = cpu_to_le16(upper_32_bits(dma_src));

	plxdesc->orig_size = len;

	if (flags & DMA_PREP_INTERRUPT)
		len |= PLX_DESC_FLAG_INT_WHEN_DONE;

	plxdesc->hw->flags_and_size = cpu_to_le32(len);
	plxdesc->txd.flags = flags;

	/* return with the lock held, it will be released in tx_submit */

	return &plxdesc->txd;

err_unlock:
	/*
	 * Keep sparse happy by restoring an even lock count on
	 * this lock.
	 */
	__acquire(plxdev->ring_lock);

	spin_unlock_bh(&plxdev->ring_lock);
	return NULL;
}

static dma_cookie_t plx_dma_tx_submit(struct dma_async_tx_descriptor *desc)
	__releases(plxdev->ring_lock)
{
	struct plx_dma_dev *plxdev = chan_to_plx_dma_dev(desc->chan);
	struct plx_dma_desc *plxdesc = to_plx_desc(desc);
	dma_cookie_t cookie;

	cookie = dma_cookie_assign(desc);

	/*
	 * Ensure the descriptor updates are visible to the dma device
	 * before setting the valid bit.
	 */
	wmb();

	plxdesc->hw->flags_and_size |= cpu_to_le32(PLX_DESC_FLAG_VALID);

	spin_unlock_bh(&plxdev->ring_lock);

	return cookie;
}

static enum dma_status plx_dma_tx_status(struct dma_chan *chan,
		dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct plx_dma_dev *plxdev = chan_to_plx_dma_dev(chan);
	enum dma_status ret;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	plx_dma_process_desc(plxdev);

	return dma_cookie_status(chan, cookie, txstate);
}

static void plx_dma_issue_pending(struct dma_chan *chan)
{
	struct plx_dma_dev *plxdev = chan_to_plx_dma_dev(chan);

	rcu_read_lock();
	if (!rcu_dereference(plxdev->pdev)) {
		rcu_read_unlock();
		return;
	}

	/*
	 * Ensure the valid bits are visible before starting the
	 * DMA engine.
	 */
	wmb();

	writew(PLX_REG_CTRL_START_VAL, plxdev->bar + PLX_REG_CTRL);

	rcu_read_unlock();
}

static irqreturn_t plx_dma_isr(int irq, void *devid)
{
	struct plx_dma_dev *plxdev = devid;
	u32 status;

	status = readw(plxdev->bar + PLX_REG_INTR_STATUS);

	if (!status)
		return IRQ_NONE;

	if (status & PLX_REG_INTR_STATUS_DESC_DONE && plxdev->ring_active)
		tasklet_schedule(&plxdev->desc_task);

	writew(status, plxdev->bar + PLX_REG_INTR_STATUS);

	return IRQ_HANDLED;
}

static int plx_dma_alloc_desc(struct plx_dma_dev *plxdev)
{
	struct plx_dma_desc *desc;
	int i;

	plxdev->desc_ring = kcalloc(PLX_DMA_RING_COUNT,
				    sizeof(*plxdev->desc_ring), GFP_KERNEL);
	if (!plxdev->desc_ring)
		return -ENOMEM;

	for (i = 0; i < PLX_DMA_RING_COUNT; i++) {
		desc = kzalloc(sizeof(*desc), GFP_KERNEL);
		if (!desc)
			goto free_and_exit;

		dma_async_tx_descriptor_init(&desc->txd, &plxdev->dma_chan);
		desc->txd.tx_submit = plx_dma_tx_submit;
		desc->hw = &plxdev->hw_ring[i];

		plxdev->desc_ring[i] = desc;
	}

	return 0;

free_and_exit:
	for (i = 0; i < PLX_DMA_RING_COUNT; i++)
		kfree(plxdev->desc_ring[i]);
	kfree(plxdev->desc_ring);
	return -ENOMEM;
}

static int plx_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct plx_dma_dev *plxdev = chan_to_plx_dma_dev(chan);
	size_t ring_sz = PLX_DMA_RING_COUNT * sizeof(*plxdev->hw_ring);
	int rc;

	plxdev->head = plxdev->tail = 0;
	plxdev->hw_ring = dma_alloc_coherent(plxdev->dma_dev.dev, ring_sz,
					     &plxdev->hw_ring_dma, GFP_KERNEL);
	if (!plxdev->hw_ring)
		return -ENOMEM;

	rc = plx_dma_alloc_desc(plxdev);
	if (rc)
		goto out_free_hw_ring;

	rcu_read_lock();
	if (!rcu_dereference(plxdev->pdev)) {
		rcu_read_unlock();
		rc = -ENODEV;
		goto out_free_hw_ring;
	}

	writel(PLX_REG_CTRL_RESET_VAL, plxdev->bar + PLX_REG_CTRL);
	writel(lower_32_bits(plxdev->hw_ring_dma),
	       plxdev->bar + PLX_REG_DESC_RING_ADDR);
	writel(upper_32_bits(plxdev->hw_ring_dma),
	       plxdev->bar + PLX_REG_DESC_RING_ADDR_HI);
	writel(lower_32_bits(plxdev->hw_ring_dma),
	       plxdev->bar + PLX_REG_DESC_RING_NEXT_ADDR);
	writel(PLX_DMA_RING_COUNT, plxdev->bar + PLX_REG_DESC_RING_COUNT);
	writel(PLX_REG_PREF_LIMIT_PREF_FOUR, plxdev->bar + PLX_REG_PREF_LIMIT);

	plxdev->ring_active = true;

	rcu_read_unlock();

	return PLX_DMA_RING_COUNT;

out_free_hw_ring:
	dma_free_coherent(plxdev->dma_dev.dev, ring_sz, plxdev->hw_ring,
			  plxdev->hw_ring_dma);
	return rc;
}

static void plx_dma_free_chan_resources(struct dma_chan *chan)
{
	struct plx_dma_dev *plxdev = chan_to_plx_dma_dev(chan);
	size_t ring_sz = PLX_DMA_RING_COUNT * sizeof(*plxdev->hw_ring);
	struct pci_dev *pdev;
	int irq = -1;
	int i;

	spin_lock_bh(&plxdev->ring_lock);
	plxdev->ring_active = false;
	spin_unlock_bh(&plxdev->ring_lock);

	plx_dma_stop(plxdev);

	rcu_read_lock();
	pdev = rcu_dereference(plxdev->pdev);
	if (pdev)
		irq = pci_irq_vector(pdev, 0);
	rcu_read_unlock();

	if (irq > 0)
		synchronize_irq(irq);

	tasklet_kill(&plxdev->desc_task);

	plx_dma_abort_desc(plxdev);

	for (i = 0; i < PLX_DMA_RING_COUNT; i++)
		kfree(plxdev->desc_ring[i]);

	kfree(plxdev->desc_ring);
	dma_free_coherent(plxdev->dma_dev.dev, ring_sz, plxdev->hw_ring,
			  plxdev->hw_ring_dma);

}

static void plx_dma_release(struct dma_device *dma_dev)
{
	struct plx_dma_dev *plxdev =
		container_of(dma_dev, struct plx_dma_dev, dma_dev);

	put_device(dma_dev->dev);
	kfree(plxdev);
}

static int plx_dma_create(struct pci_dev *pdev)
{
	struct plx_dma_dev *plxdev;
	struct dma_device *dma;
	struct dma_chan *chan;
	int rc;

	plxdev = kzalloc(sizeof(*plxdev), GFP_KERNEL);
	if (!plxdev)
		return -ENOMEM;

	rc = request_irq(pci_irq_vector(pdev, 0), plx_dma_isr, 0,
			 KBUILD_MODNAME, plxdev);
	if (rc)
		goto free_plx;

	spin_lock_init(&plxdev->ring_lock);
	tasklet_setup(&plxdev->desc_task, plx_dma_desc_task);

	RCU_INIT_POINTER(plxdev->pdev, pdev);
	plxdev->bar = pcim_iomap_table(pdev)[0];

	dma = &plxdev->dma_dev;
	INIT_LIST_HEAD(&dma->channels);
	dma_cap_set(DMA_MEMCPY, dma->cap_mask);
	dma->copy_align = DMAENGINE_ALIGN_1_BYTE;
	dma->dev = get_device(&pdev->dev);

	dma->device_alloc_chan_resources = plx_dma_alloc_chan_resources;
	dma->device_free_chan_resources = plx_dma_free_chan_resources;
	dma->device_prep_dma_memcpy = plx_dma_prep_memcpy;
	dma->device_issue_pending = plx_dma_issue_pending;
	dma->device_tx_status = plx_dma_tx_status;
	dma->device_release = plx_dma_release;

	chan = &plxdev->dma_chan;
	chan->device = dma;
	dma_cookie_init(chan);
	list_add_tail(&chan->device_node, &dma->channels);

	rc = dma_async_device_register(dma);
	if (rc) {
		pci_err(pdev, "Failed to register dma device: %d\n", rc);
		goto put_device;
	}

	pci_set_drvdata(pdev, plxdev);

	return 0;

put_device:
	put_device(&pdev->dev);
	free_irq(pci_irq_vector(pdev, 0),  plxdev);
free_plx:
	kfree(plxdev);

	return rc;
}

static int plx_dma_probe(struct pci_dev *pdev,
			 const struct pci_device_id *id)
{
	int rc;

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(48));
	if (rc)
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev, 1, KBUILD_MODNAME);
	if (rc)
		return rc;

	rc = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (rc <= 0)
		return rc;

	pci_set_master(pdev);

	rc = plx_dma_create(pdev);
	if (rc)
		goto err_free_irq_vectors;

	pci_info(pdev, "PLX DMA Channel Registered\n");

	return 0;

err_free_irq_vectors:
	pci_free_irq_vectors(pdev);
	return rc;
}

static void plx_dma_remove(struct pci_dev *pdev)
{
	struct plx_dma_dev *plxdev = pci_get_drvdata(pdev);

	free_irq(pci_irq_vector(pdev, 0),  plxdev);

	rcu_assign_pointer(plxdev->pdev, NULL);
	synchronize_rcu();

	spin_lock_bh(&plxdev->ring_lock);
	plxdev->ring_active = false;
	spin_unlock_bh(&plxdev->ring_lock);

	__plx_dma_stop(plxdev);
	plx_dma_abort_desc(plxdev);

	plxdev->bar = NULL;
	dma_async_device_unregister(&plxdev->dma_dev);

	pci_free_irq_vectors(pdev);
}

static const struct pci_device_id plx_dma_pci_tbl[] = {
	{
		.vendor		= PCI_VENDOR_ID_PLX,
		.device		= 0x87D0,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_SYSTEM_OTHER << 8,
		.class_mask	= 0xFFFFFFFF,
	},
	{0}
};
MODULE_DEVICE_TABLE(pci, plx_dma_pci_tbl);

static struct pci_driver plx_dma_pci_driver = {
	.name           = KBUILD_MODNAME,
	.id_table       = plx_dma_pci_tbl,
	.probe          = plx_dma_probe,
	.remove		= plx_dma_remove,
};
module_pci_driver(plx_dma_pci_driver);
