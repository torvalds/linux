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

/* Read Status Register */
#define READ_STATUS		0x000000
#define READ_STATUS_BUSY	0
#define READ_STATUS_IDLE	1

/* Read Index Register */
#define READ_INDEX		0x000004

/* Read Data Size Register */
#define READ_DATA_SIZE		0x000008

/* Write Status Register */
#define WRITE_STATUS		0x00000C
#define WRITE_STATUS_BUSY	0
#define WRITE_STATUS_IDLE	1

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
#define GCR_A_INIT		0
#define GCR_A_REMAP		1
#define GCR_A_RUN		2

/* General Communication Register B */
#define GCR_B			0x00002C
#define GCR_B_ACTIVE		0
#define GCR_B_DOZE		1

/* Wakeup Register */
/* #define WAKEUP			0x008104 */
/* #define WAKEUP_REQ		0x00 */
#define WAKEUP			0x008018
#define WAKEUP_REQ		0x5a

/* AHB Data Window  0x010000-0x01FFFF */
#define DATA_WINDOW		0x010000
#define WINDOW_SIZE		(64 * 1024)

#define KS7010_IRAM_ADDRESS	0x06000000

/*
 * struct define
 */
struct hw_info_t {
	struct ks_sdio_card *sdio_card;
	struct completion ks7010_sdio_wait;
	struct workqueue_struct *ks7010sdio_wq;
	struct delayed_work rw_wq;
	unsigned char *read_buf;
	struct tasklet_struct rx_bh_task;
};

struct ks_sdio_packet {
	struct ks_sdio_packet *next;
	u16 nb;
	u8 buffer[0] __aligned(4);
};

struct ks_sdio_card {
	struct sdio_func *func;
	struct ks_wlan_private *priv;
	spinlock_t lock;
};

/* Tx Device struct */
#define	TX_DEVICE_BUFF_SIZE	1024

struct tx_device_buffer {
	unsigned char *sendp;	/* pointer of send req data */
	unsigned int size;
	void (*complete_handler) (void *arg1, void *arg2);
	void *arg1;
	void *arg2;
};

struct tx_device {
	struct tx_device_buffer tx_dev_buff[TX_DEVICE_BUFF_SIZE];
	unsigned int qhead;	/* tx buffer queue first pointer */
	unsigned int qtail;	/* tx buffer queue last pointer */
	spinlock_t tx_dev_lock;
};

/* Rx Device struct */
#define	RX_DATA_SIZE	(2 + 2 + 2347 + 1)
#define	RX_DEVICE_BUFF_SIZE	32

struct rx_device_buffer {
	unsigned char data[RX_DATA_SIZE];
	unsigned int size;
};

struct rx_device {
	struct rx_device_buffer rx_dev_buff[RX_DEVICE_BUFF_SIZE];
	unsigned int qhead;	/* rx buffer queue first pointer */
	unsigned int qtail;	/* rx buffer queue last pointer */
	spinlock_t rx_dev_lock;
};

#define	ROM_FILE "ks7010sd.rom"

#endif /* _KS7010_SDIO_H */
