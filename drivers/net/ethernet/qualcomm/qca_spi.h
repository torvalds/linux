/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 *   Copyright (c) 2011, 2012, Qualcomm Atheros Communications Inc.
 *   Copyright (c) 2014, I2SE GmbH
 */

/*   Qualcomm Atheros SPI register definition.
 *
 *   This module is designed to define the Qualcomm Atheros SPI register
 *   placeholders;
 */

#ifndef _QCA_SPI_H
#define _QCA_SPI_H

#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "qca_7k_common.h"

#define QCASPI_DRV_VERSION "0.2.7-i"
#define QCASPI_DRV_NAME    "qcaspi"

#define QCASPI_GOOD_SIGNATURE 0xAA55

#define QCASPI_TX_RING_MAX_LEN 10
#define QCASPI_TX_RING_MIN_LEN 2
#define QCASPI_RX_MAX_FRAMES 4

/* sync related constants */
#define QCASPI_SYNC_UNKNOWN 0
#define QCASPI_SYNC_RESET   1
#define QCASPI_SYNC_READY   2

#define QCASPI_RESET_TIMEOUT 10

/* sync events */
#define QCASPI_EVENT_UPDATE 0
#define QCASPI_EVENT_CPUON  1

struct tx_ring {
	struct sk_buff *skb[QCASPI_TX_RING_MAX_LEN];
	u16 head;
	u16 tail;
	u16 size;
	u16 count;
};

struct qcaspi_stats {
	u64 trig_reset;
	u64 device_reset;
	u64 reset_timeout;
	u64 read_err;
	u64 write_err;
	u64 read_buf_err;
	u64 write_buf_err;
	u64 out_of_mem;
	u64 write_buf_miss;
	u64 ring_full;
	u64 spi_err;
	u64 write_verify_failed;
	u64 buf_avail_err;
	u64 bad_signature;
};

struct qcaspi {
	struct net_device *net_dev;
	struct spi_device *spi_dev;
	struct task_struct *spi_thread;

	struct tx_ring txr;
	struct qcaspi_stats stats;

	u8 *rx_buffer;
	u32 buffer_size;
	u8 sync;

	struct qcafrm_handle frm_handle;
	struct sk_buff *rx_skb;

	unsigned long flags;
	u16 reset_count;

#ifdef CONFIG_DEBUG_FS
	struct dentry *device_root;
#endif

	/* user configurable options */
	u8 legacy_mode;
	u16 burst_len;
};

#endif /* _QCA_SPI_H */
