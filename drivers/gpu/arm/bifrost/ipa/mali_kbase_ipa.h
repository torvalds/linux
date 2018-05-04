/*
 *
 * (C) COPYRIGHT 2016-2018 ARM Limited. All rights reserved.
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

#ifndef _KBASE_IPA_H_
#define _KBASE_IPA_H_

#if defined(CONFIG_MALI_BIFROST_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)

struct devfreq;

/**
 * struct kbase_ipa_model - Object describing a particular IPA model.
 * @kbdev:                    pointer to kbase device
 * @model_data:               opaque pointer to model specific data, accessed
 *                            only by model specific methods.
 * @ops:                      pointer to object containing model specific methods.
 * @params:                   head of the list of debugfs params added for model
 * @missing_dt_node_warning:  flag to limit the matching power model DT not found
 *                            warning to once.
 */
struct kbase_ipa_model {
	struct kbase_device *kbdev;
	void *model_data;
	struct kbase_ipa_model_ops *ops;
	struct list_head params;
	bool missing_dt_node_warning;
};

/**
 * kbase_ipa_model_add_param_s32 - Add an integer model parameter
 * @model:	pointer to IPA model
 * @name:	name of corresponding debugfs entry
 * @addr:	address where the value is stored
 * @num_elems:	number of elements (1 if not an array)
 * @dt_required: if false, a corresponding devicetree entry is not required,
 *		 and the current value will be used. If true, a warning is
 *		 output and the data is zeroed
 *
 * Return: 0 on success, or an error code
 */
int kbase_ipa_model_add_param_s32(struct kbase_ipa_model *model,
				  const char *name, s32 *addr,
				  size_t num_elems, bool dt_required);

/**
 * kbase_ipa_model_add_param_string - Add a string model parameter
 * @model:	pointer to IPA model
 * @name:	name of corresponding debugfs entry
 * @addr:	address where the value is stored
 * @size:	size, in bytes, of the value storage (so the maximum string
 *		length is size - 1)
 * @dt_required: if false, a corresponding devicetree entry is not required,
 *		 and the current value will be used. If true, a warning is
 *		 output and the data is zeroed
 *
 * Return: 0 on success, or an error code
 */
int kbase_ipa_model_add_param_string(struct kbase_ipa_model *model,
				     const char *name, char *addr,
				     size_t size, bool dt_required);

struct kbase_ipa_model_ops {
	char *name;
	/* The init, recalculate and term ops on the default model are always
	 * called.  However, all the other models are only invoked if the model
	 * is selected in the device tree. Otherwise they are never
	 * initialized. Additional resources can be acquired by models in
	 * init(), however they must be terminated in the term().
	 */
	int (*init)(struct kbase_ipa_model *model);
	/* Called immediately after init(), or when a parameter is changed, so
	 * that any coefficients derived from model parameters can be
	 * recalculated. */
	int (*recalculate)(struct kbase_ipa_model *model);
	void (*term)(struct kbase_ipa_model *model);
	/*
	 * get_dynamic_coeff() - calculate dynamic power coefficient
	 * @model:		pointer to model
	 * @coeffp:		pointer to return value location
	 *
	 * Calculate a dynamic power coefficient, with units pW/(Hz V^2), which
	 * is then scaled by the IPA framework according to the current OPP's
	 * frequency and voltage.
	 *
	 * Return: 0 on success, or an error code.
	 */
	int (*get_dynamic_coeff)(struct kbase_ipa_model *model, u32 *coeffp);
	/*
	 * get_static_coeff() - calculate static power coefficient
	 * @model:		pointer to model
	 * @coeffp:		pointer to return value location
	 *
	 * Calculate a static power coefficient, with units uW/(V^3), which is
	 * scaled by the IPA framework according to the current OPP's voltage.
	 *
	 * Return: 0 on success, or an error code.
	 */
	int (*get_static_coeff)(struct kbase_ipa_model *model, u32 *coeffp);
};

/**
 * kbase_ipa_init - Initialize the IPA feature
 * @kbdev:      pointer to kbase device
 *
 * simple IPA power model is initialized as a fallback model and if that
 * initialization fails then IPA is not used.
 * The device tree is read for the name of ipa model to be used, by using the
 * property string "ipa-model". If that ipa model is supported then it is
 * initialized but if the initialization fails then simple power model is used.
 *
 * Return: 0 on success, negative -errno on error
 */
int kbase_ipa_init(struct kbase_device *kbdev);

/**
 * kbase_ipa_term - Terminate the IPA feature
 * @kbdev:      pointer to kbase device
 *
 * Both simple IPA power model and model retrieved from device tree are
 * terminated.
 */
void kbase_ipa_term(struct kbase_device *kbdev);

/**
 * kbase_ipa_model_recalculate - Recalculate the model coefficients
 * @model:      pointer to the IPA model object, already initialized
 *
 * It shall be called immediately after the model has been initialized
 * or when the model parameter has changed, so that any coefficients
 * derived from parameters can be recalculated.
 * Its a wrapper for the module specific recalculate() method.
 *
 * Return: 0 on success, negative -errno on error
 */
int kbase_ipa_model_recalculate(struct kbase_ipa_model *model);

/**
 * kbase_ipa_init_model - Initilaize the particular IPA model
 * @kbdev:      pointer to the IPA model object, already initialized
 * @ops:        pointer to object containing model specific methods.
 *
 * Initialize the model corresponding to the @ops pointer passed.
 * The init() method specified in @ops would be called.
 *
 * Return: pointer to kbase_ipa_model on success, NULL on error
 */
struct kbase_ipa_model *kbase_ipa_init_model(struct kbase_device *kbdev,
					     struct kbase_ipa_model_ops *ops);
/**
 * kbase_ipa_term_model - Terminate the particular IPA model
 * @model:      pointer to the IPA model object, already initialized
 *
 * Terminate the model, using the term() method.
 * Module specific parameters would be freed.
 */
void kbase_ipa_term_model(struct kbase_ipa_model *model);

/* Switch to the fallback model */
void kbase_ipa_model_use_fallback_locked(struct kbase_device *kbdev);

/* Switch to the model retrieved from device tree */
void kbase_ipa_model_use_configured_locked(struct kbase_device *kbdev);

extern struct kbase_ipa_model_ops kbase_g71_ipa_model_ops;
extern struct kbase_ipa_model_ops kbase_g72_ipa_model_ops;
extern struct kbase_ipa_model_ops kbase_tnox_ipa_model_ops;

#if MALI_UNIT_TEST
/**
 * kbase_get_real_power() - get the real power consumption of the GPU
 * @df: dynamic voltage and frequency scaling information for the GPU.
 * @power: where to store the power consumption, in mW.
 * @freq: a frequency, in HZ.
 * @voltage: a voltage, in mV.
 *
 * This function is only exposed for use by unit tests. The returned value
 * incorporates both static and dynamic power consumption.
 *
 * Return: 0 on success, or an error code.
 */
int kbase_get_real_power(struct devfreq *df, u32 *power,
				unsigned long freq,
				unsigned long voltage);

/* Called by kbase_get_real_power() to invoke the power models.
 * Must be called with kbdev->ipa.lock held.
 */
int kbase_get_real_power_locked(struct kbase_device *kbdev, u32 *power,
				unsigned long freq,
				unsigned long voltage);
#endif /* MALI_UNIT_TEST */

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
extern struct devfreq_cooling_ops kbase_ipa_power_model_ops;
#else
extern struct devfreq_cooling_power kbase_ipa_power_model_ops;
#endif

#else /* !(defined(CONFIG_MALI_BIFROST_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)) */

static inline void kbase_ipa_model_use_fallback_locked(struct kbase_device *kbdev)
{ }

static inline void kbase_ipa_model_use_configured_locked(struct kbase_device *kbdev)
{ }

#endif /* (defined(CONFIG_MALI_BIFROST_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)) */

#endif
