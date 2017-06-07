/*
 *   Driver for KeyStream, KS7010 based SDIO cards.
 *
 *   Copyright (C) 2006-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation.
 */
#ifndef _KS7010_SDIO_H
#define _KS7010_SDIO_H

#ifdef	DEVICE_ALIGNMENT
#undef	DEVICE_ALIGNMENT
#endif
#define DEVICE_ALIGNMENT 32

/*  SDIO KeyStream vendor and device */
#define SDIO_VENDOR_ID_KS_CODE_A	0x005b
#define SDIO_VENDOR_ID_KS_CODE_B	0x0023
/* Older sources suggest earlier versions were named 7910 or 79xx */
#define SDIO_DEVICE_ID_KS_7010		0x7910

/* Read/Write Status Register */
#define READ_STATUS		0x000000
#define WRITE_STATUS		0x00000C
enum reg_status_type {
	REG_STATUS_BUSY,
	REG_STATUS_IDLE
};

/* Read Index Register */
#define READ_INDEX		0x000004

/* Read Data Size Register */
#define READ_DATA_SIZE		0x000008

/* Write Index Register */
#define WRITE_INDEX		0x000010

/* Write Status/Read Data Size Register
 * for network packet (less than 2048 bytes data)
 */
#define WSTATUS_RSIZE		0x000014
#define WSTATUS_MASK		0x80	/* Write Status Register value */
#define RSIZE_MASK		0x7F	/* Read Data Size Register value [10:4] */

/* ARM to SD interrupt Enable */
#define INT_ENABLE		0x000020
/* ARM to SD interrupt Pending */
#define INT_PENDING		0x000024

#define INT_GCR_B              BIT(7)
#define INT_GCR_A              BIT(6)
#define INT_WRITE_STATUS       BIT(5)
#define INT_WRITE_INDEX        BIT(4)
#define INT_WRITE_SIZE         BIT(3)
#define INT_READ_STATUS        BIT(2)
#define INT_READ_INDEX         BIT(1)
#define INT_READ_SIZE          BIT(0)

/* General Communication Register A */
#define GCR_A			0x000028
enum gen_com_reg_a {
	GCR_A_INIT,
	GCR_A_REMAP,
	GCR_A_RUN
};

/* General Communication Register B */
#define GCR_B			0x00002C
enum gen_com_reg_b {
	GCR_B_ACTIVE,
	GCR_B_DOZE
};

/* Wakeup Register */
#define WAKEUP			0x008018
#define WAKEUP_REQ		0x5a

/* AHB Data Window  0x010000-0x01FFFF */
#define DATA_WINDOW		0x010000
#define WINDOW_SIZE		(64 * 1024)

#define KS7010_IRAM_ADDRESS	0x06000000

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

#define	ROM_FILE "ks7010sd.rom"

#endif /* _KS7010_SDIO_H */
