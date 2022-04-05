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

#define TX_RING_MAX_LEN 10
#define TX_RING_MIN_LEN 2

/* sync related constants */
#define QCASPI_SYNC_UNKNOWN 0
#define QCASPI_SYNC_RESET   1
#define QCASPI_SYNC_READY   2

#define QCASPI_RESET_TIMEOUT 10

/* sync events */
#define QCASPI_EVENT_UPDATE 0
#define QCASPI_EVENT_CPUON  1

struct tx_ring {
	struct sk_buff *skb[TX_RING_MAX_LEN];
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

	unsigned int intr_req;
	unsigned int intr_svc;
	u16 reset_count;

#ifdef CONFIG_DEBUG_FS
	struct dentry *device_root;
#endif

	/* user configurable options */
	u32 clkspeed;
	u8 legacy_mode;
	u16 burst_len;
};

#endif /* _QCA_SPI_H */
