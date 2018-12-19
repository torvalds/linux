/*
 * Thunderbolt Cactus Ridge driver - NHI driver
 *
 * The NHI (native host interface) is the pci device that allows us to send and
 * receive frames from the thunderbolt bus.
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dmi.h>

#include "nhi.h"
#include "nhi_regs.h"
#include "tb.h"

#define RING_TYPE(ring) ((ring)->is_tx ? "TX ring" : "RX ring")


static int ring_interrupt_index(struct tb_ring *ring)
{
	int bit = ring->hop;
	if (!ring->is_tx)
		bit += ring->nhi->hop_count;
	return bit;
}

/**
 * ring_interrupt_active() - activate/deactivate interrupts for a single ring
 *
 * ring->nhi->lock must be held.
 */
static void ring_interrupt_active(struct tb_ring *ring, bool active)
{
	int reg = REG_RING_INTERRUPT_BASE + ring_interrupt_index(ring) / 32;
	int bit = ring_interrupt_index(ring) & 31;
	int mask = 1 << bit;
	u32 old, new;
	old = ioread32(ring->nhi->iobase + reg);
	if (active)
		new = old | mask;
	else
		new = old & ~mask;

	dev_info(&ring->nhi->pdev->dev,
		 "%s interrupt at register %#x bit %d (%#x -> %#x)\n",
		 active ? "enabling" : "disabling", reg, bit, old, new);

	if (new == old)
		dev_WARN(&ring->nhi->pdev->dev,
					 "interrupt for %s %d is already %s\n",
					 RING_TYPE(ring), ring->hop,
					 active ? "enabled" : "disabled");
	iowrite32(new, ring->nhi->iobase + reg);
}

/**
 * nhi_disable_interrupts() - disable interrupts for all rings
 *
 * Use only during init and shutdown.
 */
static void nhi_disable_interrupts(struct tb_nhi *nhi)
{
	int i = 0;
	/* disable interrupts */
	for (i = 0; i < RING_INTERRUPT_REG_COUNT(nhi); i++)
		iowrite32(0, nhi->iobase + REG_RING_INTERRUPT_BASE + 4 * i);

	/* clear interrupt status bits */
	for (i = 0; i < RING_NOTIFY_REG_COUNT(nhi); i++)
		ioread32(nhi->iobase + REG_RING_NOTIFY_BASE + 4 * i);
}

/* ring helper methods */

static void __iomem *ring_desc_base(struct tb_ring *ring)
{
	void __iomem *io = ring->nhi->iobase;
	io += ring->is_tx ? REG_TX_RING_BASE : REG_RX_RING_BASE;
	io += ring->hop * 16;
	return io;
}

static void __iomem *ring_options_base(struct tb_ring *ring)
{
	void __iomem *io = ring->nhi->iobase;
	io += ring->is_tx ? REG_TX_OPTIONS_BASE : REG_RX_OPTIONS_BASE;
	io += ring->hop * 32;
	return io;
}

static void ring_iowrite16desc(struct tb_ring *ring, u32 value, u32 offset)
{
	iowrite16(value, ring_desc_base(ring) + offset);
}

static void ring_iowrite32desc(struct tb_ring *ring, u32 value, u32 offset)
{
	iowrite32(value, ring_desc_base(ring) + offset);
}

static void ring_iowrite64desc(struct tb_ring *ring, u64 value, u32 offset)
{
	iowrite32(value, ring_desc_base(ring) + offset);
	iowrite32(value >> 32, ring_desc_base(ring) + offset + 4);
}

static void ring_iowrite32options(struct tb_ring *ring, u32 value, u32 offset)
{
	iowrite32(value, ring_options_base(ring) + offset);
}

static bool ring_full(struct tb_ring *ring)
{
	return ((ring->head + 1) % ring->size) == ring->tail;
}

static bool ring_empty(struct tb_ring *ring)
{
	return ring->head == ring->tail;
}

/**
 * ring_write_descriptors() - post frames from ring->queue to the controller
 *
 * ring->lock is held.
 */
static void ring_write_descriptors(struct tb_ring *ring)
{
	struct ring_frame *frame, *n;
	struct ring_desc *descriptor;
	list_for_each_entry_safe(frame, n, &ring->queue, list) {
		if (ring_full(ring))
			break;
		list_move_tail(&frame->list, &ring->in_flight);
		descriptor = &ring->descriptors[ring->head];
		descriptor->phys = frame->buffer_phy;
		descriptor->time = 0;
		descriptor->flags = RING_DESC_POSTED | RING_DESC_INTERRUPT;
		if (ring->is_tx) {
			descriptor->length = frame->size;
			descriptor->eof = frame->eof;
			descriptor->sof = frame->sof;
		}
		ring->head = (ring->head + 1) % ring->size;
		ring_iowrite16desc(ring, ring->head, ring->is_tx ? 10 : 8);
	}
}

/**
 * ring_work() - progress completed frames
 *
 * If the ring is shutting down then all frames are marked as canceled and
 * their callbacks are invoked.
 *
 * Otherwise we collect all completed frame from the ring buffer, write new
 * frame to the ring buffer and invoke the callbacks for the completed frames.
 */
static void ring_work(struct work_struct *work)
{
	struct tb_ring *ring = container_of(work, typeof(*ring), work);
	struct ring_frame *frame;
	bool canceled = false;
	LIST_HEAD(done);
	mutex_lock(&ring->lock);

	if (!ring->running) {
		/*  Move all frames to done and mark them as canceled. */
		list_splice_tail_init(&ring->in_flight, &done);
		list_splice_tail_init(&ring->queue, &done);
		canceled = true;
		goto invoke_callback;
	}

	while (!ring_empty(ring)) {
		if (!(ring->descriptors[ring->tail].flags
				& RING_DESC_COMPLETED))
			break;
		frame = list_first_entry(&ring->in_flight, typeof(*frame),
					 list);
		list_move_tail(&frame->list, &done);
		if (!ring->is_tx) {
			frame->size = ring->descriptors[ring->tail].length;
			frame->eof = ring->descriptors[ring->tail].eof;
			frame->sof = ring->descriptors[ring->tail].sof;
			frame->flags = ring->descriptors[ring->tail].flags;
			if (frame->sof != 0)
				dev_WARN(&ring->nhi->pdev->dev,
					 "%s %d got unexpected SOF: %#x\n",
					 RING_TYPE(ring), ring->hop,
					 frame->sof);
			/*
			 * known flags:
			 * raw not enabled, interupt not set: 0x2=0010
			 * raw enabled: 0xa=1010
			 * raw not enabled: 0xb=1011
			 * partial frame (>MAX_FRAME_SIZE): 0xe=1110
			 */
			if (frame->flags != 0xa)
				dev_WARN(&ring->nhi->pdev->dev,
					 "%s %d got unexpected flags: %#x\n",
					 RING_TYPE(ring), ring->hop,
					 frame->flags);
		}
		ring->tail = (ring->tail + 1) % ring->size;
	}
	ring_write_descriptors(ring);

invoke_callback:
	mutex_unlock(&ring->lock); /* allow callbacks to schedule new work */
	while (!list_empty(&done)) {
		frame = list_first_entry(&done, typeof(*frame), list);
		/*
		 * The callback may reenqueue or delete frame.
		 * Do not hold on to it.
		 */
		list_del_init(&frame->list);
		frame->callback(ring, frame, canceled);
	}
}

int __ring_enqueue(struct tb_ring *ring, struct ring_frame *frame)
{
	int ret = 0;
	mutex_lock(&ring->lock);
	if (ring->running) {
		list_add_tail(&frame->list, &ring->queue);
		ring_write_descriptors(ring);
	} else {
		ret = -ESHUTDOWN;
	}
	mutex_unlock(&ring->lock);
	return ret;
}

static struct tb_ring *ring_alloc(struct tb_nhi *nhi, u32 hop, int size,
				  bool transmit)
{
	struct tb_ring *ring = NULL;
	dev_info(&nhi->pdev->dev, "allocating %s ring %d of size %d\n",
		 transmit ? "TX" : "RX", hop, size);

	mutex_lock(&nhi->lock);
	if (hop >= nhi->hop_count) {
		dev_WARN(&nhi->pdev->dev, "invalid hop: %d\n", hop);
		goto err;
	}
	if (transmit && nhi->tx_rings[hop]) {
		dev_WARN(&nhi->pdev->dev, "TX hop %d already allocated\n", hop);
		goto err;
	} else if (!transmit && nhi->rx_rings[hop]) {
		dev_WARN(&nhi->pdev->dev, "RX hop %d already allocated\n", hop);
		goto err;
	}
	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		goto err;

	mutex_init(&ring->lock);
	INIT_LIST_HEAD(&ring->queue);
	INIT_LIST_HEAD(&ring->in_flight);
	INIT_WORK(&ring->work, ring_work);

	ring->nhi = nhi;
	ring->hop = hop;
	ring->is_tx = transmit;
	ring->size = size;
	ring->head = 0;
	ring->tail = 0;
	ring->running = false;
	ring->descriptors = dma_alloc_coherent(&ring->nhi->pdev->dev,
			size * sizeof(*ring->descriptors),
			&ring->descriptors_dma, GFP_KERNEL | __GFP_ZERO);
	if (!ring->descriptors)
		goto err;

	if (transmit)
		nhi->tx_rings[hop] = ring;
	else
		nhi->rx_rings[hop] = ring;
	mutex_unlock(&nhi->lock);
	return ring;

err:
	if (ring)
		mutex_destroy(&ring->lock);
	kfree(ring);
	mutex_unlock(&nhi->lock);
	return NULL;
}

struct tb_ring *ring_alloc_tx(struct tb_nhi *nhi, int hop, int size)
{
	return ring_alloc(nhi, hop, size, true);
}

struct tb_ring *ring_alloc_rx(struct tb_nhi *nhi, int hop, int size)
{
	return ring_alloc(nhi, hop, size, false);
}

/**
 * ring_start() - enable a ring
 *
 * Must not be invoked in parallel with ring_stop().
 */
void ring_start(struct tb_ring *ring)
{
	mutex_lock(&ring->nhi->lock);
	mutex_lock(&ring->lock);
	if (ring->running) {
		dev_WARN(&ring->nhi->pdev->dev, "ring already started\n");
		goto err;
	}
	dev_info(&ring->nhi->pdev->dev, "starting %s %d\n",
		 RING_TYPE(ring), ring->hop);

	ring_iowrite64desc(ring, ring->descriptors_dma, 0);
	if (ring->is_tx) {
		ring_iowrite32desc(ring, ring->size, 12);
		ring_iowrite32options(ring, 0, 4); /* time releated ? */
		ring_iowrite32options(ring,
				      RING_FLAG_ENABLE | RING_FLAG_RAW, 0);
	} else {
		ring_iowrite32desc(ring,
				   (TB_FRAME_SIZE << 16) | ring->size, 12);
		ring_iowrite32options(ring, 0xffffffff, 4); /* SOF EOF mask */
		ring_iowrite32options(ring,
				      RING_FLAG_ENABLE | RING_FLAG_RAW, 0);
	}
	ring_interrupt_active(ring, true);
	ring->running = true;
err:
	mutex_unlock(&ring->lock);
	mutex_unlock(&ring->nhi->lock);
}


/**
 * ring_stop() - shutdown a ring
 *
 * Must not be invoked from a callback.
 *
 * This method will disable the ring. Further calls to ring_tx/ring_rx will
 * return -ESHUTDOWN until ring_stop has been called.
 *
 * All enqueued frames will be canceled and their callbacks will be executed
 * with frame->canceled set to true (on the callback thread). This method
 * returns only after all callback invocations have finished.
 */
void ring_stop(struct tb_ring *ring)
{
	mutex_lock(&ring->nhi->lock);
	mutex_lock(&ring->lock);
	dev_info(&ring->nhi->pdev->dev, "stopping %s %d\n",
		 RING_TYPE(ring), ring->hop);
	if (!ring->running) {
		dev_WARN(&ring->nhi->pdev->dev, "%s %d already stopped\n",
			 RING_TYPE(ring), ring->hop);
		goto err;
	}
	ring_interrupt_active(ring, false);

	ring_iowrite32options(ring, 0, 0);
	ring_iowrite64desc(ring, 0, 0);
	ring_iowrite16desc(ring, 0, ring->is_tx ? 10 : 8);
	ring_iowrite32desc(ring, 0, 12);
	ring->head = 0;
	ring->tail = 0;
	ring->running = false;

err:
	mutex_unlock(&ring->lock);
	mutex_unlock(&ring->nhi->lock);

	/*
	 * schedule ring->work to invoke callbacks on all remaining frames.
	 */
	schedule_work(&ring->work);
	flush_work(&ring->work);
}

/*
 * ring_free() - free ring
 *
 * When this method returns all invocations of ring->callback will have
 * finished.
 *
 * Ring must be stopped.
 *
 * Must NOT be called from ring_frame->callback!
 */
void ring_free(struct tb_ring *ring)
{
	mutex_lock(&ring->nhi->lock);
	/*
	 * Dissociate the ring from the NHI. This also ensures that
	 * nhi_interrupt_work cannot reschedule ring->work.
	 */
	if (ring->is_tx)
		ring->nhi->tx_rings[ring->hop] = NULL;
	else
		ring->nhi->rx_rings[ring->hop] = NULL;

	if (ring->running) {
		dev_WARN(&ring->nhi->pdev->dev, "%s %d still running\n",
			 RING_TYPE(ring), ring->hop);
	}

	dma_free_coherent(&ring->nhi->pdev->dev,
			  ring->size * sizeof(*ring->descriptors),
			  ring->descriptors, ring->descriptors_dma);

	ring->descriptors = NULL;
	ring->descriptors_dma = 0;


	dev_info(&ring->nhi->pdev->dev,
		 "freeing %s %d\n",
		 RING_TYPE(ring),
		 ring->hop);

	mutex_unlock(&ring->nhi->lock);
	/**
	 * ring->work can no longer be scheduled (it is scheduled only by
	 * nhi_interrupt_work and ring_stop). Wait for it to finish before
	 * freeing the ring.
	 */
	flush_work(&ring->work);
	mutex_destroy(&ring->lock);
	kfree(ring);
}

static void nhi_interrupt_work(struct work_struct *work)
{
	struct tb_nhi *nhi = container_of(work, typeof(*nhi), interrupt_work);
	int value = 0; /* Suppress uninitialized usage warning. */
	int bit;
	int hop = -1;
	int type = 0; /* current interrupt type 0: TX, 1: RX, 2: RX overflow */
	struct tb_ring *ring;

	mutex_lock(&nhi->lock);

	/*
	 * Starting at REG_RING_NOTIFY_BASE there are three status bitfields
	 * (TX, RX, RX overflow). We iterate over the bits and read a new
	 * dwords as required. The registers are cleared on read.
	 */
	for (bit = 0; bit < 3 * nhi->hop_count; bit++) {
		if (bit % 32 == 0)
			value = ioread32(nhi->iobase
					 + REG_RING_NOTIFY_BASE
					 + 4 * (bit / 32));
		if (++hop == nhi->hop_count) {
			hop = 0;
			type++;
		}
		if ((value & (1 << (bit % 32))) == 0)
			continue;
		if (type == 2) {
			dev_warn(&nhi->pdev->dev,
				 "RX overflow for ring %d\n",
				 hop);
			continue;
		}
		if (type == 0)
			ring = nhi->tx_rings[hop];
		else
			ring = nhi->rx_rings[hop];
		if (ring == NULL) {
			dev_warn(&nhi->pdev->dev,
				 "got interrupt for inactive %s ring %d\n",
				 type ? "RX" : "TX",
				 hop);
			continue;
		}
		/* we do not check ring->running, this is done in ring->work */
		schedule_work(&ring->work);
	}
	mutex_unlock(&nhi->lock);
}

static irqreturn_t nhi_msi(int irq, void *data)
{
	struct tb_nhi *nhi = data;
	schedule_work(&nhi->interrupt_work);
	return IRQ_HANDLED;
}

static int nhi_suspend_noirq(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct tb *tb = pci_get_drvdata(pdev);
	thunderbolt_suspend(tb);
	return 0;
}

static int nhi_resume_noirq(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct tb *tb = pci_get_drvdata(pdev);
	thunderbolt_resume(tb);
	return 0;
}

static void nhi_shutdown(struct tb_nhi *nhi)
{
	int i;
	dev_info(&nhi->pdev->dev, "shutdown\n");

	for (i = 0; i < nhi->hop_count; i++) {
		if (nhi->tx_rings[i])
			dev_WARN(&nhi->pdev->dev,
				 "TX ring %d is still active\n", i);
		if (nhi->rx_rings[i])
			dev_WARN(&nhi->pdev->dev,
				 "RX ring %d is still active\n", i);
	}
	nhi_disable_interrupts(nhi);
	/*
	 * We have to release the irq before calling flush_work. Otherwise an
	 * already executing IRQ handler could call schedule_work again.
	 */
	devm_free_irq(&nhi->pdev->dev, nhi->pdev->irq, nhi);
	flush_work(&nhi->interrupt_work);
	mutex_destroy(&nhi->lock);
}

static int nhi_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct tb_nhi *nhi;
	struct tb *tb;
	int res;

	res = pcim_enable_device(pdev);
	if (res) {
		dev_err(&pdev->dev, "cannot enable PCI device, aborting\n");
		return res;
	}

	res = pci_enable_msi(pdev);
	if (res) {
		dev_err(&pdev->dev, "cannot enable MSI, aborting\n");
		return res;
	}

	res = pcim_iomap_regions(pdev, 1 << 0, "thunderbolt");
	if (res) {
		dev_err(&pdev->dev, "cannot obtain PCI resources, aborting\n");
		return res;
	}

	nhi = devm_kzalloc(&pdev->dev, sizeof(*nhi), GFP_KERNEL);
	if (!nhi)
		return -ENOMEM;

	nhi->pdev = pdev;
	/* cannot fail - table is allocated bin pcim_iomap_regions */
	nhi->iobase = pcim_iomap_table(pdev)[0];
	nhi->hop_count = ioread32(nhi->iobase + REG_HOP_COUNT) & 0x3ff;
	if (nhi->hop_count != 12)
		dev_warn(&pdev->dev, "unexpected hop count: %d\n",
			 nhi->hop_count);
	INIT_WORK(&nhi->interrupt_work, nhi_interrupt_work);

	nhi->tx_rings = devm_kcalloc(&pdev->dev, nhi->hop_count,
				     sizeof(*nhi->tx_rings), GFP_KERNEL);
	nhi->rx_rings = devm_kcalloc(&pdev->dev, nhi->hop_count,
				     sizeof(*nhi->rx_rings), GFP_KERNEL);
	if (!nhi->tx_rings || !nhi->rx_rings)
		return -ENOMEM;

	nhi_disable_interrupts(nhi); /* In case someone left them on. */
	res = devm_request_irq(&pdev->dev, pdev->irq, nhi_msi,
			       IRQF_NO_SUSPEND, /* must work during _noirq */
			       "thunderbolt", nhi);
	if (res) {
		dev_err(&pdev->dev, "request_irq failed, aborting\n");
		return res;
	}

	mutex_init(&nhi->lock);

	pci_set_master(pdev);

	/* magic value - clock related? */
	iowrite32(3906250 / 10000, nhi->iobase + 0x38c00);

	dev_info(&nhi->pdev->dev, "NHI initialized, starting thunderbolt\n");
	tb = thunderbolt_alloc_and_start(nhi);
	if (!tb) {
		/*
		 * At this point the RX/TX rings might already have been
		 * activated. Do a proper shutdown.
		 */
		nhi_shutdown(nhi);
		return -EIO;
	}
	pci_set_drvdata(pdev, tb);

	return 0;
}

static void nhi_remove(struct pci_dev *pdev)
{
	struct tb *tb = pci_get_drvdata(pdev);
	struct tb_nhi *nhi = tb->nhi;
	thunderbolt_shutdown_and_free(tb);
	nhi_shutdown(nhi);
}

/*
 * The tunneled pci bridges are siblings of us. Use resume_noirq to reenable
 * the tunnels asap. A corresponding pci quirk blocks the downstream bridges
 * resume_noirq until we are done.
 */
static const struct dev_pm_ops nhi_pm_ops = {
	.suspend_noirq = nhi_suspend_noirq,
	.resume_noirq = nhi_resume_noirq,
	.freeze_noirq = nhi_suspend_noirq, /*
					    * we just disable hotplug, the
					    * pci-tunnels stay alive.
					    */
	.thaw_noirq = nhi_resume_noirq,
	.restore_noirq = nhi_resume_noirq,
};

static struct pci_device_id nhi_ids[] = {
	/*
	 * We have to specify class, the TB bridges use the same device and
	 * vendor (sub)id.
	 */
	{
		.class = PCI_CLASS_SYSTEM_OTHER << 8, .class_mask = ~0,
		.vendor = PCI_VENDOR_ID_INTEL, .device = 0x1547,
		.subvendor = 0x2222, .subdevice = 0x1111,
	},
	{
		.class = PCI_CLASS_SYSTEM_OTHER << 8, .class_mask = ~0,
		.vendor = PCI_VENDOR_ID_INTEL, .device = 0x156c,
		.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID,
	},
	{ 0,}
};

MODULE_DEVICE_TABLE(pci, nhi_ids);
MODULE_LICENSE("GPL");

static struct pci_driver nhi_driver = {
	.name = "thunderbolt",
	.id_table = nhi_ids,
	.probe = nhi_probe,
	.remove = nhi_remove,
	.driver.pm = &nhi_pm_ops,
};

static int __init nhi_init(void)
{
	if (!dmi_match(DMI_BOARD_VENDOR, "Apple Inc."))
		return -ENOSYS;
	return pci_register_driver(&nhi_driver);
}

static void __exit nhi_unload(void)
{
	pci_unregister_driver(&nhi_driver);
}

module_init(nhi_init);
module_exit(nhi_unload);
