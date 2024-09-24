/*
 *
 * (C) COPYRIGHT 2020-2023 Arm Limited.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include "ethosn_core.h"

#include "ethosn_device.h"
#include "ethosn_network.h"
#include "ethosn_smc.h"
#include "scylla_addr_fields_public.h"
#include "scylla_regs_public.h"

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/iommu.h>
#include <linux/pm_runtime.h>

static ssize_t architecture_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct ethosn_core *core = dev_get_drvdata(dev);
	struct dl1_npu_id_r scylla_id;

	scylla_id.word = ethosn_read_top_reg(core, DL1_RP, DL1_NPU_ID);

	return scnprintf(buf, PAGE_SIZE, "%u.%u.%u\n",
			 scylla_id.bits.arch_major, scylla_id.bits.arch_minor,
			 scylla_id.bits.arch_rev);
}

static ssize_t product_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct ethosn_core *core = dev_get_drvdata(dev);
	struct dl1_npu_id_r scylla_id;

	scylla_id.word = ethosn_read_top_reg(core, DL1_RP, DL1_NPU_ID);

	return scnprintf(buf, PAGE_SIZE, "%u\n", scylla_id.bits.product_major);
}

static ssize_t version_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct ethosn_core *core = dev_get_drvdata(dev);
	struct dl1_npu_id_r scylla_id;

	scylla_id.word = ethosn_read_top_reg(core, DL1_RP, DL1_NPU_ID);

	return scnprintf(buf, PAGE_SIZE, "%u.%u.%u\n",
			 scylla_id.bits.version_major,
			 scylla_id.bits.version_minor,
			 scylla_id.bits.version_status);
}

static ssize_t unit_count_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct ethosn_core *core = dev_get_drvdata(dev);
	struct dl1_unit_count_r unit_count;

	unit_count.word = ethosn_read_top_reg(core, DL1_RP, DL1_UNIT_COUNT);

	return scnprintf(buf, PAGE_SIZE,
			 "quad_count=%u\n"
			 "engines_per_quad=%u\n"
			 "dfc_emc_per_engine=%u\n",
			 unit_count.bits.quad_count,
			 unit_count.bits.engines_per_quad,
			 unit_count.bits.dfc_emc_per_engine);
}

static ssize_t mce_features_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct ethosn_core *core = dev_get_drvdata(dev);
	struct dl1_mce_features_r mce;

	mce.word = ethosn_read_top_reg(core, DL1_RP, DL1_MCE_FEATURES);

	return scnprintf(buf, PAGE_SIZE,
			 "ifm_generated_per_engine=%u\n"
			 "ofm_generated_per_engine=%u\n"
			 "mce_num_macs=%u\n"
			 "mce_num_acc=%u\n"
			 "winograd_support=%u\n"
			 "tsu_16bit_sequence_support=%u\n"
			 "ofm_scaling_16bit_support=%u\n",
			 mce.bits.ifm_generated_per_engine,
			 mce.bits.ofm_generated_per_engine,
			 mce.bits.mce_num_macs,
			 mce.bits.mce_num_acc,
			 mce.bits.winograd_support,
			 mce.bits.tsu_16bit_sequence_support,
			 mce.bits.ofm_scaling_16bit_support);
}

static ssize_t dfc_features_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct ethosn_core *core = dev_get_drvdata(dev);
	struct dl1_dfc_features_r dfc;

	dfc.word = ethosn_read_top_reg(core, DL1_RP, DL1_DFC_FEATURES);

	return scnprintf(buf, PAGE_SIZE,
			 "dfc_mem_size_per_emc=%u\n"
			 "bank_count=%u\n",
			 dfc.bits.dfc_mem_size_per_emc << 12,
			 dfc.bits.bank_count);
}

static ssize_t ple_features_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct ethosn_core *core = dev_get_drvdata(dev);
	struct dl1_ple_features_r ple;

	ple.word = ethosn_read_top_reg(core, DL1_RP, DL1_PLE_FEATURES);

	return scnprintf(buf, PAGE_SIZE,
			 "ple_input_mem_size=%u\n"
			 "ple_output_mem_size=%u\n"
			 "ple_vrf_mem_size=%u\n"
			 "ple_mem_size=%u\n",
			 ple.bits.ple_input_mem_size << 8,
			 ple.bits.ple_output_mem_size << 8,
			 ple.bits.ple_vrf_mem_size << 4,
			 ple.bits.ple_mem_size << 8);
}

static ssize_t ecoid_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct ethosn_core *core = dev_get_drvdata(dev);
	struct dl1_ecoid_r ecoid;

	ecoid.word = ethosn_read_top_reg(core, DL1_RP, DL1_ECOID);

	return scnprintf(buf, PAGE_SIZE, "%x\n", ecoid.bits.ecoid);
}

static ssize_t firmware_reset_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t count)
{
	struct ethosn_core *core = dev_get_drvdata(dev);
	int ret;

	ret = ethosn_reset_and_start_ethosn(core, core->set_alloc_id);
	if (ret != 0)
		return ret;

	return count;
}

static ssize_t variant_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct ethosn_core *core = dev_get_drvdata(dev);

	struct dl1_unit_count_r unit_count;
	struct dl1_mce_features_r mce_features;
	struct dl1_vector_engine_features_r ve_features;
	struct dl1_dfc_features_r dfc_features;

	uint32_t engines, igs, ogs, tops, ple_ratio, sram;

	mce_features.word = ethosn_read_top_reg(core, DL1_RP, DL1_MCE_FEATURES);
	unit_count.word = ethosn_read_top_reg(core, DL1_RP, DL1_UNIT_COUNT);
	ve_features.word = ethosn_read_top_reg(core, DL1_RP,
					       DL1_VECTOR_ENGINE_FEATURES);
	dfc_features.word = ethosn_read_top_reg(core, DL1_RP, DL1_DFC_FEATURES);

	engines = unit_count.bits.quad_count * unit_count.bits.engines_per_quad;
	igs = engines * mce_features.bits.ifm_generated_per_engine;
	ogs = engines * mce_features.bits.ofm_generated_per_engine;
	/* Calculate TOPS, assuming the standard frequency of 1GHz. */
	tops = (mce_features.bits.mce_num_macs * igs * ogs * 2) / 1024;
	ple_ratio = ((ve_features.bits.ple_lanes + 1) * engines) / tops;
	sram = (dfc_features.bits.dfc_mem_size_per_emc << 12) *
	       unit_count.bits.dfc_emc_per_engine * engines;

	return scnprintf(buf, PAGE_SIZE,
			 "%uTOPS_%uPLE_RATIO_%uKB\n",
			 tops, ple_ratio, sram / 1024);
}

static const DEVICE_ATTR_RO(architecture);
static const DEVICE_ATTR_RO(product);
static const DEVICE_ATTR_RO(version);
static const DEVICE_ATTR_RO(unit_count);
static const DEVICE_ATTR_RO(mce_features);
static const DEVICE_ATTR_RO(dfc_features);
static const DEVICE_ATTR_RO(ple_features);
static const DEVICE_ATTR_RO(ecoid);
static const DEVICE_ATTR_WO(firmware_reset);
static const DEVICE_ATTR_RO(variant);

static const struct attribute *attrs[] = {
	&dev_attr_architecture.attr,
	&dev_attr_product.attr,
	&dev_attr_version.attr,
	&dev_attr_unit_count.attr,
	&dev_attr_mce_features.attr,
	&dev_attr_dfc_features.attr,
	&dev_attr_ple_features.attr,
	&dev_attr_ecoid.attr,
	&dev_attr_firmware_reset.attr,
	&dev_attr_variant.attr,
	NULL
};

#ifdef CONFIG_PM
static bool ethosn_is_sleeping(struct ethosn_core *core)
{
#ifdef ETHOSN_NS
	struct dl1_sysctlr0_r sysctlr0 = { .word = 0 };

	sysctlr0.word =
		ethosn_read_top_reg(core, DL1_RP, DL1_SYSCTLR0);

	return sysctlr0.bits.sleeping;
#else
	int ret = ethosn_smc_core_is_sleeping(core->dev, core->phys_addr);

	if (ret < 0)
		dev_err(core->dev,
			"Failed to get core state, assuming it's active: %d\n",
			ret);

	return ret == 1;
#endif
}

static int ethosn_pm_common_resume(struct device *dev)
{
	int ret;
	struct ethosn_core *core = dev_get_drvdata(dev);

	if (!core) {
		dev_dbg(dev, "Driver data not found\n");
		ret = -EFAULT;
		goto exit_pm_resume;
	}

	dev_dbg(dev, "%s: Restarting core\n", __func__);
	ret = ethosn_reset_and_start_ethosn(core, core->set_alloc_id);
	if (ret)
		goto exit_pm_resume;

	ret = mutex_lock_interruptible(&core->mutex);
	if (ret)
		goto exit_pm_resume;

	ethosn_schedule_queued_inference(core);

	if (!core->current_inference)
		pm_runtime_mark_last_busy(core->dev);

	mutex_unlock(&core->mutex);

exit_pm_resume:
	if (!ret && core->profiling.config.enable_profiling)
		++core->profiling.pm_resume_count;

	return ret;
}

static int ethosn_pm_resume(struct device *dev)
{
	int ret = ethosn_pm_common_resume(dev);

	dev_dbg(dev, "Core pm resume: %d\n", ret);

	return ret;
}

static int ethosn_rpm_resume(struct device *dev)
{
	int ret;
	struct ethosn_core *core = dev_get_drvdata(dev);

	if (!core) {
		dev_dbg(dev, "Driver data not found\n");
		ret = -EFAULT;
		goto exit_rpm_resume;
	}

	dev_dbg(dev, "%s: Restarting core\n", __func__);
	ret = ethosn_reset_and_start_ethosn(core, core->set_alloc_id);

exit_rpm_resume:
	if (!ret && core->profiling.config.enable_profiling)
		++core->profiling.rpm_resume_count;

	dev_dbg(dev, "Core rpm resume: %d\n", ret);

	return ret;
}

static int ethosn_pm_common_suspend(struct device *dev)
{
	int ret = 0;
	struct ethosn_core *core = dev_get_drvdata(dev);
	struct ethosn_device *ethosn;

	if (!core) {
		dev_dbg(dev, "Driver data not found\n");
		ret = -EFAULT;
		goto exit_pm_suspend;
	}

	/* We're querying/modifying the state of the core, so get exclusive
	 * access
	 */
	mutex_lock(&core->mutex);

	/* If there is an inference currently running on the core, we need to
	 * cancel it so that the core can be put to sleep. We re-queue the
	 * inference so that it will be executed again once the core comes
	 * out of sleep.
	 */
	if (core->current_inference) {
		WARN_ON(
			core->current_inference->status !=
			ETHOSN_INFERENCE_RUNNING);
		core->current_inference->status = ETHOSN_INFERENCE_SCHEDULED;
		pm_runtime_mark_last_busy(core->dev);
		pm_runtime_put(core->dev);
		ethosn = core->parent;

		ret = mutex_lock_interruptible(
			&ethosn->queue.inference_queue_mutex);

		if (ret) {
			ret = -EFAULT;
			goto exit_pm_suspend;
		}

		/* Queue the inference again */
		list_add(&core->current_inference->queue_node,
			 &ethosn->queue.inference_queue);

		mutex_unlock(&ethosn->queue.inference_queue_mutex);

		core->current_inference = NULL;
	}

	ret = ethosn_reset(core, true, core->set_alloc_id);
exit_pm_suspend:
	if (!ret && core->profiling.config.enable_profiling)
		++core->profiling.pm_suspend_count;

	if (core)
		mutex_unlock(&core->mutex);

	return ret;
}

static int ethosn_pm_suspend_noirq(struct device *dev)
{
	int ret = ethosn_pm_common_suspend(dev);

	dev_dbg(dev, "Core pm suspend: %d\n", ret);

	return ret;
}

static int ethosn_rpm_suspend(struct device *dev)
{
	int ret = 0;
	struct ethosn_core *core = dev_get_drvdata(dev);

	if (!core) {
		dev_dbg(dev, "Driver data not found\n");
		ret = -EFAULT;
		goto exit_rpm_suspend;
	}

	if (!ethosn_is_sleeping(core))
		ret = -EBUSY;

exit_rpm_suspend:
	if (!ret && core->profiling.config.enable_profiling)
		++core->profiling.rpm_suspend_count;

	dev_dbg(dev, "Core rpm suspend: %d\n", ret);

	return ret;
}

static int ethosn_pm_freeze_noirq(struct device *dev)
{
	int ret = ethosn_pm_common_suspend(dev);

	dev_dbg(dev, "Core pm freeze: %d\n", ret);

	return ret;
}

static int ethosn_pm_restore(struct device *dev)
{
	int ret = ethosn_pm_common_resume(dev);

	dev_dbg(dev, "Core pm restore: %d\n", ret);

	return ret;
}

static const struct dev_pm_ops ethosn_pm_ops = {
	.resume        = ethosn_pm_resume,
	.suspend_noirq = ethosn_pm_suspend_noirq,
	.restore       = ethosn_pm_restore,
	.freeze_noirq  = ethosn_pm_freeze_noirq,
	SET_RUNTIME_PM_OPS(ethosn_rpm_suspend, ethosn_rpm_resume, NULL)
};
#define ETHOSN_PM_OPS (&ethosn_pm_ops)
#else
#define ETHOSN_PM_OPS (NULL)
#endif  /* CONFIG_PM */

struct ethosn_device *ethosn_driver(struct platform_device *pdev)
{
	struct ethosn_device *ethosn = dev_get_drvdata(pdev->dev.parent);

	return ethosn;
}

static int ethosn_child_pdev_remove(struct platform_device *pdev)
{
	struct ethosn_core *core = dev_get_drvdata(&pdev->dev);
	struct ethosn_device *ethosn = ethosn_driver(pdev);

	BUG_ON(ethosn->num_cores <= 0);

	/* We need to disable the runtime pm and
	 * hence wake up the core before tear down
	 */
	pm_runtime_disable(core->dev);

	ethosn_device_deinit(core);

	dev_info(&pdev->dev, "Depopulate ethosn core child devices\n");

	/* Main allocator handled as a child node in SMMU case */
	if (ethosn->smmu_available)
		of_platform_depopulate(&pdev->dev);
	else
		ethosn_destroy_carveout_main_allocator(core);

	sysfs_remove_files(&core->dev->kobj, attrs);
	dev_set_drvdata(&pdev->dev, NULL);

	dev_dbg(&pdev->dev, "Removed core %u from parent %u\n",
		core->core_id, core->parent->parent_id);

	return 0;
}

static int ethosn_child_pdev_probe(struct platform_device *pdev)
{
	struct ethosn_device *ethosn = ethosn_driver(pdev);
	int core_id;
	int ret = 0;

	dev_info(&pdev->dev, "Probing core\n");

	if (IS_ERR_OR_NULL(ethosn)) {
		dev_err(&pdev->dev, "Invalid parent device driver");

		return -EINVAL;
	}

	core_id = ethosn->num_cores;

	/* Allocating the core device (ie struct ethosn_core)
	 * Allocated against parent device
	 */
	ethosn->core[core_id] = devm_kzalloc(
		pdev->dev.parent,
		sizeof(struct ethosn_core),
		GFP_KERNEL);

	if (!ethosn->core[core_id])
		return -ENOMEM;

	/* Link child device object */
	ethosn->core[core_id]->dev = &pdev->dev;
	ethosn->core[core_id]->core_id = core_id;
	ethosn->core[core_id]->parent = ethosn;

	dev_set_drvdata(&pdev->dev, ethosn->core[core_id]);

	ret = sysfs_create_files(&ethosn->core[core_id]->dev->kobj, attrs);
	if (ret) {
		ret = -ENOMEM;
		goto err_free_core;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev,
					 ETHOSN_AUTOSUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	dev_dbg(&pdev->dev, "Init reserved mem\n");

	if (ethosn_init_reserved_mem(&pdev->dev))
		dev_dbg(&pdev->dev,
			"Reserved mem not present or init failed\n");

	/* Main allocator handled as a child node in SMMU case */
	if (ethosn->smmu_available) {
		ret = of_platform_default_populate(pdev->dev.of_node, NULL,
						   &pdev->dev);
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to populate child devices\n");

			ret = -EINVAL;
			goto err_free_core;
		}
	}

	++ethosn->num_cores;

	return ret;

err_free_core:
	devm_kfree(&pdev->dev, ethosn->core[core_id]);

	return ret;
}

static const struct of_device_id ethosn_child_pdev_match[] = {
	{ .compatible = ETHOSN_CORE_DRIVER_NAME },
	{ /* Sentinel */ },
};

MODULE_DEVICE_TABLE(of, ethosn_child_pdev_match);

static struct platform_driver ethosn_child_pdev_driver = {
	.probe                  = &ethosn_child_pdev_probe,
	.remove                 = &ethosn_child_pdev_remove,
	.driver                 = {
		.name           = ETHOSN_CORE_DRIVER_NAME,
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(ethosn_child_pdev_match),
		.pm             = ETHOSN_PM_OPS,
	},
};

int ethosn_core_platform_driver_register(void)
{
	pr_info("Registering %s", ETHOSN_CORE_DRIVER_NAME);

	return platform_driver_register(&ethosn_child_pdev_driver);
}

void ethosn_core_platform_driver_unregister(void)
{
	pr_info("Unregistering %s", ETHOSN_CORE_DRIVER_NAME);
	platform_driver_unregister(&ethosn_child_pdev_driver);
}
