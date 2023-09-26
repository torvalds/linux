// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "hab.h"
#include "hab_qvm.h"
#include "hab_trace_os.h"

static unsigned long long xvm_sche_tx_tv_buffer[2];

static void pipe_read_trace(struct qvm_channel *dev,
			int size, int ret)
{
	struct hab_pipe_endpoint *ep = dev->pipe_ep;
	struct hab_shared_buf *sh_buf = dev->rx_buf;

	struct dbg_items *its = dev->dbg_itms;
	struct dbg_item *it = &its->it[its->idx];

	it->rd_cnt = sh_buf->rd_count;
	it->wr_cnt = sh_buf->wr_count;
	it->va = (void *)&sh_buf->data[ep->rx_info.index];
	it->index = ep->rx_info.index;
	it->sz = size;
	it->ret = ret;

	its->idx++;
	if (its->idx >= DBG_ITEM_SIZE)
		its->idx = 0;
}

/* this is only used to read payload, never the head! */
int physical_channel_read(struct physical_channel *pchan,
		void *payload,
		size_t read_size)
{
	struct qvm_channel *dev  = (struct qvm_channel *)pchan->hyp_data;

	if (dev) {
		int ret = hab_pipe_read(dev->pipe_ep,
				dev->rx_buf, PIPE_SHMEM_SIZE,
				payload, read_size, 0);

		/* log */
		pipe_read_trace(dev, read_size, ret);

		return ret;
	} else
		return 0;
}

int physical_channel_send(struct physical_channel *pchan,
		struct hab_header *header,
		void *payload,
		unsigned int flags)
{
	size_t sizebytes = HAB_HEADER_GET_SIZE(*header);
	struct qvm_channel *dev  = (struct qvm_channel *)pchan->hyp_data;
	size_t total_size = sizeof(*header) + sizebytes;
	uint32_t buf_size = PIPE_SHMEM_SIZE;
	int irqs_disabled = irqs_disabled();

	/* Only used in virtio arch */
	(void)flags;

	if (total_size > buf_size)
		return -EINVAL; /* too much data for ring */

	hab_spin_lock(&dev->io_lock, irqs_disabled);

	trace_hab_pchan_send_start(pchan);

	if ((buf_size -
		(dev->pipe_ep->tx_info.wr_count -
		dev->tx_buf->rd_count)) < total_size) {
		hab_spin_unlock(&dev->io_lock, irqs_disabled);
		return -EAGAIN; /* not enough free space */
	}

	header->sequence = pchan->sequence_tx + 1;
	header->signature = HAB_HEAD_SIGNATURE;

	if (hab_pipe_write(dev->pipe_ep, dev->tx_buf, buf_size,
		(unsigned char *)header,
		sizeof(*header)) != sizeof(*header)) {
		hab_spin_unlock(&dev->io_lock, irqs_disabled);
		pr_err("***incompleted pchan send id-type %x size %x session %d seq# %d\n",
			header->id_type, header->payload_size,
			header->session_id,
			header->sequence);
		return -EIO;
	}

	if (HAB_HEADER_GET_TYPE(*header) == HAB_PAYLOAD_TYPE_PROFILE) {
		struct timespec64 ts = {0};
		struct habmm_xing_vm_stat *pstat =
			(struct habmm_xing_vm_stat *)payload;

		if (pstat) {
			ktime_get_ts64(&ts);
			pstat->tx_sec = ts.tv_sec;
			pstat->tx_usec = ts.tv_nsec/NSEC_PER_USEC;
		} else {
			hab_spin_unlock(&dev->io_lock, irqs_disabled);
			pr_err("***incompleted pchan send prof id-type %x size %x session %d seq# %d\n",
				header->id_type, header->payload_size,
				header->session_id,
				header->sequence);
			return -EINVAL;
		}
	} else if (HAB_HEADER_GET_TYPE(*header)
		== HAB_PAYLOAD_TYPE_SCHE_RESULT_REQ) {
		((unsigned long long *)payload)[0] = xvm_sche_tx_tv_buffer[0];
	} else if (HAB_HEADER_GET_TYPE(*header)
		== HAB_PAYLOAD_TYPE_SCHE_RESULT_RSP) {
		((unsigned long long *)payload)[2] = xvm_sche_tx_tv_buffer[1];
	}

	if (sizebytes) {
		if (hab_pipe_write(dev->pipe_ep, dev->tx_buf, buf_size,
			(unsigned char *)payload,
			sizebytes) != sizebytes) {
			hab_spin_unlock(&dev->io_lock, irqs_disabled);
			pr_err("***incompleted pchan send id-type %x size %x session %d seq# %d\n",
				header->id_type, header->payload_size,
				header->session_id,
				header->sequence);
			return -EIO;
		}
	}

	hab_pipe_write_commit(dev->pipe_ep, dev->tx_buf);

	/* locally +1 as late as possible but before unlock */
	++pchan->sequence_tx;

	trace_hab_pchan_send_done(pchan);

	hab_spin_unlock(&dev->io_lock, irqs_disabled);
	if (HAB_HEADER_GET_TYPE(*header) == HAB_PAYLOAD_TYPE_SCHE_MSG)
		xvm_sche_tx_tv_buffer[0] = msm_timer_get_sclk_ticks();
	else if (HAB_HEADER_GET_TYPE(*header) == HAB_PAYLOAD_TYPE_SCHE_MSG_ACK)
		xvm_sche_tx_tv_buffer[1] = msm_timer_get_sclk_ticks();
	habhyp_notify(dev);
	return 0;
}

void physical_channel_rx_dispatch(unsigned long data)
{
	struct hab_header header;
	struct physical_channel *pchan = (struct physical_channel *)data;
	struct qvm_channel *dev = (struct qvm_channel *)pchan->hyp_data;
	int irqs_disabled = irqs_disabled();
	uint32_t buf_size = PIPE_SHMEM_SIZE;

	hab_spin_lock(&pchan->rxbuf_lock, irqs_disabled);
	while (1) {
		uint32_t rd, wr, idx;
		int ret;

		ret = hab_pipe_read(dev->pipe_ep,
			dev->rx_buf, buf_size,
			(unsigned char *)&header,
			sizeof(header), 1); /* clear head after read */

		/* debug */
		pipe_read_trace(dev, sizeof(header), ret);

		if (ret == 0xFFFFFFFF) { /* signature mismatched first time */
			hab_pipe_rxinfo(dev->pipe_ep, dev->rx_buf, &rd, &wr, &idx);

			pr_err("!!!!! HAB signature mismatch expect %X received %X, id_type %X size %X session %X sequence %X\n",
				HAB_HEAD_SIGNATURE, header.signature,
				header.id_type,
				header.payload_size,
				header.session_id,
				header.sequence);

			pr_err("!!!!! rxinfo rd %d wr %d index %X\n",
				rd, wr, idx);

			memcpy(dev->side_buf,
				(void *)&dev->rx_buf->data[0],
				buf_size);

			hab_spin_unlock(&pchan->rxbuf_lock, irqs_disabled);
			/* cannot run in elevated context */
			dump_hab_wq(pchan);
			hab_spin_lock(&pchan->rxbuf_lock, irqs_disabled);

		} else if (ret == 0xFFFFFFFE) { /* continuous signature mismatches */
			continue;
		} else if (ret != sizeof(header))
			break; /* no data available */

		if (pchan->sequence_rx + 1 != header.sequence)
			pr_err("%s: expected sequence_rx is %u, received is %u\n",
				pchan->name, pchan->sequence_rx, header.sequence);
		pchan->sequence_rx = header.sequence;

		/* log msg recv timestamp: enter pchan dispatcher */
		trace_hab_pchan_recv_start(pchan);

		hab_msg_recv(pchan, &header);
	}
	hab_spin_unlock(&pchan->rxbuf_lock, irqs_disabled);
}
