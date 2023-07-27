/*
 *   Copyright (c) 2011, 2012, Qualcomm Atheros Communications Inc.
 *   Copyright (c) 2014, I2SE GmbH
 *
 *   Permission to use, copy, modify, and/or distribute this software
 *   for any purpose with or without fee is hereby granted, provided
 *   that the above copyright notice and this permission notice appear
 *   in all copies.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *   WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *   WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 *   THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 *   CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 *   LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 *   NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 *   CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*   This module implements the Qualcomm Atheros SPI protocol for
 *   kernel-based SPI device; it is essentially an Ethernet-to-SPI
 *   serial converter;
 */

#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "qca_7k.h"
#include "qca_7k_common.h"
#include "qca_debug.h"
#include "qca_spi.h"

#define MAX_DMA_BURST_LEN 5000

/*   Modules parameters     */
#define QCASPI_CLK_SPEED_MIN 1000000
#define QCASPI_CLK_SPEED_MAX 16000000
#define QCASPI_CLK_SPEED     8000000
static int qcaspi_clkspeed;
module_param(qcaspi_clkspeed, int, 0);
MODULE_PARM_DESC(qcaspi_clkspeed, "SPI bus clock speed (Hz). Use 1000000-16000000.");

#define QCASPI_BURST_LEN_MIN 1
#define QCASPI_BURST_LEN_MAX MAX_DMA_BURST_LEN
static int qcaspi_burst_len = MAX_DMA_BURST_LEN;
module_param(qcaspi_burst_len, int, 0);
MODULE_PARM_DESC(qcaspi_burst_len, "Number of data bytes per burst. Use 1-5000.");

#define QCASPI_PLUGGABLE_MIN 0
#define QCASPI_PLUGGABLE_MAX 1
static int qcaspi_pluggable = QCASPI_PLUGGABLE_MIN;
module_param(qcaspi_pluggable, int, 0);
MODULE_PARM_DESC(qcaspi_pluggable, "Pluggable SPI connection (yes/no).");

#define QCASPI_WRITE_VERIFY_MIN 0
#define QCASPI_WRITE_VERIFY_MAX 3
static int wr_verify = QCASPI_WRITE_VERIFY_MIN;
module_param(wr_verify, int, 0);
MODULE_PARM_DESC(wr_verify, "SPI register write verify trails. Use 0-3.");

#define QCASPI_TX_TIMEOUT (1 * HZ)
#define QCASPI_QCA7K_REBOOT_TIME_MS 1000

static void
start_spi_intr_handling(struct qcaspi *qca, u16 *intr_cause)
{
	*intr_cause = 0;

	qcaspi_write_register(qca, SPI_REG_INTR_ENABLE, 0, wr_verify);
	qcaspi_read_register(qca, SPI_REG_INTR_CAUSE, intr_cause);
	netdev_dbg(qca->net_dev, "interrupts: 0x%04x\n", *intr_cause);
}

static void
end_spi_intr_handling(struct qcaspi *qca, u16 intr_cause)
{
	u16 intr_enable = (SPI_INT_CPU_ON |
			   SPI_INT_PKT_AVLBL |
			   SPI_INT_RDBUF_ERR |
			   SPI_INT_WRBUF_ERR);

	qcaspi_write_register(qca, SPI_REG_INTR_CAUSE, intr_cause, 0);
	qcaspi_write_register(qca, SPI_REG_INTR_ENABLE, intr_enable, wr_verify);
	netdev_dbg(qca->net_dev, "acking int: 0x%04x\n", intr_cause);
}

static u32
qcaspi_write_burst(struct qcaspi *qca, u8 *src, u32 len)
{
	__be16 cmd;
	struct spi_message msg;
	struct spi_transfer transfer[2];
	int ret;

	memset(&transfer, 0, sizeof(transfer));
	spi_message_init(&msg);

	cmd = cpu_to_be16(QCA7K_SPI_WRITE | QCA7K_SPI_EXTERNAL);
	transfer[0].tx_buf = &cmd;
	transfer[0].len = QCASPI_CMD_LEN;
	transfer[1].tx_buf = src;
	transfer[1].len = len;

	spi_message_add_tail(&transfer[0], &msg);
	spi_message_add_tail(&transfer[1], &msg);
	ret = spi_sync(qca->spi_dev, &msg);

	if (ret || (msg.actual_length != QCASPI_CMD_LEN + len)) {
		qcaspi_spi_error(qca);
		return 0;
	}

	return len;
}

static u32
qcaspi_write_legacy(struct qcaspi *qca, u8 *src, u32 len)
{
	struct spi_message msg;
	struct spi_transfer transfer;
	int ret;

	memset(&transfer, 0, sizeof(transfer));
	spi_message_init(&msg);

	transfer.tx_buf = src;
	transfer.len = len;

	spi_message_add_tail(&transfer, &msg);
	ret = spi_sync(qca->spi_dev, &msg);

	if (ret || (msg.actual_length != len)) {
		qcaspi_spi_error(qca);
		return 0;
	}

	return len;
}

static u32
qcaspi_read_burst(struct qcaspi *qca, u8 *dst, u32 len)
{
	struct spi_message msg;
	__be16 cmd;
	struct spi_transfer transfer[2];
	int ret;

	memset(&transfer, 0, sizeof(transfer));
	spi_message_init(&msg);

	cmd = cpu_to_be16(QCA7K_SPI_READ | QCA7K_SPI_EXTERNAL);
	transfer[0].tx_buf = &cmd;
	transfer[0].len = QCASPI_CMD_LEN;
	transfer[1].rx_buf = dst;
	transfer[1].len = len;

	spi_message_add_tail(&transfer[0], &msg);
	spi_message_add_tail(&transfer[1], &msg);
	ret = spi_sync(qca->spi_dev, &msg);

	if (ret || (msg.actual_length != QCASPI_CMD_LEN + len)) {
		qcaspi_spi_error(qca);
		return 0;
	}

	return len;
}

static u32
qcaspi_read_legacy(struct qcaspi *qca, u8 *dst, u32 len)
{
	struct spi_message msg;
	struct spi_transfer transfer;
	int ret;

	memset(&transfer, 0, sizeof(transfer));
	spi_message_init(&msg);

	transfer.rx_buf = dst;
	transfer.len = len;

	spi_message_add_tail(&transfer, &msg);
	ret = spi_sync(qca->spi_dev, &msg);

	if (ret || (msg.actual_length != len)) {
		qcaspi_spi_error(qca);
		return 0;
	}

	return len;
}

static int
qcaspi_tx_cmd(struct qcaspi *qca, u16 cmd)
{
	__be16 tx_data;
	struct spi_message msg;
	struct spi_transfer transfer;
	int ret;

	memset(&transfer, 0, sizeof(transfer));

	spi_message_init(&msg);

	tx_data = cpu_to_be16(cmd);
	transfer.len = sizeof(cmd);
	transfer.tx_buf = &tx_data;
	spi_message_add_tail(&transfer, &msg);

	ret = spi_sync(qca->spi_dev, &msg);

	if (!ret)
		ret = msg.status;

	if (ret)
		qcaspi_spi_error(qca);

	return ret;
}

static int
qcaspi_tx_frame(struct qcaspi *qca, struct sk_buff *skb)
{
	u32 count;
	u32 written;
	u32 offset;
	u32 len;

	len = skb->len;

	qcaspi_write_register(qca, SPI_REG_BFR_SIZE, len, wr_verify);
	if (qca->legacy_mode)
		qcaspi_tx_cmd(qca, QCA7K_SPI_WRITE | QCA7K_SPI_EXTERNAL);

	offset = 0;
	while (len) {
		count = len;
		if (count > qca->burst_len)
			count = qca->burst_len;

		if (qca->legacy_mode) {
			written = qcaspi_write_legacy(qca,
						      skb->data + offset,
						      count);
		} else {
			written = qcaspi_write_burst(qca,
						     skb->data + offset,
						     count);
		}

		if (written != count)
			return -1;

		offset += count;
		len -= count;
	}

	return 0;
}

static int
qcaspi_transmit(struct qcaspi *qca)
{
	struct net_device_stats *n_stats = &qca->net_dev->stats;
	u16 available = 0;
	u32 pkt_len;
	u16 new_head;
	u16 packets = 0;

	if (qca->txr.skb[qca->txr.head] == NULL)
		return 0;

	qcaspi_read_register(qca, SPI_REG_WRBUF_SPC_AVA, &available);

	if (available > QCASPI_HW_BUF_LEN) {
		/* This could only happen by interferences on the SPI line.
		 * So retry later ...
		 */
		qca->stats.buf_avail_err++;
		return -1;
	}

	while (qca->txr.skb[qca->txr.head]) {
		pkt_len = qca->txr.skb[qca->txr.head]->len + QCASPI_HW_PKT_LEN;

		if (available < pkt_len) {
			if (packets == 0)
				qca->stats.write_buf_miss++;
			break;
		}

		if (qcaspi_tx_frame(qca, qca->txr.skb[qca->txr.head]) == -1) {
			qca->stats.write_err++;
			return -1;
		}

		packets++;
		n_stats->tx_packets++;
		n_stats->tx_bytes += qca->txr.skb[qca->txr.head]->len;
		available -= pkt_len;

		/* remove the skb from the queue */
		/* XXX After inconsistent lock states netif_tx_lock()
		 * has been replaced by netif_tx_lock_bh() and so on.
		 */
		netif_tx_lock_bh(qca->net_dev);
		dev_kfree_skb(qca->txr.skb[qca->txr.head]);
		qca->txr.skb[qca->txr.head] = NULL;
		qca->txr.size -= pkt_len;
		new_head = qca->txr.head + 1;
		if (new_head >= qca->txr.count)
			new_head = 0;
		qca->txr.head = new_head;
		if (netif_queue_stopped(qca->net_dev))
			netif_wake_queue(qca->net_dev);
		netif_tx_unlock_bh(qca->net_dev);
	}

	return 0;
}

static int
qcaspi_receive(struct qcaspi *qca)
{
	struct net_device *net_dev = qca->net_dev;
	struct net_device_stats *n_stats = &net_dev->stats;
	u16 available = 0;
	u32 bytes_read;
	u8 *cp;

	/* Allocate rx SKB if we don't have one available. */
	if (!qca->rx_skb) {
		qca->rx_skb = netdev_alloc_skb_ip_align(net_dev,
							net_dev->mtu +
							VLAN_ETH_HLEN);
		if (!qca->rx_skb) {
			netdev_dbg(net_dev, "out of RX resources\n");
			qca->stats.out_of_mem++;
			return -1;
		}
	}

	/* Read the packet size. */
	qcaspi_read_register(qca, SPI_REG_RDBUF_BYTE_AVA, &available);

	netdev_dbg(net_dev, "qcaspi_receive: SPI_REG_RDBUF_BYTE_AVA: Value: %08x\n",
		   available);

	if (available > QCASPI_HW_BUF_LEN + QCASPI_HW_PKT_LEN) {
		/* This could only happen by interferences on the SPI line.
		 * So retry later ...
		 */
		qca->stats.buf_avail_err++;
		return -1;
	} else if (available == 0) {
		netdev_dbg(net_dev, "qcaspi_receive called without any data being available!\n");
		return -1;
	}

	qcaspi_write_register(qca, SPI_REG_BFR_SIZE, available, wr_verify);

	if (qca->legacy_mode)
		qcaspi_tx_cmd(qca, QCA7K_SPI_READ | QCA7K_SPI_EXTERNAL);

	while (available) {
		u32 count = available;

		if (count > qca->burst_len)
			count = qca->burst_len;

		if (qca->legacy_mode) {
			bytes_read = qcaspi_read_legacy(qca, qca->rx_buffer,
							count);
		} else {
			bytes_read = qcaspi_read_burst(qca, qca->rx_buffer,
						       count);
		}

		netdev_dbg(net_dev, "available: %d, byte read: %d\n",
			   available, bytes_read);

		if (bytes_read) {
			available -= bytes_read;
		} else {
			qca->stats.read_err++;
			return -1;
		}

		cp = qca->rx_buffer;

		while ((bytes_read--) && (qca->rx_skb)) {
			s32 retcode;

			retcode = qcafrm_fsm_decode(&qca->frm_handle,
						    qca->rx_skb->data,
						    skb_tailroom(qca->rx_skb),
						    *cp);
			cp++;
			switch (retcode) {
			case QCAFRM_GATHER:
			case QCAFRM_NOHEAD:
				break;
			case QCAFRM_NOTAIL:
				netdev_dbg(net_dev, "no RX tail\n");
				n_stats->rx_errors++;
				n_stats->rx_dropped++;
				break;
			case QCAFRM_INVLEN:
				netdev_dbg(net_dev, "invalid RX length\n");
				n_stats->rx_errors++;
				n_stats->rx_dropped++;
				break;
			default:
				qca->rx_skb->dev = qca->net_dev;
				n_stats->rx_packets++;
				n_stats->rx_bytes += retcode;
				skb_put(qca->rx_skb, retcode);
				qca->rx_skb->protocol = eth_type_trans(
					qca->rx_skb, qca->rx_skb->dev);
				skb_checksum_none_assert(qca->rx_skb);
				netif_rx(qca->rx_skb);
				qca->rx_skb = netdev_alloc_skb_ip_align(net_dev,
					net_dev->mtu + VLAN_ETH_HLEN);
				if (!qca->rx_skb) {
					netdev_dbg(net_dev, "out of RX resources\n");
					n_stats->rx_errors++;
					qca->stats.out_of_mem++;
					break;
				}
			}
		}
	}

	return 0;
}

/*   Check that tx ring stores only so much bytes
 *   that fit into the internal QCA buffer.
 */

static int
qcaspi_tx_ring_has_space(struct tx_ring *txr)
{
	if (txr->skb[txr->tail])
		return 0;

	return (txr->size + QCAFRM_MAX_LEN < QCASPI_HW_BUF_LEN) ? 1 : 0;
}

/*   Flush the tx ring. This function is only safe to
 *   call from the qcaspi_spi_thread.
 */

static void
qcaspi_flush_tx_ring(struct qcaspi *qca)
{
	int i;

	/* XXX After inconsistent lock states netif_tx_lock()
	 * has been replaced by netif_tx_lock_bh() and so on.
	 */
	netif_tx_lock_bh(qca->net_dev);
	for (i = 0; i < TX_RING_MAX_LEN; i++) {
		if (qca->txr.skb[i]) {
			dev_kfree_skb(qca->txr.skb[i]);
			qca->txr.skb[i] = NULL;
			qca->net_dev->stats.tx_dropped++;
		}
	}
	qca->txr.tail = 0;
	qca->txr.head = 0;
	qca->txr.size = 0;
	netif_tx_unlock_bh(qca->net_dev);
}

static void
qcaspi_qca7k_sync(struct qcaspi *qca, int event)
{
	u16 signature = 0;
	u16 spi_config;
	u16 wrbuf_space = 0;

	if (event == QCASPI_EVENT_CPUON) {
		/* Read signature twice, if not valid
		 * go back to unknown state.
		 */
		qcaspi_read_register(qca, SPI_REG_SIGNATURE, &signature);
		qcaspi_read_register(qca, SPI_REG_SIGNATURE, &signature);
		if (signature != QCASPI_GOOD_SIGNATURE) {
			if (qca->sync == QCASPI_SYNC_READY)
				qca->stats.bad_signature++;

			qca->sync = QCASPI_SYNC_UNKNOWN;
			netdev_dbg(qca->net_dev, "sync: got CPU on, but signature was invalid, restart\n");
			return;
		} else {
			/* ensure that the WRBUF is empty */
			qcaspi_read_register(qca, SPI_REG_WRBUF_SPC_AVA,
					     &wrbuf_space);
			if (wrbuf_space != QCASPI_HW_BUF_LEN) {
				netdev_dbg(qca->net_dev, "sync: got CPU on, but wrbuf not empty. reset!\n");
				qca->sync = QCASPI_SYNC_UNKNOWN;
			} else {
				netdev_dbg(qca->net_dev, "sync: got CPU on, now in sync\n");
				qca->sync = QCASPI_SYNC_READY;
				return;
			}
		}
	}

	switch (qca->sync) {
	case QCASPI_SYNC_READY:
		/* Check signature twice, if not valid go to unknown state. */
		qcaspi_read_register(qca, SPI_REG_SIGNATURE, &signature);
		if (signature != QCASPI_GOOD_SIGNATURE)
			qcaspi_read_register(qca, SPI_REG_SIGNATURE, &signature);

		if (signature != QCASPI_GOOD_SIGNATURE) {
			qca->sync = QCASPI_SYNC_UNKNOWN;
			qca->stats.bad_signature++;
			netdev_dbg(qca->net_dev, "sync: bad signature, restart\n");
			/* don't reset right away */
			return;
		}
		break;
	case QCASPI_SYNC_UNKNOWN:
		/* Read signature, if not valid stay in unknown state */
		qcaspi_read_register(qca, SPI_REG_SIGNATURE, &signature);
		if (signature != QCASPI_GOOD_SIGNATURE) {
			netdev_dbg(qca->net_dev, "sync: could not read signature to reset device, retry.\n");
			return;
		}

		/* TODO: use GPIO to reset QCA7000 in legacy mode*/
		netdev_dbg(qca->net_dev, "sync: resetting device.\n");
		qcaspi_read_register(qca, SPI_REG_SPI_CONFIG, &spi_config);
		spi_config |= QCASPI_SLAVE_RESET_BIT;
		qcaspi_write_register(qca, SPI_REG_SPI_CONFIG, spi_config, 0);

		qca->sync = QCASPI_SYNC_RESET;
		qca->stats.trig_reset++;
		qca->reset_count = 0;
		break;
	case QCASPI_SYNC_RESET:
		qca->reset_count++;
		netdev_dbg(qca->net_dev, "sync: waiting for CPU on, count %u.\n",
			   qca->reset_count);
		if (qca->reset_count >= QCASPI_RESET_TIMEOUT) {
			/* reset did not seem to take place, try again */
			qca->sync = QCASPI_SYNC_UNKNOWN;
			qca->stats.reset_timeout++;
			netdev_dbg(qca->net_dev, "sync: reset timeout, restarting process.\n");
		}
		break;
	}
}

static int
qcaspi_spi_thread(void *data)
{
	struct qcaspi *qca = data;
	u16 intr_cause = 0;

	netdev_info(qca->net_dev, "SPI thread created\n");
	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		if ((qca->intr_req == qca->intr_svc) &&
		    !qca->txr.skb[qca->txr.head])
			schedule();

		set_current_state(TASK_RUNNING);

		netdev_dbg(qca->net_dev, "have work to do. int: %d, tx_skb: %p\n",
			   qca->intr_req - qca->intr_svc,
			   qca->txr.skb[qca->txr.head]);

		qcaspi_qca7k_sync(qca, QCASPI_EVENT_UPDATE);

		if (qca->sync != QCASPI_SYNC_READY) {
			netdev_dbg(qca->net_dev, "sync: not ready %u, turn off carrier and flush\n",
				   (unsigned int)qca->sync);
			netif_stop_queue(qca->net_dev);
			netif_carrier_off(qca->net_dev);
			qcaspi_flush_tx_ring(qca);
			msleep(QCASPI_QCA7K_REBOOT_TIME_MS);
		}

		if (qca->intr_svc != qca->intr_req) {
			qca->intr_svc = qca->intr_req;
			start_spi_intr_handling(qca, &intr_cause);

			if (intr_cause & SPI_INT_CPU_ON) {
				qcaspi_qca7k_sync(qca, QCASPI_EVENT_CPUON);

				/* not synced. */
				if (qca->sync != QCASPI_SYNC_READY)
					continue;

				qca->stats.device_reset++;
				netif_wake_queue(qca->net_dev);
				netif_carrier_on(qca->net_dev);
			}

			if (intr_cause & SPI_INT_RDBUF_ERR) {
				/* restart sync */
				netdev_dbg(qca->net_dev, "===> rdbuf error!\n");
				qca->stats.read_buf_err++;
				qca->sync = QCASPI_SYNC_UNKNOWN;
				continue;
			}

			if (intr_cause & SPI_INT_WRBUF_ERR) {
				/* restart sync */
				netdev_dbg(qca->net_dev, "===> wrbuf error!\n");
				qca->stats.write_buf_err++;
				qca->sync = QCASPI_SYNC_UNKNOWN;
				continue;
			}

			/* can only handle other interrupts
			 * if sync has occurred
			 */
			if (qca->sync == QCASPI_SYNC_READY) {
				if (intr_cause & SPI_INT_PKT_AVLBL)
					qcaspi_receive(qca);
			}

			end_spi_intr_handling(qca, intr_cause);
		}

		if (qca->sync == QCASPI_SYNC_READY)
			qcaspi_transmit(qca);
	}
	set_current_state(TASK_RUNNING);
	netdev_info(qca->net_dev, "SPI thread exit\n");

	return 0;
}

static irqreturn_t
qcaspi_intr_handler(int irq, void *data)
{
	struct qcaspi *qca = data;

	qca->intr_req++;
	if (qca->spi_thread)
		wake_up_process(qca->spi_thread);

	return IRQ_HANDLED;
}

static int
qcaspi_netdev_open(struct net_device *dev)
{
	struct qcaspi *qca = netdev_priv(dev);
	int ret = 0;

	if (!qca)
		return -EINVAL;

	qca->intr_req = 1;
	qca->intr_svc = 0;
	qca->sync = QCASPI_SYNC_UNKNOWN;
	qcafrm_fsm_init_spi(&qca->frm_handle);

	qca->spi_thread = kthread_run((void *)qcaspi_spi_thread,
				      qca, "%s", dev->name);

	if (IS_ERR(qca->spi_thread)) {
		netdev_err(dev, "%s: unable to start kernel thread.\n",
			   QCASPI_DRV_NAME);
		return PTR_ERR(qca->spi_thread);
	}

	ret = request_irq(qca->spi_dev->irq, qcaspi_intr_handler, 0,
			  dev->name, qca);
	if (ret) {
		netdev_err(dev, "%s: unable to get IRQ %d (irqval=%d).\n",
			   QCASPI_DRV_NAME, qca->spi_dev->irq, ret);
		kthread_stop(qca->spi_thread);
		return ret;
	}

	/* SPI thread takes care of TX queue */

	return 0;
}

static int
qcaspi_netdev_close(struct net_device *dev)
{
	struct qcaspi *qca = netdev_priv(dev);

	netif_stop_queue(dev);

	qcaspi_write_register(qca, SPI_REG_INTR_ENABLE, 0, wr_verify);
	free_irq(qca->spi_dev->irq, qca);

	kthread_stop(qca->spi_thread);
	qca->spi_thread = NULL;
	qcaspi_flush_tx_ring(qca);

	return 0;
}

static netdev_tx_t
qcaspi_netdev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	u32 frame_len;
	u8 *ptmp;
	struct qcaspi *qca = netdev_priv(dev);
	u16 new_tail;
	struct sk_buff *tskb;
	u8 pad_len = 0;

	if (skb->len < QCAFRM_MIN_LEN)
		pad_len = QCAFRM_MIN_LEN - skb->len;

	if (qca->txr.skb[qca->txr.tail]) {
		netdev_warn(qca->net_dev, "queue was unexpectedly full!\n");
		netif_stop_queue(qca->net_dev);
		qca->stats.ring_full++;
		return NETDEV_TX_BUSY;
	}

	if ((skb_headroom(skb) < QCAFRM_HEADER_LEN) ||
	    (skb_tailroom(skb) < QCAFRM_FOOTER_LEN + pad_len)) {
		tskb = skb_copy_expand(skb, QCAFRM_HEADER_LEN,
				       QCAFRM_FOOTER_LEN + pad_len, GFP_ATOMIC);
		if (!tskb) {
			qca->stats.out_of_mem++;
			return NETDEV_TX_BUSY;
		}
		dev_kfree_skb(skb);
		skb = tskb;
	}

	frame_len = skb->len + pad_len;

	ptmp = skb_push(skb, QCAFRM_HEADER_LEN);
	qcafrm_create_header(ptmp, frame_len);

	if (pad_len) {
		ptmp = skb_put_zero(skb, pad_len);
	}

	ptmp = skb_put(skb, QCAFRM_FOOTER_LEN);
	qcafrm_create_footer(ptmp);

	netdev_dbg(qca->net_dev, "Tx-ing packet: Size: 0x%08x\n",
		   skb->len);

	qca->txr.size += skb->len + QCASPI_HW_PKT_LEN;

	new_tail = qca->txr.tail + 1;
	if (new_tail >= qca->txr.count)
		new_tail = 0;

	qca->txr.skb[qca->txr.tail] = skb;
	qca->txr.tail = new_tail;

	if (!qcaspi_tx_ring_has_space(&qca->txr)) {
		netif_stop_queue(qca->net_dev);
		qca->stats.ring_full++;
	}

	netif_trans_update(dev);

	if (qca->spi_thread)
		wake_up_process(qca->spi_thread);

	return NETDEV_TX_OK;
}

static void
qcaspi_netdev_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct qcaspi *qca = netdev_priv(dev);

	netdev_info(qca->net_dev, "Transmit timeout at %ld, latency %ld\n",
		    jiffies, jiffies - dev_trans_start(dev));
	qca->net_dev->stats.tx_errors++;
	/* Trigger tx queue flush and QCA7000 reset */
	qca->sync = QCASPI_SYNC_UNKNOWN;

	if (qca->spi_thread)
		wake_up_process(qca->spi_thread);
}

static int
qcaspi_netdev_init(struct net_device *dev)
{
	struct qcaspi *qca = netdev_priv(dev);

	dev->mtu = QCAFRM_MAX_MTU;
	dev->type = ARPHRD_ETHER;
	qca->clkspeed = qcaspi_clkspeed;
	qca->burst_len = qcaspi_burst_len;
	qca->spi_thread = NULL;
	qca->buffer_size = (dev->mtu + VLAN_ETH_HLEN + QCAFRM_HEADER_LEN +
		QCAFRM_FOOTER_LEN + 4) * 4;

	memset(&qca->stats, 0, sizeof(struct qcaspi_stats));

	qca->rx_buffer = kmalloc(qca->buffer_size, GFP_KERNEL);
	if (!qca->rx_buffer)
		return -ENOBUFS;

	qca->rx_skb = netdev_alloc_skb_ip_align(dev, qca->net_dev->mtu +
						VLAN_ETH_HLEN);
	if (!qca->rx_skb) {
		kfree(qca->rx_buffer);
		netdev_info(qca->net_dev, "Failed to allocate RX sk_buff.\n");
		return -ENOBUFS;
	}

	return 0;
}

static void
qcaspi_netdev_uninit(struct net_device *dev)
{
	struct qcaspi *qca = netdev_priv(dev);

	kfree(qca->rx_buffer);
	qca->buffer_size = 0;
	dev_kfree_skb(qca->rx_skb);
}

static const struct net_device_ops qcaspi_netdev_ops = {
	.ndo_init = qcaspi_netdev_init,
	.ndo_uninit = qcaspi_netdev_uninit,
	.ndo_open = qcaspi_netdev_open,
	.ndo_stop = qcaspi_netdev_close,
	.ndo_start_xmit = qcaspi_netdev_xmit,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_tx_timeout = qcaspi_netdev_tx_timeout,
	.ndo_validate_addr = eth_validate_addr,
};

static void
qcaspi_netdev_setup(struct net_device *dev)
{
	struct qcaspi *qca = NULL;

	dev->netdev_ops = &qcaspi_netdev_ops;
	qcaspi_set_ethtool_ops(dev);
	dev->watchdog_timeo = QCASPI_TX_TIMEOUT;
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->tx_queue_len = 100;

	/* MTU range: 46 - 1500 */
	dev->min_mtu = QCAFRM_MIN_MTU;
	dev->max_mtu = QCAFRM_MAX_MTU;

	qca = netdev_priv(dev);
	memset(qca, 0, sizeof(struct qcaspi));

	memset(&qca->txr, 0, sizeof(qca->txr));
	qca->txr.count = TX_RING_MAX_LEN;
}

static const struct of_device_id qca_spi_of_match[] = {
	{ .compatible = "qca,qca7000" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, qca_spi_of_match);

static int
qca_spi_probe(struct spi_device *spi)
{
	struct qcaspi *qca = NULL;
	struct net_device *qcaspi_devs = NULL;
	u8 legacy_mode = 0;
	u16 signature;
	int ret;

	if (!spi->dev.of_node) {
		dev_err(&spi->dev, "Missing device tree\n");
		return -EINVAL;
	}

	legacy_mode = of_property_read_bool(spi->dev.of_node,
					    "qca,legacy-mode");

	if (qcaspi_clkspeed == 0) {
		if (spi->max_speed_hz)
			qcaspi_clkspeed = spi->max_speed_hz;
		else
			qcaspi_clkspeed = QCASPI_CLK_SPEED;
	}

	if ((qcaspi_clkspeed < QCASPI_CLK_SPEED_MIN) ||
	    (qcaspi_clkspeed > QCASPI_CLK_SPEED_MAX)) {
		dev_err(&spi->dev, "Invalid clkspeed: %d\n",
			qcaspi_clkspeed);
		return -EINVAL;
	}

	if ((qcaspi_burst_len < QCASPI_BURST_LEN_MIN) ||
	    (qcaspi_burst_len > QCASPI_BURST_LEN_MAX)) {
		dev_err(&spi->dev, "Invalid burst len: %d\n",
			qcaspi_burst_len);
		return -EINVAL;
	}

	if ((qcaspi_pluggable < QCASPI_PLUGGABLE_MIN) ||
	    (qcaspi_pluggable > QCASPI_PLUGGABLE_MAX)) {
		dev_err(&spi->dev, "Invalid pluggable: %d\n",
			qcaspi_pluggable);
		return -EINVAL;
	}

	if (wr_verify < QCASPI_WRITE_VERIFY_MIN ||
	    wr_verify > QCASPI_WRITE_VERIFY_MAX) {
		dev_err(&spi->dev, "Invalid write verify: %d\n",
			wr_verify);
		return -EINVAL;
	}

	dev_info(&spi->dev, "ver=%s, clkspeed=%d, burst_len=%d, pluggable=%d\n",
		 QCASPI_DRV_VERSION,
		 qcaspi_clkspeed,
		 qcaspi_burst_len,
		 qcaspi_pluggable);

	spi->mode = SPI_MODE_3;
	spi->max_speed_hz = qcaspi_clkspeed;
	if (spi_setup(spi) < 0) {
		dev_err(&spi->dev, "Unable to setup SPI device\n");
		return -EFAULT;
	}

	qcaspi_devs = alloc_etherdev(sizeof(struct qcaspi));
	if (!qcaspi_devs)
		return -ENOMEM;

	qcaspi_netdev_setup(qcaspi_devs);
	SET_NETDEV_DEV(qcaspi_devs, &spi->dev);

	qca = netdev_priv(qcaspi_devs);
	if (!qca) {
		free_netdev(qcaspi_devs);
		dev_err(&spi->dev, "Fail to retrieve private structure\n");
		return -ENOMEM;
	}
	qca->net_dev = qcaspi_devs;
	qca->spi_dev = spi;
	qca->legacy_mode = legacy_mode;

	spi_set_drvdata(spi, qcaspi_devs);

	ret = of_get_ethdev_address(spi->dev.of_node, qca->net_dev);
	if (ret) {
		eth_hw_addr_random(qca->net_dev);
		dev_info(&spi->dev, "Using random MAC address: %pM\n",
			 qca->net_dev->dev_addr);
	}

	netif_carrier_off(qca->net_dev);

	if (!qcaspi_pluggable) {
		qcaspi_read_register(qca, SPI_REG_SIGNATURE, &signature);
		qcaspi_read_register(qca, SPI_REG_SIGNATURE, &signature);

		if (signature != QCASPI_GOOD_SIGNATURE) {
			dev_err(&spi->dev, "Invalid signature (0x%04X)\n",
				signature);
			free_netdev(qcaspi_devs);
			return -EFAULT;
		}
	}

	if (register_netdev(qcaspi_devs)) {
		dev_err(&spi->dev, "Unable to register net device %s\n",
			qcaspi_devs->name);
		free_netdev(qcaspi_devs);
		return -EFAULT;
	}

	qcaspi_init_device_debugfs(qca);

	return 0;
}

static void
qca_spi_remove(struct spi_device *spi)
{
	struct net_device *qcaspi_devs = spi_get_drvdata(spi);
	struct qcaspi *qca = netdev_priv(qcaspi_devs);

	qcaspi_remove_device_debugfs(qca);

	unregister_netdev(qcaspi_devs);
	free_netdev(qcaspi_devs);
}

static const struct spi_device_id qca_spi_id[] = {
	{ "qca7000", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, qca_spi_id);

static struct spi_driver qca_spi_driver = {
	.driver	= {
		.name	= QCASPI_DRV_NAME,
		.of_match_table = qca_spi_of_match,
	},
	.id_table = qca_spi_id,
	.probe    = qca_spi_probe,
	.remove   = qca_spi_remove,
};
module_spi_driver(qca_spi_driver);

MODULE_DESCRIPTION("Qualcomm Atheros QCA7000 SPI Driver");
MODULE_AUTHOR("Qualcomm Atheros Communications");
MODULE_AUTHOR("Stefan Wahren <stefan.wahren@i2se.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(QCASPI_DRV_VERSION);
