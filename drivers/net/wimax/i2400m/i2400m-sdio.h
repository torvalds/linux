/*
 * Intel Wireless WiMAX Connection 2400m
 * SDIO-specific i2400m driver definitions
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <linux-wimax@intel.com>
 * Brian Bian <brian.bian@intel.com>
 * Dirk Brandewie <dirk.j.brandewie@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 * Yanir Lubetkin <yanirx.lubetkin@intel.com>
 *  - Initial implementation
 *
 *
 * This driver implements the bus-specific part of the i2400m for
 * SDIO. Check i2400m.h for a generic driver description.
 *
 * ARCHITECTURE
 *
 * This driver sits under the bus-generic i2400m driver, providing the
 * connection to the device.
 *
 * When probed, all the function pointers are setup and then the
 * bus-generic code called. The generic driver will then use the
 * provided pointers for uploading firmware (i2400ms_bus_bm*() in
 * sdio-fw.c) and then setting up the device (i2400ms_dev_*() in
 * sdio.c).
 *
 * Once firmware is uploaded, TX functions (sdio-tx.c) are called when
 * data is ready for transmission in the TX fifo; then the SDIO IRQ is
 * fired and data is available (sdio-rx.c), it is sent to the generic
 * driver for processing with i2400m_rx.
 */

#ifndef __I2400M_SDIO_H__
#define __I2400M_SDIO_H__

#include "i2400m.h"

/* Host-Device interface for SDIO */
enum {
	I2400M_SDIO_BOOT_RETRIES = 3,
	I2400MS_BLK_SIZE = 256,
	I2400MS_PL_SIZE_MAX = 0x3E00,

	I2400MS_DATA_ADDR = 0x0,
	I2400MS_INTR_STATUS_ADDR = 0x13,
	I2400MS_INTR_CLEAR_ADDR = 0x13,
	I2400MS_INTR_ENABLE_ADDR = 0x14,
	I2400MS_INTR_GET_SIZE_ADDR = 0x2C,
	/* The number of ticks to wait for the device to signal that
	 * it is ready */
	I2400MS_INIT_SLEEP_INTERVAL = 100,
	/* How long to wait for the device to settle after reset */
	I2400MS_SETTLE_TIME = 40,
	/* The number of msec to wait for IOR after sending IOE */
	IWMC3200_IOR_TIMEOUT = 10,
};


/**
 * struct i2400ms - descriptor for a SDIO connected i2400m
 *
 * @i2400m: bus-generic i2400m implementation; has to be first (see
 *     it's documentation in i2400m.h).
 *
 * @func: pointer to our SDIO function
 *
 * @tx_worker: workqueue struct used to TX data when the bus-generic
 *     code signals packets are pending for transmission to the device.
 *
 * @tx_workqueue: workqeueue used for data TX; we don't use the
 *     system's workqueue as that might cause deadlocks with code in
 *     the bus-generic driver. The read/write operation to the queue
 *     is protected with spinlock (tx_lock in struct i2400m) to avoid
 *     the queue being destroyed in the middle of a the queue read/write
 *     operation.
 *
 * @debugfs_dentry: dentry for the SDIO specific debugfs files
 *
 *     Note this value is set to NULL upon destruction; this is
 *     because some routinges use it to determine if we are inside the
 *     probe() path or some other path. When debugfs is disabled,
 *     creation sets the dentry to '(void*) -ENODEV', which is valid
 *     for the test.
 */
struct i2400ms {
	struct i2400m i2400m;		/* FIRST! See doc */
	struct sdio_func *func;

	struct work_struct tx_worker;
	struct workqueue_struct *tx_workqueue;
	char tx_wq_name[32];

	struct dentry *debugfs_dentry;

	wait_queue_head_t bm_wfa_wq;
	int bm_wait_result;
	size_t bm_ack_size;

	/* Device is any of the iwmc3200 SKUs */
	unsigned iwmc3200:1;
};


static inline
void i2400ms_init(struct i2400ms *i2400ms)
{
	i2400m_init(&i2400ms->i2400m);
}


extern int i2400ms_rx_setup(struct i2400ms *);
extern void i2400ms_rx_release(struct i2400ms *);
extern ssize_t __i2400ms_rx_get_size(struct i2400ms *);

extern int i2400ms_tx_setup(struct i2400ms *);
extern void i2400ms_tx_release(struct i2400ms *);
extern void i2400ms_bus_tx_kick(struct i2400m *);

extern ssize_t i2400ms_bus_bm_cmd_send(struct i2400m *,
				       const struct i2400m_bootrom_header *,
				       size_t, int);
extern ssize_t i2400ms_bus_bm_wait_for_ack(struct i2400m *,
					   struct i2400m_bootrom_header *,
					   size_t);
extern void i2400ms_bus_bm_release(struct i2400m *);
extern int i2400ms_bus_bm_setup(struct i2400m *);

#endif /* #ifndef __I2400M_SDIO_H__ */
