// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "hgsl.h"

#define GMUGOS_REG_SET    (0x0)
#define GMUGOS_REG_STATUS (0x4)
#define GMUGOS_REG_CLR    (0x8)
#define GMUGOS_REG_MASK   (0xC)

u32 hgsl_regmap_read(struct regmap *regmap, u32 offset)
{
	u32 val;

	regmap_read(regmap, offset, &val);
	/* Ensure all previous reads has completed before return */
	rmb();
	return val;
}

void hgsl_regmap_write(struct regmap *regmap, u32 value, u32 offset)
{
	/* Ensure all previous writes has completed */
	wmb();
	regmap_write(regmap, offset, value);
}

static irqreturn_t hgsl_gmugos_ts_retire(int num, void *data)
{
	struct hgsl_gmugos_irq *gmugos_irq = data;
	struct hgsl_gmugos *gmugos = container_of(gmugos_irq,
					typeof(*gmugos), irq[gmugos_irq->id]);
	u32 dev_id = hgsl_hnd2id(gmugos->dev_hnd);

	if (dev_id >= HGSL_DEVICE_NUM) {
		pr_warn_ratelimited("Invalid dev handle %u",
			gmugos->dev_hnd);
		return IRQ_HANDLED;
	}

	hgsl_retire_common
		(container_of(gmugos, typeof(struct qcom_hgsl),
			gmugos[dev_id]),
		gmugos->dev_hnd);
	return IRQ_HANDLED;
}

static irqreturn_t hgsl_gmugos_isr(int num, void *data)
{
	struct hgsl_gmugos_irq *gmugos_irq = data;
	u32 val;

	val = hgsl_regmap_read(gmugos_irq->regmap, GMUGOS_REG_STATUS);
	hgsl_regmap_write(gmugos_irq->regmap, GMUGOS_REG_CLR, val);

	return IRQ_HANDLED;
}

static int hgsl_gmugos_request_irq(struct platform_device *pdev, const  char *name,
		irq_handler_t handler, irq_handler_t thread_fn, void *data)
{
	int ret, num = platform_get_irq_byname(pdev, name);

	if (num < 0)
		return num;

	ret = devm_request_threaded_irq(&pdev->dev, num, handler,
		thread_fn, IRQF_TRIGGER_HIGH, name, data);

	if (ret) {
		dev_err(&pdev->dev, "Unable to get interrupt %s: %d\n",
			name, ret);
		return ret;
	}

	disable_irq(num);
	return num;
}

static int hgsl_syscon_node_regmap(struct hgsl_gmugos *gmugos,
				u32 dev_id, u32 irq_idx)
{
	char syscon_name[HGSL_GMUGOS_NAME_LEN];
	struct regmap *regmap;
	int ret = 0;

	if (!gmugos) {
		LOGE("Invalid gmugos");
		return -EINVAL;
	}

	if (gmugos->irq[irq_idx].regmap)
		goto out;

	snprintf(syscon_name, sizeof(syscon_name),
			"qcom,hgsl-gmugos%u-ipc%u", dev_id, irq_idx);
	regmap = syscon_regmap_lookup_by_compatible(syscon_name);
	if (IS_ERR_OR_NULL(regmap)) {
		LOGE("failed to regmap node\n");
		ret = PTR_ERR(regmap);
		goto out;
	}

	gmugos->irq[irq_idx].regmap = regmap;
out:
	return ret;
}

void hgsl_gmugos_irq_enable(
	struct hgsl_gmugos_irq *gmugos_irq,
	u32 mask_bits)
{
	u32 val;

	if (!gmugos_irq) {
		LOGE("Invalid gmugos_irq");
		return;
	}

	/* Clear pending IRQs */
	hgsl_regmap_write(gmugos_irq->regmap, GMUGOS_REG_CLR,
			mask_bits);

	/* Mask needed IRQs */
	val = hgsl_regmap_read(gmugos_irq->regmap, GMUGOS_REG_MASK);
	hgsl_regmap_write(gmugos_irq->regmap, GMUGOS_REG_MASK,
			val | mask_bits);

	/* Enable IRQ */
	if (gmugos_irq->num)
		enable_irq(gmugos_irq->num);
}

void hgsl_gmugos_irq_disable(
	struct hgsl_gmugos_irq *gmugos_irq,
	u32 mask_bits)
{
	u32 val;

	if (!gmugos_irq) {
		LOGE("Invalid gmugos_irq");
		return;
	}

	/* Disable IRQ */
	if (gmugos_irq->num)
		disable_irq(gmugos_irq->num);

	/* Unmask IRQs */
	val = hgsl_regmap_read(gmugos_irq->regmap, GMUGOS_REG_MASK);
	hgsl_regmap_write(gmugos_irq->regmap, GMUGOS_REG_MASK,
			val & ~mask_bits);

	/* Clear pending IRQs */
	hgsl_regmap_write(gmugos_irq->regmap, GMUGOS_REG_CLR,
			mask_bits);
}

void hgsl_gmugos_irq_trigger(struct hgsl_gmugos *gmugos,
				u32 id)
{
	if (!gmugos || id >= HGSL_GMUGOS_IRQ_NUM) {
		LOGE("Invalid gmugos or Invalid irq index %u", id);
		return;
	}

	hgsl_regmap_write(gmugos->irq[id].regmap,
		GMUGOS_REG_SET, BIT(id));
}

void hgsl_gmugos_irq_free(struct hgsl_gmugos_irq *irq)
{
	if (irq->num)
		free_irq(irq->num, irq);

	irq->num = 0;
}

int hgsl_init_gmugos(struct platform_device *pdev,
				struct hgsl_context *ctxt,
				u32 irq_idx)
{
	struct qcom_hgsl *hgsl = platform_get_drvdata(pdev);
	struct hgsl_gmugos *gmugos;
	struct hgsl_gmugos_irq *gmugos_irq;
	char irq_name[HGSL_GMUGOS_NAME_LEN];
	int ret = 0;
	u32 dev_id = hgsl_hnd2id(ctxt->devhandle);

	if (dev_id >= HGSL_DEVICE_NUM) {
		dev_err(&pdev->dev, "Invalid dev handle %u\n", ctxt->devhandle);
		return -EFAULT;
	}

	if (irq_idx >= HGSL_GMUGOS_IRQ_NUM) {
		dev_err(&pdev->dev, "Invalid irq index %u\n", irq_idx);
		ret = -EFAULT;
		goto out;
	}

	mutex_lock(&hgsl->mutex);
	gmugos = &hgsl->gmugos[dev_id];
	ret = hgsl_syscon_node_regmap(gmugos, dev_id, irq_idx);
	if (ret)
		goto out;

	gmugos->dev_hnd = ctxt->devhandle;
	gmugos_irq = &gmugos->irq[irq_idx];
	if (gmugos_irq->num)
		goto out;

	snprintf(irq_name, sizeof(irq_name), "hgsl_gmugos%u_irq%u",
			dev_id, irq_idx);
	ret = hgsl_gmugos_request_irq(pdev, irq_name, hgsl_gmugos_isr,
				hgsl_gmugos_ts_retire, gmugos_irq);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request gmugos irq %s, irq %u\n",
			irq_name, ret);
		goto out;
	}
	gmugos_irq->id = irq_idx;
	gmugos_irq->num = ret;

	hgsl_gmugos_irq_enable(gmugos_irq, GMUGOS_IRQ_MASK);
out:
	mutex_unlock(&hgsl->mutex);
	return ret;
}

