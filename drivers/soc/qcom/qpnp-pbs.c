// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2018, 2020-2021, The Linux Foundation.
 * All rights reserved.
 */

#define pr_fmt(fmt)	"PBS: %s: " fmt, __func__

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spmi.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/qpnp/qpnp-pbs.h>

#define QPNP_PBS_DEV_NAME "qcom,qpnp-pbs"

#define PBS_CLIENT_TRIG_CTL		0x42
#define PBS_CLIENT_SW_TRIG_BIT		BIT(7)
#define PBS_CLIENT_SCRATCH1		0x50
#define PBS_CLIENT_SCRATCH2		0x51

static LIST_HEAD(pbs_dev_list);
static DEFINE_MUTEX(pbs_list_lock);

struct qpnp_pbs {
	struct platform_device	*pdev;
	struct device		*dev;
	struct device_node	*dev_node;
	struct regmap		*regmap;
	struct mutex		pbs_lock;
	struct list_head	link;

	u32			base;
};

static int qpnp_pbs_read(struct qpnp_pbs *pbs, u32 address,
					u8 *val, int count)
{
	int rc = 0;
	struct platform_device *pdev = pbs->pdev;

	rc = regmap_bulk_read(pbs->regmap, address, val, count);
	if (rc)
		pr_err("Failed to read address=0x%02x sid=0x%02x rc=%d\n",
			address, to_spmi_device(pdev->dev.parent)->usid, rc);

	return rc;
}

static int qpnp_pbs_write(struct qpnp_pbs *pbs, u16 address,
					u8 *val, int count)
{
	int rc = 0;
	struct platform_device *pdev = pbs->pdev;

	rc = regmap_bulk_write(pbs->regmap, address, val, count);
	if (rc < 0)
		pr_err("Failed to write address =0x%02x sid=0x%02x rc=%d\n",
			  address, to_spmi_device(pdev->dev.parent)->usid, rc);
	else
		pr_debug("Wrote 0x%02X to addr 0x%04x\n", *val, address);

	return rc;
}

static int qpnp_pbs_masked_write(struct qpnp_pbs *pbs, u16 address,
						   u8 mask, u8 val)
{
	int rc;

	rc = regmap_update_bits(pbs->regmap, address, mask, val);
	if (rc < 0)
		pr_err("Failed to write address 0x%04X, rc = %d\n",
					address, rc);
	else
		pr_debug("Wrote 0x%02X to addr 0x%04X\n",
			val, address);

	return rc;
}

static struct qpnp_pbs *get_pbs_client_node(struct device_node *dev_node)
{
	struct qpnp_pbs *pbs;

	mutex_lock(&pbs_list_lock);
	list_for_each_entry(pbs, &pbs_dev_list, link) {
		if (dev_node == pbs->dev_node) {
			mutex_unlock(&pbs_list_lock);
			return pbs;
		}
	}

	mutex_unlock(&pbs_list_lock);
	return ERR_PTR(-EINVAL);
}

static int qpnp_pbs_wait_for_ack(struct qpnp_pbs *pbs, u8 bit_pos)
{
	int rc = 0;
	u16 retries = 2000, dly = 1000;
	u8 val;

	while (retries--) {
		rc = qpnp_pbs_read(pbs, pbs->base +
					PBS_CLIENT_SCRATCH2, &val, 1);
		if (rc < 0) {
			pr_err("Failed to read register %x rc = %d\n",
						PBS_CLIENT_SCRATCH2, rc);
			return rc;
		}

		if (val == 0xFF) {
			val = 0;
			/* PBS error - clear SCRATCH2 register */
			rc = qpnp_pbs_write(pbs, pbs->base +
					PBS_CLIENT_SCRATCH2, &val, 1);
			if (rc < 0) {
				pr_err("Failed to clear register %x rc=%d\n",
						PBS_CLIENT_SCRATCH2, rc);
				return rc;
			}

			pr_err("NACK from PBS for bit %d\n", bit_pos);
			return -EINVAL;
		}

		if (val & BIT(bit_pos)) {
			pr_debug("PBS sequence for bit %d executed!\n",
						 bit_pos);
			break;
		}

		usleep_range(dly, dly + 100);
	}

	if (!retries) {
		pr_err("Timeout for PBS ACK/NACK for bit %d\n", bit_pos);
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * qpnp_pbs_trigger_single_event - trigger PBS sequence which is connected
 * directly to SW_TRIGGER bit without using bitmap.
 *
 * Returns = 0 enable SW_TRIGGER in PBS client successfully.
 *
 * Returns < 0 for errors.
 *
 * This function is used to trigger the PBS that is hooked on the
 * SW_TRIGGER directly in PBS client.
 */
int qpnp_pbs_trigger_single_event(struct device_node *dev_node)
{
	struct qpnp_pbs *pbs_dev;
	int rc;

	if (!dev_node)
		return -EINVAL;

	pbs_dev = get_pbs_client_node(dev_node);
	if (IS_ERR(pbs_dev)) {
		rc = PTR_ERR(pbs_dev);
		pr_err("Unable to find the PBS dev_node, rc=%d\n", rc);
		return rc;
	}

	mutex_lock(&pbs_dev->pbs_lock);
	rc = qpnp_pbs_masked_write(pbs_dev, pbs_dev->base +
				PBS_CLIENT_TRIG_CTL, PBS_CLIENT_SW_TRIG_BIT,
				PBS_CLIENT_SW_TRIG_BIT);
	if (rc < 0)
		pr_err("Failed to write register %x rc=%d\n",
				PBS_CLIENT_TRIG_CTL, rc);
	mutex_unlock(&pbs_dev->pbs_lock);

	return rc;
}
EXPORT_SYMBOL(qpnp_pbs_trigger_single_event);

/**
 * qpnp_pbs_trigger_event - Trigger the PBS RAM sequence
 *
 * Returns = 0 If the PBS RAM sequence executed successfully.
 *
 * Returns < 0 for errors.
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
 */
int qpnp_pbs_trigger_event(struct device_node *dev_node, u8 bitmap)
{
	struct qpnp_pbs *pbs;
	int rc = 0;
	u16 bit_pos = 0;
	u8 val, mask  = 0;

	if (!dev_node)
		return -EINVAL;

	if (!bitmap) {
		pr_err("Invalid bitmap passed by client\n");
		return -EINVAL;
	}

	pbs = get_pbs_client_node(dev_node);
	if (IS_ERR_OR_NULL(pbs)) {
		pr_err("Unable to find the PBS dev_node\n");
		return -EINVAL;
	}

	mutex_lock(&pbs->pbs_lock);
	rc = qpnp_pbs_read(pbs, pbs->base + PBS_CLIENT_SCRATCH2, &val, 1);
	if (rc < 0) {
		pr_err("read register %x failed rc = %d\n",
					PBS_CLIENT_SCRATCH2, rc);
		goto out;
	}

	if (val == 0xFF) {
		val = 0;
		/* PBS error - clear SCRATCH2 register */
		rc = qpnp_pbs_write(pbs, pbs->base + PBS_CLIENT_SCRATCH2, &val,
				    1);
		if (rc < 0) {
			pr_err("Failed to clear register %x rc=%d\n",
						PBS_CLIENT_SCRATCH2, rc);
			goto out;
		}
	}

	for (bit_pos = 0; bit_pos < 8; bit_pos++) {
		if (bitmap & BIT(bit_pos)) {
			/*
			 * Clear the PBS sequence bit position in
			 * PBS_CLIENT_SCRATCH2 mask register.
			 */
			rc = qpnp_pbs_masked_write(pbs, pbs->base +
					 PBS_CLIENT_SCRATCH2, BIT(bit_pos), 0);
			if (rc < 0) {
				pr_err("Failed to clear %x reg bit rc=%d\n",
						PBS_CLIENT_SCRATCH2, rc);
				goto error;
			}

			/*
			 * Set the PBS sequence bit position in
			 * PBS_CLIENT_SCRATCH1 register.
			 */
			val = mask = BIT(bit_pos);
			rc = qpnp_pbs_masked_write(pbs, pbs->base +
						PBS_CLIENT_SCRATCH1, mask, val);
			if (rc < 0) {
				pr_err("Failed to set %x reg bit rc=%d\n",
						PBS_CLIENT_SCRATCH1, rc);
				goto error;
			}

			/* Initiate the SW trigger */
			val = mask = PBS_CLIENT_SW_TRIG_BIT;
			rc = qpnp_pbs_masked_write(pbs, pbs->base +
						PBS_CLIENT_TRIG_CTL, mask, val);
			if (rc < 0) {
				pr_err("Failed to write register %x rc=%d\n",
						PBS_CLIENT_TRIG_CTL, rc);
				goto error;
			}

			rc = qpnp_pbs_wait_for_ack(pbs, bit_pos);
			if (rc < 0) {
				pr_err("Error during wait_for_ack\n");
				goto error;
			}

			/*
			 * Clear the PBS sequence bit position in
			 * PBS_CLIENT_SCRATCH1 register.
			 */
			rc = qpnp_pbs_masked_write(pbs, pbs->base +
					PBS_CLIENT_SCRATCH1, BIT(bit_pos), 0);
			if (rc < 0) {
				pr_err("Failed to clear %x reg bit rc=%d\n",
						PBS_CLIENT_SCRATCH1, rc);
				goto error;
			}

			/*
			 * Clear the PBS sequence bit position in
			 * PBS_CLIENT_SCRATCH2 mask register.
			 */
			rc = qpnp_pbs_masked_write(pbs, pbs->base +
					PBS_CLIENT_SCRATCH2, BIT(bit_pos), 0);
			if (rc < 0) {
				pr_err("Failed to clear %x reg bit rc=%d\n",
						PBS_CLIENT_SCRATCH2, rc);
				goto error;
			}

		}
	}

error:
	/* Clear all the requested bitmap */
	rc = qpnp_pbs_masked_write(pbs, pbs->base + PBS_CLIENT_SCRATCH1,
						bitmap, 0);
	if (rc < 0)
		pr_err("Failed to clear %x reg bit rc=%d\n",
					PBS_CLIENT_SCRATCH1, rc);
out:
	mutex_unlock(&pbs->pbs_lock);

	return rc;
}
EXPORT_SYMBOL(qpnp_pbs_trigger_event);

static int qpnp_pbs_probe(struct platform_device *pdev)
{
	int rc = 0;
	u32 val = 0;
	struct qpnp_pbs *pbs;

	pbs = devm_kzalloc(&pdev->dev, sizeof(*pbs), GFP_KERNEL);
	if (!pbs)
		return -ENOMEM;

	pbs->pdev = pdev;
	pbs->dev = &pdev->dev;
	pbs->dev_node = pdev->dev.of_node;
	pbs->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!pbs->regmap) {
		dev_err(&pdev->dev, "Couldn't get parent's regmap\n");
		return -EINVAL;
	}

	rc = of_property_read_u32(pdev->dev.of_node, "reg", &val);
	if (rc < 0) {
		dev_err(&pdev->dev,
			"Couldn't find reg in node = %s rc = %d\n",
			pdev->dev.of_node->full_name, rc);
		return rc;
	}

	pbs->base = val;
	mutex_init(&pbs->pbs_lock);

	dev_set_drvdata(&pdev->dev, pbs);

	mutex_lock(&pbs_list_lock);
	list_add(&pbs->link, &pbs_dev_list);
	mutex_unlock(&pbs_list_lock);

	return 0;
}

static const struct of_device_id qpnp_pbs_match_table[] = {
	{ .compatible = QPNP_PBS_DEV_NAME },
	{}
};

static struct platform_driver qpnp_pbs_driver = {
	.driver	= {
		.name		= QPNP_PBS_DEV_NAME,
		.of_match_table	= qpnp_pbs_match_table,
	},
	.probe	= qpnp_pbs_probe,
};

static int __init qpnp_pbs_init(void)
{
	return platform_driver_register(&qpnp_pbs_driver);
}
arch_initcall(qpnp_pbs_init);

static void __exit qpnp_pbs_exit(void)
{
	return platform_driver_unregister(&qpnp_pbs_driver);
}
module_exit(qpnp_pbs_exit);

MODULE_DESCRIPTION("QPNP PBS DRIVER");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" QPNP_PBS_DEV_NAME);
