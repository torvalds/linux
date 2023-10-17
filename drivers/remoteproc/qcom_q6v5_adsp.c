// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm Technology Inc. ADSP Peripheral Image Loader for SDM845.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/smem_state.h>

#include "qcom_common.h"
#include "qcom_pil_info.h"
#include "qcom_q6v5.h"
#include "remoteproc_internal.h"

/* time out value */
#define ACK_TIMEOUT			1000
#define ACK_TIMEOUT_US			1000000
#define BOOT_FSM_TIMEOUT		10000
/* mask values */
#define EVB_MASK			GENMASK(27, 4)
/*QDSP6SS register offsets*/
#define RST_EVB_REG			0x10
#define CORE_START_REG			0x400
#define BOOT_CMD_REG			0x404
#define BOOT_STATUS_REG			0x408
#define RET_CFG_REG			0x1C
/*TCSR register offsets*/
#define LPASS_MASTER_IDLE_REG		0x8
#define LPASS_HALTACK_REG		0x4
#define LPASS_PWR_ON_REG		0x10
#define LPASS_HALTREQ_REG		0x0

#define SID_MASK_DEFAULT        0xF

#define QDSP6SS_XO_CBCR		0x38
#define QDSP6SS_CORE_CBCR	0x20
#define QDSP6SS_SLEEP_CBCR	0x3c

#define QCOM_Q6V5_RPROC_PROXY_PD_MAX	3

#define LPASS_BOOT_CORE_START	BIT(0)
#define LPASS_BOOT_CMD_START	BIT(0)
#define LPASS_EFUSE_Q6SS_EVB_SEL 0x0

struct adsp_pil_data {
	int crash_reason_smem;
	const char *firmware_name;

	const char *ssr_name;
	const char *sysmon_name;
	int ssctl_id;
	bool is_wpss;
	bool has_iommu;
	bool auto_boot;

	const char **clk_ids;
	int num_clks;
	const char **proxy_pd_names;
	const char *load_state;
};

struct qcom_adsp {
	struct device *dev;
	struct rproc *rproc;

	struct qcom_q6v5 q6v5;

	struct clk *xo;

	int num_clks;
	struct clk_bulk_data *clks;

	void __iomem *qdsp6ss_base;
	void __iomem *lpass_efuse;

	struct reset_control *pdc_sync_reset;
	struct reset_control *restart;

	struct regmap *halt_map;
	unsigned int halt_lpass;

	int crash_reason_smem;
	const char *info_name;

	struct completion start_done;
	struct completion stop_done;

	phys_addr_t mem_phys;
	phys_addr_t mem_reloc;
	void *mem_region;
	size_t mem_size;
	bool has_iommu;

	struct device *proxy_pds[QCOM_Q6V5_RPROC_PROXY_PD_MAX];
	size_t proxy_pd_count;

	struct qcom_rproc_glink glink_subdev;
	struct qcom_rproc_ssr ssr_subdev;
	struct qcom_sysmon *sysmon;

	int (*shutdown)(struct qcom_adsp *adsp);
};

static int qcom_rproc_pds_attach(struct device *dev, struct qcom_adsp *adsp,
				 const char **pd_names)
{
	struct device **devs = adsp->proxy_pds;
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

	if (num_pds > ARRAY_SIZE(adsp->proxy_pds))
		return -E2BIG;

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
}

static void qcom_rproc_pds_detach(struct qcom_adsp *adsp, struct device **pds,
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

static int qcom_rproc_pds_enable(struct qcom_adsp *adsp, struct device **pds,
				 size_t pd_count)
{
	int ret;
	int i;

	for (i = 0; i < pd_count; i++) {
		dev_pm_genpd_set_performance_state(pds[i], INT_MAX);
		ret = pm_runtime_resume_and_get(pds[i]);
		if (ret < 0) {
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
}

static void qcom_rproc_pds_disable(struct qcom_adsp *adsp, struct device **pds,
				   size_t pd_count)
{
	int i;

	for (i = 0; i < pd_count; i++) {
		dev_pm_genpd_set_performance_state(pds[i], 0);
		pm_runtime_put(pds[i]);
	}
}

static int qcom_wpss_shutdown(struct qcom_adsp *adsp)
{
	unsigned int val;

	regmap_write(adsp->halt_map, adsp->halt_lpass + LPASS_HALTREQ_REG, 1);

	/* Wait for halt ACK from QDSP6 */
	regmap_read_poll_timeout(adsp->halt_map,
				 adsp->halt_lpass + LPASS_HALTACK_REG, val,
				 val, 1000, ACK_TIMEOUT_US);

	/* Assert the WPSS PDC Reset */
	reset_control_assert(adsp->pdc_sync_reset);

	/* Place the WPSS processor into reset */
	reset_control_assert(adsp->restart);

	/* wait after asserting subsystem restart from AOSS */
	usleep_range(200, 205);

	/* Remove the WPSS reset */
	reset_control_deassert(adsp->restart);

	/* De-assert the WPSS PDC Reset */
	reset_control_deassert(adsp->pdc_sync_reset);

	usleep_range(100, 105);

	clk_bulk_disable_unprepare(adsp->num_clks, adsp->clks);

	regmap_write(adsp->halt_map, adsp->halt_lpass + LPASS_HALTREQ_REG, 0);

	/* Wait for halt ACK from QDSP6 */
	regmap_read_poll_timeout(adsp->halt_map,
				 adsp->halt_lpass + LPASS_HALTACK_REG, val,
				 !val, 1000, ACK_TIMEOUT_US);

	return 0;
}

static int qcom_adsp_shutdown(struct qcom_adsp *adsp)
{
	unsigned long timeout;
	unsigned int val;
	int ret;

	/* Reset the retention logic */
	val = readl(adsp->qdsp6ss_base + RET_CFG_REG);
	val |= 0x1;
	writel(val, adsp->qdsp6ss_base + RET_CFG_REG);

	clk_bulk_disable_unprepare(adsp->num_clks, adsp->clks);

	/* QDSP6 master port needs to be explicitly halted */
	ret = regmap_read(adsp->halt_map,
			adsp->halt_lpass + LPASS_PWR_ON_REG, &val);
	if (ret || !val)
		goto reset;

	ret = regmap_read(adsp->halt_map,
			adsp->halt_lpass + LPASS_MASTER_IDLE_REG,
			&val);
	if (ret || val)
		goto reset;

	regmap_write(adsp->halt_map,
			adsp->halt_lpass + LPASS_HALTREQ_REG, 1);

	/* Wait for halt ACK from QDSP6 */
	timeout = jiffies + msecs_to_jiffies(ACK_TIMEOUT);
	for (;;) {
		ret = regmap_read(adsp->halt_map,
			adsp->halt_lpass + LPASS_HALTACK_REG, &val);
		if (ret || val || time_after(jiffies, timeout))
			break;

		usleep_range(1000, 1100);
	}

	ret = regmap_read(adsp->halt_map,
			adsp->halt_lpass + LPASS_MASTER_IDLE_REG, &val);
	if (ret || !val)
		dev_err(adsp->dev, "port failed halt\n");

reset:
	/* Assert the LPASS PDC Reset */
	reset_control_assert(adsp->pdc_sync_reset);
	/* Place the LPASS processor into reset */
	reset_control_assert(adsp->restart);
	/* wait after asserting subsystem restart from AOSS */
	usleep_range(200, 300);

	/* Clear the halt request for the AXIM and AHBM for Q6 */
	regmap_write(adsp->halt_map, adsp->halt_lpass + LPASS_HALTREQ_REG, 0);

	/* De-assert the LPASS PDC Reset */
	reset_control_deassert(adsp->pdc_sync_reset);
	/* Remove the LPASS reset */
	reset_control_deassert(adsp->restart);
	/* wait after de-asserting subsystem restart from AOSS */
	usleep_range(200, 300);

	return 0;
}

static int adsp_load(struct rproc *rproc, const struct firmware *fw)
{
	struct qcom_adsp *adsp = rproc->priv;
	int ret;

	ret = qcom_mdt_load_no_init(adsp->dev, fw, rproc->firmware, 0,
				    adsp->mem_region, adsp->mem_phys,
				    adsp->mem_size, &adsp->mem_reloc);
	if (ret)
		return ret;

	qcom_pil_info_store(adsp->info_name, adsp->mem_phys, adsp->mem_size);

	return 0;
}

static void adsp_unmap_carveout(struct rproc *rproc)
{
	struct qcom_adsp *adsp = rproc->priv;

	if (adsp->has_iommu)
		iommu_unmap(rproc->domain, adsp->mem_phys, adsp->mem_size);
}

static int adsp_map_carveout(struct rproc *rproc)
{
	struct qcom_adsp *adsp = rproc->priv;
	struct of_phandle_args args;
	long long sid;
	unsigned long iova;
	int ret;

	if (!adsp->has_iommu)
		return 0;

	if (!rproc->domain)
		return -EINVAL;

	ret = of_parse_phandle_with_args(adsp->dev->of_node, "iommus", "#iommu-cells", 0, &args);
	if (ret < 0)
		return ret;

	sid = args.args[0] & SID_MASK_DEFAULT;

	/* Add SID configuration for ADSP Firmware to SMMU */
	iova =  adsp->mem_phys | (sid << 32);

	ret = iommu_map(rproc->domain, iova, adsp->mem_phys,
			adsp->mem_size,	IOMMU_READ | IOMMU_WRITE,
			GFP_KERNEL);
	if (ret) {
		dev_err(adsp->dev, "Unable to map ADSP Physical Memory\n");
		return ret;
	}

	return 0;
}

static int adsp_start(struct rproc *rproc)
{
	struct qcom_adsp *adsp = rproc->priv;
	int ret;
	unsigned int val;

	ret = qcom_q6v5_prepare(&adsp->q6v5);
	if (ret)
		return ret;

	ret = adsp_map_carveout(rproc);
	if (ret) {
		dev_err(adsp->dev, "ADSP smmu mapping failed\n");
		goto disable_irqs;
	}

	ret = clk_prepare_enable(adsp->xo);
	if (ret)
		goto adsp_smmu_unmap;

	ret = qcom_rproc_pds_enable(adsp, adsp->proxy_pds,
				    adsp->proxy_pd_count);
	if (ret < 0)
		goto disable_xo_clk;

	ret = clk_bulk_prepare_enable(adsp->num_clks, adsp->clks);
	if (ret) {
		dev_err(adsp->dev, "adsp clk_enable failed\n");
		goto disable_power_domain;
	}

	/* Enable the XO clock */
	writel(1, adsp->qdsp6ss_base + QDSP6SS_XO_CBCR);

	/* Enable the QDSP6SS sleep clock */
	writel(1, adsp->qdsp6ss_base + QDSP6SS_SLEEP_CBCR);

	/* Enable the QDSP6 core clock */
	writel(1, adsp->qdsp6ss_base + QDSP6SS_CORE_CBCR);

	/* Program boot address */
	writel(adsp->mem_phys >> 4, adsp->qdsp6ss_base + RST_EVB_REG);

	if (adsp->lpass_efuse)
		writel(LPASS_EFUSE_Q6SS_EVB_SEL, adsp->lpass_efuse);

	/* De-assert QDSP6 stop core. QDSP6 will execute after out of reset */
	writel(LPASS_BOOT_CORE_START, adsp->qdsp6ss_base + CORE_START_REG);

	/* Trigger boot FSM to start QDSP6 */
	writel(LPASS_BOOT_CMD_START, adsp->qdsp6ss_base + BOOT_CMD_REG);

	/* Wait for core to come out of reset */
	ret = readl_poll_timeout(adsp->qdsp6ss_base + BOOT_STATUS_REG,
			val, (val & BIT(0)) != 0, 10, BOOT_FSM_TIMEOUT);
	if (ret) {
		dev_err(adsp->dev, "failed to bootup adsp\n");
		goto disable_adsp_clks;
	}

	ret = qcom_q6v5_wait_for_start(&adsp->q6v5, msecs_to_jiffies(5 * HZ));
	if (ret == -ETIMEDOUT) {
		dev_err(adsp->dev, "start timed out\n");
		goto disable_adsp_clks;
	}

	return 0;

disable_adsp_clks:
	clk_bulk_disable_unprepare(adsp->num_clks, adsp->clks);
disable_power_domain:
	qcom_rproc_pds_disable(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
disable_xo_clk:
	clk_disable_unprepare(adsp->xo);
adsp_smmu_unmap:
	adsp_unmap_carveout(rproc);
disable_irqs:
	qcom_q6v5_unprepare(&adsp->q6v5);

	return ret;
}

static void qcom_adsp_pil_handover(struct qcom_q6v5 *q6v5)
{
	struct qcom_adsp *adsp = container_of(q6v5, struct qcom_adsp, q6v5);

	clk_disable_unprepare(adsp->xo);
	qcom_rproc_pds_disable(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
}

static int adsp_stop(struct rproc *rproc)
{
	struct qcom_adsp *adsp = rproc->priv;
	int handover;
	int ret;

	ret = qcom_q6v5_request_stop(&adsp->q6v5, adsp->sysmon);
	if (ret == -ETIMEDOUT)
		dev_err(adsp->dev, "timed out on wait\n");

	ret = adsp->shutdown(adsp);
	if (ret)
		dev_err(adsp->dev, "failed to shutdown: %d\n", ret);

	adsp_unmap_carveout(rproc);

	handover = qcom_q6v5_unprepare(&adsp->q6v5);
	if (handover)
		qcom_adsp_pil_handover(&adsp->q6v5);

	return ret;
}

static void *adsp_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct qcom_adsp *adsp = rproc->priv;
	int offset;

	offset = da - adsp->mem_reloc;
	if (offset < 0 || offset + len > adsp->mem_size)
		return NULL;

	return adsp->mem_region + offset;
}

static int adsp_parse_firmware(struct rproc *rproc, const struct firmware *fw)
{
	struct qcom_adsp *adsp = rproc->priv;
	int ret;

	ret = qcom_register_dump_segments(rproc, fw);
	if (ret) {
		dev_err(&rproc->dev, "Error in registering dump segments\n");
		return ret;
	}

	if (adsp->has_iommu) {
		ret = rproc_elf_load_rsc_table(rproc, fw);
		if (ret) {
			dev_err(&rproc->dev, "Error in loading resource table\n");
			return ret;
		}
	}
	return 0;
}

static unsigned long adsp_panic(struct rproc *rproc)
{
	struct qcom_adsp *adsp = rproc->priv;

	return qcom_q6v5_panic(&adsp->q6v5);
}

static const struct rproc_ops adsp_ops = {
	.start = adsp_start,
	.stop = adsp_stop,
	.da_to_va = adsp_da_to_va,
	.parse_fw = adsp_parse_firmware,
	.load = adsp_load,
	.panic = adsp_panic,
};

static int adsp_init_clock(struct qcom_adsp *adsp, const char **clk_ids)
{
	int num_clks = 0;
	int i, ret;

	adsp->xo = devm_clk_get(adsp->dev, "xo");
	if (IS_ERR(adsp->xo)) {
		ret = PTR_ERR(adsp->xo);
		if (ret != -EPROBE_DEFER)
			dev_err(adsp->dev, "failed to get xo clock");
		return ret;
	}

	for (i = 0; clk_ids[i]; i++)
		num_clks++;

	adsp->num_clks = num_clks;
	adsp->clks = devm_kcalloc(adsp->dev, adsp->num_clks,
				sizeof(*adsp->clks), GFP_KERNEL);
	if (!adsp->clks)
		return -ENOMEM;

	for (i = 0; i < adsp->num_clks; i++)
		adsp->clks[i].id = clk_ids[i];

	return devm_clk_bulk_get(adsp->dev, adsp->num_clks, adsp->clks);
}

static int adsp_init_reset(struct qcom_adsp *adsp)
{
	adsp->pdc_sync_reset = devm_reset_control_get_optional_exclusive(adsp->dev,
			"pdc_sync");
	if (IS_ERR(adsp->pdc_sync_reset)) {
		dev_err(adsp->dev, "failed to acquire pdc_sync reset\n");
		return PTR_ERR(adsp->pdc_sync_reset);
	}

	adsp->restart = devm_reset_control_get_optional_exclusive(adsp->dev, "restart");

	/* Fall back to the  old "cc_lpass" if "restart" is absent */
	if (!adsp->restart)
		adsp->restart = devm_reset_control_get_exclusive(adsp->dev, "cc_lpass");

	if (IS_ERR(adsp->restart)) {
		dev_err(adsp->dev, "failed to acquire restart\n");
		return PTR_ERR(adsp->restart);
	}

	return 0;
}

static int adsp_init_mmio(struct qcom_adsp *adsp,
				struct platform_device *pdev)
{
	struct resource *efuse_region;
	struct device_node *syscon;
	int ret;

	adsp->qdsp6ss_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(adsp->qdsp6ss_base)) {
		dev_err(adsp->dev, "failed to map QDSP6SS registers\n");
		return PTR_ERR(adsp->qdsp6ss_base);
	}

	efuse_region = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!efuse_region) {
		adsp->lpass_efuse = NULL;
		dev_dbg(adsp->dev, "failed to get efuse memory region\n");
	} else {
		adsp->lpass_efuse = devm_ioremap_resource(&pdev->dev, efuse_region);
		if (IS_ERR(adsp->lpass_efuse)) {
			dev_err(adsp->dev, "failed to map efuse registers\n");
			return PTR_ERR(adsp->lpass_efuse);
		}
	}
	syscon = of_parse_phandle(pdev->dev.of_node, "qcom,halt-regs", 0);
	if (!syscon) {
		dev_err(&pdev->dev, "failed to parse qcom,halt-regs\n");
		return -EINVAL;
	}

	adsp->halt_map = syscon_node_to_regmap(syscon);
	of_node_put(syscon);
	if (IS_ERR(adsp->halt_map))
		return PTR_ERR(adsp->halt_map);

	ret = of_property_read_u32_index(pdev->dev.of_node, "qcom,halt-regs",
			1, &adsp->halt_lpass);
	if (ret < 0) {
		dev_err(&pdev->dev, "no offset in syscon\n");
		return ret;
	}

	return 0;
}

static int adsp_alloc_memory_region(struct qcom_adsp *adsp)
{
	struct reserved_mem *rmem = NULL;
	struct device_node *node;

	node = of_parse_phandle(adsp->dev->of_node, "memory-region", 0);
	if (node)
		rmem = of_reserved_mem_lookup(node);
	of_node_put(node);

	if (!rmem) {
		dev_err(adsp->dev, "unable to resolve memory-region\n");
		return -EINVAL;
	}

	adsp->mem_phys = adsp->mem_reloc = rmem->base;
	adsp->mem_size = rmem->size;
	adsp->mem_region = devm_ioremap_wc(adsp->dev,
				adsp->mem_phys, adsp->mem_size);
	if (!adsp->mem_region) {
		dev_err(adsp->dev, "unable to map memory region: %pa+%zx\n",
			&rmem->base, adsp->mem_size);
		return -EBUSY;
	}

	return 0;
}

static int adsp_probe(struct platform_device *pdev)
{
	const struct adsp_pil_data *desc;
	const char *firmware_name;
	struct qcom_adsp *adsp;
	struct rproc *rproc;
	int ret;

	desc = of_device_get_match_data(&pdev->dev);
	if (!desc)
		return -EINVAL;

	firmware_name = desc->firmware_name;
	ret = of_property_read_string(pdev->dev.of_node, "firmware-name",
				      &firmware_name);
	if (ret < 0 && ret != -EINVAL) {
		dev_err(&pdev->dev, "unable to read firmware-name\n");
		return ret;
	}

	rproc = rproc_alloc(&pdev->dev, pdev->name, &adsp_ops,
			    firmware_name, sizeof(*adsp));
	if (!rproc) {
		dev_err(&pdev->dev, "unable to allocate remoteproc\n");
		return -ENOMEM;
	}

	rproc->auto_boot = desc->auto_boot;
	rproc->has_iommu = desc->has_iommu;
	rproc_coredump_set_elf_info(rproc, ELFCLASS32, EM_NONE);

	adsp = rproc->priv;
	adsp->dev = &pdev->dev;
	adsp->rproc = rproc;
	adsp->info_name = desc->sysmon_name;
	adsp->has_iommu = desc->has_iommu;

	platform_set_drvdata(pdev, adsp);

	if (desc->is_wpss)
		adsp->shutdown = qcom_wpss_shutdown;
	else
		adsp->shutdown = qcom_adsp_shutdown;

	ret = adsp_alloc_memory_region(adsp);
	if (ret)
		goto free_rproc;

	ret = adsp_init_clock(adsp, desc->clk_ids);
	if (ret)
		goto free_rproc;

	ret = qcom_rproc_pds_attach(adsp->dev, adsp,
				    desc->proxy_pd_names);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to attach proxy power domains\n");
		goto free_rproc;
	}
	adsp->proxy_pd_count = ret;

	ret = adsp_init_reset(adsp);
	if (ret)
		goto disable_pm;

	ret = adsp_init_mmio(adsp, pdev);
	if (ret)
		goto disable_pm;

	ret = qcom_q6v5_init(&adsp->q6v5, pdev, rproc, desc->crash_reason_smem,
			     desc->load_state, qcom_adsp_pil_handover);
	if (ret)
		goto disable_pm;

	qcom_add_glink_subdev(rproc, &adsp->glink_subdev, desc->ssr_name);
	qcom_add_ssr_subdev(rproc, &adsp->ssr_subdev, desc->ssr_name);
	adsp->sysmon = qcom_add_sysmon_subdev(rproc,
					      desc->sysmon_name,
					      desc->ssctl_id);
	if (IS_ERR(adsp->sysmon)) {
		ret = PTR_ERR(adsp->sysmon);
		goto disable_pm;
	}

	ret = rproc_add(rproc);
	if (ret)
		goto disable_pm;

	return 0;

disable_pm:
	qcom_rproc_pds_detach(adsp, adsp->proxy_pds, adsp->proxy_pd_count);

free_rproc:
	rproc_free(rproc);

	return ret;
}

static void adsp_remove(struct platform_device *pdev)
{
	struct qcom_adsp *adsp = platform_get_drvdata(pdev);

	rproc_del(adsp->rproc);

	qcom_q6v5_deinit(&adsp->q6v5);
	qcom_remove_glink_subdev(adsp->rproc, &adsp->glink_subdev);
	qcom_remove_sysmon_subdev(adsp->sysmon);
	qcom_remove_ssr_subdev(adsp->rproc, &adsp->ssr_subdev);
	qcom_rproc_pds_detach(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
	rproc_free(adsp->rproc);
}

static const struct adsp_pil_data adsp_resource_init = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.mdt",
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.ssctl_id = 0x14,
	.is_wpss = false,
	.auto_boot = true,
	.clk_ids = (const char*[]) {
		"sway_cbcr", "lpass_ahbs_aon_cbcr", "lpass_ahbm_aon_cbcr",
		"qdsp6ss_xo", "qdsp6ss_sleep", "qdsp6ss_core", NULL
	},
	.num_clks = 7,
	.proxy_pd_names = (const char*[]) {
		"cx", NULL
	},
};

static const struct adsp_pil_data adsp_sc7280_resource_init = {
	.crash_reason_smem = 423,
	.firmware_name = "adsp.pbn",
	.load_state = "adsp",
	.ssr_name = "lpass",
	.sysmon_name = "adsp",
	.ssctl_id = 0x14,
	.has_iommu = true,
	.auto_boot = true,
	.clk_ids = (const char*[]) {
		"gcc_cfg_noc_lpass", NULL
	},
	.num_clks = 1,
};

static const struct adsp_pil_data cdsp_resource_init = {
	.crash_reason_smem = 601,
	.firmware_name = "cdsp.mdt",
	.ssr_name = "cdsp",
	.sysmon_name = "cdsp",
	.ssctl_id = 0x17,
	.is_wpss = false,
	.auto_boot = true,
	.clk_ids = (const char*[]) {
		"sway", "tbu", "bimc", "ahb_aon", "q6ss_slave", "q6ss_master",
		"q6_axim", NULL
	},
	.num_clks = 7,
	.proxy_pd_names = (const char*[]) {
		"cx", NULL
	},
};

static const struct adsp_pil_data wpss_resource_init = {
	.crash_reason_smem = 626,
	.firmware_name = "wpss.mdt",
	.ssr_name = "wpss",
	.sysmon_name = "wpss",
	.ssctl_id = 0x19,
	.is_wpss = true,
	.auto_boot = false,
	.load_state = "wpss",
	.clk_ids = (const char*[]) {
		"ahb_bdg", "ahb", "rscp", NULL
	},
	.num_clks = 3,
	.proxy_pd_names = (const char*[]) {
		"cx", "mx", NULL
	},
};

static const struct of_device_id adsp_of_match[] = {
	{ .compatible = "qcom,qcs404-cdsp-pil", .data = &cdsp_resource_init },
	{ .compatible = "qcom,sc7280-adsp-pil", .data = &adsp_sc7280_resource_init },
	{ .compatible = "qcom,sc7280-wpss-pil", .data = &wpss_resource_init },
	{ .compatible = "qcom,sdm845-adsp-pil", .data = &adsp_resource_init },
	{ },
};
MODULE_DEVICE_TABLE(of, adsp_of_match);

static struct platform_driver adsp_pil_driver = {
	.probe = adsp_probe,
	.remove_new = adsp_remove,
	.driver = {
		.name = "qcom_q6v5_adsp",
		.of_match_table = adsp_of_match,
	},
};

module_platform_driver(adsp_pil_driver);
MODULE_DESCRIPTION("QTI SDM845 ADSP Peripheral Image Loader");
MODULE_LICENSE("GPL v2");
