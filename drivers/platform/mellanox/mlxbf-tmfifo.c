// SPDX-License-Identifier: GPL-2.0+
/*
 * Mellanox BlueField SoC TmFifo driver
 *
 * Copyright (C) 2019 Mellanox Technologies
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/circ_buf.h>
#include <linux/efi.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <linux/virtio_config.h>
#include <linux/virtio_console.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_net.h>
#include <linux/virtio_ring.h>

#include "mlxbf-tmfifo-regs.h"

/* Vring size. */
#define MLXBF_TMFIFO_VRING_SIZE			SZ_1K

/* Console Tx buffer size. */
#define MLXBF_TMFIFO_CON_TX_BUF_SIZE		SZ_32K

/* Console Tx buffer reserved space. */
#define MLXBF_TMFIFO_CON_TX_BUF_RSV_SIZE	8

/* House-keeping timer interval. */
#define MLXBF_TMFIFO_TIMER_INTERVAL		(HZ / 10)

/* Virtual devices sharing the TM FIFO. */
#define MLXBF_TMFIFO_VDEV_MAX		(VIRTIO_ID_CONSOLE + 1)

/*
 * Reserve 1/16 of TmFifo space, so console messages are not starved by
 * the networking traffic.
 */
#define MLXBF_TMFIFO_RESERVE_RATIO		16

/* Message with data needs at least two words (for header & data). */
#define MLXBF_TMFIFO_DATA_MIN_WORDS		2

struct mlxbf_tmfifo;

/**
 * mlxbf_tmfifo_vring - Structure of the TmFifo virtual ring
 * @va: virtual address of the ring
 * @dma: dma address of the ring
 * @vq: pointer to the virtio virtqueue
 * @desc: current descriptor of the pending packet
 * @desc_head: head descriptor of the pending packet
 * @cur_len: processed length of the current descriptor
 * @rem_len: remaining length of the pending packet
 * @pkt_len: total length of the pending packet
 * @next_avail: next avail descriptor id
 * @num: vring size (number of descriptors)
 * @align: vring alignment size
 * @index: vring index
 * @vdev_id: vring virtio id (VIRTIO_ID_xxx)
 * @fifo: pointer to the tmfifo structure
 */
struct mlxbf_tmfifo_vring {
	void *va;
	dma_addr_t dma;
	struct virtqueue *vq;
	struct vring_desc *desc;
	struct vring_desc *desc_head;
	int cur_len;
	int rem_len;
	u32 pkt_len;
	u16 next_avail;
	int num;
	int align;
	int index;
	int vdev_id;
	struct mlxbf_tmfifo *fifo;
};

/* Interrupt types. */
enum {
	MLXBF_TM_RX_LWM_IRQ,
	MLXBF_TM_RX_HWM_IRQ,
	MLXBF_TM_TX_LWM_IRQ,
	MLXBF_TM_TX_HWM_IRQ,
	MLXBF_TM_MAX_IRQ
};

/* Ring types (Rx & Tx). */
enum {
	MLXBF_TMFIFO_VRING_RX,
	MLXBF_TMFIFO_VRING_TX,
	MLXBF_TMFIFO_VRING_MAX
};

/**
 * mlxbf_tmfifo_vdev - Structure of the TmFifo virtual device
 * @vdev: virtio device, in which the vdev.id.device field has the
 *        VIRTIO_ID_xxx id to distinguish the virtual device.
 * @status: status of the device
 * @features: supported features of the device
 * @vrings: array of tmfifo vrings of this device
 * @config.cons: virtual console config -
 *               select if vdev.id.device is VIRTIO_ID_CONSOLE
 * @config.net: virtual network config -
 *              select if vdev.id.device is VIRTIO_ID_NET
 * @tx_buf: tx buffer used to buffer data before writing into the FIFO
 */
struct mlxbf_tmfifo_vdev {
	struct virtio_device vdev;
	u8 status;
	u64 features;
	struct mlxbf_tmfifo_vring vrings[MLXBF_TMFIFO_VRING_MAX];
	union {
		struct virtio_console_config cons;
		struct virtio_net_config net;
	} config;
	struct circ_buf tx_buf;
};

/**
 * mlxbf_tmfifo_irq_info - Structure of the interrupt information
 * @fifo: pointer to the tmfifo structure
 * @irq: interrupt number
 * @index: index into the interrupt array
 */
struct mlxbf_tmfifo_irq_info {
	struct mlxbf_tmfifo *fifo;
	int irq;
	int index;
};

/**
 * mlxbf_tmfifo - Structure of the TmFifo
 * @vdev: array of the virtual devices running over the TmFifo
 * @lock: lock to protect the TmFifo access
 * @rx_base: mapped register base address for the Rx FIFO
 * @tx_base: mapped register base address for the Tx FIFO
 * @rx_fifo_size: number of entries of the Rx FIFO
 * @tx_fifo_size: number of entries of the Tx FIFO
 * @pend_events: pending bits for deferred events
 * @irq_info: interrupt information
 * @work: work struct for deferred process
 * @timer: background timer
 * @vring: Tx/Rx ring
 * @spin_lock: Tx/Rx spin lock
 * @is_ready: ready flag
 */
struct mlxbf_tmfifo {
	struct mlxbf_tmfifo_vdev *vdev[MLXBF_TMFIFO_VDEV_MAX];
	struct mutex lock;		/* TmFifo lock */
	void __iomem *rx_base;
	void __iomem *tx_base;
	int rx_fifo_size;
	int tx_fifo_size;
	unsigned long pend_events;
	struct mlxbf_tmfifo_irq_info irq_info[MLXBF_TM_MAX_IRQ];
	struct work_struct work;
	struct timer_list timer;
	struct mlxbf_tmfifo_vring *vring[2];
	spinlock_t spin_lock[2];	/* spin lock */
	bool is_ready;
};

/**
 * mlxbf_tmfifo_msg_hdr - Structure of the TmFifo message header
 * @type: message type
 * @len: payload length in network byte order. Messages sent into the FIFO
 *       will be read by the other side as data stream in the same byte order.
 *       The length needs to be encoded into network order so both sides
 *       could understand it.
 */
struct mlxbf_tmfifo_msg_hdr {
	u8 type;
	__be16 len;
	u8 unused[5];
} __packed __aligned(sizeof(u64));

/*
 * Default MAC.
 * This MAC address will be read from EFI persistent variable if configured.
 * It can also be reconfigured with standard Linux tools.
 */
static u8 mlxbf_tmfifo_net_default_mac[ETH_ALEN] = {
	0x00, 0x1A, 0xCA, 0xFF, 0xFF, 0x01
};

/* EFI variable name of the MAC address. */
static efi_char16_t mlxbf_tmfifo_efi_name[] = L"RshimMacAddr";

/* Maximum L2 header length. */
#define MLXBF_TMFIFO_NET_L2_OVERHEAD	36

/* Supported virtio-net features. */
#define MLXBF_TMFIFO_NET_FEATURES \
	(BIT_ULL(VIRTIO_NET_F_MTU) | BIT_ULL(VIRTIO_NET_F_STATUS) | \
	 BIT_ULL(VIRTIO_NET_F_MAC))

#define mlxbf_vdev_to_tmfifo(d) container_of(d, struct mlxbf_tmfifo_vdev, vdev)

/* Free vrings of the FIFO device. */
static void mlxbf_tmfifo_free_vrings(struct mlxbf_tmfifo *fifo,
				     struct mlxbf_tmfifo_vdev *tm_vdev)
{
	struct mlxbf_tmfifo_vring *vring;
	int i, size;

	for (i = 0; i < ARRAY_SIZE(tm_vdev->vrings); i++) {
		vring = &tm_vdev->vrings[i];
		if (vring->va) {
			size = vring_size(vring->num, vring->align);
			dma_free_coherent(tm_vdev->vdev.dev.parent, size,
					  vring->va, vring->dma);
			vring->va = NULL;
			if (vring->vq) {
				vring_del_virtqueue(vring->vq);
				vring->vq = NULL;
			}
		}
	}
}

/* Allocate vrings for the FIFO. */
static int mlxbf_tmfifo_alloc_vrings(struct mlxbf_tmfifo *fifo,
				     struct mlxbf_tmfifo_vdev *tm_vdev)
{
	struct mlxbf_tmfifo_vring *vring;
	struct device *dev;
	dma_addr_t dma;
	int i, size;
	void *va;

	for (i = 0; i < ARRAY_SIZE(tm_vdev->vrings); i++) {
		vring = &tm_vdev->vrings[i];
		vring->fifo = fifo;
		vring->num = MLXBF_TMFIFO_VRING_SIZE;
		vring->align = SMP_CACHE_BYTES;
		vring->index = i;
		vring->vdev_id = tm_vdev->vdev.id.device;
		dev = &tm_vdev->vdev.dev;

		size = vring_size(vring->num, vring->align);
		va = dma_alloc_coherent(dev->parent, size, &dma, GFP_KERNEL);
		if (!va) {
			mlxbf_tmfifo_free_vrings(fifo, tm_vdev);
			dev_err(dev->parent, "dma_alloc_coherent failed\n");
			return -ENOMEM;
		}

		vring->va = va;
		vring->dma = dma;
	}

	return 0;
}

/* Disable interrupts of the FIFO device. */
static void mlxbf_tmfifo_disable_irqs(struct mlxbf_tmfifo *fifo)
{
	int i, irq;

	for (i = 0; i < MLXBF_TM_MAX_IRQ; i++) {
		irq = fifo->irq_info[i].irq;
		fifo->irq_info[i].irq = 0;
		disable_irq(irq);
	}
}

/* Interrupt handler. */
static irqreturn_t mlxbf_tmfifo_irq_handler(int irq, void *arg)
{
	struct mlxbf_tmfifo_irq_info *irq_info = arg;

	if (!test_and_set_bit(irq_info->index, &irq_info->fifo->pend_events))
		schedule_work(&irq_info->fifo->work);

	return IRQ_HANDLED;
}

/* Get the next packet descriptor from the vring. */
static struct vring_desc *
mlxbf_tmfifo_get_next_desc(struct mlxbf_tmfifo_vring *vring)
{
	const struct vring *vr = virtqueue_get_vring(vring->vq);
	struct virtio_device *vdev = vring->vq->vdev;
	unsigned int idx, head;

	if (vring->next_avail == virtio16_to_cpu(vdev, vr->avail->idx))
		return NULL;

	idx = vring->next_avail % vr->num;
	head = virtio16_to_cpu(vdev, vr->avail->ring[idx]);
	if (WARN_ON(head >= vr->num))
		return NULL;

	vring->next_avail++;

	return &vr->desc[head];
}

/* Release virtio descriptor. */
static void mlxbf_tmfifo_release_desc(struct mlxbf_tmfifo_vring *vring,
				      struct vring_desc *desc, u32 len)
{
	const struct vring *vr = virtqueue_get_vring(vring->vq);
	struct virtio_device *vdev = vring->vq->vdev;
	u16 idx, vr_idx;

	vr_idx = virtio16_to_cpu(vdev, vr->used->idx);
	idx = vr_idx % vr->num;
	vr->used->ring[idx].id = cpu_to_virtio32(vdev, desc - vr->desc);
	vr->used->ring[idx].len = cpu_to_virtio32(vdev, len);

	/*
	 * Virtio could poll and check the 'idx' to decide whether the desc is
	 * done or not. Add a memory barrier here to make sure the update above
	 * completes before updating the idx.
	 */
	mb();
	vr->used->idx = cpu_to_virtio16(vdev, vr_idx + 1);
}

/* Get the total length of the descriptor chain. */
static u32 mlxbf_tmfifo_get_pkt_len(struct mlxbf_tmfifo_vring *vring,
				    struct vring_desc *desc)
{
	const struct vring *vr = virtqueue_get_vring(vring->vq);
	struct virtio_device *vdev = vring->vq->vdev;
	u32 len = 0, idx;

	while (desc) {
		len += virtio32_to_cpu(vdev, desc->len);
		if (!(virtio16_to_cpu(vdev, desc->flags) & VRING_DESC_F_NEXT))
			break;
		idx = virtio16_to_cpu(vdev, desc->next);
		desc = &vr->desc[idx];
	}

	return len;
}

static void mlxbf_tmfifo_release_pending_pkt(struct mlxbf_tmfifo_vring *vring)
{
	struct vring_desc *desc_head;
	u32 len = 0;

	if (vring->desc_head) {
		desc_head = vring->desc_head;
		len = vring->pkt_len;
	} else {
		desc_head = mlxbf_tmfifo_get_next_desc(vring);
		len = mlxbf_tmfifo_get_pkt_len(vring, desc_head);
	}

	if (desc_head)
		mlxbf_tmfifo_release_desc(vring, desc_head, len);

	vring->pkt_len = 0;
	vring->desc = NULL;
	vring->desc_head = NULL;
}

static void mlxbf_tmfifo_init_net_desc(struct mlxbf_tmfifo_vring *vring,
				       struct vring_desc *desc, bool is_rx)
{
	struct virtio_device *vdev = vring->vq->vdev;
	struct virtio_net_hdr *net_hdr;

	net_hdr = phys_to_virt(virtio64_to_cpu(vdev, desc->addr));
	memset(net_hdr, 0, sizeof(*net_hdr));
}

/* Get and initialize the next packet. */
static struct vring_desc *
mlxbf_tmfifo_get_next_pkt(struct mlxbf_tmfifo_vring *vring, bool is_rx)
{
	struct vring_desc *desc;

	desc = mlxbf_tmfifo_get_next_desc(vring);
	if (desc && is_rx && vring->vdev_id == VIRTIO_ID_NET)
		mlxbf_tmfifo_init_net_desc(vring, desc, is_rx);

	vring->desc_head = desc;
	vring->desc = desc;

	return desc;
}

/* House-keeping timer. */
static void mlxbf_tmfifo_timer(struct timer_list *t)
{
	struct mlxbf_tmfifo *fifo = container_of(t, struct mlxbf_tmfifo, timer);
	int rx, tx;

	rx = !test_and_set_bit(MLXBF_TM_RX_HWM_IRQ, &fifo->pend_events);
	tx = !test_and_set_bit(MLXBF_TM_TX_LWM_IRQ, &fifo->pend_events);

	if (rx || tx)
		schedule_work(&fifo->work);

	mod_timer(&fifo->timer, jiffies + MLXBF_TMFIFO_TIMER_INTERVAL);
}

/* Copy one console packet into the output buffer. */
static void mlxbf_tmfifo_console_output_one(struct mlxbf_tmfifo_vdev *cons,
					    struct mlxbf_tmfifo_vring *vring,
					    struct vring_desc *desc)
{
	const struct vring *vr = virtqueue_get_vring(vring->vq);
	struct virtio_device *vdev = &cons->vdev;
	u32 len, idx, seg;
	void *addr;

	while (desc) {
		addr = phys_to_virt(virtio64_to_cpu(vdev, desc->addr));
		len = virtio32_to_cpu(vdev, desc->len);

		seg = CIRC_SPACE_TO_END(cons->tx_buf.head, cons->tx_buf.tail,
					MLXBF_TMFIFO_CON_TX_BUF_SIZE);
		if (len <= seg) {
			memcpy(cons->tx_buf.buf + cons->tx_buf.head, addr, len);
		} else {
			memcpy(cons->tx_buf.buf + cons->tx_buf.head, addr, seg);
			addr += seg;
			memcpy(cons->tx_buf.buf, addr, len - seg);
		}
		cons->tx_buf.head = (cons->tx_buf.head + len) %
			MLXBF_TMFIFO_CON_TX_BUF_SIZE;

		if (!(virtio16_to_cpu(vdev, desc->flags) & VRING_DESC_F_NEXT))
			break;
		idx = virtio16_to_cpu(vdev, desc->next);
		desc = &vr->desc[idx];
	}
}

/* Copy console data into the output buffer. */
static void mlxbf_tmfifo_console_output(struct mlxbf_tmfifo_vdev *cons,
					struct mlxbf_tmfifo_vring *vring)
{
	struct vring_desc *desc;
	u32 len, avail;

	desc = mlxbf_tmfifo_get_next_desc(vring);
	while (desc) {
		/* Release the packet if not enough space. */
		len = mlxbf_tmfifo_get_pkt_len(vring, desc);
		avail = CIRC_SPACE(cons->tx_buf.head, cons->tx_buf.tail,
				   MLXBF_TMFIFO_CON_TX_BUF_SIZE);
		if (len + MLXBF_TMFIFO_CON_TX_BUF_RSV_SIZE > avail) {
			mlxbf_tmfifo_release_desc(vring, desc, len);
			break;
		}

		mlxbf_tmfifo_console_output_one(cons, vring, desc);
		mlxbf_tmfifo_release_desc(vring, desc, len);
		desc = mlxbf_tmfifo_get_next_desc(vring);
	}
}

/* Get the number of available words in Rx FIFO for receiving. */
static int mlxbf_tmfifo_get_rx_avail(struct mlxbf_tmfifo *fifo)
{
	u64 sts;

	sts = readq(fifo->rx_base + MLXBF_TMFIFO_RX_STS);
	return FIELD_GET(MLXBF_TMFIFO_RX_STS__COUNT_MASK, sts);
}

/* Get the number of available words in the TmFifo for sending. */
static int mlxbf_tmfifo_get_tx_avail(struct mlxbf_tmfifo *fifo, int vdev_id)
{
	int tx_reserve;
	u32 count;
	u64 sts;

	/* Reserve some room in FIFO for console messages. */
	if (vdev_id == VIRTIO_ID_NET)
		tx_reserve = fifo->tx_fifo_size / MLXBF_TMFIFO_RESERVE_RATIO;
	else
		tx_reserve = 1;

	sts = readq(fifo->tx_base + MLXBF_TMFIFO_TX_STS);
	count = FIELD_GET(MLXBF_TMFIFO_TX_STS__COUNT_MASK, sts);
	return fifo->tx_fifo_size - tx_reserve - count;
}

/* Console Tx (move data from the output buffer into the TmFifo). */
static void mlxbf_tmfifo_console_tx(struct mlxbf_tmfifo *fifo, int avail)
{
	struct mlxbf_tmfifo_msg_hdr hdr;
	struct mlxbf_tmfifo_vdev *cons;
	unsigned long flags;
	int size, seg;
	void *addr;
	u64 data;

	/* Return if not enough space available. */
	if (avail < MLXBF_TMFIFO_DATA_MIN_WORDS)
		return;

	cons = fifo->vdev[VIRTIO_ID_CONSOLE];
	if (!cons || !cons->tx_buf.buf)
		return;

	/* Return if no data to send. */
	size = CIRC_CNT(cons->tx_buf.head, cons->tx_buf.tail,
			MLXBF_TMFIFO_CON_TX_BUF_SIZE);
	if (size == 0)
		return;

	/* Adjust the size to available space. */
	if (size + sizeof(hdr) > avail * sizeof(u64))
		size = avail * sizeof(u64) - sizeof(hdr);

	/* Write header. */
	hdr.type = VIRTIO_ID_CONSOLE;
	hdr.len = htons(size);
	writeq(*(u64 *)&hdr, fifo->tx_base + MLXBF_TMFIFO_TX_DATA);

	/* Use spin-lock to protect the 'cons->tx_buf'. */
	spin_lock_irqsave(&fifo->spin_lock[0], flags);

	while (size > 0) {
		addr = cons->tx_buf.buf + cons->tx_buf.tail;

		seg = CIRC_CNT_TO_END(cons->tx_buf.head, cons->tx_buf.tail,
				      MLXBF_TMFIFO_CON_TX_BUF_SIZE);
		if (seg >= sizeof(u64)) {
			memcpy(&data, addr, sizeof(u64));
		} else {
			memcpy(&data, addr, seg);
			memcpy((u8 *)&data + seg, cons->tx_buf.buf,
			       sizeof(u64) - seg);
		}
		writeq(data, fifo->tx_base + MLXBF_TMFIFO_TX_DATA);

		if (size >= sizeof(u64)) {
			cons->tx_buf.tail = (cons->tx_buf.tail + sizeof(u64)) %
				MLXBF_TMFIFO_CON_TX_BUF_SIZE;
			size -= sizeof(u64);
		} else {
			cons->tx_buf.tail = (cons->tx_buf.tail + size) %
				MLXBF_TMFIFO_CON_TX_BUF_SIZE;
			size = 0;
		}
	}

	spin_unlock_irqrestore(&fifo->spin_lock[0], flags);
}

/* Rx/Tx one word in the descriptor buffer. */
static void mlxbf_tmfifo_rxtx_word(struct mlxbf_tmfifo_vring *vring,
				   struct vring_desc *desc,
				   bool is_rx, int len)
{
	struct virtio_device *vdev = vring->vq->vdev;
	struct mlxbf_tmfifo *fifo = vring->fifo;
	void *addr;
	u64 data;

	/* Get the buffer address of this desc. */
	addr = phys_to_virt(virtio64_to_cpu(vdev, desc->addr));

	/* Read a word from FIFO for Rx. */
	if (is_rx)
		data = readq(fifo->rx_base + MLXBF_TMFIFO_RX_DATA);

	if (vring->cur_len + sizeof(u64) <= len) {
		/* The whole word. */
		if (is_rx)
			memcpy(addr + vring->cur_len, &data, sizeof(u64));
		else
			memcpy(&data, addr + vring->cur_len, sizeof(u64));
		vring->cur_len += sizeof(u64);
	} else {
		/* Leftover bytes. */
		if (is_rx)
			memcpy(addr + vring->cur_len, &data,
			       len - vring->cur_len);
		else
			memcpy(&data, addr + vring->cur_len,
			       len - vring->cur_len);
		vring->cur_len = len;
	}

	/* Write the word into FIFO for Tx. */
	if (!is_rx)
		writeq(data, fifo->tx_base + MLXBF_TMFIFO_TX_DATA);
}

/*
 * Rx/Tx packet header.
 *
 * In Rx case, the packet might be found to belong to a different vring since
 * the TmFifo is shared by different services. In such case, the 'vring_change'
 * flag is set.
 */
static void mlxbf_tmfifo_rxtx_header(struct mlxbf_tmfifo_vring *vring,
				     struct vring_desc *desc,
				     bool is_rx, bool *vring_change)
{
	struct mlxbf_tmfifo *fifo = vring->fifo;
	struct virtio_net_config *config;
	struct mlxbf_tmfifo_msg_hdr hdr;
	int vdev_id, hdr_len;

	/* Read/Write packet header. */
	if (is_rx) {
		/* Drain one word from the FIFO. */
		*(u64 *)&hdr = readq(fifo->rx_base + MLXBF_TMFIFO_RX_DATA);

		/* Skip the length 0 packets (keepalive). */
		if (hdr.len == 0)
			return;

		/* Check packet type. */
		if (hdr.type == VIRTIO_ID_NET) {
			vdev_id = VIRTIO_ID_NET;
			hdr_len = sizeof(struct virtio_net_hdr);
			config = &fifo->vdev[vdev_id]->config.net;
			/* A legacy-only interface for now. */
			if (ntohs(hdr.len) >
			    __virtio16_to_cpu(virtio_legacy_is_little_endian(),
					      config->mtu) +
			    MLXBF_TMFIFO_NET_L2_OVERHEAD)
				return;
		} else {
			vdev_id = VIRTIO_ID_CONSOLE;
			hdr_len = 0;
		}

		/*
		 * Check whether the new packet still belongs to this vring.
		 * If not, update the pkt_len of the new vring.
		 */
		if (vdev_id != vring->vdev_id) {
			struct mlxbf_tmfifo_vdev *tm_dev2 = fifo->vdev[vdev_id];

			if (!tm_dev2)
				return;
			vring->desc = desc;
			vring = &tm_dev2->vrings[MLXBF_TMFIFO_VRING_RX];
			*vring_change = true;
		}
		vring->pkt_len = ntohs(hdr.len) + hdr_len;
	} else {
		/* Network virtio has an extra header. */
		hdr_len = (vring->vdev_id == VIRTIO_ID_NET) ?
			   sizeof(struct virtio_net_hdr) : 0;
		vring->pkt_len = mlxbf_tmfifo_get_pkt_len(vring, desc);
		hdr.type = (vring->vdev_id == VIRTIO_ID_NET) ?
			    VIRTIO_ID_NET : VIRTIO_ID_CONSOLE;
		hdr.len = htons(vring->pkt_len - hdr_len);
		writeq(*(u64 *)&hdr, fifo->tx_base + MLXBF_TMFIFO_TX_DATA);
	}

	vring->cur_len = hdr_len;
	vring->rem_len = vring->pkt_len;
	fifo->vring[is_rx] = vring;
}

/*
 * Rx/Tx one descriptor.
 *
 * Return true to indicate more data available.
 */
static bool mlxbf_tmfifo_rxtx_one_desc(struct mlxbf_tmfifo_vring *vring,
				       bool is_rx, int *avail)
{
	const struct vring *vr = virtqueue_get_vring(vring->vq);
	struct mlxbf_tmfifo *fifo = vring->fifo;
	struct virtio_device *vdev;
	bool vring_change = false;
	struct vring_desc *desc;
	unsigned long flags;
	u32 len, idx;

	vdev = &fifo->vdev[vring->vdev_id]->vdev;

	/* Get the descriptor of the next packet. */
	if (!vring->desc) {
		desc = mlxbf_tmfifo_get_next_pkt(vring, is_rx);
		if (!desc)
			return false;
	} else {
		desc = vring->desc;
	}

	/* Beginning of a packet. Start to Rx/Tx packet header. */
	if (vring->pkt_len == 0) {
		mlxbf_tmfifo_rxtx_header(vring, desc, is_rx, &vring_change);
		(*avail)--;

		/* Return if new packet is for another ring. */
		if (vring_change)
			return false;
		goto mlxbf_tmfifo_desc_done;
	}

	/* Get the length of this desc. */
	len = virtio32_to_cpu(vdev, desc->len);
	if (len > vring->rem_len)
		len = vring->rem_len;

	/* Rx/Tx one word (8 bytes) if not done. */
	if (vring->cur_len < len) {
		mlxbf_tmfifo_rxtx_word(vring, desc, is_rx, len);
		(*avail)--;
	}

	/* Check again whether it's done. */
	if (vring->cur_len == len) {
		vring->cur_len = 0;
		vring->rem_len -= len;

		/* Get the next desc on the chain. */
		if (vring->rem_len > 0 &&
		    (virtio16_to_cpu(vdev, desc->flags) & VRING_DESC_F_NEXT)) {
			idx = virtio16_to_cpu(vdev, desc->next);
			desc = &vr->desc[idx];
			goto mlxbf_tmfifo_desc_done;
		}

		/* Done and release the pending packet. */
		mlxbf_tmfifo_release_pending_pkt(vring);
		desc = NULL;
		fifo->vring[is_rx] = NULL;

		/* Notify upper layer that packet is done. */
		spin_lock_irqsave(&fifo->spin_lock[is_rx], flags);
		vring_interrupt(0, vring->vq);
		spin_unlock_irqrestore(&fifo->spin_lock[is_rx], flags);
	}

mlxbf_tmfifo_desc_done:
	/* Save the current desc. */
	vring->desc = desc;

	return true;
}

/* Rx & Tx processing of a queue. */
static void mlxbf_tmfifo_rxtx(struct mlxbf_tmfifo_vring *vring, bool is_rx)
{
	int avail = 0, devid = vring->vdev_id;
	struct mlxbf_tmfifo *fifo;
	bool more;

	fifo = vring->fifo;

	/* Return if vdev is not ready. */
	if (!fifo->vdev[devid])
		return;

	/* Return if another vring is running. */
	if (fifo->vring[is_rx] && fifo->vring[is_rx] != vring)
		return;

	/* Only handle console and network for now. */
	if (WARN_ON(devid != VIRTIO_ID_NET && devid != VIRTIO_ID_CONSOLE))
		return;

	do {
		/* Get available FIFO space. */
		if (avail == 0) {
			if (is_rx)
				avail = mlxbf_tmfifo_get_rx_avail(fifo);
			else
				avail = mlxbf_tmfifo_get_tx_avail(fifo, devid);
			if (avail <= 0)
				break;
		}

		/* Console output always comes from the Tx buffer. */
		if (!is_rx && devid == VIRTIO_ID_CONSOLE) {
			mlxbf_tmfifo_console_tx(fifo, avail);
			break;
		}

		/* Handle one descriptor. */
		more = mlxbf_tmfifo_rxtx_one_desc(vring, is_rx, &avail);
	} while (more);
}

/* Handle Rx or Tx queues. */
static void mlxbf_tmfifo_work_rxtx(struct mlxbf_tmfifo *fifo, int queue_id,
				   int irq_id, bool is_rx)
{
	struct mlxbf_tmfifo_vdev *tm_vdev;
	struct mlxbf_tmfifo_vring *vring;
	int i;

	if (!test_and_clear_bit(irq_id, &fifo->pend_events) ||
	    !fifo->irq_info[irq_id].irq)
		return;

	for (i = 0; i < MLXBF_TMFIFO_VDEV_MAX; i++) {
		tm_vdev = fifo->vdev[i];
		if (tm_vdev) {
			vring = &tm_vdev->vrings[queue_id];
			if (vring->vq)
				mlxbf_tmfifo_rxtx(vring, is_rx);
		}
	}
}

/* Work handler for Rx and Tx case. */
static void mlxbf_tmfifo_work_handler(struct work_struct *work)
{
	struct mlxbf_tmfifo *fifo;

	fifo = container_of(work, struct mlxbf_tmfifo, work);
	if (!fifo->is_ready)
		return;

	mutex_lock(&fifo->lock);

	/* Tx (Send data to the TmFifo). */
	mlxbf_tmfifo_work_rxtx(fifo, MLXBF_TMFIFO_VRING_TX,
			       MLXBF_TM_TX_LWM_IRQ, false);

	/* Rx (Receive data from the TmFifo). */
	mlxbf_tmfifo_work_rxtx(fifo, MLXBF_TMFIFO_VRING_RX,
			       MLXBF_TM_RX_HWM_IRQ, true);

	mutex_unlock(&fifo->lock);
}

/* The notify function is called when new buffers are posted. */
static bool mlxbf_tmfifo_virtio_notify(struct virtqueue *vq)
{
	struct mlxbf_tmfifo_vring *vring = vq->priv;
	struct mlxbf_tmfifo_vdev *tm_vdev;
	struct mlxbf_tmfifo *fifo;
	unsigned long flags;

	fifo = vring->fifo;

	/*
	 * Virtio maintains vrings in pairs, even number ring for Rx
	 * and odd number ring for Tx.
	 */
	if (vring->index & BIT(0)) {
		/*
		 * Console could make blocking call with interrupts disabled.
		 * In such case, the vring needs to be served right away. For
		 * other cases, just set the TX LWM bit to start Tx in the
		 * worker handler.
		 */
		if (vring->vdev_id == VIRTIO_ID_CONSOLE) {
			spin_lock_irqsave(&fifo->spin_lock[0], flags);
			tm_vdev = fifo->vdev[VIRTIO_ID_CONSOLE];
			mlxbf_tmfifo_console_output(tm_vdev, vring);
			spin_unlock_irqrestore(&fifo->spin_lock[0], flags);
		} else if (test_and_set_bit(MLXBF_TM_TX_LWM_IRQ,
					    &fifo->pend_events)) {
			return true;
		}
	} else {
		if (test_and_set_bit(MLXBF_TM_RX_HWM_IRQ, &fifo->pend_events))
			return true;
	}

	schedule_work(&fifo->work);

	return true;
}

/* Get the array of feature bits for this device. */
static u64 mlxbf_tmfifo_virtio_get_features(struct virtio_device *vdev)
{
	struct mlxbf_tmfifo_vdev *tm_vdev = mlxbf_vdev_to_tmfifo(vdev);

	return tm_vdev->features;
}

/* Confirm device features to use. */
static int mlxbf_tmfifo_virtio_finalize_features(struct virtio_device *vdev)
{
	struct mlxbf_tmfifo_vdev *tm_vdev = mlxbf_vdev_to_tmfifo(vdev);

	tm_vdev->features = vdev->features;

	return 0;
}

/* Free virtqueues found by find_vqs(). */
static void mlxbf_tmfifo_virtio_del_vqs(struct virtio_device *vdev)
{
	struct mlxbf_tmfifo_vdev *tm_vdev = mlxbf_vdev_to_tmfifo(vdev);
	struct mlxbf_tmfifo_vring *vring;
	struct virtqueue *vq;
	int i;

	for (i = 0; i < ARRAY_SIZE(tm_vdev->vrings); i++) {
		vring = &tm_vdev->vrings[i];

		/* Release the pending packet. */
		if (vring->desc)
			mlxbf_tmfifo_release_pending_pkt(vring);
		vq = vring->vq;
		if (vq) {
			vring->vq = NULL;
			vring_del_virtqueue(vq);
		}
	}
}

/* Create and initialize the virtual queues. */
static int mlxbf_tmfifo_virtio_find_vqs(struct virtio_device *vdev,
					unsigned int nvqs,
					struct virtqueue *vqs[],
					vq_callback_t *callbacks[],
					const char * const names[],
					const bool *ctx,
					struct irq_affinity *desc)
{
	struct mlxbf_tmfifo_vdev *tm_vdev = mlxbf_vdev_to_tmfifo(vdev);
	struct mlxbf_tmfifo_vring *vring;
	struct virtqueue *vq;
	int i, ret, size;

	if (nvqs > ARRAY_SIZE(tm_vdev->vrings))
		return -EINVAL;

	for (i = 0; i < nvqs; ++i) {
		if (!names[i]) {
			ret = -EINVAL;
			goto error;
		}
		vring = &tm_vdev->vrings[i];

		/* zero vring */
		size = vring_size(vring->num, vring->align);
		memset(vring->va, 0, size);
		vq = vring_new_virtqueue(i, vring->num, vring->align, vdev,
					 false, false, vring->va,
					 mlxbf_tmfifo_virtio_notify,
					 callbacks[i], names[i]);
		if (!vq) {
			dev_err(&vdev->dev, "vring_new_virtqueue failed\n");
			ret = -ENOMEM;
			goto error;
		}

		vqs[i] = vq;
		vring->vq = vq;
		vq->priv = vring;
	}

	return 0;

error:
	mlxbf_tmfifo_virtio_del_vqs(vdev);
	return ret;
}

/* Read the status byte. */
static u8 mlxbf_tmfifo_virtio_get_status(struct virtio_device *vdev)
{
	struct mlxbf_tmfifo_vdev *tm_vdev = mlxbf_vdev_to_tmfifo(vdev);

	return tm_vdev->status;
}

/* Write the status byte. */
static void mlxbf_tmfifo_virtio_set_status(struct virtio_device *vdev,
					   u8 status)
{
	struct mlxbf_tmfifo_vdev *tm_vdev = mlxbf_vdev_to_tmfifo(vdev);

	tm_vdev->status = status;
}

/* Reset the device. Not much here for now. */
static void mlxbf_tmfifo_virtio_reset(struct virtio_device *vdev)
{
	struct mlxbf_tmfifo_vdev *tm_vdev = mlxbf_vdev_to_tmfifo(vdev);

	tm_vdev->status = 0;
}

/* Read the value of a configuration field. */
static void mlxbf_tmfifo_virtio_get(struct virtio_device *vdev,
				    unsigned int offset,
				    void *buf,
				    unsigned int len)
{
	struct mlxbf_tmfifo_vdev *tm_vdev = mlxbf_vdev_to_tmfifo(vdev);

	if ((u64)offset + len > sizeof(tm_vdev->config))
		return;

	memcpy(buf, (u8 *)&tm_vdev->config + offset, len);
}

/* Write the value of a configuration field. */
static void mlxbf_tmfifo_virtio_set(struct virtio_device *vdev,
				    unsigned int offset,
				    const void *buf,
				    unsigned int len)
{
	struct mlxbf_tmfifo_vdev *tm_vdev = mlxbf_vdev_to_tmfifo(vdev);

	if ((u64)offset + len > sizeof(tm_vdev->config))
		return;

	memcpy((u8 *)&tm_vdev->config + offset, buf, len);
}

static void tmfifo_virtio_dev_release(struct device *device)
{
	struct virtio_device *vdev =
			container_of(device, struct virtio_device, dev);
	struct mlxbf_tmfifo_vdev *tm_vdev = mlxbf_vdev_to_tmfifo(vdev);

	kfree(tm_vdev);
}

/* Virtio config operations. */
static const struct virtio_config_ops mlxbf_tmfifo_virtio_config_ops = {
	.get_features = mlxbf_tmfifo_virtio_get_features,
	.finalize_features = mlxbf_tmfifo_virtio_finalize_features,
	.find_vqs = mlxbf_tmfifo_virtio_find_vqs,
	.del_vqs = mlxbf_tmfifo_virtio_del_vqs,
	.reset = mlxbf_tmfifo_virtio_reset,
	.set_status = mlxbf_tmfifo_virtio_set_status,
	.get_status = mlxbf_tmfifo_virtio_get_status,
	.get = mlxbf_tmfifo_virtio_get,
	.set = mlxbf_tmfifo_virtio_set,
};

/* Create vdev for the FIFO. */
static int mlxbf_tmfifo_create_vdev(struct device *dev,
				    struct mlxbf_tmfifo *fifo,
				    int vdev_id, u64 features,
				    void *config, u32 size)
{
	struct mlxbf_tmfifo_vdev *tm_vdev, *reg_dev = NULL;
	int ret;

	mutex_lock(&fifo->lock);

	tm_vdev = fifo->vdev[vdev_id];
	if (tm_vdev) {
		dev_err(dev, "vdev %d already exists\n", vdev_id);
		ret = -EEXIST;
		goto fail;
	}

	tm_vdev = kzalloc(sizeof(*tm_vdev), GFP_KERNEL);
	if (!tm_vdev) {
		ret = -ENOMEM;
		goto fail;
	}

	tm_vdev->vdev.id.device = vdev_id;
	tm_vdev->vdev.config = &mlxbf_tmfifo_virtio_config_ops;
	tm_vdev->vdev.dev.parent = dev;
	tm_vdev->vdev.dev.release = tmfifo_virtio_dev_release;
	tm_vdev->features = features;
	if (config)
		memcpy(&tm_vdev->config, config, size);

	if (mlxbf_tmfifo_alloc_vrings(fifo, tm_vdev)) {
		dev_err(dev, "unable to allocate vring\n");
		ret = -ENOMEM;
		goto vdev_fail;
	}

	/* Allocate an output buffer for the console device. */
	if (vdev_id == VIRTIO_ID_CONSOLE)
		tm_vdev->tx_buf.buf = devm_kmalloc(dev,
						   MLXBF_TMFIFO_CON_TX_BUF_SIZE,
						   GFP_KERNEL);
	fifo->vdev[vdev_id] = tm_vdev;

	/* Register the virtio device. */
	ret = register_virtio_device(&tm_vdev->vdev);
	reg_dev = tm_vdev;
	if (ret) {
		dev_err(dev, "register_virtio_device failed\n");
		goto vdev_fail;
	}

	mutex_unlock(&fifo->lock);
	return 0;

vdev_fail:
	mlxbf_tmfifo_free_vrings(fifo, tm_vdev);
	fifo->vdev[vdev_id] = NULL;
	if (reg_dev)
		put_device(&tm_vdev->vdev.dev);
	else
		kfree(tm_vdev);
fail:
	mutex_unlock(&fifo->lock);
	return ret;
}

/* Delete vdev for the FIFO. */
static int mlxbf_tmfifo_delete_vdev(struct mlxbf_tmfifo *fifo, int vdev_id)
{
	struct mlxbf_tmfifo_vdev *tm_vdev;

	mutex_lock(&fifo->lock);

	/* Unregister vdev. */
	tm_vdev = fifo->vdev[vdev_id];
	if (tm_vdev) {
		unregister_virtio_device(&tm_vdev->vdev);
		mlxbf_tmfifo_free_vrings(fifo, tm_vdev);
		fifo->vdev[vdev_id] = NULL;
	}

	mutex_unlock(&fifo->lock);

	return 0;
}

/* Read the configured network MAC address from efi variable. */
static void mlxbf_tmfifo_get_cfg_mac(u8 *mac)
{
	efi_guid_t guid = EFI_GLOBAL_VARIABLE_GUID;
	unsigned long size = ETH_ALEN;
	u8 buf[ETH_ALEN];
	efi_status_t rc;

	rc = efi.get_variable(mlxbf_tmfifo_efi_name, &guid, NULL, &size, buf);
	if (rc == EFI_SUCCESS && size == ETH_ALEN)
		ether_addr_copy(mac, buf);
	else
		ether_addr_copy(mac, mlxbf_tmfifo_net_default_mac);
}

/* Set TmFifo thresolds which is used to trigger interrupts. */
static void mlxbf_tmfifo_set_threshold(struct mlxbf_tmfifo *fifo)
{
	u64 ctl;

	/* Get Tx FIFO size and set the low/high watermark. */
	ctl = readq(fifo->tx_base + MLXBF_TMFIFO_TX_CTL);
	fifo->tx_fifo_size =
		FIELD_GET(MLXBF_TMFIFO_TX_CTL__MAX_ENTRIES_MASK, ctl);
	ctl = (ctl & ~MLXBF_TMFIFO_TX_CTL__LWM_MASK) |
		FIELD_PREP(MLXBF_TMFIFO_TX_CTL__LWM_MASK,
			   fifo->tx_fifo_size / 2);
	ctl = (ctl & ~MLXBF_TMFIFO_TX_CTL__HWM_MASK) |
		FIELD_PREP(MLXBF_TMFIFO_TX_CTL__HWM_MASK,
			   fifo->tx_fifo_size - 1);
	writeq(ctl, fifo->tx_base + MLXBF_TMFIFO_TX_CTL);

	/* Get Rx FIFO size and set the low/high watermark. */
	ctl = readq(fifo->rx_base + MLXBF_TMFIFO_RX_CTL);
	fifo->rx_fifo_size =
		FIELD_GET(MLXBF_TMFIFO_RX_CTL__MAX_ENTRIES_MASK, ctl);
	ctl = (ctl & ~MLXBF_TMFIFO_RX_CTL__LWM_MASK) |
		FIELD_PREP(MLXBF_TMFIFO_RX_CTL__LWM_MASK, 0);
	ctl = (ctl & ~MLXBF_TMFIFO_RX_CTL__HWM_MASK) |
		FIELD_PREP(MLXBF_TMFIFO_RX_CTL__HWM_MASK, 1);
	writeq(ctl, fifo->rx_base + MLXBF_TMFIFO_RX_CTL);
}

static void mlxbf_tmfifo_cleanup(struct mlxbf_tmfifo *fifo)
{
	int i;

	fifo->is_ready = false;
	del_timer_sync(&fifo->timer);
	mlxbf_tmfifo_disable_irqs(fifo);
	cancel_work_sync(&fifo->work);
	for (i = 0; i < MLXBF_TMFIFO_VDEV_MAX; i++)
		mlxbf_tmfifo_delete_vdev(fifo, i);
}

/* Probe the TMFIFO. */
static int mlxbf_tmfifo_probe(struct platform_device *pdev)
{
	struct virtio_net_config net_config;
	struct device *dev = &pdev->dev;
	struct mlxbf_tmfifo *fifo;
	int i, rc;

	fifo = devm_kzalloc(dev, sizeof(*fifo), GFP_KERNEL);
	if (!fifo)
		return -ENOMEM;

	spin_lock_init(&fifo->spin_lock[0]);
	spin_lock_init(&fifo->spin_lock[1]);
	INIT_WORK(&fifo->work, mlxbf_tmfifo_work_handler);
	mutex_init(&fifo->lock);

	/* Get the resource of the Rx FIFO. */
	fifo->rx_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(fifo->rx_base))
		return PTR_ERR(fifo->rx_base);

	/* Get the resource of the Tx FIFO. */
	fifo->tx_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(fifo->tx_base))
		return PTR_ERR(fifo->tx_base);

	platform_set_drvdata(pdev, fifo);

	timer_setup(&fifo->timer, mlxbf_tmfifo_timer, 0);

	for (i = 0; i < MLXBF_TM_MAX_IRQ; i++) {
		fifo->irq_info[i].index = i;
		fifo->irq_info[i].fifo = fifo;
		fifo->irq_info[i].irq = platform_get_irq(pdev, i);
		rc = devm_request_irq(dev, fifo->irq_info[i].irq,
				      mlxbf_tmfifo_irq_handler, 0,
				      "tmfifo", &fifo->irq_info[i]);
		if (rc) {
			dev_err(dev, "devm_request_irq failed\n");
			fifo->irq_info[i].irq = 0;
			return rc;
		}
	}

	mlxbf_tmfifo_set_threshold(fifo);

	/* Create the console vdev. */
	rc = mlxbf_tmfifo_create_vdev(dev, fifo, VIRTIO_ID_CONSOLE, 0, NULL, 0);
	if (rc)
		goto fail;

	/* Create the network vdev. */
	memset(&net_config, 0, sizeof(net_config));

	/* A legacy-only interface for now. */
	net_config.mtu = __cpu_to_virtio16(virtio_legacy_is_little_endian(),
					   ETH_DATA_LEN);
	net_config.status = __cpu_to_virtio16(virtio_legacy_is_little_endian(),
					      VIRTIO_NET_S_LINK_UP);
	mlxbf_tmfifo_get_cfg_mac(net_config.mac);
	rc = mlxbf_tmfifo_create_vdev(dev, fifo, VIRTIO_ID_NET,
				      MLXBF_TMFIFO_NET_FEATURES, &net_config,
				      sizeof(net_config));
	if (rc)
		goto fail;

	mod_timer(&fifo->timer, jiffies + MLXBF_TMFIFO_TIMER_INTERVAL);

	fifo->is_ready = true;
	return 0;

fail:
	mlxbf_tmfifo_cleanup(fifo);
	return rc;
}

/* Device remove function. */
static int mlxbf_tmfifo_remove(struct platform_device *pdev)
{
	struct mlxbf_tmfifo *fifo = platform_get_drvdata(pdev);

	mlxbf_tmfifo_cleanup(fifo);

	return 0;
}

static const struct acpi_device_id mlxbf_tmfifo_acpi_match[] = {
	{ "MLNXBF01", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, mlxbf_tmfifo_acpi_match);

static struct platform_driver mlxbf_tmfifo_driver = {
	.probe = mlxbf_tmfifo_probe,
	.remove = mlxbf_tmfifo_remove,
	.driver = {
		.name = "bf-tmfifo",
		.acpi_match_table = mlxbf_tmfifo_acpi_match,
	},
};

module_platform_driver(mlxbf_tmfifo_driver);

MODULE_DESCRIPTION("Mellanox BlueField SoC TmFifo Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Mellanox Technologies");
