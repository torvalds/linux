/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2017, 2020-2021 ARM Limited. All rights reserved.
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
