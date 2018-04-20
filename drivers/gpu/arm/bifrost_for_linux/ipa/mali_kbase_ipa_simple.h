/*
 *
 * (C) COPYRIGHT 2017 ARM Limited. All rights reserved.
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



#ifndef _KBASE_IPA_SIMPLE_H_
#define _KBASE_IPA_SIMPLE_H_

#if defined(CONFIG_MALI_BIFROST_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)

extern struct kbase_ipa_model_ops kbase_simple_ipa_model_ops;

#if MALI_UNIT_TEST
/**
 * kbase_simple_power_model_set_dummy_temp() - set a dummy temperature value
 * @temp: Temperature of the thermal zone, in millidegrees celsius.
 *
 * This is only intended for use in unit tests, to ensure that the temperature
 * values used by the simple power model are predictable. Deterministic
 * behavior is necessary to allow validation of the static power values
 * computed by this model.
 */
void kbase_simple_power_model_set_dummy_temp(int temp);
#endif /* MALI_UNIT_TEST */

#endif /* (defined(CONFIG_MALI_BIFROST_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)) */

#endif /* _KBASE_IPA_SIMPLE_H_ */
