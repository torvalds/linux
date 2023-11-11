// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

#include <linux/types.h>
#include <linux/io.h>

#include "shm_ipc.h"

#undef pr_fmt
#define pr_fmt(fmt)	"qtnfmac shm_ipc: %s: " fmt, __func__

static bool qtnf_shm_ipc_has_new_data(struct qtnf_shm_ipc *ipc)
{
	const u32 flags = readl(&ipc->shm_region->headroom.hdr.flags);

	return (flags & QTNF_SHM_IPC_NEW_DATA);
}

static void qtnf_shm_handle_new_data(struct qtnf_shm_ipc *ipc)
{
	size_t size;
	bool rx_buff_ok = true;
	struct qtnf_shm_ipc_region_header __iomem *shm_reg_hdr;

	shm_reg_hdr = &ipc->shm_region->headroom.hdr;

	size = readw(&shm_reg_hdr->data_len);

	if (unlikely(size == 0 || size > QTN_IPC_MAX_DATA_SZ)) {
		pr_err("wrong rx packet size: %zu\n", size);
		rx_buff_ok = false;
	}

	if (likely(rx_buff_ok)) {
		ipc->rx_packet_count++;
		ipc->rx_callback.fn(ipc->rx_callback.arg,
				    ipc->shm_region->data, size);
	}

	writel(QTNF_SHM_IPC_ACK, &shm_reg_hdr->flags);
	readl(&shm_reg_hdr->flags); /* flush PCIe write */

	ipc->interrupt.fn(ipc->interrupt.arg);
}

static void qtnf_shm_ipc_irq_work(struct work_struct *work)
{
	struct qtnf_shm_ipc *ipc = container_of(work, struct qtnf_shm_ipc,
						irq_work);

	while (qtnf_shm_ipc_has_new_data(ipc))
		qtnf_shm_handle_new_data(ipc);
}

static void qtnf_shm_ipc_irq_inbound_handler(struct qtnf_shm_ipc *ipc)
{
	u32 flags;

	flags = readl(&ipc->shm_region->headroom.hdr.flags);

	if (flags & QTNF_SHM_IPC_NEW_DATA)
		queue_work(ipc->workqueue, &ipc->irq_work);
}

static void qtnf_shm_ipc_irq_outbound_handler(struct qtnf_shm_ipc *ipc)
{
	u32 flags;

	if (!READ_ONCE(ipc->waiting_for_ack))
		return;

	flags = readl(&ipc->shm_region->headroom.hdr.flags);

	if (flags & QTNF_SHM_IPC_ACK) {
		WRITE_ONCE(ipc->waiting_for_ack, 0);
		complete(&ipc->tx_completion);
	}
}

int qtnf_shm_ipc_init(struct qtnf_shm_ipc *ipc,
		      enum qtnf_shm_ipc_direction direction,
		      struct qtnf_shm_ipc_region __iomem *shm_region,
		      struct workqueue_struct *workqueue,
		      const struct qtnf_shm_ipc_int *interrupt,
		      const struct qtnf_shm_ipc_rx_callback *rx_callback)
{
	BUILD_BUG_ON(offsetof(struct qtnf_shm_ipc_region, data) !=
		     QTN_IPC_REG_HDR_SZ);
	BUILD_BUG_ON(sizeof(struct qtnf_shm_ipc_region) > QTN_IPC_REG_SZ);

	ipc->shm_region = shm_region;
	ipc->direction = direction;
	ipc->interrupt = *interrupt;
	ipc->rx_callback = *rx_callback;
	ipc->tx_packet_count = 0;
	ipc->rx_packet_count = 0;
	ipc->workqueue = workqueue;
	ipc->waiting_for_ack = 0;
	ipc->tx_timeout_count = 0;

	switch (direction) {
	case QTNF_SHM_IPC_OUTBOUND:
		ipc->irq_handler = qtnf_shm_ipc_irq_outbound_handler;
		break;
	case QTNF_SHM_IPC_INBOUND:
		ipc->irq_handler = qtnf_shm_ipc_irq_inbound_handler;
		break;
	default:
		return -EINVAL;
	}

	INIT_WORK(&ipc->irq_work, qtnf_shm_ipc_irq_work);
	init_completion(&ipc->tx_completion);

	return 0;
}

void qtnf_shm_ipc_free(struct qtnf_shm_ipc *ipc)
{
	complete_all(&ipc->tx_completion);
}

int qtnf_shm_ipc_send(struct qtnf_shm_ipc *ipc, const u8 *buf, size_t size)
{
	int ret = 0;
	struct qtnf_shm_ipc_region_header __iomem *shm_reg_hdr;

	shm_reg_hdr = &ipc->shm_region->headroom.hdr;

	if (unlikely(size > QTN_IPC_MAX_DATA_SZ))
		return -E2BIG;

	ipc->tx_packet_count++;

	writew(size, &shm_reg_hdr->data_len);
	memcpy_toio(ipc->shm_region->data, buf, size);

	/* sync previous writes before proceeding */
	dma_wmb();

	WRITE_ONCE(ipc->waiting_for_ack, 1);

	/* sync previous memory write before announcing new data ready */
	wmb();

	writel(QTNF_SHM_IPC_NEW_DATA, &shm_reg_hdr->flags);
	readl(&shm_reg_hdr->flags); /* flush PCIe write */

	ipc->interrupt.fn(ipc->interrupt.arg);

	if (!wait_for_completion_timeout(&ipc->tx_completion,
					 QTN_SHM_IPC_ACK_TIMEOUT)) {
		ret = -ETIMEDOUT;
		ipc->tx_timeout_count++;
		pr_err("TX ACK timeout\n");
	}

	/* now we're not waiting for ACK even in case of timeout */
	WRITE_ONCE(ipc->waiting_for_ack, 0);

	return ret;
}
