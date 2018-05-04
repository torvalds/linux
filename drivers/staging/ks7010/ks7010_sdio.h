/* SPDX-License-Identifier: GPL-2.0 */
/*
 *   Driver for KeyStream, KS7010 based SDIO cards.
 *
 *   Copyright (C) 2006-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 */
#ifndef _KS7010_SDIO_H
#define _KS7010_SDIO_H

/**
 * struct ks_sdio_card - SDIO device data.
 *
 * Structure is used as the &struct sdio_func private data.
 *
 * @func: Pointer to the SDIO function device.
 * @priv: Pointer to the &struct net_device private data.
 */
struct ks_sdio_card {
	struct sdio_func *func;
	struct ks_wlan_private *priv;
};

/* Tx Device struct */
#define	TX_DEVICE_BUFF_SIZE	1024

/**
 * struct tx_device_buffer - Queue item for the tx queue.
 * @sendp: Pointer to the send request data.
 * @size: Size of @sendp data.
 * @complete_handler: Function called once data write to device is complete.
 * @arg1: First argument to @complete_handler.
 * @arg2: Second argument to @complete_handler.
 */
struct tx_device_buffer {
	unsigned char *sendp;
	unsigned int size;
	void (*complete_handler)(struct ks_wlan_private *priv,
				 struct sk_buff *skb);
	struct sk_buff *skb;
};

/**
 * struct tx_device - Tx buffer queue.
 * @tx_device_buffer: Queue buffer.
 * @qhead: Head of tx queue.
 * @qtail: Tail of tx queue.
 * @tx_dev_lock: Queue lock.
 */
struct tx_device {
	struct tx_device_buffer tx_dev_buff[TX_DEVICE_BUFF_SIZE];
	unsigned int qhead;
	unsigned int qtail;
	spinlock_t tx_dev_lock;	/* protect access to the queue */
};

/* Rx Device struct */
#define	RX_DATA_SIZE	(2 + 2 + 2347 + 1)
#define	RX_DEVICE_BUFF_SIZE	32

/**
 * struct rx_device_buffer - Queue item for the rx queue.
 * @data: rx data.
 * @size: Size of @data.
 */
struct rx_device_buffer {
	unsigned char data[RX_DATA_SIZE];
	unsigned int size;
};

/**
 * struct rx_device - Rx buffer queue.
 * @rx_device_buffer: Queue buffer.
 * @qhead: Head of rx queue.
 * @qtail: Tail of rx queue.
 * @rx_dev_lock: Queue lock.
 */
struct rx_device {
	struct rx_device_buffer rx_dev_buff[RX_DEVICE_BUFF_SIZE];
	unsigned int qhead;
	unsigned int qtail;
	spinlock_t rx_dev_lock;	/* protect access to the queue */
};

#endif /* _KS7010_SDIO_H */
