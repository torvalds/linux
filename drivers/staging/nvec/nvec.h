/*
 * NVEC: NVIDIA compliant embedded controller interface
 *
 * Copyright (C) 2011 The AC100 Kernel Team <ac100@lists.launchpad.net>
 *
 * Authors:  Pierre-Hugues Husson <phhusson@free.fr>
 *           Ilya Petrov <ilya.muromec@gmail.com>
 *           Marc Dietrich <marvin24@gmx.de>
 *           Julian Andres Klode <jak@jak-linux.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#ifndef __LINUX_MFD_NVEC
#define __LINUX_MFD_NVEC

#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

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

/**
 * enum nvec_event_size - The size of an event message
 * @NVEC_2BYTES: The message has one command byte and one data byte
 * @NVEC_3BYTES: The message has one command byte and two data bytes
 * @NVEC_VAR_SIZE: The message has one command byte, one count byte, and has
 *                 up to as many bytes as the number in the count byte. The
 *                 maximum is 32
 *
 * Events can be fixed or variable sized. This is useless on other message
 * types, which are always variable sized.
 */
enum nvec_event_size {
	NVEC_2BYTES,
	NVEC_3BYTES,
	NVEC_VAR_SIZE,
};

/**
 * enum nvec_msg_type - The type of a message
 * @NVEC_SYS: A system request/response
 * @NVEC_BAT: A battery request/response
 * @NVEC_KBD: A keyboard request/response
 * @NVEC_PS2: A mouse request/response
 * @NVEC_CNTL: A EC control request/response
 * @NVEC_KB_EVT: An event from the keyboard
 * @NVEC_PS2_EVT: An event from the mouse
 *
 * Events can be fixed or variable sized. This is useless on other message
 * types, which are always variable sized.
 */
enum nvec_msg_type {
	NVEC_SYS = 1,
	NVEC_BAT,
	NVEC_GPIO,
	NVEC_SLEEP,
	NVEC_KBD,
	NVEC_PS2,
	NVEC_CNTL,
	NVEC_OEM0 = 0x0d,
	NVEC_KB_EVT = 0x80,
	NVEC_PS2_EVT,
};

/**
 * struct nvec_msg - A buffer for a single message
 * @node: Messages are part of various lists in a &struct nvec_chip
 * @data: The data of the message
 * @size: For TX messages, the number of bytes used in @data
 * @pos:  For RX messages, the current position to write to. For TX messages,
 *        the position to read from.
 * @used: Used for the message pool to mark a message as free/allocated.
 *
 * This structure is used to hold outgoing and incoming messages. Outgoing
 * messages have a different format than incoming messages, and that is not
 * documented yet.
 */
struct nvec_msg {
	struct list_head node;
	unsigned char data[NVEC_MSG_SIZE];
	unsigned short size;
	unsigned short pos;
	atomic_t used;
};

/**
 * struct nvec_subdev - A subdevice of nvec, such as nvec_kbd
 * @name: The name of the sub device
 * @platform_data: Platform data
 * @id: Identifier of the sub device
 */
struct nvec_subdev {
	const char *name;
	void *platform_data;
	int id;
};

/**
 * struct nvec_platform_data - platform data for a tegra slave controller
 * @i2c_addr: number of i2c slave adapter the ec is connected to
 * @gpio: gpio number for the ec request line
 *
 * Platform data, to be used in board definitions. For an example, take a
 * look at the paz00 board in arch/arm/mach-tegra/board-paz00.c
 */
struct nvec_platform_data {
	int i2c_addr;
	int gpio;
};

/**
 * struct nvec_chip - A single connection to an NVIDIA Embedded controller
 * @dev: The device
 * @gpio: The same as for &struct nvec_platform_data
 * @irq: The IRQ of the I2C device
 * @i2c_addr: The address of the I2C slave
 * @base: The base of the memory mapped region of the I2C device
 * @clk: The clock of the I2C device
 * @notifier_list: Notifiers to be called on received messages, see
 *                 nvec_register_notifier()
 * @rx_data: Received messages that have to be processed
 * @tx_data: Messages waiting to be sent to the controller
 * @nvec_status_notifier: Internal notifier (see nvec_status_notifier())
 * @rx_work: A work structure for the RX worker nvec_dispatch()
 * @tx_work: A work structure for the TX worker nvec_request_master()
 * @wq: The work queue in which @rx_work and @tx_work are executed
 * @rx: The message currently being retrieved or %NULL
 * @msg_pool: A pool of messages for allocation
 * @tx: The message currently being transferred
 * @tx_scratch: Used for building pseudo messages
 * @ec_transfer: A completion that will be completed once a message has been
 *               received (see nvec_rx_completed())
 * @tx_lock: Spinlock for modifications on @tx_data
 * @rx_lock: Spinlock for modifications on @rx_data
 * @sync_write_mutex: A mutex for nvec_write_sync()
 * @sync_write: A completion to signal that a synchronous message is complete
 * @sync_write_pending: The first two bytes of the request (type and subtype)
 * @last_sync_msg: The last synchronous message.
 * @state: State of our finite state machine used in nvec_interrupt()
 */
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

extern int nvec_write_async(struct nvec_chip *nvec, const unsigned char *data,
			     short size);

extern struct nvec_msg *nvec_write_sync(struct nvec_chip *nvec,
					const unsigned char *data, short size);

extern int nvec_register_notifier(struct nvec_chip *nvec,
				  struct notifier_block *nb,
				  unsigned int events);

extern int nvec_unregister_notifier(struct nvec_chip *dev,
				    struct notifier_block *nb);

extern void nvec_msg_free(struct nvec_chip *nvec, struct nvec_msg *msg);

#endif
