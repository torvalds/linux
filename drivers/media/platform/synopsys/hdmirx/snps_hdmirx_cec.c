// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen <csq@rock-chips.com>
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/cec.h>

#include "snps_hdmirx.h"
#include "snps_hdmirx_cec.h"

static void hdmirx_cec_write(struct hdmirx_cec *cec, int reg, u32 val)
{
	cec->ops->write(cec->hdmirx, reg, val);
}

static u32 hdmirx_cec_read(struct hdmirx_cec *cec, int reg)
{
	return cec->ops->read(cec->hdmirx, reg);
}

static void hdmirx_cec_update_bits(struct hdmirx_cec *cec, int reg, u32 mask,
				   u32 data)
{
	u32 val = hdmirx_cec_read(cec, reg) & ~mask;

	val |= (data & mask);
	hdmirx_cec_write(cec, reg, val);
}

static int hdmirx_cec_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct hdmirx_cec *cec = cec_get_drvdata(adap);

	if (logical_addr == CEC_LOG_ADDR_INVALID)
		cec->addresses = 0;
	else
		cec->addresses |= BIT(logical_addr) | BIT(15);

	hdmirx_cec_write(cec, CEC_ADDR, cec->addresses);

	return 0;
}

/* signal_free_time is handled by the Synopsys Designware
 * HDMIRX Controller hardware.
 */
static int hdmirx_cec_transmit(struct cec_adapter *adap, u8 attempts,
			       u32 signal_free_time, struct cec_msg *msg)
{
	struct hdmirx_cec *cec = cec_get_drvdata(adap);
	u32 data[4] = {0};
	int i, data_len, msg_len;

	msg_len = msg->len;

	hdmirx_cec_write(cec, CEC_TX_COUNT, msg_len - 1);
	for (i = 0; i < msg_len; i++)
		data[i / 4] |= msg->msg[i] << (i % 4) * 8;

	data_len = DIV_ROUND_UP(msg_len, 4);

	for (i = 0; i < data_len; i++)
		hdmirx_cec_write(cec, CEC_TX_DATA3_0 + i * 4, data[i]);

	hdmirx_cec_write(cec, CEC_TX_CONTROL, 0x1);

	return 0;
}

static irqreturn_t hdmirx_cec_hardirq(int irq, void *data)
{
	struct cec_adapter *adap = data;
	struct hdmirx_cec *cec = cec_get_drvdata(adap);
	u32 stat = hdmirx_cec_read(cec, CEC_INT_STATUS);
	irqreturn_t ret = IRQ_HANDLED;
	u32 val;

	if (!stat)
		return IRQ_NONE;

	hdmirx_cec_write(cec, CEC_INT_CLEAR, stat);

	if (stat & CECTX_LINE_ERR) {
		cec->tx_status = CEC_TX_STATUS_ERROR;
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	} else if (stat & CECTX_DONE) {
		cec->tx_status = CEC_TX_STATUS_OK;
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	} else if (stat & CECTX_NACK) {
		cec->tx_status = CEC_TX_STATUS_NACK;
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	} else if (stat & CECTX_ARBLOST) {
		cec->tx_status = CEC_TX_STATUS_ARB_LOST;
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	}

	if (stat & CECRX_EOM) {
		unsigned int len, i;

		val = hdmirx_cec_read(cec, CEC_RX_COUNT_STATUS);
		/* rxbuffer locked status */
		if ((val & 0x80))
			return ret;

		len = (val & 0xf) + 1;
		if (len > sizeof(cec->rx_msg.msg))
			len = sizeof(cec->rx_msg.msg);

		for (i = 0; i < len; i++) {
			if (!(i % 4))
				val = hdmirx_cec_read(cec, CEC_RX_DATA3_0 + i / 4 * 4);
			cec->rx_msg.msg[i] = (val >> ((i % 4) * 8)) & 0xff;
		}

		cec->rx_msg.len = len;
		smp_wmb(); /* receive RX msg */
		cec->rx_done = true;
		hdmirx_cec_write(cec, CEC_LOCK_CONTROL, 0x1);

		ret = IRQ_WAKE_THREAD;
	}

	return ret;
}

static irqreturn_t hdmirx_cec_thread(int irq, void *data)
{
	struct cec_adapter *adap = data;
	struct hdmirx_cec *cec = cec_get_drvdata(adap);

	if (cec->tx_done) {
		cec->tx_done = false;
		cec_transmit_attempt_done(adap, cec->tx_status);
	}
	if (cec->rx_done) {
		cec->rx_done = false;
		smp_rmb(); /* RX msg has been received */
		cec_received_msg(adap, &cec->rx_msg);
	}

	return IRQ_HANDLED;
}

static int hdmirx_cec_enable(struct cec_adapter *adap, bool enable)
{
	struct hdmirx_cec *cec = cec_get_drvdata(adap);

	if (!enable) {
		hdmirx_cec_write(cec, CEC_INT_MASK_N, 0);
		hdmirx_cec_write(cec, CEC_INT_CLEAR, 0);
		if (cec->ops->disable)
			cec->ops->disable(cec->hdmirx);
	} else {
		unsigned int irqs;

		hdmirx_cec_log_addr(cec->adap, CEC_LOG_ADDR_INVALID);
		if (cec->ops->enable)
			cec->ops->enable(cec->hdmirx);
		hdmirx_cec_update_bits(cec, GLOBAL_SWENABLE, CEC_ENABLE, CEC_ENABLE);

		irqs = CECTX_LINE_ERR | CECTX_NACK | CECRX_EOM | CECTX_DONE;
		hdmirx_cec_write(cec, CEC_INT_MASK_N, irqs);
	}

	return 0;
}

static const struct cec_adap_ops hdmirx_cec_ops = {
	.adap_enable = hdmirx_cec_enable,
	.adap_log_addr = hdmirx_cec_log_addr,
	.adap_transmit = hdmirx_cec_transmit,
};

static void hdmirx_cec_del(void *data)
{
	struct hdmirx_cec *cec = data;

	cec_delete_adapter(cec->adap);
}

struct hdmirx_cec *snps_hdmirx_cec_register(struct hdmirx_cec_data *data)
{
	struct hdmirx_cec *cec;
	unsigned int irqs;
	int ret;

	/*
	 * Our device is just a convenience - we want to link to the real
	 * hardware device here, so that userspace can see the association
	 * between the HDMI hardware and its associated CEC chardev.
	 */
	cec = devm_kzalloc(data->dev, sizeof(*cec), GFP_KERNEL);
	if (!cec)
		return ERR_PTR(-ENOMEM);

	cec->dev = data->dev;
	cec->irq = data->irq;
	cec->ops = data->ops;
	cec->hdmirx = data->hdmirx;

	hdmirx_cec_update_bits(cec, GLOBAL_SWENABLE, CEC_ENABLE, CEC_ENABLE);
	hdmirx_cec_update_bits(cec, CEC_CONFIG, RX_AUTO_DRIVE_ACKNOWLEDGE,
			       RX_AUTO_DRIVE_ACKNOWLEDGE);

	hdmirx_cec_write(cec, CEC_TX_COUNT, 0);
	hdmirx_cec_write(cec, CEC_INT_MASK_N, 0);
	hdmirx_cec_write(cec, CEC_INT_CLEAR, ~0);

	cec->adap = cec_allocate_adapter(&hdmirx_cec_ops, cec, "snps-hdmirx",
					 CEC_CAP_DEFAULTS | CEC_CAP_MONITOR_ALL,
					 CEC_MAX_LOG_ADDRS);
	if (IS_ERR(cec->adap)) {
		dev_err(cec->dev, "cec adapter allocation failed\n");
		return ERR_CAST(cec->adap);
	}

	/* override the module pointer */
	cec->adap->owner = THIS_MODULE;

	ret = devm_add_action(cec->dev, hdmirx_cec_del, cec);
	if (ret) {
		cec_delete_adapter(cec->adap);
		return ERR_PTR(ret);
	}

	irq_set_status_flags(cec->irq, IRQ_NOAUTOEN);

	ret = devm_request_threaded_irq(cec->dev, cec->irq,
					hdmirx_cec_hardirq,
					hdmirx_cec_thread, IRQF_ONESHOT,
					"rk_hdmirx_cec", cec->adap);
	if (ret) {
		dev_err(cec->dev, "cec irq request failed\n");
		return ERR_PTR(ret);
	}

	ret = cec_register_adapter(cec->adap, cec->dev);
	if (ret < 0) {
		dev_err(cec->dev, "cec adapter registration failed\n");
		return ERR_PTR(ret);
	}

	irqs = CECTX_LINE_ERR | CECTX_NACK | CECRX_EOM | CECTX_DONE;
	hdmirx_cec_write(cec, CEC_INT_MASK_N, irqs);

	/*
	 * CEC documentation says we must not call cec_delete_adapter
	 * after a successful call to cec_register_adapter().
	 */
	devm_remove_action(cec->dev, hdmirx_cec_del, cec);

	enable_irq(cec->irq);

	return cec;
}

void snps_hdmirx_cec_unregister(struct hdmirx_cec *cec)
{
	disable_irq(cec->irq);

	cec_unregister_adapter(cec->adap);
}
