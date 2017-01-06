/*
 *
 * (C) COPYRIGHT 2016-2017 ARM Limited. All rights reserved.
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


#include <linux/thermal.h>
#include <linux/devfreq_cooling.h>
#include <linux/of.h>
#include "mali_kbase.h"
#include "mali_kbase_ipa.h"
#include "mali_kbase_ipa_debugfs.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
#include <linux/pm_opp.h>
#else
#include <linux/opp.h>
#define dev_pm_opp_find_freq_exact opp_find_freq_exact
#define dev_pm_opp_get_voltage opp_get_voltage
#define dev_pm_opp opp
#endif
#include <linux/math64.h>

#define KBASE_IPA_FALLBACK_MODEL_NAME "mali-simple-power-model"

static struct kbase_ipa_model_ops *kbase_ipa_all_model_ops[] = {
	&kbase_simple_ipa_model_ops,
};

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

static struct kbase_ipa_model_ops *kbase_ipa_model_ops_find(struct kbase_device *kbdev,
							    const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(kbase_ipa_all_model_ops); ++i) {
		struct kbase_ipa_model_ops *ops = kbase_ipa_all_model_ops[i];

		if (!strcmp(ops->name, name))
			return ops;
	}

	dev_err(kbdev->dev, "power model \'%s\' not found\n", name);

	return NULL;
}

void kbase_ipa_model_use_fallback_locked(struct kbase_device *kbdev)
{
	atomic_set(&kbdev->ipa_use_configured_model, false);
}

void kbase_ipa_model_use_configured_locked(struct kbase_device *kbdev)
{
	atomic_set(&kbdev->ipa_use_configured_model, true);
}

const char *kbase_ipa_model_name_from_id(u32 gpu_id)
{
	const u32 prod_id = (gpu_id & GPU_ID_VERSION_PRODUCT_ID) >>
			GPU_ID_VERSION_PRODUCT_ID_SHIFT;

	if (GPU_ID_IS_NEW_FORMAT(prod_id)) {
		switch (GPU_ID2_MODEL_MATCH_VALUE(prod_id)) {
		case GPU_ID2_PRODUCT_TMIX:
			return KBASE_IPA_FALLBACK_MODEL_NAME;
		default:
			return KBASE_IPA_FALLBACK_MODEL_NAME;
		}
	}

	return KBASE_IPA_FALLBACK_MODEL_NAME;
}

static struct device_node *get_model_dt_node(struct kbase_ipa_model *model)
{
	struct device_node *model_dt_node;
	char compat_string[64];

	snprintf(compat_string, sizeof(compat_string), "arm,%s",
		 model->ops->name);

	model_dt_node = of_find_compatible_node(model->kbdev->dev->of_node,
						NULL, compat_string);
	if (!model_dt_node && !model->missing_dt_node_warning) {
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
	struct device_node *model_dt_node = get_model_dt_node(model);
	char *origin;

	err = of_property_read_u32_array(model_dt_node, name, addr, num_elems);

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
	struct device_node *model_dt_node = get_model_dt_node(model);
	const char *string_prop_value;
	char *origin;

	err = of_property_read_string(model_dt_node, name,
				      &string_prop_value);
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
					     struct kbase_ipa_model_ops *ops)
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
		goto term_model;
	}

	err = kbase_ipa_model_recalculate(model);
	if (err)
		goto term_model;

	return model;

term_model:
	kbase_ipa_term_model(model);
	return NULL;
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
	struct kbase_ipa_model_ops *ops;
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

	if (!ops->do_utilization_scaling_in_framework) {
		dev_err(kbdev->dev,
			"Fallback IPA model %s should not account for utilization\n",
			ops->name);
		err = -EINVAL;
		goto end;
	}

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
	} else {
		dev_dbg(kbdev->dev,
			"Using ipa-model parameter from DT: \'%s\'\n",
			model_name);
	}

	if (strcmp(KBASE_IPA_FALLBACK_MODEL_NAME, model_name) != 0) {
		ops = kbase_ipa_model_ops_find(kbdev, model_name);
		kbdev->ipa.configured_model = kbase_ipa_init_model(kbdev, ops);
		if (!kbdev->ipa.configured_model) {
			err = -EINVAL;
			goto end;
		}
	} else {
		kbdev->ipa.configured_model = default_model;
		err = 0;
	}

	kbase_ipa_model_use_configured_locked(kbdev);

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
	 * Must be < 2^42 to avoid overflowing the return value. */
	const u64 v2fc = (u64) c * (u64) v2f;
	u32 remainder;

	/* Range: 0 < v2fc / 1000 < 2^13 mW */
	// static inline u64 div_u64_rem(u64 dividend, u32 divisor, u32 *remainder)
	return div_u64_rem(v2fc, 1000, &remainder);
}

/**
 * kbase_scale_static_power() - Scale a static power coefficient to an OPP
 * @c:		Static model coefficient, in uW/V^3. Should be in range
 *		0 < c < 2^32 to prevent overflow.
 * @voltage:	Voltage, in mV. Range: 2^9 < voltage < 2^13 (~0.5V to ~8V)
 *
 * Return: Power consumption, in mW. Range: 0 < p < 2^13 (0W to ~8W)
 */
u32 kbase_scale_static_power(const u32 c, const u32 voltage)
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
	u32 remainder;

	/* Range: 0 < v3c_big / 1000000 < 2^13 mW */
	// return v3c_big / 1000000;
	return div_u64_rem(v3c_big, 1000000, &remainder);
}

static struct kbase_ipa_model *get_current_model(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->ipa.lock);

	if (atomic_read(&kbdev->ipa_use_configured_model))
		return kbdev->ipa.configured_model;
	else
		return kbdev->ipa.fallback_model;
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

#ifdef CONFIG_MALI_PWRSOFT_765
static unsigned long kbase_get_static_power(struct devfreq *df,
					    unsigned long voltage)
#else
static unsigned long kbase_get_static_power(unsigned long voltage)
#endif
{
	struct kbase_ipa_model *model;
	u32 power = 0;
#ifdef CONFIG_MALI_PWRSOFT_765
	struct kbase_device *kbdev = dev_get_drvdata(&df->dev);
#else
	struct kbase_device *kbdev = kbase_find_device(-1);
#endif

	mutex_lock(&kbdev->ipa.lock);

	model = get_current_model(kbdev);
	power = get_static_power_locked(kbdev, model, voltage);

	mutex_unlock(&kbdev->ipa.lock);

#ifndef CONFIG_MALI_PWRSOFT_765
	kbase_release_device(kbdev);
#endif

	return power;
}

#ifdef CONFIG_MALI_PWRSOFT_765
static unsigned long kbase_get_dynamic_power(struct devfreq *df,
					     unsigned long freq,
					     unsigned long voltage)
#else
static unsigned long kbase_get_dynamic_power(unsigned long freq,
					     unsigned long voltage)
#endif
{
	struct kbase_ipa_model *model;
	u32 power_coeff = 0, power = 0;
	int err = 0;
#ifdef CONFIG_MALI_PWRSOFT_765
	struct kbase_device *kbdev = dev_get_drvdata(&df->dev);
#else
	struct kbase_device *kbdev = kbase_find_device(-1);
#endif

	mutex_lock(&kbdev->ipa.lock);

	model = kbdev->ipa.fallback_model;

	err = model->ops->get_dynamic_coeff(model, &power_coeff, freq);

	if (!err)
		power = kbase_scale_dynamic_power(power_coeff, freq, voltage);
	else
		dev_err_ratelimited(kbdev->dev,
				    "Model %s returned error code %d\n",
				    model->ops->name, err);

	mutex_unlock(&kbdev->ipa.lock);

#ifndef CONFIG_MALI_PWRSOFT_765
	kbase_release_device(kbdev);
#endif

	return power;
}

int kbase_get_real_power(struct devfreq *df, u32 *power,
				unsigned long freq,
				unsigned long voltage)
{
	struct kbase_ipa_model *model;
	u32 power_coeff = 0;
	int err = 0;
	struct kbase_device *kbdev = dev_get_drvdata(&df->dev);

	mutex_lock(&kbdev->ipa.lock);

	model = get_current_model(kbdev);

	err = model->ops->get_dynamic_coeff(model, &power_coeff, freq);

	/* If we switch to protected model between get_current_model() and
	 * get_dynamic_coeff(), counter reading could fail. If that happens
	 * (unlikely, but possible), revert to the fallback model. */
	if (err && model != kbdev->ipa.fallback_model) {
		model = kbdev->ipa.fallback_model;
		err = model->ops->get_dynamic_coeff(model, &power_coeff, freq);
	}

	if (err)
		goto exit_unlock;

	*power = kbase_scale_dynamic_power(power_coeff, freq, voltage);

	if (model->ops->do_utilization_scaling_in_framework) {
		struct devfreq_dev_status *status = &df->last_status;
		unsigned long total_time = max(status->total_time, 1ul);
		u64 busy_time = min(status->busy_time, total_time);
		u32 remainder;

		// *power = ((u64) *power * (u64) busy_time) / total_time;
		*power = div_u64_rem(((u64) *power * (u64) busy_time), total_time, &remainder);
	}

	*power += get_static_power_locked(kbdev, model, voltage);

exit_unlock:
	mutex_unlock(&kbdev->ipa.lock);

	return err;
}
KBASE_EXPORT_TEST_API(kbase_get_real_power);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
struct devfreq_cooling_ops kbase_ipa_power_model_ops = {
#else
struct devfreq_cooling_power kbase_ipa_power_model_ops = {
#endif
	.get_static_power = &kbase_get_static_power,
	.get_dynamic_power = &kbase_get_dynamic_power,
};
KBASE_EXPORT_TEST_API(kbase_ipa_power_model_ops);
