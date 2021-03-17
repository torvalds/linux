/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
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

/**
 * @file
 * Defines the Mali arbiter interface
 */

#ifndef _MALI_KBASE_ARBITER_INTERFACE_H_
#define _MALI_KBASE_ARBITER_INTERFACE_H_

/**
 *  Mali arbiter interface version
 *
 * This specifies the current version of the configuration interface. Whenever
 * the arbiter interface changes, so that integration effort is required, the
 * version number will be increased. Each configuration must make an effort
 * to check that it implements the correct version.
 *
 * Version history:
 * 1 - Added the Mali arbiter configuration interface.
 * 2 - Strip out reference code from header
 * 3 - Removed DVFS utilization interface (DVFS moved to arbiter side)
 * 4 - Added max_config support
 * 5 - Added GPU clock frequency reporting support from arbiter
 */
#define MALI_KBASE_ARBITER_INTERFACE_VERSION 5

/**
 * NO_FREQ is used in case platform doesn't support reporting frequency
 */
#define NO_FREQ 0

struct arbiter_if_dev;

/**
 * struct arbiter_if_arb_vm_ops - Interface to communicate messages to VM
 *
 * This struct contains callbacks used to deliver messages
 * from the arbiter to the corresponding VM.
 *
 * Note that calls into these callbacks may have synchronous calls back into
 * the arbiter arbiter_if_vm_arb_ops callbacks below.
 * For example vm_arb_gpu_stopped() may be called as a side effect of
 * arb_vm_gpu_stop() being called here.
 */
struct arbiter_if_arb_vm_ops {
	/**
	 * arb_vm_gpu_stop() - Ask VM to stop using GPU
	 * @dev: The arbif kernel module device.
	 *
	 * Informs KBase to stop using the GPU as soon as possible.
	 * @Note: Once the driver is no longer using the GPU, a call to
	 *        vm_arb_gpu_stopped is expected by the arbiter.
	 */
	void (*arb_vm_gpu_stop)(struct device *dev);

	/**
	 * arb_vm_gpu_granted() - GPU has been granted to VM
	 * @dev: The arbif kernel module device.
	 *
	 * Informs KBase that the GPU can now be used by the VM.
	 */
	void (*arb_vm_gpu_granted)(struct device *dev);

	/**
	 * arb_vm_gpu_lost() - VM has lost the GPU
	 * @dev: The arbif kernel module device.
	 *
	 * This is called if KBase takes too long to respond to the arbiter
	 * stop request.
	 * Once this is called, KBase will assume that access to the GPU
	 * has been lost and will fail all running jobs and reset its
	 * internal state.
	 * If successful, will respond with a vm_arb_gpu_stopped message.
	 */
	void (*arb_vm_gpu_lost)(struct device *dev);

	/**
	 * arb_vm_max_config() - Send max config info to the VM
	 * @dev: The arbif kernel module device.
	 * @max_l2_slices: The maximum number of L2 slices.
	 * @max_core_mask: The largest core mask.
	 *
	 * Informs KBase the maximum resources that can be allocated to the
	 * partition in use.
	 */
	void (*arb_vm_max_config)(struct device *dev, uint32_t max_l2_slices,
				  uint32_t max_core_mask);

	/**
	 * arb_vm_update_freq() - GPU clock frequency has been updated
	 * @dev: The arbif kernel module device.
	 * @freq: GPU clock frequency value reported from arbiter
	 *
	 * Informs KBase that the GPU clock frequency has been updated.
	 */
	void (*arb_vm_update_freq)(struct device *dev, uint32_t freq);
};

/**
 * struct arbiter_if_vm_arb_ops - Interface to communicate messages to arbiter
 *
 * This struct contains callbacks used to request operations
 * from the VM to the arbiter
 *
 * Note that we must not make any synchronous calls back in to the VM
 * (via arbiter_if_arb_vm_ops above) in the context of these callbacks.
 */
struct arbiter_if_vm_arb_ops {
	/**
	 * vm_arb_register_dev() - Register VM device driver callbacks.
	 * @arbif_dev: The arbiter interface we are registering device callbacks
	 * @dev: The device structure to supply in the callbacks.
	 * @ops: The callbacks that the device driver supports
	 *       (none are optional).
	 */
	int (*vm_arb_register_dev)(struct arbiter_if_dev *arbif_dev,
		struct device *dev, struct arbiter_if_arb_vm_ops *ops);

	/**
	 * vm_arb_unregister_dev() - Unregister VM device driver callbacks.
	 * @arbif_dev: The arbiter interface we are unregistering from.
	 */
	void (*vm_arb_unregister_dev)(struct arbiter_if_dev *arbif_dev);

	/**
	 * vm_arb_gpu_get_max_config() - Request the max config from the
	 * Arbiter.
	 * @arbif_dev: The arbiter interface we want to issue the request.
	 */
	void (*vm_arb_get_max_config)(struct arbiter_if_dev *arbif_dev);

	/**
	 * vm_arb_gpu_request() - Ask the arbiter interface for GPU access.
	 * @arbif_dev: The arbiter interface we want to issue the request.
	 */
	void (*vm_arb_gpu_request)(struct arbiter_if_dev *arbif_dev);

	/**
	 * vm_arb_gpu_active() - Inform arbiter that the driver has gone active
	 * @arbif_dev: The arbiter interface device.
	 */
	void (*vm_arb_gpu_active)(struct arbiter_if_dev *arbif_dev);

	/**
	 * vm_arb_gpu_idle() - Inform the arbiter that the driver has gone idle
	 * @arbif_dev: The arbiter interface device.
	 */
	void (*vm_arb_gpu_idle)(struct arbiter_if_dev *arbif_dev);

	/**
	 * vm_arb_gpu_stopped() - Inform the arbiter that the driver has stopped
	 *                        using the GPU
	 * @arbif_dev: The arbiter interface device.
	 * @gpu_required: The GPU is still needed to do more work.
	 */
	void (*vm_arb_gpu_stopped)(struct arbiter_if_dev *arbif_dev,
		u8 gpu_required);
};

/**
 * struct arbiter_if_dev - Arbiter Interface
 * @vm_ops: Callback functions for connecting KBase with
 *          arbiter interface device.
 * @priv_data: Internal arbif data not used by KBASE.
 *
 * Arbiter Interface Kernel Module State used for linking KBase
 * with an arbiter interface platform device
 */
struct arbiter_if_dev {
	struct arbiter_if_vm_arb_ops vm_ops;
	void *priv_data;
};

#endif /* _MALI_KBASE_ARBITER_INTERFACE_H_ */
