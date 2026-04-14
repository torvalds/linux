// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@oss.qualcomm.com>
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pwrseq/provider.h>
#include <linux/regulator/consumer.h>
#include <linux/serdev.h>
#include <linux/slab.h>

struct pwrseq_pcie_m2_pdata {
	const struct pwrseq_target_data **targets;
};

struct pwrseq_pcie_m2_ctx {
	struct pwrseq_device *pwrseq;
	struct device_node *of_node;
	const struct pwrseq_pcie_m2_pdata *pdata;
	struct regulator_bulk_data *regs;
	size_t num_vregs;
	struct notifier_block nb;
	struct gpio_desc *w_disable1_gpio;
	struct gpio_desc *w_disable2_gpio;
	struct serdev_device *serdev;
	struct of_changeset *ocs;
	struct device *dev;
};

static int pwrseq_pcie_m2_vregs_enable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return regulator_bulk_enable(ctx->num_vregs, ctx->regs);
}

static int pwrseq_pcie_m2_vregs_disable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return regulator_bulk_disable(ctx->num_vregs, ctx->regs);
}

static const struct pwrseq_unit_data pwrseq_pcie_m2_vregs_unit_data = {
	.name = "regulators-enable",
	.enable = pwrseq_pcie_m2_vregs_enable,
	.disable = pwrseq_pcie_m2_vregs_disable,
};

static const struct pwrseq_unit_data *pwrseq_pcie_m2_unit_deps[] = {
	&pwrseq_pcie_m2_vregs_unit_data,
	NULL
};

static int pwrseq_pci_m2_e_uart_enable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return gpiod_set_value_cansleep(ctx->w_disable2_gpio, 0);
}

static int pwrseq_pci_m2_e_uart_disable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return gpiod_set_value_cansleep(ctx->w_disable2_gpio, 1);
}

static const struct pwrseq_unit_data pwrseq_pcie_m2_e_uart_unit_data = {
	.name = "uart-enable",
	.deps = pwrseq_pcie_m2_unit_deps,
	.enable = pwrseq_pci_m2_e_uart_enable,
	.disable = pwrseq_pci_m2_e_uart_disable,
};

static int pwrseq_pci_m2_e_pcie_enable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return gpiod_set_value_cansleep(ctx->w_disable1_gpio, 0);
}

static int pwrseq_pci_m2_e_pcie_disable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return gpiod_set_value_cansleep(ctx->w_disable1_gpio, 1);
}

static const struct pwrseq_unit_data pwrseq_pcie_m2_e_pcie_unit_data = {
	.name = "pcie-enable",
	.deps = pwrseq_pcie_m2_unit_deps,
	.enable = pwrseq_pci_m2_e_pcie_enable,
	.disable = pwrseq_pci_m2_e_pcie_disable,
};

static const struct pwrseq_unit_data pwrseq_pcie_m2_m_pcie_unit_data = {
	.name = "pcie-enable",
	.deps = pwrseq_pcie_m2_unit_deps,
};

static int pwrseq_pcie_m2_e_pwup_delay(struct pwrseq_device *pwrseq)
{
	/*
	 * FIXME: This delay is only required for some Qcom WLAN/BT cards like
	 * WCN7850 and not for all devices. But currently, there is no way to
	 * identify the device model before enumeration.
	 */
	msleep(50);

	return 0;
}

static const struct pwrseq_target_data pwrseq_pcie_m2_e_uart_target_data = {
	.name = "uart",
	.unit = &pwrseq_pcie_m2_e_uart_unit_data,
	.post_enable = pwrseq_pcie_m2_e_pwup_delay,
};

static const struct pwrseq_target_data pwrseq_pcie_m2_e_pcie_target_data = {
	.name = "pcie",
	.unit = &pwrseq_pcie_m2_e_pcie_unit_data,
	.post_enable = pwrseq_pcie_m2_e_pwup_delay,
};

static const struct pwrseq_target_data pwrseq_pcie_m2_m_pcie_target_data = {
	.name = "pcie",
	.unit = &pwrseq_pcie_m2_m_pcie_unit_data,
};

static const struct pwrseq_target_data *pwrseq_pcie_m2_e_targets[] = {
	&pwrseq_pcie_m2_e_pcie_target_data,
	&pwrseq_pcie_m2_e_uart_target_data,
	NULL
};

static const struct pwrseq_target_data *pwrseq_pcie_m2_m_targets[] = {
	&pwrseq_pcie_m2_m_pcie_target_data,
	NULL
};

static const struct pwrseq_pcie_m2_pdata pwrseq_pcie_m2_e_of_data = {
	.targets = pwrseq_pcie_m2_e_targets,
};

static const struct pwrseq_pcie_m2_pdata pwrseq_pcie_m2_m_of_data = {
	.targets = pwrseq_pcie_m2_m_targets,
};

static int pwrseq_pcie_m2_match(struct pwrseq_device *pwrseq,
				 struct device *dev)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);
	struct device_node *endpoint __free(device_node) = NULL;

	/*
	 * Traverse the 'remote-endpoint' nodes and check if the remote node's
	 * parent matches the OF node of 'dev'.
	 */
	for_each_endpoint_of_node(ctx->of_node, endpoint) {
		struct device_node *remote __free(device_node) =
				of_graph_get_remote_port_parent(endpoint);
		if (remote && (remote == dev_of_node(dev)))
			return PWRSEQ_MATCH_OK;
	}

	return PWRSEQ_NO_MATCH;
}

static int pwrseq_m2_pcie_create_bt_node(struct pwrseq_pcie_m2_ctx *ctx,
					struct device_node *parent)
{
	struct device *dev = ctx->dev;
	struct device_node *np;
	int ret;

	ctx->ocs = kzalloc_obj(*ctx->ocs);
	if (!ctx->ocs)
		return -ENOMEM;

	of_changeset_init(ctx->ocs);

	np = of_changeset_create_node(ctx->ocs, parent, "bluetooth");
	if (!np) {
		dev_err(dev, "Failed to create bluetooth node\n");
		ret = -ENODEV;
		goto err_destroy_changeset;
	}

	ret = of_changeset_add_prop_string(ctx->ocs, np, "compatible", "qcom,wcn7850-bt");
	if (ret) {
		dev_err(dev, "Failed to add bluetooth compatible: %d\n", ret);
		goto err_destroy_changeset;
	}

	ret = of_changeset_apply(ctx->ocs);
	if (ret) {
		dev_err(dev, "Failed to apply changeset: %d\n", ret);
		goto err_destroy_changeset;
	}

	ret = device_add_of_node(&ctx->serdev->dev, np);
	if (ret) {
		dev_err(dev, "Failed to add OF node: %d\n", ret);
		goto err_revert_changeset;
	}

	return 0;

err_revert_changeset:
	of_changeset_revert(ctx->ocs);
err_destroy_changeset:
	of_changeset_destroy(ctx->ocs);
	kfree(ctx->ocs);
	ctx->ocs = NULL;

	return ret;
}

static int pwrseq_pcie_m2_create_serdev(struct pwrseq_pcie_m2_ctx *ctx)
{
	struct serdev_controller *serdev_ctrl;
	struct device *dev = ctx->dev;
	int ret;

	struct device_node *serdev_parent __free(device_node) =
		of_graph_get_remote_node(dev_of_node(ctx->dev), 3, 0);
	if (!serdev_parent)
		return 0;

	serdev_ctrl = of_find_serdev_controller_by_node(serdev_parent);
	if (!serdev_ctrl)
		return 0;

	/* Bail out if the device was already attached to this controller */
	if (serdev_ctrl->serdev) {
		serdev_controller_put(serdev_ctrl);
		return 0;
	}

	ctx->serdev = serdev_device_alloc(serdev_ctrl);
	if (!ctx->serdev) {
		ret = -ENOMEM;
		goto err_put_ctrl;
	}

	ret = pwrseq_m2_pcie_create_bt_node(ctx, serdev_parent);
	if (ret)
		goto err_free_serdev;

	ret = serdev_device_add(ctx->serdev);
	if (ret) {
		dev_err(dev, "Failed to add serdev for WCN7850: %d\n", ret);
		goto err_free_dt_node;
	}

	serdev_controller_put(serdev_ctrl);

	return 0;

err_free_dt_node:
	device_remove_of_node(&ctx->serdev->dev);
	of_changeset_revert(ctx->ocs);
	of_changeset_destroy(ctx->ocs);
	kfree(ctx->ocs);
	ctx->ocs = NULL;
err_free_serdev:
	serdev_device_put(ctx->serdev);
	ctx->serdev = NULL;
err_put_ctrl:
	serdev_controller_put(serdev_ctrl);

	return ret;
}

static void pwrseq_pcie_m2_remove_serdev(struct pwrseq_pcie_m2_ctx *ctx)
{
	if (ctx->serdev) {
		device_remove_of_node(&ctx->serdev->dev);
		serdev_device_remove(ctx->serdev);
		ctx->serdev = NULL;
	}

	if (ctx->ocs) {
		of_changeset_revert(ctx->ocs);
		of_changeset_destroy(ctx->ocs);
		kfree(ctx->ocs);
		ctx->ocs = NULL;
	}
}

static int pwrseq_m2_pcie_notify(struct notifier_block *nb, unsigned long action,
			      void *data)
{
	struct pwrseq_pcie_m2_ctx *ctx = container_of(nb, struct pwrseq_pcie_m2_ctx, nb);
	struct pci_dev *pdev = to_pci_dev(data);
	int ret;

	/*
	 * Check whether the PCI device is associated with this M.2 connector or
	 * not, by comparing the OF node of the PCI device parent and the Port 0
	 * (PCIe) remote node parent OF node.
	 */
	struct device_node *pci_parent __free(device_node) =
			of_graph_get_remote_node(dev_of_node(ctx->dev), 0, 0);
	if (!pci_parent || (pci_parent != pdev->dev.parent->of_node))
		return NOTIFY_DONE;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		/* Create serdev device for WCN7850 */
		if (pdev->vendor == PCI_VENDOR_ID_QCOM && pdev->device == 0x1107) {
			ret = pwrseq_pcie_m2_create_serdev(ctx);
			if (ret)
				return notifier_from_errno(ret);
		}
		break;
	case BUS_NOTIFY_REMOVED_DEVICE:
		/* Destroy serdev device for WCN7850 */
		if (pdev->vendor == PCI_VENDOR_ID_QCOM && pdev->device == 0x1107)
			pwrseq_pcie_m2_remove_serdev(ctx);

		break;
	}

	return NOTIFY_OK;
}

static bool pwrseq_pcie_m2_check_remote_node(struct device *dev, u8 port, u8 endpoint,
					     const char *node)
{
	struct device_node *remote __free(device_node) =
			of_graph_get_remote_node(dev_of_node(dev), port, endpoint);

	if (remote && of_node_name_eq(remote, node))
		return true;

	return false;
}

/*
 * If the connector exposes a non-discoverable bus like UART, the respective
 * protocol device needs to be created manually with the help of the notifier
 * of the discoverable bus like PCIe.
 */
static int pwrseq_pcie_m2_register_notifier(struct pwrseq_pcie_m2_ctx *ctx, struct device *dev)
{
	int ret;

	/*
	 * Register a PCI notifier for Key E connector that has PCIe as Port
	 * 0/Endpoint 0 interface and Serial as Port 3/Endpoint 0 interface.
	 */
	if (pwrseq_pcie_m2_check_remote_node(dev, 3, 0, "serial")) {
		if (pwrseq_pcie_m2_check_remote_node(dev, 0, 0, "pcie")) {
			ctx->dev = dev;
			ctx->nb.notifier_call = pwrseq_m2_pcie_notify;
			ret = bus_register_notifier(&pci_bus_type, &ctx->nb);
			if (ret)
				return dev_err_probe(dev, ret,
						     "Failed to register notifier for serdev\n");
		}
	}

	return 0;
}

static int pwrseq_pcie_m2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwrseq_pcie_m2_ctx *ctx;
	struct pwrseq_config config = {};
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	platform_set_drvdata(pdev, ctx);
	ctx->of_node = dev_of_node(dev);
	ctx->pdata = device_get_match_data(dev);
	if (!ctx->pdata)
		return dev_err_probe(dev, -ENODEV,
				     "Failed to obtain platform data\n");

	/*
	 * Currently, of_regulator_bulk_get_all() is the only regulator API that
	 * allows to get all supplies in the devicetree node without manually
	 * specifying them.
	 */
	ret = of_regulator_bulk_get_all(dev, dev_of_node(dev), &ctx->regs);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Failed to get all regulators\n");

	ctx->num_vregs = ret;

	ctx->w_disable1_gpio = devm_gpiod_get_optional(dev, "w-disable1", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->w_disable1_gpio)) {
		ret = dev_err_probe(dev, PTR_ERR(ctx->w_disable1_gpio),
				     "Failed to get the W_DISABLE_1# GPIO\n");
		goto err_free_regulators;
	}

	ctx->w_disable2_gpio = devm_gpiod_get_optional(dev, "w-disable2", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->w_disable2_gpio)) {
		ret = dev_err_probe(dev, PTR_ERR(ctx->w_disable2_gpio),
				     "Failed to get the W_DISABLE_2# GPIO\n");
		goto err_free_regulators;
	}

	config.parent = dev;
	config.owner = THIS_MODULE;
	config.drvdata = ctx;
	config.match = pwrseq_pcie_m2_match;
	config.targets = ctx->pdata->targets;

	ctx->pwrseq = devm_pwrseq_device_register(dev, &config);
	if (IS_ERR(ctx->pwrseq)) {
		ret = dev_err_probe(dev, PTR_ERR(ctx->pwrseq),
				     "Failed to register the power sequencer\n");
		goto err_free_regulators;
	}

	/*
	 * Register a notifier for creating protocol devices for
	 * non-discoverable busses like UART.
	 */
	ret = pwrseq_pcie_m2_register_notifier(ctx, dev);
	if (ret)
		goto err_free_regulators;

	return 0;

err_free_regulators:
	regulator_bulk_free(ctx->num_vregs, ctx->regs);

	return ret;
}

static void pwrseq_pcie_m2_remove(struct platform_device *pdev)
{
	struct pwrseq_pcie_m2_ctx *ctx = platform_get_drvdata(pdev);

	bus_unregister_notifier(&pci_bus_type, &ctx->nb);
	pwrseq_pcie_m2_remove_serdev(ctx);

	regulator_bulk_free(ctx->num_vregs, ctx->regs);
}

static const struct of_device_id pwrseq_pcie_m2_of_match[] = {
	{
		.compatible = "pcie-m2-m-connector",
		.data = &pwrseq_pcie_m2_m_of_data,
	},
	{
		.compatible = "pcie-m2-e-connector",
		.data = &pwrseq_pcie_m2_e_of_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pwrseq_pcie_m2_of_match);

static struct platform_driver pwrseq_pcie_m2_driver = {
	.driver = {
		.name = "pwrseq-pcie-m2",
		.of_match_table = pwrseq_pcie_m2_of_match,
	},
	.probe = pwrseq_pcie_m2_probe,
	.remove = pwrseq_pcie_m2_remove,
};
module_platform_driver(pwrseq_pcie_m2_driver);

MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@oss.qualcomm.com>");
MODULE_DESCRIPTION("Power Sequencing driver for PCIe M.2 connector");
MODULE_LICENSE("GPL");
