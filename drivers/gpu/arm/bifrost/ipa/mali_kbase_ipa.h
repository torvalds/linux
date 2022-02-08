/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
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

#ifndef _KBASE_IPA_H_
#define _KBASE_IPA_H_

#if defined(CONFIG_MALI_BIFROST_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)

struct devfreq;

/**
 * enum kbase_ipa_block_type - Type of block for which power estimation is done.
 *
 * @KBASE_IPA_BLOCK_TYPE_USING_CLK_MALI:
 *				       Blocks using clk_mali in dts.
 * @KBASE_IPA_BLOCK_TYPE_TOP_LEVEL:    Top-level block, that covers CSHW,
 *                                     MEMSYS, Tiler.
 * @KBASE_IPA_BLOCK_TYPE_SHADER_CORES: All Shader cores.
 * @KBASE_IPA_BLOCK_TYPE_FOR_CLK_GPU:  Dummy for clk_gpu in dts.
 * @KBASE_IPA_BLOCK_TYPE_NUM:          Number of blocks.
 */
enum kbase_ipa_block_type {
	KBASE_IPA_BLOCK_TYPE_USING_CLK_MALI,
	KBASE_IPA_BLOCK_TYPE_TOP_LEVEL,
	KBASE_IPA_BLOCK_TYPE_SHADER_CORES,
	KBASE_IPA_BLOCK_TYPE_FOR_CLK_GPU,
	KBASE_IPA_BLOCK_TYPE_NUM
};

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
	const struct kbase_ipa_model_ops *ops;
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
	 * recalculated
	 */
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
	 * Return: 0 on success, or an error code. -EOVERFLOW error code will
	 * indicate that sampling interval was too large and no meaningful
	 * scaling for GPU utiliation can be done.
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

	/*
	 * reset_counter_data() - Reset the HW counter data used for calculating
	 *                        dynamic power coefficient
	 * @model:		  pointer to model
	 *
	 * This method is currently applicable only to the counter based model.
	 * The next call to get_dynamic_coeff() will have to calculate the
	 * dynamic power coefficient based on the HW counter data generated
	 * from this point onwards.
	 */
	void (*reset_counter_data)(struct kbase_ipa_model *model);
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
 * kbase_ipa_model_ops_find - Lookup an IPA model using its name
 * @kbdev:      pointer to kbase device
 * @name:       name of model to lookup
 *
 * Return: Pointer to model's 'ops' structure, or NULL if the lookup failed.
 */
const struct kbase_ipa_model_ops *kbase_ipa_model_ops_find(struct kbase_device *kbdev,
							   const char *name);

/**
 * kbase_ipa_counter_model_ops_find - Lookup an IPA counter model using its name
 * @kbdev:      pointer to kbase device
 * @name:       name of counter model to lookup
 *
 * Return: Pointer to counter model's 'ops' structure, or NULL if the lookup
 *         failed.
 */
const struct kbase_ipa_model_ops *kbase_ipa_counter_model_ops_find(
	struct kbase_device *kbdev, const char *name);

/**
 * kbase_ipa_model_name_from_id - Find the best model for a given GPU ID
 * @gpu_id:     GPU ID of GPU the model will be used for
 *
 * Return: The name of the appropriate counter-based model, or the name of the
 *         fallback model if no counter model exists.
 */
const char *kbase_ipa_model_name_from_id(u32 gpu_id);

/**
 * kbase_ipa_counter_model_name_from_id - Find the best counter model for a
 *                                        given GPU ID
 * @gpu_id:     GPU ID of GPU the counter model will be used for
 *
 * Return: The name of the appropriate counter-based model, or NULL if the
 *         no counter model exists.
 */
const char *kbase_ipa_counter_model_name_from_id(u32 gpu_id);

/**
 * kbase_ipa_init_model - Initilaize the particular IPA model
 * @kbdev:      pointer to kbase device
 * @ops:        pointer to object containing model specific methods.
 *
 * Initialize the model corresponding to the @ops pointer passed.
 * The init() method specified in @ops would be called.
 *
 * Return: pointer to kbase_ipa_model on success, NULL on error
 */
struct kbase_ipa_model *kbase_ipa_init_model(struct kbase_device *kbdev,
					const struct kbase_ipa_model_ops *ops);
/**
 * kbase_ipa_term_model - Terminate the particular IPA model
 * @model:      pointer to the IPA model object, already initialized
 *
 * Terminate the model, using the term() method.
 * Module specific parameters would be freed.
 */
void kbase_ipa_term_model(struct kbase_ipa_model *model);

/**
 * kbase_ipa_protection_mode_switch_event - Inform IPA of the GPU's entry into
 *                                          protected mode
 * @kbdev:      pointer to kbase device
 *
 * Makes IPA aware of the GPU switching to protected mode.
 */
void kbase_ipa_protection_mode_switch_event(struct kbase_device *kbdev);

/**
 * kbase_get_real_power() - get the real power consumption of the GPU
 * @df: dynamic voltage and frequency scaling information for the GPU.
 * @power: where to store the power consumption, in mW.
 * @freq: a frequency, in HZ.
 * @voltage: a voltage, in mV.
 *
 * The returned value incorporates both static and dynamic power consumption.
 *
 * Return: 0 on success, or an error code.
 */
int kbase_get_real_power(struct devfreq *df, u32 *power,
				unsigned long freq,
				unsigned long voltage);

#if MALI_UNIT_TEST
/* Called by kbase_get_real_power() to invoke the power models.
 * Must be called with kbdev->ipa.lock held.
 * This function is only exposed for use by unit tests.
 */
int kbase_get_real_power_locked(struct kbase_device *kbdev, u32 *power,
				unsigned long freq,
				unsigned long voltage);
#endif /* MALI_UNIT_TEST */

extern struct devfreq_cooling_power kbase_ipa_power_model_ops;

/**
 * kbase_ipa_reset_data() - Reset the data required for power estimation.
 * @kbdev:  Pointer to kbase device.
 *
 * This function is called to ensure a meaningful baseline for
 * kbase_get_real_power(), when thermal governor starts the polling, and
 * that is achieved by updating the GPU utilization metrics and retrieving
 * the accumulated value of HW counters.
 * Basically this function collects all the data required for power estimation
 * but does not process it.
 */
void kbase_ipa_reset_data(struct kbase_device *kbdev);

#else /* !(defined(CONFIG_MALI_BIFROST_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)) */

static inline void kbase_ipa_protection_mode_switch_event(struct kbase_device *kbdev)
{ }

#endif /* (defined(CONFIG_MALI_BIFROST_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)) */

#endif
