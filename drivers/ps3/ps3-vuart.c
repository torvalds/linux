/*
 *  PS3 virtual uart
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <asm/ps3.h>

#include <asm/firmware.h>
#include <asm/lv1call.h>

#include "vuart.h"

MODULE_AUTHOR("Sony Corporation");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PS3 vuart");

/**
 * vuart - An inter-partition data link service.
 *  port 0: PS3 AV Settings.
 *  port 2: PS3 System Manager.
 *
 * The vuart provides a bi-directional byte stream data link between logical
 * partitions.  Its primary role is as a communications link between the guest
 * OS and the system policy module.  The current HV does not support any
 * connections other than those listed.
 */

enum {PORT_COUNT = 3,};

enum vuart_param {
	PARAM_TX_TRIGGER = 0,
	PARAM_RX_TRIGGER = 1,
	PARAM_INTERRUPT_MASK = 2,
	PARAM_RX_BUF_SIZE = 3, /* read only */
	PARAM_RX_BYTES = 4, /* read only */
	PARAM_TX_BUF_SIZE = 5, /* read only */
	PARAM_TX_BYTES = 6, /* read only */
	PARAM_INTERRUPT_STATUS = 7, /* read only */
};

enum vuart_interrupt_bit {
	INTERRUPT_BIT_TX = 0,
	INTERRUPT_BIT_RX = 1,
	INTERRUPT_BIT_DISCONNECT = 2,
};

enum vuart_interrupt_mask {
	INTERRUPT_MASK_TX = 1,
	INTERRUPT_MASK_RX = 2,
	INTERRUPT_MASK_DISCONNECT = 4,
};

/**
 * struct ps3_vuart_port_priv - private vuart device data.
 */

struct ps3_vuart_port_priv {
	u64 interrupt_mask;

	struct {
		spinlock_t lock;
		struct list_head head;
	} tx_list;
	struct {
		struct ps3_vuart_work work;
		unsigned long bytes_held;
		spinlock_t lock;
		struct list_head head;
	} rx_list;
	struct ps3_vuart_stats stats;
};

static struct ps3_vuart_port_priv *to_port_priv(
	struct ps3_system_bus_device *dev)
{
	BUG_ON(!dev);
	BUG_ON(!dev->driver_priv);
	return (struct ps3_vuart_port_priv *)dev->driver_priv;
}

/**
 * struct ports_bmp - bitmap indicating ports needing service.
 *
 * A 256 bit read only bitmap indicating ports needing service.  Do not write
 * to these bits.  Must not cross a page boundary.
 */

struct ports_bmp {
	u64 status;
	u64 unused[3];
} __attribute__((aligned(32)));

#define dump_ports_bmp(_b) _dump_ports_bmp(_b, __func__, __LINE__)
static void __maybe_unused _dump_ports_bmp(
	const struct ports_bmp *bmp, const char *func, int line)
{
	pr_debug("%s:%d: ports_bmp: %016llxh\n", func, line, bmp->status);
}

#define dump_port_params(_b) _dump_port_params(_b, __func__, __LINE__)
static void __maybe_unused _dump_port_params(unsigned int port_number,
	const char *func, int line)
{
#if defined(DEBUG)
	static const char *strings[] = {
		"tx_trigger      ",
		"rx_trigger      ",
		"interrupt_mask  ",
		"rx_buf_size     ",
		"rx_bytes        ",
		"tx_buf_size     ",
		"tx_bytes        ",
		"interrupt_status",
	};
	int result;
	unsigned int i;
	u64 value;

	for (i = 0; i < ARRAY_SIZE(strings); i++) {
		result = lv1_get_virtual_uart_param(port_number, i, &value);

		if (result) {
			pr_debug("%s:%d: port_%u: %s failed: %s\n", func, line,
				port_number, strings[i], ps3_result(result));
			continue;
		}
		pr_debug("%s:%d: port_%u: %s = %lxh\n",
			func, line, port_number, strings[i], value);
	}
#endif
}

int ps3_vuart_get_triggers(struct ps3_system_bus_device *dev,
	struct vuart_triggers *trig)
{
	int result;
	u64 size;
	u64 val;
	u64 tx;

	result = lv1_get_virtual_uart_param(dev->port_number,
		PARAM_TX_TRIGGER, &tx);
	trig->tx = tx;

	if (result) {
		dev_dbg(&dev->core, "%s:%d: tx_trigger failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	result = lv1_get_virtual_uart_param(dev->port_number,
		PARAM_RX_BUF_SIZE, &size);

	if (result) {
		dev_dbg(&dev->core, "%s:%d: tx_buf_size failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	result = lv1_get_virtual_uart_param(dev->port_number,
		PARAM_RX_TRIGGER, &val);

	if (result) {
		dev_dbg(&dev->core, "%s:%d: rx_trigger failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	trig->rx = size - val;

	dev_dbg(&dev->core, "%s:%d: tx %lxh, rx %lxh\n", __func__, __LINE__,
		trig->tx, trig->rx);

	return result;
}

int ps3_vuart_set_triggers(struct ps3_system_bus_device *dev, unsigned int tx,
	unsigned int rx)
{
	int result;
	u64 size;

	result = lv1_set_virtual_uart_param(dev->port_number,
		PARAM_TX_TRIGGER, tx);

	if (result) {
		dev_dbg(&dev->core, "%s:%d: tx_trigger failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	result = lv1_get_virtual_uart_param(dev->port_number,
		PARAM_RX_BUF_SIZE, &size);

	if (result) {
		dev_dbg(&dev->core, "%s:%d: tx_buf_size failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	result = lv1_set_virtual_uart_param(dev->port_number,
		PARAM_RX_TRIGGER, size - rx);

	if (result) {
		dev_dbg(&dev->core, "%s:%d: rx_trigger failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	dev_dbg(&dev->core, "%s:%d: tx %xh, rx %xh\n", __func__, __LINE__,
		tx, rx);

	return result;
}

static int ps3_vuart_get_rx_bytes_waiting(struct ps3_system_bus_device *dev,
	u64 *bytes_waiting)
{
	int result;

	result = lv1_get_virtual_uart_param(dev->port_number,
		PARAM_RX_BYTES, bytes_waiting);

	if (result)
		dev_dbg(&dev->core, "%s:%d: rx_bytes failed: %s\n",
			__func__, __LINE__, ps3_result(result));

	dev_dbg(&dev->core, "%s:%d: %llxh\n", __func__, __LINE__,
		*bytes_waiting);
	return result;
}

/**
 * ps3_vuart_set_interrupt_mask - Enable/disable the port interrupt sources.
 * @dev: The struct ps3_system_bus_device instance.
 * @bmp: Logical OR of enum vuart_interrupt_mask values. A zero bit disables.
 */

static int ps3_vuart_set_interrupt_mask(struct ps3_system_bus_device *dev,
	unsigned long mask)
{
	int result;
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);

	dev_dbg(&dev->core, "%s:%d: %lxh\n", __func__, __LINE__, mask);

	priv->interrupt_mask = mask;

	result = lv1_set_virtual_uart_param(dev->port_number,
		PARAM_INTERRUPT_MASK, priv->interrupt_mask);

	if (result)
		dev_dbg(&dev->core, "%s:%d: interrupt_mask failed: %s\n",
			__func__, __LINE__, ps3_result(result));

	return result;
}

static int ps3_vuart_get_interrupt_status(struct ps3_system_bus_device *dev,
	unsigned long *status)
{
	int result;
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);
	u64 tmp;

	result = lv1_get_virtual_uart_param(dev->port_number,
		PARAM_INTERRUPT_STATUS, &tmp);

	if (result)
		dev_dbg(&dev->core, "%s:%d: interrupt_status failed: %s\n",
			__func__, __LINE__, ps3_result(result));

	*status = tmp & priv->interrupt_mask;

	dev_dbg(&dev->core, "%s:%d: m %llxh, s %llxh, m&s %lxh\n",
		__func__, __LINE__, priv->interrupt_mask, tmp, *status);

	return result;
}

int ps3_vuart_enable_interrupt_tx(struct ps3_system_bus_device *dev)
{
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);

	return (priv->interrupt_mask & INTERRUPT_MASK_TX) ? 0
		: ps3_vuart_set_interrupt_mask(dev, priv->interrupt_mask
		| INTERRUPT_MASK_TX);
}

int ps3_vuart_enable_interrupt_rx(struct ps3_system_bus_device *dev)
{
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);

	return (priv->interrupt_mask & INTERRUPT_MASK_RX) ? 0
		: ps3_vuart_set_interrupt_mask(dev, priv->interrupt_mask
		| INTERRUPT_MASK_RX);
}

int ps3_vuart_enable_interrupt_disconnect(struct ps3_system_bus_device *dev)
{
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);

	return (priv->interrupt_mask & INTERRUPT_MASK_DISCONNECT) ? 0
		: ps3_vuart_set_interrupt_mask(dev, priv->interrupt_mask
		| INTERRUPT_MASK_DISCONNECT);
}

int ps3_vuart_disable_interrupt_tx(struct ps3_system_bus_device *dev)
{
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);

	return (priv->interrupt_mask & INTERRUPT_MASK_TX)
		? ps3_vuart_set_interrupt_mask(dev, priv->interrupt_mask
		& ~INTERRUPT_MASK_TX) : 0;
}

int ps3_vuart_disable_interrupt_rx(struct ps3_system_bus_device *dev)
{
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);

	return (priv->interrupt_mask & INTERRUPT_MASK_RX)
		? ps3_vuart_set_interrupt_mask(dev, priv->interrupt_mask
		& ~INTERRUPT_MASK_RX) : 0;
}

int ps3_vuart_disable_interrupt_disconnect(struct ps3_system_bus_device *dev)
{
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);

	return (priv->interrupt_mask & INTERRUPT_MASK_DISCONNECT)
		? ps3_vuart_set_interrupt_mask(dev, priv->interrupt_mask
		& ~INTERRUPT_MASK_DISCONNECT) : 0;
}

/**
 * ps3_vuart_raw_write - Low level write helper.
 * @dev: The struct ps3_system_bus_device instance.
 *
 * Do not call ps3_vuart_raw_write directly, use ps3_vuart_write.
 */

static int ps3_vuart_raw_write(struct ps3_system_bus_device *dev,
	const void *buf, unsigned int bytes, u64 *bytes_written)
{
	int result;
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);

	result = lv1_write_virtual_uart(dev->port_number,
		ps3_mm_phys_to_lpar(__pa(buf)), bytes, bytes_written);

	if (result) {
		dev_dbg(&dev->core, "%s:%d: lv1_write_virtual_uart failed: "
			"%s\n", __func__, __LINE__, ps3_result(result));
		return result;
	}

	priv->stats.bytes_written += *bytes_written;

	dev_dbg(&dev->core, "%s:%d: wrote %llxh/%xh=>%lxh\n", __func__, __LINE__,
		*bytes_written, bytes, priv->stats.bytes_written);

	return result;
}

/**
 * ps3_vuart_raw_read - Low level read helper.
 * @dev: The struct ps3_system_bus_device instance.
 *
 * Do not call ps3_vuart_raw_read directly, use ps3_vuart_read.
 */

static int ps3_vuart_raw_read(struct ps3_system_bus_device *dev, void *buf,
	unsigned int bytes, u64 *bytes_read)
{
	int result;
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);

	dev_dbg(&dev->core, "%s:%d: %xh\n", __func__, __LINE__, bytes);

	result = lv1_read_virtual_uart(dev->port_number,
		ps3_mm_phys_to_lpar(__pa(buf)), bytes, bytes_read);

	if (result) {
		dev_dbg(&dev->core, "%s:%d: lv1_read_virtual_uart failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	priv->stats.bytes_read += *bytes_read;

	dev_dbg(&dev->core, "%s:%d: read %llxh/%xh=>%lxh\n", __func__, __LINE__,
		*bytes_read, bytes, priv->stats.bytes_read);

	return result;
}

/**
 * ps3_vuart_clear_rx_bytes - Discard bytes received.
 * @dev: The struct ps3_system_bus_device instance.
 * @bytes: Max byte count to discard, zero = all pending.
 *
 * Used to clear pending rx interrupt source.  Will not block.
 */

void ps3_vuart_clear_rx_bytes(struct ps3_system_bus_device *dev,
	unsigned int bytes)
{
	int result;
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);
	u64 bytes_waiting;
	void *tmp;

	result = ps3_vuart_get_rx_bytes_waiting(dev, &bytes_waiting);

	BUG_ON(result);

	bytes = bytes ? min(bytes, (unsigned int)bytes_waiting) : bytes_waiting;

	dev_dbg(&dev->core, "%s:%d: %u\n", __func__, __LINE__, bytes);

	if (!bytes)
		return;

	/* Add some extra space for recently arrived data. */

	bytes += 128;

	tmp = kmalloc(bytes, GFP_KERNEL);

	if (!tmp)
		return;

	ps3_vuart_raw_read(dev, tmp, bytes, &bytes_waiting);

	kfree(tmp);

	/* Don't include these bytes in the stats. */

	priv->stats.bytes_read -= bytes_waiting;
}
EXPORT_SYMBOL_GPL(ps3_vuart_clear_rx_bytes);

/**
 * struct list_buffer - An element for a port device fifo buffer list.
 */

struct list_buffer {
	struct list_head link;
	const unsigned char *head;
	const unsigned char *tail;
	unsigned long dbg_number;
	unsigned char data[];
};

/**
 * ps3_vuart_write - the entry point for writing data to a port
 * @dev: The struct ps3_system_bus_device instance.
 *
 * If the port is idle on entry as much of the incoming data is written to
 * the port as the port will accept.  Otherwise a list buffer is created
 * and any remaning incoming data is copied to that buffer.  The buffer is
 * then enqueued for transmision via the transmit interrupt.
 */

int ps3_vuart_write(struct ps3_system_bus_device *dev, const void *buf,
	unsigned int bytes)
{
	static unsigned long dbg_number;
	int result;
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);
	unsigned long flags;
	struct list_buffer *lb;

	dev_dbg(&dev->core, "%s:%d: %u(%xh) bytes\n", __func__, __LINE__,
		bytes, bytes);

	spin_lock_irqsave(&priv->tx_list.lock, flags);

	if (list_empty(&priv->tx_list.head)) {
		u64 bytes_written;

		result = ps3_vuart_raw_write(dev, buf, bytes, &bytes_written);

		spin_unlock_irqrestore(&priv->tx_list.lock, flags);

		if (result) {
			dev_dbg(&dev->core,
				"%s:%d: ps3_vuart_raw_write failed\n",
				__func__, __LINE__);
			return result;
		}

		if (bytes_written == bytes) {
			dev_dbg(&dev->core, "%s:%d: wrote %xh bytes\n",
				__func__, __LINE__, bytes);
			return 0;
		}

		bytes -= bytes_written;
		buf += bytes_written;
	} else
		spin_unlock_irqrestore(&priv->tx_list.lock, flags);

	lb = kmalloc(sizeof(struct list_buffer) + bytes, GFP_KERNEL);

	if (!lb)
		return -ENOMEM;

	memcpy(lb->data, buf, bytes);
	lb->head = lb->data;
	lb->tail = lb->data + bytes;
	lb->dbg_number = ++dbg_number;

	spin_lock_irqsave(&priv->tx_list.lock, flags);
	list_add_tail(&lb->link, &priv->tx_list.head);
	ps3_vuart_enable_interrupt_tx(dev);
	spin_unlock_irqrestore(&priv->tx_list.lock, flags);

	dev_dbg(&dev->core, "%s:%d: queued buf_%lu, %xh bytes\n",
		__func__, __LINE__, lb->dbg_number, bytes);

	return 0;
}
EXPORT_SYMBOL_GPL(ps3_vuart_write);

/**
 * ps3_vuart_queue_rx_bytes - Queue waiting bytes into the buffer list.
 * @dev: The struct ps3_system_bus_device instance.
 * @bytes_queued: Number of bytes queued to the buffer list.
 *
 * Must be called with priv->rx_list.lock held.
 */

static int ps3_vuart_queue_rx_bytes(struct ps3_system_bus_device *dev,
	u64 *bytes_queued)
{
	static unsigned long dbg_number;
	int result;
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);
	struct list_buffer *lb;
	u64 bytes;

	*bytes_queued = 0;

	result = ps3_vuart_get_rx_bytes_waiting(dev, &bytes);
	BUG_ON(result);

	if (result)
		return -EIO;

	if (!bytes)
		return 0;

	/* Add some extra space for recently arrived data. */

	bytes += 128;

	lb = kmalloc(sizeof(struct list_buffer) + bytes, GFP_ATOMIC);

	if (!lb)
		return -ENOMEM;

	ps3_vuart_raw_read(dev, lb->data, bytes, &bytes);

	lb->head = lb->data;
	lb->tail = lb->data + bytes;
	lb->dbg_number = ++dbg_number;

	list_add_tail(&lb->link, &priv->rx_list.head);
	priv->rx_list.bytes_held += bytes;

	dev_dbg(&dev->core, "%s:%d: buf_%lu: queued %llxh bytes\n",
		__func__, __LINE__, lb->dbg_number, bytes);

	*bytes_queued = bytes;

	return 0;
}

/**
 * ps3_vuart_read - The entry point for reading data from a port.
 *
 * Queue data waiting at the port, and if enough bytes to satisfy the request
 * are held in the buffer list those bytes are dequeued and copied to the
 * caller's buffer.  Emptied list buffers are retiered.  If the request cannot
 * be statified by bytes held in the list buffers -EAGAIN is returned.
 */

int ps3_vuart_read(struct ps3_system_bus_device *dev, void *buf,
	unsigned int bytes)
{
	int result;
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);
	unsigned long flags;
	struct list_buffer *lb, *n;
	unsigned long bytes_read;

	dev_dbg(&dev->core, "%s:%d: %u(%xh) bytes\n", __func__, __LINE__,
		bytes, bytes);

	spin_lock_irqsave(&priv->rx_list.lock, flags);

	/* Queue rx bytes here for polled reads. */

	while (priv->rx_list.bytes_held < bytes) {
		u64 tmp;

		result = ps3_vuart_queue_rx_bytes(dev, &tmp);
		if (result || !tmp) {
			dev_dbg(&dev->core, "%s:%d: starved for %lxh bytes\n",
				__func__, __LINE__,
				bytes - priv->rx_list.bytes_held);
			spin_unlock_irqrestore(&priv->rx_list.lock, flags);
			return -EAGAIN;
		}
	}

	list_for_each_entry_safe(lb, n, &priv->rx_list.head, link) {
		bytes_read = min((unsigned int)(lb->tail - lb->head), bytes);

		memcpy(buf, lb->head, bytes_read);
		buf += bytes_read;
		bytes -= bytes_read;
		priv->rx_list.bytes_held -= bytes_read;

		if (bytes_read < lb->tail - lb->head) {
			lb->head += bytes_read;
			dev_dbg(&dev->core, "%s:%d: buf_%lu: dequeued %lxh "
				"bytes\n", __func__, __LINE__, lb->dbg_number,
				bytes_read);
			spin_unlock_irqrestore(&priv->rx_list.lock, flags);
			return 0;
		}

		dev_dbg(&dev->core, "%s:%d: buf_%lu: free, dequeued %lxh "
			"bytes\n", __func__, __LINE__, lb->dbg_number,
			bytes_read);

		list_del(&lb->link);
		kfree(lb);
	}

	spin_unlock_irqrestore(&priv->rx_list.lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(ps3_vuart_read);

/**
 * ps3_vuart_work - Asynchronous read handler.
 */

static void ps3_vuart_work(struct work_struct *work)
{
	struct ps3_system_bus_device *dev =
		ps3_vuart_work_to_system_bus_dev(work);
	struct ps3_vuart_port_driver *drv =
		ps3_system_bus_dev_to_vuart_drv(dev);

	BUG_ON(!drv);
	drv->work(dev);
}

int ps3_vuart_read_async(struct ps3_system_bus_device *dev, unsigned int bytes)
{
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);
	unsigned long flags;

	if (priv->rx_list.work.trigger) {
		dev_dbg(&dev->core, "%s:%d: warning, multiple calls\n",
			__func__, __LINE__);
		return -EAGAIN;
	}

	BUG_ON(!bytes);

	spin_lock_irqsave(&priv->rx_list.lock, flags);
	if (priv->rx_list.bytes_held >= bytes) {
		dev_dbg(&dev->core, "%s:%d: schedule_work %xh bytes\n",
			__func__, __LINE__, bytes);
		schedule_work(&priv->rx_list.work.work);
		spin_unlock_irqrestore(&priv->rx_list.lock, flags);
		return 0;
	}

	priv->rx_list.work.trigger = bytes;
	spin_unlock_irqrestore(&priv->rx_list.lock, flags);

	dev_dbg(&dev->core, "%s:%d: waiting for %u(%xh) bytes\n", __func__,
		__LINE__, bytes, bytes);

	return 0;
}
EXPORT_SYMBOL_GPL(ps3_vuart_read_async);

void ps3_vuart_cancel_async(struct ps3_system_bus_device *dev)
{
	to_port_priv(dev)->rx_list.work.trigger = 0;
}
EXPORT_SYMBOL_GPL(ps3_vuart_cancel_async);

/**
 * ps3_vuart_handle_interrupt_tx - third stage transmit interrupt handler
 *
 * Services the transmit interrupt for the port.  Writes as much data from the
 * buffer list as the port will accept.  Retires any emptied list buffers and
 * adjusts the final list buffer state for a partial write.
 */

static int ps3_vuart_handle_interrupt_tx(struct ps3_system_bus_device *dev)
{
	int result = 0;
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);
	unsigned long flags;
	struct list_buffer *lb, *n;
	unsigned long bytes_total = 0;

	dev_dbg(&dev->core, "%s:%d\n", __func__, __LINE__);

	spin_lock_irqsave(&priv->tx_list.lock, flags);

	list_for_each_entry_safe(lb, n, &priv->tx_list.head, link) {

		u64 bytes_written;

		result = ps3_vuart_raw_write(dev, lb->head, lb->tail - lb->head,
			&bytes_written);

		if (result) {
			dev_dbg(&dev->core,
				"%s:%d: ps3_vuart_raw_write failed\n",
				__func__, __LINE__);
			break;
		}

		bytes_total += bytes_written;

		if (bytes_written < lb->tail - lb->head) {
			lb->head += bytes_written;
			dev_dbg(&dev->core,
				"%s:%d cleared buf_%lu, %llxh bytes\n",
				__func__, __LINE__, lb->dbg_number,
				bytes_written);
			goto port_full;
		}

		dev_dbg(&dev->core, "%s:%d free buf_%lu\n", __func__, __LINE__,
			lb->dbg_number);

		list_del(&lb->link);
		kfree(lb);
	}

	ps3_vuart_disable_interrupt_tx(dev);
port_full:
	spin_unlock_irqrestore(&priv->tx_list.lock, flags);
	dev_dbg(&dev->core, "%s:%d wrote %lxh bytes total\n",
		__func__, __LINE__, bytes_total);
	return result;
}

/**
 * ps3_vuart_handle_interrupt_rx - third stage receive interrupt handler
 *
 * Services the receive interrupt for the port.  Creates a list buffer and
 * copies all waiting port data to that buffer and enqueues the buffer in the
 * buffer list.  Buffer list data is dequeued via ps3_vuart_read.
 */

static int ps3_vuart_handle_interrupt_rx(struct ps3_system_bus_device *dev)
{
	int result;
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);
	unsigned long flags;
	u64 bytes;

	dev_dbg(&dev->core, "%s:%d\n", __func__, __LINE__);

	spin_lock_irqsave(&priv->rx_list.lock, flags);
	result = ps3_vuart_queue_rx_bytes(dev, &bytes);

	if (result) {
		spin_unlock_irqrestore(&priv->rx_list.lock, flags);
		return result;
	}

	if (priv->rx_list.work.trigger && priv->rx_list.bytes_held
		>= priv->rx_list.work.trigger) {
		dev_dbg(&dev->core, "%s:%d: schedule_work %lxh bytes\n",
			__func__, __LINE__, priv->rx_list.work.trigger);
		priv->rx_list.work.trigger = 0;
		schedule_work(&priv->rx_list.work.work);
	}

	spin_unlock_irqrestore(&priv->rx_list.lock, flags);
	return result;
}

static int ps3_vuart_handle_interrupt_disconnect(
	struct ps3_system_bus_device *dev)
{
	dev_dbg(&dev->core, "%s:%d\n", __func__, __LINE__);
	BUG_ON("no support");
	return -1;
}

/**
 * ps3_vuart_handle_port_interrupt - second stage interrupt handler
 *
 * Services any pending interrupt types for the port.  Passes control to the
 * third stage type specific interrupt handler.  Returns control to the first
 * stage handler after one iteration.
 */

static int ps3_vuart_handle_port_interrupt(struct ps3_system_bus_device *dev)
{
	int result;
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);
	unsigned long status;

	result = ps3_vuart_get_interrupt_status(dev, &status);

	if (result)
		return result;

	dev_dbg(&dev->core, "%s:%d: status: %lxh\n", __func__, __LINE__,
		status);

	if (status & INTERRUPT_MASK_DISCONNECT) {
		priv->stats.disconnect_interrupts++;
		result = ps3_vuart_handle_interrupt_disconnect(dev);
		if (result)
			ps3_vuart_disable_interrupt_disconnect(dev);
	}

	if (status & INTERRUPT_MASK_TX) {
		priv->stats.tx_interrupts++;
		result = ps3_vuart_handle_interrupt_tx(dev);
		if (result)
			ps3_vuart_disable_interrupt_tx(dev);
	}

	if (status & INTERRUPT_MASK_RX) {
		priv->stats.rx_interrupts++;
		result = ps3_vuart_handle_interrupt_rx(dev);
		if (result)
			ps3_vuart_disable_interrupt_rx(dev);
	}

	return 0;
}

struct vuart_bus_priv {
	struct ports_bmp *bmp;
	unsigned int virq;
	struct mutex probe_mutex;
	int use_count;
	struct ps3_system_bus_device *devices[PORT_COUNT];
} static vuart_bus_priv;

/**
 * ps3_vuart_irq_handler - first stage interrupt handler
 *
 * Loops finding any interrupting port and its associated instance data.
 * Passes control to the second stage port specific interrupt handler.  Loops
 * until all outstanding interrupts are serviced.
 */

static irqreturn_t ps3_vuart_irq_handler(int irq, void *_private)
{
	struct vuart_bus_priv *bus_priv = _private;

	BUG_ON(!bus_priv);

	while (1) {
		unsigned int port;

		dump_ports_bmp(bus_priv->bmp);

		port = (BITS_PER_LONG - 1) - __ilog2(bus_priv->bmp->status);

		if (port == BITS_PER_LONG)
			break;

		BUG_ON(port >= PORT_COUNT);
		BUG_ON(!bus_priv->devices[port]);

		ps3_vuart_handle_port_interrupt(bus_priv->devices[port]);
	}

	return IRQ_HANDLED;
}

static int ps3_vuart_bus_interrupt_get(void)
{
	int result;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	vuart_bus_priv.use_count++;

	BUG_ON(vuart_bus_priv.use_count > 2);

	if (vuart_bus_priv.use_count != 1)
		return 0;

	BUG_ON(vuart_bus_priv.bmp);

	vuart_bus_priv.bmp = kzalloc(sizeof(struct ports_bmp), GFP_KERNEL);

	if (!vuart_bus_priv.bmp) {
		pr_debug("%s:%d: kzalloc failed.\n", __func__, __LINE__);
		result = -ENOMEM;
		goto fail_bmp_malloc;
	}

	result = ps3_vuart_irq_setup(PS3_BINDING_CPU_ANY, vuart_bus_priv.bmp,
		&vuart_bus_priv.virq);

	if (result) {
		pr_debug("%s:%d: ps3_vuart_irq_setup failed (%d)\n",
			__func__, __LINE__, result);
		result = -EPERM;
		goto fail_alloc_irq;
	}

	result = request_irq(vuart_bus_priv.virq, ps3_vuart_irq_handler,
		0, "vuart", &vuart_bus_priv);

	if (result) {
		pr_debug("%s:%d: request_irq failed (%d)\n",
			__func__, __LINE__, result);
		goto fail_request_irq;
	}

	pr_debug(" <- %s:%d: ok\n", __func__, __LINE__);
	return result;

fail_request_irq:
	ps3_vuart_irq_destroy(vuart_bus_priv.virq);
	vuart_bus_priv.virq = NO_IRQ;
fail_alloc_irq:
	kfree(vuart_bus_priv.bmp);
	vuart_bus_priv.bmp = NULL;
fail_bmp_malloc:
	vuart_bus_priv.use_count--;
	pr_debug(" <- %s:%d: failed\n", __func__, __LINE__);
	return result;
}

static int ps3_vuart_bus_interrupt_put(void)
{
	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	vuart_bus_priv.use_count--;

	BUG_ON(vuart_bus_priv.use_count < 0);

	if (vuart_bus_priv.use_count != 0)
		return 0;

	free_irq(vuart_bus_priv.virq, &vuart_bus_priv);

	ps3_vuart_irq_destroy(vuart_bus_priv.virq);
	vuart_bus_priv.virq = NO_IRQ;

	kfree(vuart_bus_priv.bmp);
	vuart_bus_priv.bmp = NULL;

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return 0;
}

static int ps3_vuart_probe(struct ps3_system_bus_device *dev)
{
	int result;
	struct ps3_vuart_port_driver *drv;
	struct ps3_vuart_port_priv *priv = NULL;

	dev_dbg(&dev->core, "%s:%d\n", __func__, __LINE__);

	drv = ps3_system_bus_dev_to_vuart_drv(dev);
	BUG_ON(!drv);

	dev_dbg(&dev->core, "%s:%d: (%s)\n", __func__, __LINE__,
		drv->core.core.name);

	if (dev->port_number >= PORT_COUNT) {
		BUG();
		return -EINVAL;
	}

	mutex_lock(&vuart_bus_priv.probe_mutex);

	result = ps3_vuart_bus_interrupt_get();

	if (result)
		goto fail_setup_interrupt;

	if (vuart_bus_priv.devices[dev->port_number]) {
		dev_dbg(&dev->core, "%s:%d: port busy (%d)\n", __func__,
			__LINE__, dev->port_number);
		result = -EBUSY;
		goto fail_busy;
	}

	vuart_bus_priv.devices[dev->port_number] = dev;

	/* Setup dev->driver_priv. */

	dev->driver_priv = kzalloc(sizeof(struct ps3_vuart_port_priv),
		GFP_KERNEL);

	if (!dev->driver_priv) {
		result = -ENOMEM;
		goto fail_dev_malloc;
	}

	priv = to_port_priv(dev);

	INIT_LIST_HEAD(&priv->tx_list.head);
	spin_lock_init(&priv->tx_list.lock);

	INIT_LIST_HEAD(&priv->rx_list.head);
	spin_lock_init(&priv->rx_list.lock);

	INIT_WORK(&priv->rx_list.work.work, ps3_vuart_work);
	priv->rx_list.work.trigger = 0;
	priv->rx_list.work.dev = dev;

	/* clear stale pending interrupts */

	ps3_vuart_clear_rx_bytes(dev, 0);

	ps3_vuart_set_interrupt_mask(dev, INTERRUPT_MASK_RX);

	ps3_vuart_set_triggers(dev, 1, 1);

	if (drv->probe)
		result = drv->probe(dev);
	else {
		result = 0;
		dev_info(&dev->core, "%s:%d: no probe method\n", __func__,
			__LINE__);
	}

	if (result) {
		dev_dbg(&dev->core, "%s:%d: drv->probe failed\n",
			__func__, __LINE__);
		goto fail_probe;
	}

	mutex_unlock(&vuart_bus_priv.probe_mutex);

	return result;

fail_probe:
	ps3_vuart_set_interrupt_mask(dev, 0);
	kfree(dev->driver_priv);
	dev->driver_priv = NULL;
fail_dev_malloc:
	vuart_bus_priv.devices[dev->port_number] = NULL;
fail_busy:
	ps3_vuart_bus_interrupt_put();
fail_setup_interrupt:
	mutex_unlock(&vuart_bus_priv.probe_mutex);
	dev_dbg(&dev->core, "%s:%d: failed\n", __func__, __LINE__);
	return result;
}

/**
 * ps3_vuart_cleanup - common cleanup helper.
 * @dev: The struct ps3_system_bus_device instance.
 *
 * Cleans interrupts and HV resources.  Must be called with
 * vuart_bus_priv.probe_mutex held.  Used by ps3_vuart_remove and
 * ps3_vuart_shutdown.  After this call, polled reading will still work.
 */

static int ps3_vuart_cleanup(struct ps3_system_bus_device *dev)
{
	dev_dbg(&dev->core, "%s:%d\n", __func__, __LINE__);

	ps3_vuart_cancel_async(dev);
	ps3_vuart_set_interrupt_mask(dev, 0);
	ps3_vuart_bus_interrupt_put();
	return 0;
}

/**
 * ps3_vuart_remove - Completely clean the device instance.
 * @dev: The struct ps3_system_bus_device instance.
 *
 * Cleans all memory, interrupts and HV resources.  After this call the
 * device can no longer be used.
 */

static int ps3_vuart_remove(struct ps3_system_bus_device *dev)
{
	struct ps3_vuart_port_priv *priv = to_port_priv(dev);
	struct ps3_vuart_port_driver *drv;

	BUG_ON(!dev);

	mutex_lock(&vuart_bus_priv.probe_mutex);

	dev_dbg(&dev->core, " -> %s:%d: match_id %d\n", __func__, __LINE__,
		dev->match_id);

	if (!dev->core.driver) {
		dev_dbg(&dev->core, "%s:%d: no driver bound\n", __func__,
			__LINE__);
		mutex_unlock(&vuart_bus_priv.probe_mutex);
		return 0;
	}

	drv = ps3_system_bus_dev_to_vuart_drv(dev);

	BUG_ON(!drv);

	if (drv->remove) {
		drv->remove(dev);
	} else {
		dev_dbg(&dev->core, "%s:%d: no remove method\n", __func__,
		__LINE__);
		BUG();
	}

	ps3_vuart_cleanup(dev);

	vuart_bus_priv.devices[dev->port_number] = NULL;
	kfree(priv);
	priv = NULL;

	dev_dbg(&dev->core, " <- %s:%d\n", __func__, __LINE__);
	mutex_unlock(&vuart_bus_priv.probe_mutex);
	return 0;
}

/**
 * ps3_vuart_shutdown - Cleans interrupts and HV resources.
 * @dev: The struct ps3_system_bus_device instance.
 *
 * Cleans interrupts and HV resources.  After this call the
 * device can still be used in polling mode.  This behavior required
 * by sys-manager to be able to complete the device power operation
 * sequence.
 */

static int ps3_vuart_shutdown(struct ps3_system_bus_device *dev)
{
	struct ps3_vuart_port_driver *drv;

	BUG_ON(!dev);

	mutex_lock(&vuart_bus_priv.probe_mutex);

	dev_dbg(&dev->core, " -> %s:%d: match_id %d\n", __func__, __LINE__,
		dev->match_id);

	if (!dev->core.driver) {
		dev_dbg(&dev->core, "%s:%d: no driver bound\n", __func__,
			__LINE__);
		mutex_unlock(&vuart_bus_priv.probe_mutex);
		return 0;
	}

	drv = ps3_system_bus_dev_to_vuart_drv(dev);

	BUG_ON(!drv);

	if (drv->shutdown)
		drv->shutdown(dev);
	else if (drv->remove) {
		dev_dbg(&dev->core, "%s:%d: no shutdown, calling remove\n",
			__func__, __LINE__);
		drv->remove(dev);
	} else {
		dev_dbg(&dev->core, "%s:%d: no shutdown method\n", __func__,
			__LINE__);
		BUG();
	}

	ps3_vuart_cleanup(dev);

	dev_dbg(&dev->core, " <- %s:%d\n", __func__, __LINE__);

	mutex_unlock(&vuart_bus_priv.probe_mutex);
	return 0;
}

static int __init ps3_vuart_bus_init(void)
{
	pr_debug("%s:%d:\n", __func__, __LINE__);

	if (!firmware_has_feature(FW_FEATURE_PS3_LV1))
		return -ENODEV;

	mutex_init(&vuart_bus_priv.probe_mutex);

	return 0;
}

static void __exit ps3_vuart_bus_exit(void)
{
	pr_debug("%s:%d:\n", __func__, __LINE__);
}

core_initcall(ps3_vuart_bus_init);
module_exit(ps3_vuart_bus_exit);

/**
 * ps3_vuart_port_driver_register - Add a vuart port device driver.
 */

int ps3_vuart_port_driver_register(struct ps3_vuart_port_driver *drv)
{
	int result;

	pr_debug("%s:%d: (%s)\n", __func__, __LINE__, drv->core.core.name);

	BUG_ON(!drv->core.match_id);
	BUG_ON(!drv->core.core.name);

	drv->core.probe = ps3_vuart_probe;
	drv->core.remove = ps3_vuart_remove;
	drv->core.shutdown = ps3_vuart_shutdown;

	result = ps3_system_bus_driver_register(&drv->core);
	return result;
}
EXPORT_SYMBOL_GPL(ps3_vuart_port_driver_register);

/**
 * ps3_vuart_port_driver_unregister - Remove a vuart port device driver.
 */

void ps3_vuart_port_driver_unregister(struct ps3_vuart_port_driver *drv)
{
	pr_debug("%s:%d: (%s)\n", __func__, __LINE__, drv->core.core.name);
	ps3_system_bus_driver_unregister(&drv->core);
}
EXPORT_SYMBOL_GPL(ps3_vuart_port_driver_unregister);
