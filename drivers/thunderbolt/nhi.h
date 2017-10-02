/*
 * Thunderbolt Cactus Ridge driver - NHI driver
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#ifndef DSL3510_H_
#define DSL3510_H_

#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

/**
 * struct tb_nhi - thunderbolt native host interface
 * @lock: Must be held during ring creation/destruction. Is acquired by
 *	  interrupt_work when dispatching interrupts to individual rings.
 * @pdev: Pointer to the PCI device
 * @iobase: MMIO space of the NHI
 * @tx_rings: All Tx rings available on this host controller
 * @rx_rings: All Rx rings available on this host controller
 * @msix_ida: Used to allocate MSI-X vectors for rings
 * @going_away: The host controller device is about to disappear so when
 *		this flag is set, avoid touching the hardware anymore.
 * @interrupt_work: Work scheduled to handle ring interrupt when no
 *		    MSI-X is used.
 * @hop_count: Number of rings (end point hops) supported by NHI.
 */
struct tb_nhi {
	struct mutex lock;
	struct pci_dev *pdev;
	void __iomem *iobase;
	struct tb_ring **tx_rings;
	struct tb_ring **rx_rings;
	struct ida msix_ida;
	bool going_away;
	struct work_struct interrupt_work;
	u32 hop_count;
};

/**
 * struct tb_ring - thunderbolt TX or RX ring associated with a NHI
 * @lock: Lock serializing actions to this ring. Must be acquired after
 *	  nhi->lock.
 * @nhi: Pointer to the native host controller interface
 * @size: Size of the ring
 * @hop: Hop (DMA channel) associated with this ring
 * @head: Head of the ring (write next descriptor here)
 * @tail: Tail of the ring (complete next descriptor here)
 * @descriptors: Allocated descriptors for this ring
 * @queue: Queue holding frames to be transferred over this ring
 * @in_flight: Queue holding frames that are currently in flight
 * @work: Interrupt work structure
 * @is_tx: Is the ring Tx or Rx
 * @running: Is the ring running
 * @irq: MSI-X irq number if the ring uses MSI-X. %0 otherwise.
 * @vector: MSI-X vector number the ring uses (only set if @irq is > 0)
 * @flags: Ring specific flags
 */
struct tb_ring {
	struct mutex lock;
	struct tb_nhi *nhi;
	int size;
	int hop;
	int head;
	int tail;
	struct ring_desc *descriptors;
	dma_addr_t descriptors_dma;
	struct list_head queue;
	struct list_head in_flight;
	struct work_struct work;
	bool is_tx:1;
	bool running:1;
	int irq;
	u8 vector;
	unsigned int flags;
};

/* Leave ring interrupt enabled on suspend */
#define RING_FLAG_NO_SUSPEND	BIT(0)

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

struct tb_ring *ring_alloc_tx(struct tb_nhi *nhi, int hop, int size,
			      unsigned int flags);
struct tb_ring *ring_alloc_rx(struct tb_nhi *nhi, int hop, int size,
			      unsigned int flags);
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

enum nhi_fw_mode {
	NHI_FW_SAFE_MODE,
	NHI_FW_AUTH_MODE,
	NHI_FW_EP_MODE,
	NHI_FW_CM_MODE,
};

enum nhi_mailbox_cmd {
	NHI_MAILBOX_SAVE_DEVS = 0x05,
	NHI_MAILBOX_DISCONNECT_PCIE_PATHS = 0x06,
	NHI_MAILBOX_DRV_UNLOADS = 0x07,
	NHI_MAILBOX_DISCONNECT_PA = 0x10,
	NHI_MAILBOX_DISCONNECT_PB = 0x11,
	NHI_MAILBOX_ALLOW_ALL_DEVS = 0x23,
};

int nhi_mailbox_cmd(struct tb_nhi *nhi, enum nhi_mailbox_cmd cmd, u32 data);
enum nhi_fw_mode nhi_mailbox_mode(struct tb_nhi *nhi);

/*
 * PCI IDs used in this driver from Win Ridge forward. There is no
 * need for the PCI quirk anymore as we will use ICM also on Apple
 * hardware.
 */
#define PCI_DEVICE_ID_INTEL_WIN_RIDGE_2C_NHI            0x157d
#define PCI_DEVICE_ID_INTEL_WIN_RIDGE_2C_BRIDGE         0x157e
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_NHI		0x15bf
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_BRIDGE	0x15c0
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_NHI	0x15d2
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_BRIDGE	0x15d3
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_NHI	0x15d9
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_BRIDGE	0x15da
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_USBONLY_NHI	0x15dc
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_USBONLY_NHI	0x15dd
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_USBONLY_NHI	0x15de

#endif
