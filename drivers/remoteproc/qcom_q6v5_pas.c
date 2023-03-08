// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm ADSP/SLPI Peripheral Image Loader for MSM8974 and MSM8996
 *
 * Copyright (C) 2016 Linaro Ltd
 * Copyright (C) 2014 Sony Mobile Communications AB
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/regulator/consumer.h>
#include <linux/remoteproc.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/smem_state.h>

#include "qcom_common.h"
#include "qcom_pil_info.h"
#include "qcom_q6v5.h"
#include "remoteproc_internal.h"

#define ADSP_DECRYPT_SHUTDOWN_DELAY_MS	100

struct adsp_data {
	int crash_reason_smem;
	const char *firmware_name;
	const char *dtb_firmware_name;
	int pas_id;
	int dtb_pas_id;
	unsigned int minidump_id;
	bool auto_boot;
	bool decrypt_shutdown;

	char **proxy_pd_names;

	const char *load_state;
	const char *ssr_name;
	const char *sysmon_name;
	int ssctl_id;

	int region_assign_idx;
};

struct qcom_adsp {
	struct device *dev;
	struct rproc *rproc;

	struct qcom_q6v5 q6v5;

	struct clk *xo;
	struct clk *aggre2_clk;

	struct regulator *cx_supply;
	struct regulator *px_supply;

	struct device *proxy_pds[3];

	int proxy_pd_count;

	const char *dtb_firmware_name;
	int pas_id;
	int dtb_pas_id;
	unsigned int minidump_id;
	int crash_reason_smem;
	bool decrypt_shutdown;
	const char *info_name;

	const struct firmware *firmware;
	const struct firmware *dtb_firmware;

	struct completion start_done;
	struct completion stop_done;

	phys_addr_t mem_phys;
	phys_addr_t dtb_mem_phys;
	phys_addr_t mem_reloc;
	phys_addr_t dtb_mem_reloc;
	phys_addr_t region_assign_phys;
	void *mem_region;
	void *dtb_mem_region;
	size_t mem_size;
	size_t dtb_mem_size;
	size_t region_assign_size;

	int region_assign_idx;
	int region_assign_perms;

	struct qcom_rproc_glink glink_subdev;
	struct qcom_rproc_subdev smd_subdev;
	struct qcom_rproc_ssr ssr_subdev;
	struct qcom_sysmon *sysmon;

	struct qcom_scm_pas_metadata pas_metadata;
	struct qcom_scm_pas_metadata dtb_pas_metadata;
};

void adsp_segment_dump(struct rproc *rproc, struct rproc_dump_segment *segment,
		       void *dest, size_t offset, size_t size)
{
	struct qcom_adsp *adsp = rproc->priv;
	int total_offset;

	total_offset = segment->da + segment->offset + offset - adsp->mem_phys;
	if (total_offset < 0 || total_offset + size > adsp->mem_size) {
		dev_err(adsp->dev,
			"invalid copy request for segment %pad with offset %zu and size %zu)\n",
			&segment->da, offset, size);
		memset(dest, 0xff, size);
		return;
	}

	memcpy_fromio(dest, adsp->mem_region + total_offset, size);
}

static void adsp_minidump(struct rproc *rproc)
{
	struct qcom_adsp *adsp = rproc->priv;

	if (rproc->dump_conf == RPROC_COREDUMP_DISABLED)
		return;

	qcom_minidump(rproc, adsp->minidump_id, adsp_segment_dump);
}

static int adsp_pds_enable(struct qcom_adsp *adsp, struct device **pds,
			   size_t pd_count)
{
	int ret;
	int i;

	for (i = 0; i < pd_count; i++) {
		dev_pm_genpd_set_performance_state(pds[i], INT_MAX);
		ret = pm_runtime_get_sync(pds[i]);
		if (ret < 0) {
			pm_runtime_put_noidle(pds[i]);
			dev_pm_genpd_set_performance_state(pds[i], 0);
			goto unroll_pd_votes;
		}
	}

	return 0;

unroll_pd_votes:
	for (i--; i >= 0; i--) {
		dev_pm_genpd_set_performance_state(pds[i], 0);
		pm_runtime_put(pds[i]);
	}

	return ret;
};

static void adsp_pds_disable(struct qcom_adsp *adsp, struct device **pds,
			     size_t pd_count)
{
	int i;

	for (i = 0; i < pd_count; i++) {
		dev_pm_genpd_set_performance_state(pds[i], 0);
		pm_runtime_put(pds[i]);
	}
}

static int adsp_shutdown_poll_decrypt(struct qcom_adsp *adsp)
{
	unsigned int retry_num = 50;
	int ret;

	do {
		msleep(ADSP_DECRYPT_SHUTDOWN_DELAY_MS);
		ret = qcom_scm_pas_shutdown(adsp->pas_id);
	} while (ret == -EINVAL && --retry_num);

	return ret;
}

static int adsp_unprepare(struct rproc *rproc)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;

	/*
	 * adsp_load() did pass pas_metadata to the SCM driver for storing
	 * metadata context. It might have been released already if
	 * auth_and_reset() was successful, but in other cases clean it up
	 * here.
	 */
	qcom_scm_pas_metadata_release(&adsp->pas_metadata);
	if (adsp->dtb_pas_id)
		qcom_scm_pas_metadata_release(&adsp->dtb_pas_metadata);

	return 0;
}

static int adsp_load(struct rproc *rproc, const struct firmware *fw)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;
	int ret;

	/* Store firmware handle to be used in adsp_start() */
	adsp->firmware = fw;

	if (adsp->dtb_pas_id) {
		ret = request_firmware(&adsp->dtb_firmware, adsp->dtb_firmware_name, adsp->dev);
		if (ret) {
			dev_err(adsp->dev, "request_firmware failed for %s: %d\n",
				adsp->dtb_firmware_name, ret);
			return ret;
		}

		ret = qcom_mdt_pas_init(adsp->dev, adsp->dtb_firmware, adsp->dtb_firmware_name,
					adsp->dtb_pas_id, adsp->dtb_mem_phys,
					&adsp->dtb_pas_metadata);
		if (ret)
			goto release_dtb_firmware;

		ret = qcom_mdt_load_no_init(adsp->dev, adsp->dtb_firmware, adsp->dtb_firmware_name,
					    adsp->dtb_pas_id, adsp->dtb_mem_region,
					    adsp->dtb_mem_phys, adsp->dtb_mem_size,
					    &adsp->dtb_mem_reloc);
		if (ret)
			goto release_dtb_metadata;
	}

	return 0;

release_dtb_metadata:
	qcom_scm_pas_metadata_release(&adsp->dtb_pas_metadata);

release_dtb_firmware:
	release_firmware(adsp->dtb_firmware);

	return ret;
}

static int adsp_start(struct rproc *rproc)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;
	int ret;

	ret = qcom_q6v5_prepare(&adsp->q6v5);
	if (ret)
		return ret;

	ret = adsp_pds_enable(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
	if (ret < 0)
		goto disable_irqs;

	ret = clk_prepare_enable(adsp->xo);
	if (ret)
		goto disable_proxy_pds;

	ret = clk_prepare_enable(adsp->aggre2_clk);
	if (ret)
		goto disable_xo_clk;

	if (adsp->cx_supply) {
		ret = regulator_enable(adsp->cx_supply);
		if (ret)
			goto disable_aggre2_clk;
	}

	if (adsp->px_supply) {
		ret = regulator_enable(adsp->px_supply);
		if (ret)
			goto disable_cx_supply;
	}

	if (adsp->dtb_pas_id) {
		ret = qcom_scm_pas_auth_and_reset(adsp->dtb_pas_id);
		if (ret) {
			dev_err(adsp->dev,
				"failed to authenticate dtb image and release reset\n");
			goto disable_px_supply;
		}
	}

	ret = qcom_mdt_pas_init(adsp->dev, adsp->firmware, rproc->firmware, adsp->pas_id,
				adsp->mem_phys, &adsp->pas_metadata);
	if (ret)
		goto disable_px_supply;

	ret = qcom_mdt_load_no_init(adsp->dev, adsp->firmware, rproc->firmware, adsp->pas_id,
				    adsp->mem_region, adsp->mem_phys, adsp->mem_size,
				    &adsp->mem_reloc);
	if (ret)
		goto release_pas_metadata;

	qcom_pil_info_store(adsp->info_name, adsp->mem_phys, adsp->mem_size);

	ret = qcom_scm_pas_auth_and_reset(adsp->pas_id);
	if (ret) {
		dev_err(adsp->dev,
			"failed to authenticate image and release reset\n");
		goto release_pas_metadata;
	}

	ret = qcom_q6v5_wait_for_start(&adsp->q6v5, msecs_to_jiffies(5000));
	if (ret == -ETIMEDOUT) {
		dev_err(adsp->dev, "start timed out\n");
		qcom_scm_pas_shutdown(adsp->pas_id);
		goto release_pas_metadata;
	}

	qcom_scm_pas_metadata_release(&adsp->pas_metadata);
	if (adsp->dtb_pas_id)
		qcom_scm_pas_metadata_release(&adsp->dtb_pas_metadata);

	/* Remove pointer to the loaded firmware, only valid in adsp_load() & adsp_start() */
	adsp->firmware = NULL;

	return 0;

release_pas_metadata:
	qcom_scm_pas_metadata_release(&adsp->pas_metadata);
	if (adsp->dtb_pas_id)
		qcom_scm_pas_metadata_release(&adsp->dtb_pas_metadata);
disable_px_supply:
	if (adsp->px_supply)
		regulator_disable(adsp->px_supply);
disable_cx_supply:
	if (adsp->cx_supply)
		regulator_disable(adsp->cx_supply);
disable_aggre2_clk:
	clk_disable_unprepare(adsp->aggre2_clk);
disable_xo_clk:
	clk_disable_unprepare(adsp->xo);
disable_proxy_pds:
	adsp_pds_disable(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
disable_irqs:
	qcom_q6v5_unprepare(&adsp->q6v5);

	/* Remove pointer to the loaded firmware, only valid in adsp_load() & adsp_start() */
	adsp->firmware = NULL;

	return ret;
}

static void qcom_pas_handover(struct qcom_q6v5 *q6v5)
{
	struct qcom_adsp *adsp = container_of(q6v5, struct qcom_adsp, q6v5);

	if (adsp->px_supply)
		regulator_disable(adsp->px_supply);
	if (adsp->cx_supply)
		regulator_disable(adsp->cx_supply);
	clk_disable_unprepare(adsp->aggre2_clk);
	clk_disable_unprepare(adsp->xo);
	adsp_pds_disable(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
}

static int adsp_stop(struct rproc *rproc)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;
	int handover;
	int ret;

	ret = qcom_q6v5_request_stop(&adsp->q6v5, adsp->sysmon);
	if (ret == -ETIMEDOUT)
		dev_err(adsp->dev, "timed out on wait\n");

	ret = qcom_scm_pas_shutdown(adsp->pas_id);
	if (ret && adsp->decrypt_shutdown)
		ret = adsp_shutdown_poll_decrypt(adsp);

	if (ret)
		dev_err(adsp->dev, "failed to shutdown: %d\n", ret);

	if (adsp->dtb_pas_id) {
		ret = qcom_scm_pas_shutdown(adsp->dtb_pas_id);
		if (ret)
			dev_err(adsp->dev, "failed to shutdown dtb: %d\n", ret);
	}

	handover = qcom_q6v5_unprepare(&adsp->q6v5);
	if (handover)
		qcom_pas_handover(&adsp->q6v5);

	return ret;
}

static void *adsp_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;
	int offset;

	offset = da - adsp->mem_reloc;
	if (offset < 0 || offset + len > adsp->mem_size)
		return NULL;

	if (is_iomem)
		*is_iomem = true;

	return adsp->mem_region + offset;
}

static unsigned long adsp_panic(struct rproc *rproc)
{
	struct qcom_adsp *adsp = (struct qcom_adsp *)rproc->priv;

	return qcom_q6v5_panic(&adsp->q6v5);
}

static const struct rproc_ops adsp_ops = {
	.unprepare = adsp_unprepare,
	.start = adsp_start,
	.stop = adsp_stop,
	.da_to_va = adsp_da_to_va,
	.parse_fw = qcom_register_dump_segments,
	.load = adsp_load,
	.panic = adsp_panic,
};

static const struct rproc_ops adsp_minidump_ops = {
	.unprepare = adsp_unprepare,
	.start = adsp_start,
	.stop = adsp_stop,
	.da_to_va = adsp_da_to_va,
	.load = adsp_load,
	.panic = adsp_panic,
	.coredump = adsp_minidump,
};

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

	adsp->aggre2_clk = devm_clk_get_optional(adsp->dev, "aggre2");
	if (IS_ERR(adsp->aggre2_clk)) {
		ret = PTR_ERR(adsp->aggre2_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(adsp->dev,
				"failed to get aggre2 clock");
		return ret;
	}

	return 0;
}

static int adsp_init_regulator(struct qcom_adsp *adsp)
{
	adsp->cx_supply = devm_regulator_get_optional(adsp->dev, "cx");
	if (IS_ERR(adsp->cx_supply)) {
		if (PTR_ERR(adsp->cx_supply) == -ENODEV)
			adsp->cx_supply = NULL;
		else
			return PTR_ERR(adsp->cx_supply);
	}

	if (adsp->cx_supply)
		regulator_set_load(adsp->cx_supply, 100000);

	adsp->px_supply = devm_regulator_get_optional(adsp->dev, "px");
	if (IS_ERR(adsp->px_supply)) {
		if (PTR_ERR(adsp->px_supply) == -ENODEV)
			adsp->px_supply = NULL;
		else
			return PTR_ERR(adsp->px_supply);
	}

	return 0;
}

static int adsp_pds_attach(struct device *dev, struct device **devs,
			   char **pd_names)
{
	size_t num_pds = 0;
	int ret;
	int i;

	if (!pd_names)
		return 0;

	/* Handle single power domain */
	if (dev->pm_domain) {
		devs[0] = dev;
		pm_runtime_enable(dev);
		return 1;
	}

	while (pd_names[num_pds])
		num_pds++;

	for (i = 0; i < num_pds; i++) {
		devs[i] = dev_pm_domain_attach_by_name(dev, pd_names[i]);
		if (IS_ERR_OR_NULL(devs[i])) {
			ret = PTR_ERR(devs[i]) ? : -ENODATA;
			goto unroll_attach;
		}
	}

	return num_pds;

unroll_attach:
	for (i--; i >= 0; i--)
		dev_pm_domain_detach(devs[i], false);

	return ret;
};

static void adsp_pds_detach(struct qcom_adsp *adsp, struct device **pds,
			    size_t pd_count)
{
	struct device *dev = adsp->dev;
	int i;

	/* Handle single power domain */
	if (dev->pm_domain && pd_count) {
		pm_runtime_disable(dev);
		return;
	}

	for (i = 0; i < pd_count; i++)
		dev_pm_domain_detach(pds[i], false);
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
	of_node_put(node);
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

	if (!adsp->dtb_pas_id)
		return 0;

	node = of_parse_phandle(adsp->dev->of_node, "memory-region", 1);
	if (!node) {
		dev_err(adsp->dev, "no dtb memory-region specified\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &r);
	if (ret)
		return ret;

	adsp->dtb_mem_phys = adsp->dtb_mem_reloc = r.start;
	adsp->dtb_mem_size = resource_size(&r);
	adsp->dtb_mem_region = devm_ioremap_wc(adsp->dev, adsp->dtb_mem_phys, adsp->dtb_mem_size);
	if (!adsp->dtb_mem_region) {
		dev_err(adsp->dev, "unable to map dtb memory region: %pa+%zx\n",
			&r.start, adsp->dtb_mem_size);
		return -EBUSY;
	}

	return 0;
}

static int adsp_assign_memory_region(struct qcom_adsp *adsp)
{
	struct qcom_scm_vmperm perm;
	struct device_node *node;
	struct resource r;
	int ret;

	if (!adsp->region_assign_idx)
		return 0;

	node = of_parse_phandle(adsp->dev->of_node, "memory-region", adsp->region_assign_idx);
	if (!node) {
		dev_err(adsp->dev, "missing shareable memory-region\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &r);
	if (ret)
		return ret;

	perm.vmid = QCOM_SCM_VMID_MSS_MSA;
	perm.perm = QCOM_SCM_PERM_RW;

	adsp->region_assign_phys = r.start;
	adsp->region_assign_size = resource_size(&r);
	adsp->region_assign_perms = BIT(QCOM_SCM_VMID_HLOS);

	ret = qcom_scm_assign_mem(adsp->region_assign_phys,
				  adsp->region_assign_size,
				  &adsp->region_assign_perms,
				  &perm, 1);
	if (ret < 0) {
		dev_err(adsp->dev, "assign memory failed\n");
		return ret;
	}

	return 0;
}

static void adsp_unassign_memory_region(struct qcom_adsp *adsp)
{
	struct qcom_scm_vmperm perm;
	int ret;

	if (!adsp->region_assign_idx)
		return;

	perm.vmid = QCOM_SCM_VMID_HLOS;
	perm.perm = QCOM_SCM_PERM_RW;

	ret = qcom_scm_assign_mem(adsp->region_assign_phys,
				  adsp->region_assign_size,
				  &adsp->region_assign_perms,
				  &perm, 1);
	if (ret < 0)
		dev_err(adsp->dev, "unassign memory failed\n");
}

static int adsp_probe(struct platform_device *pdev)
{
	const struct adsp_data *desc;
	struct qcom_adsp *adsp;
	struct rproc *rproc;
	const char *fw_name, *dtb_fw_name = NULL;
	const struct rproc_ops *ops = &adsp_ops;
	int ret;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;

	fw_name = desc->firmware_name;
	ret = of_property_read_string(pdev->dev.of_node, "firmware-name",
				      &fw_name);
	if (ret < 0 && ret != -EINVAL)
		return ret;

	if (desc->dtb_firmware_name) {
		dtb_fw_name = desc->dtb_firmware_name;
		ret = of_property_read_string_index(pdev->dev.of_node, "firmware-name", 1,
						    &dtb_fw_name);
		if (ret < 0 && ret != -EINVAL)
			return ret;
	}

	if (desc->minidump_id)
		ops = &adsp_minidump_ops;

	rproc = rproc_alloc(&pdev->dev, pdev->name, ops, fw_name, sizeof(*adsp));

	if (!rproc) {
		dev_err(&pdev->dev, "unable to allocate remoteproc\n");
		return -ENOMEM;
	}

	rproc->auto_boot = desc->auto_boot;
	rproc_coredump_set_elf_info(rproc, ELFCLASS32, EM_NONE);

	adsp = (struct qcom_adsp *)rproc->priv;
	adsp->dev = &pdev->dev;
	adsp->rproc = rproc;
	adsp->minidump_id = desc->minidump_id;
	adsp->pas_id = desc->pas_id;
	adsp->info_name = desc->sysmon_name;
	adsp->decrypt_shutdown = desc->decrypt_shutdown;
	adsp->region_assign_idx = desc->region_assign_idx;
	if (dtb_fw_name) {
		adsp->dtb_firmware_name = dtb_fw_name;
		adsp->dtb_pas_id = desc->dtb_pas_id;
	}
	platform_set_drvdata(pdev, adsp);

	ret = device_init_wakeup(adsp->dev, true);
	if (ret)
		goto free_rproc;

	ret = adsp_alloc_memory_region(adsp);
	if (ret)
		goto free_rproc;

	ret = adsp_assign_memory_region(adsp);
	if (ret)
		goto free_rproc;

	ret = adsp_init_clock(adsp);
	if (ret)
		goto free_rproc;

	ret = adsp_init_regulator(adsp);
	if (ret)
		goto free_rproc;

	ret = adsp_pds_attach(&pdev->dev, adsp->proxy_pds,
			      desc->proxy_pd_names);
	if (ret < 0)
		goto free_rproc;
	adsp->proxy_pd_count = ret;

	ret = qcom_q6v5_init(&adsp->q6v5, pdev, rproc, desc->crash_reason_smem, desc->load_state,
			     qcom_pas_handover);
	if (ret)
		goto detach_proxy_pds;

	qcom_add_glink_subdev(rproc, &adsp->glink_subdev, desc->ssr_name);
	qcom_add_smd_subdev(rproc, &adsp->smd_subdev);
	adsp->sysmon = qcom_add_sysmon_subdev(rproc,
					      desc->sysmon_name,
					      desc->ssctl_id);
	if (IS_ERR(adsp->sysmon)) {
		ret = PTR_ERR(adsp->sysmon);
		goto detach_proxy_pds;
	}

	qcom_add_ssr_subdev(rproc, &adsp->ssr_subdev, desc->ssr_name);
	ret = rproc_add(rproc);
	if (ret)
		goto detach_proxy_pds;

	return 0;

detach_proxy_pds:
	adsp_pds_detach(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
free_rproc:
	device_init_wakeup(adsp->dev, false);
	rproc_free(rproc);

	return ret;
}

static int adsp_remove(struct platform_device *pdev)
{
	struct qcom_adsp *adsp = platform_get_drvdata(pdev);

	rproc_del(adsp->rproc);

	qcom_q6v5_deinit(&adsp->q6v5);
	adsp_unassign_memory_region(adsp);
	qcom_remove_glink_subdev(adsp->rproc, &adsp->glink_subdev);
	qcom_remove_sysmon_subdev(adsp->sysmon);
	qcom_remove_smd_subdev(adsp->rproc, &adsp->smd_subdev);
	qcom_remove_ssr_subdev(adsp->rproc, &adsp->ssr_subdev);
	adsp_pds_detach(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
	device_init_wakeup(adsp->dev, false);
	rproc_free(adsp->rproc);

	return 0;
}

static const struct adsp_data adsp_resource_init = {
		.crash_reason_smem = 423,
		.firmware_name = "adsp.mdt",
		.pas_id = 1,
		.auto_boot = true,
		.ssr_name = "lpass",
		.sysmon_name = "adsp",
		.ssctl_id = 0x14,
};

static const struct adsp_data sdm845_adsp_resource_init = {
		.crash_reason_smem = 423,
		.firmware_name = "adsp.mdt",
		.pas_id = 1,
		.auto_boot = true,
		.load_state = "adsp",
		.ssr_name = "lpass",
		.sysmon_name = "adsp",
		.ssctl_id = 0x14,
};

static const struct adsp_data sm6350_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"lcx",
		"lmx",
		NULL
	},
	.load_state = "adsp",
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data sm8150_adsp_resource = {
		.crash_reason_smem = 423,
		.firmware_name = "adsp.mdt",
		.pas_id = 1,
		.auto_boot = true,
		.proxy_pd_names = (char*[]){
			"cx",
			NULL
		},
		.load_state = "adsp",
		.ssr_name = "lpass",
		.sysmon_name = "adsp",
		.ssctl_id = 0x14,
};

static const struct adsp_data sm8250_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"lcx",
		"lmx",
		NULL
	},
	.load_state = "adsp",
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data sm8350_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"lcx",
		"lmx",
		NULL
	},
	.load_state = "adsp",
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data msm8996_adsp_resource = {
		.crash_reason_smem = 423,
		.firmware_name = "adsp.mdt",
		.pas_id = 1,
		.auto_boot = true,
		.proxy_pd_names = (char*[]){
			"cx",
			NULL
		},
		.ssr_name = "lpass",
		.sysmon_name = "adsp",
		.ssctl_id = 0x14,
};

static const struct adsp_data cdsp_resource_init = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.auto_boot = true,
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data sdm845_cdsp_resource_init = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.auto_boot = true,
	.load_state = "cdsp",
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data sm6350_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"cx",
		"mx",
		NULL
	},
	.load_state = "cdsp",
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data sm8150_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"cx",
		NULL
	},
	.load_state = "cdsp",
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data sm8250_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"cx",
		NULL
	},
	.load_state = "cdsp",
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data sc8280xp_nsp0_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"nsp",
		NULL
	},
	.ssr_name = "cdsp0",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data sc8280xp_nsp1_resource = {
	.crash_reason_smem = 633,
	.firmware_name = "cdsp.mdt",
	.pas_id = 30,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"nsp",
		NULL
	},
	.ssr_name = "cdsp1",
	.sysmon_name = "cdsp1",
	.ssctl_id = 0x20,
};

static const struct adsp_data sm8350_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"cx",
		"mxc",
		NULL
	},
	.load_state = "cdsp",
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data mpss_resource_init = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.pas_id = 4,
	.minidump_id = 3,
	.auto_boot = false,
	.proxy_pd_names = (char*[]){
		"cx",
		"mss",
		NULL
	},
	.load_state = "modem",
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.ssctl_id = 0x12,
};

static const struct adsp_data sc8180x_mpss_resource = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.pas_id = 4,
	.auto_boot = false,
	.proxy_pd_names = (char*[]){
		"cx",
		NULL
	},
	.load_state = "modem",
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.ssctl_id = 0x12,
};

static const struct adsp_data slpi_resource_init = {
		.crash_reason_smem = 424,
		.firmware_name = "slpi.mdt",
		.pas_id = 12,
		.auto_boot = true,
		.proxy_pd_names = (char*[]){
			"ssc_cx",
			NULL
		},
		.ssr_name = "dsps",
		.sysmon_name = "slpi",
		.ssctl_id = 0x16,
};

static const struct adsp_data sm8150_slpi_resource = {
		.crash_reason_smem = 424,
		.firmware_name = "slpi.mdt",
		.pas_id = 12,
		.auto_boot = true,
		.proxy_pd_names = (char*[]){
			"lcx",
			"lmx",
			NULL
		},
		.load_state = "slpi",
		.ssr_name = "dsps",
		.sysmon_name = "slpi",
		.ssctl_id = 0x16,
};

static const struct adsp_data sm8250_slpi_resource = {
	.crash_reason_smem = 424,
	.firmware_name = "slpi.mdt",
	.pas_id = 12,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"lcx",
		"lmx",
		NULL
	},
	.load_state = "slpi",
	.ssr_name = "dsps",
	.sysmon_name = "slpi",
	.ssctl_id = 0x16,
};

static const struct adsp_data sm8350_slpi_resource = {
	.crash_reason_smem = 424,
	.firmware_name = "slpi.mdt",
	.pas_id = 12,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"lcx",
		"lmx",
		NULL
	},
	.load_state = "slpi",
	.ssr_name = "dsps",
	.sysmon_name = "slpi",
	.ssctl_id = 0x16,
};

static const struct adsp_data wcss_resource_init = {
	.crash_reason_smem = 421,
	.firmware_name = "wcnss.mdt",
	.pas_id = 6,
	.auto_boot = true,
	.ssr_name = "mpss",
	.sysmon_name = "wcnss",
	.ssctl_id = 0x12,
};

static const struct adsp_data sdx55_mpss_resource = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.pas_id = 4,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"cx",
		"mss",
		NULL
	},
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.ssctl_id = 0x22,
};

static const struct adsp_data sm8450_mpss_resource = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.pas_id = 4,
	.minidump_id = 3,
	.auto_boot = false,
	.decrypt_shutdown = true,
	.proxy_pd_names = (char*[]){
		"cx",
		"mss",
		NULL
	},
	.load_state = "modem",
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.ssctl_id = 0x12,
};

static const struct adsp_data sm8550_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.dtb_firmware_name = "adsp_dtb.mdt",
	.pas_id = 1,
	.dtb_pas_id = 0x24,
	.minidump_id = 5,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"lcx",
		"lmx",
		NULL
	},
	.load_state = "adsp",
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct adsp_data sm8550_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.dtb_firmware_name = "cdsp_dtb.mdt",
	.pas_id = 18,
	.dtb_pas_id = 0x25,
	.minidump_id = 7,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"cx",
		"mxc",
		"nsp",
		NULL
	},
	.load_state = "cdsp",
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct adsp_data sm8550_mpss_resource = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.dtb_firmware_name = "modem_dtb.mdt",
	.pas_id = 4,
	.dtb_pas_id = 0x26,
	.minidump_id = 3,
	.auto_boot = false,
	.decrypt_shutdown = true,
	.proxy_pd_names = (char*[]){
		"cx",
		"mss",
		NULL
	},
	.load_state = "modem",
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.ssctl_id = 0x12,
	.region_assign_idx = 2,
};

static const struct of_device_id adsp_of_match[] = {
	{ .compatible = "qcom,msm8226-adsp-pil", .data = &adsp_resource_init},
	{ .compatible = "qcom,msm8953-adsp-pil", .data = &msm8996_adsp_resource},
	{ .compatible = "qcom,msm8974-adsp-pil", .data = &adsp_resource_init},
	{ .compatible = "qcom,msm8996-adsp-pil", .data = &msm8996_adsp_resource},
	{ .compatible = "qcom,msm8996-slpi-pil", .data = &slpi_resource_init},
	{ .compatible = "qcom,msm8998-adsp-pas", .data = &msm8996_adsp_resource},
	{ .compatible = "qcom,msm8998-slpi-pas", .data = &slpi_resource_init},
	{ .compatible = "qcom,qcs404-adsp-pas", .data = &adsp_resource_init },
	{ .compatible = "qcom,qcs404-cdsp-pas", .data = &cdsp_resource_init },
	{ .compatible = "qcom,qcs404-wcss-pas", .data = &wcss_resource_init },
	{ .compatible = "qcom,sc7180-mpss-pas", .data = &mpss_resource_init},
	{ .compatible = "qcom,sc7280-mpss-pas", .data = &mpss_resource_init},
	{ .compatible = "qcom,sc8180x-adsp-pas", .data = &sm8150_adsp_resource},
	{ .compatible = "qcom,sc8180x-cdsp-pas", .data = &sm8150_cdsp_resource},
	{ .compatible = "qcom,sc8180x-mpss-pas", .data = &sc8180x_mpss_resource},
	{ .compatible = "qcom,sc8280xp-adsp-pas", .data = &sm8250_adsp_resource},
	{ .compatible = "qcom,sc8280xp-nsp0-pas", .data = &sc8280xp_nsp0_resource},
	{ .compatible = "qcom,sc8280xp-nsp1-pas", .data = &sc8280xp_nsp1_resource},
	{ .compatible = "qcom,sdm660-adsp-pas", .data = &adsp_resource_init},
	{ .compatible = "qcom,sdm845-adsp-pas", .data = &sdm845_adsp_resource_init},
	{ .compatible = "qcom,sdm845-cdsp-pas", .data = &sdm845_cdsp_resource_init},
	{ .compatible = "qcom,sdx55-mpss-pas", .data = &sdx55_mpss_resource},
	{ .compatible = "qcom,sm6115-adsp-pas", .data = &adsp_resource_init},
	{ .compatible = "qcom,sm6115-cdsp-pas", .data = &cdsp_resource_init},
	{ .compatible = "qcom,sm6115-mpss-pas", .data = &sc8180x_mpss_resource},
	{ .compatible = "qcom,sm6350-adsp-pas", .data = &sm6350_adsp_resource},
	{ .compatible = "qcom,sm6350-cdsp-pas", .data = &sm6350_cdsp_resource},
	{ .compatible = "qcom,sm6350-mpss-pas", .data = &mpss_resource_init},
	{ .compatible = "qcom,sm8150-adsp-pas", .data = &sm8150_adsp_resource},
	{ .compatible = "qcom,sm8150-cdsp-pas", .data = &sm8150_cdsp_resource},
	{ .compatible = "qcom,sm8150-mpss-pas", .data = &mpss_resource_init},
	{ .compatible = "qcom,sm8150-slpi-pas", .data = &sm8150_slpi_resource},
	{ .compatible = "qcom,sm8250-adsp-pas", .data = &sm8250_adsp_resource},
	{ .compatible = "qcom,sm8250-cdsp-pas", .data = &sm8250_cdsp_resource},
	{ .compatible = "qcom,sm8250-slpi-pas", .data = &sm8250_slpi_resource},
	{ .compatible = "qcom,sm8350-adsp-pas", .data = &sm8350_adsp_resource},
	{ .compatible = "qcom,sm8350-cdsp-pas", .data = &sm8350_cdsp_resource},
	{ .compatible = "qcom,sm8350-slpi-pas", .data = &sm8350_slpi_resource},
	{ .compatible = "qcom,sm8350-mpss-pas", .data = &mpss_resource_init},
	{ .compatible = "qcom,sm8450-adsp-pas", .data = &sm8350_adsp_resource},
	{ .compatible = "qcom,sm8450-cdsp-pas", .data = &sm8350_cdsp_resource},
	{ .compatible = "qcom,sm8450-slpi-pas", .data = &sm8350_slpi_resource},
	{ .compatible = "qcom,sm8450-mpss-pas", .data = &sm8450_mpss_resource},
	{ .compatible = "qcom,sm8550-adsp-pas", .data = &sm8550_adsp_resource},
	{ .compatible = "qcom,sm8550-cdsp-pas", .data = &sm8550_cdsp_resource},
	{ .compatible = "qcom,sm8550-mpss-pas", .data = &sm8550_mpss_resource},
	{ },
};
MODULE_DEVICE_TABLE(of, adsp_of_match);

static struct platform_driver adsp_driver = {
	.probe = adsp_probe,
	.remove = adsp_remove,
	.driver = {
		.name = "qcom_q6v5_pas",
		.of_match_table = adsp_of_match,
	},
};

module_platform_driver(adsp_driver);
MODULE_DESCRIPTION("Qualcomm Hexagon v5 Peripheral Authentication Service driver");
MODULE_LICENSE("GPL v2");
