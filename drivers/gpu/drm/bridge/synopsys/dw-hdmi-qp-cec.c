// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author:
 *      Algea Cao <algea.cao@rock-chips.com>
 */
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <drm/drm_edid.h>
#include <drm/bridge/dw_hdmi.h>

#include <media/cec.h>
#include <media/cec-notifier.h>

#include "dw-hdmi-qp-cec.h"

enum {
	CEC_TX_CONTROL		= 0x1000,
	CEC_CTRL_CLEAR		= BIT(0),
	CEC_CTRL_START		= BIT(0),

	CEC_STAT_DONE		= BIT(0),
	CEC_STAT_NACK		= BIT(1),
	CEC_STAT_ARBLOST	= BIT(2),
	CEC_STAT_LINE_ERR	= BIT(3),
	CEC_STAT_RETRANS_FAIL	= BIT(4),
	CEC_STAT_DISCARD	= BIT(5),
	CEC_STAT_TX_BUSY	= BIT(8),
	CEC_STAT_RX_BUSY	= BIT(9),
	CEC_STAT_DRIVE_ERR	= BIT(10),
	CEC_STAT_EOM		= BIT(11),
	CEC_STAT_NOTIFY_ERR	= BIT(12),

	CEC_CONFIG		= 0x1008,
	CEC_ADDR		= 0x100c,
	CEC_TX_CNT		= 0x1020,
	CEC_RX_CNT		= 0x1040,
	CEC_TX_DATA3_0		= 0x1024,
	CEC_RX_DATA3_0		= 0x1044,
	CEC_LOCK_CONTROL	= 0x1054,

	CEC_INT_STATUS		= 0x4000,
	CEC_INT_MASK_N		= 0x4004,
	CEC_INT_CLEAR		= 0x4008,
};

struct dw_hdmi_qp_cec {
	struct dw_hdmi_qp *hdmi;
	const struct dw_hdmi_qp_cec_ops *ops;
	u32 addresses;
	struct cec_adapter *adap;
	struct cec_msg rx_msg;
	unsigned int tx_status;
	bool tx_done;
	bool rx_done;
	struct cec_notifier *notify;
	int irq;
};

static void dw_hdmi_qp_write(struct dw_hdmi_qp_cec *cec, u32 val, int offset)
{
	cec->ops->write(cec->hdmi, val, offset);
}

static u32 dw_hdmi_qp_read(struct dw_hdmi_qp_cec *cec, int offset)
{
	return cec->ops->read(cec->hdmi, offset);
}

static int dw_hdmi_qp_cec_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct dw_hdmi_qp_cec *cec = cec_get_drvdata(adap);

	if (logical_addr == CEC_LOG_ADDR_INVALID)
		cec->addresses = 0;
	else
		cec->addresses |= BIT(logical_addr) | BIT(15);

	dw_hdmi_qp_write(cec, cec->addresses, CEC_ADDR);

	return 0;
}

static int dw_hdmi_qp_cec_transmit(struct cec_adapter *adap, u8 attempts,
				   u32 signal_free_time, struct cec_msg *msg)
{
	struct dw_hdmi_qp_cec *cec = cec_get_drvdata(adap);
	unsigned int i;
	u32 val;

	for (i = 0; i < msg->len; i++) {
		if (!(i % 4))
			val = msg->msg[i];
		if ((i % 4) == 1)
			val |= msg->msg[i] << 8;
		if ((i % 4) == 2)
			val |= msg->msg[i] << 16;
		if ((i % 4) == 3)
			val |= msg->msg[i] << 24;

		if (i == (msg->len - 1) || (i % 4) == 3)
			dw_hdmi_qp_write(cec, val, CEC_TX_DATA3_0 + (i / 4) * 4);
	}

	dw_hdmi_qp_write(cec, msg->len - 1, CEC_TX_CNT);
	dw_hdmi_qp_write(cec, CEC_CTRL_START, CEC_TX_CONTROL);

	return 0;
}

static irqreturn_t dw_hdmi_qp_cec_hardirq(int irq, void *data)
{
	struct cec_adapter *adap = data;
	struct dw_hdmi_qp_cec *cec = cec_get_drvdata(adap);
	u32 stat = dw_hdmi_qp_read(cec, CEC_INT_STATUS);
	irqreturn_t ret = IRQ_HANDLED;

	if (stat == 0)
		return IRQ_NONE;

	dw_hdmi_qp_write(cec, stat, CEC_INT_CLEAR);

	if (stat & CEC_STAT_LINE_ERR) {
		cec->tx_status = CEC_TX_STATUS_ERROR;
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	} else if (stat & CEC_STAT_DONE) {
		cec->tx_status = CEC_TX_STATUS_OK;
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	} else if (stat & CEC_STAT_NACK) {
		cec->tx_status = CEC_TX_STATUS_NACK;
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	}

	if (stat & CEC_STAT_EOM) {
		unsigned int len, i, val;

		val = dw_hdmi_qp_read(cec, CEC_RX_CNT);
		len = (val & 0xf) + 1;

		if (len > sizeof(cec->rx_msg.msg))
			len = sizeof(cec->rx_msg.msg);

		for (i = 0; i < 4; i++) {
			val = dw_hdmi_qp_read(cec, CEC_RX_DATA3_0 + i);
			cec->rx_msg.msg[i * 4] = val & 0xff;
			cec->rx_msg.msg[i * 4 + 1] = (val >> 8) & 0xff;
			cec->rx_msg.msg[i * 4 + 2] = (val >> 16) & 0xff;
			cec->rx_msg.msg[i * 4 + 3] = (val >> 24) & 0xff;
		}

		dw_hdmi_qp_write(cec, 1, CEC_LOCK_CONTROL);

		cec->rx_msg.len = len;
		cec->rx_done = true;

		ret = IRQ_WAKE_THREAD;
	}

	return ret;
}

static irqreturn_t dw_hdmi_qp_cec_thread(int irq, void *data)
{
	struct cec_adapter *adap = data;
	struct dw_hdmi_qp_cec *cec = cec_get_drvdata(adap);

	if (cec->tx_done) {
		cec->tx_done = false;
		cec_transmit_attempt_done(adap, cec->tx_status);
	}
	if (cec->rx_done) {
		cec->rx_done = false;
		cec_received_msg(adap, &cec->rx_msg);
	}
	return IRQ_HANDLED;
}

static int dw_hdmi_qp_cec_enable(struct cec_adapter *adap, bool enable)
{
	struct dw_hdmi_qp_cec *cec = cec_get_drvdata(adap);

	if (!enable) {
		dw_hdmi_qp_write(cec, 0, CEC_INT_MASK_N);
		dw_hdmi_qp_write(cec, ~0, CEC_INT_CLEAR);
		cec->ops->disable(cec->hdmi);
	} else {
		unsigned int irqs;

		cec->ops->enable(cec->hdmi);

		dw_hdmi_qp_write(cec, ~0, CEC_INT_CLEAR);
		dw_hdmi_qp_write(cec, 1, CEC_LOCK_CONTROL);

		dw_hdmi_qp_cec_log_addr(cec->adap, CEC_LOG_ADDR_INVALID);

		irqs = CEC_STAT_LINE_ERR | CEC_STAT_NACK | CEC_STAT_EOM |
		       CEC_STAT_DONE;
		dw_hdmi_qp_write(cec, ~0, CEC_INT_CLEAR);
		dw_hdmi_qp_write(cec, irqs, CEC_INT_MASK_N);
	}
	return 0;
}

static const struct cec_adap_ops dw_hdmi_qp_cec_ops = {
	.adap_enable = dw_hdmi_qp_cec_enable,
	.adap_log_addr = dw_hdmi_qp_cec_log_addr,
	.adap_transmit = dw_hdmi_qp_cec_transmit,
};

static void dw_hdmi_qp_cec_del(void *data)
{
	struct dw_hdmi_qp_cec *cec = data;

	cec_delete_adapter(cec->adap);
}

static int dw_hdmi_qp_cec_probe(struct platform_device *pdev)
{
	struct dw_hdmi_qp_cec_data *data = dev_get_platdata(&pdev->dev);
	struct dw_hdmi_qp_cec *cec;
	int ret;

	if (!data) {
		dev_err(&pdev->dev, "can't get data\n");
		return -ENXIO;
	}

	/*
	 * Our device is just a convenience - we want to link to the real
	 * hardware device here, so that userspace can see the association
	 * between the HDMI hardware and its associated CEC chardev.
	 */
	cec = devm_kzalloc(&pdev->dev, sizeof(*cec), GFP_KERNEL);
	if (!cec)
		return -ENOMEM;

	cec->ops = data->ops;
	cec->hdmi = data->hdmi;
	cec->irq = data->irq;

	platform_set_drvdata(pdev, cec);

	dw_hdmi_qp_write(cec, 0, CEC_TX_CNT);
	dw_hdmi_qp_write(cec, ~0, CEC_INT_CLEAR);
	dw_hdmi_qp_write(cec, 0, CEC_INT_MASK_N);

	cec->adap = cec_allocate_adapter(&dw_hdmi_qp_cec_ops, cec, "dw_hdmi_qp",
					 CEC_CAP_LOG_ADDRS | CEC_CAP_TRANSMIT |
					 CEC_CAP_RC | CEC_CAP_PASSTHROUGH,
					 CEC_MAX_LOG_ADDRS);
	if (IS_ERR(cec->adap)) {
		dev_err(&pdev->dev, "cec allocate adapter failed\n");
		return PTR_ERR(cec->adap);
	}

	dw_hdmi_qp_set_cec_adap(cec->hdmi, cec->adap);

	/* override the module pointer */
	cec->adap->owner = THIS_MODULE;

	ret = devm_add_action(&pdev->dev, dw_hdmi_qp_cec_del, cec);
	if (ret) {
		dev_err(&pdev->dev, "cec add action failed\n");
		cec_delete_adapter(cec->adap);
		return ret;
	}

	if (cec->irq < 0) {
		ret = cec->irq;
		dev_err(&pdev->dev, "cec get irq failed\n");
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, cec->irq,
					dw_hdmi_qp_cec_hardirq,
					dw_hdmi_qp_cec_thread, IRQF_SHARED,
					"dw-hdmi-qp-cec", cec->adap);
	if (ret < 0) {
		dev_err(&pdev->dev, "cec request irq thread failed\n");
		return ret;
	}

	cec->notify = cec_notifier_cec_adap_register(pdev->dev.parent,
						     NULL, cec->adap);
	if (!cec->notify) {
		dev_err(&pdev->dev, "cec notifier adap register failed\n");
		return -ENOMEM;
	}

	ret = cec_register_adapter(cec->adap, pdev->dev.parent);
	if (ret < 0) {
		dev_err(&pdev->dev, "cec adap register failed\n");
		cec_notifier_cec_adap_unregister(cec->notify, cec->adap);
		return ret;
	}

	/*
	 * CEC documentation says we must not call cec_delete_adapter
	 * after a successful call to cec_register_adapter().
	 */
	devm_remove_action(&pdev->dev, dw_hdmi_qp_cec_del, cec);

	return 0;
}

static int dw_hdmi_qp_cec_remove(struct platform_device *pdev)
{
	struct dw_hdmi_qp_cec *cec = platform_get_drvdata(pdev);

	cec_notifier_cec_adap_unregister(cec->notify, cec->adap);
	cec_unregister_adapter(cec->adap);

	return 0;
}

static struct platform_driver dw_hdmi_qp_cec_driver = {
	.probe	= dw_hdmi_qp_cec_probe,
	.remove	= dw_hdmi_qp_cec_remove,
	.driver = {
		.name = "dw-hdmi-qp-cec",
	},
};
module_platform_driver(dw_hdmi_qp_cec_driver);

MODULE_AUTHOR("Algea Cao <algea.cao@rock-chips.com>");
MODULE_DESCRIPTION("Synopsys Designware HDMI QP CEC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS(PLATFORM_MODULE_PREFIX "dw-hdmi-qp-cec");
