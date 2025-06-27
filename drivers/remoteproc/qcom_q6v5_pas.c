// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Peripheral Authentication Service remoteproc driver
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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
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

#define QCOM_PAS_DECRYPT_SHUTDOWN_DELAY_MS	100

#define MAX_ASSIGN_COUNT 3

struct qcom_pas_data {
	int crash_reason_smem;
	const char *firmware_name;
	const char *dtb_firmware_name;
	int pas_id;
	int dtb_pas_id;
	int lite_pas_id;
	unsigned int minidump_id;
	bool auto_boot;
	bool decrypt_shutdown;

	char **proxy_pd_names;

	const char *load_state;
	const char *ssr_name;
	const char *sysmon_name;
	int ssctl_id;
	unsigned int smem_host_id;

	int region_assign_idx;
	int region_assign_count;
	bool region_assign_shared;
	int region_assign_vmid;
};

struct qcom_pas {
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
	int lite_pas_id;
	unsigned int minidump_id;
	int crash_reason_smem;
	unsigned int smem_host_id;
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
	phys_addr_t region_assign_phys[MAX_ASSIGN_COUNT];
	void *mem_region;
	void *dtb_mem_region;
	size_t mem_size;
	size_t dtb_mem_size;
	size_t region_assign_size[MAX_ASSIGN_COUNT];

	int region_assign_idx;
	int region_assign_count;
	bool region_assign_shared;
	int region_assign_vmid;
	u64 region_assign_owners[MAX_ASSIGN_COUNT];

	struct qcom_rproc_glink glink_subdev;
	struct qcom_rproc_subdev smd_subdev;
	struct qcom_rproc_pdm pdm_subdev;
	struct qcom_rproc_ssr ssr_subdev;
	struct qcom_sysmon *sysmon;

	struct qcom_scm_pas_metadata pas_metadata;
	struct qcom_scm_pas_metadata dtb_pas_metadata;
};

static void qcom_pas_segment_dump(struct rproc *rproc,
				  struct rproc_dump_segment *segment,
				  void *dest, size_t offset, size_t size)
{
	struct qcom_pas *pas = rproc->priv;
	int total_offset;

	total_offset = segment->da + segment->offset + offset - pas->mem_phys;
	if (total_offset < 0 || total_offset + size > pas->mem_size) {
		dev_err(pas->dev,
			"invalid copy request for segment %pad with offset %zu and size %zu)\n",
			&segment->da, offset, size);
		memset(dest, 0xff, size);
		return;
	}

	memcpy_fromio(dest, pas->mem_region + total_offset, size);
}

static void qcom_pas_minidump(struct rproc *rproc)
{
	struct qcom_pas *pas = rproc->priv;

	if (rproc->dump_conf == RPROC_COREDUMP_DISABLED)
		return;

	qcom_minidump(rproc, pas->minidump_id, qcom_pas_segment_dump);
}

static int qcom_pas_pds_enable(struct qcom_pas *pas, struct device **pds,
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

static void qcom_pas_pds_disable(struct qcom_pas *pas, struct device **pds,
				 size_t pd_count)
{
	int i;

	for (i = 0; i < pd_count; i++) {
		dev_pm_genpd_set_performance_state(pds[i], 0);
		pm_runtime_put(pds[i]);
	}
}

static int qcom_pas_shutdown_poll_decrypt(struct qcom_pas *pas)
{
	unsigned int retry_num = 50;
	int ret;

	do {
		msleep(QCOM_PAS_DECRYPT_SHUTDOWN_DELAY_MS);
		ret = qcom_scm_pas_shutdown(pas->pas_id);
	} while (ret == -EINVAL && --retry_num);

	return ret;
}

static int qcom_pas_unprepare(struct rproc *rproc)
{
	struct qcom_pas *pas = rproc->priv;

	/*
	 * qcom_pas_load() did pass pas_metadata to the SCM driver for storing
	 * metadata context. It might have been released already if
	 * auth_and_reset() was successful, but in other cases clean it up
	 * here.
	 */
	qcom_scm_pas_metadata_release(&pas->pas_metadata);
	if (pas->dtb_pas_id)
		qcom_scm_pas_metadata_release(&pas->dtb_pas_metadata);

	return 0;
}

static int qcom_pas_load(struct rproc *rproc, const struct firmware *fw)
{
	struct qcom_pas *pas = rproc->priv;
	int ret;

	/* Store firmware handle to be used in qcom_pas_start() */
	pas->firmware = fw;

	if (pas->lite_pas_id)
		ret = qcom_scm_pas_shutdown(pas->lite_pas_id);

	if (pas->dtb_pas_id) {
		ret = request_firmware(&pas->dtb_firmware, pas->dtb_firmware_name, pas->dev);
		if (ret) {
			dev_err(pas->dev, "request_firmware failed for %s: %d\n",
				pas->dtb_firmware_name, ret);
			return ret;
		}

		ret = qcom_mdt_pas_init(pas->dev, pas->dtb_firmware, pas->dtb_firmware_name,
					pas->dtb_pas_id, pas->dtb_mem_phys,
					&pas->dtb_pas_metadata);
		if (ret)
			goto release_dtb_firmware;

		ret = qcom_mdt_load_no_init(pas->dev, pas->dtb_firmware, pas->dtb_firmware_name,
					    pas->dtb_pas_id, pas->dtb_mem_region,
					    pas->dtb_mem_phys, pas->dtb_mem_size,
					    &pas->dtb_mem_reloc);
		if (ret)
			goto release_dtb_metadata;
	}

	return 0;

release_dtb_metadata:
	qcom_scm_pas_metadata_release(&pas->dtb_pas_metadata);

release_dtb_firmware:
	release_firmware(pas->dtb_firmware);

	return ret;
}

static int qcom_pas_start(struct rproc *rproc)
{
	struct qcom_pas *pas = rproc->priv;
	int ret;

	ret = qcom_q6v5_prepare(&pas->q6v5);
	if (ret)
		return ret;

	ret = qcom_pas_pds_enable(pas, pas->proxy_pds, pas->proxy_pd_count);
	if (ret < 0)
		goto disable_irqs;

	ret = clk_prepare_enable(pas->xo);
	if (ret)
		goto disable_proxy_pds;

	ret = clk_prepare_enable(pas->aggre2_clk);
	if (ret)
		goto disable_xo_clk;

	if (pas->cx_supply) {
		ret = regulator_enable(pas->cx_supply);
		if (ret)
			goto disable_aggre2_clk;
	}

	if (pas->px_supply) {
		ret = regulator_enable(pas->px_supply);
		if (ret)
			goto disable_cx_supply;
	}

	if (pas->dtb_pas_id) {
		ret = qcom_scm_pas_auth_and_reset(pas->dtb_pas_id);
		if (ret) {
			dev_err(pas->dev,
				"failed to authenticate dtb image and release reset\n");
			goto disable_px_supply;
		}
	}

	ret = qcom_mdt_pas_init(pas->dev, pas->firmware, rproc->firmware, pas->pas_id,
				pas->mem_phys, &pas->pas_metadata);
	if (ret)
		goto disable_px_supply;

	ret = qcom_mdt_load_no_init(pas->dev, pas->firmware, rproc->firmware, pas->pas_id,
				    pas->mem_region, pas->mem_phys, pas->mem_size,
				    &pas->mem_reloc);
	if (ret)
		goto release_pas_metadata;

	qcom_pil_info_store(pas->info_name, pas->mem_phys, pas->mem_size);

	ret = qcom_scm_pas_auth_and_reset(pas->pas_id);
	if (ret) {
		dev_err(pas->dev,
			"failed to authenticate image and release reset\n");
		goto release_pas_metadata;
	}

	ret = qcom_q6v5_wait_for_start(&pas->q6v5, msecs_to_jiffies(5000));
	if (ret == -ETIMEDOUT) {
		dev_err(pas->dev, "start timed out\n");
		qcom_scm_pas_shutdown(pas->pas_id);
		goto release_pas_metadata;
	}

	qcom_scm_pas_metadata_release(&pas->pas_metadata);
	if (pas->dtb_pas_id)
		qcom_scm_pas_metadata_release(&pas->dtb_pas_metadata);

	/* firmware is used to pass reference from qcom_pas_start(), drop it now */
	pas->firmware = NULL;

	return 0;

release_pas_metadata:
	qcom_scm_pas_metadata_release(&pas->pas_metadata);
	if (pas->dtb_pas_id)
		qcom_scm_pas_metadata_release(&pas->dtb_pas_metadata);
disable_px_supply:
	if (pas->px_supply)
		regulator_disable(pas->px_supply);
disable_cx_supply:
	if (pas->cx_supply)
		regulator_disable(pas->cx_supply);
disable_aggre2_clk:
	clk_disable_unprepare(pas->aggre2_clk);
disable_xo_clk:
	clk_disable_unprepare(pas->xo);
disable_proxy_pds:
	qcom_pas_pds_disable(pas, pas->proxy_pds, pas->proxy_pd_count);
disable_irqs:
	qcom_q6v5_unprepare(&pas->q6v5);

	/* firmware is used to pass reference from qcom_pas_start(), drop it now */
	pas->firmware = NULL;

	return ret;
}

static void qcom_pas_handover(struct qcom_q6v5 *q6v5)
{
	struct qcom_pas *pas = container_of(q6v5, struct qcom_pas, q6v5);

	if (pas->px_supply)
		regulator_disable(pas->px_supply);
	if (pas->cx_supply)
		regulator_disable(pas->cx_supply);
	clk_disable_unprepare(pas->aggre2_clk);
	clk_disable_unprepare(pas->xo);
	qcom_pas_pds_disable(pas, pas->proxy_pds, pas->proxy_pd_count);
}

static int qcom_pas_stop(struct rproc *rproc)
{
	struct qcom_pas *pas = rproc->priv;
	int handover;
	int ret;

	ret = qcom_q6v5_request_stop(&pas->q6v5, pas->sysmon);
	if (ret == -ETIMEDOUT)
		dev_err(pas->dev, "timed out on wait\n");

	ret = qcom_scm_pas_shutdown(pas->pas_id);
	if (ret && pas->decrypt_shutdown)
		ret = qcom_pas_shutdown_poll_decrypt(pas);

	if (ret)
		dev_err(pas->dev, "failed to shutdown: %d\n", ret);

	if (pas->dtb_pas_id) {
		ret = qcom_scm_pas_shutdown(pas->dtb_pas_id);
		if (ret)
			dev_err(pas->dev, "failed to shutdown dtb: %d\n", ret);
	}

	handover = qcom_q6v5_unprepare(&pas->q6v5);
	if (handover)
		qcom_pas_handover(&pas->q6v5);

	if (pas->smem_host_id)
		ret = qcom_smem_bust_hwspin_lock_by_host(pas->smem_host_id);

	return ret;
}

static void *qcom_pas_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct qcom_pas *pas = rproc->priv;
	int offset;

	offset = da - pas->mem_reloc;
	if (offset < 0 || offset + len > pas->mem_size)
		return NULL;

	if (is_iomem)
		*is_iomem = true;

	return pas->mem_region + offset;
}

static unsigned long qcom_pas_panic(struct rproc *rproc)
{
	struct qcom_pas *pas = rproc->priv;

	return qcom_q6v5_panic(&pas->q6v5);
}

static const struct rproc_ops qcom_pas_ops = {
	.unprepare = qcom_pas_unprepare,
	.start = qcom_pas_start,
	.stop = qcom_pas_stop,
	.da_to_va = qcom_pas_da_to_va,
	.parse_fw = qcom_register_dump_segments,
	.load = qcom_pas_load,
	.panic = qcom_pas_panic,
};

static const struct rproc_ops qcom_pas_minidump_ops = {
	.unprepare = qcom_pas_unprepare,
	.start = qcom_pas_start,
	.stop = qcom_pas_stop,
	.da_to_va = qcom_pas_da_to_va,
	.parse_fw = qcom_register_dump_segments,
	.load = qcom_pas_load,
	.panic = qcom_pas_panic,
	.coredump = qcom_pas_minidump,
};

static int qcom_pas_init_clock(struct qcom_pas *pas)
{
	pas->xo = devm_clk_get(pas->dev, "xo");
	if (IS_ERR(pas->xo))
		return dev_err_probe(pas->dev, PTR_ERR(pas->xo),
				     "failed to get xo clock");

	pas->aggre2_clk = devm_clk_get_optional(pas->dev, "aggre2");
	if (IS_ERR(pas->aggre2_clk))
		return dev_err_probe(pas->dev, PTR_ERR(pas->aggre2_clk),
				     "failed to get aggre2 clock");

	return 0;
}

static int qcom_pas_init_regulator(struct qcom_pas *pas)
{
	pas->cx_supply = devm_regulator_get_optional(pas->dev, "cx");
	if (IS_ERR(pas->cx_supply)) {
		if (PTR_ERR(pas->cx_supply) == -ENODEV)
			pas->cx_supply = NULL;
		else
			return PTR_ERR(pas->cx_supply);
	}

	if (pas->cx_supply)
		regulator_set_load(pas->cx_supply, 100000);

	pas->px_supply = devm_regulator_get_optional(pas->dev, "px");
	if (IS_ERR(pas->px_supply)) {
		if (PTR_ERR(pas->px_supply) == -ENODEV)
			pas->px_supply = NULL;
		else
			return PTR_ERR(pas->px_supply);
	}

	return 0;
}

static int qcom_pas_pds_attach(struct device *dev, struct device **devs, char **pd_names)
{
	size_t num_pds = 0;
	int ret;
	int i;

	if (!pd_names)
		return 0;

	while (pd_names[num_pds])
		num_pds++;

	/* Handle single power domain */
	if (num_pds == 1 && dev->pm_domain) {
		devs[0] = dev;
		pm_runtime_enable(dev);
		return 1;
	}

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

static void qcom_pas_pds_detach(struct qcom_pas *pas, struct device **pds, size_t pd_count)
{
	struct device *dev = pas->dev;
	int i;

	/* Handle single power domain */
	if (pd_count == 1 && dev->pm_domain) {
		pm_runtime_disable(dev);
		return;
	}

	for (i = 0; i < pd_count; i++)
		dev_pm_domain_detach(pds[i], false);
}

static int qcom_pas_alloc_memory_region(struct qcom_pas *pas)
{
	struct reserved_mem *rmem;
	struct device_node *node;

	node = of_parse_phandle(pas->dev->of_node, "memory-region", 0);
	if (!node) {
		dev_err(pas->dev, "no memory-region specified\n");
		return -EINVAL;
	}

	rmem = of_reserved_mem_lookup(node);
	of_node_put(node);
	if (!rmem) {
		dev_err(pas->dev, "unable to resolve memory-region\n");
		return -EINVAL;
	}

	pas->mem_phys = pas->mem_reloc = rmem->base;
	pas->mem_size = rmem->size;
	pas->mem_region = devm_ioremap_wc(pas->dev, pas->mem_phys, pas->mem_size);
	if (!pas->mem_region) {
		dev_err(pas->dev, "unable to map memory region: %pa+%zx\n",
			&rmem->base, pas->mem_size);
		return -EBUSY;
	}

	if (!pas->dtb_pas_id)
		return 0;

	node = of_parse_phandle(pas->dev->of_node, "memory-region", 1);
	if (!node) {
		dev_err(pas->dev, "no dtb memory-region specified\n");
		return -EINVAL;
	}

	rmem = of_reserved_mem_lookup(node);
	of_node_put(node);
	if (!rmem) {
		dev_err(pas->dev, "unable to resolve dtb memory-region\n");
		return -EINVAL;
	}

	pas->dtb_mem_phys = pas->dtb_mem_reloc = rmem->base;
	pas->dtb_mem_size = rmem->size;
	pas->dtb_mem_region = devm_ioremap_wc(pas->dev, pas->dtb_mem_phys, pas->dtb_mem_size);
	if (!pas->dtb_mem_region) {
		dev_err(pas->dev, "unable to map dtb memory region: %pa+%zx\n",
			&rmem->base, pas->dtb_mem_size);
		return -EBUSY;
	}

	return 0;
}

static int qcom_pas_assign_memory_region(struct qcom_pas *pas)
{
	struct qcom_scm_vmperm perm[MAX_ASSIGN_COUNT];
	struct device_node *node;
	unsigned int perm_size;
	int offset;
	int ret;

	if (!pas->region_assign_idx)
		return 0;

	for (offset = 0; offset < pas->region_assign_count; ++offset) {
		struct reserved_mem *rmem = NULL;

		node = of_parse_phandle(pas->dev->of_node, "memory-region",
					pas->region_assign_idx + offset);
		if (node)
			rmem = of_reserved_mem_lookup(node);
		of_node_put(node);
		if (!rmem) {
			dev_err(pas->dev, "unable to resolve shareable memory-region index %d\n",
				offset);
			return -EINVAL;
		}

		if (pas->region_assign_shared)  {
			perm[0].vmid = QCOM_SCM_VMID_HLOS;
			perm[0].perm = QCOM_SCM_PERM_RW;
			perm[1].vmid = pas->region_assign_vmid;
			perm[1].perm = QCOM_SCM_PERM_RW;
			perm_size = 2;
		} else {
			perm[0].vmid = pas->region_assign_vmid;
			perm[0].perm = QCOM_SCM_PERM_RW;
			perm_size = 1;
		}

		pas->region_assign_phys[offset] = rmem->base;
		pas->region_assign_size[offset] = rmem->size;
		pas->region_assign_owners[offset] = BIT(QCOM_SCM_VMID_HLOS);

		ret = qcom_scm_assign_mem(pas->region_assign_phys[offset],
					  pas->region_assign_size[offset],
					  &pas->region_assign_owners[offset],
					  perm, perm_size);
		if (ret < 0) {
			dev_err(pas->dev, "assign memory %d failed\n", offset);
			return ret;
		}
	}

	return 0;
}

static void qcom_pas_unassign_memory_region(struct qcom_pas *pas)
{
	struct qcom_scm_vmperm perm;
	int offset;
	int ret;

	if (!pas->region_assign_idx || pas->region_assign_shared)
		return;

	for (offset = 0; offset < pas->region_assign_count; ++offset) {
		perm.vmid = QCOM_SCM_VMID_HLOS;
		perm.perm = QCOM_SCM_PERM_RW;

		ret = qcom_scm_assign_mem(pas->region_assign_phys[offset],
					  pas->region_assign_size[offset],
					  &pas->region_assign_owners[offset],
					  &perm, 1);
		if (ret < 0)
			dev_err(pas->dev, "unassign memory %d failed\n", offset);
	}
}

static int qcom_pas_probe(struct platform_device *pdev)
{
	const struct qcom_pas_data *desc;
	struct qcom_pas *pas;
	struct rproc *rproc;
	const char *fw_name, *dtb_fw_name = NULL;
	const struct rproc_ops *ops = &qcom_pas_ops;
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
		ops = &qcom_pas_minidump_ops;

	rproc = devm_rproc_alloc(&pdev->dev, desc->sysmon_name, ops, fw_name, sizeof(*pas));

	if (!rproc) {
		dev_err(&pdev->dev, "unable to allocate remoteproc\n");
		return -ENOMEM;
	}

	rproc->auto_boot = desc->auto_boot;
	rproc_coredump_set_elf_info(rproc, ELFCLASS32, EM_NONE);

	pas = rproc->priv;
	pas->dev = &pdev->dev;
	pas->rproc = rproc;
	pas->minidump_id = desc->minidump_id;
	pas->pas_id = desc->pas_id;
	pas->lite_pas_id = desc->lite_pas_id;
	pas->info_name = desc->sysmon_name;
	pas->smem_host_id = desc->smem_host_id;
	pas->decrypt_shutdown = desc->decrypt_shutdown;
	pas->region_assign_idx = desc->region_assign_idx;
	pas->region_assign_count = min_t(int, MAX_ASSIGN_COUNT, desc->region_assign_count);
	pas->region_assign_vmid = desc->region_assign_vmid;
	pas->region_assign_shared = desc->region_assign_shared;
	if (dtb_fw_name) {
		pas->dtb_firmware_name = dtb_fw_name;
		pas->dtb_pas_id = desc->dtb_pas_id;
	}
	platform_set_drvdata(pdev, pas);

	ret = device_init_wakeup(pas->dev, true);
	if (ret)
		goto free_rproc;

	ret = qcom_pas_alloc_memory_region(pas);
	if (ret)
		goto free_rproc;

	ret = qcom_pas_assign_memory_region(pas);
	if (ret)
		goto free_rproc;

	ret = qcom_pas_init_clock(pas);
	if (ret)
		goto unassign_mem;

	ret = qcom_pas_init_regulator(pas);
	if (ret)
		goto unassign_mem;

	ret = qcom_pas_pds_attach(&pdev->dev, pas->proxy_pds, desc->proxy_pd_names);
	if (ret < 0)
		goto unassign_mem;
	pas->proxy_pd_count = ret;

	ret = qcom_q6v5_init(&pas->q6v5, pdev, rproc, desc->crash_reason_smem,
			     desc->load_state, qcom_pas_handover);
	if (ret)
		goto detach_proxy_pds;

	qcom_add_glink_subdev(rproc, &pas->glink_subdev, desc->ssr_name);
	qcom_add_smd_subdev(rproc, &pas->smd_subdev);
	qcom_add_pdm_subdev(rproc, &pas->pdm_subdev);
	pas->sysmon = qcom_add_sysmon_subdev(rproc, desc->sysmon_name, desc->ssctl_id);
	if (IS_ERR(pas->sysmon)) {
		ret = PTR_ERR(pas->sysmon);
		goto deinit_remove_pdm_smd_glink;
	}

	qcom_add_ssr_subdev(rproc, &pas->ssr_subdev, desc->ssr_name);
	ret = rproc_add(rproc);
	if (ret)
		goto remove_ssr_sysmon;

	return 0;

remove_ssr_sysmon:
	qcom_remove_ssr_subdev(rproc, &pas->ssr_subdev);
	qcom_remove_sysmon_subdev(pas->sysmon);
deinit_remove_pdm_smd_glink:
	qcom_remove_pdm_subdev(rproc, &pas->pdm_subdev);
	qcom_remove_smd_subdev(rproc, &pas->smd_subdev);
	qcom_remove_glink_subdev(rproc, &pas->glink_subdev);
	qcom_q6v5_deinit(&pas->q6v5);
detach_proxy_pds:
	qcom_pas_pds_detach(pas, pas->proxy_pds, pas->proxy_pd_count);
unassign_mem:
	qcom_pas_unassign_memory_region(pas);
free_rproc:
	device_init_wakeup(pas->dev, false);

	return ret;
}

static void qcom_pas_remove(struct platform_device *pdev)
{
	struct qcom_pas *pas = platform_get_drvdata(pdev);

	rproc_del(pas->rproc);

	qcom_q6v5_deinit(&pas->q6v5);
	qcom_pas_unassign_memory_region(pas);
	qcom_remove_glink_subdev(pas->rproc, &pas->glink_subdev);
	qcom_remove_sysmon_subdev(pas->sysmon);
	qcom_remove_smd_subdev(pas->rproc, &pas->smd_subdev);
	qcom_remove_pdm_subdev(pas->rproc, &pas->pdm_subdev);
	qcom_remove_ssr_subdev(pas->rproc, &pas->ssr_subdev);
	qcom_pas_pds_detach(pas, pas->proxy_pds, pas->proxy_pd_count);
	device_init_wakeup(pas->dev, false);
}

static const struct qcom_pas_data adsp_resource_init = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.auto_boot = true,
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct qcom_pas_data sa8775p_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mbn",
	.pas_id = 1,
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

static const struct qcom_pas_data sdm845_adsp_resource_init = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
	.auto_boot = true,
	.load_state = "adsp",
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.ssctl_id = 0x14,
};

static const struct qcom_pas_data sm6350_adsp_resource = {
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

static const struct qcom_pas_data sm6375_mpss_resource = {
	.crash_reason_smem = 421,
	.firmware_name = "modem.mdt",
	.pas_id = 4,
	.minidump_id = 3,
	.auto_boot = false,
	.proxy_pd_names = (char*[]){
		"cx",
		NULL
	},
	.ssr_name = "mpss",
	.sysmon_name = "modem",
	.ssctl_id = 0x12,
};

static const struct qcom_pas_data sm8150_adsp_resource = {
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

static const struct qcom_pas_data sm8250_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.pas_id = 1,
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

static const struct qcom_pas_data sm8350_adsp_resource = {
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

static const struct qcom_pas_data msm8996_adsp_resource = {
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

static const struct qcom_pas_data cdsp_resource_init = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.auto_boot = true,
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct qcom_pas_data sa8775p_cdsp0_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp0.mbn",
	.pas_id = 18,
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

static const struct qcom_pas_data sa8775p_cdsp1_resource = {
	.crash_reason_smem = 633,
	.firmware_name = "cdsp1.mbn",
	.pas_id = 30,
	.minidump_id = 20,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"cx",
		"mxc",
		"nsp",
		NULL
	},
	.load_state = "nsp",
	.ssr_name = "cdsp1",
	.sysmon_name = "cdsp1",
	.ssctl_id = 0x20,
};

static const struct qcom_pas_data sdm845_cdsp_resource_init = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.auto_boot = true,
	.load_state = "cdsp",
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
};

static const struct qcom_pas_data sm6350_cdsp_resource = {
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

static const struct qcom_pas_data sm8150_cdsp_resource = {
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

static const struct qcom_pas_data sm8250_cdsp_resource = {
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

static const struct qcom_pas_data sc8280xp_nsp0_resource = {
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

static const struct qcom_pas_data sc8280xp_nsp1_resource = {
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

static const struct qcom_pas_data x1e80100_adsp_resource = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.dtb_firmware_name = "adsp_dtb.mdt",
	.pas_id = 1,
	.dtb_pas_id = 0x24,
	.lite_pas_id = 0x1f,
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

static const struct qcom_pas_data x1e80100_cdsp_resource = {
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

static const struct qcom_pas_data sm8350_cdsp_resource = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.pas_id = 18,
	.minidump_id = 7,
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

static const struct qcom_pas_data sa8775p_gpdsp0_resource = {
	.crash_reason_smem = 640,
	.firmware_name = "gpdsp0.mbn",
	.pas_id = 39,
	.minidump_id = 21,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"cx",
		"mxc",
		NULL
	},
	.load_state = "gpdsp0",
	.ssr_name = "gpdsp0",
	.sysmon_name = "gpdsp0",
	.ssctl_id = 0x21,
};

static const struct qcom_pas_data sa8775p_gpdsp1_resource = {
	.crash_reason_smem = 641,
	.firmware_name = "gpdsp1.mbn",
	.pas_id = 40,
	.minidump_id = 22,
	.auto_boot = true,
	.proxy_pd_names = (char*[]){
		"cx",
		"mxc",
		NULL
	},
	.load_state = "gpdsp1",
	.ssr_name = "gpdsp1",
	.sysmon_name = "gpdsp1",
	.ssctl_id = 0x22,
};

static const struct qcom_pas_data mpss_resource_init = {
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

static const struct qcom_pas_data sc8180x_mpss_resource = {
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

static const struct qcom_pas_data msm8996_slpi_resource_init = {
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

static const struct qcom_pas_data sdm845_slpi_resource_init = {
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

static const struct qcom_pas_data wcss_resource_init = {
	.crash_reason_smem = 421,
	.firmware_name = "wcnss.mdt",
	.pas_id = 6,
	.auto_boot = true,
	.ssr_name = "mpss",
	.sysmon_name = "wcnss",
	.ssctl_id = 0x12,
};

static const struct qcom_pas_data sdx55_mpss_resource = {
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

static const struct qcom_pas_data sm8450_mpss_resource = {
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

static const struct qcom_pas_data sm8550_adsp_resource = {
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
	.smem_host_id = 2,
};

static const struct qcom_pas_data sm8550_cdsp_resource = {
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
	.smem_host_id = 5,
};

static const struct qcom_pas_data sm8550_mpss_resource = {
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
	.smem_host_id = 1,
	.region_assign_idx = 2,
	.region_assign_count = 1,
	.region_assign_vmid = QCOM_SCM_VMID_MSS_MSA,
};

static const struct qcom_pas_data sc7280_wpss_resource = {
	.crash_reason_smem = 626,
	.firmware_name = "wpss.mdt",
	.pas_id = 6,
	.minidump_id = 4,
	.auto_boot = false,
	.proxy_pd_names = (char*[]){
		"cx",
		"mx",
		NULL
	},
	.load_state = "wpss",
	.ssr_name = "wpss",
	.sysmon_name = "wpss",
	.ssctl_id = 0x19,
};

static const struct qcom_pas_data sm8650_cdsp_resource = {
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
	.smem_host_id = 5,
	.region_assign_idx = 2,
	.region_assign_count = 1,
	.region_assign_shared = true,
	.region_assign_vmid = QCOM_SCM_VMID_CDSP,
};

static const struct qcom_pas_data sm8650_mpss_resource = {
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
	.smem_host_id = 1,
	.region_assign_idx = 2,
	.region_assign_count = 3,
	.region_assign_vmid = QCOM_SCM_VMID_MSS_MSA,
};

static const struct qcom_pas_data sm8750_mpss_resource = {
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
	.smem_host_id = 1,
	.region_assign_idx = 2,
	.region_assign_count = 2,
	.region_assign_vmid = QCOM_SCM_VMID_MSS_MSA,
};

static const struct of_device_id qcom_pas_of_match[] = {
	{ .compatible = "qcom,msm8226-adsp-pil", .data = &msm8996_adsp_resource},
	{ .compatible = "qcom,msm8953-adsp-pil", .data = &msm8996_adsp_resource},
	{ .compatible = "qcom,msm8974-adsp-pil", .data = &adsp_resource_init},
	{ .compatible = "qcom,msm8996-adsp-pil", .data = &msm8996_adsp_resource},
	{ .compatible = "qcom,msm8996-slpi-pil", .data = &msm8996_slpi_resource_init},
	{ .compatible = "qcom,msm8998-adsp-pas", .data = &msm8996_adsp_resource},
	{ .compatible = "qcom,msm8998-slpi-pas", .data = &msm8996_slpi_resource_init},
	{ .compatible = "qcom,qcs404-adsp-pas", .data = &adsp_resource_init },
	{ .compatible = "qcom,qcs404-cdsp-pas", .data = &cdsp_resource_init },
	{ .compatible = "qcom,qcs404-wcss-pas", .data = &wcss_resource_init },
	{ .compatible = "qcom,sa8775p-adsp-pas", .data = &sa8775p_adsp_resource},
	{ .compatible = "qcom,sa8775p-cdsp0-pas", .data = &sa8775p_cdsp0_resource},
	{ .compatible = "qcom,sa8775p-cdsp1-pas", .data = &sa8775p_cdsp1_resource},
	{ .compatible = "qcom,sa8775p-gpdsp0-pas", .data = &sa8775p_gpdsp0_resource},
	{ .compatible = "qcom,sa8775p-gpdsp1-pas", .data = &sa8775p_gpdsp1_resource},
	{ .compatible = "qcom,sar2130p-adsp-pas", .data = &sm8350_adsp_resource},
	{ .compatible = "qcom,sc7180-adsp-pas", .data = &sm8250_adsp_resource},
	{ .compatible = "qcom,sc7180-mpss-pas", .data = &mpss_resource_init},
	{ .compatible = "qcom,sc7280-adsp-pas", .data = &sm8350_adsp_resource},
	{ .compatible = "qcom,sc7280-cdsp-pas", .data = &sm6350_cdsp_resource},
	{ .compatible = "qcom,sc7280-mpss-pas", .data = &mpss_resource_init},
	{ .compatible = "qcom,sc7280-wpss-pas", .data = &sc7280_wpss_resource},
	{ .compatible = "qcom,sc8180x-adsp-pas", .data = &sm8150_adsp_resource},
	{ .compatible = "qcom,sc8180x-cdsp-pas", .data = &sm8150_cdsp_resource},
	{ .compatible = "qcom,sc8180x-mpss-pas", .data = &sc8180x_mpss_resource},
	{ .compatible = "qcom,sc8280xp-adsp-pas", .data = &sm8250_adsp_resource},
	{ .compatible = "qcom,sc8280xp-nsp0-pas", .data = &sc8280xp_nsp0_resource},
	{ .compatible = "qcom,sc8280xp-nsp1-pas", .data = &sc8280xp_nsp1_resource},
	{ .compatible = "qcom,sdm660-adsp-pas", .data = &adsp_resource_init},
	{ .compatible = "qcom,sdm845-adsp-pas", .data = &sdm845_adsp_resource_init},
	{ .compatible = "qcom,sdm845-cdsp-pas", .data = &sdm845_cdsp_resource_init},
	{ .compatible = "qcom,sdm845-slpi-pas", .data = &sdm845_slpi_resource_init},
	{ .compatible = "qcom,sdx55-mpss-pas", .data = &sdx55_mpss_resource},
	{ .compatible = "qcom,sdx75-mpss-pas", .data = &sm8650_mpss_resource},
	{ .compatible = "qcom,sm6115-adsp-pas", .data = &adsp_resource_init},
	{ .compatible = "qcom,sm6115-cdsp-pas", .data = &cdsp_resource_init},
	{ .compatible = "qcom,sm6115-mpss-pas", .data = &sc8180x_mpss_resource},
	{ .compatible = "qcom,sm6350-adsp-pas", .data = &sm6350_adsp_resource},
	{ .compatible = "qcom,sm6350-cdsp-pas", .data = &sm6350_cdsp_resource},
	{ .compatible = "qcom,sm6350-mpss-pas", .data = &mpss_resource_init},
	{ .compatible = "qcom,sm6375-adsp-pas", .data = &sm6350_adsp_resource},
	{ .compatible = "qcom,sm6375-cdsp-pas", .data = &sm8150_cdsp_resource},
	{ .compatible = "qcom,sm6375-mpss-pas", .data = &sm6375_mpss_resource},
	{ .compatible = "qcom,sm8150-adsp-pas", .data = &sm8150_adsp_resource},
	{ .compatible = "qcom,sm8150-cdsp-pas", .data = &sm8150_cdsp_resource},
	{ .compatible = "qcom,sm8150-mpss-pas", .data = &mpss_resource_init},
	{ .compatible = "qcom,sm8150-slpi-pas", .data = &sdm845_slpi_resource_init},
	{ .compatible = "qcom,sm8250-adsp-pas", .data = &sm8250_adsp_resource},
	{ .compatible = "qcom,sm8250-cdsp-pas", .data = &sm8250_cdsp_resource},
	{ .compatible = "qcom,sm8250-slpi-pas", .data = &sdm845_slpi_resource_init},
	{ .compatible = "qcom,sm8350-adsp-pas", .data = &sm8350_adsp_resource},
	{ .compatible = "qcom,sm8350-cdsp-pas", .data = &sm8350_cdsp_resource},
	{ .compatible = "qcom,sm8350-slpi-pas", .data = &sdm845_slpi_resource_init},
	{ .compatible = "qcom,sm8350-mpss-pas", .data = &mpss_resource_init},
	{ .compatible = "qcom,sm8450-adsp-pas", .data = &sm8350_adsp_resource},
	{ .compatible = "qcom,sm8450-cdsp-pas", .data = &sm8350_cdsp_resource},
	{ .compatible = "qcom,sm8450-slpi-pas", .data = &sdm845_slpi_resource_init},
	{ .compatible = "qcom,sm8450-mpss-pas", .data = &sm8450_mpss_resource},
	{ .compatible = "qcom,sm8550-adsp-pas", .data = &sm8550_adsp_resource},
	{ .compatible = "qcom,sm8550-cdsp-pas", .data = &sm8550_cdsp_resource},
	{ .compatible = "qcom,sm8550-mpss-pas", .data = &sm8550_mpss_resource},
	{ .compatible = "qcom,sm8650-adsp-pas", .data = &sm8550_adsp_resource},
	{ .compatible = "qcom,sm8650-cdsp-pas", .data = &sm8650_cdsp_resource},
	{ .compatible = "qcom,sm8650-mpss-pas", .data = &sm8650_mpss_resource},
	{ .compatible = "qcom,sm8750-mpss-pas", .data = &sm8750_mpss_resource},
	{ .compatible = "qcom,x1e80100-adsp-pas", .data = &x1e80100_adsp_resource},
	{ .compatible = "qcom,x1e80100-cdsp-pas", .data = &x1e80100_cdsp_resource},
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_pas_of_match);

static struct platform_driver qcom_pas_driver = {
	.probe = qcom_pas_probe,
	.remove = qcom_pas_remove,
	.driver = {
		.name = "qcom_q6v5_pas",
		.of_match_table = qcom_pas_of_match,
	},
};

module_platform_driver(qcom_pas_driver);
MODULE_DESCRIPTION("Qualcomm Peripheral Authentication Service remoteproc driver");
MODULE_LICENSE("GPL v2");
