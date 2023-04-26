// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Wireless Connectivity Subsystem Peripheral Image Loader
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
#include <linux/io.h>
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
#include "remoteproc_internal.h"
#include "qcom_pil_info.h"
#include "qcom_wcnss.h"

#define WCNSS_CRASH_REASON_SMEM		422
#define WCNSS_FIRMWARE_NAME		"wcnss.mdt"
#define WCNSS_PAS_ID			6
#define WCNSS_SSCTL_ID			0x13

#define WCNSS_SPARE_NVBIN_DLND		BIT(25)

#define WCNSS_PMU_IRIS_XO_CFG		BIT(3)
#define WCNSS_PMU_IRIS_XO_EN		BIT(4)
#define WCNSS_PMU_GC_BUS_MUX_SEL_TOP	BIT(5)
#define WCNSS_PMU_IRIS_XO_CFG_STS	BIT(6) /* 1: in progress, 0: done */

#define WCNSS_PMU_IRIS_RESET		BIT(7)
#define WCNSS_PMU_IRIS_RESET_STS	BIT(8) /* 1: in progress, 0: done */
#define WCNSS_PMU_IRIS_XO_READ		BIT(9)
#define WCNSS_PMU_IRIS_XO_READ_STS	BIT(10)

#define WCNSS_PMU_XO_MODE_MASK		GENMASK(2, 1)
#define WCNSS_PMU_XO_MODE_19p2		0
#define WCNSS_PMU_XO_MODE_48		3

#define WCNSS_MAX_PDS			2

struct wcnss_data {
	size_t pmu_offset;
	size_t spare_offset;

	const char *pd_names[WCNSS_MAX_PDS];
	const struct wcnss_vreg_info *vregs;
	size_t num_vregs, num_pd_vregs;
};

struct qcom_wcnss {
	struct device *dev;
	struct rproc *rproc;

	void __iomem *pmu_cfg;
	void __iomem *spare_out;

	bool use_48mhz_xo;

	int wdog_irq;
	int fatal_irq;
	int ready_irq;
	int handover_irq;
	int stop_ack_irq;

	struct qcom_smem_state *state;
	unsigned stop_bit;

	struct mutex iris_lock;
	struct qcom_iris *iris;

	struct device *pds[WCNSS_MAX_PDS];
	size_t num_pds;
	struct regulator_bulk_data *vregs;
	size_t num_vregs;

	struct completion start_done;
	struct completion stop_done;

	phys_addr_t mem_phys;
	phys_addr_t mem_reloc;
	void *mem_region;
	size_t mem_size;

	struct qcom_rproc_subdev smd_subdev;
	struct qcom_sysmon *sysmon;
};

static const struct wcnss_data riva_data = {
	.pmu_offset = 0x28,
	.spare_offset = 0xb4,

	.vregs = (struct wcnss_vreg_info[]) {
		{ "vddmx",  1050000, 1150000, 0 },
		{ "vddcx",  1050000, 1150000, 0 },
		{ "vddpx",  1800000, 1800000, 0 },
	},
	.num_vregs = 3,
};

static const struct wcnss_data pronto_v1_data = {
	.pmu_offset = 0x1004,
	.spare_offset = 0x1088,

	.pd_names = { "mx", "cx" },
	.vregs = (struct wcnss_vreg_info[]) {
		{ "vddmx", 950000, 1150000, 0 },
		{ "vddcx", .super_turbo = true},
		{ "vddpx", 1800000, 1800000, 0 },
	},
	.num_pd_vregs = 2,
	.num_vregs = 1,
};

static const struct wcnss_data pronto_v2_data = {
	.pmu_offset = 0x1004,
	.spare_offset = 0x1088,

	.pd_names = { "mx", "cx" },
	.vregs = (struct wcnss_vreg_info[]) {
		{ "vddmx", 1287500, 1287500, 0 },
		{ "vddcx", .super_turbo = true },
		{ "vddpx", 1800000, 1800000, 0 },
	},
	.num_pd_vregs = 2,
	.num_vregs = 1,
};

static const struct wcnss_data pronto_v3_data = {
	.pmu_offset = 0x1004,
	.spare_offset = 0x1088,

	.pd_names = { "mx", "cx" },
	.vregs = (struct wcnss_vreg_info[]) {
		{ "vddpx", 1800000, 1800000, 0 },
	},
	.num_vregs = 1,
};

static int wcnss_load(struct rproc *rproc, const struct firmware *fw)
{
	struct qcom_wcnss *wcnss = (struct qcom_wcnss *)rproc->priv;
	int ret;

	ret = qcom_mdt_load(wcnss->dev, fw, rproc->firmware, WCNSS_PAS_ID,
			    wcnss->mem_region, wcnss->mem_phys,
			    wcnss->mem_size, &wcnss->mem_reloc);
	if (ret)
		return ret;

	qcom_pil_info_store("wcnss", wcnss->mem_phys, wcnss->mem_size);

	return 0;
}

static void wcnss_indicate_nv_download(struct qcom_wcnss *wcnss)
{
	u32 val;

	/* Indicate NV download capability */
	val = readl(wcnss->spare_out);
	val |= WCNSS_SPARE_NVBIN_DLND;
	writel(val, wcnss->spare_out);
}

static void wcnss_configure_iris(struct qcom_wcnss *wcnss)
{
	u32 val;

	/* Clear PMU cfg register */
	writel(0, wcnss->pmu_cfg);

	val = WCNSS_PMU_GC_BUS_MUX_SEL_TOP | WCNSS_PMU_IRIS_XO_EN;
	writel(val, wcnss->pmu_cfg);

	/* Clear XO_MODE */
	val &= ~WCNSS_PMU_XO_MODE_MASK;
	if (wcnss->use_48mhz_xo)
		val |= WCNSS_PMU_XO_MODE_48 << 1;
	else
		val |= WCNSS_PMU_XO_MODE_19p2 << 1;
	writel(val, wcnss->pmu_cfg);

	/* Reset IRIS */
	val |= WCNSS_PMU_IRIS_RESET;
	writel(val, wcnss->pmu_cfg);

	/* Wait for PMU.iris_reg_reset_sts */
	while (readl(wcnss->pmu_cfg) & WCNSS_PMU_IRIS_RESET_STS)
		cpu_relax();

	/* Clear IRIS reset */
	val &= ~WCNSS_PMU_IRIS_RESET;
	writel(val, wcnss->pmu_cfg);

	/* Start IRIS XO configuration */
	val |= WCNSS_PMU_IRIS_XO_CFG;
	writel(val, wcnss->pmu_cfg);

	/* Wait for XO configuration to finish */
	while (readl(wcnss->pmu_cfg) & WCNSS_PMU_IRIS_XO_CFG_STS)
		cpu_relax();

	/* Stop IRIS XO configuration */
	val &= ~WCNSS_PMU_GC_BUS_MUX_SEL_TOP;
	val &= ~WCNSS_PMU_IRIS_XO_CFG;
	writel(val, wcnss->pmu_cfg);

	/* Add some delay for XO to settle */
	msleep(20);
}

static int wcnss_start(struct rproc *rproc)
{
	struct qcom_wcnss *wcnss = (struct qcom_wcnss *)rproc->priv;
	int ret, i;

	mutex_lock(&wcnss->iris_lock);
	if (!wcnss->iris) {
		dev_err(wcnss->dev, "no iris registered\n");
		ret = -EINVAL;
		goto release_iris_lock;
	}

	for (i = 0; i < wcnss->num_pds; i++) {
		dev_pm_genpd_set_performance_state(wcnss->pds[i], INT_MAX);
		ret = pm_runtime_get_sync(wcnss->pds[i]);
		if (ret < 0) {
			pm_runtime_put_noidle(wcnss->pds[i]);
			goto disable_pds;
		}
	}

	ret = regulator_bulk_enable(wcnss->num_vregs, wcnss->vregs);
	if (ret)
		goto disable_pds;

	ret = qcom_iris_enable(wcnss->iris);
	if (ret)
		goto disable_regulators;

	wcnss_indicate_nv_download(wcnss);
	wcnss_configure_iris(wcnss);

	ret = qcom_scm_pas_auth_and_reset(WCNSS_PAS_ID);
	if (ret) {
		dev_err(wcnss->dev,
			"failed to authenticate image and release reset\n");
		goto disable_iris;
	}

	ret = wait_for_completion_timeout(&wcnss->start_done,
					  msecs_to_jiffies(5000));
	if (wcnss->ready_irq > 0 && ret == 0) {
		/* We have a ready_irq, but it didn't fire in time. */
		dev_err(wcnss->dev, "start timed out\n");
		qcom_scm_pas_shutdown(WCNSS_PAS_ID);
		ret = -ETIMEDOUT;
		goto disable_iris;
	}

	ret = 0;

disable_iris:
	qcom_iris_disable(wcnss->iris);
disable_regulators:
	regulator_bulk_disable(wcnss->num_vregs, wcnss->vregs);
disable_pds:
	for (i--; i >= 0; i--) {
		pm_runtime_put(wcnss->pds[i]);
		dev_pm_genpd_set_performance_state(wcnss->pds[i], 0);
	}
release_iris_lock:
	mutex_unlock(&wcnss->iris_lock);

	return ret;
}

static int wcnss_stop(struct rproc *rproc)
{
	struct qcom_wcnss *wcnss = (struct qcom_wcnss *)rproc->priv;
	int ret;

	if (wcnss->state) {
		qcom_smem_state_update_bits(wcnss->state,
					    BIT(wcnss->stop_bit),
					    BIT(wcnss->stop_bit));

		ret = wait_for_completion_timeout(&wcnss->stop_done,
						  msecs_to_jiffies(5000));
		if (ret == 0)
			dev_err(wcnss->dev, "timed out on wait\n");

		qcom_smem_state_update_bits(wcnss->state,
					    BIT(wcnss->stop_bit),
					    0);
	}

	ret = qcom_scm_pas_shutdown(WCNSS_PAS_ID);
	if (ret)
		dev_err(wcnss->dev, "failed to shutdown: %d\n", ret);

	return ret;
}

static void *wcnss_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct qcom_wcnss *wcnss = (struct qcom_wcnss *)rproc->priv;
	int offset;

	offset = da - wcnss->mem_reloc;
	if (offset < 0 || offset + len > wcnss->mem_size)
		return NULL;

	return wcnss->mem_region + offset;
}

static const struct rproc_ops wcnss_ops = {
	.start = wcnss_start,
	.stop = wcnss_stop,
	.da_to_va = wcnss_da_to_va,
	.parse_fw = qcom_register_dump_segments,
	.load = wcnss_load,
};

static irqreturn_t wcnss_wdog_interrupt(int irq, void *dev)
{
	struct qcom_wcnss *wcnss = dev;

	rproc_report_crash(wcnss->rproc, RPROC_WATCHDOG);

	return IRQ_HANDLED;
}

static irqreturn_t wcnss_fatal_interrupt(int irq, void *dev)
{
	struct qcom_wcnss *wcnss = dev;
	size_t len;
	char *msg;

	msg = qcom_smem_get(QCOM_SMEM_HOST_ANY, WCNSS_CRASH_REASON_SMEM, &len);
	if (!IS_ERR(msg) && len > 0 && msg[0])
		dev_err(wcnss->dev, "fatal error received: %s\n", msg);

	rproc_report_crash(wcnss->rproc, RPROC_FATAL_ERROR);

	return IRQ_HANDLED;
}

static irqreturn_t wcnss_ready_interrupt(int irq, void *dev)
{
	struct qcom_wcnss *wcnss = dev;

	complete(&wcnss->start_done);

	return IRQ_HANDLED;
}

static irqreturn_t wcnss_handover_interrupt(int irq, void *dev)
{
	/*
	 * XXX: At this point we're supposed to release the resources that we
	 * have been holding on behalf of the WCNSS. Unfortunately this
	 * interrupt comes way before the other side seems to be done.
	 *
	 * So we're currently relying on the ready interrupt firing later then
	 * this and we just disable the resources at the end of wcnss_start().
	 */

	return IRQ_HANDLED;
}

static irqreturn_t wcnss_stop_ack_interrupt(int irq, void *dev)
{
	struct qcom_wcnss *wcnss = dev;

	complete(&wcnss->stop_done);

	return IRQ_HANDLED;
}

static int wcnss_init_pds(struct qcom_wcnss *wcnss,
			  const char * const pd_names[WCNSS_MAX_PDS])
{
	int i, ret;

	for (i = 0; i < WCNSS_MAX_PDS; i++) {
		if (!pd_names[i])
			break;

		wcnss->pds[i] = dev_pm_domain_attach_by_name(wcnss->dev, pd_names[i]);
		if (IS_ERR_OR_NULL(wcnss->pds[i])) {
			ret = PTR_ERR(wcnss->pds[i]) ? : -ENODATA;
			for (i--; i >= 0; i--)
				dev_pm_domain_detach(wcnss->pds[i], false);
			return ret;
		}
	}
	wcnss->num_pds = i;

	return 0;
}

static void wcnss_release_pds(struct qcom_wcnss *wcnss)
{
	int i;

	for (i = 0; i < wcnss->num_pds; i++)
		dev_pm_domain_detach(wcnss->pds[i], false);
}

static int wcnss_init_regulators(struct qcom_wcnss *wcnss,
				 const struct wcnss_vreg_info *info,
				 int num_vregs, int num_pd_vregs)
{
	struct regulator_bulk_data *bulk;
	int ret;
	int i;

	/*
	 * If attaching the power domains suceeded we can skip requesting
	 * the regulators for the power domains. For old device trees we need to
	 * reserve extra space to manage them through the regulator interface.
	 */
	if (wcnss->num_pds)
		info += num_pd_vregs;
	else
		num_vregs += num_pd_vregs;

	bulk = devm_kcalloc(wcnss->dev,
			    num_vregs, sizeof(struct regulator_bulk_data),
			    GFP_KERNEL);
	if (!bulk)
		return -ENOMEM;

	for (i = 0; i < num_vregs; i++)
		bulk[i].supply = info[i].name;

	ret = devm_regulator_bulk_get(wcnss->dev, num_vregs, bulk);
	if (ret)
		return ret;

	for (i = 0; i < num_vregs; i++) {
		if (info[i].max_voltage)
			regulator_set_voltage(bulk[i].consumer,
					      info[i].min_voltage,
					      info[i].max_voltage);

		if (info[i].load_uA)
			regulator_set_load(bulk[i].consumer, info[i].load_uA);
	}

	wcnss->vregs = bulk;
	wcnss->num_vregs = num_vregs;

	return 0;
}

static int wcnss_request_irq(struct qcom_wcnss *wcnss,
			     struct platform_device *pdev,
			     const char *name,
			     bool optional,
			     irq_handler_t thread_fn)
{
	int ret;
	int irq_number;

	ret = platform_get_irq_byname(pdev, name);
	if (ret < 0 && optional) {
		dev_dbg(&pdev->dev, "no %s IRQ defined, ignoring\n", name);
		return 0;
	} else if (ret < 0) {
		dev_err(&pdev->dev, "no %s IRQ defined\n", name);
		return ret;
	}

	irq_number = ret;

	ret = devm_request_threaded_irq(&pdev->dev, ret,
					NULL, thread_fn,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"wcnss", wcnss);
	if (ret) {
		dev_err(&pdev->dev, "request %s IRQ failed\n", name);
		return ret;
	}

	/* Return the IRQ number if the IRQ was successfully acquired */
	return irq_number;
}

static int wcnss_alloc_memory_region(struct qcom_wcnss *wcnss)
{
	struct device_node *node;
	struct resource r;
	int ret;

	node = of_parse_phandle(wcnss->dev->of_node, "memory-region", 0);
	if (!node) {
		dev_err(wcnss->dev, "no memory-region specified\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &r);
	of_node_put(node);
	if (ret)
		return ret;

	wcnss->mem_phys = wcnss->mem_reloc = r.start;
	wcnss->mem_size = resource_size(&r);
	wcnss->mem_region = devm_ioremap_wc(wcnss->dev, wcnss->mem_phys, wcnss->mem_size);
	if (!wcnss->mem_region) {
		dev_err(wcnss->dev, "unable to map memory region: %pa+%zx\n",
			&r.start, wcnss->mem_size);
		return -EBUSY;
	}

	return 0;
}

static int wcnss_probe(struct platform_device *pdev)
{
	const char *fw_name = WCNSS_FIRMWARE_NAME;
	const struct wcnss_data *data;
	struct qcom_wcnss *wcnss;
	struct resource *res;
	struct rproc *rproc;
	void __iomem *mmio;
	int ret;

	data = of_device_get_match_data(&pdev->dev);

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;

	if (!qcom_scm_pas_supported(WCNSS_PAS_ID)) {
		dev_err(&pdev->dev, "PAS is not available for WCNSS\n");
		return -ENXIO;
	}

	ret = of_property_read_string(pdev->dev.of_node, "firmware-name",
				      &fw_name);
	if (ret < 0 && ret != -EINVAL)
		return ret;

	rproc = rproc_alloc(&pdev->dev, pdev->name, &wcnss_ops,
			    fw_name, sizeof(*wcnss));
	if (!rproc) {
		dev_err(&pdev->dev, "unable to allocate remoteproc\n");
		return -ENOMEM;
	}
	rproc_coredump_set_elf_info(rproc, ELFCLASS32, EM_NONE);

	wcnss = (struct qcom_wcnss *)rproc->priv;
	wcnss->dev = &pdev->dev;
	wcnss->rproc = rproc;
	platform_set_drvdata(pdev, wcnss);

	init_completion(&wcnss->start_done);
	init_completion(&wcnss->stop_done);

	mutex_init(&wcnss->iris_lock);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pmu");
	mmio = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mmio)) {
		ret = PTR_ERR(mmio);
		goto free_rproc;
	}

	ret = wcnss_alloc_memory_region(wcnss);
	if (ret)
		goto free_rproc;

	wcnss->pmu_cfg = mmio + data->pmu_offset;
	wcnss->spare_out = mmio + data->spare_offset;

	/*
	 * We might need to fallback to regulators instead of power domains
	 * for old device trees. Don't report an error in that case.
	 */
	ret = wcnss_init_pds(wcnss, data->pd_names);
	if (ret && (ret != -ENODATA || !data->num_pd_vregs))
		goto free_rproc;

	ret = wcnss_init_regulators(wcnss, data->vregs, data->num_vregs,
				    data->num_pd_vregs);
	if (ret)
		goto detach_pds;

	ret = wcnss_request_irq(wcnss, pdev, "wdog", false, wcnss_wdog_interrupt);
	if (ret < 0)
		goto detach_pds;
	wcnss->wdog_irq = ret;

	ret = wcnss_request_irq(wcnss, pdev, "fatal", false, wcnss_fatal_interrupt);
	if (ret < 0)
		goto detach_pds;
	wcnss->fatal_irq = ret;

	ret = wcnss_request_irq(wcnss, pdev, "ready", true, wcnss_ready_interrupt);
	if (ret < 0)
		goto detach_pds;
	wcnss->ready_irq = ret;

	ret = wcnss_request_irq(wcnss, pdev, "handover", true, wcnss_handover_interrupt);
	if (ret < 0)
		goto detach_pds;
	wcnss->handover_irq = ret;

	ret = wcnss_request_irq(wcnss, pdev, "stop-ack", true, wcnss_stop_ack_interrupt);
	if (ret < 0)
		goto detach_pds;
	wcnss->stop_ack_irq = ret;

	if (wcnss->stop_ack_irq) {
		wcnss->state = devm_qcom_smem_state_get(&pdev->dev, "stop",
							&wcnss->stop_bit);
		if (IS_ERR(wcnss->state)) {
			ret = PTR_ERR(wcnss->state);
			goto detach_pds;
		}
	}

	qcom_add_smd_subdev(rproc, &wcnss->smd_subdev);
	wcnss->sysmon = qcom_add_sysmon_subdev(rproc, "wcnss", WCNSS_SSCTL_ID);
	if (IS_ERR(wcnss->sysmon)) {
		ret = PTR_ERR(wcnss->sysmon);
		goto detach_pds;
	}

	wcnss->iris = qcom_iris_probe(&pdev->dev, &wcnss->use_48mhz_xo);
	if (IS_ERR(wcnss->iris)) {
		ret = PTR_ERR(wcnss->iris);
		goto detach_pds;
	}

	ret = rproc_add(rproc);
	if (ret)
		goto remove_iris;

	return 0;

remove_iris:
	qcom_iris_remove(wcnss->iris);
detach_pds:
	wcnss_release_pds(wcnss);
free_rproc:
	rproc_free(rproc);

	return ret;
}

static int wcnss_remove(struct platform_device *pdev)
{
	struct qcom_wcnss *wcnss = platform_get_drvdata(pdev);

	qcom_iris_remove(wcnss->iris);

	rproc_del(wcnss->rproc);

	qcom_remove_sysmon_subdev(wcnss->sysmon);
	qcom_remove_smd_subdev(wcnss->rproc, &wcnss->smd_subdev);
	wcnss_release_pds(wcnss);
	rproc_free(wcnss->rproc);

	return 0;
}

static const struct of_device_id wcnss_of_match[] = {
	{ .compatible = "qcom,riva-pil", &riva_data },
	{ .compatible = "qcom,pronto-v1-pil", &pronto_v1_data },
	{ .compatible = "qcom,pronto-v2-pil", &pronto_v2_data },
	{ .compatible = "qcom,pronto-v3-pil", &pronto_v3_data },
	{ },
};
MODULE_DEVICE_TABLE(of, wcnss_of_match);

static struct platform_driver wcnss_driver = {
	.probe = wcnss_probe,
	.remove = wcnss_remove,
	.driver = {
		.name = "qcom-wcnss-pil",
		.of_match_table = wcnss_of_match,
	},
};

module_platform_driver(wcnss_driver);

MODULE_DESCRIPTION("Qualcomm Peripheral Image Loader for Wireless Subsystem");
MODULE_LICENSE("GPL v2");
