// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Intel Corporation.
 * Intel Visual Sensing Controller Transport Layer Linux driver
 */

#include <linux/acpi.h>
#include <linux/cleanup.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/irqreturn.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "vsc-tp.h"

#define VSC_TP_RESET_PIN_TOGGLE_INTERVAL_MS	20
#define VSC_TP_ROM_BOOTUP_DELAY_MS		10
#define VSC_TP_ROM_XFER_POLL_TIMEOUT_US		(500 * USEC_PER_MSEC)
#define VSC_TP_ROM_XFER_POLL_DELAY_US		(20 * USEC_PER_MSEC)
#define VSC_TP_WAIT_FW_POLL_TIMEOUT		(2 * HZ)
#define VSC_TP_WAIT_FW_POLL_DELAY_US		(20 * USEC_PER_MSEC)
#define VSC_TP_MAX_XFER_COUNT			5

#define VSC_TP_PACKET_SYNC			0x31
#define VSC_TP_CRC_SIZE				sizeof(u32)
#define VSC_TP_MAX_MSG_SIZE			2048
/* SPI xfer timeout size */
#define VSC_TP_XFER_TIMEOUT_BYTES		700
#define VSC_TP_PACKET_PADDING_SIZE		1
#define VSC_TP_PACKET_SIZE(pkt) \
	(sizeof(struct vsc_tp_packet) + le16_to_cpu((pkt)->len) + VSC_TP_CRC_SIZE)
#define VSC_TP_MAX_PACKET_SIZE \
	(sizeof(struct vsc_tp_packet) + VSC_TP_MAX_MSG_SIZE + VSC_TP_CRC_SIZE)
#define VSC_TP_MAX_XFER_SIZE \
	(VSC_TP_MAX_PACKET_SIZE + VSC_TP_XFER_TIMEOUT_BYTES)
#define VSC_TP_NEXT_XFER_LEN(len, offset) \
	(len + sizeof(struct vsc_tp_packet) + VSC_TP_CRC_SIZE - offset + VSC_TP_PACKET_PADDING_SIZE)

struct vsc_tp_packet {
	__u8 sync;
	__u8 cmd;
	__le16 len;
	__le32 seq;
	__u8 buf[] __counted_by(len);
};

struct vsc_tp {
	/* do the actual data transfer */
	struct spi_device *spi;

	/* bind with mei framework */
	struct platform_device *pdev;

	struct gpio_desc *wakeuphost;
	struct gpio_desc *resetfw;
	struct gpio_desc *wakeupfw;

	/* command sequence number */
	u32 seq;

	/* command buffer */
	void *tx_buf;
	void *rx_buf;

	atomic_t assert_cnt;
	wait_queue_head_t xfer_wait;

	vsc_tp_event_cb_t event_notify;
	void *event_notify_context;

	/* used to protect command download */
	struct mutex mutex;
};

/* GPIO resources */
static const struct acpi_gpio_params wakeuphost_gpio = { 0, 0, false };
static const struct acpi_gpio_params wakeuphostint_gpio = { 1, 0, false };
static const struct acpi_gpio_params resetfw_gpio = { 2, 0, false };
static const struct acpi_gpio_params wakeupfw = { 3, 0, false };

static const struct acpi_gpio_mapping vsc_tp_acpi_gpios[] = {
	{ "wakeuphost-gpios", &wakeuphost_gpio, 1 },
	{ "wakeuphostint-gpios", &wakeuphostint_gpio, 1 },
	{ "resetfw-gpios", &resetfw_gpio, 1 },
	{ "wakeupfw-gpios", &wakeupfw, 1 },
	{}
};

/* wakeup firmware and wait for response */
static int vsc_tp_wakeup_request(struct vsc_tp *tp)
{
	int ret;

	gpiod_set_value_cansleep(tp->wakeupfw, 0);

	ret = wait_event_timeout(tp->xfer_wait,
				 atomic_read(&tp->assert_cnt),
				 VSC_TP_WAIT_FW_POLL_TIMEOUT);
	if (!ret)
		return -ETIMEDOUT;

	return read_poll_timeout(gpiod_get_value_cansleep, ret, ret,
				 VSC_TP_WAIT_FW_POLL_DELAY_US,
				 VSC_TP_WAIT_FW_POLL_TIMEOUT, false,
				 tp->wakeuphost);
}

static void vsc_tp_wakeup_release(struct vsc_tp *tp)
{
	atomic_dec_if_positive(&tp->assert_cnt);

	gpiod_set_value_cansleep(tp->wakeupfw, 1);
}

static int vsc_tp_dev_xfer(struct vsc_tp *tp, void *obuf, void *ibuf, size_t len)
{
	struct spi_message msg = { 0 };
	struct spi_transfer xfer = {
		.tx_buf = obuf,
		.rx_buf = ibuf,
		.len = len,
	};

	spi_message_init_with_transfers(&msg, &xfer, 1);

	return spi_sync_locked(tp->spi, &msg);
}

static int vsc_tp_xfer_helper(struct vsc_tp *tp, struct vsc_tp_packet *pkt,
			      void *ibuf, u16 ilen)
{
	int ret, offset = 0, cpy_len, src_len, dst_len = sizeof(struct vsc_tp_packet);
	int next_xfer_len = VSC_TP_PACKET_SIZE(pkt) + VSC_TP_XFER_TIMEOUT_BYTES;
	u8 *src, *crc_src, *rx_buf = tp->rx_buf;
	int count_down = VSC_TP_MAX_XFER_COUNT;
	u32 recv_crc = 0, crc = ~0;
	struct vsc_tp_packet ack;
	u8 *dst = (u8 *)&ack;
	bool synced = false;

	do {
		ret = vsc_tp_dev_xfer(tp, pkt, rx_buf, next_xfer_len);
		if (ret)
			return ret;
		memset(pkt, 0, VSC_TP_MAX_XFER_SIZE);

		if (synced) {
			src = rx_buf;
			src_len = next_xfer_len;
		} else {
			src = memchr(rx_buf, VSC_TP_PACKET_SYNC, next_xfer_len);
			if (!src)
				continue;
			synced = true;
			src_len = next_xfer_len - (src - rx_buf);
		}

		/* traverse received data */
		while (src_len > 0) {
			cpy_len = min(src_len, dst_len);
			memcpy(dst, src, cpy_len);
			crc_src = src;
			src += cpy_len;
			src_len -= cpy_len;
			dst += cpy_len;
			dst_len -= cpy_len;

			if (offset < sizeof(ack)) {
				offset += cpy_len;
				crc = crc32(crc, crc_src, cpy_len);

				if (!src_len)
					continue;

				if (le16_to_cpu(ack.len)) {
					dst = ibuf;
					dst_len = min(ilen, le16_to_cpu(ack.len));
				} else {
					dst = (u8 *)&recv_crc;
					dst_len = sizeof(recv_crc);
				}
			} else if (offset < sizeof(ack) + le16_to_cpu(ack.len)) {
				offset += cpy_len;
				crc = crc32(crc, crc_src, cpy_len);

				if (src_len) {
					int remain = sizeof(ack) + le16_to_cpu(ack.len) - offset;

					cpy_len = min(src_len, remain);
					offset += cpy_len;
					crc = crc32(crc, src, cpy_len);
					src += cpy_len;
					src_len -= cpy_len;
					if (src_len) {
						dst = (u8 *)&recv_crc;
						dst_len = sizeof(recv_crc);
						continue;
					}
				}
				next_xfer_len = VSC_TP_NEXT_XFER_LEN(le16_to_cpu(ack.len), offset);
			} else if (offset < sizeof(ack) + le16_to_cpu(ack.len) + VSC_TP_CRC_SIZE) {
				offset += cpy_len;

				if (src_len) {
					/* terminate the traverse */
					next_xfer_len = 0;
					break;
				}
				next_xfer_len = VSC_TP_NEXT_XFER_LEN(le16_to_cpu(ack.len), offset);
			}
		}
	} while (next_xfer_len > 0 && --count_down);

	if (next_xfer_len > 0)
		return -EAGAIN;

	if (~recv_crc != crc || le32_to_cpu(ack.seq) != tp->seq) {
		dev_err(&tp->spi->dev, "recv crc or seq error\n");
		return -EINVAL;
	}

	if (ack.cmd == VSC_TP_CMD_ACK || ack.cmd == VSC_TP_CMD_NACK ||
	    ack.cmd == VSC_TP_CMD_BUSY) {
		dev_err(&tp->spi->dev, "recv cmd ack error\n");
		return -EAGAIN;
	}

	return min(le16_to_cpu(ack.len), ilen);
}

/**
 * vsc_tp_xfer - transfer data to firmware
 * @tp: vsc_tp device handle
 * @cmd: the command to be sent to the device
 * @obuf: the tx buffer to be sent to the device
 * @olen: the length of tx buffer
 * @ibuf: the rx buffer to receive from the device
 * @ilen: the length of rx buffer
 * Return: the length of received data in case of success,
 *	otherwise negative value
 */
int vsc_tp_xfer(struct vsc_tp *tp, u8 cmd, const void *obuf, size_t olen,
		void *ibuf, size_t ilen)
{
	struct vsc_tp_packet *pkt = tp->tx_buf;
	u32 crc;
	int ret;

	if (!obuf || !ibuf || olen > VSC_TP_MAX_MSG_SIZE)
		return -EINVAL;

	guard(mutex)(&tp->mutex);

	pkt->sync = VSC_TP_PACKET_SYNC;
	pkt->cmd = cmd;
	pkt->len = cpu_to_le16(olen);
	pkt->seq = cpu_to_le32(++tp->seq);
	memcpy(pkt->buf, obuf, olen);

	crc = ~crc32(~0, (u8 *)pkt, sizeof(pkt) + olen);
	memcpy(pkt->buf + olen, &crc, sizeof(crc));

	ret = vsc_tp_wakeup_request(tp);
	if (unlikely(ret))
		dev_err(&tp->spi->dev, "wakeup firmware failed ret: %d\n", ret);
	else
		ret = vsc_tp_xfer_helper(tp, pkt, ibuf, ilen);

	vsc_tp_wakeup_release(tp);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(vsc_tp_xfer, VSC_TP);

/**
 * vsc_tp_rom_xfer - transfer data to rom code
 * @tp: vsc_tp device handle
 * @obuf: the data buffer to be sent to the device
 * @ibuf: the buffer to receive data from the device
 * @len: the length of tx buffer and rx buffer
 * Return: 0 in case of success, negative value in case of error
 */
int vsc_tp_rom_xfer(struct vsc_tp *tp, const void *obuf, void *ibuf, size_t len)
{
	size_t words = len / sizeof(__be32);
	int ret;

	if (len % sizeof(__be32) || len > VSC_TP_MAX_MSG_SIZE)
		return -EINVAL;

	guard(mutex)(&tp->mutex);

	/* rom xfer is big endian */
	cpu_to_be32_array(tp->tx_buf, obuf, words);

	ret = read_poll_timeout(gpiod_get_value_cansleep, ret,
				!ret, VSC_TP_ROM_XFER_POLL_DELAY_US,
				VSC_TP_ROM_XFER_POLL_TIMEOUT_US, false,
				tp->wakeuphost);
	if (ret) {
		dev_err(&tp->spi->dev, "wait rom failed ret: %d\n", ret);
		return ret;
	}

	ret = vsc_tp_dev_xfer(tp, tp->tx_buf, tp->rx_buf, len);
	if (ret)
		return ret;

	if (ibuf)
		cpu_to_be32_array(ibuf, tp->rx_buf, words);

	return ret;
}

/**
 * vsc_tp_reset - reset vsc transport layer
 * @tp: vsc_tp device handle
 */
void vsc_tp_reset(struct vsc_tp *tp)
{
	disable_irq(tp->spi->irq);

	/* toggle reset pin */
	gpiod_set_value_cansleep(tp->resetfw, 0);
	msleep(VSC_TP_RESET_PIN_TOGGLE_INTERVAL_MS);
	gpiod_set_value_cansleep(tp->resetfw, 1);

	/* wait for ROM */
	msleep(VSC_TP_ROM_BOOTUP_DELAY_MS);

	/*
	 * Set default host wakeup pin to non-active
	 * to avoid unexpected host irq interrupt.
	 */
	gpiod_set_value_cansleep(tp->wakeupfw, 1);

	atomic_set(&tp->assert_cnt, 0);

	enable_irq(tp->spi->irq);
}
EXPORT_SYMBOL_NS_GPL(vsc_tp_reset, VSC_TP);

/**
 * vsc_tp_need_read - check if device has data to sent
 * @tp: vsc_tp device handle
 * Return: true if device has data to sent, otherwise false
 */
bool vsc_tp_need_read(struct vsc_tp *tp)
{
	if (!atomic_read(&tp->assert_cnt))
		return false;
	if (!gpiod_get_value_cansleep(tp->wakeuphost))
		return false;
	if (!gpiod_get_value_cansleep(tp->wakeupfw))
		return false;

	return true;
}
EXPORT_SYMBOL_NS_GPL(vsc_tp_need_read, VSC_TP);

/**
 * vsc_tp_register_event_cb - register a callback function to receive event
 * @tp: vsc_tp device handle
 * @event_cb: callback function
 * @context: execution context of event callback
 * Return: 0 in case of success, negative value in case of error
 */
int vsc_tp_register_event_cb(struct vsc_tp *tp, vsc_tp_event_cb_t event_cb,
			    void *context)
{
	tp->event_notify = event_cb;
	tp->event_notify_context = context;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(vsc_tp_register_event_cb, VSC_TP);

/**
 * vsc_tp_intr_synchronize - synchronize vsc_tp interrupt
 * @tp: vsc_tp device handle
 */
void vsc_tp_intr_synchronize(struct vsc_tp *tp)
{
	synchronize_irq(tp->spi->irq);
}
EXPORT_SYMBOL_NS_GPL(vsc_tp_intr_synchronize, VSC_TP);

/**
 * vsc_tp_intr_enable - enable vsc_tp interrupt
 * @tp: vsc_tp device handle
 */
void vsc_tp_intr_enable(struct vsc_tp *tp)
{
	enable_irq(tp->spi->irq);
}
EXPORT_SYMBOL_NS_GPL(vsc_tp_intr_enable, VSC_TP);

/**
 * vsc_tp_intr_disable - disable vsc_tp interrupt
 * @tp: vsc_tp device handle
 */
void vsc_tp_intr_disable(struct vsc_tp *tp)
{
	disable_irq(tp->spi->irq);
}
EXPORT_SYMBOL_NS_GPL(vsc_tp_intr_disable, VSC_TP);

static irqreturn_t vsc_tp_isr(int irq, void *data)
{
	struct vsc_tp *tp = data;

	atomic_inc(&tp->assert_cnt);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t vsc_tp_thread_isr(int irq, void *data)
{
	struct vsc_tp *tp = data;

	wake_up(&tp->xfer_wait);

	if (tp->event_notify)
		tp->event_notify(tp->event_notify_context);

	return IRQ_HANDLED;
}

static int vsc_tp_match_any(struct acpi_device *adev, void *data)
{
	struct acpi_device **__adev = data;

	*__adev = adev;

	return 1;
}

static int vsc_tp_probe(struct spi_device *spi)
{
	struct vsc_tp *tp;
	struct platform_device_info pinfo = {
		.name = "intel_vsc",
		.data = &tp,
		.size_data = sizeof(tp),
		.id = PLATFORM_DEVID_NONE,
	};
	struct device *dev = &spi->dev;
	struct platform_device *pdev;
	struct acpi_device *adev;
	int ret;

	tp = devm_kzalloc(dev, sizeof(*tp), GFP_KERNEL);
	if (!tp)
		return -ENOMEM;

	tp->tx_buf = devm_kzalloc(dev, VSC_TP_MAX_XFER_SIZE, GFP_KERNEL);
	if (!tp->tx_buf)
		return -ENOMEM;

	tp->rx_buf = devm_kzalloc(dev, VSC_TP_MAX_XFER_SIZE, GFP_KERNEL);
	if (!tp->rx_buf)
		return -ENOMEM;

	ret = devm_acpi_dev_add_driver_gpios(dev, vsc_tp_acpi_gpios);
	if (ret)
		return ret;

	tp->wakeuphost = devm_gpiod_get(dev, "wakeuphost", GPIOD_IN);
	if (IS_ERR(tp->wakeuphost))
		return PTR_ERR(tp->wakeuphost);

	tp->resetfw = devm_gpiod_get(dev, "resetfw", GPIOD_OUT_HIGH);
	if (IS_ERR(tp->resetfw))
		return PTR_ERR(tp->resetfw);

	tp->wakeupfw = devm_gpiod_get(dev, "wakeupfw", GPIOD_OUT_HIGH);
	if (IS_ERR(tp->wakeupfw))
		return PTR_ERR(tp->wakeupfw);

	atomic_set(&tp->assert_cnt, 0);
	init_waitqueue_head(&tp->xfer_wait);
	tp->spi = spi;

	irq_set_status_flags(spi->irq, IRQ_DISABLE_UNLAZY);
	ret = devm_request_threaded_irq(dev, spi->irq, vsc_tp_isr,
					vsc_tp_thread_isr,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					dev_name(dev), tp);
	if (ret)
		return ret;

	mutex_init(&tp->mutex);

	/* only one child acpi device */
	ret = acpi_dev_for_each_child(ACPI_COMPANION(dev),
				      vsc_tp_match_any, &adev);
	if (!ret) {
		ret = -ENODEV;
		goto err_destroy_lock;
	}

	pinfo.fwnode = acpi_fwnode_handle(adev);
	pdev = platform_device_register_full(&pinfo);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		goto err_destroy_lock;
	}

	tp->pdev = pdev;
	spi_set_drvdata(spi, tp);

	return 0;

err_destroy_lock:
	mutex_destroy(&tp->mutex);

	return ret;
}

static void vsc_tp_remove(struct spi_device *spi)
{
	struct vsc_tp *tp = spi_get_drvdata(spi);

	platform_device_unregister(tp->pdev);

	mutex_destroy(&tp->mutex);
}

static const struct acpi_device_id vsc_tp_acpi_ids[] = {
	{ "INTC1009" }, /* Raptor Lake */
	{ "INTC1058" }, /* Tiger Lake */
	{ "INTC1094" }, /* Alder Lake */
	{ "INTC10D0" }, /* Meteor Lake */
	{}
};
MODULE_DEVICE_TABLE(acpi, vsc_tp_acpi_ids);

static struct spi_driver vsc_tp_driver = {
	.probe = vsc_tp_probe,
	.remove = vsc_tp_remove,
	.driver = {
		.name = "vsc-tp",
		.acpi_match_table = vsc_tp_acpi_ids,
	},
};
module_spi_driver(vsc_tp_driver);

MODULE_AUTHOR("Wentong Wu <wentong.wu@intel.com>");
MODULE_AUTHOR("Zhifeng Wang <zhifeng.wang@intel.com>");
MODULE_DESCRIPTION("Intel Visual Sensing Controller Transport Layer");
MODULE_LICENSE("GPL");
