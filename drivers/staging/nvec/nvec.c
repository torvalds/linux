/*
 * NVEC: NVIDIA compliant embedded controller interface
 *
 * Copyright (C) 2011 The AC100 Kernel Team <ac100@lists.lauchpad.net>
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

/* #define DEBUG */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/list.h>
#include <linux/mfd/core.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "nvec.h"

#define I2C_CNFG			0x00
#define I2C_CNFG_PACKET_MODE_EN		(1<<10)
#define I2C_CNFG_NEW_MASTER_SFM		(1<<11)
#define I2C_CNFG_DEBOUNCE_CNT_SHIFT	12

#define I2C_SL_CNFG		0x20
#define I2C_SL_NEWSL		(1<<2)
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

/**
 * enum nvec_msg_category - Message categories for nvec_msg_alloc()
 * @NVEC_MSG_RX: The message is an incoming message (from EC)
 * @NVEC_MSG_TX: The message is an outgoing message (to EC)
 */
enum nvec_msg_category  {
	NVEC_MSG_RX,
	NVEC_MSG_TX,
};

enum nvec_sleep_subcmds {
	GLOBAL_EVENTS,
	AP_PWR_DOWN,
	AP_SUSPEND,
};

#define CNF_EVENT_REPORTING 0x01
#define GET_FIRMWARE_VERSION 0x15
#define LID_SWITCH BIT(1)
#define PWR_BUTTON BIT(15)

static struct nvec_chip *nvec_power_handle;

static const struct mfd_cell nvec_devices[] = {
	{
		.name = "nvec-kbd",
		.id = 1,
	},
	{
		.name = "nvec-mouse",
		.id = 1,
	},
	{
		.name = "nvec-power",
		.id = 1,
	},
	{
		.name = "nvec-power",
		.id = 2,
	},
	{
		.name = "nvec-paz00",
		.id = 1,
	},
};

/**
 * nvec_register_notifier - Register a notifier with nvec
 * @nvec: A &struct nvec_chip
 * @nb: The notifier block to register
 *
 * Registers a notifier with @nvec. The notifier will be added to an atomic
 * notifier chain that is called for all received messages except those that
 * correspond to a request initiated by nvec_write_sync().
 */
int nvec_register_notifier(struct nvec_chip *nvec, struct notifier_block *nb,
			   unsigned int events)
{
	return atomic_notifier_chain_register(&nvec->notifier_list, nb);
}
EXPORT_SYMBOL_GPL(nvec_register_notifier);

/**
 * nvec_unregister_notifier - Unregister a notifier with nvec
 * @nvec: A &struct nvec_chip
 * @nb: The notifier block to unregister
 *
 * Unregisters a notifier with @nvec. The notifier will be removed from the
 * atomic notifier chain.
 */
int nvec_unregister_notifier(struct nvec_chip *nvec, struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&nvec->notifier_list, nb);
}
EXPORT_SYMBOL_GPL(nvec_unregister_notifier);

/**
 * nvec_status_notifier - The final notifier
 *
 * Prints a message about control events not handled in the notifier
 * chain.
 */
static int nvec_status_notifier(struct notifier_block *nb,
				unsigned long event_type, void *data)
{
	struct nvec_chip *nvec = container_of(nb, struct nvec_chip,
						nvec_status_notifier);
	unsigned char *msg = (unsigned char *)data;

	if (event_type != NVEC_CNTL)
		return NOTIFY_DONE;

	dev_warn(nvec->dev, "unhandled msg type %ld\n", event_type);
	print_hex_dump(KERN_WARNING, "payload: ", DUMP_PREFIX_NONE, 16, 1,
		msg, msg[1] + 2, true);

	return NOTIFY_OK;
}

/**
 * nvec_msg_alloc:
 * @nvec: A &struct nvec_chip
 * @category: Pool category, see &enum nvec_msg_category
 *
 * Allocate a single &struct nvec_msg object from the message pool of
 * @nvec. The result shall be passed to nvec_msg_free() if no longer
 * used.
 *
 * Outgoing messages are placed in the upper 75% of the pool, keeping the
 * lower 25% available for RX buffers only. The reason is to prevent a
 * situation where all buffers are full and a message is thus endlessly
 * retried because the response could never be processed.
 */
static struct nvec_msg *nvec_msg_alloc(struct nvec_chip *nvec,
				       enum nvec_msg_category category)
{
	int i = (category == NVEC_MSG_TX) ? (NVEC_POOL_SIZE / 4) : 0;

	for (; i < NVEC_POOL_SIZE; i++) {
		if (atomic_xchg(&nvec->msg_pool[i].used, 1) == 0) {
			dev_vdbg(nvec->dev, "INFO: Allocate %i\n", i);
			return &nvec->msg_pool[i];
		}
	}

	dev_err(nvec->dev, "could not allocate %s buffer\n",
		(category == NVEC_MSG_TX) ? "TX" : "RX");

	return NULL;
}

/**
 * nvec_msg_free:
 * @nvec: A &struct nvec_chip
 * @msg:  A message (must be allocated by nvec_msg_alloc() and belong to @nvec)
 *
 * Free the given message
 */
void nvec_msg_free(struct nvec_chip *nvec, struct nvec_msg *msg)
{
	if (msg != &nvec->tx_scratch)
		dev_vdbg(nvec->dev, "INFO: Free %ti\n", msg - nvec->msg_pool);
	atomic_set(&msg->used, 0);
}
EXPORT_SYMBOL_GPL(nvec_msg_free);

/**
 * nvec_msg_is_event - Return %true if @msg is an event
 * @msg: A message
 */
static bool nvec_msg_is_event(struct nvec_msg *msg)
{
	return msg->data[0] >> 7;
}

/**
 * nvec_msg_size - Get the size of a message
 * @msg: The message to get the size for
 *
 * This only works for received messages, not for outgoing messages.
 */
static size_t nvec_msg_size(struct nvec_msg *msg)
{
	bool is_event = nvec_msg_is_event(msg);
	int event_length = (msg->data[0] & 0x60) >> 5;

	/* for variable size, payload size in byte 1 + count (1) + cmd (1) */
	if (!is_event || event_length == NVEC_VAR_SIZE)
		return (msg->pos || msg->size) ? (msg->data[1] + 2) : 0;
	else if (event_length == NVEC_2BYTES)
		return 2;
	else if (event_length == NVEC_3BYTES)
		return 3;
	return 0;
}

/**
 * nvec_gpio_set_value - Set the GPIO value
 * @nvec: A &struct nvec_chip
 * @value: The value to write (0 or 1)
 *
 * Like gpio_set_value(), but generating debugging information
 */
static void nvec_gpio_set_value(struct nvec_chip *nvec, int value)
{
	dev_dbg(nvec->dev, "GPIO changed from %u to %u\n",
		gpio_get_value(nvec->gpio), value);
	gpio_set_value(nvec->gpio, value);
}

/**
 * nvec_write_async - Asynchronously write a message to NVEC
 * @nvec: An nvec_chip instance
 * @data: The message data, starting with the request type
 * @size: The size of @data
 *
 * Queue a single message to be transferred to the embedded controller
 * and return immediately.
 *
 * Returns: 0 on success, a negative error code on failure. If a failure
 * occured, the nvec driver may print an error.
 */
int nvec_write_async(struct nvec_chip *nvec, const unsigned char *data,
			short size)
{
	struct nvec_msg *msg;
	unsigned long flags;

	msg = nvec_msg_alloc(nvec, NVEC_MSG_TX);

	if (msg == NULL)
		return -ENOMEM;

	msg->data[0] = size;
	memcpy(msg->data + 1, data, size);
	msg->size = size + 1;

	spin_lock_irqsave(&nvec->tx_lock, flags);
	list_add_tail(&msg->node, &nvec->tx_data);
	spin_unlock_irqrestore(&nvec->tx_lock, flags);

	schedule_work(&nvec->tx_work);

	return 0;
}
EXPORT_SYMBOL(nvec_write_async);

/**
 * nvec_write_sync - Write a message to nvec and read the response
 * @nvec: An &struct nvec_chip
 * @data: The data to write
 * @size: The size of @data
 *
 * This is similar to nvec_write_async(), but waits for the
 * request to be answered before returning. This function
 * uses a mutex and can thus not be called from e.g.
 * interrupt handlers.
 *
 * Returns: A pointer to the response message on success,
 * %NULL on failure. Free with nvec_msg_free() once no longer
 * used.
 */
struct nvec_msg *nvec_write_sync(struct nvec_chip *nvec,
		const unsigned char *data, short size)
{
	struct nvec_msg *msg;

	mutex_lock(&nvec->sync_write_mutex);

	nvec->sync_write_pending = (data[1] << 8) + data[0];

	if (nvec_write_async(nvec, data, size) < 0) {
		mutex_unlock(&nvec->sync_write_mutex);
		return NULL;
	}

	dev_dbg(nvec->dev, "nvec_sync_write: 0x%04x\n",
					nvec->sync_write_pending);
	if (!(wait_for_completion_timeout(&nvec->sync_write,
				msecs_to_jiffies(2000)))) {
		dev_warn(nvec->dev, "timeout waiting for sync write to complete\n");
		mutex_unlock(&nvec->sync_write_mutex);
		return NULL;
	}

	dev_dbg(nvec->dev, "nvec_sync_write: pong!\n");

	msg = nvec->last_sync_msg;

	mutex_unlock(&nvec->sync_write_mutex);

	return msg;
}
EXPORT_SYMBOL(nvec_write_sync);

/**
 * nvec_toggle_global_events - enables or disables global event reporting
 * @nvec: nvec handle
 * @state: true for enable, false for disable
 *
 * This switches on/off global event reports by the embedded controller.
 */
static void nvec_toggle_global_events(struct nvec_chip *nvec, bool state)
{
	unsigned char global_events[] = { NVEC_SLEEP, GLOBAL_EVENTS, state };

	nvec_write_async(nvec, global_events, 3);
}

/**
 * nvec_event_mask - fill the command string with event bitfield
 * ev: points to event command string
 * mask: bit to insert into the event mask
 *
 * Configure event command expects a 32 bit bitfield which describes
 * which events to enable. The bitfield has the following structure
 * (from highest byte to lowest):
 *	system state bits 7-0
 *	system state bits 15-8
 *	oem system state bits 7-0
 *	oem system state bits 15-8
 */
static void nvec_event_mask(char *ev, u32 mask)
{
	ev[3] = mask >> 16 & 0xff;
	ev[4] = mask >> 24 & 0xff;
	ev[5] = mask >> 0  & 0xff;
	ev[6] = mask >> 8  & 0xff;
}

/**
 * nvec_request_master - Process outgoing messages
 * @work: A &struct work_struct (the tx_worker member of &struct nvec_chip)
 *
 * Processes all outgoing requests by sending the request and awaiting the
 * response, then continuing with the next request. Once a request has a
 * matching response, it will be freed and removed from the list.
 */
static void nvec_request_master(struct work_struct *work)
{
	struct nvec_chip *nvec = container_of(work, struct nvec_chip, tx_work);
	unsigned long flags;
	long err;
	struct nvec_msg *msg;

	spin_lock_irqsave(&nvec->tx_lock, flags);
	while (!list_empty(&nvec->tx_data)) {
		msg = list_first_entry(&nvec->tx_data, struct nvec_msg, node);
		spin_unlock_irqrestore(&nvec->tx_lock, flags);
		nvec_gpio_set_value(nvec, 0);
		err = wait_for_completion_interruptible_timeout(
				&nvec->ec_transfer, msecs_to_jiffies(5000));

		if (err == 0) {
			dev_warn(nvec->dev, "timeout waiting for ec transfer\n");
			nvec_gpio_set_value(nvec, 1);
			msg->pos = 0;
		}

		spin_lock_irqsave(&nvec->tx_lock, flags);

		if (err > 0) {
			list_del_init(&msg->node);
			nvec_msg_free(nvec, msg);
		}
	}
	spin_unlock_irqrestore(&nvec->tx_lock, flags);
}

/**
 * parse_msg - Print some information and call the notifiers on an RX message
 * @nvec: A &struct nvec_chip
 * @msg: A message received by @nvec
 *
 * Paarse some pieces of the message and then call the chain of notifiers
 * registered via nvec_register_notifier.
 */
static int parse_msg(struct nvec_chip *nvec, struct nvec_msg *msg)
{
	if ((msg->data[0] & 1 << 7) == 0 && msg->data[3]) {
		dev_err(nvec->dev, "ec responded %*ph\n", 4, msg->data);
		return -EINVAL;
	}

	if ((msg->data[0] >> 7) == 1 && (msg->data[0] & 0x0f) == 5)
		print_hex_dump(KERN_WARNING, "ec system event ",
				DUMP_PREFIX_NONE, 16, 1, msg->data,
				msg->data[1] + 2, true);

	atomic_notifier_call_chain(&nvec->notifier_list, msg->data[0] & 0x8f,
				   msg->data);

	return 0;
}

/**
 * nvec_dispatch - Process messages received from the EC
 * @work: A &struct work_struct (the tx_worker member of &struct nvec_chip)
 *
 * Process messages previously received from the EC and put into the RX
 * queue of the &struct nvec_chip instance associated with @work.
 */
static void nvec_dispatch(struct work_struct *work)
{
	struct nvec_chip *nvec = container_of(work, struct nvec_chip, rx_work);
	unsigned long flags;
	struct nvec_msg *msg;

	spin_lock_irqsave(&nvec->rx_lock, flags);
	while (!list_empty(&nvec->rx_data)) {
		msg = list_first_entry(&nvec->rx_data, struct nvec_msg, node);
		list_del_init(&msg->node);
		spin_unlock_irqrestore(&nvec->rx_lock, flags);

		if (nvec->sync_write_pending ==
		      (msg->data[2] << 8) + msg->data[0]) {
			dev_dbg(nvec->dev, "sync write completed!\n");
			nvec->sync_write_pending = 0;
			nvec->last_sync_msg = msg;
			complete(&nvec->sync_write);
		} else {
			parse_msg(nvec, msg);
			nvec_msg_free(nvec, msg);
		}
		spin_lock_irqsave(&nvec->rx_lock, flags);
	}
	spin_unlock_irqrestore(&nvec->rx_lock, flags);
}

/**
 * nvec_tx_completed - Complete the current transfer
 * @nvec: A &struct nvec_chip
 *
 * This is called when we have received an END_TRANS on a TX transfer.
 */
static void nvec_tx_completed(struct nvec_chip *nvec)
{
	/* We got an END_TRANS, let's skip this, maybe there's an event */
	if (nvec->tx->pos != nvec->tx->size) {
		dev_err(nvec->dev, "premature END_TRANS, resending\n");
		nvec->tx->pos = 0;
		nvec_gpio_set_value(nvec, 0);
	} else {
		nvec->state = 0;
	}
}

/**
 * nvec_rx_completed - Complete the current transfer
 * @nvec: A &struct nvec_chip
 *
 * This is called when we have received an END_TRANS on a RX transfer.
 */
static void nvec_rx_completed(struct nvec_chip *nvec)
{
	if (nvec->rx->pos != nvec_msg_size(nvec->rx)) {
		dev_err(nvec->dev, "RX incomplete: Expected %u bytes, got %u\n",
			   (uint) nvec_msg_size(nvec->rx),
			   (uint) nvec->rx->pos);

		nvec_msg_free(nvec, nvec->rx);
		nvec->state = 0;

		/* Battery quirk - Often incomplete, and likes to crash */
		if (nvec->rx->data[0] == NVEC_BAT)
			complete(&nvec->ec_transfer);

		return;
	}

	spin_lock(&nvec->rx_lock);

	/* add the received data to the work list
	   and move the ring buffer pointer to the next entry */
	list_add_tail(&nvec->rx->node, &nvec->rx_data);

	spin_unlock(&nvec->rx_lock);

	nvec->state = 0;

	if (!nvec_msg_is_event(nvec->rx))
		complete(&nvec->ec_transfer);

	schedule_work(&nvec->rx_work);
}

/**
 * nvec_invalid_flags - Send an error message about invalid flags and jump
 * @nvec: The nvec device
 * @status: The status flags
 * @reset: Whether we shall jump to state 0.
 */
static void nvec_invalid_flags(struct nvec_chip *nvec, unsigned int status,
			       bool reset)
{
	dev_err(nvec->dev, "unexpected status flags 0x%02x during state %i\n",
		status, nvec->state);
	if (reset)
		nvec->state = 0;
}

/**
 * nvec_tx_set - Set the message to transfer (nvec->tx)
 * @nvec: A &struct nvec_chip
 *
 * Gets the first entry from the tx_data list of @nvec and sets the
 * tx member to it. If the tx_data list is empty, this uses the
 * tx_scratch message to send a no operation message.
 */
static void nvec_tx_set(struct nvec_chip *nvec)
{
	spin_lock(&nvec->tx_lock);
	if (list_empty(&nvec->tx_data)) {
		dev_err(nvec->dev, "empty tx - sending no-op\n");
		memcpy(nvec->tx_scratch.data, "\x02\x07\x02", 3);
		nvec->tx_scratch.size = 3;
		nvec->tx_scratch.pos = 0;
		nvec->tx = &nvec->tx_scratch;
		list_add_tail(&nvec->tx->node, &nvec->tx_data);
	} else {
		nvec->tx = list_first_entry(&nvec->tx_data, struct nvec_msg,
					    node);
		nvec->tx->pos = 0;
	}
	spin_unlock(&nvec->tx_lock);

	dev_dbg(nvec->dev, "Sending message of length %u, command 0x%x\n",
		(uint)nvec->tx->size, nvec->tx->data[1]);
}

/**
 * nvec_interrupt - Interrupt handler
 * @irq: The IRQ
 * @dev: The nvec device
 *
 * Interrupt handler that fills our RX buffers and empties our TX
 * buffers. This uses a finite state machine with ridiculous amounts
 * of error checking, in order to be fairly reliable.
 */
static irqreturn_t nvec_interrupt(int irq, void *dev)
{
	unsigned long status;
	unsigned int received = 0;
	unsigned char to_send = 0xff;
	const unsigned long irq_mask = I2C_SL_IRQ | END_TRANS | RCVD | RNW;
	struct nvec_chip *nvec = dev;
	unsigned int state = nvec->state;

	status = readl(nvec->base + I2C_SL_STATUS);

	/* Filter out some errors */
	if ((status & irq_mask) == 0 && (status & ~irq_mask) != 0) {
		dev_err(nvec->dev, "unexpected irq mask %lx\n", status);
		return IRQ_HANDLED;
	}
	if ((status & I2C_SL_IRQ) == 0) {
		dev_err(nvec->dev, "Spurious IRQ\n");
		return IRQ_HANDLED;
	}

	/* The EC did not request a read, so it send us something, read it */
	if ((status & RNW) == 0) {
		received = readl(nvec->base + I2C_SL_RCVD);
		if (status & RCVD)
			writel(0, nvec->base + I2C_SL_RCVD);
	}

	if (status == (I2C_SL_IRQ | RCVD))
		nvec->state = 0;

	switch (nvec->state) {
	case 0:		/* Verify that its a transfer start, the rest later */
		if (status != (I2C_SL_IRQ | RCVD))
			nvec_invalid_flags(nvec, status, false);
		break;
	case 1:		/* command byte */
		if (status != I2C_SL_IRQ) {
			nvec_invalid_flags(nvec, status, true);
		} else {
			nvec->rx = nvec_msg_alloc(nvec, NVEC_MSG_RX);
			/* Should not happen in a normal world */
			if (unlikely(nvec->rx == NULL)) {
				nvec->state = 0;
				break;
			}
			nvec->rx->data[0] = received;
			nvec->rx->pos = 1;
			nvec->state = 2;
		}
		break;
	case 2:		/* first byte after command */
		if (status == (I2C_SL_IRQ | RNW | RCVD)) {
			udelay(33);
			if (nvec->rx->data[0] != 0x01) {
				dev_err(nvec->dev,
					"Read without prior read command\n");
				nvec->state = 0;
				break;
			}
			nvec_msg_free(nvec, nvec->rx);
			nvec->state = 3;
			nvec_tx_set(nvec);
			BUG_ON(nvec->tx->size < 1);
			to_send = nvec->tx->data[0];
			nvec->tx->pos = 1;
		} else if (status == (I2C_SL_IRQ)) {
			BUG_ON(nvec->rx == NULL);
			nvec->rx->data[1] = received;
			nvec->rx->pos = 2;
			nvec->state = 4;
		} else {
			nvec_invalid_flags(nvec, status, true);
		}
		break;
	case 3:		/* EC does a block read, we transmit data */
		if (status & END_TRANS) {
			nvec_tx_completed(nvec);
		} else if ((status & RNW) == 0 || (status & RCVD)) {
			nvec_invalid_flags(nvec, status, true);
		} else if (nvec->tx && nvec->tx->pos < nvec->tx->size) {
			to_send = nvec->tx->data[nvec->tx->pos++];
		} else {
			dev_err(nvec->dev, "tx buffer underflow on %p (%u > %u)\n",
				nvec->tx,
				(uint) (nvec->tx ? nvec->tx->pos : 0),
				(uint) (nvec->tx ? nvec->tx->size : 0));
			nvec->state = 0;
		}
		break;
	case 4:		/* EC does some write, we read the data */
		if ((status & (END_TRANS | RNW)) == END_TRANS)
			nvec_rx_completed(nvec);
		else if (status & (RNW | RCVD))
			nvec_invalid_flags(nvec, status, true);
		else if (nvec->rx && nvec->rx->pos < NVEC_MSG_SIZE)
			nvec->rx->data[nvec->rx->pos++] = received;
		else
			dev_err(nvec->dev,
				"RX buffer overflow on %p: Trying to write byte %u of %u\n",
				nvec->rx, nvec->rx ? nvec->rx->pos : 0,
				NVEC_MSG_SIZE);
		break;
	default:
		nvec->state = 0;
	}

	/* If we are told that a new transfer starts, verify it */
	if ((status & (RCVD | RNW)) == RCVD) {
		if (received != nvec->i2c_addr)
			dev_err(nvec->dev,
			"received address 0x%02x, expected 0x%02x\n",
			received, nvec->i2c_addr);
		nvec->state = 1;
	}

	/* Send data if requested, but not on end of transmission */
	if ((status & (RNW | END_TRANS)) == RNW)
		writel(to_send, nvec->base + I2C_SL_RCVD);

	/* If we have send the first byte */
	if (status == (I2C_SL_IRQ | RNW | RCVD))
		nvec_gpio_set_value(nvec, 1);

	dev_dbg(nvec->dev,
		"Handled: %s 0x%02x, %s 0x%02x in state %u [%s%s%s]\n",
		(status & RNW) == 0 ? "received" : "R=",
		received,
		(status & (RNW | END_TRANS)) ? "sent" : "S=",
		to_send,
		state,
		status & END_TRANS ? " END_TRANS" : "",
		status & RCVD ? " RCVD" : "",
		status & RNW ? " RNW" : "");


	/*
	 * TODO: A correct fix needs to be found for this.
	 *
	 * We experience less incomplete messages with this delay than without
	 * it, but we don't know why. Help is appreciated.
	 */
	udelay(100);

	return IRQ_HANDLED;
}

static void tegra_init_i2c_slave(struct nvec_chip *nvec)
{
	u32 val;

	clk_prepare_enable(nvec->i2c_clk);

	reset_control_assert(nvec->rst);
	udelay(2);
	reset_control_deassert(nvec->rst);

	val = I2C_CNFG_NEW_MASTER_SFM | I2C_CNFG_PACKET_MODE_EN |
	    (0x2 << I2C_CNFG_DEBOUNCE_CNT_SHIFT);
	writel(val, nvec->base + I2C_CNFG);

	clk_set_rate(nvec->i2c_clk, 8 * 80000);

	writel(I2C_SL_NEWSL, nvec->base + I2C_SL_CNFG);
	writel(0x1E, nvec->base + I2C_SL_DELAY_COUNT);

	writel(nvec->i2c_addr>>1, nvec->base + I2C_SL_ADDR1);
	writel(0, nvec->base + I2C_SL_ADDR2);

	enable_irq(nvec->irq);
}

#ifdef CONFIG_PM_SLEEP
static void nvec_disable_i2c_slave(struct nvec_chip *nvec)
{
	disable_irq(nvec->irq);
	writel(I2C_SL_NEWSL | I2C_SL_NACK, nvec->base + I2C_SL_CNFG);
	clk_disable_unprepare(nvec->i2c_clk);
}
#endif

static void nvec_power_off(void)
{
	char ap_pwr_down[] = { NVEC_SLEEP, AP_PWR_DOWN };

	nvec_toggle_global_events(nvec_power_handle, false);
	nvec_write_async(nvec_power_handle, ap_pwr_down, 2);
}

/*
 *  Parse common device tree data
 */
static int nvec_i2c_parse_dt_pdata(struct nvec_chip *nvec)
{
	nvec->gpio = of_get_named_gpio(nvec->dev->of_node, "request-gpios", 0);

	if (nvec->gpio < 0) {
		dev_err(nvec->dev, "no gpio specified");
		return -ENODEV;
	}

	if (of_property_read_u32(nvec->dev->of_node, "slave-addr",
				&nvec->i2c_addr)) {
		dev_err(nvec->dev, "no i2c address specified");
		return -ENODEV;
	}

	return 0;
}

static int tegra_nvec_probe(struct platform_device *pdev)
{
	int err, ret;
	struct clk *i2c_clk;
	struct nvec_chip *nvec;
	struct nvec_msg *msg;
	struct resource *res;
	void __iomem *base;
	char	get_firmware_version[] = { NVEC_CNTL, GET_FIRMWARE_VERSION },
		unmute_speakers[] = { NVEC_OEM0, 0x10, 0x59, 0x95 },
		enable_event[7] = { NVEC_SYS, CNF_EVENT_REPORTING, true };

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "must be instantiated using device tree\n");
		return -ENODEV;
	}

	nvec = devm_kzalloc(&pdev->dev, sizeof(struct nvec_chip), GFP_KERNEL);
	if (nvec == NULL) {
		dev_err(&pdev->dev, "failed to reserve memory\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, nvec);
	nvec->dev = &pdev->dev;

	err = nvec_i2c_parse_dt_pdata(nvec);
	if (err < 0)
		return err;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return -ENODEV;
	}

	i2c_clk = devm_clk_get(&pdev->dev, "div-clk");
	if (IS_ERR(i2c_clk)) {
		dev_err(nvec->dev, "failed to get controller clock\n");
		return -ENODEV;
	}

	nvec->rst = devm_reset_control_get(&pdev->dev, "i2c");
	if (IS_ERR(nvec->rst)) {
		dev_err(nvec->dev, "failed to get controller reset\n");
		return PTR_ERR(nvec->rst);
	}

	nvec->base = base;
	nvec->irq = res->start;
	nvec->i2c_clk = i2c_clk;
	nvec->rx = &nvec->msg_pool[0];

	ATOMIC_INIT_NOTIFIER_HEAD(&nvec->notifier_list);

	init_completion(&nvec->sync_write);
	init_completion(&nvec->ec_transfer);
	mutex_init(&nvec->sync_write_mutex);
	spin_lock_init(&nvec->tx_lock);
	spin_lock_init(&nvec->rx_lock);
	INIT_LIST_HEAD(&nvec->rx_data);
	INIT_LIST_HEAD(&nvec->tx_data);
	INIT_WORK(&nvec->rx_work, nvec_dispatch);
	INIT_WORK(&nvec->tx_work, nvec_request_master);

	err = devm_gpio_request_one(&pdev->dev, nvec->gpio, GPIOF_OUT_INIT_HIGH,
					"nvec gpio");
	if (err < 0) {
		dev_err(nvec->dev, "couldn't request gpio\n");
		return -ENODEV;
	}

	err = devm_request_irq(&pdev->dev, nvec->irq, nvec_interrupt, 0,
				"nvec", nvec);
	if (err) {
		dev_err(nvec->dev, "couldn't request irq\n");
		return -ENODEV;
	}
	disable_irq(nvec->irq);

	tegra_init_i2c_slave(nvec);

	/* enable event reporting */
	nvec_toggle_global_events(nvec, true);

	nvec->nvec_status_notifier.notifier_call = nvec_status_notifier;
	nvec_register_notifier(nvec, &nvec->nvec_status_notifier, 0);

	nvec_power_handle = nvec;
	pm_power_off = nvec_power_off;

	/* Get Firmware Version */
	msg = nvec_write_sync(nvec, get_firmware_version, 2);

	if (msg) {
		dev_warn(nvec->dev, "ec firmware version %02x.%02x.%02x / %02x\n",
			msg->data[4], msg->data[5], msg->data[6], msg->data[7]);

		nvec_msg_free(nvec, msg);
	}

	ret = mfd_add_devices(nvec->dev, -1, nvec_devices,
			      ARRAY_SIZE(nvec_devices), base, 0, NULL);
	if (ret)
		dev_err(nvec->dev, "error adding subdevices\n");

	/* unmute speakers? */
	nvec_write_async(nvec, unmute_speakers, 4);

	/* enable lid switch event */
	nvec_event_mask(enable_event, LID_SWITCH);
	nvec_write_async(nvec, enable_event, 7);

	/* enable power button event */
	nvec_event_mask(enable_event, PWR_BUTTON);
	nvec_write_async(nvec, enable_event, 7);

	return 0;
}

static int tegra_nvec_remove(struct platform_device *pdev)
{
	struct nvec_chip *nvec = platform_get_drvdata(pdev);

	nvec_toggle_global_events(nvec, false);
	mfd_remove_devices(nvec->dev);
	nvec_unregister_notifier(nvec, &nvec->nvec_status_notifier);
	cancel_work_sync(&nvec->rx_work);
	cancel_work_sync(&nvec->tx_work);
	/* FIXME: needs check wether nvec is responsible for power off */
	pm_power_off = NULL;

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int nvec_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvec_chip *nvec = platform_get_drvdata(pdev);
	struct nvec_msg *msg;
	char ap_suspend[] = { NVEC_SLEEP, AP_SUSPEND };

	dev_dbg(nvec->dev, "suspending\n");

	/* keep these sync or you'll break suspend */
	nvec_toggle_global_events(nvec, false);

	msg = nvec_write_sync(nvec, ap_suspend, sizeof(ap_suspend));
	nvec_msg_free(nvec, msg);

	nvec_disable_i2c_slave(nvec);

	return 0;
}

static int nvec_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct nvec_chip *nvec = platform_get_drvdata(pdev);

	dev_dbg(nvec->dev, "resuming\n");
	tegra_init_i2c_slave(nvec);
	nvec_toggle_global_events(nvec, true);

	return 0;
}
#endif

static const SIMPLE_DEV_PM_OPS(nvec_pm_ops, nvec_suspend, nvec_resume);

/* Match table for of_platform binding */
static const struct of_device_id nvidia_nvec_of_match[] = {
	{ .compatible = "nvidia,nvec", },
	{},
};
MODULE_DEVICE_TABLE(of, nvidia_nvec_of_match);

static struct platform_driver nvec_device_driver = {
	.probe   = tegra_nvec_probe,
	.remove  = tegra_nvec_remove,
	.driver  = {
		.name = "nvec",
		.owner = THIS_MODULE,
		.pm = &nvec_pm_ops,
		.of_match_table = nvidia_nvec_of_match,
	}
};

module_platform_driver(nvec_device_driver);

MODULE_ALIAS("platform:nvec");
MODULE_DESCRIPTION("NVIDIA compliant embedded controller interface");
MODULE_AUTHOR("Marc Dietrich <marvin24@gmx.de>");
MODULE_LICENSE("GPL");
