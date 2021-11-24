// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2016-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

#include <linux/thermal.h>
#include <linux/devfreq_cooling.h>
#include <linux/of.h>
#include "mali_kbase.h"
#include "mali_kbase_ipa.h"
#include "mali_kbase_ipa_debugfs.h"
#include "mali_kbase_ipa_simple.h"
#include "backend/gpu/mali_kbase_pm_internal.h"
#include "backend/gpu/mali_kbase_devfreq.h"
#include <linux/pm_opp.h>

#define KBASE_IPA_FALLBACK_MODEL_NAME "mali-simple-power-model"

/* Polling by thermal governor starts when the temperature exceeds the certain
 * trip point. In order to have meaningful value for the counters, when the
 * polling starts and first call to kbase_get_real_power() is made, it is
 * required to reset the counter values every now and then.
 * It is reasonable to do the reset every second if no polling is being done,
 * the counter model implementation also assumes max sampling interval of 1 sec.
 */
#define RESET_INTERVAL_MS ((s64)1000)

int kbase_ipa_model_recalculate(struct kbase_ipa_model *model)
{
	int err = 0;

	lockdep_assert_held(&model->kbdev->ipa.lock);

	if (model->ops->recalculate) {
		err = model->ops->recalculate(model);
		if (err) {
			dev_err(model->kbdev->dev,
				"recalculation of power model %s returned error %d\n",
				model->ops->name, err);
		}
	}

	return err;
}

const struct kbase_ipa_model_ops *kbase_ipa_model_ops_find(struct kbase_device *kbdev,
							   const char *name)
{
	if (!strcmp(name, kbase_simple_ipa_model_ops.name))
		return &kbase_simple_ipa_model_ops;

	return kbase_ipa_counter_model_ops_find(kbdev, name);
}
KBASE_EXPORT_TEST_API(kbase_ipa_model_ops_find);

const char *kbase_ipa_model_name_from_id(u32 gpu_id)
{
	const char* model_name =
		kbase_ipa_counter_model_name_from_id(gpu_id);

	if (!model_name)
		return KBASE_IPA_FALLBACK_MODEL_NAME;
	else
		return model_name;
}
KBASE_EXPORT_TEST_API(kbase_ipa_model_name_from_id);

static struct device_node *get_model_dt_node(struct kbase_ipa_model *model,
					     bool dt_required)
{
	struct device_node *model_dt_node;
	char compat_string[64];

	snprintf(compat_string, sizeof(compat_string), "arm,%s",
		 model->ops->name);

	/* of_find_compatible_node() will call of_node_put() on the root node,
	 * so take a reference on it first.
	 */
	of_node_get(model->kbdev->dev->of_node);
	model_dt_node = of_find_compatible_node(model->kbdev->dev->of_node,
						NULL, compat_string);
	if (!model_dt_node && !model->missing_dt_node_warning) {
		if (dt_required)
			dev_warn(model->kbdev->dev,
			"Couldn't find power_model DT node matching \'%s\'\n",
			compat_string);
		model->missing_dt_node_warning = true;
	}

	return model_dt_node;
}

int kbase_ipa_model_add_param_s32(struct kbase_ipa_model *model,
				  const char *name, s32 *addr,
				  size_t num_elems, bool dt_required)
{
	int err, i;
	struct device_node *model_dt_node = get_model_dt_node(model,
								dt_required);
	char *origin;

	err = of_property_read_u32_array(model_dt_node, name, addr, num_elems);
	/* We're done with model_dt_node now, so drop the reference taken in
	 * get_model_dt_node()/of_find_compatible_node().
	 */
	of_node_put(model_dt_node);

	if (err && dt_required) {
		memset(addr, 0, sizeof(s32) * num_elems);
		dev_warn(model->kbdev->dev,
			 "Error %d, no DT entry: %s.%s = %zu*[0]\n",
			 err, model->ops->name, name, num_elems);
		origin = "zero";
	} else if (err && !dt_required) {
		origin = "default";
	} else /* !err */ {
		origin = "DT";
	}

	/* Create a unique debugfs entry for each element */
	for (i = 0; i < num_elems; ++i) {
		char elem_name[32];

		if (num_elems == 1)
			snprintf(elem_name, sizeof(elem_name), "%s", name);
		else
			snprintf(elem_name, sizeof(elem_name), "%s.%d",
				name, i);

		dev_dbg(model->kbdev->dev, "%s.%s = %d (%s)\n",
			model->ops->name, elem_name, addr[i], origin);

		err = kbase_ipa_model_param_add(model, elem_name,
						&addr[i], sizeof(s32),
						PARAM_TYPE_S32);
		if (err)
			goto exit;
	}
exit:
	return err;
}

int kbase_ipa_model_add_param_string(struct kbase_ipa_model *model,
				     const char *name, char *addr,
				     size_t size, bool dt_required)
{
	int err;
	struct device_node *model_dt_node = get_model_dt_node(model,
								dt_required);
	const char *string_prop_value;
	char *origin;

	err = of_property_read_string(model_dt_node, name,
				      &string_prop_value);

	/* We're done with model_dt_node now, so drop the reference taken in
	 * get_model_dt_node()/of_find_compatible_node().
	 */
	of_node_put(model_dt_node);

	if (err && dt_required) {
		strncpy(addr, "", size - 1);
		dev_warn(model->kbdev->dev,
			 "Error %d, no DT entry: %s.%s = \'%s\'\n",
			 err, model->ops->name, name, addr);
		err = 0;
		origin = "zero";
	} else if (err && !dt_required) {
		origin = "default";
	} else /* !err */ {
		strncpy(addr, string_prop_value, size - 1);
		origin = "DT";
	}

	addr[size - 1] = '\0';

	dev_dbg(model->kbdev->dev, "%s.%s = \'%s\' (%s)\n",
		model->ops->name, name, string_prop_value, origin);

	err = kbase_ipa_model_param_add(model, name, addr, size,
					PARAM_TYPE_STRING);
	return err;
}

void kbase_ipa_term_model(struct kbase_ipa_model *model)
{
	if (!model)
		return;

	lockdep_assert_held(&model->kbdev->ipa.lock);

	if (model->ops->term)
		model->ops->term(model);

	kbase_ipa_model_param_free_all(model);

	kfree(model);
}
KBASE_EXPORT_TEST_API(kbase_ipa_term_model);

struct kbase_ipa_model *kbase_ipa_init_model(struct kbase_device *kbdev,
					     const struct kbase_ipa_model_ops *ops)
{
	struct kbase_ipa_model *model;
	int err;

	lockdep_assert_held(&kbdev->ipa.lock);

	if (!ops || !ops->name)
		return NULL;

	model = kzalloc(sizeof(struct kbase_ipa_model), GFP_KERNEL);
	if (!model)
		return NULL;

	model->kbdev = kbdev;
	model->ops = ops;
	INIT_LIST_HEAD(&model->params);

	err = model->ops->init(model);
	if (err) {
		dev_err(kbdev->dev,
			"init of power model \'%s\' returned error %d\n",
			ops->name, err);
		kfree(model);
		return NULL;
	}

	err = kbase_ipa_model_recalculate(model);
	if (err) {
		kbase_ipa_term_model(model);
		return NULL;
	}

	return model;
}
KBASE_EXPORT_TEST_API(kbase_ipa_init_model);

static void kbase_ipa_term_locked(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->ipa.lock);

	/* Clean up the models */
	if (kbdev->ipa.configured_model != kbdev->ipa.fallback_model)
		kbase_ipa_term_model(kbdev->ipa.configured_model);
	kbase_ipa_term_model(kbdev->ipa.fallback_model);

	kbdev->ipa.configured_model = NULL;
	kbdev->ipa.fallback_model = NULL;
}

int kbase_ipa_init(struct kbase_device *kbdev)
{

	const char *model_name;
	const struct kbase_ipa_model_ops *ops;
	struct kbase_ipa_model *default_model = NULL;
	int err;

	mutex_init(&kbdev->ipa.lock);
	/*
	 * Lock during init to avoid warnings from lockdep_assert_held (there
	 * shouldn't be any concurrent access yet).
	 */
	mutex_lock(&kbdev->ipa.lock);

	/* The simple IPA model must *always* be present.*/
	ops = kbase_ipa_model_ops_find(kbdev, KBASE_IPA_FALLBACK_MODEL_NAME);

	default_model = kbase_ipa_init_model(kbdev, ops);
	if (!default_model) {
		err = -EINVAL;
		goto end;
	}

	kbdev->ipa.fallback_model = default_model;
	err = of_property_read_string(kbdev->dev->of_node,
				      "ipa-model",
				      &model_name);
	if (err) {
		/* Attempt to load a match from GPU-ID */
		u32 gpu_id;

		gpu_id = kbdev->gpu_props.props.raw_props.gpu_id;
		model_name = kbase_ipa_model_name_from_id(gpu_id);
		dev_dbg(kbdev->dev,
			"Inferring model from GPU ID 0x%x: \'%s\'\n",
			gpu_id, model_name);
		err = 0;
	} else {
		dev_dbg(kbdev->dev,
			"Using ipa-model parameter from DT: \'%s\'\n",
			model_name);
	}

	if (strcmp(KBASE_IPA_FALLBACK_MODEL_NAME, model_name) != 0) {
		ops = kbase_ipa_model_ops_find(kbdev, model_name);
		kbdev->ipa.configured_model = kbase_ipa_init_model(kbdev, ops);
		if (!kbdev->ipa.configured_model) {
			dev_warn(kbdev->dev,
				"Failed to initialize ipa-model: \'%s\'\n"
				"Falling back on default model\n",
				model_name);
			kbdev->ipa.configured_model = default_model;
		}
	} else {
		kbdev->ipa.configured_model = default_model;
	}

	kbdev->ipa.last_sample_time = ktime_get();

end:
	if (err)
		kbase_ipa_term_locked(kbdev);
	else
		dev_info(kbdev->dev,
			 "Using configured power model %s, and fallback %s\n",
			 kbdev->ipa.configured_model->ops->name,
			 kbdev->ipa.fallback_model->ops->name);

	mutex_unlock(&kbdev->ipa.lock);
	return err;
}
KBASE_EXPORT_TEST_API(kbase_ipa_init);

void kbase_ipa_term(struct kbase_device *kbdev)
{
	mutex_lock(&kbdev->ipa.lock);
	kbase_ipa_term_locked(kbdev);
	mutex_unlock(&kbdev->ipa.lock);

	mutex_destroy(&kbdev->ipa.lock);
}
KBASE_EXPORT_TEST_API(kbase_ipa_term);

/**
 * kbase_scale_dynamic_power() - Scale a dynamic power coefficient to an OPP
 * @c:		Dynamic model coefficient, in pW/(Hz V^2). Should be in range
 *		0 < c < 2^26 to prevent overflow.
 * @freq:	Frequency, in Hz. Range: 2^23 < freq < 2^30 (~8MHz to ~1GHz)
 * @voltage:	Voltage, in mV. Range: 2^9 < voltage < 2^13 (~0.5V to ~8V)
 *
 * Keep a record of the approximate range of each value at every stage of the
 * calculation, to ensure we don't overflow. This makes heavy use of the
 * approximations 1000 = 2^10 and 1000000 = 2^20, but does the actual
 * calculations in decimal for increased accuracy.
 *
 * Return: Power consumption, in mW. Range: 0 < p < 2^13 (0W to ~8W)
 */
static u32 kbase_scale_dynamic_power(const u32 c, const u32 freq,
				     const u32 voltage)
{
	/* Range: 2^8 < v2 < 2^16 m(V^2) */
	const u32 v2 = (voltage * voltage) / 1000;

	/* Range: 2^3 < f_MHz < 2^10 MHz */
	const u32 f_MHz = freq / 1000000;

	/* Range: 2^11 < v2f_big < 2^26 kHz V^2 */
	const u32 v2f_big = v2 * f_MHz;

	/* Range: 2^1 < v2f < 2^16 MHz V^2 */
	const u32 v2f = v2f_big / 1000;

	/* Range (working backwards from next line): 0 < v2fc < 2^23 uW.
	 * Must be < 2^42 to avoid overflowing the return value.
	 */
	const u64 v2fc = (u64) c * (u64) v2f;

	/* Range: 0 < v2fc / 1000 < 2^13 mW */
	return div_u64(v2fc, 1000);
}

/**
 * kbase_scale_static_power() - Scale a static power coefficient to an OPP
 * @c:		Static model coefficient, in uW/V^3. Should be in range
 *		0 < c < 2^32 to prevent overflow.
 * @voltage:	Voltage, in mV. Range: 2^9 < voltage < 2^13 (~0.5V to ~8V)
 *
 * Return: Power consumption, in mW. Range: 0 < p < 2^13 (0W to ~8W)
 */
static u32 kbase_scale_static_power(const u32 c, const u32 voltage)
{
	/* Range: 2^8 < v2 < 2^16 m(V^2) */
	const u32 v2 = (voltage * voltage) / 1000;

	/* Range: 2^17 < v3_big < 2^29 m(V^2) mV */
	const u32 v3_big = v2 * voltage;

	/* Range: 2^7 < v3 < 2^19 m(V^3) */
	const u32 v3 = v3_big / 1000;

	/*
	 * Range (working backwards from next line): 0 < v3c_big < 2^33 nW.
	 * The result should be < 2^52 to avoid overflowing the return value.
	 */
	const u64 v3c_big = (u64) c * (u64) v3;

	/* Range: 0 < v3c_big / 1000000 < 2^13 mW */
	return div_u64(v3c_big, 1000000);
}

void kbase_ipa_protection_mode_switch_event(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* Record the event of GPU entering protected mode. */
	kbdev->ipa_protection_mode_switched = true;
}

static struct kbase_ipa_model *get_current_model(struct kbase_device *kbdev)
{
	struct kbase_ipa_model *model;
	unsigned long flags;

	lockdep_assert_held(&kbdev->ipa.lock);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if (kbdev->ipa_protection_mode_switched ||
			kbdev->ipa.force_fallback_model)
		model = kbdev->ipa.fallback_model;
	else
		model = kbdev->ipa.configured_model;

	/*
	 * Having taken cognizance of the fact that whether GPU earlier
	 * protected mode or not, the event can be now reset (if GPU is not
	 * currently in protected mode) so that configured model is used
	 * for the next sample.
	 */
	if (!kbdev->protected_mode)
		kbdev->ipa_protection_mode_switched = false;

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return model;
}

static u32 get_static_power_locked(struct kbase_device *kbdev,
				   struct kbase_ipa_model *model,
				   unsigned long voltage)
{
	u32 power = 0;
	int err;
	u32 power_coeff;

	lockdep_assert_held(&model->kbdev->ipa.lock);

	if (!model->ops->get_static_coeff)
		model = kbdev->ipa.fallback_model;

	if (model->ops->get_static_coeff) {
		err = model->ops->get_static_coeff(model, &power_coeff);
		if (!err)
			power = kbase_scale_static_power(power_coeff,
							 (u32) voltage);
	}

	return power;
}

#if KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE
#if defined(CONFIG_MALI_PWRSOFT_765) ||                                        \
	KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE
static unsigned long kbase_get_static_power(struct devfreq *df,
					    unsigned long voltage)
#else
static unsigned long kbase_get_static_power(unsigned long voltage)
#endif
{
	struct kbase_ipa_model *model;
	u32 power = 0;
#if defined(CONFIG_MALI_PWRSOFT_765) ||                                        \
	KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE
	struct kbase_device *kbdev = dev_get_drvdata(&df->dev);
#else
	struct kbase_device *kbdev = kbase_find_device(-1);
#endif

	if (!kbdev)
		return 0ul;

	mutex_lock(&kbdev->ipa.lock);

	model = get_current_model(kbdev);
	power = get_static_power_locked(kbdev, model, voltage);

	mutex_unlock(&kbdev->ipa.lock);

#if !(defined(CONFIG_MALI_PWRSOFT_765) ||                                      \
	KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE)
	kbase_release_device(kbdev);
#endif

	return power;
}
#endif /* KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE */

/**
 * opp_translate_freq_voltage() - Translate nominal OPP frequency from
 *                                devicetree into the real frequency for
 *                                top-level and shader cores.
 * @kbdev:            Device pointer
 * @nominal_freq:     Nominal frequency in Hz.
 * @nominal_voltage:  Nominal voltage, in mV.
 * @freqs:            Pointer to array of real frequency values.
 * @volts:            Pointer to array of voltages.
 *
 * If there are 2 clock domains, then top-level and shader cores can operate
 * at different frequency and voltage level. The nominal frequency ("opp-hz")
 * used by devfreq from the devicetree may not be same as the real frequency
 * at which top-level and shader cores are operating, so a translation is
 * needed.
 * Nominal voltage shall always be same as the real voltage for top-level.
 */
static void opp_translate_freq_voltage(struct kbase_device *kbdev,
				       unsigned long nominal_freq,
				       unsigned long nominal_voltage,
				       unsigned long *freqs,
				       unsigned long *volts)
{
	u64 core_mask;

	kbase_devfreq_opp_translate(kbdev, nominal_freq, &core_mask,
				    freqs, volts);
	CSTD_UNUSED(core_mask);

	if (kbdev->nr_clocks == 1) {
		freqs[KBASE_IPA_BLOCK_TYPE_SHADER_CORES] =
			freqs[KBASE_IPA_BLOCK_TYPE_TOP_LEVEL];
		volts[KBASE_IPA_BLOCK_TYPE_SHADER_CORES] =
			volts[KBASE_IPA_BLOCK_TYPE_TOP_LEVEL];
	}
}

#if KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE
#if defined(CONFIG_MALI_PWRSOFT_765) ||                                        \
	KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE
static unsigned long kbase_get_dynamic_power(struct devfreq *df,
					     unsigned long freq,
					     unsigned long voltage)
#else
static unsigned long kbase_get_dynamic_power(unsigned long freq,
					     unsigned long voltage)
#endif
{
	struct kbase_ipa_model *model;
	unsigned long freqs[KBASE_IPA_BLOCK_TYPE_NUM] = {0};
	unsigned long volts[KBASE_IPA_BLOCK_TYPE_NUM] = {0};
	u32 power_coeffs[KBASE_IPA_BLOCK_TYPE_NUM] = {0};
	u32 power = 0;
	int err = 0;
#if defined(CONFIG_MALI_PWRSOFT_765) ||                                        \
	KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE
	struct kbase_device *kbdev = dev_get_drvdata(&df->dev);
#else
	struct kbase_device *kbdev = kbase_find_device(-1);
#endif

	if (!kbdev)
		return 0ul;

	mutex_lock(&kbdev->ipa.lock);

	model = kbdev->ipa.fallback_model;

	err = model->ops->get_dynamic_coeff(model, power_coeffs);

	if (!err) {
		opp_translate_freq_voltage(kbdev, freq, voltage, freqs, volts);

		power = kbase_scale_dynamic_power(
			power_coeffs[KBASE_IPA_BLOCK_TYPE_TOP_LEVEL],
			freqs[KBASE_IPA_BLOCK_TYPE_TOP_LEVEL],
			volts[KBASE_IPA_BLOCK_TYPE_TOP_LEVEL]);

		/* Here unlike kbase_get_real_power(), shader core frequency is
		 * used for the scaling as simple power model is used to obtain
		 * the value of dynamic coefficient (which is is a fixed value
		 * retrieved from the device tree).
		 */
		power += kbase_scale_dynamic_power(
			 power_coeffs[KBASE_IPA_BLOCK_TYPE_SHADER_CORES],
			 freqs[KBASE_IPA_BLOCK_TYPE_SHADER_CORES],
			 volts[KBASE_IPA_BLOCK_TYPE_SHADER_CORES]);
	} else
		dev_err_ratelimited(kbdev->dev,
				    "Model %s returned error code %d\n",
				    model->ops->name, err);

	mutex_unlock(&kbdev->ipa.lock);

#if !(defined(CONFIG_MALI_PWRSOFT_765) ||                                      \
	KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE)
	kbase_release_device(kbdev);
#endif

	return power;
}
#endif /* KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE */

int kbase_get_real_power_locked(struct kbase_device *kbdev, u32 *power,
				unsigned long freq,
				unsigned long voltage)
{
	struct kbase_ipa_model *model;
	unsigned long freqs[KBASE_IPA_BLOCK_TYPE_NUM] = {0};
	unsigned long volts[KBASE_IPA_BLOCK_TYPE_NUM] = {0};
	u32 power_coeffs[KBASE_IPA_BLOCK_TYPE_NUM] = {0};
	struct kbasep_pm_metrics diff;
	u64 total_time;
	bool skip_utilization_scaling = false;
	int err = 0;

	lockdep_assert_held(&kbdev->ipa.lock);

	kbase_pm_get_dvfs_metrics(kbdev, &kbdev->ipa.last_metrics, &diff);

	model = get_current_model(kbdev);

	err = model->ops->get_dynamic_coeff(model, power_coeffs);

	/* If the counter model returns an error (e.g. switching back to
	 * protected mode and failing to read counters, or a counter sample
	 * with too few cycles), revert to the fallback model.
	 */
	if (err && model != kbdev->ipa.fallback_model) {
		/* No meaningful scaling for GPU utilization can be done if
		 * the sampling interval was too long. This is equivalent to
		 * assuming GPU was busy throughout (similar to what is done
		 * during protected mode).
		 */
		if (err == -EOVERFLOW)
			skip_utilization_scaling = true;

		model = kbdev->ipa.fallback_model;
		err = model->ops->get_dynamic_coeff(model, power_coeffs);
	}

	if (WARN_ON(err))
		return err;

	opp_translate_freq_voltage(kbdev, freq, voltage, freqs, volts);

	*power = kbase_scale_dynamic_power(
			power_coeffs[KBASE_IPA_BLOCK_TYPE_TOP_LEVEL],
			freqs[KBASE_IPA_BLOCK_TYPE_TOP_LEVEL],
			volts[KBASE_IPA_BLOCK_TYPE_TOP_LEVEL]);

	if (power_coeffs[KBASE_IPA_BLOCK_TYPE_SHADER_CORES]) {
		unsigned long freq = freqs[KBASE_IPA_BLOCK_TYPE_SHADER_CORES];

		/* As per the HW team, the top-level frequency needs to be used
		 * for the scaling if the counter based model was used as
		 * counter values are normalized with the GPU_ACTIVE counter
		 * value, which increments at the rate of top-level frequency.
		 */
		if (model != kbdev->ipa.fallback_model)
			freq = freqs[KBASE_IPA_BLOCK_TYPE_TOP_LEVEL];

		*power += kbase_scale_dynamic_power(
				power_coeffs[KBASE_IPA_BLOCK_TYPE_SHADER_CORES],
				freq, volts[KBASE_IPA_BLOCK_TYPE_SHADER_CORES]);
	}

	if (!skip_utilization_scaling) {
		/* time_busy / total_time cannot be >1, so assigning the 64-bit
		 * result of div_u64 to *power cannot overflow.
		 */
		total_time = diff.time_busy + (u64) diff.time_idle;
		*power = div_u64(*power * (u64) diff.time_busy,
				 max(total_time, 1ull));
	}

	*power += get_static_power_locked(kbdev, model,
				volts[KBASE_IPA_BLOCK_TYPE_TOP_LEVEL]);

	return err;
}
KBASE_EXPORT_TEST_API(kbase_get_real_power_locked);

int kbase_get_real_power(struct devfreq *df, u32 *power,
				unsigned long freq,
				unsigned long voltage)
{
	int ret;
	struct kbase_device *kbdev = dev_get_drvdata(&df->dev);

	if (!kbdev)
		return -ENODEV;

	mutex_lock(&kbdev->ipa.lock);
	ret = kbase_get_real_power_locked(kbdev, power, freq, voltage);
	mutex_unlock(&kbdev->ipa.lock);

	return ret;
}
KBASE_EXPORT_TEST_API(kbase_get_real_power);

struct devfreq_cooling_power kbase_ipa_power_model_ops = {
#if KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE
	.get_static_power = &kbase_get_static_power,
	.get_dynamic_power = &kbase_get_dynamic_power,
#endif /* KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE */
#if defined(CONFIG_MALI_PWRSOFT_765) ||                                        \
	KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE
	.get_real_power = &kbase_get_real_power,
#endif
};
KBASE_EXPORT_TEST_API(kbase_ipa_power_model_ops);

void kbase_ipa_reset_data(struct kbase_device *kbdev)
{
	ktime_t now, diff;
	s64 elapsed_time;

	mutex_lock(&kbdev->ipa.lock);

	now = ktime_get();
	diff = ktime_sub(now, kbdev->ipa.last_sample_time);
	elapsed_time = ktime_to_ms(diff);

	if (elapsed_time > RESET_INTERVAL_MS) {
		struct kbasep_pm_metrics diff;
		struct kbase_ipa_model *model;

		kbase_pm_get_dvfs_metrics(
			kbdev, &kbdev->ipa.last_metrics, &diff);

		model = get_current_model(kbdev);
		if (model != kbdev->ipa.fallback_model)
			model->ops->reset_counter_data(model);

		kbdev->ipa.last_sample_time = ktime_get();
	}

	mutex_unlock(&kbdev->ipa.lock);
}
