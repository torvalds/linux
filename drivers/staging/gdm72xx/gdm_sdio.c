/*
 * Copyright (c) 2012 GCT Semiconductor, Inc. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>

#include "gdm_sdio.h"
#include "gdm_wimax.h"
#include "sdio_boot.h"
#include "hci.h"

#define TYPE_A_HEADER_SIZE	4
#define TYPE_A_LOOKAHEAD_SIZE	16

#define MAX_NR_RX_BUF	4

#define SDU_TX_BUF_SIZE	2048
#define TX_BUF_SIZE	2048
#define TX_CHUNK_SIZE	(2048 - TYPE_A_HEADER_SIZE)
#define RX_BUF_SIZE	(25*1024)

#define TX_HZ		2000
#define TX_INTERVAL	(1000000/TX_HZ)

static struct sdio_tx *alloc_tx_struct(struct tx_cxt *tx)
{
	struct sdio_tx *t = kzalloc(sizeof(*t), GFP_ATOMIC);

	if (!t)
		return NULL;

	t->buf = kmalloc(TX_BUF_SIZE, GFP_ATOMIC);
	if (!t->buf) {
		kfree(t);
		return NULL;
	}

	t->tx_cxt = tx;

	return t;
}

static void free_tx_struct(struct sdio_tx *t)
{
	if (t) {
		kfree(t->buf);
		kfree(t);
	}
}

static struct sdio_rx *alloc_rx_struct(struct rx_cxt *rx)
{
	struct sdio_rx *r = kzalloc(sizeof(*r), GFP_ATOMIC);

	if (r)
		r->rx_cxt = rx;

	return r;
}

static void free_rx_struct(struct sdio_rx *r)
{
	kfree(r);
}

/* Before this function is called, spin lock should be locked. */
static struct sdio_tx *get_tx_struct(struct tx_cxt *tx, int *no_spc)
{
	struct sdio_tx *t;

	if (list_empty(&tx->free_list))
		return NULL;

	t = list_entry(tx->free_list.prev, struct sdio_tx, list);
	list_del(&t->list);

	*no_spc = list_empty(&tx->free_list) ? 1 : 0;

	return t;
}

/* Before this function is called, spin lock should be locked. */
static void put_tx_struct(struct tx_cxt *tx, struct sdio_tx *t)
{
	list_add_tail(&t->list, &tx->free_list);
}

/* Before this function is called, spin lock should be locked. */
static struct sdio_rx *get_rx_struct(struct rx_cxt *rx)
{
	struct sdio_rx *r;

	if (list_empty(&rx->free_list))
		return NULL;

	r = list_entry(rx->free_list.prev, struct sdio_rx, list);
	list_del(&r->list);

	return r;
}

/* Before this function is called, spin lock should be locked. */
static void put_rx_struct(struct rx_cxt *rx, struct sdio_rx *r)
{
	list_add_tail(&r->list, &rx->free_list);
}

static void release_sdio(struct sdiowm_dev *sdev)
{
	struct tx_cxt	*tx = &sdev->tx;
	struct rx_cxt	*rx = &sdev->rx;
	struct sdio_tx	*t, *t_next;
	struct sdio_rx	*r, *r_next;

	kfree(tx->sdu_buf);

	list_for_each_entry_safe(t, t_next, &tx->free_list, list) {
		list_del(&t->list);
		free_tx_struct(t);
	}

	list_for_each_entry_safe(t, t_next, &tx->sdu_list, list) {
		list_del(&t->list);
		free_tx_struct(t);
	}

	list_for_each_entry_safe(t, t_next, &tx->hci_list, list) {
		list_del(&t->list);
		free_tx_struct(t);
	}

	kfree(rx->rx_buf);

	list_for_each_entry_safe(r, r_next, &rx->free_list, list) {
		list_del(&r->list);
		free_rx_struct(r);
	}

	list_for_each_entry_safe(r, r_next, &rx->req_list, list) {
		list_del(&r->list);
		free_rx_struct(r);
	}
}

static int init_sdio(struct sdiowm_dev *sdev)
{
	int ret = 0, i;
	struct tx_cxt *tx = &sdev->tx;
	struct rx_cxt *rx = &sdev->rx;
	struct sdio_tx *t;
	struct sdio_rx *r;

	INIT_LIST_HEAD(&tx->free_list);
	INIT_LIST_HEAD(&tx->sdu_list);
	INIT_LIST_HEAD(&tx->hci_list);

	spin_lock_init(&tx->lock);

	tx->sdu_buf = kmalloc(SDU_TX_BUF_SIZE, GFP_KERNEL);
	if (tx->sdu_buf == NULL)
		goto fail;

	for (i = 0; i < MAX_NR_SDU_BUF; i++) {
		t = alloc_tx_struct(tx);
		if (t == NULL) {
			ret = -ENOMEM;
			goto fail;
		}
		list_add(&t->list, &tx->free_list);
	}

	INIT_LIST_HEAD(&rx->free_list);
	INIT_LIST_HEAD(&rx->req_list);

	spin_lock_init(&rx->lock);

	for (i = 0; i < MAX_NR_RX_BUF; i++) {
		r = alloc_rx_struct(rx);
		if (r == NULL) {
			ret = -ENOMEM;
			goto fail;
		}
		list_add(&r->list, &rx->free_list);
	}

	rx->rx_buf = kmalloc(RX_BUF_SIZE, GFP_KERNEL);
	if (rx->rx_buf == NULL)
		goto fail;

	return 0;

fail:
	release_sdio(sdev);
	return ret;
}

static void send_sdio_pkt(struct sdio_func *func, u8 *data, int len)
{
	int n, blocks, ret, remain;

	sdio_claim_host(func);

	blocks = len / func->cur_blksize;
	n = blocks * func->cur_blksize;
	if (blocks) {
		ret = sdio_memcpy_toio(func, 0, data, n);
		if (ret < 0) {
			if (ret != -ENOMEDIUM)
				dev_err(&func->dev,
					"gdmwms:  error: ret = %d\n", ret);
			goto end_io;
		}
	}

	remain = len - n;
	remain = (remain + 3) & ~3;

	if (remain) {
		ret = sdio_memcpy_toio(func, 0, data + n, remain);
		if (ret < 0) {
			if (ret != -ENOMEDIUM)
				dev_err(&func->dev,
					"gdmwms:  error: ret = %d\n", ret);
			goto end_io;
		}
	}

end_io:
	sdio_release_host(func);
}

static void send_sdu(struct sdio_func *func, struct tx_cxt *tx)
{
	struct list_head *l, *next;
	struct hci_s *hci;
	struct sdio_tx *t;
	int pos, len, i, estlen, aggr_num = 0, aggr_len;
	u8 *buf;
	unsigned long flags;

	spin_lock_irqsave(&tx->lock, flags);

	pos = TYPE_A_HEADER_SIZE + HCI_HEADER_SIZE;
	list_for_each_entry(t, &tx->sdu_list, list) {
		estlen = ((t->len + 3) & ~3) + 4;
		if ((pos + estlen) > SDU_TX_BUF_SIZE)
			break;

		aggr_num++;
		memcpy(tx->sdu_buf + pos, t->buf, t->len);
		memset(tx->sdu_buf + pos + t->len, 0, estlen - t->len);
		pos += estlen;
	}
	aggr_len = pos;

	hci = (struct hci_s *)(tx->sdu_buf + TYPE_A_HEADER_SIZE);
	hci->cmd_evt = cpu_to_be16(WIMAX_TX_SDU_AGGR);
	hci->length = cpu_to_be16(aggr_len - TYPE_A_HEADER_SIZE -
				  HCI_HEADER_SIZE);

	spin_unlock_irqrestore(&tx->lock, flags);

	dev_dbg(&func->dev, "sdio_send: %*ph\n", aggr_len - TYPE_A_HEADER_SIZE,
		tx->sdu_buf + TYPE_A_HEADER_SIZE);

	for (pos = TYPE_A_HEADER_SIZE; pos < aggr_len; pos += TX_CHUNK_SIZE) {
		len = aggr_len - pos;
		len = len > TX_CHUNK_SIZE ? TX_CHUNK_SIZE : len;
		buf = tx->sdu_buf + pos - TYPE_A_HEADER_SIZE;

		buf[0] = len & 0xff;
		buf[1] = (len >> 8) & 0xff;
		buf[2] = (len >> 16) & 0xff;
		buf[3] = (pos + len) >= aggr_len ? 0 : 1;
		send_sdio_pkt(func, buf, len + TYPE_A_HEADER_SIZE);
	}

	spin_lock_irqsave(&tx->lock, flags);

	for (l = tx->sdu_list.next, i = 0; i < aggr_num; i++, l = next) {
		next = l->next;
		t = list_entry(l, struct sdio_tx, list);
		if (t->callback)
			t->callback(t->cb_data);

		list_del(l);
		put_tx_struct(t->tx_cxt, t);
	}

	do_gettimeofday(&tx->sdu_stamp);
	spin_unlock_irqrestore(&tx->lock, flags);
}

static void send_hci(struct sdio_func *func, struct tx_cxt *tx,
		     struct sdio_tx *t)
{
	unsigned long flags;

	dev_dbg(&func->dev, "sdio_send: %*ph\n", t->len - TYPE_A_HEADER_SIZE,
		t->buf + TYPE_A_HEADER_SIZE);

	send_sdio_pkt(func, t->buf, t->len);

	spin_lock_irqsave(&tx->lock, flags);
	if (t->callback)
		t->callback(t->cb_data);
	free_tx_struct(t);
	spin_unlock_irqrestore(&tx->lock, flags);
}

static void do_tx(struct work_struct *work)
{
	struct sdiowm_dev *sdev = container_of(work, struct sdiowm_dev, ws);
	struct sdio_func *func = sdev->func;
	struct tx_cxt *tx = &sdev->tx;
	struct sdio_tx *t = NULL;
	struct timeval now, *before;
	int is_sdu = 0;
	long diff;
	unsigned long flags;

	spin_lock_irqsave(&tx->lock, flags);
	if (!tx->can_send) {
		spin_unlock_irqrestore(&tx->lock, flags);
		return;
	}

	if (!list_empty(&tx->hci_list)) {
		t = list_entry(tx->hci_list.next, struct sdio_tx, list);
		list_del(&t->list);
		is_sdu = 0;
	} else if (!tx->stop_sdu_tx && !list_empty(&tx->sdu_list)) {
		do_gettimeofday(&now);
		before = &tx->sdu_stamp;

		diff = (now.tv_sec - before->tv_sec) * 1000000 +
			(now.tv_usec - before->tv_usec);
		if (diff >= 0 && diff < TX_INTERVAL) {
			schedule_work(&sdev->ws);
			spin_unlock_irqrestore(&tx->lock, flags);
			return;
		}
		is_sdu = 1;
	}

	if (!is_sdu && t == NULL) {
		spin_unlock_irqrestore(&tx->lock, flags);
		return;
	}

	tx->can_send = 0;

	spin_unlock_irqrestore(&tx->lock, flags);

	if (is_sdu)
		send_sdu(func, tx);
	else
		send_hci(func, tx, t);
}

static int gdm_sdio_send(void *priv_dev, void *data, int len,
			 void (*cb)(void *data), void *cb_data)
{
	struct sdiowm_dev *sdev = priv_dev;
	struct tx_cxt *tx = &sdev->tx;
	struct sdio_tx *t;
	u8 *pkt = data;
	int no_spc = 0;
	u16 cmd_evt;
	unsigned long flags;

	if (len > TX_BUF_SIZE - TYPE_A_HEADER_SIZE)
		return -EINVAL;

	spin_lock_irqsave(&tx->lock, flags);

	cmd_evt = (pkt[0] << 8) | pkt[1];
	if (cmd_evt == WIMAX_TX_SDU) {
		t = get_tx_struct(tx, &no_spc);
		if (t == NULL) {
			/* This case must not happen. */
			spin_unlock_irqrestore(&tx->lock, flags);
			return -ENOSPC;
		}
		list_add_tail(&t->list, &tx->sdu_list);

		memcpy(t->buf, data, len);

		t->len = len;
		t->callback = cb;
		t->cb_data = cb_data;
	} else {
		t = alloc_tx_struct(tx);
		if (t == NULL) {
			spin_unlock_irqrestore(&tx->lock, flags);
			return -ENOMEM;
		}
		list_add_tail(&t->list, &tx->hci_list);

		t->buf[0] = len & 0xff;
		t->buf[1] = (len >> 8) & 0xff;
		t->buf[2] = (len >> 16) & 0xff;
		t->buf[3] = 2;
		memcpy(t->buf + TYPE_A_HEADER_SIZE, data, len);

		t->len = len + TYPE_A_HEADER_SIZE;
		t->callback = cb;
		t->cb_data = cb_data;
	}

	if (tx->can_send)
		schedule_work(&sdev->ws);

	spin_unlock_irqrestore(&tx->lock, flags);

	if (no_spc)
		return -ENOSPC;

	return 0;
}

/* Handle the HCI, WIMAX_SDU_TX_FLOW. */
static int control_sdu_tx_flow(struct sdiowm_dev *sdev, u8 *hci_data, int len)
{
	struct tx_cxt *tx = &sdev->tx;
	u16 cmd_evt;
	unsigned long flags;

	spin_lock_irqsave(&tx->lock, flags);

	cmd_evt = (hci_data[0] << 8) | (hci_data[1]);
	if (cmd_evt != WIMAX_SDU_TX_FLOW)
		goto out;

	if (hci_data[4] == 0) {
		dev_dbg(&sdev->func->dev, "WIMAX ==> STOP SDU TX\n");
		tx->stop_sdu_tx = 1;
	} else if (hci_data[4] == 1) {
		dev_dbg(&sdev->func->dev, "WIMAX ==> START SDU TX\n");
		tx->stop_sdu_tx = 0;
		if (tx->can_send)
			schedule_work(&sdev->ws);
		/* If free buffer for sdu tx doesn't exist, then tx queue
		 * should not be woken. For this reason, don't pass the command,
		 * START_SDU_TX.
		 */
		if (list_empty(&tx->free_list))
			len = 0;
	}

out:
	spin_unlock_irqrestore(&tx->lock, flags);
	return len;
}

static void gdm_sdio_irq(struct sdio_func *func)
{
	struct phy_dev *phy_dev = sdio_get_drvdata(func);
	struct sdiowm_dev *sdev = phy_dev->priv_dev;
	struct tx_cxt *tx = &sdev->tx;
	struct rx_cxt *rx = &sdev->rx;
	struct sdio_rx *r;
	unsigned long flags;
	u8 val, hdr[TYPE_A_LOOKAHEAD_SIZE], *buf;
	u32 len, blocks, n;
	int ret, remain;

	/* Check interrupt */
	val = sdio_readb(func, 0x13, &ret);
	if (val & 0x01)
		sdio_writeb(func, 0x01, 0x13, &ret);	/* clear interrupt */
	else
		return;

	ret = sdio_memcpy_fromio(func, hdr, 0x0, TYPE_A_LOOKAHEAD_SIZE);
	if (ret) {
		dev_err(&func->dev,
			"Cannot read from function %d\n", func->num);
		goto done;
	}

	len = (hdr[2] << 16) | (hdr[1] << 8) | hdr[0];
	if (len > (RX_BUF_SIZE - TYPE_A_HEADER_SIZE)) {
		dev_err(&func->dev, "Too big Type-A size: %d\n", len);
		goto done;
	}

	if (hdr[3] == 1) {	/* Ack */
		u32 *ack_seq = (u32 *)&hdr[4];

		spin_lock_irqsave(&tx->lock, flags);
		tx->can_send = 1;

		if (!list_empty(&tx->sdu_list) || !list_empty(&tx->hci_list))
			schedule_work(&sdev->ws);
		spin_unlock_irqrestore(&tx->lock, flags);
		dev_dbg(&func->dev, "Ack... %0x\n", ntohl(*ack_seq));
		goto done;
	}

	memcpy(rx->rx_buf, hdr + TYPE_A_HEADER_SIZE,
	       TYPE_A_LOOKAHEAD_SIZE - TYPE_A_HEADER_SIZE);

	buf = rx->rx_buf + TYPE_A_LOOKAHEAD_SIZE - TYPE_A_HEADER_SIZE;
	remain = len - TYPE_A_LOOKAHEAD_SIZE + TYPE_A_HEADER_SIZE;
	if (remain <= 0)
		goto end_io;

	blocks = remain / func->cur_blksize;

	if (blocks) {
		n = blocks * func->cur_blksize;
		ret = sdio_memcpy_fromio(func, buf, 0x0, n);
		if (ret) {
			dev_err(&func->dev,
				"Cannot read from function %d\n", func->num);
			goto done;
		}
		buf += n;
		remain -= n;
	}

	if (remain) {
		ret = sdio_memcpy_fromio(func, buf, 0x0, remain);
		if (ret) {
			dev_err(&func->dev,
				"Cannot read from function %d\n", func->num);
			goto done;
		}
	}

end_io:
	dev_dbg(&func->dev, "sdio_receive: %*ph\n", len, rx->rx_buf);

	len = control_sdu_tx_flow(sdev, rx->rx_buf, len);

	spin_lock_irqsave(&rx->lock, flags);

	if (!list_empty(&rx->req_list)) {
		r = list_entry(rx->req_list.next, struct sdio_rx, list);
		spin_unlock_irqrestore(&rx->lock, flags);
		if (r->callback)
			r->callback(r->cb_data, rx->rx_buf, len);
		spin_lock_irqsave(&rx->lock, flags);
		list_del(&r->list);
		put_rx_struct(rx, r);
	}

	spin_unlock_irqrestore(&rx->lock, flags);

done:
	sdio_writeb(func, 0x00, 0x10, &ret);	/* PCRRT */
	if (!phy_dev->netdev)
		register_wimax_device(phy_dev, &func->dev);
}

static int gdm_sdio_receive(void *priv_dev,
			    void (*cb)(void *cb_data, void *data, int len),
			    void *cb_data)
{
	struct sdiowm_dev *sdev = priv_dev;
	struct rx_cxt *rx = &sdev->rx;
	struct sdio_rx *r;
	unsigned long flags;

	spin_lock_irqsave(&rx->lock, flags);
	r = get_rx_struct(rx);
	if (r == NULL) {
		spin_unlock_irqrestore(&rx->lock, flags);
		return -ENOMEM;
	}

	r->callback = cb;
	r->cb_data = cb_data;

	list_add_tail(&r->list, &rx->req_list);
	spin_unlock_irqrestore(&rx->lock, flags);

	return 0;
}

static int sdio_wimax_probe(struct sdio_func *func,
			    const struct sdio_device_id *id)
{
	int ret;
	struct phy_dev *phy_dev = NULL;
	struct sdiowm_dev *sdev = NULL;

	dev_info(&func->dev, "Found GDM SDIO VID = 0x%04x PID = 0x%04x...\n",
		 func->vendor, func->device);
	dev_info(&func->dev, "GCT WiMax driver version %s\n", DRIVER_VERSION);

	sdio_claim_host(func);
	sdio_enable_func(func);
	sdio_claim_irq(func, gdm_sdio_irq);

	ret = sdio_boot(func);
	if (ret)
		return ret;

	phy_dev = kzalloc(sizeof(*phy_dev), GFP_KERNEL);
	if (phy_dev == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
	if (sdev == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	phy_dev->priv_dev = (void *)sdev;
	phy_dev->send_func = gdm_sdio_send;
	phy_dev->rcv_func = gdm_sdio_receive;

	ret = init_sdio(sdev);
	if (ret < 0)
		goto out;

	sdev->func = func;

	sdio_writeb(func, 1, 0x14, &ret);	/* Enable interrupt */
	sdio_release_host(func);

	INIT_WORK(&sdev->ws, do_tx);

	sdio_set_drvdata(func, phy_dev);
out:
	if (ret) {
		kfree(phy_dev);
		kfree(sdev);
	}

	return ret;
}

static void sdio_wimax_remove(struct sdio_func *func)
{
	struct phy_dev *phy_dev = sdio_get_drvdata(func);
	struct sdiowm_dev *sdev = phy_dev->priv_dev;

	cancel_work_sync(&sdev->ws);
	if (phy_dev->netdev)
		unregister_wimax_device(phy_dev);
	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);
	release_sdio(sdev);

	kfree(sdev);
	kfree(phy_dev);
}

static const struct sdio_device_id sdio_wimax_ids[] = {
	{ SDIO_DEVICE(0x0296, 0x5347) },
	{0}
};

MODULE_DEVICE_TABLE(sdio, sdio_wimax_ids);

static struct sdio_driver sdio_wimax_driver = {
	.probe		= sdio_wimax_probe,
	.remove		= sdio_wimax_remove,
	.name		= "sdio_wimax",
	.id_table	= sdio_wimax_ids,
};

static int __init sdio_gdm_wimax_init(void)
{
	return sdio_register_driver(&sdio_wimax_driver);
}

static void __exit sdio_gdm_wimax_exit(void)
{
	sdio_unregister_driver(&sdio_wimax_driver);
}

module_init(sdio_gdm_wimax_init);
module_exit(sdio_gdm_wimax_exit);

MODULE_VERSION(DRIVER_VERSION);
MODULE_DESCRIPTION("GCT WiMax SDIO Device Driver");
MODULE_AUTHOR("Ethan Park");
MODULE_LICENSE("GPL");
