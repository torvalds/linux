/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2020-2022 ARM Limited. All rights reserved.
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

#ifndef _KBASE_CSF_IPA_CONTROL_H_
#define _KBASE_CSF_IPA_CONTROL_H_

#include <mali_kbase.h>

/*
 * Maximum index accepted to configure an IPA Control performance counter.
 */
#define KBASE_IPA_CONTROL_CNT_MAX_IDX ((u8)64 * 3)

/**
 * struct kbase_ipa_control_perf_counter - Performance counter description
 *
 * @scaling_factor: Scaling factor by which the counter's value shall be
 *                  multiplied. A scaling factor of 1 corresponds to units
 *                  of 1 second if values are normalised by GPU frequency.
 * @gpu_norm:       Indicating whether counter values shall be normalized by
 *                  GPU frequency. If true, returned values represent
 *                  an interval of time expressed in seconds (when the scaling
 *                  factor is set to 1).
 * @type:           Type of counter block for performance counter.
 * @idx:            Index of the performance counter inside the block.
 *                  It may be dependent on GPU architecture.
 *                  It cannot be greater than KBASE_IPA_CONTROL_CNT_MAX_IDX.
 *
 * This structure is used by clients of the IPA Control component to describe
 * a performance counter that they intend to read. The counter is identified
 * by block and index. In addition to that, the client also specifies how
 * values shall be represented. Raw values are a number of GPU cycles;
 * if normalized, they are divided by GPU frequency and become an interval
 * of time expressed in seconds, since the GPU frequency is given in Hz.
 * The client may specify a scaling factor to multiply counter values before
 * they are divided by frequency, in case the unit of time of 1 second is
 * too low in resolution. For instance: a scaling factor of 1000 implies
 * that the returned value is a time expressed in milliseconds; a scaling
 * factor of 1000 * 1000 implies that the returned value is a time expressed
 * in microseconds.
 */
struct kbase_ipa_control_perf_counter {
	u64 scaling_factor;
	bool gpu_norm;
	enum kbase_ipa_core_type type;
	u8 idx;
};

/**
 * kbase_ipa_control_init - Initialize the IPA Control component
 *
 * @kbdev: Pointer to Kbase device.
 */
void kbase_ipa_control_init(struct kbase_device *kbdev);

/**
 * kbase_ipa_control_term - Terminate the IPA Control component
 *
 * @kbdev: Pointer to Kbase device.
 */
void kbase_ipa_control_term(struct kbase_device *kbdev);

/**
 * kbase_ipa_control_register - Register a client to the IPA Control component
 *
 * @kbdev:         Pointer to Kbase device.
 * @perf_counters: Array of performance counters the client intends to read.
 *                 For each counter the client specifies block, index,
 *                 scaling factor and whether it must be normalized by GPU
 *                 frequency.
 * @num_counters:  Number of performance counters. It cannot exceed the total
 *                 number of counters that exist on the IPA Control interface.
 * @client:        Handle to an opaque structure set by IPA Control if
 *                 the registration is successful. This handle identifies
 *                 a client's session and shall be provided in its future
 *                 queries.
 *
 * A client needs to subscribe to the IPA Control component by declaring which
 * performance counters it intends to read, and specifying a scaling factor
 * and whether normalization is requested for each performance counter.
 * The function shall configure the IPA Control interface accordingly and start
 * a session for the client that made the request. A unique handle is returned
 * if registration is successful in order to identify the client's session
 * and be used for future queries.
 *
 * Return: 0 on success, negative -errno on error
 */
int kbase_ipa_control_register(
	struct kbase_device *kbdev,
	const struct kbase_ipa_control_perf_counter *perf_counters,
	size_t num_counters, void **client);

/**
 * kbase_ipa_control_unregister - Unregister a client from IPA Control
 *
 * @kbdev:  Pointer to kbase device.
 * @client: Handle to an opaque structure that identifies the client session
 *          to terminate, as returned by kbase_ipa_control_register.
 *
 * Return: 0 on success, negative -errno on error
 */
int kbase_ipa_control_unregister(struct kbase_device *kbdev,
				 const void *client);

/**
 * kbase_ipa_control_query - Query performance counters
 *
 * @kbdev:          Pointer to kbase device.
 * @client:         Handle to an opaque structure that identifies the client
 *                  session, as returned by kbase_ipa_control_register.
 * @values:         Array of values queried from performance counters, whose
 *                  length depends on the number of counters requested at
 *                  the time of registration. Values are scaled and normalized
 *                  and represent the difference since the last query.
 * @num_values:     Number of entries in the array of values that has been
 *                  passed by the caller. It must be at least equal to the
 *                  number of performance counters the client registered itself
 *                  to read.
 * @protected_time: Time spent in protected mode since last query,
 *                  expressed in nanoseconds. This pointer may be NULL if the
 *                  client doesn't want to know about this.
 *
 * A client that has already opened a session by registering itself to read
 * some performance counters may use this function to query the values of
 * those counters. The values returned are normalized by GPU frequency if
 * requested and then multiplied by the scaling factor provided at the time
 * of registration. Values always represent a difference since the last query.
 *
 * Performance counters are not updated while the GPU operates in protected
 * mode. For this reason, returned values may be unreliable if the GPU has
 * been in protected mode since the last query. The function returns success
 * in that case, but it also gives a measure of how much time has been spent
 * in protected mode.
 *
 * Return: 0 on success, negative -errno on error
 */
int kbase_ipa_control_query(struct kbase_device *kbdev, const void *client,
			    u64 *values, size_t num_values,
			    u64 *protected_time);

/**
 * kbase_ipa_control_handle_gpu_power_on - Handle the GPU power on event
 *
 * @kbdev:          Pointer to kbase device.
 *
 * This function is called after GPU has been powered and is ready for use.
 * After the GPU power on, IPA Control component needs to ensure that the
 * counters start incrementing again.
 */
void kbase_ipa_control_handle_gpu_power_on(struct kbase_device *kbdev);

/**
 * kbase_ipa_control_handle_gpu_power_off - Handle the GPU power off event
 *
 * @kbdev:          Pointer to kbase device.
 *
 * This function is called just before the GPU is powered off when it is still
 * ready for use.
 * IPA Control component needs to be aware of the GPU power off so that it can
 * handle the query from Clients appropriately and return meaningful values
 * to them.
 */
void kbase_ipa_control_handle_gpu_power_off(struct kbase_device *kbdev);

/**
 * kbase_ipa_control_handle_gpu_reset_pre - Handle the pre GPU reset event
 *
 * @kbdev:          Pointer to kbase device.
 *
 * This function is called when the GPU is about to be reset.
 */
void kbase_ipa_control_handle_gpu_reset_pre(struct kbase_device *kbdev);

/**
 * kbase_ipa_control_handle_gpu_reset_post - Handle the post GPU reset event
 *
 * @kbdev:          Pointer to kbase device.
 *
 * This function is called after the GPU has been reset.
 */
void kbase_ipa_control_handle_gpu_reset_post(struct kbase_device *kbdev);

#ifdef KBASE_PM_RUNTIME
/**
 * kbase_ipa_control_handle_gpu_sleep_enter - Handle the pre GPU Sleep event
 *
 * @kbdev:          Pointer to kbase device.
 *
 * This function is called after MCU has been put to sleep state & L2 cache has
 * been powered down. The top level part of GPU is still powered up when this
 * function is called.
 */
void kbase_ipa_control_handle_gpu_sleep_enter(struct kbase_device *kbdev);

/**
 * kbase_ipa_control_handle_gpu_sleep_exit - Handle the post GPU Sleep event
 *
 * @kbdev:          Pointer to kbase device.
 *
 * This function is called when L2 needs to be powered up and MCU can exit the
 * sleep state. The top level part of GPU is powered up when this function is
 * called.
 *
 * This function must be called only if kbase_ipa_control_handle_gpu_sleep_enter()
 * was called previously.
 */
void kbase_ipa_control_handle_gpu_sleep_exit(struct kbase_device *kbdev);
#endif

#if MALI_UNIT_TEST
/**
 * kbase_ipa_control_rate_change_notify_test - Notify GPU rate change
 *                                             (only for testing)
 *
 * @kbdev:       Pointer to kbase device.
 * @clk_index:   Index of the clock for which the change has occurred.
 * @clk_rate_hz: Clock frequency(Hz).
 *
 * Notify the IPA Control component about a GPU rate change.
 */
void kbase_ipa_control_rate_change_notify_test(struct kbase_device *kbdev,
					       u32 clk_index, u32 clk_rate_hz);
#endif /* MALI_UNIT_TEST */

/**
 * kbase_ipa_control_protm_entered - Tell IPA_CONTROL that protected mode
 * has been entered.
 *
 * @kbdev:		Pointer to kbase device.
 *
 * This function provides a means through which IPA_CONTROL can be informed
 * that the GPU has entered protected mode. Since the GPU cannot access
 * performance counters while in this mode, this information is useful as
 * it implies (a) the values of these registers cannot change, so theres no
 * point trying to read them, and (b) IPA_CONTROL has a means through which
 * to record the duration of time the GPU is in protected mode, which can
 * then be forwarded on to clients, who may wish, for example, to assume
 * that the GPU was busy 100% of the time while in this mode.
 */
void kbase_ipa_control_protm_entered(struct kbase_device *kbdev);

/**
 * kbase_ipa_control_protm_exited - Tell IPA_CONTROL that protected mode
 * has been exited.
 *
 * @kbdev:		Pointer to kbase device
 *
 * This function provides a means through which IPA_CONTROL can be informed
 * that the GPU has exited from protected mode.
 */
void kbase_ipa_control_protm_exited(struct kbase_device *kbdev);

#endif /* _KBASE_CSF_IPA_CONTROL_H_ */
