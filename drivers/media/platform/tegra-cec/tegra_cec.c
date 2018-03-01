/*
 * Tegra CEC implementation
 *
 * The original 3.10 CEC driver using a custom API:
 *
 * Copyright (c) 2012-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * Conversion to the CEC framework and to the mainline kernel:
 *
 * Copyright 2016-2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/clk/tegra.h>

#include <media/cec-notifier.h>

#include "tegra_cec.h"

#define TEGRA_CEC_NAME "tegra-cec"

struct tegra_cec {
	struct cec_adapter	*adap;
	struct device		*dev;
	struct clk		*clk;
	void __iomem		*cec_base;
	struct cec_notifier	*notifier;
	int			tegra_cec_irq;
	bool			rx_done;
	bool			tx_done;
	int			tx_status;
	u8			rx_buf[CEC_MAX_MSG_SIZE];
	u8			rx_buf_cnt;
	u32			tx_buf[CEC_MAX_MSG_SIZE];
	u8			tx_buf_cur;
	u8			tx_buf_cnt;
};

static inline u32 cec_read(struct tegra_cec *cec, u32 reg)
{
	return readl(cec->cec_base + reg);
}

static inline void cec_write(struct tegra_cec *cec, u32 reg, u32 val)
{
	writel(val, cec->cec_base + reg);
}

static void tegra_cec_error_recovery(struct tegra_cec *cec)
{
	u32 hw_ctrl;

	hw_ctrl = cec_read(cec, TEGRA_CEC_HW_CONTROL);
	cec_write(cec, TEGRA_CEC_HW_CONTROL, 0);
	cec_write(cec, TEGRA_CEC_INT_STAT, 0xffffffff);
	cec_write(cec, TEGRA_CEC_HW_CONTROL, hw_ctrl);
}

static irqreturn_t tegra_cec_irq_thread_handler(int irq, void *data)
{
	struct device *dev = data;
	struct tegra_cec *cec = dev_get_drvdata(dev);

	if (cec->tx_done) {
		cec_transmit_attempt_done(cec->adap, cec->tx_status);
		cec->tx_done = false;
	}
	if (cec->rx_done) {
		struct cec_msg msg = {};

		msg.len = cec->rx_buf_cnt;
		memcpy(msg.msg, cec->rx_buf, msg.len);
		cec_received_msg(cec->adap, &msg);
		cec->rx_done = false;
		cec->rx_buf_cnt = 0;
	}
	return IRQ_HANDLED;
}

static irqreturn_t tegra_cec_irq_handler(int irq, void *data)
{
	struct device *dev = data;
	struct tegra_cec *cec = dev_get_drvdata(dev);
	u32 status, mask;

	status = cec_read(cec, TEGRA_CEC_INT_STAT);
	mask = cec_read(cec, TEGRA_CEC_INT_MASK);

	status &= mask;

	if (!status)
		return IRQ_HANDLED;

	if (status & TEGRA_CEC_INT_STAT_TX_REGISTER_UNDERRUN) {
		dev_err(dev, "TX underrun, interrupt timing issue!\n");

		tegra_cec_error_recovery(cec);
		cec_write(cec, TEGRA_CEC_INT_MASK,
			  mask & ~TEGRA_CEC_INT_MASK_TX_REGISTER_EMPTY);

		cec->tx_done = true;
		cec->tx_status = CEC_TX_STATUS_ERROR;
		return IRQ_WAKE_THREAD;
	}

	if ((status & TEGRA_CEC_INT_STAT_TX_ARBITRATION_FAILED) ||
		   (status & TEGRA_CEC_INT_STAT_TX_BUS_ANOMALY_DETECTED)) {
		tegra_cec_error_recovery(cec);
		cec_write(cec, TEGRA_CEC_INT_MASK,
			  mask & ~TEGRA_CEC_INT_MASK_TX_REGISTER_EMPTY);

		cec->tx_done = true;
		if (status & TEGRA_CEC_INT_STAT_TX_BUS_ANOMALY_DETECTED)
			cec->tx_status = CEC_TX_STATUS_LOW_DRIVE;
		else
			cec->tx_status = CEC_TX_STATUS_ARB_LOST;
		return IRQ_WAKE_THREAD;
	}

	if (status & TEGRA_CEC_INT_STAT_TX_FRAME_TRANSMITTED) {
		cec_write(cec, TEGRA_CEC_INT_STAT,
			  TEGRA_CEC_INT_STAT_TX_FRAME_TRANSMITTED);

		if (status & TEGRA_CEC_INT_STAT_TX_FRAME_OR_BLOCK_NAKD) {
			tegra_cec_error_recovery(cec);

			cec->tx_done = true;
			cec->tx_status = CEC_TX_STATUS_NACK;
		} else {
			cec->tx_done = true;
			cec->tx_status = CEC_TX_STATUS_OK;
		}
		return IRQ_WAKE_THREAD;
	}

	if (status & TEGRA_CEC_INT_STAT_TX_FRAME_OR_BLOCK_NAKD)
		dev_warn(dev, "TX NAKed on the fly!\n");

	if (status & TEGRA_CEC_INT_STAT_TX_REGISTER_EMPTY) {
		if (cec->tx_buf_cur == cec->tx_buf_cnt) {
			cec_write(cec, TEGRA_CEC_INT_MASK,
				  mask & ~TEGRA_CEC_INT_MASK_TX_REGISTER_EMPTY);
		} else {
			cec_write(cec, TEGRA_CEC_TX_REGISTER,
				  cec->tx_buf[cec->tx_buf_cur++]);
			cec_write(cec, TEGRA_CEC_INT_STAT,
				  TEGRA_CEC_INT_STAT_TX_REGISTER_EMPTY);
		}
	}

	if (status & (TEGRA_CEC_INT_STAT_RX_REGISTER_OVERRUN |
		      TEGRA_CEC_INT_STAT_RX_BUS_ANOMALY_DETECTED |
		      TEGRA_CEC_INT_STAT_RX_START_BIT_DETECTED |
		      TEGRA_CEC_INT_STAT_RX_BUS_ERROR_DETECTED)) {
		cec_write(cec, TEGRA_CEC_INT_STAT,
			  (TEGRA_CEC_INT_STAT_RX_REGISTER_OVERRUN |
			   TEGRA_CEC_INT_STAT_RX_BUS_ANOMALY_DETECTED |
			   TEGRA_CEC_INT_STAT_RX_START_BIT_DETECTED |
			   TEGRA_CEC_INT_STAT_RX_BUS_ERROR_DETECTED));
	} else if (status & TEGRA_CEC_INT_STAT_RX_REGISTER_FULL) {
		u32 v;

		cec_write(cec, TEGRA_CEC_INT_STAT,
			  TEGRA_CEC_INT_STAT_RX_REGISTER_FULL);
		v = cec_read(cec, TEGRA_CEC_RX_REGISTER);
		if (cec->rx_buf_cnt < CEC_MAX_MSG_SIZE)
			cec->rx_buf[cec->rx_buf_cnt++] = v & 0xff;
		if (v & TEGRA_CEC_RX_REGISTER_EOM) {
			cec->rx_done = true;
			return IRQ_WAKE_THREAD;
		}
	}

	return IRQ_HANDLED;
}

static int tegra_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct tegra_cec *cec = adap->priv;

	cec->rx_buf_cnt = 0;
	cec->tx_buf_cnt = 0;
	cec->tx_buf_cur = 0;

	cec_write(cec, TEGRA_CEC_HW_CONTROL, 0);
	cec_write(cec, TEGRA_CEC_INT_MASK, 0);
	cec_write(cec, TEGRA_CEC_INT_STAT, 0xffffffff);
	cec_write(cec, TEGRA_CEC_SW_CONTROL, 0);

	if (!enable)
		return 0;

	cec_write(cec, TEGRA_CEC_INPUT_FILTER, (1U << 31) | 0x20);

	cec_write(cec, TEGRA_CEC_RX_TIMING_0,
		  (0x7a << TEGRA_CEC_RX_TIM0_START_BIT_MAX_LO_TIME_SHIFT) |
		  (0x6d << TEGRA_CEC_RX_TIM0_START_BIT_MIN_LO_TIME_SHIFT) |
		  (0x93 << TEGRA_CEC_RX_TIM0_START_BIT_MAX_DURATION_SHIFT) |
		  (0x86 << TEGRA_CEC_RX_TIM0_START_BIT_MIN_DURATION_SHIFT));

	cec_write(cec, TEGRA_CEC_RX_TIMING_1,
		  (0x35 << TEGRA_CEC_RX_TIM1_DATA_BIT_MAX_LO_TIME_SHIFT) |
		  (0x21 << TEGRA_CEC_RX_TIM1_DATA_BIT_SAMPLE_TIME_SHIFT) |
		  (0x56 << TEGRA_CEC_RX_TIM1_DATA_BIT_MAX_DURATION_SHIFT) |
		  (0x40 << TEGRA_CEC_RX_TIM1_DATA_BIT_MIN_DURATION_SHIFT));

	cec_write(cec, TEGRA_CEC_RX_TIMING_2,
		  (0x50 << TEGRA_CEC_RX_TIM2_END_OF_BLOCK_TIME_SHIFT));

	cec_write(cec, TEGRA_CEC_TX_TIMING_0,
		  (0x74 << TEGRA_CEC_TX_TIM0_START_BIT_LO_TIME_SHIFT) |
		  (0x8d << TEGRA_CEC_TX_TIM0_START_BIT_DURATION_SHIFT) |
		  (0x08 << TEGRA_CEC_TX_TIM0_BUS_XITION_TIME_SHIFT) |
		  (0x71 << TEGRA_CEC_TX_TIM0_BUS_ERROR_LO_TIME_SHIFT));

	cec_write(cec, TEGRA_CEC_TX_TIMING_1,
		  (0x2f << TEGRA_CEC_TX_TIM1_LO_DATA_BIT_LO_TIME_SHIFT) |
		  (0x13 << TEGRA_CEC_TX_TIM1_HI_DATA_BIT_LO_TIME_SHIFT) |
		  (0x4b << TEGRA_CEC_TX_TIM1_DATA_BIT_DURATION_SHIFT) |
		  (0x21 << TEGRA_CEC_TX_TIM1_ACK_NAK_BIT_SAMPLE_TIME_SHIFT));

	cec_write(cec, TEGRA_CEC_TX_TIMING_2,
		  (0x07 << TEGRA_CEC_TX_TIM2_BUS_IDLE_TIME_ADDITIONAL_FRAME_SHIFT) |
		  (0x05 << TEGRA_CEC_TX_TIM2_BUS_IDLE_TIME_NEW_FRAME_SHIFT) |
		  (0x03 << TEGRA_CEC_TX_TIM2_BUS_IDLE_TIME_RETRY_FRAME_SHIFT));

	cec_write(cec, TEGRA_CEC_INT_MASK,
		  TEGRA_CEC_INT_MASK_TX_REGISTER_UNDERRUN |
		  TEGRA_CEC_INT_MASK_TX_FRAME_OR_BLOCK_NAKD |
		  TEGRA_CEC_INT_MASK_TX_ARBITRATION_FAILED |
		  TEGRA_CEC_INT_MASK_TX_BUS_ANOMALY_DETECTED |
		  TEGRA_CEC_INT_MASK_TX_FRAME_TRANSMITTED |
		  TEGRA_CEC_INT_MASK_RX_REGISTER_FULL |
		  TEGRA_CEC_INT_MASK_RX_REGISTER_OVERRUN);

	cec_write(cec, TEGRA_CEC_HW_CONTROL, TEGRA_CEC_HWCTRL_TX_RX_MODE);
	return 0;
}

static int tegra_cec_adap_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct tegra_cec *cec = adap->priv;
	u32 state = cec_read(cec, TEGRA_CEC_HW_CONTROL);

	if (logical_addr == CEC_LOG_ADDR_INVALID)
		state &= ~TEGRA_CEC_HWCTRL_RX_LADDR_MASK;
	else
		state |= TEGRA_CEC_HWCTRL_RX_LADDR((1 << logical_addr));

	cec_write(cec, TEGRA_CEC_HW_CONTROL, state);
	return 0;
}

static int tegra_cec_adap_monitor_all_enable(struct cec_adapter *adap,
					     bool enable)
{
	struct tegra_cec *cec = adap->priv;
	u32 reg = cec_read(cec, TEGRA_CEC_HW_CONTROL);

	if (enable)
		reg |= TEGRA_CEC_HWCTRL_RX_SNOOP;
	else
		reg &= ~TEGRA_CEC_HWCTRL_RX_SNOOP;
	cec_write(cec, TEGRA_CEC_HW_CONTROL, reg);
	return 0;
}

static int tegra_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				   u32 signal_free_time_ms, struct cec_msg *msg)
{
	bool retry_xfer = signal_free_time_ms == CEC_SIGNAL_FREE_TIME_RETRY;
	struct tegra_cec *cec = adap->priv;
	unsigned int i;
	u32 mode = 0;
	u32 mask;

	if (cec_msg_is_broadcast(msg))
		mode = TEGRA_CEC_TX_REG_BCAST;

	cec->tx_buf_cur = 0;
	cec->tx_buf_cnt = msg->len;

	for (i = 0; i < msg->len; i++) {
		cec->tx_buf[i] = mode | msg->msg[i];
		if (i == 0)
			cec->tx_buf[i] |= TEGRA_CEC_TX_REG_START_BIT;
		if (i == msg->len - 1)
			cec->tx_buf[i] |= TEGRA_CEC_TX_REG_EOM;
		if (i == 0 && retry_xfer)
			cec->tx_buf[i] |= TEGRA_CEC_TX_REG_RETRY;
	}

	mask = cec_read(cec, TEGRA_CEC_INT_MASK);
	cec_write(cec, TEGRA_CEC_INT_MASK,
		  mask | TEGRA_CEC_INT_MASK_TX_REGISTER_EMPTY);

	return 0;
}

static const struct cec_adap_ops tegra_cec_ops = {
	.adap_enable = tegra_cec_adap_enable,
	.adap_log_addr = tegra_cec_adap_log_addr,
	.adap_transmit = tegra_cec_adap_transmit,
	.adap_monitor_all_enable = tegra_cec_adap_monitor_all_enable,
};

static int tegra_cec_probe(struct platform_device *pdev)
{
	struct platform_device *hdmi_dev;
	struct device_node *np;
	struct tegra_cec *cec;
	struct resource *res;
	int ret = 0;

	np = of_parse_phandle(pdev->dev.of_node, "hdmi-phandle", 0);

	if (!np) {
		dev_err(&pdev->dev, "Failed to find hdmi node in device tree\n");
		return -ENODEV;
	}
	hdmi_dev = of_find_device_by_node(np);
	if (hdmi_dev == NULL)
		return -EPROBE_DEFER;

	cec = devm_kzalloc(&pdev->dev, sizeof(struct tegra_cec), GFP_KERNEL);

	if (!cec)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res) {
		dev_err(&pdev->dev,
			"Unable to allocate resources for device\n");
		return -EBUSY;
	}

	if (!devm_request_mem_region(&pdev->dev, res->start, resource_size(res),
		pdev->name)) {
		dev_err(&pdev->dev,
			"Unable to request mem region for device\n");
		return -EBUSY;
	}

	cec->tegra_cec_irq = platform_get_irq(pdev, 0);

	if (cec->tegra_cec_irq <= 0)
		return -EBUSY;

	cec->cec_base = devm_ioremap_nocache(&pdev->dev, res->start,
					     resource_size(res));

	if (!cec->cec_base) {
		dev_err(&pdev->dev, "Unable to grab IOs for device\n");
		return -EBUSY;
	}

	cec->clk = devm_clk_get(&pdev->dev, "cec");

	if (IS_ERR_OR_NULL(cec->clk)) {
		dev_err(&pdev->dev, "Can't get clock for CEC\n");
		return -ENOENT;
	}

	clk_prepare_enable(cec->clk);

	/* set context info. */
	cec->dev = &pdev->dev;

	platform_set_drvdata(pdev, cec);

	ret = devm_request_threaded_irq(&pdev->dev, cec->tegra_cec_irq,
		tegra_cec_irq_handler, tegra_cec_irq_thread_handler,
		0, "cec_irq", &pdev->dev);

	if (ret) {
		dev_err(&pdev->dev,
			"Unable to request interrupt for device\n");
		goto clk_error;
	}

	cec->notifier = cec_notifier_get(&hdmi_dev->dev);
	if (!cec->notifier) {
		ret = -ENOMEM;
		goto clk_error;
	}

	cec->adap = cec_allocate_adapter(&tegra_cec_ops, cec, TEGRA_CEC_NAME,
			CEC_CAP_DEFAULTS | CEC_CAP_MONITOR_ALL,
			CEC_MAX_LOG_ADDRS);
	if (IS_ERR(cec->adap)) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Couldn't create cec adapter\n");
		goto cec_error;
	}
	ret = cec_register_adapter(cec->adap, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register device\n");
		goto cec_error;
	}

	cec_register_cec_notifier(cec->adap, cec->notifier);

	return 0;

cec_error:
	if (cec->notifier)
		cec_notifier_put(cec->notifier);
	cec_delete_adapter(cec->adap);
clk_error:
	clk_disable_unprepare(cec->clk);
	return ret;
}

static int tegra_cec_remove(struct platform_device *pdev)
{
	struct tegra_cec *cec = platform_get_drvdata(pdev);

	clk_disable_unprepare(cec->clk);

	cec_unregister_adapter(cec->adap);
	cec_notifier_put(cec->notifier);

	return 0;
}

#ifdef CONFIG_PM
static int tegra_cec_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct tegra_cec *cec = platform_get_drvdata(pdev);

	clk_disable_unprepare(cec->clk);

	dev_notice(&pdev->dev, "suspended\n");
	return 0;
}

static int tegra_cec_resume(struct platform_device *pdev)
{
	struct tegra_cec *cec = platform_get_drvdata(pdev);

	dev_notice(&pdev->dev, "Resuming\n");

	clk_prepare_enable(cec->clk);

	return 0;
}
#endif

static const struct of_device_id tegra_cec_of_match[] = {
	{ .compatible = "nvidia,tegra114-cec", },
	{ .compatible = "nvidia,tegra124-cec", },
	{ .compatible = "nvidia,tegra210-cec", },
	{},
};

static struct platform_driver tegra_cec_driver = {
	.driver = {
		.name = TEGRA_CEC_NAME,
		.of_match_table = of_match_ptr(tegra_cec_of_match),
	},
	.probe = tegra_cec_probe,
	.remove = tegra_cec_remove,

#ifdef CONFIG_PM
	.suspend = tegra_cec_suspend,
	.resume = tegra_cec_resume,
#endif
};

module_platform_driver(tegra_cec_driver);

MODULE_DESCRIPTION("Tegra HDMI CEC driver");
MODULE_AUTHOR("NVIDIA CORPORATION");
MODULE_AUTHOR("Cisco Systems, Inc. and/or its affiliates");
MODULE_LICENSE("GPL v2");
