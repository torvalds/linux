/*
 * NVEC: NVIDIA compliant embedded controller interface
 *
 * Copyright (C) 2011 The AC100 Kernel Team <ac100@lists.launchpad.net>
 *
 * Authors:  Pierre-Hugues Husson <phhusson@free.fr>
 *           Ilya Petrov <ilya.muromec@gmail.com>
 *           Marc Dietrich <marvin24@gmx.de>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#ifndef __LINUX_MFD_NVEC
#define __LINUX_MFD_NVEC

#include <linux/atomic.h>
#include <linux/notifier.h>
#include <linux/semaphore.h>

/* NVEC_POOL_SIZE - Size of the pool in &struct nvec_msg */
#define NVEC_POOL_SIZE	64

/*
 * NVEC_MSG_SIZE - Maximum size of the data field of &struct nvec_msg.
 *
 * A message must store up to a SMBus block operation which consists of
 * one command byte, one count byte, and up to 32 payload bytes = 34
 * byte.
 */
#define NVEC_MSG_SIZE	34

enum {
	NVEC_2BYTES,
	NVEC_3BYTES,
	NVEC_VAR_SIZE,
};

enum {
	NVEC_SYS = 1,
	NVEC_BAT,
	NVEC_KBD = 5,
	NVEC_PS2,
	NVEC_CNTL,
	NVEC_KB_EVT = 0x80,
	NVEC_PS2_EVT,
};

struct nvec_msg {
	struct list_head node;
	unsigned char data[NVEC_MSG_SIZE];
	unsigned short size;
	unsigned short pos;
	atomic_t used;
};

struct nvec_subdev {
	const char *name;
	void *platform_data;
	int id;
};

struct nvec_platform_data {
	int i2c_addr;
	int gpio;
};

struct nvec_chip {
	struct device *dev;
	int gpio;
	int irq;
	int i2c_addr;
	void __iomem *base;
	struct clk *i2c_clk;
	struct atomic_notifier_head notifier_list;
	struct list_head rx_data, tx_data;
	struct notifier_block nvec_status_notifier;
	struct work_struct rx_work, tx_work;
	struct workqueue_struct *wq;
	struct nvec_msg msg_pool[NVEC_POOL_SIZE];
	struct nvec_msg *rx;

	struct nvec_msg *tx;
	struct nvec_msg tx_scratch;
	struct completion ec_transfer;

	spinlock_t tx_lock, rx_lock;

	/* sync write stuff */
	struct mutex sync_write_mutex;
	struct completion sync_write;
	u16 sync_write_pending;
	struct nvec_msg *last_sync_msg;

	int state;
};

extern void nvec_write_async(struct nvec_chip *nvec, const unsigned char *data,
			     short size);

extern struct nvec_msg *nvec_write_sync(struct nvec_chip *nvec,
					const unsigned char *data, short size);

extern int nvec_register_notifier(struct nvec_chip *nvec,
				  struct notifier_block *nb,
				  unsigned int events);

extern int nvec_unregister_notifier(struct device *dev,
				    struct notifier_block *nb,
				    unsigned int events);

#define I2C_CNFG			0x00
#define I2C_CNFG_PACKET_MODE_EN		(1<<10)
#define I2C_CNFG_NEW_MASTER_SFM		(1<<11)
#define I2C_CNFG_DEBOUNCE_CNT_SHIFT	12

#define I2C_SL_CNFG		0x20
#define I2C_SL_NEWL		(1<<2)
#define I2C_SL_NACK		(1<<1)
#define I2C_SL_RESP		(1<<0)
#define I2C_SL_IRQ		(1<<3)
#define END_TRANS		(1<<4)
#define RCVD			(1<<2)
#define RNW			(1<<1)

#define I2C_SL_RCVD		0x24
#define I2C_SL_STATUS		0x28
#define I2C_SL_ADDR1		0x2c
#define I2C_SL_ADDR2		0x30
#define I2C_SL_DELAY_COUNT	0x3c

#endif
