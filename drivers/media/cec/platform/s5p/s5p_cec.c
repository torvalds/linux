// SPDX-License-Identifier: GPL-2.0-or-later
/* drivers/media/platform/s5p-cec/s5p_cec.c
 *
 * Samsung S5P CEC driver
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *
 * This driver is based on the "cec interface driver for exynos soc" by
 * SangPil Moon.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <media/cec.h>
#include <media/cec-notifier.h>

#include "exynos_hdmi_cec.h"
#include "regs-cec.h"
#include "s5p_cec.h"

#define CEC_NAME	"s5p-cec"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

static int s5p_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	int ret;
	struct s5p_cec_dev *cec = cec_get_drvdata(adap);

	if (enable) {
		ret = pm_runtime_resume_and_get(cec->dev);
		if (ret < 0)
			return ret;

		s5p_cec_reset(cec);

		s5p_cec_set_divider(cec);
		s5p_cec_threshold(cec);

		s5p_cec_unmask_tx_interrupts(cec);
		s5p_cec_unmask_rx_interrupts(cec);
		s5p_cec_enable_rx(cec);
	} else {
		s5p_cec_mask_tx_interrupts(cec);
		s5p_cec_mask_rx_interrupts(cec);
		pm_runtime_put(cec->dev);
	}

	return 0;
}

static int s5p_cec_adap_log_addr(struct cec_adapter *adap, u8 addr)
{
	struct s5p_cec_dev *cec = cec_get_drvdata(adap);

	s5p_cec_set_addr(cec, addr);
	return 0;
}

static int s5p_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				 u32 signal_free_time, struct cec_msg *msg)
{
	struct s5p_cec_dev *cec = cec_get_drvdata(adap);

	/*
	 * Unclear if 0 retries are allowed by the hardware, so have 1 as
	 * the minimum.
	 */
	s5p_cec_copy_packet(cec, msg->msg, msg->len, max(1, attempts - 1));
	return 0;
}

static irqreturn_t s5p_cec_irq_handler(int irq, void *priv)
{
	struct s5p_cec_dev *cec = priv;
	u32 status = 0;

	status = s5p_cec_get_status(cec);

	dev_dbg(cec->dev, "irq received\n");

	if (status & CEC_STATUS_TX_DONE) {
		if (status & CEC_STATUS_TX_NACK) {
			dev_dbg(cec->dev, "CEC_STATUS_TX_NACK set\n");
			cec->tx = STATE_NACK;
		} else if (status & CEC_STATUS_TX_ERROR) {
			dev_dbg(cec->dev, "CEC_STATUS_TX_ERROR set\n");
			cec->tx = STATE_ERROR;
		} else {
			dev_dbg(cec->dev, "CEC_STATUS_TX_DONE\n");
			cec->tx = STATE_DONE;
		}
		s5p_clr_pending_tx(cec);
	}

	if (status & CEC_STATUS_RX_DONE) {
		if (status & CEC_STATUS_RX_ERROR) {
			dev_dbg(cec->dev, "CEC_STATUS_RX_ERROR set\n");
			s5p_cec_rx_reset(cec);
			s5p_cec_enable_rx(cec);
		} else {
			dev_dbg(cec->dev, "CEC_STATUS_RX_DONE set\n");
			if (cec->rx != STATE_IDLE)
				dev_dbg(cec->dev, "Buffer overrun (worker did not process previous message)\n");
			cec->rx = STATE_BUSY;
			cec->msg.len = status >> 24;
			cec->msg.rx_status = CEC_RX_STATUS_OK;
			s5p_cec_get_rx_buf(cec, cec->msg.len,
					cec->msg.msg);
			cec->rx = STATE_DONE;
			s5p_cec_enable_rx(cec);
		}
		/* Clear interrupt pending bit */
		s5p_clr_pending_rx(cec);
	}
	return IRQ_WAKE_THREAD;
}

static irqreturn_t s5p_cec_irq_handler_thread(int irq, void *priv)
{
	struct s5p_cec_dev *cec = priv;

	dev_dbg(cec->dev, "irq processing thread\n");
	switch (cec->tx) {
	case STATE_DONE:
		cec_transmit_done(cec->adap, CEC_TX_STATUS_OK, 0, 0, 0, 0);
		cec->tx = STATE_IDLE;
		break;
	case STATE_NACK:
		cec_transmit_done(cec->adap,
			CEC_TX_STATUS_MAX_RETRIES | CEC_TX_STATUS_NACK,
			0, 1, 0, 0);
		cec->tx = STATE_IDLE;
		break;
	case STATE_ERROR:
		cec_transmit_done(cec->adap,
			CEC_TX_STATUS_MAX_RETRIES | CEC_TX_STATUS_ERROR,
			0, 0, 0, 1);
		cec->tx = STATE_IDLE;
		break;
	case STATE_BUSY:
		dev_err(cec->dev, "state set to busy, this should not occur here\n");
		break;
	default:
		break;
	}

	switch (cec->rx) {
	case STATE_DONE:
		cec_received_msg(cec->adap, &cec->msg);
		cec->rx = STATE_IDLE;
		break;
	default:
		break;
	}

	return IRQ_HANDLED;
}

static const struct cec_adap_ops s5p_cec_adap_ops = {
	.adap_enable = s5p_cec_adap_enable,
	.adap_log_addr = s5p_cec_adap_log_addr,
	.adap_transmit = s5p_cec_adap_transmit,
};

static int s5p_cec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *hdmi_dev;
	struct resource *res;
	struct s5p_cec_dev *cec;
	bool needs_hpd = of_property_read_bool(pdev->dev.of_node, "needs-hpd");
	int ret;

	hdmi_dev = cec_notifier_parse_hdmi_phandle(dev);

	if (IS_ERR(hdmi_dev))
		return PTR_ERR(hdmi_dev);

	cec = devm_kzalloc(&pdev->dev, sizeof(*cec), GFP_KERNEL);
	if (!cec)
		return -ENOMEM;

	cec->dev = dev;

	cec->irq = platform_get_irq(pdev, 0);
	if (cec->irq < 0)
		return cec->irq;

	ret = devm_request_threaded_irq(dev, cec->irq, s5p_cec_irq_handler,
		s5p_cec_irq_handler_thread, 0, pdev->name, cec);
	if (ret)
		return ret;

	cec->clk = devm_clk_get(dev, "hdmicec");
	if (IS_ERR(cec->clk))
		return PTR_ERR(cec->clk);

	cec->pmu = syscon_regmap_lookup_by_phandle(dev->of_node,
						 "samsung,syscon-phandle");
	if (IS_ERR(cec->pmu))
		return -EPROBE_DEFER;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cec->reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(cec->reg))
		return PTR_ERR(cec->reg);

	cec->adap = cec_allocate_adapter(&s5p_cec_adap_ops, cec, CEC_NAME,
		CEC_CAP_DEFAULTS | (needs_hpd ? CEC_CAP_NEEDS_HPD : 0) |
		CEC_CAP_CONNECTOR_INFO, 1);
	ret = PTR_ERR_OR_ZERO(cec->adap);
	if (ret)
		return ret;

	cec->notifier = cec_notifier_cec_adap_register(hdmi_dev, NULL,
						       cec->adap);
	if (!cec->notifier) {
		ret = -ENOMEM;
		goto err_delete_adapter;
	}

	ret = cec_register_adapter(cec->adap, &pdev->dev);
	if (ret)
		goto err_notifier;

	platform_set_drvdata(pdev, cec);
	pm_runtime_enable(dev);

	dev_dbg(dev, "successfully probed\n");
	return 0;

err_notifier:
	cec_notifier_cec_adap_unregister(cec->notifier, cec->adap);

err_delete_adapter:
	cec_delete_adapter(cec->adap);
	return ret;
}

static int s5p_cec_remove(struct platform_device *pdev)
{
	struct s5p_cec_dev *cec = platform_get_drvdata(pdev);

	cec_notifier_cec_adap_unregister(cec->notifier, cec->adap);
	cec_unregister_adapter(cec->adap);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int __maybe_unused s5p_cec_runtime_suspend(struct device *dev)
{
	struct s5p_cec_dev *cec = dev_get_drvdata(dev);

	clk_disable_unprepare(cec->clk);
	return 0;
}

static int __maybe_unused s5p_cec_runtime_resume(struct device *dev)
{
	struct s5p_cec_dev *cec = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(cec->clk);
	if (ret < 0)
		return ret;
	return 0;
}

static const struct dev_pm_ops s5p_cec_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(s5p_cec_runtime_suspend, s5p_cec_runtime_resume,
			   NULL)
};

static const struct of_device_id s5p_cec_match[] = {
	{
		.compatible	= "samsung,s5p-cec",
	},
	{},
};
MODULE_DEVICE_TABLE(of, s5p_cec_match);

static struct platform_driver s5p_cec_pdrv = {
	.probe	= s5p_cec_probe,
	.remove	= s5p_cec_remove,
	.driver	= {
		.name		= CEC_NAME,
		.of_match_table	= s5p_cec_match,
		.pm		= &s5p_cec_pm_ops,
	},
};

module_platform_driver(s5p_cec_pdrv);

MODULE_AUTHOR("Kamil Debski <kamil@wypas.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Samsung S5P CEC driver");
