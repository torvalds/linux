/*
 * FPGA Freeze Bridge Controller
 *
 *  Copyright (C) 2016 Altera Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/fpga/fpga-bridge.h>

#define FREEZE_CSR_STATUS_OFFSET		0
#define FREEZE_CSR_CTRL_OFFSET			4
#define FREEZE_CSR_ILLEGAL_REQ_OFFSET		8
#define FREEZE_CSR_REG_VERSION			12

#define FREEZE_CSR_SUPPORTED_VERSION		2

#define FREEZE_CSR_STATUS_FREEZE_REQ_DONE	BIT(0)
#define FREEZE_CSR_STATUS_UNFREEZE_REQ_DONE	BIT(1)

#define FREEZE_CSR_CTRL_FREEZE_REQ		BIT(0)
#define FREEZE_CSR_CTRL_RESET_REQ		BIT(1)
#define FREEZE_CSR_CTRL_UNFREEZE_REQ		BIT(2)

#define FREEZE_BRIDGE_NAME			"freeze"

struct altera_freeze_br_data {
	struct device *dev;
	void __iomem *base_addr;
	bool enable;
};

/*
 * Poll status until status bit is set or we have a timeout.
 */
static int altera_freeze_br_req_ack(struct altera_freeze_br_data *priv,
				    u32 timeout, u32 req_ack)
{
	struct device *dev = priv->dev;
	void __iomem *csr_illegal_req_addr = priv->base_addr +
					     FREEZE_CSR_ILLEGAL_REQ_OFFSET;
	u32 status, illegal, ctrl;
	int ret = -ETIMEDOUT;

	do {
		illegal = readl(csr_illegal_req_addr);
		if (illegal) {
			dev_err(dev, "illegal request detected 0x%x", illegal);

			writel(1, csr_illegal_req_addr);

			illegal = readl(csr_illegal_req_addr);
			if (illegal)
				dev_err(dev, "illegal request not cleared 0x%x",
					illegal);

			ret = -EINVAL;
			break;
		}

		status = readl(priv->base_addr + FREEZE_CSR_STATUS_OFFSET);
		dev_dbg(dev, "%s %x %x\n", __func__, status, req_ack);
		status &= req_ack;
		if (status) {
			ctrl = readl(priv->base_addr + FREEZE_CSR_CTRL_OFFSET);
			dev_dbg(dev, "%s request %x acknowledged %x %x\n",
				__func__, req_ack, status, ctrl);
			ret = 0;
			break;
		}

		udelay(1);
	} while (timeout--);

	if (ret == -ETIMEDOUT)
		dev_err(dev, "%s timeout waiting for 0x%x\n",
			__func__, req_ack);

	return ret;
}

static int altera_freeze_br_do_freeze(struct altera_freeze_br_data *priv,
				      u32 timeout)
{
	struct device *dev = priv->dev;
	void __iomem *csr_ctrl_addr = priv->base_addr +
				      FREEZE_CSR_CTRL_OFFSET;
	u32 status;
	int ret;

	status = readl(priv->base_addr + FREEZE_CSR_STATUS_OFFSET);

	dev_dbg(dev, "%s %d %d\n", __func__, status, readl(csr_ctrl_addr));

	if (status & FREEZE_CSR_STATUS_FREEZE_REQ_DONE) {
		dev_dbg(dev, "%s bridge already disabled %d\n",
			__func__, status);
		return 0;
	} else if (!(status & FREEZE_CSR_STATUS_UNFREEZE_REQ_DONE)) {
		dev_err(dev, "%s bridge not enabled %d\n", __func__, status);
		return -EINVAL;
	}

	writel(FREEZE_CSR_CTRL_FREEZE_REQ, csr_ctrl_addr);

	ret = altera_freeze_br_req_ack(priv, timeout,
				       FREEZE_CSR_STATUS_FREEZE_REQ_DONE);

	if (ret)
		writel(0, csr_ctrl_addr);
	else
		writel(FREEZE_CSR_CTRL_RESET_REQ, csr_ctrl_addr);

	return ret;
}

static int altera_freeze_br_do_unfreeze(struct altera_freeze_br_data *priv,
					u32 timeout)
{
	struct device *dev = priv->dev;
	void __iomem *csr_ctrl_addr = priv->base_addr +
				      FREEZE_CSR_CTRL_OFFSET;
	u32 status;
	int ret;

	writel(0, csr_ctrl_addr);

	status = readl(priv->base_addr + FREEZE_CSR_STATUS_OFFSET);

	dev_dbg(dev, "%s %d %d\n", __func__, status, readl(csr_ctrl_addr));

	if (status & FREEZE_CSR_STATUS_UNFREEZE_REQ_DONE) {
		dev_dbg(dev, "%s bridge already enabled %d\n",
			__func__, status);
		return 0;
	} else if (!(status & FREEZE_CSR_STATUS_FREEZE_REQ_DONE)) {
		dev_err(dev, "%s bridge not frozen %d\n", __func__, status);
		return -EINVAL;
	}

	writel(FREEZE_CSR_CTRL_UNFREEZE_REQ, csr_ctrl_addr);

	ret = altera_freeze_br_req_ack(priv, timeout,
				       FREEZE_CSR_STATUS_UNFREEZE_REQ_DONE);

	status = readl(priv->base_addr + FREEZE_CSR_STATUS_OFFSET);

	dev_dbg(dev, "%s %d %d\n", __func__, status, readl(csr_ctrl_addr));

	writel(0, csr_ctrl_addr);

	return ret;
}

/*
 * enable = 1 : allow traffic through the bridge
 * enable = 0 : disable traffic through the bridge
 */
static int altera_freeze_br_enable_set(struct fpga_bridge *bridge,
				       bool enable)
{
	struct altera_freeze_br_data *priv = bridge->priv;
	struct fpga_image_info *info = bridge->info;
	u32 timeout = 0;
	int ret;

	if (enable) {
		if (info)
			timeout = info->enable_timeout_us;

		ret = altera_freeze_br_do_unfreeze(bridge->priv, timeout);
	} else {
		if (info)
			timeout = info->disable_timeout_us;

		ret = altera_freeze_br_do_freeze(bridge->priv, timeout);
	}

	if (!ret)
		priv->enable = enable;

	return ret;
}

static int altera_freeze_br_enable_show(struct fpga_bridge *bridge)
{
	struct altera_freeze_br_data *priv = bridge->priv;

	return priv->enable;
}

static struct fpga_bridge_ops altera_freeze_br_br_ops = {
	.enable_set = altera_freeze_br_enable_set,
	.enable_show = altera_freeze_br_enable_show,
};

static const struct of_device_id altera_freeze_br_of_match[] = {
	{ .compatible = "altr,freeze-bridge-controller", },
	{},
};
MODULE_DEVICE_TABLE(of, altera_freeze_br_of_match);

static int altera_freeze_br_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct altera_freeze_br_data *priv;
	struct resource *res;
	u32 status, revision;

	if (!np)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base_addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base_addr))
		return PTR_ERR(priv->base_addr);

	status = readl(priv->base_addr + FREEZE_CSR_STATUS_OFFSET);
	if (status & FREEZE_CSR_STATUS_UNFREEZE_REQ_DONE)
		priv->enable = 1;

	revision = readl(priv->base_addr + FREEZE_CSR_REG_VERSION);
	if (revision != FREEZE_CSR_SUPPORTED_VERSION)
		dev_warn(dev,
			 "%s Freeze Controller unexpected revision %d != %d\n",
			 __func__, revision, FREEZE_CSR_SUPPORTED_VERSION);

	return fpga_bridge_register(dev, FREEZE_BRIDGE_NAME,
				    &altera_freeze_br_br_ops, priv);
}

static int altera_freeze_br_remove(struct platform_device *pdev)
{
	fpga_bridge_unregister(&pdev->dev);

	return 0;
}

static struct platform_driver altera_freeze_br_driver = {
	.probe = altera_freeze_br_probe,
	.remove = altera_freeze_br_remove,
	.driver = {
		.name	= "altera_freeze_br",
		.of_match_table = of_match_ptr(altera_freeze_br_of_match),
	},
};

module_platform_driver(altera_freeze_br_driver);

MODULE_DESCRIPTION("Altera Freeze Bridge");
MODULE_AUTHOR("Alan Tull <atull@opensource.altera.com>");
MODULE_LICENSE("GPL v2");
