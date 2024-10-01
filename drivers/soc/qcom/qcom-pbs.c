// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/spmi.h>
#include <linux/soc/qcom/qcom-pbs.h>

#define PBS_CLIENT_TRIG_CTL		0x42
#define PBS_CLIENT_SW_TRIG_BIT		BIT(7)
#define PBS_CLIENT_SCRATCH1		0x50
#define PBS_CLIENT_SCRATCH2		0x51
#define PBS_CLIENT_SCRATCH2_ERROR	0xFF

#define RETRIES				2000
#define DELAY				1100

struct pbs_dev {
	struct device		*dev;
	struct regmap		*regmap;
	struct mutex		lock;
	struct device_link	*link;

	u32			base;
};

static int qcom_pbs_wait_for_ack(struct pbs_dev *pbs, u8 bit_pos)
{
	unsigned int val;
	int ret;

	ret = regmap_read_poll_timeout(pbs->regmap,  pbs->base + PBS_CLIENT_SCRATCH2,
				       val, val & BIT(bit_pos), DELAY, DELAY * RETRIES);

	if (ret < 0) {
		dev_err(pbs->dev, "Timeout for PBS ACK/NACK for bit %u\n", bit_pos);
		return -ETIMEDOUT;
	}

	if (val == PBS_CLIENT_SCRATCH2_ERROR) {
		ret = regmap_write(pbs->regmap, pbs->base + PBS_CLIENT_SCRATCH2, 0);
		dev_err(pbs->dev, "NACK from PBS for bit %u\n", bit_pos);
		return -EINVAL;
	}

	dev_dbg(pbs->dev, "PBS sequence for bit %u executed!\n", bit_pos);
	return 0;
}

/**
 * qcom_pbs_trigger_event() - Trigger the PBS RAM sequence
 * @pbs: Pointer to PBS device
 * @bitmap: bitmap
 *
 * This function is used to trigger the PBS RAM sequence to be
 * executed by the client driver.
 *
 * The PBS trigger sequence involves
 * 1. setting the PBS sequence bit in PBS_CLIENT_SCRATCH1
 * 2. Initiating the SW PBS trigger
 * 3. Checking the equivalent bit in PBS_CLIENT_SCRATCH2 for the
 *    completion of the sequence.
 * 4. If PBS_CLIENT_SCRATCH2 == 0xFF, the PBS sequence failed to execute
 *
 * Return: 0 on success, < 0 on failure
 */
int qcom_pbs_trigger_event(struct pbs_dev *pbs, u8 bitmap)
{
	unsigned int val;
	u16 bit_pos;
	int ret;

	if (WARN_ON(!bitmap))
		return -EINVAL;

	if (IS_ERR_OR_NULL(pbs))
		return -EINVAL;

	mutex_lock(&pbs->lock);
	ret = regmap_read(pbs->regmap, pbs->base + PBS_CLIENT_SCRATCH2, &val);
	if (ret < 0)
		goto out;

	if (val == PBS_CLIENT_SCRATCH2_ERROR) {
		/* PBS error - clear SCRATCH2 register */
		ret = regmap_write(pbs->regmap, pbs->base + PBS_CLIENT_SCRATCH2, 0);
		if (ret < 0)
			goto out;
	}

	for (bit_pos = 0; bit_pos < 8; bit_pos++) {
		if (!(bitmap & BIT(bit_pos)))
			continue;

		/* Clear the PBS sequence bit position */
		ret = regmap_update_bits(pbs->regmap, pbs->base + PBS_CLIENT_SCRATCH2,
					 BIT(bit_pos), 0);
		if (ret < 0)
			goto out_clear_scratch1;

		/* Set the PBS sequence bit position */
		ret = regmap_update_bits(pbs->regmap, pbs->base + PBS_CLIENT_SCRATCH1,
					 BIT(bit_pos), BIT(bit_pos));
		if (ret < 0)
			goto out_clear_scratch1;

		/* Initiate the SW trigger */
		ret = regmap_update_bits(pbs->regmap, pbs->base + PBS_CLIENT_TRIG_CTL,
					 PBS_CLIENT_SW_TRIG_BIT, PBS_CLIENT_SW_TRIG_BIT);
		if (ret < 0)
			goto out_clear_scratch1;

		ret = qcom_pbs_wait_for_ack(pbs, bit_pos);
		if (ret < 0)
			goto out_clear_scratch1;

		/* Clear the PBS sequence bit position */
		regmap_update_bits(pbs->regmap, pbs->base + PBS_CLIENT_SCRATCH1, BIT(bit_pos), 0);
		regmap_update_bits(pbs->regmap, pbs->base + PBS_CLIENT_SCRATCH2, BIT(bit_pos), 0);
	}

out_clear_scratch1:
	/* Clear all the requested bitmap */
	ret = regmap_update_bits(pbs->regmap, pbs->base + PBS_CLIENT_SCRATCH1, bitmap, 0);

out:
	mutex_unlock(&pbs->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_pbs_trigger_event);

/**
 * get_pbs_client_device() - Get the PBS device used by client
 * @dev: Client device
 *
 * This function is used to get the PBS device that is being
 * used by the client.
 *
 * Return: pbs_dev on success, ERR_PTR on failure
 */
struct pbs_dev *get_pbs_client_device(struct device *dev)
{
	struct platform_device *pdev;
	struct pbs_dev *pbs;

	struct device_node *pbs_dev_node __free(device_node) = of_parse_phandle(dev->of_node,
										"qcom,pbs", 0);
	if (!pbs_dev_node) {
		dev_err(dev, "Missing qcom,pbs property\n");
		return ERR_PTR(-ENODEV);
	}

	pdev = of_find_device_by_node(pbs_dev_node);
	if (!pdev) {
		dev_err(dev, "Unable to find PBS dev_node\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	pbs = platform_get_drvdata(pdev);
	if (!pbs) {
		dev_err(dev, "Cannot get pbs instance from %s\n", dev_name(&pdev->dev));
		platform_device_put(pdev);
		return ERR_PTR(-EPROBE_DEFER);
	}

	pbs->link = device_link_add(dev, &pdev->dev, DL_FLAG_AUTOREMOVE_SUPPLIER);
	if (!pbs->link) {
		dev_err(&pdev->dev, "Failed to create device link to consumer %s\n", dev_name(dev));
		platform_device_put(pdev);
		return ERR_PTR(-EINVAL);
	}

	return pbs;
}
EXPORT_SYMBOL_GPL(get_pbs_client_device);

static int qcom_pbs_probe(struct platform_device *pdev)
{
	struct pbs_dev *pbs;
	u32 val;
	int ret;

	pbs = devm_kzalloc(&pdev->dev, sizeof(*pbs), GFP_KERNEL);
	if (!pbs)
		return -ENOMEM;

	pbs->dev = &pdev->dev;
	pbs->regmap = dev_get_regmap(pbs->dev->parent, NULL);
	if (!pbs->regmap) {
		dev_err(pbs->dev, "Couldn't get parent's regmap\n");
		return -EINVAL;
	}

	ret = device_property_read_u32(pbs->dev, "reg", &val);
	if (ret < 0) {
		dev_err(pbs->dev, "Couldn't find reg, ret = %d\n", ret);
		return ret;
	}
	pbs->base = val;
	mutex_init(&pbs->lock);

	platform_set_drvdata(pdev, pbs);

	return 0;
}

static const struct of_device_id qcom_pbs_match_table[] = {
	{ .compatible = "qcom,pbs" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_pbs_match_table);

static struct platform_driver qcom_pbs_driver = {
	.driver = {
		.name		= "qcom-pbs",
		.of_match_table	= qcom_pbs_match_table,
	},
	.probe = qcom_pbs_probe,
};
module_platform_driver(qcom_pbs_driver)

MODULE_DESCRIPTION("QCOM PBS DRIVER");
MODULE_LICENSE("GPL");
