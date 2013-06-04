/*
 * mac80211 glue code for mac80211 ST-Ericsson CW1200 drivers
 * ITP code
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/poll.h>
#include <linux/time.h>
#include <linux/random.h>
#include <linux/kallsyms.h>
#include <net/mac80211.h>
#include "cw1200.h"
#include "debug.h"
#include "itp.h"
#include "sta.h"

static int __cw1200_itp_open(struct cw1200_common *priv);
static int __cw1200_itp_close(struct cw1200_common *priv);
static void cw1200_itp_rx_start(struct cw1200_common *priv);
static void cw1200_itp_rx_stop(struct cw1200_common *priv);
static void cw1200_itp_rx_stats(struct cw1200_common *priv);
static void cw1200_itp_rx_reset(struct cw1200_common *priv);
static void cw1200_itp_tx_stop(struct cw1200_common *priv);
static void cw1200_itp_handle(struct cw1200_common *priv,
			      struct sk_buff *skb);
static void cw1200_itp_err(struct cw1200_common *priv,
			   int err,
			   int arg);
static void __cw1200_itp_tx_stop(struct cw1200_common *priv);

static ssize_t cw1200_itp_read(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct cw1200_common *priv = file->private_data;
	struct cw1200_itp *itp = &priv->debug->itp;
	struct sk_buff *skb;
	int ret;

	if (skb_queue_empty(&itp->log_queue))
		return 0;

	skb = skb_dequeue(&itp->log_queue);
	ret = copy_to_user(user_buf, skb->data, skb->len);
	*ppos += skb->len;
	skb->data[skb->len] = 0;
	pr_debug("[ITP] >>> %s", skb->data);
	consume_skb(skb);

	return skb->len - ret;
}

static ssize_t cw1200_itp_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct cw1200_common *priv = file->private_data;
	struct sk_buff *skb;

	if (!count || count > 1024)
		return -EINVAL;
	skb = dev_alloc_skb(count + 1);
	if (!skb)
		return -ENOMEM;
	skb_trim(skb, 0);
	skb_put(skb, count + 1);
	if (copy_from_user(skb->data, user_buf, count)) {
		kfree_skb(skb);
		return -EFAULT;
	}
	skb->data[count] = 0;

	cw1200_itp_handle(priv, skb);
	consume_skb(skb);
	return count;
}

static unsigned int cw1200_itp_poll(struct file *file, poll_table *wait)
{
	struct cw1200_common *priv = file->private_data;
	struct cw1200_itp *itp = &priv->debug->itp;
	unsigned int mask = 0;

	poll_wait(file, &itp->read_wait, wait);

	if (!skb_queue_empty(&itp->log_queue))
		mask |= POLLIN | POLLRDNORM;

	mask |= POLLOUT | POLLWRNORM;

	return mask;
}

static int cw1200_itp_open(struct inode *inode, struct file *file)
{
	struct cw1200_common *priv = inode->i_private;
	struct cw1200_itp *itp = &priv->debug->itp;
	int ret = 0;

	file->private_data = priv;
	if (atomic_inc_return(&itp->open_count) == 1) {
		ret = __cw1200_itp_open(priv);
		if (ret && !atomic_dec_return(&itp->open_count))
			__cw1200_itp_close(priv);
	} else {
		atomic_dec(&itp->open_count);
		ret = -EBUSY;
	}

	return ret;
}

static int cw1200_itp_close(struct inode *inode, struct file *file)
{
	struct cw1200_common *priv = file->private_data;
	struct cw1200_itp *itp = &priv->debug->itp;
	if (!atomic_dec_return(&itp->open_count)) {
		__cw1200_itp_close(priv);
		wake_up(&itp->close_wait);
	}
	return 0;
}

static const struct file_operations fops_itp = {
	.open = cw1200_itp_open,
	.read = cw1200_itp_read,
	.write = cw1200_itp_write,
	.poll = cw1200_itp_poll,
	.release = cw1200_itp_close,
	.llseek = default_llseek,
	.owner = THIS_MODULE,
};

static void cw1200_itp_fill_pattern(u8 *data, int size,
		enum cw1200_itp_data_modes mode)
{
	if (size <= 0)
		return;

	switch (mode) {
	default:
	case ITP_DATA_ZEROS:
		memset(data, 0x0, size);
		break;
	case ITP_DATA_ONES:
		memset(data, 0xff, size);
		break;
	case ITP_DATA_ZERONES:
		memset(data, 0x55, size);
		break;
	case ITP_DATA_RANDOM:
		get_random_bytes(data, size);
		break;
	}
	return;
}

static void cw1200_itp_tx_work(struct work_struct *work)
{
	struct cw1200_itp *itp = container_of(work, struct cw1200_itp,
		    tx_work.work);
	struct cw1200_common *priv = itp->priv;
	atomic_set(&priv->bh_tx, 1);
	wake_up(&priv->bh_wq);
}

static void cw1200_itp_tx_finish(struct work_struct *work)
{
	struct cw1200_itp *itp = container_of(work, struct cw1200_itp,
		    tx_finish.work);
	__cw1200_itp_tx_stop(itp->priv);
}

int cw1200_itp_init(struct cw1200_common *priv)
{
	struct cw1200_itp *itp = &priv->debug->itp;

	itp->priv = priv;
	atomic_set(&itp->open_count, 0);
	atomic_set(&itp->stop_tx, 0);
	atomic_set(&itp->awaiting_confirm, 0);
	skb_queue_head_init(&itp->log_queue);
	spin_lock_init(&itp->tx_lock);
	init_waitqueue_head(&itp->read_wait);
	init_waitqueue_head(&itp->write_wait);
	init_waitqueue_head(&itp->close_wait);
	INIT_DELAYED_WORK(&itp->tx_work, cw1200_itp_tx_work);
	INIT_DELAYED_WORK(&itp->tx_finish, cw1200_itp_tx_finish);
	itp->data = NULL;
	itp->hdr_len = WSM_TX_EXTRA_HEADROOM +
			sizeof(struct ieee80211_hdr_3addr);

	if (!debugfs_create_file("itp", S_IRUSR | S_IWUSR,
				 priv->debug->debugfs_phy, priv, &fops_itp))
		return -ENOMEM;

	return 0;
}

void cw1200_itp_release(struct cw1200_common *priv)
{
	struct cw1200_itp *itp = &priv->debug->itp;

	wait_event_interruptible(itp->close_wait,
				 !atomic_read(&itp->open_count));

	WARN_ON(atomic_read(&itp->open_count));

	skb_queue_purge(&itp->log_queue);
	cw1200_itp_tx_stop(priv);
}

static int __cw1200_itp_open(struct cw1200_common *priv)
{
	struct cw1200_itp *itp = &priv->debug->itp;

	if (!priv->vif)
		return -EINVAL;
	if (priv->join_status)
		return -EINVAL;
	itp->saved_channel = priv->channel;
	if (!priv->channel)
		priv->channel = &priv->hw->wiphy->bands[IEEE80211_BAND_2GHZ]->channels[0];
	wsm_set_bssid_filtering(priv, false);
	cw1200_itp_rx_reset(priv);
	return 0;
}

static int __cw1200_itp_close(struct cw1200_common *priv)
{
	struct cw1200_itp *itp = &priv->debug->itp;
	if (atomic_read(&itp->test_mode) == TEST_MODE_RX_TEST)
		cw1200_itp_rx_stop(priv);
	cw1200_itp_tx_stop(priv);
	cw1200_disable_listening(priv);
	cw1200_update_filtering(priv);
	priv->channel = itp->saved_channel;
	return 0;
}

bool cw1200_is_itp(struct cw1200_common *priv)
{
	struct cw1200_itp *itp = &priv->debug->itp;
	return atomic_read(&itp->open_count) != 0;
}

static void cw1200_itp_rx_reset(struct cw1200_common *priv)
{
	struct cw1200_itp *itp = &priv->debug->itp;
	itp->rx_cnt = 0;
	itp->rx_rssi = 0;
	itp->rx_rssi_max = -1000;
	itp->rx_rssi_min = 1000;
}

static void cw1200_itp_rx_start(struct cw1200_common *priv)
{
	struct cw1200_itp *itp = &priv->debug->itp;

	pr_debug("[ITP] RX start, band = %d, ch = %d\n",
		 itp->band, itp->ch);
	atomic_set(&itp->test_mode, TEST_MODE_RX_TEST);
	cw1200_update_listening(priv, false);
	priv->channel = &priv->hw->
		wiphy->bands[itp->band]->channels[itp->ch];
	cw1200_update_listening(priv, true);
	wsm_set_bssid_filtering(priv, false);
}

static void cw1200_itp_rx_stop(struct cw1200_common *priv)
{
	struct cw1200_itp *itp = &priv->debug->itp;
	pr_debug("[ITP] RX stop\n");
	atomic_set(&itp->test_mode, TEST_MODE_NO_TEST);
	cw1200_itp_rx_reset(priv);
}

static void cw1200_itp_rx_stats(struct cw1200_common *priv)
{
	struct cw1200_itp *itp = &priv->debug->itp;
	struct sk_buff *skb;
	char buf[128];
	int len, ret;
	struct wsm_mib_counters_table counters;

	ret = wsm_get_counters_table(priv, &counters);

	if (ret)
		cw1200_itp_err(priv, -EBUSY, 20);

	if (!itp->rx_cnt)
		len = snprintf(buf, sizeof(buf), "1,0,0,0,0,%d\n",
			       counters.rx_packet_errors);
	else
		len = snprintf(buf, sizeof(buf), "1,%d,%ld,%d,%d,%d\n",
			       itp->rx_cnt,
			       itp->rx_cnt ? itp->rx_rssi / itp->rx_cnt : 0,
			       itp->rx_rssi_min, itp->rx_rssi_max,
			       counters.rx_packet_errors);

	if (len <= 0) {
		cw1200_itp_err(priv, -EBUSY, 21);
		return;
	}

	skb = dev_alloc_skb(len);
	if (!skb) {
		cw1200_itp_err(priv, -ENOMEM, 22);
		return;
	}

	itp->rx_cnt = 0;
	itp->rx_rssi = 0;
	itp->rx_rssi_max = -1000;
	itp->rx_rssi_min = 1000;

	skb_trim(skb, 0);
	skb_put(skb, len);

	memcpy(skb->data, buf, len);
	skb_queue_tail(&itp->log_queue, skb);
	wake_up(&itp->read_wait);
}

static void cw1200_itp_tx_start(struct cw1200_common *priv)
{
	struct wsm_tx *tx;
	struct ieee80211_hdr_3addr *hdr;
	struct cw1200_itp *itp = &priv->debug->itp;
	struct wsm_mib_association_mode assoc_mode = {
		.flags = WSM_ASSOCIATION_MODE_USE_PREAMBLE_TYPE,
		.preamble = itp->preamble,
	};
	int len;
	u8 da_addr[6] = ITP_DEFAULT_DA_ADDR;

	/* Rates index 4 and 5 are not supported */
	if (itp->rate > 3)
		itp->rate += 2;

	pr_debug("[ITP] TX start: band = %d, ch = %d, rate = %d, preamble = %d, number = %d, data_mode = %d, interval = %d, power = %d, data_len = %d\n",
		 itp->band, itp->ch, itp->rate, itp->preamble,
		 itp->number, itp->data_mode, itp->interval_us,
		 itp->power, itp->data_len);

	len = itp->hdr_len + itp->data_len;

	itp->data = kmalloc(len, GFP_KERNEL);
	tx = (struct wsm_tx *)itp->data;
	tx->hdr.len = itp->data_len + itp->hdr_len;
	tx->hdr.id = __cpu_to_le16(0x0004 | 1 << 6);
	tx->max_tx_rate = itp->rate;
	tx->queue_id = 3;
	tx->more = 0;
	tx->flags = 0xc;
	tx->packet_id = 0x55ff55;
	tx->reserved = 0;
	tx->expire_time = 1;

	if (itp->preamble == ITP_PREAMBLE_GREENFIELD)
		tx->ht_tx_parameters = WSM_HT_TX_GREENFIELD;
	else if (itp->preamble == ITP_PREAMBLE_MIXED)
		tx->ht_tx_parameters = WSM_HT_TX_MIXED;

	hdr = (struct ieee80211_hdr_3addr *)&itp->data[sizeof(struct wsm_tx)];
	memset(hdr, 0, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |	IEEE80211_FCTL_TODS);
	memcpy(hdr->addr1, da_addr, ETH_ALEN);
	memcpy(hdr->addr2, priv->vif->addr, ETH_ALEN);
	memcpy(hdr->addr3, da_addr, ETH_ALEN);

	cw1200_itp_fill_pattern(&itp->data[itp->hdr_len],
				itp->data_len, itp->data_mode);

	cw1200_update_listening(priv, false);
	priv->channel = &priv->hw->wiphy->bands[itp->band]->channels[itp->ch];
	WARN_ON(wsm_set_output_power(priv, itp->power));
	if (itp->preamble == ITP_PREAMBLE_SHORT ||
	    itp->preamble == ITP_PREAMBLE_LONG)
		WARN_ON(wsm_set_association_mode(priv,
						 &assoc_mode));
	wsm_set_bssid_filtering(priv, false);
	cw1200_update_listening(priv, true);

	spin_lock_bh(&itp->tx_lock);
	atomic_set(&itp->test_mode, TEST_MODE_TX_TEST);
	atomic_set(&itp->awaiting_confirm, 0);
	atomic_set(&itp->stop_tx, 0);
	atomic_set(&priv->bh_tx, 1);
	ktime_get_ts(&itp->last_sent);
	wake_up(&priv->bh_wq);
	spin_unlock_bh(&itp->tx_lock);
}

void __cw1200_itp_tx_stop(struct cw1200_common *priv)
{
	struct cw1200_itp *itp = &priv->debug->itp;
	spin_lock_bh(&itp->tx_lock);
	kfree(itp->data);
	itp->data = NULL;
	atomic_set(&itp->test_mode, TEST_MODE_NO_TEST);
	spin_unlock_bh(&itp->tx_lock);
}

static void cw1200_itp_tx_stop(struct cw1200_common *priv)
{
	struct cw1200_itp *itp = &priv->debug->itp;
	pr_debug("[ITP] TX stop\n");
	atomic_set(&itp->stop_tx, 1);
	flush_workqueue(priv->workqueue);

	/* time for FW to confirm all tx requests */
	msleep(500);

	__cw1200_itp_tx_stop(priv);
}

static int cw1200_print_fw_version(struct cw1200_common *priv,
				   u8 *buf, size_t len)
{
	return snprintf(buf, len, "%s %d.%d",
			cw1200_fw_types[priv->wsm_caps.fw_type],
			priv->wsm_caps.fw_ver,
			priv->wsm_caps.fw_build);
}

static void cw1200_itp_get_version(struct cw1200_common *priv,
		enum cw1200_itp_version_type type)
{
	struct cw1200_itp *itp = &priv->debug->itp;
	struct sk_buff *skb;
	char buf[ITP_BUF_SIZE];
	size_t size = 0;
	int len;
	pr_debug("[ITP] print %s version\n",
		 type == ITP_CHIP_ID ? "chip" : "firmware");

	len = snprintf(buf, ITP_BUF_SIZE, "2,");
	if (len <= 0) {
		cw1200_itp_err(priv, -EINVAL, 40);
		return;
	}
	size += len;

	switch (type) {
	case ITP_CHIP_ID:
		len = cw1200_print_fw_version(priv, buf+size,
				ITP_BUF_SIZE - size);

		if (len <= 0) {
			cw1200_itp_err(priv, -EINVAL, 41);
			return;
		}
		size += len;
		break;
	case ITP_FW_VER:
		len = snprintf(buf+size, ITP_BUF_SIZE - size,
				"%d.%d", priv->wsm_caps.hw_id,
				priv->wsm_caps.hw_subid);
		if (len <= 0) {
			cw1200_itp_err(priv, -EINVAL, 42);
			return;
		}
		size += len;
		break;
	default:
		cw1200_itp_err(priv, -EINVAL, 43);
		break;
	}

	len = snprintf(buf+size, ITP_BUF_SIZE-size, "\n");
	if (len <= 0) {
		cw1200_itp_err(priv, -EINVAL, 44);
		return;
	}
	size += len;

	skb = dev_alloc_skb(size);
	if (!skb) {
		cw1200_itp_err(priv, -ENOMEM, 45);
		return;
	}

	skb_trim(skb, 0);
	skb_put(skb, size);

	memcpy(skb->data, buf, size);
	skb_queue_tail(&itp->log_queue, skb);
	wake_up(&itp->read_wait);
}

int cw1200_itp_get_tx(struct cw1200_common *priv, u8 **data,
		size_t *tx_len, int *burst)
{
	struct cw1200_itp *itp;
	struct timespec now;
	int time_left_us;

	if (!priv->debug)
		return 0;

	itp	= &priv->debug->itp;

	if (!itp)
		return 0;

	spin_lock_bh(&itp->tx_lock);
	if (atomic_read(&itp->test_mode) != TEST_MODE_TX_TEST)
		goto out;

	if (atomic_read(&itp->stop_tx))
		goto out;

	if (itp->number == 0) {
		atomic_set(&itp->stop_tx, 1);
		queue_delayed_work(priv->workqueue, &itp->tx_finish, HZ/10);
		goto out;
	}

	if (!itp->data)
		goto out;

	if (priv->hw_bufs_used >= 2) {
		if (!atomic_read(&priv->bh_rx))
			atomic_set(&priv->bh_rx, 1);
		atomic_set(&priv->bh_tx, 1);
		goto out;
	}

	ktime_get_ts(&now);
	time_left_us = (itp->last_sent.tv_sec -	now.tv_sec)*1000000 +
		(itp->last_sent.tv_nsec - now.tv_nsec)/1000 +
		itp->interval_us;

	if (time_left_us > ITP_TIME_THRES_US) {
		queue_delayed_work(priv->workqueue, &itp->tx_work,
				   ITP_US_TO_MS(time_left_us)*HZ/1000);
		goto out;
	}

	if (time_left_us > 50)
		udelay(time_left_us);

	if (itp->number > 0)
		itp->number--;

	*data = itp->data;
	*tx_len = itp->data_len + itp->hdr_len;

	if (itp->data_mode == ITP_DATA_RANDOM)
		cw1200_itp_fill_pattern(&itp->data[itp->hdr_len],
					itp->data_len, itp->data_mode);
	*burst = 2;
	atomic_set(&priv->bh_tx, 1);
	ktime_get_ts(&itp->last_sent);
	atomic_add(1, &itp->awaiting_confirm);
	spin_unlock_bh(&itp->tx_lock);
	return 1;

out:
	spin_unlock_bh(&itp->tx_lock);
	return 0;
}

bool cw1200_itp_rxed(struct cw1200_common *priv, struct sk_buff *skb)
{
	struct cw1200_itp *itp = &priv->debug->itp;
	struct ieee80211_rx_status *rx = IEEE80211_SKB_RXCB(skb);
	int signal;

	if (atomic_read(&itp->test_mode) != TEST_MODE_RX_TEST)
		return cw1200_is_itp(priv);
	if (rx->freq != priv->channel->center_freq)
		return true;

	signal = rx->signal;
	itp->rx_cnt++;
	itp->rx_rssi += signal;
	if (itp->rx_rssi_min > rx->signal)
		itp->rx_rssi_min = rx->signal;
	if (itp->rx_rssi_max < rx->signal)
		itp->rx_rssi_max = rx->signal;

	return true;
}

void cw1200_itp_wake_up_tx(struct cw1200_common *priv)
{
	wake_up(&priv->debug->itp.write_wait);
}

bool cw1200_itp_tx_running(struct cw1200_common *priv)
{
	if (atomic_read(&priv->debug->itp.awaiting_confirm) ||
	    atomic_read(&priv->debug->itp.test_mode) ==
	    TEST_MODE_TX_TEST) {
		atomic_sub(1, &priv->debug->itp.awaiting_confirm);
		return true;
	}
	return false;
}

static void cw1200_itp_handle(struct cw1200_common *priv,
			      struct sk_buff *skb)
{
	struct cw1200_itp *itp = &priv->debug->itp;
	const struct wiphy *wiphy = priv->hw->wiphy;
	int cmd;
	int ret;

	pr_debug("[ITP] <<< %s", skb->data);
	if (sscanf(skb->data, "%d", &cmd) != 1) {
		cw1200_itp_err(priv, -EINVAL, 1);
		return;
	}

	switch (cmd) {
	case 1: /* RX test */
		if (atomic_read(&itp->test_mode)) {
			cw1200_itp_err(priv, -EBUSY, 0);
			return;
		}
		ret = sscanf(skb->data, "%d,%d,%d",
				&cmd, &itp->band, &itp->ch);
		if (ret != 3) {
			cw1200_itp_err(priv, -EINVAL, ret + 1);
			return;
		}
		if (itp->band >= 2) {
			cw1200_itp_err(priv, -EINVAL, 2);
		} else if (!wiphy->bands[itp->band]) {
			cw1200_itp_err(priv, -EINVAL, 2);
		} else if (itp->ch >= wiphy->bands[itp->band]->n_channels) {
			cw1200_itp_err(priv, -EINVAL, 3);
		} else {
			cw1200_itp_rx_stats(priv);
			cw1200_itp_rx_start(priv);
		}
		break;
	case 2: /* RX stat */
		cw1200_itp_rx_stats(priv);
		break;
	case 3: /* RX/TX stop */
		if (atomic_read(&itp->test_mode) == TEST_MODE_RX_TEST) {
			cw1200_itp_rx_stats(priv);
			cw1200_itp_rx_stop(priv);
		} else if (atomic_read(&itp->test_mode) == TEST_MODE_TX_TEST) {
			cw1200_itp_tx_stop(priv);
		} else {
			cw1200_itp_err(priv, -EBUSY, 0);
		}
		break;
	case 4: /* TX start */
		if (atomic_read(&itp->test_mode) != TEST_MODE_NO_TEST) {
			cw1200_itp_err(priv, -EBUSY, 0);
			return;
		}
		ret = sscanf(skb->data, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
				&cmd, &itp->band, &itp->ch, &itp->rate,
				&itp->preamble, &itp->number, &itp->data_mode,
				&itp->interval_us, &itp->power, &itp->data_len);
		if (ret != 10) {
			cw1200_itp_err(priv, -EINVAL, ret + 1);
			return;
		}
		if (itp->band >= 2) {
			cw1200_itp_err(priv, -EINVAL, 2);
		} else if (!wiphy->bands[itp->band]) {
			cw1200_itp_err(priv, -EINVAL, 2);
		} else if (itp->ch >= wiphy->bands[itp->band]->n_channels) {
			cw1200_itp_err(priv, -EINVAL, 3);
		} else if (itp->rate >= 20) {
			cw1200_itp_err(priv, -EINVAL, 4);
		} else if (itp->preamble >= ITP_PREAMBLE_MAX) {
			cw1200_itp_err(priv, -EINVAL, 5);
		} else if (itp->data_mode >= ITP_DATA_MAX_MODE) {
			cw1200_itp_err(priv, -EINVAL, 7);
		} else if (itp->data_len < ITP_MIN_DATA_SIZE ||
			   itp->data_len > (priv->wsm_caps.input_buffer_size - itp->hdr_len)) {
			cw1200_itp_err(priv, -EINVAL, 8);
		} else {
		    cw1200_itp_tx_start(priv);
		}
		break;
	case 5:
		cw1200_itp_get_version(priv, ITP_CHIP_ID);
		break;
	case 6:
		cw1200_itp_get_version(priv, ITP_FW_VER);
		break;
	}
}

static void cw1200_itp_err(struct cw1200_common *priv,
			   int err, int arg)
{
	struct cw1200_itp *itp = &priv->debug->itp;
	struct sk_buff *skb;
	static char buf[255];
	int len;

	len = snprintf(buf, sizeof(buf), "%d,%d\n",
		err, arg);
	if (len <= 0)
		return;

	skb = dev_alloc_skb(len);
	if (!skb)
		return;

	skb_trim(skb, 0);
	skb_put(skb, len);

	memcpy(skb->data, buf, len);
	skb_queue_tail(&itp->log_queue, skb);
	wake_up(&itp->read_wait);

	len = sprint_symbol(buf,
			(unsigned long)__builtin_return_address(0));
	if (len <= 0)
		return;
	pr_debug("[ITP] error %d,%d from %s\n",
		 err, arg, buf);
}
