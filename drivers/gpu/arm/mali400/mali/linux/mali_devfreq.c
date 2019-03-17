/*
 * Copyright (C) 2011-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_osk_mali.h"
#include "mali_kernel_common.h"

#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
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
#endif /* Linux >= 3.13 */

#include "mali_pm_metrics.h"

#include <soc/rockchip/rockchip_opp_select.h>
#include <soc/rockchip/rockchip_system_monitor.h>

static struct monitor_dev_profile mali_mdevp = {
	.type = MONITOR_TPYE_DEV,
	.low_temp_adjust = rockchip_monitor_dev_low_temp_adjust,
	.high_temp_adjust = rockchip_monitor_dev_high_temp_adjust,
};

static int
mali_devfreq_target(struct device *dev, unsigned long *target_freq, u32 flags)
{
	struct mali_device *mdev = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	unsigned long freq = 0;
	unsigned long old_freq = mdev->current_freq;
	unsigned long voltage;
	int err;

	freq = *target_freq;

	rcu_read_lock();
	opp = devfreq_recommended_opp(dev, &freq, flags);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		MALI_PRINT_ERROR(("Failed to get opp (%ld)\n", PTR_ERR(opp)));
		return PTR_ERR(opp);
	}
	voltage = dev_pm_opp_get_voltage(opp);
	rcu_read_unlock();

	MALI_DEBUG_PRINT(2, ("mali_devfreq_target:set_freq = %lld flags = 0x%x\n", freq, flags));
	/*
	 * Only update if there is a change of frequency
	 */
	if (old_freq == freq) {
		*target_freq = freq;
		mali_pm_reset_dvfs_utilisation(mdev);
#ifdef CONFIG_REGULATOR
		if (mdev->current_voltage == voltage)
			return 0;
		err = regulator_set_voltage(mdev->regulator, voltage, INT_MAX);
		if (err) {
			dev_err(dev, "Failed to set voltage (%d)\n", err);
			return err;
		}
		mdev->current_voltage = voltage;
#endif
		return 0;
	}

#ifdef CONFIG_REGULATOR
	if (mdev->regulator && mdev->current_voltage != voltage &&
	    old_freq < freq) {
		err = regulator_set_voltage(mdev->regulator, voltage, INT_MAX);
		if (err) {
			MALI_PRINT_ERROR(("Failed to increase voltage (%d)\n", err));
			return err;
		}
	}
#endif

	err = clk_set_rate(mdev->clock, freq);
	if (err) {
		MALI_PRINT_ERROR(("Failed to set clock %lu (target %lu)\n", freq, *target_freq));
		return err;
	}
	*target_freq = freq;
	mdev->current_freq = freq;
	if (mdev->devfreq)
		mdev->devfreq->last_status.current_frequency = freq;

#ifdef CONFIG_REGULATOR
	if (mdev->regulator && mdev->current_voltage != voltage &&
	    old_freq > freq) {
		err = regulator_set_voltage(mdev->regulator, voltage, INT_MAX);
		if (err) {
			MALI_PRINT_ERROR(("Failed to decrease voltage (%d)\n", err));
			return err;
		}
	}
#endif

	mdev->current_voltage = voltage;

	mali_pm_reset_dvfs_utilisation(mdev);

	return err;
}

static int
mali_devfreq_cur_freq(struct device *dev, unsigned long *freq)
{
	struct mali_device *mdev = dev_get_drvdata(dev);

	*freq = mdev->current_freq;

	MALI_DEBUG_PRINT(2, ("mali_devfreq_cur_freq: freq = %d \n", *freq));
	return 0;
}

static int
mali_devfreq_status(struct device *dev, struct devfreq_dev_status *stat)
{
	struct mali_device *mdev = dev_get_drvdata(dev);

	stat->current_frequency = mdev->current_freq;

	mali_pm_get_dvfs_utilisation(mdev,
				     &stat->total_time, &stat->busy_time);

	stat->private_data = NULL;

#ifdef CONFIG_DEVFREQ_THERMAL
	memcpy(&mdev->devfreq->last_status, stat, sizeof(*stat));
#endif

	return 0;
}

/* setup platform specific opp in platform.c*/
int __weak setup_opps(void)
{
	return 0;
}

/* term platform specific opp in platform.c*/
int __weak term_opps(struct device *dev)
{
	return 0;
}

static int mali_devfreq_init_freq_table(struct mali_device *mdev,
					struct devfreq_dev_profile *dp)
{
	int err, count;
	int i = 0;
	unsigned long freq = 0;
	struct dev_pm_opp *opp;

	err = setup_opps();
	if (err)
		return err;

	rcu_read_lock();
	count = dev_pm_opp_get_opp_count(mdev->dev);
	if (count < 0) {
		rcu_read_unlock();
		return count;
	}
	rcu_read_unlock();

	MALI_DEBUG_PRINT(2, ("mali devfreq table count %d\n", count));

	dp->freq_table = kmalloc_array(count, sizeof(dp->freq_table[0]),
				       GFP_KERNEL);
	if (!dp->freq_table)
		return -ENOMEM;

	rcu_read_lock();
	for (i = 0; i < count; i++, freq++) {
		opp = dev_pm_opp_find_freq_ceil(mdev->dev, &freq);
		if (IS_ERR(opp))
			break;

		dp->freq_table[i] = freq;
		MALI_DEBUG_PRINT(2, ("mali devfreq table array[%d] = %d\n", i, freq));
	}
	rcu_read_unlock();

	if (count != i)
		MALI_PRINT_ERROR(("Unable to enumerate all OPPs (%d!=%d)\n",
				  count, i));

	dp->max_state = i;

	return 0;
}

static void mali_devfreq_term_freq_table(struct mali_device *mdev)
{
	struct devfreq_dev_profile *dp = mdev->devfreq->profile;

	kfree(dp->freq_table);
	term_opps(mdev->dev);
}

static void mali_devfreq_exit(struct device *dev)
{
	struct mali_device *mdev = dev_get_drvdata(dev);

	mali_devfreq_term_freq_table(mdev);
}

int mali_devfreq_init(struct mali_device *mdev)
{
#ifdef CONFIG_DEVFREQ_THERMAL
	struct devfreq_cooling_power *callbacks = NULL;
	_mali_osk_device_data data;
#endif
	struct devfreq_dev_profile *dp;
	unsigned long opp_rate;
	int err;

	MALI_DEBUG_PRINT(2, ("Init Mali devfreq\n"));

	if (!mdev->clock)
		return -ENODEV;

	mdev->current_freq = clk_get_rate(mdev->clock);

	dp = &mdev->devfreq_profile;

	dp->initial_freq = mdev->current_freq;
	dp->polling_ms = 100;
	dp->target = mali_devfreq_target;
	dp->get_dev_status = mali_devfreq_status;
	dp->get_cur_freq = mali_devfreq_cur_freq;
	dp->exit = mali_devfreq_exit;

	if (mali_devfreq_init_freq_table(mdev, dp))
		return -EFAULT;

	mdev->devfreq = devfreq_add_device(mdev->dev, dp,
					   "simple_ondemand", NULL);
	if (IS_ERR(mdev->devfreq)) {
		mali_devfreq_term_freq_table(mdev);
		return PTR_ERR(mdev->devfreq);
	}

	err = devfreq_register_opp_notifier(mdev->dev, mdev->devfreq);
	if (err) {
		MALI_PRINT_ERROR(("Failed to register OPP notifier (%d)\n", err));
		goto opp_notifier_failed;
	}

	opp_rate = mdev->current_freq;
	rcu_read_lock();
	devfreq_recommended_opp(mdev->dev, &opp_rate, 0);
	rcu_read_unlock();
	mdev->devfreq->last_status.current_frequency = opp_rate;

	mali_mdevp.data = mdev->devfreq;
	mdev->mdev_info = rockchip_system_monitor_register(mdev->dev,
							   &mali_mdevp);
	if (IS_ERR(mdev->mdev_info)) {
		dev_dbg(mdev->dev, "without system monitor\n");
		mdev->mdev_info = NULL;
	}
#ifdef CONFIG_DEVFREQ_THERMAL
	if (of_machine_is_compatible("rockchip,rk3036"))
		return 0;

	/* Initilization last_status it will be used when first power allocate called */
	mdev->devfreq->last_status.current_frequency = mdev->current_freq;

	if (_MALI_OSK_ERR_OK == _mali_osk_device_data_get(&data)) {
		if (NULL != data.gpu_cooling_ops) {
			callbacks = data.gpu_cooling_ops;
			MALI_DEBUG_PRINT(2, ("Mali GPU Thermal: Callback handler installed \n"));
		}
	}

	if (callbacks) {
		mdev->devfreq_cooling = of_devfreq_cooling_register_power(
						mdev->dev->of_node,
						mdev->devfreq,
						callbacks);
		if (IS_ERR_OR_NULL(mdev->devfreq_cooling)) {
			err = PTR_ERR(mdev->devfreq_cooling);
			MALI_PRINT_ERROR(("Failed to register cooling device (%d)\n", err));
			goto cooling_failed;
		} else {
			MALI_DEBUG_PRINT(2, ("Mali GPU Thermal Cooling installed \n"));
		}
	}
#endif

	return 0;

#ifdef CONFIG_DEVFREQ_THERMAL
cooling_failed:
	devfreq_unregister_opp_notifier(mdev->dev, mdev->devfreq);
#endif /* CONFIG_DEVFREQ_THERMAL */
opp_notifier_failed:
	err = devfreq_remove_device(mdev->devfreq);
	if (err)
		MALI_PRINT_ERROR(("Failed to terminate devfreq (%d)\n", err));
	else
		mdev->devfreq = NULL;

	return err;
}

void mali_devfreq_term(struct mali_device *mdev)
{
	int err;

	MALI_DEBUG_PRINT(2, ("Term Mali devfreq\n"));

	rockchip_system_monitor_unregister(mdev->mdev_info);
#ifdef CONFIG_DEVFREQ_THERMAL
	devfreq_cooling_unregister(mdev->devfreq_cooling);
#endif

	devfreq_unregister_opp_notifier(mdev->dev, mdev->devfreq);

	err = devfreq_remove_device(mdev->devfreq);
	if (err)
		MALI_PRINT_ERROR(("Failed to terminate devfreq (%d)\n", err));
	else
		mdev->devfreq = NULL;
}
