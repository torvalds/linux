// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014-2022 ARM Limited. All rights reserved.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <mali_kbase.h>
#include <tl/mali_kbase_tracepoints.h>
#include <backend/gpu/mali_kbase_devfreq.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

#include <linux/of.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/devfreq.h>
#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
#include <linux/devfreq_cooling.h>
#endif

#include <linux/version.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include "mali_kbase_devfreq.h"

#include <soc/rockchip/rockchip_ipa.h>
#include <soc/rockchip/rockchip_opp_select.h>
#include <soc/rockchip/rockchip_system_monitor.h>

static struct devfreq_simple_ondemand_data ondemand_data;

static struct monitor_dev_profile mali_mdevp = {
	.type = MONITOR_TPYE_DEV,
	.low_temp_adjust = rockchip_monitor_dev_low_temp_adjust,
	.high_temp_adjust = rockchip_monitor_dev_high_temp_adjust,
	.update_volt = rockchip_monitor_check_rate_volt,
};

/**
 * get_voltage() - Get the voltage value corresponding to the nominal frequency
 *                 used by devfreq.
 * @kbdev:    Device pointer
 * @freq:     Nominal frequency in Hz passed by devfreq.
 *
 * This function will be called only when the opp table which is compatible with
 * "operating-points-v2-mali", is not present in the devicetree for GPU device.
 *
 * Return: Voltage value in micro volts, 0 in case of error.
 */
static unsigned long get_voltage(struct kbase_device *kbdev, unsigned long freq)
{
	struct dev_pm_opp *opp;
	unsigned long voltage = 0;

#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	rcu_read_lock();
#endif

	opp = dev_pm_opp_find_freq_exact(kbdev->dev, freq, true);

	if (IS_ERR_OR_NULL(opp))
		dev_err(kbdev->dev, "Failed to get opp (%d)\n", PTR_ERR_OR_ZERO(opp));
	else {
		voltage = dev_pm_opp_get_voltage(opp);
#if KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE
		dev_pm_opp_put(opp);
#endif
	}

#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	rcu_read_unlock();
#endif

	/* Return the voltage in micro volts */
	return voltage;
}

void kbase_devfreq_opp_translate(struct kbase_device *kbdev, unsigned long freq,
	u64 *core_mask, unsigned long *freqs, unsigned long *volts)
{
	unsigned int i;

	for (i = 0; i < kbdev->num_opps; i++) {
		if (kbdev->devfreq_table[i].opp_freq == freq) {
			unsigned int j;

			*core_mask = kbdev->devfreq_table[i].core_mask;
			for (j = 0; j < kbdev->nr_clocks; j++) {
				freqs[j] =
					kbdev->devfreq_table[i].real_freqs[j];
				volts[j] =
					kbdev->devfreq_table[i].opp_volts[j];
			}

			break;
		}
	}

	/* If failed to find OPP, return all cores enabled
	 * and nominal frequency and the corresponding voltage.
	 */
	if (i == kbdev->num_opps) {
		unsigned long voltage = get_voltage(kbdev, freq);

		*core_mask = kbdev->gpu_props.props.raw_props.shader_present;

		for (i = 0; i < kbdev->nr_clocks; i++) {
			freqs[i] = freq;
			volts[i] = voltage;
		}
	}
}

int kbase_devfreq_opp_helper(struct dev_pm_set_opp_data *data)
{
	struct device *dev = data->dev;
	struct dev_pm_opp_supply *old_supply_vdd = &data->old_opp.supplies[0];
	struct dev_pm_opp_supply *new_supply_vdd = &data->new_opp.supplies[0];
	struct regulator *vdd_reg = data->regulators[0];
	struct dev_pm_opp_supply *old_supply_mem;
	struct dev_pm_opp_supply *new_supply_mem;
	struct regulator *mem_reg;
	struct clk *clk = data->clk;
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct rockchip_opp_info *opp_info = &kbdev->opp_info;
	unsigned long old_freq = data->old_opp.rate;
	unsigned long new_freq = data->new_opp.rate;
	unsigned int reg_count = data->regulator_count;
	bool is_set_rm = true;
	bool is_set_clk = true;
	u32 target_rm = UINT_MAX;
	int ret = 0;

	if (reg_count > 1) {
		old_supply_mem = &data->old_opp.supplies[1];
		new_supply_mem = &data->new_opp.supplies[1];
		mem_reg = data->regulators[1];
	}

	if (!pm_runtime_active(dev)) {
		is_set_rm = false;
		if (opp_info->scmi_clk)
			is_set_clk = false;
	}

	ret = clk_bulk_prepare_enable(opp_info->num_clks,  opp_info->clks);
	if (ret) {
		dev_err(dev, "failed to enable opp clks\n");
		return ret;
	}
	rockchip_get_read_margin(dev, opp_info, new_supply_vdd->u_volt,
				 &target_rm);

	/* Change frequency */
	dev_dbg(dev, "switching OPP: %lu Hz --> %lu Hz\n", old_freq, new_freq);
	/* Scaling up? Scale voltage before frequency */
	if (new_freq >= old_freq) {
		rockchip_set_intermediate_rate(dev, opp_info, clk, old_freq,
					       new_freq, true, is_set_clk);
		if (reg_count > 1) {
			ret = regulator_set_voltage(mem_reg,
						    new_supply_mem->u_volt,
						    INT_MAX);
			if (ret) {
				dev_err(dev, "failed to set volt %lu uV for mem reg\n",
					new_supply_mem->u_volt);
				goto restore_voltage;
			}
		}
		ret = regulator_set_voltage(vdd_reg, new_supply_vdd->u_volt,
					    INT_MAX);
		if (ret) {
			dev_err(dev, "failed to set volt %lu uV for vdd reg\n",
				new_supply_vdd->u_volt);
			goto restore_voltage;
		}
		rockchip_set_read_margin(dev, opp_info, target_rm, is_set_rm);
		if (is_set_clk && clk_set_rate(clk, new_freq)) {
			ret = -EINVAL;
			dev_err(dev, "failed to set clk rate\n");
			goto restore_rm;
		}
	/* Scaling down? Scale voltage after frequency */
	} else {
		rockchip_set_intermediate_rate(dev, opp_info, clk, old_freq,
					       new_freq, false, is_set_clk);
		rockchip_set_read_margin(dev, opp_info, target_rm, is_set_rm);
		if (is_set_clk && clk_set_rate(clk, new_freq)) {
			ret = -EINVAL;
			dev_err(dev, "failed to set clk rate\n");
			goto restore_rm;
		}
		ret = regulator_set_voltage(vdd_reg, new_supply_vdd->u_volt,
					    INT_MAX);
		if (ret) {
			dev_err(dev, "failed to set volt %lu uV for vdd reg\n",
				new_supply_vdd->u_volt);
			goto restore_freq;
		}
		if (reg_count > 1) {
			ret = regulator_set_voltage(mem_reg,
						    new_supply_mem->u_volt,
						    INT_MAX);
			if (ret) {
				dev_err(dev, "failed to set volt %lu uV for mem reg\n",
					new_supply_mem->u_volt);
				goto restore_voltage;
			}
		}
	}

	clk_bulk_disable_unprepare(opp_info->num_clks, opp_info->clks);

	return 0;

restore_freq:
	if (is_set_clk && clk_set_rate(clk, old_freq))
		dev_err(dev, "failed to restore old-freq %lu Hz\n", old_freq);
restore_rm:
	rockchip_get_read_margin(dev, opp_info, old_supply_vdd->u_volt,
				 &target_rm);
	rockchip_set_read_margin(dev, opp_info, opp_info->target_rm, is_set_rm);
restore_voltage:
	if (reg_count > 1 && old_supply_mem->u_volt)
		regulator_set_voltage(mem_reg, old_supply_mem->u_volt, INT_MAX);
	regulator_set_voltage(vdd_reg, old_supply_vdd->u_volt, INT_MAX);
	clk_bulk_disable_unprepare(opp_info->num_clks, opp_info->clks);

	return ret;
}

static int
kbase_devfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	int ret = 0;

	if (!mali_mdevp.is_checked)
		return -EINVAL;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp))
		return PTR_ERR(opp);
	dev_pm_opp_put(opp);

	if (*freq == kbdev->current_nominal_freq)
		return 0;
	rockchip_monitor_volt_adjust_lock(kbdev->mdev_info);
	ret = dev_pm_opp_set_rate(dev, *freq);
	if (!ret) {
		kbdev->current_nominal_freq = *freq;
		KBASE_TLSTREAM_AUX_DEVFREQ_TARGET(kbdev, (u64)*freq);
	}
	rockchip_monitor_volt_adjust_unlock(kbdev->mdev_info);

	return ret;
}

void kbase_devfreq_force_freq(struct kbase_device *kbdev, unsigned long freq)
{
	unsigned long target_freq = freq;

	kbase_devfreq_target(kbdev->dev, &target_freq, 0);
}

static int
kbase_devfreq_cur_freq(struct device *dev, unsigned long *freq)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);

	*freq = kbdev->current_nominal_freq;

	return 0;
}

static int
kbase_devfreq_status(struct device *dev, struct devfreq_dev_status *stat)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct kbasep_pm_metrics diff;

	kbase_pm_get_dvfs_metrics(kbdev, &kbdev->last_devfreq_metrics, &diff);

	stat->busy_time = diff.time_busy;
	stat->total_time = diff.time_busy + diff.time_idle;
	stat->current_frequency = kbdev->current_nominal_freq;
	stat->private_data = NULL;

#if MALI_USE_CSF && defined CONFIG_DEVFREQ_THERMAL
	kbase_ipa_reset_data(kbdev);
#endif

	return 0;
}

static int kbase_devfreq_init_freq_table(struct kbase_device *kbdev,
		struct devfreq_dev_profile *dp)
{
	int count;
	int i = 0;
	unsigned long freq;
	struct dev_pm_opp *opp;

#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	rcu_read_lock();
#endif
	count = dev_pm_opp_get_opp_count(kbdev->dev);
#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	rcu_read_unlock();
#endif
	if (count < 0)
		return count;

	dp->freq_table = kmalloc_array(count, sizeof(dp->freq_table[0]),
				GFP_KERNEL);
	if (!dp->freq_table)
		return -ENOMEM;

#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	rcu_read_lock();
#endif
	for (i = 0, freq = ULONG_MAX; i < count; i++, freq--) {
		opp = dev_pm_opp_find_freq_floor(kbdev->dev, &freq);
		if (IS_ERR(opp))
			break;
#if KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE
		dev_pm_opp_put(opp);
#endif /* KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE */

		dp->freq_table[i] = freq;
	}
#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	rcu_read_unlock();
#endif

	if (count != i)
		dev_warn(kbdev->dev, "Unable to enumerate all OPPs (%d!=%d\n",
				count, i);

	dp->max_state = i;


	/* Have the lowest clock as suspend clock.
	 * It may be overridden by 'opp-mali-errata-1485982'.
	 */
	if (kbdev->pm.backend.gpu_clock_slow_down_wa) {
		freq = 0;
		opp = dev_pm_opp_find_freq_ceil(kbdev->dev, &freq);
		if (IS_ERR(opp)) {
			dev_err(kbdev->dev, "failed to find slowest clock");
			return 0;
		}
		dev_pm_opp_put(opp);
		dev_info(kbdev->dev, "suspend clock %lu from slowest", freq);
		kbdev->pm.backend.gpu_clock_suspend_freq = freq;
	}

	return 0;
}

static void kbase_devfreq_term_freq_table(struct kbase_device *kbdev)
{
	struct devfreq_dev_profile *dp = &kbdev->devfreq_profile;

	kfree(dp->freq_table);
	dp->freq_table = NULL;
}

static void kbase_devfreq_term_core_mask_table(struct kbase_device *kbdev)
{
	kfree(kbdev->devfreq_table);
	kbdev->devfreq_table = NULL;
}

static void kbase_devfreq_exit(struct device *dev)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);

	if (kbdev)
		kbase_devfreq_term_freq_table(kbdev);
}

static void kbasep_devfreq_read_suspend_clock(struct kbase_device *kbdev,
		struct device_node *node)
{
	u64 freq = 0;
	int err = 0;

	/* Check if this node is the opp entry having 'opp-mali-errata-1485982'
	 * to get the suspend clock, otherwise skip it.
	 */
	if (!of_property_read_bool(node, "opp-mali-errata-1485982"))
		return;

	/* In kbase DevFreq, the clock will be read from 'opp-hz'
	 * and translated into the actual clock by opp_translate.
	 *
	 * In customer DVFS, the clock will be read from 'opp-hz-real'
	 * for clk driver. If 'opp-hz-real' does not exist,
	 * read from 'opp-hz'.
	 */
	if (IS_ENABLED(CONFIG_MALI_BIFROST_DEVFREQ))
		err = of_property_read_u64(node, "opp-hz", &freq);
	else {
		if (of_property_read_u64(node, "opp-hz-real", &freq))
			err = of_property_read_u64(node, "opp-hz", &freq);
	}

	if (WARN_ON(err || !freq))
		return;

	kbdev->pm.backend.gpu_clock_suspend_freq = freq;
	dev_info(kbdev->dev,
		"suspend clock %llu by opp-mali-errata-1485982", freq);
}

static int kbase_devfreq_init_core_mask_table(struct kbase_device *kbdev)
{
#ifndef CONFIG_OF
	/* OPP table initialization requires at least the capability to get
	 * regulators and clocks from the device tree, as well as parsing
	 * arrays of unsigned integer values.
	 *
	 * The whole initialization process shall simply be skipped if the
	 * minimum capability is not available.
	 */
	return 0;
#else
	struct device_node *opp_node = of_parse_phandle(kbdev->dev->of_node,
			"operating-points-v2", 0);
	struct device_node *node;
	int i = 0;
	int count;
	u64 shader_present = kbdev->gpu_props.props.raw_props.shader_present;

	if (!opp_node)
		return 0;
	if (!of_device_is_compatible(opp_node, "operating-points-v2-mali"))
		return 0;

	count = dev_pm_opp_get_opp_count(kbdev->dev);
	kbdev->devfreq_table = kmalloc_array(count,
			sizeof(struct kbase_devfreq_opp), GFP_KERNEL);
	if (!kbdev->devfreq_table)
		return -ENOMEM;

	for_each_available_child_of_node(opp_node, node) {
		const void *core_count_p;
		u64 core_mask, opp_freq,
			real_freqs[BASE_MAX_NR_CLOCKS_REGULATORS];
		int err;
#if IS_ENABLED(CONFIG_REGULATOR)
		u32 opp_volts[BASE_MAX_NR_CLOCKS_REGULATORS];
#endif

		/* Read suspend clock from opp table */
		if (kbdev->pm.backend.gpu_clock_slow_down_wa)
			kbasep_devfreq_read_suspend_clock(kbdev, node);

		err = of_property_read_u64(node, "opp-hz", &opp_freq);
		if (err) {
			dev_warn(kbdev->dev, "Failed to read opp-hz property with error %d\n",
					err);
			continue;
		}


#if BASE_MAX_NR_CLOCKS_REGULATORS > 1
		err = of_property_read_u64_array(node, "opp-hz-real",
				real_freqs, kbdev->nr_clocks);
#else
		WARN_ON(kbdev->nr_clocks != 1);
		err = of_property_read_u64(node, "opp-hz-real", real_freqs);
#endif
		if (err < 0) {
			dev_warn(kbdev->dev, "Failed to read opp-hz-real property with error %d\n",
					err);
			continue;
		}
#if IS_ENABLED(CONFIG_REGULATOR)
		err = of_property_read_u32_array(node,
			"opp-microvolt", opp_volts, kbdev->nr_regulators);
		if (err < 0) {
			dev_warn(kbdev->dev, "Failed to read opp-microvolt property with error %d\n",
					err);
			continue;
		}
#endif

		if (of_property_read_u64(node, "opp-core-mask", &core_mask))
			core_mask = shader_present;
		if (core_mask != shader_present && corestack_driver_control) {

			dev_warn(kbdev->dev, "Ignoring OPP %llu - Dynamic Core Scaling not supported on this GPU\n",
					opp_freq);
			continue;
		}

		core_count_p = of_get_property(node, "opp-core-count", NULL);
		if (core_count_p) {
			u64 remaining_core_mask =
				kbdev->gpu_props.props.raw_props.shader_present;
			int core_count = be32_to_cpup(core_count_p);

			core_mask = 0;

			for (; core_count > 0; core_count--) {
				int core = ffs(remaining_core_mask);

				if (!core) {
					dev_err(kbdev->dev, "OPP has more cores than GPU\n");
					return -ENODEV;
				}

				core_mask |= (1ull << (core-1));
				remaining_core_mask &= ~(1ull << (core-1));
			}
		}

		if (!core_mask) {
			dev_err(kbdev->dev, "OPP has invalid core mask of 0\n");
			return -ENODEV;
		}

		kbdev->devfreq_table[i].opp_freq = opp_freq;
		kbdev->devfreq_table[i].core_mask = core_mask;
		if (kbdev->nr_clocks > 0) {
			int j;

			for (j = 0; j < kbdev->nr_clocks; j++)
				kbdev->devfreq_table[i].real_freqs[j] =
					real_freqs[j];
		}
#if IS_ENABLED(CONFIG_REGULATOR)
		if (kbdev->nr_regulators > 0) {
			int j;

			for (j = 0; j < kbdev->nr_regulators; j++)
				kbdev->devfreq_table[i].opp_volts[j] =
						opp_volts[j];
		}
#endif

		dev_info(kbdev->dev, "OPP %d : opp_freq=%llu core_mask=%llx\n",
				i, opp_freq, core_mask);

		i++;
	}

	kbdev->num_opps = i;

	return 0;
#endif /* CONFIG_OF */
}

static const char *kbase_devfreq_req_type_name(enum kbase_devfreq_work_type type)
{
	const char *p;

	switch (type) {
	case DEVFREQ_WORK_NONE:
		p = "devfreq_none";
		break;
	case DEVFREQ_WORK_SUSPEND:
		p = "devfreq_suspend";
		break;
	case DEVFREQ_WORK_RESUME:
		p = "devfreq_resume";
		break;
	default:
		p = "Unknown devfreq_type";
	}
	return p;
}

static void kbase_devfreq_suspend_resume_worker(struct work_struct *work)
{
	struct kbase_devfreq_queue_info *info = container_of(work,
			struct kbase_devfreq_queue_info, work);
	struct kbase_device *kbdev = container_of(info, struct kbase_device,
			devfreq_queue);
	unsigned long flags;
	enum kbase_devfreq_work_type type, acted_type;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	type = kbdev->devfreq_queue.req_type;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	acted_type = kbdev->devfreq_queue.acted_type;
	dev_dbg(kbdev->dev, "Worker handles queued req: %s (acted: %s)\n",
		kbase_devfreq_req_type_name(type),
		kbase_devfreq_req_type_name(acted_type));
	switch (type) {
	case DEVFREQ_WORK_SUSPEND:
	case DEVFREQ_WORK_RESUME:
		if (type != acted_type) {
			if (type == DEVFREQ_WORK_RESUME)
				devfreq_resume_device(kbdev->devfreq);
			else
				devfreq_suspend_device(kbdev->devfreq);
			dev_dbg(kbdev->dev, "Devfreq transition occured: %s => %s\n",
				kbase_devfreq_req_type_name(acted_type),
				kbase_devfreq_req_type_name(type));
			kbdev->devfreq_queue.acted_type = type;
		}
		break;
	default:
		WARN_ON(1);
	}
}

void kbase_devfreq_enqueue_work(struct kbase_device *kbdev,
				       enum kbase_devfreq_work_type work_type)
{
	unsigned long flags;

	WARN_ON(work_type == DEVFREQ_WORK_NONE);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	/* Skip enqueuing a work if workqueue has already been terminated. */
	if (likely(kbdev->devfreq_queue.workq)) {
		kbdev->devfreq_queue.req_type = work_type;
		queue_work(kbdev->devfreq_queue.workq,
			   &kbdev->devfreq_queue.work);
	}
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	dev_dbg(kbdev->dev, "Enqueuing devfreq req: %s\n",
		kbase_devfreq_req_type_name(work_type));
}

static int kbase_devfreq_work_init(struct kbase_device *kbdev)
{
	kbdev->devfreq_queue.req_type = DEVFREQ_WORK_NONE;
	kbdev->devfreq_queue.acted_type = DEVFREQ_WORK_RESUME;

	kbdev->devfreq_queue.workq = alloc_ordered_workqueue("devfreq_workq", 0);
	if (!kbdev->devfreq_queue.workq)
		return -ENOMEM;

	INIT_WORK(&kbdev->devfreq_queue.work,
			kbase_devfreq_suspend_resume_worker);
	return 0;
}

static void kbase_devfreq_work_term(struct kbase_device *kbdev)
{
	unsigned long flags;
	struct workqueue_struct *workq;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	workq = kbdev->devfreq_queue.workq;
	kbdev->devfreq_queue.workq = NULL;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	destroy_workqueue(workq);
}

static unsigned long kbase_devfreq_get_static_power(struct devfreq *devfreq,
		unsigned long voltage)
{
	struct device *dev = devfreq->dev.parent;
	struct kbase_device *kbdev = dev_get_drvdata(dev);

	return rockchip_ipa_get_static_power(kbdev->model_data, voltage);
}

int kbase_devfreq_init(struct kbase_device *kbdev)
{
	struct devfreq_cooling_power *kbase_dcp = &kbdev->dfc_power;
	struct device_node *np = kbdev->dev->of_node;
	struct device_node *model_node;
	struct devfreq_dev_profile *dp;
	int err;
	struct dev_pm_opp *opp;
	unsigned int i;
	bool free_devfreq_freq_table = true;

	if (kbdev->nr_clocks == 0) {
		dev_err(kbdev->dev, "Clock not available for devfreq\n");
		return -ENODEV;
	}

	for (i = 0; i < kbdev->nr_clocks; i++) {
		if (kbdev->clocks[i])
			kbdev->current_freqs[i] =
				clk_get_rate(kbdev->clocks[i]);
		else
			kbdev->current_freqs[i] = 0;
	}
	if (strstr(__clk_get_name(kbdev->clocks[0]), "scmi"))
		kbdev->opp_info.scmi_clk = kbdev->clocks[0];
	kbdev->current_nominal_freq = kbdev->current_freqs[0];

	opp = devfreq_recommended_opp(kbdev->dev, &kbdev->current_nominal_freq, 0);
	if (IS_ERR(opp))
		return PTR_ERR(opp);
	dev_pm_opp_put(opp);

	dp = &kbdev->devfreq_profile;

	dp->initial_freq = kbdev->current_nominal_freq;
	dp->polling_ms = 100;
	dp->target = kbase_devfreq_target;
	dp->get_dev_status = kbase_devfreq_status;
	dp->get_cur_freq = kbase_devfreq_cur_freq;
	dp->exit = kbase_devfreq_exit;

	if (kbase_devfreq_init_freq_table(kbdev, dp))
		return -EFAULT;

	if (dp->max_state > 0) {
		/* Record the maximum frequency possible */
		kbdev->gpu_props.props.core_props.gpu_freq_khz_max =
			dp->freq_table[0] / 1000;
	};

#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
	err = kbase_ipa_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "IPA initialization failed");
		goto ipa_init_failed;
	}
#endif

	err = kbase_devfreq_init_core_mask_table(kbdev);
	if (err)
		goto init_core_mask_table_failed;

	of_property_read_u32(np, "upthreshold",
			     &ondemand_data.upthreshold);
	of_property_read_u32(np, "downdifferential",
			     &ondemand_data.downdifferential);
	kbdev->devfreq = devfreq_add_device(kbdev->dev, dp,
				"simple_ondemand", &ondemand_data);
	if (IS_ERR(kbdev->devfreq)) {
		err = PTR_ERR(kbdev->devfreq);
		kbdev->devfreq = NULL;
		dev_err(kbdev->dev, "Fail to add devfreq device(%d)", err);
		goto devfreq_add_dev_failed;
	}

	/* Explicit free of freq table isn't needed after devfreq_add_device() */
	free_devfreq_freq_table = false;

	/* Initialize devfreq suspend/resume workqueue */
	err = kbase_devfreq_work_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "Fail to init devfreq workqueue");
		goto devfreq_work_init_failed;
	}

	/* devfreq_add_device only copies a few of kbdev->dev's fields, so
	 * set drvdata explicitly so IPA models can access kbdev.
	 */
	dev_set_drvdata(&kbdev->devfreq->dev, kbdev);

	err = devfreq_register_opp_notifier(kbdev->dev, kbdev->devfreq);
	if (err) {
		dev_err(kbdev->dev,
			"Failed to register OPP notifier (%d)", err);
		goto opp_notifier_failed;
	}

	mali_mdevp.data = kbdev->devfreq;
	mali_mdevp.opp_info = &kbdev->opp_info;
	kbdev->mdev_info = rockchip_system_monitor_register(kbdev->dev,
			&mali_mdevp);
	if (IS_ERR(kbdev->mdev_info)) {
		dev_dbg(kbdev->dev, "without system monitor\n");
               kbdev->mdev_info = NULL;
	       mali_mdevp.is_checked = true;
	}
#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
	of_property_read_u32(kbdev->dev->of_node, "dynamic-power-coefficient",
			     (u32 *)&kbase_dcp->dyn_power_coeff);
	model_node = of_get_compatible_child(kbdev->dev->of_node,
					     "simple-power-model");
	if (model_node) {
		of_node_put(model_node);
		kbdev->model_data =
			rockchip_ipa_power_model_init(kbdev->dev,
						      "gpu_leakage");
		if (IS_ERR_OR_NULL(kbdev->model_data)) {
			kbdev->model_data = NULL;
			if (kbase_dcp->dyn_power_coeff)
				dev_info(kbdev->dev,
					 "only calculate dynamic power\n");
			else
				dev_err(kbdev->dev,
					"failed to initialize power model\n");
		} else {
			kbase_dcp->get_static_power =
				kbase_devfreq_get_static_power;
			if (kbdev->model_data->dynamic_coefficient)
				kbase_dcp->dyn_power_coeff =
					kbdev->model_data->dynamic_coefficient;
		}
	}

	if (kbase_dcp->dyn_power_coeff) {
		kbdev->devfreq_cooling =
			of_devfreq_cooling_register_power(kbdev->dev->of_node,
					kbdev->devfreq,
					kbase_dcp);
		if (IS_ERR(kbdev->devfreq_cooling)) {
			err = PTR_ERR(kbdev->devfreq_cooling);
			dev_err(kbdev->dev, "failed to register cooling device\n");
			goto ipa_init_failed;
		}
	} else {
		kbdev->devfreq_cooling = of_devfreq_cooling_register_power(
				kbdev->dev->of_node,
				kbdev->devfreq,
				&kbase_ipa_power_model_ops);
		if (IS_ERR(kbdev->devfreq_cooling)) {
			err = PTR_ERR(kbdev->devfreq_cooling);
			dev_err(kbdev->dev,
					"Failed to register cooling device (%d)\n",
					err);
			goto cooling_reg_failed;
               }
	}
#endif

	return 0;

#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
cooling_reg_failed:
	devfreq_unregister_opp_notifier(kbdev->dev, kbdev->devfreq);
#endif /* CONFIG_DEVFREQ_THERMAL */

opp_notifier_failed:
	kbase_devfreq_work_term(kbdev);

devfreq_work_init_failed:
	if (devfreq_remove_device(kbdev->devfreq))
		dev_err(kbdev->dev, "Failed to terminate devfreq (%d)", err);

	kbdev->devfreq = NULL;

devfreq_add_dev_failed:
	kbase_devfreq_term_core_mask_table(kbdev);

init_core_mask_table_failed:
#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
	kbase_ipa_term(kbdev);
ipa_init_failed:
#endif
	if (free_devfreq_freq_table)
		kbase_devfreq_term_freq_table(kbdev);

	return err;
}

void kbase_devfreq_term(struct kbase_device *kbdev)
{
	int err;

	dev_dbg(kbdev->dev, "Term Mali devfreq\n");

#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
	if (kbdev->devfreq_cooling)
		devfreq_cooling_unregister(kbdev->devfreq_cooling);
#endif

	devfreq_unregister_opp_notifier(kbdev->dev, kbdev->devfreq);

	kbase_devfreq_work_term(kbdev);

	err = devfreq_remove_device(kbdev->devfreq);
	if (err)
		dev_err(kbdev->dev, "Failed to terminate devfreq (%d)\n", err);
	else
		kbdev->devfreq = NULL;

	kbase_devfreq_term_core_mask_table(kbdev);

#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
	if (!kbdev->model_data)
		kbase_ipa_term(kbdev);
	kfree(kbdev->model_data);
#endif
}
