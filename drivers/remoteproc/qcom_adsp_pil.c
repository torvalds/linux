/*
 * Qualcomm ADSP Peripheral Image Loader for MSM8974 and MSM8996
 *
 * Copyright (C) 2016 Linaro Ltd
 * Copyright (C) 2014 Sony Mobile Communications AB
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/qcom_scm.h>
#include <linux/regulator/consumer.h>
#include <linux/remoteproc.h>
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/smem_state.h>

#include "qcom_mdt_loader.h"
#include "remoteproc_internal.h"

#define ADSP_CRASH_REASON_SMEM		423
#define ADSP_FIRMWARE_NAME		"adsp.mdt"
#define ADSP_PAS_ID			1

struct qcom_adsp {
	struct device *dev;
	struct rproc *rproc;

	int wdog_irq;
	int fatal_irq;
	int ready_irq;
	int handover_irq;
	int stop_ack_irq;

	struct qcom_smem_state *state;
	unsigned stop_bit;

	struct clk *xo;

	struct regulator *cx_supply;

	struct completion start_done;
	struct completion stop_done;

	phys_addr_t mem_phys;
	phys_addr_t mem_reloc;
	void *mem_region;
	size_t mem_size;
};

static int adsp_load(struct rproc *rproc, const struct firmware *fw)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;
	phys_addr_t fw_addr;
	size_t fw_size;
	bool relocate;
	int ret;

	ret = qcom_scm_pas_init_image(ADSP_PAS_ID, fw->data, fw->size);
	if (ret) {
		dev_err(&rproc->dev, "invalid firmware metadata\n");
		return ret;
	}

	ret = qcom_mdt_parse(fw, &fw_addr, &fw_size, &relocate);
	if (ret) {
		dev_err(&rproc->dev, "failed to parse mdt header\n");
		return ret;
	}

	if (relocate) {
		adsp->mem_reloc = fw_addr;

		ret = qcom_scm_pas_mem_setup(ADSP_PAS_ID, adsp->mem_phys, fw_size);
		if (ret) {
			dev_err(&rproc->dev, "unable to setup memory for image\n");
			return ret;
		}
	}

	return qcom_mdt_load(rproc, fw, rproc->firmware);
}

static const struct rproc_fw_ops adsp_fw_ops = {
	.find_rsc_table = qcom_mdt_find_rsc_table,
	.load = adsp_load,
};

static int adsp_start(struct rproc *rproc)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;
	int ret;

	ret = clk_prepare_enable(adsp->xo);
	if (ret)
		return ret;

	ret = regulator_enable(adsp->cx_supply);
	if (ret)
		goto disable_clocks;

	ret = qcom_scm_pas_auth_and_reset(ADSP_PAS_ID);
	if (ret) {
		dev_err(adsp->dev,
			"failed to authenticate image and release reset\n");
		goto disable_regulators;
	}

	ret = wait_for_completion_timeout(&adsp->start_done,
					  msecs_to_jiffies(5000));
	if (!ret) {
		dev_err(adsp->dev, "start timed out\n");
		qcom_scm_pas_shutdown(ADSP_PAS_ID);
		ret = -ETIMEDOUT;
		goto disable_regulators;
	}

	ret = 0;

disable_regulators:
	regulator_disable(adsp->cx_supply);
disable_clocks:
	clk_disable_unprepare(adsp->xo);

	return ret;
}

static int adsp_stop(struct rproc *rproc)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;
	int ret;

	qcom_smem_state_update_bits(adsp->state,
				    BIT(adsp->stop_bit),
				    BIT(adsp->stop_bit));

	ret = wait_for_completion_timeout(&adsp->stop_done,
					  msecs_to_jiffies(5000));
	if (ret == 0)
		dev_err(adsp->dev, "timed out on wait\n");

	qcom_smem_state_update_bits(adsp->state,
				    BIT(adsp->stop_bit),
				    0);

	ret = qcom_scm_pas_shutdown(ADSP_PAS_ID);
	if (ret)
		dev_err(adsp->dev, "failed to shutdown: %d\n", ret);

	return ret;
}

static void *adsp_da_to_va(struct rproc *rproc, u64 da, int len)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;
	int offset;

	offset = da - adsp->mem_reloc;
	if (offset < 0 || offset + len > adsp->mem_size)
		return NULL;

	return adsp->mem_region + offset;
}

static const struct rproc_ops adsp_ops = {
	.start = adsp_start,
	.stop = adsp_stop,
	.da_to_va = adsp_da_to_va,
};

static irqreturn_t adsp_wdog_interrupt(int irq, void *dev)
{
	struct qcom_adsp *adsp = dev;

	rproc_report_crash(adsp->rproc, RPROC_WATCHDOG);

	return IRQ_HANDLED;
}

static irqreturn_t adsp_fatal_interrupt(int irq, void *dev)
{
	struct qcom_adsp *adsp = dev;
	size_t len;
	char *msg;

	msg = qcom_smem_get(QCOM_SMEM_HOST_ANY, ADSP_CRASH_REASON_SMEM, &len);
	if (!IS_ERR(msg) && len > 0 && msg[0])
		dev_err(adsp->dev, "fatal error received: %s\n", msg);

	rproc_report_crash(adsp->rproc, RPROC_FATAL_ERROR);

	if (!IS_ERR(msg))
		msg[0] = '\0';

	return IRQ_HANDLED;
}

static irqreturn_t adsp_ready_interrupt(int irq, void *dev)
{
	return IRQ_HANDLED;
}

static irqreturn_t adsp_handover_interrupt(int irq, void *dev)
{
	struct qcom_adsp *adsp = dev;

	complete(&adsp->start_done);

	return IRQ_HANDLED;
}

static irqreturn_t adsp_stop_ack_interrupt(int irq, void *dev)
{
	struct qcom_adsp *adsp = dev;

	complete(&adsp->stop_done);

	return IRQ_HANDLED;
}

static int adsp_init_clock(struct qcom_adsp *adsp)
{
	int ret;

	adsp->xo = devm_clk_get(adsp->dev, "xo");
	if (IS_ERR(adsp->xo)) {
		ret = PTR_ERR(adsp->xo);
		if (ret != -EPROBE_DEFER)
			dev_err(adsp->dev, "failed to get xo clock");
		return ret;
	}

	return 0;
}

static int adsp_init_regulator(struct qcom_adsp *adsp)
{
	adsp->cx_supply = devm_regulator_get(adsp->dev, "cx");
	if (IS_ERR(adsp->cx_supply))
		return PTR_ERR(adsp->cx_supply);

	regulator_set_load(adsp->cx_supply, 100000);

	return 0;
}

static int adsp_request_irq(struct qcom_adsp *adsp,
			     struct platform_device *pdev,
			     const char *name,
			     irq_handler_t thread_fn)
{
	int ret;

	ret = platform_get_irq_byname(pdev, name);
	if (ret < 0) {
		dev_err(&pdev->dev, "no %s IRQ defined\n", name);
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, ret,
					NULL, thread_fn,
					IRQF_ONESHOT,
					"adsp", adsp);
	if (ret)
		dev_err(&pdev->dev, "request %s IRQ failed\n", name);

	return ret;
}

static int adsp_alloc_memory_region(struct qcom_adsp *adsp)
{
	struct device_node *node;
	struct resource r;
	int ret;

	node = of_parse_phandle(adsp->dev->of_node, "memory-region", 0);
	if (!node) {
		dev_err(adsp->dev, "no memory-region specified\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &r);
	if (ret)
		return ret;

	adsp->mem_phys = adsp->mem_reloc = r.start;
	adsp->mem_size = resource_size(&r);
	adsp->mem_region = devm_ioremap_wc(adsp->dev, adsp->mem_phys, adsp->mem_size);
	if (!adsp->mem_region) {
		dev_err(adsp->dev, "unable to map memory region: %pa+%zx\n",
			&r.start, adsp->mem_size);
		return -EBUSY;
	}

	return 0;
}

static int adsp_probe(struct platform_device *pdev)
{
	struct qcom_adsp *adsp;
	struct rproc *rproc;
	int ret;

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;

	if (!qcom_scm_pas_supported(ADSP_PAS_ID)) {
		dev_err(&pdev->dev, "PAS is not available for ADSP\n");
		return -ENXIO;
	}

	rproc = rproc_alloc(&pdev->dev, pdev->name, &adsp_ops,
			    ADSP_FIRMWARE_NAME, sizeof(*adsp));
	if (!rproc) {
		dev_err(&pdev->dev, "unable to allocate remoteproc\n");
		return -ENOMEM;
	}

	rproc->fw_ops = &adsp_fw_ops;

	adsp = (struct qcom_adsp *)rproc->priv;
	adsp->dev = &pdev->dev;
	adsp->rproc = rproc;
	platform_set_drvdata(pdev, adsp);

	init_completion(&adsp->start_done);
	init_completion(&adsp->stop_done);

	ret = adsp_alloc_memory_region(adsp);
	if (ret)
		goto free_rproc;

	ret = adsp_init_clock(adsp);
	if (ret)
		goto free_rproc;

	ret = adsp_init_regulator(adsp);
	if (ret)
		goto free_rproc;

	ret = adsp_request_irq(adsp, pdev, "wdog", adsp_wdog_interrupt);
	if (ret < 0)
		goto free_rproc;
	adsp->wdog_irq = ret;

	ret = adsp_request_irq(adsp, pdev, "fatal", adsp_fatal_interrupt);
	if (ret < 0)
		goto free_rproc;
	adsp->fatal_irq = ret;

	ret = adsp_request_irq(adsp, pdev, "ready", adsp_ready_interrupt);
	if (ret < 0)
		goto free_rproc;
	adsp->ready_irq = ret;

	ret = adsp_request_irq(adsp, pdev, "handover", adsp_handover_interrupt);
	if (ret < 0)
		goto free_rproc;
	adsp->handover_irq = ret;

	ret = adsp_request_irq(adsp, pdev, "stop-ack", adsp_stop_ack_interrupt);
	if (ret < 0)
		goto free_rproc;
	adsp->stop_ack_irq = ret;

	adsp->state = qcom_smem_state_get(&pdev->dev, "stop",
					  &adsp->stop_bit);
	if (IS_ERR(adsp->state)) {
		ret = PTR_ERR(adsp->state);
		goto free_rproc;
	}

	ret = rproc_add(rproc);
	if (ret)
		goto free_rproc;

	return 0;

free_rproc:
	rproc_free(rproc);

	return ret;
}

static int adsp_remove(struct platform_device *pdev)
{
	struct qcom_adsp *adsp = platform_get_drvdata(pdev);

	qcom_smem_state_put(adsp->state);
	rproc_del(adsp->rproc);
	rproc_free(adsp->rproc);

	return 0;
}

static const struct of_device_id adsp_of_match[] = {
	{ .compatible = "qcom,msm8974-adsp-pil" },
	{ .compatible = "qcom,msm8996-adsp-pil" },
	{ },
};
MODULE_DEVICE_TABLE(of, adsp_of_match);

static struct platform_driver adsp_driver = {
	.probe = adsp_probe,
	.remove = adsp_remove,
	.driver = {
		.name = "qcom_adsp_pil",
		.of_match_table = adsp_of_match,
	},
};

module_platform_driver(adsp_driver);
MODULE_DESCRIPTION("Qualcomm MSM8974/MSM8996 ADSP Peripherial Image Loader");
MODULE_LICENSE("GPL v2");
