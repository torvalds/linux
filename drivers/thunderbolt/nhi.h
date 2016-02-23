/*
 * Thunderbolt Cactus Ridge driver - NHI driver
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#ifndef DSL3510_H_
#define DSL3510_H_

#include <linux/mutex.h>
#include <linux/workqueue.h>

/**
 * struct tb_nhi - thunderbolt native host interface
 */
struct tb_nhi {
	struct mutex lock; /*
			    * Must be held during ring creation/destruction.
			    * Is acquired by interrupt_work when dispatching
			    * interrupts to individual rings.
			    **/
	struct pci_dev *pdev;
	void __iomem *iobase;
	struct tb_ring **tx_rings;
	struct tb_ring **rx_rings;
	struct work_struct interrupt_work;
	u32 hop_count; /* Number of rings (end point hops) supported by NHI. */
};

/**
 * struct tb_ring - thunderbolt TX or RX ring associated with a NHI
 */
struct tb_ring {
	struct mutex lock; /* must be acquired after nhi->lock */
	struct tb_nhi *nhi;
	int size;
	int hop;
	int head; /* write next descriptor here */
	int tail; /* complete next descriptor here */
	struct ring_desc *descriptors;
	dma_addr_t descriptors_dma;
	struct list_head queue;
	struct list_head in_flight;
	struct work_struct work;
	bool is_tx:1; /* rx otherwise */
	bool running:1;
};

struct ring_frame;
typedef void (*ring_cb)(struct tb_ring*, struct ring_frame*, bool canceled);

/**
 * struct ring_frame - for use with ring_rx/ring_tx
 */
struct ring_frame {
	dma_addr_t buffer_phy;
	ring_cb callback;
	struct list_head list;
	u32 size:12; /* TX: in, RX: out*/
	u32 flags:12; /* RX: out */
	u32 eof:4; /* TX:in, RX: out */
	u32 sof:4; /* TX:in, RX: out */
};

#define TB_FRAME_SIZE 0x100    /* minimum size for ring_rx */

struct tb_ring *ring_alloc_tx(struct tb_nhi *nhi, int hop, int size);
struct tb_ring *ring_alloc_rx(struct tb_nhi *nhi, int hop, int size);
void ring_start(struct tb_ring *ring);
void ring_stop(struct tb_ring *ring);
void ring_free(struct tb_ring *ring);

int __ring_enqueue(struct tb_ring *ring, struct ring_frame *frame);

/**
 * ring_rx() - enqueue a frame on an RX ring
 *
 * frame->buffer, frame->buffer_phy and frame->callback have to be set. The
 * buffer must contain at least TB_FRAME_SIZE bytes.
 *
 * frame->callback will be invoked with frame->size, frame->flags, frame->eof,
 * frame->sof set once the frame has been received.
 *
 * If ring_stop is called after the packet has been enqueued frame->callback
 * will be called with canceled set to true.
 *
 * Return: Returns ESHUTDOWN if ring_stop has been called. Zero otherwise.
 */
static inline int ring_rx(struct tb_ring *ring, struct ring_frame *frame)
{
	WARN_ON(ring->is_tx);
	return __ring_enqueue(ring, frame);
}

/**
 * ring_tx() - enqueue a frame on an TX ring
 *
 * frame->buffer, frame->buffer_phy, frame->callback, frame->size, frame->eof
 * and frame->sof have to be set.
 *
 * frame->callback will be invoked with once the frame has been transmitted.
 *
 * If ring_stop is called after the packet has been enqueued frame->callback
 * will be called with canceled set to true.
 *
 * Return: Returns ESHUTDOWN if ring_stop has been called. Zero otherwise.
 */
static inline int ring_tx(struct tb_ring *ring, struct ring_frame *frame)
{
	WARN_ON(!ring->is_tx);
	return __ring_enqueue(ring, frame);
}

#endif
