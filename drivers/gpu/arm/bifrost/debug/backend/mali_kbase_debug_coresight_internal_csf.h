/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
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

#ifndef _KBASE_DEBUG_CORESIGHT_INTERNAL_CSF_H_
#define _KBASE_DEBUG_CORESIGHT_INTERNAL_CSF_H_

#include <mali_kbase.h>
#include <linux/mali_kbase_debug_coresight_csf.h>

/**
 * struct kbase_debug_coresight_csf_client - Coresight client definition
 *
 * @drv_data:    Pointer to driver device data.
 * @addr_ranges: Arrays of address ranges used by the registered client.
 * @nr_ranges:   Size of @addr_ranges array.
 * @link:        Link item of a Coresight client.
 *               Linked to &struct_kbase_device.csf.coresight.clients.
 */
struct kbase_debug_coresight_csf_client {
	void *drv_data;
	struct kbase_debug_coresight_csf_address_range *addr_ranges;
	u32 nr_ranges;
	struct list_head link;
};

/**
 * enum kbase_debug_coresight_csf_state - Coresight configuration states
 *
 * @KBASE_DEBUG_CORESIGHT_CSF_DISABLED: Coresight configuration is disabled.
 * @KBASE_DEBUG_CORESIGHT_CSF_ENABLED:  Coresight configuration is enabled.
 */
enum kbase_debug_coresight_csf_state {
	KBASE_DEBUG_CORESIGHT_CSF_DISABLED = 0,
	KBASE_DEBUG_CORESIGHT_CSF_ENABLED,
};

/**
 * struct kbase_debug_coresight_csf_config - Coresight configuration definition
 *
 * @client:      Pointer to the client for which the configuration is created.
 * @enable_seq:  Array of operations for Coresight client enable sequence. Can be NULL.
 * @disable_seq: Array of operations for Coresight client disable sequence. Can be NULL.
 * @state:       Current Coresight configuration state.
 * @error:       Error code used to know if an error occurred during the execution
 *               of the enable or disable sequences.
 * @link:        Link item of a Coresight configuration.
 *               Linked to &struct_kbase_device.csf.coresight.configs.
 */
struct kbase_debug_coresight_csf_config {
	void *client;
	struct kbase_debug_coresight_csf_sequence *enable_seq;
	struct kbase_debug_coresight_csf_sequence *disable_seq;
	enum kbase_debug_coresight_csf_state state;
	int error;
	struct list_head link;
};

/**
 * struct kbase_debug_coresight_device - Object representing the Coresight device
 *
 * @clients: List head to maintain Coresight clients.
 * @configs: List head to maintain Coresight configs.
 * @lock: A lock to protect client/config lists.
 *                  Lists can be accessed concurrently by
 *                  Coresight kernel modules and kernel threads.
 * @workq: Work queue for Coresight enable/disable execution.
 * @enable_work: Work item used to enable Coresight.
 * @disable_work: Work item used to disable Coresight.
 * @event_wait: Wait queue for Coresight events.
 * @enable_on_pmode_exit: Flag used by the PM state machine to
 *                        identify if Coresight enable is needed.
 * @disable_on_pmode_enter: Flag used by the PM state machine to
 *                         identify if Coresight disable is needed.
 */
struct kbase_debug_coresight_device {
	struct list_head clients;
	struct list_head configs;
	spinlock_t lock;
	struct workqueue_struct *workq;
	struct work_struct enable_work;
	struct work_struct disable_work;
	wait_queue_head_t event_wait;
	bool enable_on_pmode_exit;
	bool disable_on_pmode_enter;
};

/**
 * kbase_debug_coresight_csf_init - Initialize Coresight resources.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function should be called once at device initialization.
 *
 * Return: 0 on success.
 */
int kbase_debug_coresight_csf_init(struct kbase_device *kbdev);

/**
 * kbase_debug_coresight_csf_term - Terminate Coresight resources.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function should be called at device termination to prevent any
 * memory leaks if Coresight module would have been removed without calling
 * kbasep_debug_coresight_csf_trace_disable().
 */
void kbase_debug_coresight_csf_term(struct kbase_device *kbdev);

/**
 * kbase_debug_coresight_csf_disable_pmode_enter - Disable Coresight on Protected
 *                                                 mode enter.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function should be called just before requesting to enter protected mode.
 * It will trigger a PM state machine transition from MCU_ON
 * to ON_PMODE_ENTER_CORESIGHT_DISABLE.
 */
void kbase_debug_coresight_csf_disable_pmode_enter(struct kbase_device *kbdev);

/**
 * kbase_debug_coresight_csf_enable_pmode_exit - Enable Coresight on Protected
 *                                                 mode enter.
 *
 * @kbdev: Instance of a GPU platform device that implements a CSF interface.
 *
 * This function should be called after protected mode exit is acknowledged.
 * It will trigger a PM state machine transition from MCU_ON
 * to ON_PMODE_EXIT_CORESIGHT_ENABLE.
 */
void kbase_debug_coresight_csf_enable_pmode_exit(struct kbase_device *kbdev);

/**
 * kbase_debug_coresight_csf_state_request - Request Coresight state transition.
 *
 * @kbdev:     Instance of a GPU platform device that implements a CSF interface.
 * @state:     Coresight state to check for.
 */
void kbase_debug_coresight_csf_state_request(struct kbase_device *kbdev,
					     enum kbase_debug_coresight_csf_state state);

/**
 * kbase_debug_coresight_csf_state_check - Check Coresight state.
 *
 * @kbdev:     Instance of a GPU platform device that implements a CSF interface.
 * @state:     Coresight state to check for.
 *
 * Return: true if all states of configs are @state.
 */
bool kbase_debug_coresight_csf_state_check(struct kbase_device *kbdev,
					   enum kbase_debug_coresight_csf_state state);

/**
 * kbase_debug_coresight_csf_state_wait - Wait for Coresight state transition to complete.
 *
 * @kbdev:     Instance of a GPU platform device that implements a CSF interface.
 * @state:     Coresight state to wait for.
 *
 * Return: true if all configs become @state in pre-defined time period.
 */
bool kbase_debug_coresight_csf_state_wait(struct kbase_device *kbdev,
					  enum kbase_debug_coresight_csf_state state);

#endif /* _KBASE_DEBUG_CORESIGHT_INTERNAL_CSF_H_ */
