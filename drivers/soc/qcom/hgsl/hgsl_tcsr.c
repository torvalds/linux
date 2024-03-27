// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "hgsl_tcsr.h"

/* Sender registers */
#define TCSR_GLB_CFG_COMPUTE_SIGNALING_REG	0x000
#define TCSR_COMPUTE_SIGNALING_REG		0x000

/* Receiver registers */
#define TCSR_COMPUTE_SIGNAL_STATUS_REG		0x000
#define TCSR_COMPUTE_SIGNAL_CLEAR_REG		0x400
#define TCSR_COMPUTE_SIGNAL_MASK_REG		0x800

struct hgsl_tcsr {
	struct platform_device *pdev;
	struct device *client_dev;

	struct regmap *regmap;
	struct regmap *glb_regmap;

	enum hgsl_tcsr_role role;
	unsigned int enable_count;
	struct mutex dev_mutex;

	unsigned int irq_num;
	irqreturn_t (*isr)(struct device *dev, u32 status);
};

static irqreturn_t hgsl_tcsr_isr(int irq, void *ptr)
{
	struct hgsl_tcsr *tcsr = ptr;
	u32 status;

	regmap_read(tcsr->regmap, TCSR_COMPUTE_SIGNAL_STATUS_REG, &status);
	regmap_write(tcsr->regmap, TCSR_COMPUTE_SIGNAL_CLEAR_REG, status);

	if (tcsr->isr)
		return tcsr->isr(tcsr->client_dev, status);
	else
		return IRQ_HANDLED;
}

static int hgsl_tcsr_init_sender(struct hgsl_tcsr *tcsr)
{
	struct device *dev = &tcsr->pdev->dev;
	struct device_node *np = dev->of_node;

	if (tcsr->glb_regmap != NULL)
		return 0;

	tcsr->regmap = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR_OR_NULL(tcsr->regmap)) {
		dev_err(dev, "failed to map sender register\n");
		return -ENODEV;
	}

	tcsr->glb_regmap = syscon_regmap_lookup_by_phandle(np, "syscon-glb");
	if (IS_ERR_OR_NULL(tcsr->glb_regmap)) {
		dev_err(dev, "failed to map sender global register\n");
		tcsr->glb_regmap = NULL;
		return -ENODEV;
	}

	return 0;
}

static int hgsl_tcsr_init_receiver(struct hgsl_tcsr *tcsr)
{
	struct device *dev = &tcsr->pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	if (tcsr->irq_num != 0)
		return 0;

	tcsr->regmap = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR_OR_NULL(tcsr->regmap)) {
		dev_err(dev, "failed to map receiver register\n");
		return -ENODEV;
	}

	tcsr->irq_num = irq_of_parse_and_map(np, 0);
	if (tcsr->irq_num == 0) {
		dev_err(dev, "failed to get irq\n");
		return -ENODEV;
	}

	ret = request_irq(tcsr->irq_num, hgsl_tcsr_isr,
			IRQF_TRIGGER_HIGH, "hgsl-tcsr", tcsr);
	if (ret < 0) {
		dev_err(dev, "failed to request IRQ%u: %d\n",
				tcsr->irq_num, ret);
		tcsr->irq_num = 0;
		return ret;
	}

	disable_irq(tcsr->irq_num);

	return 0;
}

#if IS_ENABLED(CONFIG_QCOM_HGSL_TCSR_SIGNAL)
struct hgsl_tcsr *hgsl_tcsr_request(struct platform_device *pdev,
				enum hgsl_tcsr_role role,
				struct device *client,
				irqreturn_t (*isr)(struct device *, u32))
{
	struct hgsl_tcsr *tcsr = platform_get_drvdata(pdev);
	int ret = -EINVAL;

	if (!tcsr)
		return ERR_PTR(-ENODEV);
	else if (tcsr->role != role)
		return ERR_PTR(-EINVAL);
	else if (tcsr->client_dev)
		return ERR_PTR(-EBUSY);

	if (role == HGSL_TCSR_ROLE_RECEIVER) {
		if (tcsr->isr)
			return ERR_PTR(-EBUSY);
		else if (!isr)
			return ERR_PTR(-EINVAL);

		tcsr->client_dev = client;
		tcsr->isr = isr;

		ret = hgsl_tcsr_init_receiver(tcsr);
	} else { /* HGSL_TCSR_ROLE_SENDER */
		if (isr)
			return ERR_PTR(-EINVAL);

		tcsr->client_dev = client;
		ret = hgsl_tcsr_init_sender(tcsr);
	}

	if (ret) {
		tcsr = ERR_PTR(ret);
		tcsr->client_dev = NULL;
		tcsr->isr = NULL;
	}

	return tcsr;
}

void hgsl_tcsr_free(struct hgsl_tcsr *tcsr)
{
	if ((tcsr->role == HGSL_TCSR_ROLE_RECEIVER) &&
		(tcsr->irq_num != 0) && (tcsr->isr != NULL))
		free_irq(tcsr->irq_num, tcsr);

	tcsr->client_dev = NULL;
	tcsr->isr = NULL;
}

int hgsl_tcsr_enable(struct hgsl_tcsr *tcsr)
{
	mutex_lock(&tcsr->dev_mutex);
	if (tcsr->enable_count > 0)
		goto done;

	if (tcsr->irq_num)
		enable_irq(tcsr->irq_num);

done:
	tcsr->enable_count++;
	mutex_unlock(&tcsr->dev_mutex);
	return 0;
}

void hgsl_tcsr_disable(struct hgsl_tcsr *tcsr)
{
	mutex_lock(&tcsr->dev_mutex);
	if (--tcsr->enable_count > 0)
		goto done;

	if (tcsr->irq_num)
		disable_irq(tcsr->irq_num);

done:
	mutex_unlock(&tcsr->dev_mutex);
}

bool hgsl_tcsr_is_enabled(struct hgsl_tcsr *tcsr)
{
	return (tcsr->enable_count > 0);
}

void hgsl_tcsr_irq_trigger(struct hgsl_tcsr *tcsr, int irq_id)
{
	u32 reg;

	/*
	 * Read back this global config register in case
	 * it has been modified by others.
	 */
	regmap_read(tcsr->glb_regmap,
			TCSR_GLB_CFG_COMPUTE_SIGNALING_REG, &reg);
	reg = irq_id << reg;
	regmap_write(tcsr->regmap, TCSR_COMPUTE_SIGNALING_REG, reg);
}

void hgsl_tcsr_irq_enable(struct hgsl_tcsr *tcsr, u32 mask, bool enable)
{
	u32 reg;

	regmap_read(tcsr->regmap, TCSR_COMPUTE_SIGNAL_MASK_REG, &reg);
	reg = enable ? (reg | mask) : (reg & ~mask);
	regmap_write(tcsr->regmap, TCSR_COMPUTE_SIGNAL_MASK_REG, reg);
}
#endif

static const struct of_device_id hgsl_tcsr_match_table[] = {
	{ .compatible = "qcom,hgsl-tcsr-sender" },
	{ .compatible = "qcom,hgsl-tcsr-receiver" },
	{}
};

static int hgsl_tcsr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct hgsl_tcsr *tcsr = devm_kzalloc(dev, sizeof(*tcsr),
						GFP_KERNEL);

	if (!tcsr)
		return -ENOMEM;

	if (of_device_is_compatible(np, "qcom,hgsl-tcsr-receiver")) {
		tcsr->role = HGSL_TCSR_ROLE_RECEIVER;
	} else if (of_device_is_compatible(np, "qcom,hgsl-tcsr-sender")) {
		tcsr->role = HGSL_TCSR_ROLE_SENDER;
	} else {
		dev_err(dev, "Not compatible device\n");
		return -ENODEV;
	}

	mutex_init(&tcsr->dev_mutex);
	tcsr->pdev = pdev;
	platform_set_drvdata(pdev, tcsr);

	return 0;
}

static int hgsl_tcsr_remove(struct platform_device *pdev)
{
	struct hgsl_tcsr *tcsr = platform_get_drvdata(pdev);

	mutex_destroy(&tcsr->dev_mutex);

	return 0;
}

struct platform_driver hgsl_tcsr_driver = {
	.probe = hgsl_tcsr_probe,
	.remove = hgsl_tcsr_remove,
	.driver = {
		.name = "hgsl-tcsr",
		.of_match_table = hgsl_tcsr_match_table,
	}
};
