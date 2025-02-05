// SPDX-License-Identifier: GPL-2.0-only
//
// Driver for the regulator based Ethernet Power Sourcing Equipment, without
// auto classification support.
//
// Copyright (c) 2022 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
//

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pse-pd/pse.h>
#include <linux/regulator/consumer.h>

struct pse_reg_priv {
	struct pse_controller_dev pcdev;
	struct regulator *ps; /*power source */
	enum ethtool_podl_pse_admin_state admin_state;
};

static struct pse_reg_priv *to_pse_reg(struct pse_controller_dev *pcdev)
{
	return container_of(pcdev, struct pse_reg_priv, pcdev);
}

static int
pse_reg_pi_enable(struct pse_controller_dev *pcdev, int id)
{
	struct pse_reg_priv *priv = to_pse_reg(pcdev);
	int ret;

	ret = regulator_enable(priv->ps);
	if (ret)
		return ret;

	priv->admin_state = ETHTOOL_PODL_PSE_ADMIN_STATE_ENABLED;
	return 0;
}

static int
pse_reg_pi_disable(struct pse_controller_dev *pcdev, int id)
{
	struct pse_reg_priv *priv = to_pse_reg(pcdev);
	int ret;

	ret = regulator_disable(priv->ps);
	if (ret)
		return ret;

	priv->admin_state = ETHTOOL_PODL_PSE_ADMIN_STATE_DISABLED;
	return 0;
}

static int
pse_reg_pi_get_admin_state(struct pse_controller_dev *pcdev, int id,
			   struct pse_admin_state *admin_state)
{
	struct pse_reg_priv *priv = to_pse_reg(pcdev);

	admin_state->podl_admin_state = priv->admin_state;

	return 0;
}

static int
pse_reg_pi_get_pw_status(struct pse_controller_dev *pcdev, int id,
			 struct pse_pw_status *pw_status)
{
	struct pse_reg_priv *priv = to_pse_reg(pcdev);
	int ret;

	ret = regulator_is_enabled(priv->ps);
	if (ret < 0)
		return ret;

	if (!ret)
		pw_status->podl_pw_status =
			ETHTOOL_PODL_PSE_PW_D_STATUS_DISABLED;
	else
		pw_status->podl_pw_status =
			ETHTOOL_PODL_PSE_PW_D_STATUS_DELIVERING;

	return 0;
}

static const struct pse_controller_ops pse_reg_ops = {
	.pi_get_admin_state = pse_reg_pi_get_admin_state,
	.pi_get_pw_status = pse_reg_pi_get_pw_status,
	.pi_enable = pse_reg_pi_enable,
	.pi_disable = pse_reg_pi_disable,
};

static int
pse_reg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pse_reg_priv *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (!pdev->dev.of_node)
		return -ENOENT;

	priv->ps = devm_regulator_get_exclusive(dev, "pse");
	if (IS_ERR(priv->ps))
		return dev_err_probe(dev, PTR_ERR(priv->ps),
				     "failed to get PSE regulator.\n");

	platform_set_drvdata(pdev, priv);

	ret = regulator_is_enabled(priv->ps);
	if (ret < 0)
		return ret;

	if (ret)
		priv->admin_state = ETHTOOL_PODL_PSE_ADMIN_STATE_ENABLED;
	else
		priv->admin_state = ETHTOOL_PODL_PSE_ADMIN_STATE_DISABLED;

	priv->pcdev.owner = THIS_MODULE;
	priv->pcdev.ops = &pse_reg_ops;
	priv->pcdev.dev = dev;
	priv->pcdev.types = ETHTOOL_PSE_PODL;
	ret = devm_pse_controller_register(dev, &priv->pcdev);
	if (ret) {
		dev_err(dev, "failed to register PSE controller (%pe)\n",
			ERR_PTR(ret));
		return ret;
	}

	return 0;
}

static const __maybe_unused struct of_device_id pse_reg_of_match[] = {
	{ .compatible = "podl-pse-regulator", },
	{ },
};
MODULE_DEVICE_TABLE(of, pse_reg_of_match);

static struct platform_driver pse_reg_driver = {
	.probe		= pse_reg_probe,
	.driver		= {
		.name		= "PSE regulator",
		.of_match_table = of_match_ptr(pse_reg_of_match),
	},
};
module_platform_driver(pse_reg_driver);

MODULE_AUTHOR("Oleksij Rempel <kernel@pengutronix.de>");
MODULE_DESCRIPTION("regulator based Ethernet Power Sourcing Equipment");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pse-regulator");
