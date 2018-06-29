/*
 *
 * (C) COPYRIGHT 2014-2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include <mali_kbase.h>
#include <mali_kbase_tlstream.h>
#include <mali_kbase_config_defaults.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

#include <linux/of.h>
#include <linux/clk.h>
#include <linux/devfreq.h>
#ifdef CONFIG_DEVFREQ_THERMAL
#include <linux/devfreq_cooling.h>
#endif

#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
#include <linux/pm_opp.h>
#else /* Linux >= 3.13 */
/* In 3.13 the OPP include header file, types, and functions were all
 * renamed. Use the old filename for the include, and define the new names to
 * the old, when an old kernel is detected.
 */
#include <linux/opp.h>
#define dev_pm_opp opp
#define dev_pm_opp_get_voltage opp_get_voltage
#define dev_pm_opp_get_opp_count opp_get_opp_count
#define dev_pm_opp_find_freq_ceil opp_find_freq_ceil
#define dev_pm_opp_find_freq_floor opp_find_freq_floor
#endif /* Linux >= 3.13 */

#include <soc/rockchip/rockchip_opp_select.h>

static struct thermal_opp_device_data gpu_devdata = {
	.type = THERMAL_OPP_TPYE_DEV,
	.low_temp_adjust = rockchip_dev_low_temp_adjust,
	.high_temp_adjust = rockchip_dev_high_temp_adjust,
};

/**
 * opp_translate - Translate nominal OPP frequency from devicetree into real
 *                 frequency and core mask
 * @kbdev:     Device pointer
 * @freq:      Nominal frequency
 * @core_mask: Pointer to u64 to store core mask to
 *
 * Return: Real target frequency
 *
 * This function will only perform translation if an operating-points-v2-mali
 * table is present in devicetree. If one is not present then it will return an
 * untranslated frequency and all cores enabled.
 */
static unsigned long opp_translate(struct kbase_device *kbdev,
		unsigned long freq, u64 *core_mask)
{
	int i;

	for (i = 0; i < kbdev->num_opps; i++) {
		if (kbdev->opp_table[i].opp_freq == freq) {
			*core_mask = kbdev->opp_table[i].core_mask;
			return kbdev->opp_table[i].real_freq;
		}
	}

	/* Failed to find OPP - return all cores enabled & nominal frequency */
	*core_mask = kbdev->gpu_props.props.raw_props.shader_present;

	return freq;
}

static int
kbase_devfreq_target(struct device *dev, unsigned long *target_freq, u32 flags)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	unsigned long nominal_freq;
	unsigned long freq = 0;
	unsigned long voltage;
	int err;
	u64 core_mask;

	freq = *target_freq;

	rcu_read_lock();
	opp = devfreq_recommended_opp(dev, &freq, flags);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(dev, "Failed to get opp (%ld)\n", PTR_ERR(opp));
		return PTR_ERR(opp);
	}
	voltage = dev_pm_opp_get_voltage(opp);
	rcu_read_unlock();

	nominal_freq = freq;
	/*
	 * Only update if there is a change of frequency
	 */
	if (kbdev->current_nominal_freq == nominal_freq) {
		*target_freq = nominal_freq;
#ifdef CONFIG_REGULATOR
		if (kbdev->current_voltage == voltage)
			return 0;
		err = regulator_set_voltage(kbdev->regulator, voltage, INT_MAX);
		if (err) {
			dev_err(dev, "Failed to set voltage (%d)\n", err);
			return err;
		}
		kbdev->current_voltage = voltage;
#endif
		return 0;
	}

	freq = opp_translate(kbdev, nominal_freq, &core_mask);
#ifdef CONFIG_REGULATOR
	if (kbdev->regulator && kbdev->current_voltage != voltage
			&& kbdev->current_freq < freq) {
		err = regulator_set_voltage(kbdev->regulator, voltage, INT_MAX);
		if (err) {
			dev_err(dev, "Failed to increase voltage (%d)\n", err);
			return err;
		}
	}
#endif

	err = clk_set_rate(kbdev->clock, freq);
	if (err) {
		dev_err(dev, "Failed to set clock %lu (target %lu)\n",
				freq, *target_freq);
		return err;
	}

#ifdef CONFIG_REGULATOR
	if (kbdev->regulator && kbdev->current_voltage != voltage
			&& kbdev->current_freq > freq) {
		err = regulator_set_voltage(kbdev->regulator, voltage, INT_MAX);
		if (err) {
			dev_err(dev, "Failed to decrease voltage (%d)\n", err);
			return err;
		}
	}
#endif

	if (kbdev->pm.backend.ca_current_policy->id ==
			KBASE_PM_CA_POLICY_ID_DEVFREQ)
		kbase_devfreq_set_core_mask(kbdev, core_mask);

	*target_freq = nominal_freq;
	kbdev->current_voltage = voltage;
	kbdev->current_nominal_freq = nominal_freq;
	kbdev->current_freq = freq;
	kbdev->current_core_mask = core_mask;
	if (kbdev->devfreq)
		kbdev->devfreq->last_status.current_frequency = nominal_freq;

	KBASE_TLSTREAM_AUX_DEVFREQ_TARGET((u64)nominal_freq);

	kbase_pm_reset_dvfs_utilisation(kbdev);

	return err;
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

	kbase_pm_get_dvfs_utilisation(kbdev,
			&stat->total_time, &stat->busy_time);

	stat->private_data = NULL;

	return 0;
}

static int kbase_devfreq_init_freq_table(struct kbase_device *kbdev,
		struct devfreq_dev_profile *dp)
{
	int count;
	int i = 0;
	unsigned long freq;
	struct dev_pm_opp *opp;

	rcu_read_lock();
	count = dev_pm_opp_get_opp_count(kbdev->dev);
	if (count < 0) {
		rcu_read_unlock();
		return count;
	}
	rcu_read_unlock();

	dp->freq_table = kmalloc_array(count, sizeof(dp->freq_table[0]),
				GFP_KERNEL);
	if (!dp->freq_table)
		return -ENOMEM;

	rcu_read_lock();
	for (i = 0, freq = ULONG_MAX; i < count; i++, freq--) {
		opp = dev_pm_opp_find_freq_floor(kbdev->dev, &freq);
		if (IS_ERR(opp))
			break;

		dp->freq_table[i] = freq;
	}
	rcu_read_unlock();

	if (count != i)
		dev_warn(kbdev->dev, "Unable to enumerate all OPPs (%d!=%d\n",
				count, i);

	dp->max_state = i;

	return 0;
}

static void kbase_devfreq_term_freq_table(struct kbase_device *kbdev)
{
	struct devfreq_dev_profile *dp = kbdev->devfreq->profile;

	kfree(dp->freq_table);
}

static void kbase_devfreq_exit(struct device *dev)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);

	kbase_devfreq_term_freq_table(kbdev);
}

static int kbase_devfreq_init_core_mask_table(struct kbase_device *kbdev)
{
	struct device_node *opp_node = of_parse_phandle(kbdev->dev->of_node,
			"operating-points-v2", 0);
	struct device_node *node;
	int i = 0;
	int count;

	if (!opp_node)
		return 0;
	if (!of_device_is_compatible(opp_node, "operating-points-v2-mali"))
		return 0;

	count = dev_pm_opp_get_opp_count(kbdev->dev);
	kbdev->opp_table = kmalloc_array(count,
			sizeof(struct kbase_devfreq_opp), GFP_KERNEL);
	if (!kbdev->opp_table)
		return -ENOMEM;

	for_each_available_child_of_node(opp_node, node) {
		u64 core_mask;
		u64 opp_freq, real_freq;
		const void *core_count_p;

		if (of_property_read_u64(node, "opp-hz", &opp_freq)) {
			dev_warn(kbdev->dev, "OPP is missing required opp-hz property\n");
			continue;
		}
		if (of_property_read_u64(node, "opp-hz-real", &real_freq))
			real_freq = opp_freq;
		if (of_property_read_u64(node, "opp-core-mask", &core_mask))
			core_mask =
				kbdev->gpu_props.props.raw_props.shader_present;
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

		kbdev->opp_table[i].opp_freq = opp_freq;
		kbdev->opp_table[i].real_freq = real_freq;
		kbdev->opp_table[i].core_mask = core_mask;

		dev_info(kbdev->dev, "OPP %d : opp_freq=%llu real_freq=%llu core_mask=%llx\n",
				i, opp_freq, real_freq, core_mask);

		i++;
	}

	kbdev->num_opps = i;

	return 0;
}

int kbase_devfreq_init(struct kbase_device *kbdev)
{
	struct devfreq_dev_profile *dp;
	unsigned long opp_rate;
	int err;

	if (!kbdev->clock) {
		dev_err(kbdev->dev, "Clock not available for devfreq\n");
		return -ENODEV;
	}

	kbdev->current_freq = clk_get_rate(kbdev->clock);
	kbdev->current_nominal_freq = kbdev->current_freq;

	dp = &kbdev->devfreq_profile;

	dp->initial_freq = kbdev->current_freq;
	dp->polling_ms = 100;
	dp->target = kbase_devfreq_target;
	dp->get_dev_status = kbase_devfreq_status;
	dp->get_cur_freq = kbase_devfreq_cur_freq;
	dp->exit = kbase_devfreq_exit;

	if (kbase_devfreq_init_freq_table(kbdev, dp))
		return -EFAULT;

	err = kbase_devfreq_init_core_mask_table(kbdev);
	if (err)
		return err;

	kbdev->devfreq = devfreq_add_device(kbdev->dev, dp,
				"simple_ondemand", NULL);
	if (IS_ERR(kbdev->devfreq)) {
		kbase_devfreq_term_freq_table(kbdev);
		return PTR_ERR(kbdev->devfreq);
	}

	/* devfreq_add_device only copies a few of kbdev->dev's fields, so
	 * set drvdata explicitly so IPA models can access kbdev. */
	dev_set_drvdata(&kbdev->devfreq->dev, kbdev);

	err = devfreq_register_opp_notifier(kbdev->dev, kbdev->devfreq);
	if (err) {
		dev_err(kbdev->dev,
			"Failed to register OPP notifier (%d)\n", err);
		goto opp_notifier_failed;
	}

	opp_rate = kbdev->current_freq;
	rcu_read_lock();
	devfreq_recommended_opp(kbdev->dev, &opp_rate, 0);
	rcu_read_unlock();
	kbdev->devfreq->last_status.current_frequency = opp_rate;
	gpu_devdata.data = kbdev->devfreq;
	kbdev->opp_info = rockchip_register_thermal_notifier(kbdev->dev,
							     &gpu_devdata);
	if (IS_ERR(kbdev->opp_info)) {
		dev_dbg(kbdev->dev, "without thermal notifier\n");
		kbdev->opp_info = NULL;
	}
#ifdef CONFIG_DEVFREQ_THERMAL
	err = kbase_ipa_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "IPA initialization failed\n");
		goto cooling_failed;
	}

	kbdev->devfreq_cooling = of_devfreq_cooling_register_power(
			kbdev->dev->of_node,
			kbdev->devfreq,
			&kbase_ipa_power_model_ops);
	if (IS_ERR_OR_NULL(kbdev->devfreq_cooling)) {
		err = PTR_ERR(kbdev->devfreq_cooling);
		dev_err(kbdev->dev,
			"Failed to register cooling device (%d)\n",
			err);
		goto cooling_failed;
	}
#endif

	return 0;

#ifdef CONFIG_DEVFREQ_THERMAL
cooling_failed:
	devfreq_unregister_opp_notifier(kbdev->dev, kbdev->devfreq);
#endif /* CONFIG_DEVFREQ_THERMAL */
opp_notifier_failed:
	if (devfreq_remove_device(kbdev->devfreq))
		dev_err(kbdev->dev, "Failed to terminate devfreq (%d)\n", err);
	else
		kbdev->devfreq = NULL;

	return err;
}

void kbase_devfreq_term(struct kbase_device *kbdev)
{
	int err;

	dev_dbg(kbdev->dev, "Term Mali devfreq\n");

	rockchip_unregister_thermal_notifier(kbdev->opp_info);
#ifdef CONFIG_DEVFREQ_THERMAL
	if (kbdev->devfreq_cooling)
		devfreq_cooling_unregister(kbdev->devfreq_cooling);

	kbase_ipa_term(kbdev);
#endif

	devfreq_unregister_opp_notifier(kbdev->dev, kbdev->devfreq);

	err = devfreq_remove_device(kbdev->devfreq);
	if (err)
		dev_err(kbdev->dev, "Failed to terminate devfreq (%d)\n", err);
	else
		kbdev->devfreq = NULL;

	kfree(kbdev->opp_table);
}
