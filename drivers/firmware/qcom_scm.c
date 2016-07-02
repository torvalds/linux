/* Copyright (c) 2010,2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015 Linaro Ltd.
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
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/cpumask.h>
#include <linux/export.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>
#include <linux/qcom_scm.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/reset-controller.h>

#include "qcom_scm.h"

struct qcom_scm {
	struct device *dev;
	struct clk *core_clk;
	struct clk *iface_clk;
	struct clk *bus_clk;
	struct reset_controller_dev reset;
};

static struct qcom_scm *__scm;

static int qcom_scm_clk_enable(void)
{
	int ret;

	ret = clk_prepare_enable(__scm->core_clk);
	if (ret)
		goto bail;

	ret = clk_prepare_enable(__scm->iface_clk);
	if (ret)
		goto disable_core;

	ret = clk_prepare_enable(__scm->bus_clk);
	if (ret)
		goto disable_iface;

	return 0;

disable_iface:
	clk_disable_unprepare(__scm->iface_clk);
disable_core:
	clk_disable_unprepare(__scm->core_clk);
bail:
	return ret;
}

static void qcom_scm_clk_disable(void)
{
	clk_disable_unprepare(__scm->core_clk);
	clk_disable_unprepare(__scm->iface_clk);
	clk_disable_unprepare(__scm->bus_clk);
}

/**
 * qcom_scm_set_cold_boot_addr() - Set the cold boot address for cpus
 * @entry: Entry point function for the cpus
 * @cpus: The cpumask of cpus that will use the entry point
 *
 * Set the cold boot address of the cpus. Any cpu outside the supported
 * range would be removed from the cpu present mask.
 */
int qcom_scm_set_cold_boot_addr(void *entry, const cpumask_t *cpus)
{
	return __qcom_scm_set_cold_boot_addr(entry, cpus);
}
EXPORT_SYMBOL(qcom_scm_set_cold_boot_addr);

/**
 * qcom_scm_set_warm_boot_addr() - Set the warm boot address for cpus
 * @entry: Entry point function for the cpus
 * @cpus: The cpumask of cpus that will use the entry point
 *
 * Set the Linux entry point for the SCM to transfer control to when coming
 * out of a power down. CPU power down may be executed on cpuidle or hotplug.
 */
int qcom_scm_set_warm_boot_addr(void *entry, const cpumask_t *cpus)
{
	return __qcom_scm_set_warm_boot_addr(__scm->dev, entry, cpus);
}
EXPORT_SYMBOL(qcom_scm_set_warm_boot_addr);

/**
 * qcom_scm_cpu_power_down() - Power down the cpu
 * @flags - Flags to flush cache
 *
 * This is an end point to power down cpu. If there was a pending interrupt,
 * the control would return from this function, otherwise, the cpu jumps to the
 * warm boot entry point set for this cpu upon reset.
 */
void qcom_scm_cpu_power_down(u32 flags)
{
	__qcom_scm_cpu_power_down(flags);
}
EXPORT_SYMBOL(qcom_scm_cpu_power_down);

/**
 * qcom_scm_hdcp_available() - Check if secure environment supports HDCP.
 *
 * Return true if HDCP is supported, false if not.
 */
bool qcom_scm_hdcp_available(void)
{
	int ret = qcom_scm_clk_enable();

	if (ret)
		return ret;

	ret = __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_HDCP,
						QCOM_SCM_CMD_HDCP);

	qcom_scm_clk_disable();

	return ret > 0 ? true : false;
}
EXPORT_SYMBOL(qcom_scm_hdcp_available);

/**
 * qcom_scm_hdcp_req() - Send HDCP request.
 * @req: HDCP request array
 * @req_cnt: HDCP request array count
 * @resp: response buffer passed to SCM
 *
 * Write HDCP register(s) through SCM.
 */
int qcom_scm_hdcp_req(struct qcom_scm_hdcp_req *req, u32 req_cnt, u32 *resp)
{
	int ret = qcom_scm_clk_enable();

	if (ret)
		return ret;

	ret = __qcom_scm_hdcp_req(__scm->dev, req, req_cnt, resp);
	qcom_scm_clk_disable();
	return ret;
}
EXPORT_SYMBOL(qcom_scm_hdcp_req);

/**
 * qcom_scm_pas_supported() - Check if the peripheral authentication service is
 *			      available for the given peripherial
 * @peripheral:	peripheral id
 *
 * Returns true if PAS is supported for this peripheral, otherwise false.
 */
bool qcom_scm_pas_supported(u32 peripheral)
{
	int ret;

	ret = __qcom_scm_is_call_available(__scm->dev, QCOM_SCM_SVC_PIL,
					   QCOM_SCM_PAS_IS_SUPPORTED_CMD);
	if (ret <= 0)
		return false;

	return __qcom_scm_pas_supported(__scm->dev, peripheral);
}
EXPORT_SYMBOL(qcom_scm_pas_supported);

/**
 * qcom_scm_pas_init_image() - Initialize peripheral authentication service
 *			       state machine for a given peripheral, using the
 *			       metadata
 * @peripheral: peripheral id
 * @metadata:	pointer to memory containing ELF header, program header table
 *		and optional blob of data used for authenticating the metadata
 *		and the rest of the firmware
 * @size:	size of the metadata
 *
 * Returns 0 on success.
 */
int qcom_scm_pas_init_image(u32 peripheral, const void *metadata, size_t size)
{
	dma_addr_t mdata_phys;
	void *mdata_buf;
	int ret;

	/*
	 * During the scm call memory protection will be enabled for the meta
	 * data blob, so make sure it's physically contiguous, 4K aligned and
	 * non-cachable to avoid XPU violations.
	 */
	mdata_buf = dma_alloc_coherent(__scm->dev, size, &mdata_phys,
				       GFP_KERNEL);
	if (!mdata_buf) {
		dev_err(__scm->dev, "Allocation of metadata buffer failed.\n");
		return -ENOMEM;
	}
	memcpy(mdata_buf, metadata, size);

	ret = qcom_scm_clk_enable();
	if (ret)
		goto free_metadata;

	ret = __qcom_scm_pas_init_image(__scm->dev, peripheral, mdata_phys);

	qcom_scm_clk_disable();

free_metadata:
	dma_free_coherent(__scm->dev, size, mdata_buf, mdata_phys);

	return ret;
}
EXPORT_SYMBOL(qcom_scm_pas_init_image);

/**
 * qcom_scm_pas_mem_setup() - Prepare the memory related to a given peripheral
 *			      for firmware loading
 * @peripheral:	peripheral id
 * @addr:	start address of memory area to prepare
 * @size:	size of the memory area to prepare
 *
 * Returns 0 on success.
 */
int qcom_scm_pas_mem_setup(u32 peripheral, phys_addr_t addr, phys_addr_t size)
{
	int ret;

	ret = qcom_scm_clk_enable();
	if (ret)
		return ret;

	ret = __qcom_scm_pas_mem_setup(__scm->dev, peripheral, addr, size);
	qcom_scm_clk_disable();

	return ret;
}
EXPORT_SYMBOL(qcom_scm_pas_mem_setup);

/**
 * qcom_scm_pas_auth_and_reset() - Authenticate the given peripheral firmware
 *				   and reset the remote processor
 * @peripheral:	peripheral id
 *
 * Return 0 on success.
 */
int qcom_scm_pas_auth_and_reset(u32 peripheral)
{
	int ret;

	ret = qcom_scm_clk_enable();
	if (ret)
		return ret;

	ret = __qcom_scm_pas_auth_and_reset(__scm->dev, peripheral);
	qcom_scm_clk_disable();

	return ret;
}
EXPORT_SYMBOL(qcom_scm_pas_auth_and_reset);

/**
 * qcom_scm_pas_shutdown() - Shut down the remote processor
 * @peripheral: peripheral id
 *
 * Returns 0 on success.
 */
int qcom_scm_pas_shutdown(u32 peripheral)
{
	int ret;

	ret = qcom_scm_clk_enable();
	if (ret)
		return ret;

	ret = __qcom_scm_pas_shutdown(__scm->dev, peripheral);
	qcom_scm_clk_disable();

	return ret;
}
EXPORT_SYMBOL(qcom_scm_pas_shutdown);

static int qcom_scm_pas_reset_assert(struct reset_controller_dev *rcdev,
				     unsigned long idx)
{
	if (idx != 0)
		return -EINVAL;

	return __qcom_scm_pas_mss_reset(__scm->dev, 1);
}

static int qcom_scm_pas_reset_deassert(struct reset_controller_dev *rcdev,
				       unsigned long idx)
{
	if (idx != 0)
		return -EINVAL;

	return __qcom_scm_pas_mss_reset(__scm->dev, 0);
}

static const struct reset_control_ops qcom_scm_pas_reset_ops = {
	.assert = qcom_scm_pas_reset_assert,
	.deassert = qcom_scm_pas_reset_deassert,
};

/**
 * qcom_scm_is_available() - Checks if SCM is available
 */
bool qcom_scm_is_available(void)
{
	return !!__scm;
}
EXPORT_SYMBOL(qcom_scm_is_available);

static int qcom_scm_probe(struct platform_device *pdev)
{
	struct qcom_scm *scm;
	int ret;

	scm = devm_kzalloc(&pdev->dev, sizeof(*scm), GFP_KERNEL);
	if (!scm)
		return -ENOMEM;

	scm->core_clk = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(scm->core_clk)) {
		if (PTR_ERR(scm->core_clk) == -EPROBE_DEFER)
			return PTR_ERR(scm->core_clk);

		scm->core_clk = NULL;
	}

	if (of_device_is_compatible(pdev->dev.of_node, "qcom,scm")) {
		scm->iface_clk = devm_clk_get(&pdev->dev, "iface");
		if (IS_ERR(scm->iface_clk)) {
			if (PTR_ERR(scm->iface_clk) != -EPROBE_DEFER)
				dev_err(&pdev->dev, "failed to acquire iface clk\n");
			return PTR_ERR(scm->iface_clk);
		}

		scm->bus_clk = devm_clk_get(&pdev->dev, "bus");
		if (IS_ERR(scm->bus_clk)) {
			if (PTR_ERR(scm->bus_clk) != -EPROBE_DEFER)
				dev_err(&pdev->dev, "failed to acquire bus clk\n");
			return PTR_ERR(scm->bus_clk);
		}
	}

	scm->reset.ops = &qcom_scm_pas_reset_ops;
	scm->reset.nr_resets = 1;
	scm->reset.of_node = pdev->dev.of_node;
	reset_controller_register(&scm->reset);

	/* vote for max clk rate for highest performance */
	ret = clk_set_rate(scm->core_clk, INT_MAX);
	if (ret)
		return ret;

	__scm = scm;
	__scm->dev = &pdev->dev;

	__qcom_scm_init();

	return 0;
}

static const struct of_device_id qcom_scm_dt_match[] = {
	{ .compatible = "qcom,scm-apq8064",},
	{ .compatible = "qcom,scm-msm8660",},
	{ .compatible = "qcom,scm-msm8960",},
	{ .compatible = "qcom,scm",},
	{}
};

MODULE_DEVICE_TABLE(of, qcom_scm_dt_match);

static struct platform_driver qcom_scm_driver = {
	.driver = {
		.name	= "qcom_scm",
		.of_match_table = qcom_scm_dt_match,
	},
	.probe = qcom_scm_probe,
};

static int __init qcom_scm_init(void)
{
	struct device_node *np, *fw_np;
	int ret;

	fw_np = of_find_node_by_name(NULL, "firmware");

	if (!fw_np)
		return -ENODEV;

	np = of_find_matching_node(fw_np, qcom_scm_dt_match);

	if (!np) {
		of_node_put(fw_np);
		return -ENODEV;
	}

	of_node_put(np);

	ret = of_platform_populate(fw_np, qcom_scm_dt_match, NULL, NULL);

	of_node_put(fw_np);

	if (ret)
		return ret;

	return platform_driver_register(&qcom_scm_driver);
}

subsys_initcall(qcom_scm_init);

static void __exit qcom_scm_exit(void)
{
	platform_driver_unregister(&qcom_scm_driver);
}
module_exit(qcom_scm_exit);

MODULE_DESCRIPTION("Qualcomm SCM driver");
MODULE_LICENSE("GPL v2");
