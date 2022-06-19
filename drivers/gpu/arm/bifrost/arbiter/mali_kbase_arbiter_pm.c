// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
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
 * DOC: Mali arbiter power manager state machine and APIs
 */

#include <mali_kbase.h>
#include <mali_kbase_pm.h>
#include <backend/gpu/mali_kbase_irq_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <tl/mali_kbase_tracepoints.h>
#include <mali_kbase_gpuprops.h>

/* A dmesg warning will occur if the GPU is not granted
 * after the following time (in milliseconds) has ellapsed.
 */
#define GPU_REQUEST_TIMEOUT 1000
#define KHZ_TO_HZ 1000

#define MAX_L2_SLICES_MASK		0xFF

/* Maximum time in ms, before deferring probe incase
 * GPU_GRANTED message is not received
 */
static int gpu_req_timeout = 1;
module_param(gpu_req_timeout, int, 0644);
MODULE_PARM_DESC(gpu_req_timeout,
	"On a virtualized platform, if the GPU is not granted within this time(ms) kbase will defer the probe");

static void kbase_arbiter_pm_vm_wait_gpu_assignment(struct kbase_device *kbdev);
static inline bool kbase_arbiter_pm_vm_gpu_assigned_lockheld(
	struct kbase_device *kbdev);

/**
 * kbase_arbiter_pm_vm_state_str() - Helper function to get string
 *                                   for kbase VM state.(debug)
 * @state: kbase VM state
 *
 * Return: string representation of Kbase_vm_state
 */
static inline const char *kbase_arbiter_pm_vm_state_str(
	enum kbase_vm_state state)
{
	switch (state) {
	case KBASE_VM_STATE_INITIALIZING:
		return "KBASE_VM_STATE_INITIALIZING";
	case KBASE_VM_STATE_INITIALIZING_WITH_GPU:
		return "KBASE_VM_STATE_INITIALIZING_WITH_GPU";
	case KBASE_VM_STATE_SUSPENDED:
		return "KBASE_VM_STATE_SUSPENDED";
	case KBASE_VM_STATE_STOPPED:
		return "KBASE_VM_STATE_STOPPED";
	case KBASE_VM_STATE_STOPPED_GPU_REQUESTED:
		return "KBASE_VM_STATE_STOPPED_GPU_REQUESTED";
	case KBASE_VM_STATE_STARTING:
		return "KBASE_VM_STATE_STARTING";
	case KBASE_VM_STATE_IDLE:
		return "KBASE_VM_STATE_IDLE";
	case KBASE_VM_STATE_ACTIVE:
		return "KBASE_VM_STATE_ACTIVE";
	case KBASE_VM_STATE_STOPPING_IDLE:
		return "KBASE_VM_STATE_STOPPING_IDLE";
	case KBASE_VM_STATE_STOPPING_ACTIVE:
		return "KBASE_VM_STATE_STOPPING_ACTIVE";
	case KBASE_VM_STATE_SUSPEND_PENDING:
		return "KBASE_VM_STATE_SUSPEND_PENDING";
	case KBASE_VM_STATE_SUSPEND_WAIT_FOR_GRANT:
		return "KBASE_VM_STATE_SUSPEND_WAIT_FOR_GRANT";
	default:
		KBASE_DEBUG_ASSERT(false);
		return "[UnknownState]";
	}
}

/**
 * kbase_arbiter_pm_vm_event_str() - Helper function to get string
 *                                   for kbase VM event.(debug)
 * @evt: kbase VM state
 *
 * Return: String representation of Kbase_arbif_event
 */
static inline const char *kbase_arbiter_pm_vm_event_str(
	enum kbase_arbif_evt evt)
{
	switch (evt) {
	case KBASE_VM_GPU_INITIALIZED_EVT:
		return "KBASE_VM_GPU_INITIALIZED_EVT";
	case KBASE_VM_GPU_STOP_EVT:
		return "KBASE_VM_GPU_STOP_EVT";
	case KBASE_VM_GPU_GRANTED_EVT:
		return "KBASE_VM_GPU_GRANTED_EVT";
	case KBASE_VM_GPU_LOST_EVT:
		return "KBASE_VM_GPU_LOST_EVT";
	case KBASE_VM_OS_SUSPEND_EVENT:
		return "KBASE_VM_OS_SUSPEND_EVENT";
	case KBASE_VM_OS_RESUME_EVENT:
		return "KBASE_VM_OS_RESUME_EVENT";
	case KBASE_VM_GPU_IDLE_EVENT:
		return "KBASE_VM_GPU_IDLE_EVENT";
	case KBASE_VM_REF_EVENT:
		return "KBASE_VM_REF_EVENT";
	default:
		KBASE_DEBUG_ASSERT(false);
		return "[UnknownEvent]";
	}
}

/**
 * kbase_arbiter_pm_vm_set_state() - Sets new kbase_arbiter_vm_state
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @new_state: kbase VM new state
 *
 * This function sets the new state for the VM
 */
static void kbase_arbiter_pm_vm_set_state(struct kbase_device *kbdev,
	enum kbase_vm_state new_state)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;

	dev_dbg(kbdev->dev, "VM set_state %s -> %s",
	kbase_arbiter_pm_vm_state_str(arb_vm_state->vm_state),
	kbase_arbiter_pm_vm_state_str(new_state));

	lockdep_assert_held(&arb_vm_state->vm_state_lock);
	arb_vm_state->vm_state = new_state;
	if (new_state != KBASE_VM_STATE_INITIALIZING_WITH_GPU &&
		new_state != KBASE_VM_STATE_INITIALIZING)
		KBASE_KTRACE_ADD(kbdev, ARB_VM_STATE, NULL, new_state);
	wake_up(&arb_vm_state->vm_state_wait);
}

/**
 * kbase_arbiter_pm_suspend_wq() - suspend work queue of the driver.
 * @data: work queue
 *
 * Suspends work queue of the driver, when VM is in SUSPEND_PENDING or
 * STOPPING_IDLE or STOPPING_ACTIVE state
 */
static void kbase_arbiter_pm_suspend_wq(struct work_struct *data)
{
	struct kbase_arbiter_vm_state *arb_vm_state = container_of(data,
				struct kbase_arbiter_vm_state,
				vm_suspend_work);
	struct kbase_device *kbdev = arb_vm_state->kbdev;

	mutex_lock(&arb_vm_state->vm_state_lock);
	dev_dbg(kbdev->dev, ">%s\n", __func__);
	if (arb_vm_state->vm_state == KBASE_VM_STATE_STOPPING_IDLE ||
			arb_vm_state->vm_state ==
					KBASE_VM_STATE_STOPPING_ACTIVE ||
			arb_vm_state->vm_state ==
					KBASE_VM_STATE_SUSPEND_PENDING) {
		mutex_unlock(&arb_vm_state->vm_state_lock);
		dev_dbg(kbdev->dev, ">kbase_pm_driver_suspend\n");
		kbase_pm_driver_suspend(kbdev);
		dev_dbg(kbdev->dev, "<kbase_pm_driver_suspend\n");
		mutex_lock(&arb_vm_state->vm_state_lock);
	}
	mutex_unlock(&arb_vm_state->vm_state_lock);
	dev_dbg(kbdev->dev, "<%s\n", __func__);
}

/**
 * kbase_arbiter_pm_resume_wq() -Kbase resume work queue.
 * @data: work item
 *
 * Resume work queue of the driver when VM is in STARTING state,
 * else if its in STOPPING_ACTIVE will request a stop event.
 */
static void kbase_arbiter_pm_resume_wq(struct work_struct *data)
{
	struct kbase_arbiter_vm_state *arb_vm_state = container_of(data,
				struct kbase_arbiter_vm_state,
				vm_resume_work);
	struct kbase_device *kbdev = arb_vm_state->kbdev;

	mutex_lock(&arb_vm_state->vm_state_lock);
	dev_dbg(kbdev->dev, ">%s\n", __func__);
	arb_vm_state->vm_arb_starting = true;
	if (arb_vm_state->vm_state == KBASE_VM_STATE_STARTING) {
		mutex_unlock(&arb_vm_state->vm_state_lock);
		dev_dbg(kbdev->dev, ">kbase_pm_driver_resume\n");
		kbase_pm_driver_resume(kbdev, true);
		dev_dbg(kbdev->dev, "<kbase_pm_driver_resume\n");
		mutex_lock(&arb_vm_state->vm_state_lock);
	} else if (arb_vm_state->vm_state == KBASE_VM_STATE_STOPPING_ACTIVE) {
		kbase_arbiter_pm_vm_stopped(kbdev);
	}
	arb_vm_state->vm_arb_starting = false;
	mutex_unlock(&arb_vm_state->vm_state_lock);
	KBASE_TLSTREAM_TL_ARBITER_STARTED(kbdev, kbdev);
	dev_dbg(kbdev->dev, "<%s\n", __func__);
}

/**
 * request_timer_callback() - Issue warning on request timer expiration
 * @timer: Request hr timer data
 *
 * Called when the Arbiter takes too long to grant the GPU after a
 * request has been made.  Issues a warning in dmesg.
 *
 * Return: Always returns HRTIMER_NORESTART
 */
static enum hrtimer_restart request_timer_callback(struct hrtimer *timer)
{
	struct kbase_arbiter_vm_state *arb_vm_state = container_of(timer,
			struct kbase_arbiter_vm_state, vm_request_timer);

	KBASE_DEBUG_ASSERT(arb_vm_state);
	KBASE_DEBUG_ASSERT(arb_vm_state->kbdev);

	dev_warn(arb_vm_state->kbdev->dev,
		"Still waiting for GPU to be granted from Arbiter after %d ms\n",
		GPU_REQUEST_TIMEOUT);
	return HRTIMER_NORESTART;
}

/**
 * start_request_timer() - Start a timer after requesting GPU
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Start a timer to track when kbase is waiting for the GPU from the
 * Arbiter.  If the timer expires before GPU is granted, a warning in
 * dmesg will be issued.
 */
static void start_request_timer(struct kbase_device *kbdev)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;

	hrtimer_start(&arb_vm_state->vm_request_timer,
			HR_TIMER_DELAY_MSEC(GPU_REQUEST_TIMEOUT),
			HRTIMER_MODE_REL);
}

/**
 * cancel_request_timer() - Stop the request timer
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Stops the request timer once GPU has been granted.  Safe to call
 * even if timer is no longer running.
 */
static void cancel_request_timer(struct kbase_device *kbdev)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;

	hrtimer_cancel(&arb_vm_state->vm_request_timer);
}

/**
 * kbase_arbiter_pm_early_init() - Initialize arbiter for VM
 *                                 Paravirtualized use.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Initialize the arbiter and other required resources during the runtime
 * and request the GPU for the VM for the first time.
 *
 * Return: 0 if success, or a Linux error code
 */
int kbase_arbiter_pm_early_init(struct kbase_device *kbdev)
{
	int err;
	struct kbase_arbiter_vm_state *arb_vm_state = NULL;

	arb_vm_state = kmalloc(sizeof(struct kbase_arbiter_vm_state),
				GFP_KERNEL);
	if (arb_vm_state == NULL)
		return -ENOMEM;

	arb_vm_state->kbdev = kbdev;
	arb_vm_state->vm_state = KBASE_VM_STATE_INITIALIZING;

	mutex_init(&arb_vm_state->vm_state_lock);
	init_waitqueue_head(&arb_vm_state->vm_state_wait);
	arb_vm_state->vm_arb_wq = alloc_ordered_workqueue("kbase_vm_arb_wq",
		WQ_HIGHPRI);
	if (!arb_vm_state->vm_arb_wq) {
		dev_err(kbdev->dev, "Failed to allocate vm_arb workqueue\n");
		kfree(arb_vm_state);
		return -ENOMEM;
	}
	INIT_WORK(&arb_vm_state->vm_suspend_work, kbase_arbiter_pm_suspend_wq);
	INIT_WORK(&arb_vm_state->vm_resume_work, kbase_arbiter_pm_resume_wq);
	arb_vm_state->vm_arb_starting = false;
	atomic_set(&kbdev->pm.gpu_users_waiting, 0);
	hrtimer_init(&arb_vm_state->vm_request_timer, CLOCK_MONOTONIC,
							HRTIMER_MODE_REL);
	arb_vm_state->vm_request_timer.function =
						request_timer_callback;
	kbdev->pm.arb_vm_state = arb_vm_state;

	err = kbase_arbif_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "Failed to initialise arbif module\n");
		goto arbif_init_fail;
	}

	if (kbdev->arb.arb_if) {
		kbase_arbif_gpu_request(kbdev);
		dev_dbg(kbdev->dev, "Waiting for initial GPU assignment...\n");

		err = wait_event_timeout(arb_vm_state->vm_state_wait,
			arb_vm_state->vm_state ==
					KBASE_VM_STATE_INITIALIZING_WITH_GPU,
			msecs_to_jiffies(gpu_req_timeout));

		if (!err) {
			dev_dbg(kbdev->dev,
			"Kbase probe Deferred after waiting %d ms to receive GPU_GRANT\n",
			gpu_req_timeout);

			err = -ENODEV;
			goto arbif_timeout;
		}

		dev_dbg(kbdev->dev,
			"Waiting for initial GPU assignment - done\n");
	}
	return 0;

arbif_timeout:
	kbase_arbiter_pm_early_term(kbdev);
	return err;

arbif_init_fail:
	destroy_workqueue(arb_vm_state->vm_arb_wq);
	kfree(arb_vm_state);
	kbdev->pm.arb_vm_state = NULL;
	return err;
}

/**
 * kbase_arbiter_pm_early_term() - Shutdown arbiter and free resources
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Clean up all the resources
 */
void kbase_arbiter_pm_early_term(struct kbase_device *kbdev)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;

	cancel_request_timer(kbdev);
	mutex_lock(&arb_vm_state->vm_state_lock);
	if (arb_vm_state->vm_state > KBASE_VM_STATE_STOPPED_GPU_REQUESTED) {
		kbase_pm_set_gpu_lost(kbdev, false);
		kbase_arbif_gpu_stopped(kbdev, false);
	}
	mutex_unlock(&arb_vm_state->vm_state_lock);
	destroy_workqueue(arb_vm_state->vm_arb_wq);
	kbase_arbif_destroy(kbdev);
	arb_vm_state->vm_arb_wq = NULL;
	kfree(kbdev->pm.arb_vm_state);
	kbdev->pm.arb_vm_state = NULL;
}

/**
 * kbase_arbiter_pm_release_interrupts() - Release the GPU interrupts
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Releases interrupts and set the interrupt flag to false
 */
void kbase_arbiter_pm_release_interrupts(struct kbase_device *kbdev)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;

	mutex_lock(&arb_vm_state->vm_state_lock);
	if (arb_vm_state->interrupts_installed == true) {
		arb_vm_state->interrupts_installed = false;
		kbase_release_interrupts(kbdev);
	}
	mutex_unlock(&arb_vm_state->vm_state_lock);
}

/**
 * kbase_arbiter_pm_install_interrupts() - Install the GPU interrupts
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Install interrupts and set the interrupt_install flag to true.
 *
 * Return: 0 if success, or a Linux error code
 */
int kbase_arbiter_pm_install_interrupts(struct kbase_device *kbdev)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;
	int err;

	mutex_lock(&arb_vm_state->vm_state_lock);
	arb_vm_state->interrupts_installed = true;
	err = kbase_install_interrupts(kbdev);
	mutex_unlock(&arb_vm_state->vm_state_lock);
	return err;
}

/**
 * kbase_arbiter_pm_vm_stopped() - Handle stop state for the VM
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Handles a stop state for the VM
 */
void kbase_arbiter_pm_vm_stopped(struct kbase_device *kbdev)
{
	bool request_gpu = false;
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;

	lockdep_assert_held(&arb_vm_state->vm_state_lock);

	if (atomic_read(&kbdev->pm.gpu_users_waiting) > 0 &&
			arb_vm_state->vm_state == KBASE_VM_STATE_STOPPING_IDLE)
		kbase_arbiter_pm_vm_set_state(kbdev,
			 KBASE_VM_STATE_STOPPING_ACTIVE);

	dev_dbg(kbdev->dev, "%s %s\n", __func__,
		kbase_arbiter_pm_vm_state_str(arb_vm_state->vm_state));

	if (arb_vm_state->interrupts_installed) {
		arb_vm_state->interrupts_installed = false;
		kbase_release_interrupts(kbdev);
	}

	switch (arb_vm_state->vm_state) {
	case KBASE_VM_STATE_STOPPING_ACTIVE:
		request_gpu = true;
		kbase_arbiter_pm_vm_set_state(kbdev,
			KBASE_VM_STATE_STOPPED_GPU_REQUESTED);
		break;
	case KBASE_VM_STATE_STOPPING_IDLE:
		kbase_arbiter_pm_vm_set_state(kbdev, KBASE_VM_STATE_STOPPED);
		break;
	case KBASE_VM_STATE_SUSPEND_PENDING:
		kbase_arbiter_pm_vm_set_state(kbdev, KBASE_VM_STATE_SUSPENDED);
		break;
	default:
		dev_warn(kbdev->dev, "unexpected pm_stop VM state %u",
			arb_vm_state->vm_state);
		break;
	}

	kbase_pm_set_gpu_lost(kbdev, false);
	kbase_arbif_gpu_stopped(kbdev, request_gpu);
	if (request_gpu)
		start_request_timer(kbdev);
}

void kbase_arbiter_set_max_config(struct kbase_device *kbdev,
				  uint32_t max_l2_slices,
				  uint32_t max_core_mask)
{
	struct kbase_arbiter_vm_state *arb_vm_state;
	struct max_config_props max_config;

	if (!kbdev)
		return;

	/* Mask the max_l2_slices as it is stored as 8 bits into kbase */
	max_config.l2_slices = max_l2_slices & MAX_L2_SLICES_MASK;
	max_config.core_mask = max_core_mask;
	arb_vm_state = kbdev->pm.arb_vm_state;

	mutex_lock(&arb_vm_state->vm_state_lock);
	/* Just set the max_props in kbase during initialization. */
	if (arb_vm_state->vm_state == KBASE_VM_STATE_INITIALIZING)
		kbase_gpuprops_set_max_config(kbdev, &max_config);
	else
		dev_dbg(kbdev->dev, "Unexpected max_config on VM state %s",
			kbase_arbiter_pm_vm_state_str(arb_vm_state->vm_state));

	mutex_unlock(&arb_vm_state->vm_state_lock);
}

int kbase_arbiter_pm_gpu_assigned(struct kbase_device *kbdev)
{
	struct kbase_arbiter_vm_state *arb_vm_state;
	int result = -EINVAL;

	if (!kbdev)
		return result;

	/* First check the GPU_LOST state */
	kbase_pm_lock(kbdev);
	if (kbase_pm_is_gpu_lost(kbdev)) {
		kbase_pm_unlock(kbdev);
		return 0;
	}
	kbase_pm_unlock(kbdev);

	/* Then the arbitration state machine */
	arb_vm_state = kbdev->pm.arb_vm_state;

	mutex_lock(&arb_vm_state->vm_state_lock);
	switch (arb_vm_state->vm_state) {
	case KBASE_VM_STATE_INITIALIZING:
	case KBASE_VM_STATE_SUSPENDED:
	case KBASE_VM_STATE_STOPPED:
	case KBASE_VM_STATE_STOPPED_GPU_REQUESTED:
	case KBASE_VM_STATE_SUSPEND_WAIT_FOR_GRANT:
		result = 0;
		break;
	default:
		result = 1;
		break;
	}
	mutex_unlock(&arb_vm_state->vm_state_lock);

	return result;
}

/**
 * kbase_arbiter_pm_vm_gpu_start() - Handles the start state of the VM
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Handles the start state of the VM
 */
static void kbase_arbiter_pm_vm_gpu_start(struct kbase_device *kbdev)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;
	bool freq_updated = false;

	lockdep_assert_held(&arb_vm_state->vm_state_lock);
	mutex_lock(&kbdev->arb.arb_freq.arb_freq_lock);
	if (kbdev->arb.arb_freq.freq_updated) {
		kbdev->arb.arb_freq.freq_updated = false;
		freq_updated = true;
	}
	mutex_unlock(&kbdev->arb.arb_freq.arb_freq_lock);

	cancel_request_timer(kbdev);
	switch (arb_vm_state->vm_state) {
	case KBASE_VM_STATE_INITIALIZING:
		kbase_arbiter_pm_vm_set_state(kbdev,
			KBASE_VM_STATE_INITIALIZING_WITH_GPU);
		break;
	case KBASE_VM_STATE_STOPPED_GPU_REQUESTED:
		kbase_arbiter_pm_vm_set_state(kbdev, KBASE_VM_STATE_STARTING);
		arb_vm_state->interrupts_installed = true;
		kbase_install_interrupts(kbdev);
		/*
		 * GPU GRANTED received while in stop can be a result of a
		 * repartitioning.
		 */
		kbase_gpuprops_req_curr_config_update(kbdev);
		/* curr_config will be updated while resuming the PM. */
		queue_work(arb_vm_state->vm_arb_wq,
			&arb_vm_state->vm_resume_work);
		break;
	case KBASE_VM_STATE_SUSPEND_WAIT_FOR_GRANT:
		kbase_pm_set_gpu_lost(kbdev, false);
		kbase_arbif_gpu_stopped(kbdev, false);
		kbase_arbiter_pm_vm_set_state(kbdev, KBASE_VM_STATE_SUSPENDED);
		break;
	default:
		/*
		 * GPU_GRANTED can be received when there is a frequency update
		 * Only show a warning if received in an unexpected state
		 * without a frequency update
		 */
		if (!freq_updated)
			dev_warn(kbdev->dev,
				"GPU_GRANTED when not expected - state %s\n",
				kbase_arbiter_pm_vm_state_str(
					arb_vm_state->vm_state));
		break;
	}
}

/**
 * kbase_arbiter_pm_vm_gpu_stop() - Handles the stop state of the VM
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Handles the start state of the VM
 */
static void kbase_arbiter_pm_vm_gpu_stop(struct kbase_device *kbdev)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;

	lockdep_assert_held(&arb_vm_state->vm_state_lock);
	if (arb_vm_state->vm_state == KBASE_VM_STATE_INITIALIZING_WITH_GPU) {
		mutex_unlock(&arb_vm_state->vm_state_lock);
		kbase_arbiter_pm_vm_wait_gpu_assignment(kbdev);
		mutex_lock(&arb_vm_state->vm_state_lock);
	}

	switch (arb_vm_state->vm_state) {
	case KBASE_VM_STATE_IDLE:
		kbase_arbiter_pm_vm_set_state(kbdev,
			KBASE_VM_STATE_STOPPING_IDLE);
		queue_work(arb_vm_state->vm_arb_wq,
			&arb_vm_state->vm_suspend_work);
		break;
	case KBASE_VM_STATE_ACTIVE:
		kbase_arbiter_pm_vm_set_state(kbdev,
			KBASE_VM_STATE_STOPPING_ACTIVE);
		queue_work(arb_vm_state->vm_arb_wq,
			&arb_vm_state->vm_suspend_work);
		break;
	case KBASE_VM_STATE_STARTING:
		dev_dbg(kbdev->dev, "Got GPU_STOP event while STARTING.");
		kbase_arbiter_pm_vm_set_state(kbdev,
			KBASE_VM_STATE_STOPPING_ACTIVE);
		if (arb_vm_state->vm_arb_starting)
			queue_work(arb_vm_state->vm_arb_wq,
				&arb_vm_state->vm_suspend_work);
		break;
	case KBASE_VM_STATE_SUSPEND_PENDING:
		/* Suspend finishes with a stop so nothing else to do */
		break;
	default:
		dev_warn(kbdev->dev, "GPU_STOP when not expected - state %s\n",
			kbase_arbiter_pm_vm_state_str(arb_vm_state->vm_state));
		break;
	}
}

/**
 * kbase_gpu_lost() - Kbase signals GPU is lost on a lost event signal
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * On GPU lost event signals GPU_LOST to the aribiter
 */
static void kbase_gpu_lost(struct kbase_device *kbdev)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;
	bool handle_gpu_lost = false;

	lockdep_assert_held(&arb_vm_state->vm_state_lock);

	switch (arb_vm_state->vm_state) {
	case KBASE_VM_STATE_STARTING:
	case KBASE_VM_STATE_ACTIVE:
	case KBASE_VM_STATE_IDLE:
		dev_warn(kbdev->dev, "GPU lost in state %s",
		kbase_arbiter_pm_vm_state_str(arb_vm_state->vm_state));
		kbase_arbiter_pm_vm_gpu_stop(kbdev);
		handle_gpu_lost = true;
		break;
	case KBASE_VM_STATE_STOPPING_IDLE:
	case KBASE_VM_STATE_STOPPING_ACTIVE:
	case KBASE_VM_STATE_SUSPEND_PENDING:
		dev_dbg(kbdev->dev, "GPU lost while stopping");
		handle_gpu_lost = true;
		break;
	case KBASE_VM_STATE_SUSPENDED:
	case KBASE_VM_STATE_STOPPED:
	case KBASE_VM_STATE_STOPPED_GPU_REQUESTED:
		dev_dbg(kbdev->dev, "GPU lost while already stopped");
		break;
	case KBASE_VM_STATE_SUSPEND_WAIT_FOR_GRANT:
		dev_dbg(kbdev->dev, "GPU lost while waiting to suspend");
		kbase_arbiter_pm_vm_set_state(kbdev, KBASE_VM_STATE_SUSPENDED);
		break;
	default:
		break;
	}
	if (handle_gpu_lost) {
		/* Releasing the VM state lock here is safe because
		 * we are guaranteed to be in either STOPPING_IDLE,
		 * STOPPING_ACTIVE or SUSPEND_PENDING at this point.
		 * The only transitions that are valid from here are to
		 * STOPPED, STOPPED_GPU_REQUESTED or SUSPENDED which can
		 * only happen at the completion of the GPU lost handling.
		 */
		mutex_unlock(&arb_vm_state->vm_state_lock);
		kbase_pm_handle_gpu_lost(kbdev);
		mutex_lock(&arb_vm_state->vm_state_lock);
	}
}

/**
 * kbase_arbiter_pm_vm_os_suspend_ready_state() - checks if VM is ready
 *			to be moved to suspended state.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Return: True if its ready to be suspended else False.
 */
static inline bool kbase_arbiter_pm_vm_os_suspend_ready_state(
	struct kbase_device *kbdev)
{
	switch (kbdev->pm.arb_vm_state->vm_state) {
	case KBASE_VM_STATE_SUSPENDED:
	case KBASE_VM_STATE_STOPPED:
	case KBASE_VM_STATE_IDLE:
	case KBASE_VM_STATE_ACTIVE:
		return true;
	default:
		return false;
	}
}

/**
 * kbase_arbiter_pm_vm_os_prepare_suspend() - Prepare OS to be in suspend state
 *                             until it receives the grant message from arbiter
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Prepares OS to be in suspend state until it receives GRANT message
 * from Arbiter asynchronously.
 */
static void kbase_arbiter_pm_vm_os_prepare_suspend(struct kbase_device *kbdev)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;
	enum kbase_vm_state prev_state;

	lockdep_assert_held(&arb_vm_state->vm_state_lock);
	if (kbdev->arb.arb_if) {
		if (kbdev->pm.arb_vm_state->vm_state ==
					KBASE_VM_STATE_SUSPENDED)
			return;
	}
	/* Block suspend OS function until we are in a stable state
	 * with vm_state_lock
	 */
	while (!kbase_arbiter_pm_vm_os_suspend_ready_state(kbdev)) {
		prev_state = arb_vm_state->vm_state;
		switch (arb_vm_state->vm_state) {
		case KBASE_VM_STATE_STOPPING_ACTIVE:
		case KBASE_VM_STATE_STOPPING_IDLE:
			kbase_arbiter_pm_vm_set_state(kbdev,
				KBASE_VM_STATE_SUSPEND_PENDING);
			break;
		case KBASE_VM_STATE_STOPPED_GPU_REQUESTED:
			kbase_arbiter_pm_vm_set_state(kbdev,
				KBASE_VM_STATE_SUSPEND_WAIT_FOR_GRANT);
			break;
		case KBASE_VM_STATE_STARTING:
			if (!arb_vm_state->vm_arb_starting) {
				kbase_arbiter_pm_vm_set_state(kbdev,
					KBASE_VM_STATE_SUSPEND_PENDING);
				kbase_arbiter_pm_vm_stopped(kbdev);
			}
			break;
		default:
			break;
		}
		mutex_unlock(&arb_vm_state->vm_state_lock);
		wait_event(arb_vm_state->vm_state_wait,
			arb_vm_state->vm_state != prev_state);
		mutex_lock(&arb_vm_state->vm_state_lock);
	}

	switch (arb_vm_state->vm_state) {
	case KBASE_VM_STATE_STOPPED:
		kbase_arbiter_pm_vm_set_state(kbdev,
			KBASE_VM_STATE_SUSPENDED);
		break;
	case KBASE_VM_STATE_IDLE:
	case KBASE_VM_STATE_ACTIVE:
		kbase_arbiter_pm_vm_set_state(kbdev,
			KBASE_VM_STATE_SUSPEND_PENDING);
		mutex_unlock(&arb_vm_state->vm_state_lock);
		/* Ensure resume has completed fully before starting suspend */
		flush_work(&arb_vm_state->vm_resume_work);
		kbase_pm_driver_suspend(kbdev);
		mutex_lock(&arb_vm_state->vm_state_lock);
		break;
	case KBASE_VM_STATE_SUSPENDED:
		break;
	default:
		KBASE_DEBUG_ASSERT_MSG(false, "Unexpected state to suspend");
		break;
	}
}

/**
 * kbase_arbiter_pm_vm_os_resume() - Resume OS function once it receives
 *                                   a grant message from arbiter
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Resume OS function once it receives GRANT message
 * from Arbiter asynchronously.
 */
static void kbase_arbiter_pm_vm_os_resume(struct kbase_device *kbdev)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;

	lockdep_assert_held(&arb_vm_state->vm_state_lock);
	KBASE_DEBUG_ASSERT_MSG(arb_vm_state->vm_state ==
						KBASE_VM_STATE_SUSPENDED,
		"Unexpected state to resume");

	kbase_arbiter_pm_vm_set_state(kbdev,
		KBASE_VM_STATE_STOPPED_GPU_REQUESTED);
	kbase_arbif_gpu_request(kbdev);
	start_request_timer(kbdev);

	/* Release lock and block resume OS function until we have
	 * asynchronously received the GRANT message from the Arbiter and
	 * fully resumed
	 */
	mutex_unlock(&arb_vm_state->vm_state_lock);
	kbase_arbiter_pm_vm_wait_gpu_assignment(kbdev);
	flush_work(&arb_vm_state->vm_resume_work);
	mutex_lock(&arb_vm_state->vm_state_lock);
}

/**
 * kbase_arbiter_pm_vm_event() - Dispatch VM event to the state machine.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @evt: VM event
 *
 * The state machine function. Receives events and transitions states
 * according the event received and the current state
 */
void kbase_arbiter_pm_vm_event(struct kbase_device *kbdev,
	enum kbase_arbif_evt evt)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;

	if (!kbdev->arb.arb_if)
		return;

	mutex_lock(&arb_vm_state->vm_state_lock);
	dev_dbg(kbdev->dev, "%s %s\n", __func__,
		kbase_arbiter_pm_vm_event_str(evt));
	if (arb_vm_state->vm_state != KBASE_VM_STATE_INITIALIZING_WITH_GPU &&
		arb_vm_state->vm_state != KBASE_VM_STATE_INITIALIZING)
		KBASE_KTRACE_ADD(kbdev, ARB_VM_EVT, NULL, evt);
	switch (evt) {
	case KBASE_VM_GPU_GRANTED_EVT:
		kbase_arbiter_pm_vm_gpu_start(kbdev);
		break;
	case KBASE_VM_GPU_STOP_EVT:
		kbase_arbiter_pm_vm_gpu_stop(kbdev);
		break;
	case KBASE_VM_GPU_LOST_EVT:
		dev_dbg(kbdev->dev, "KBASE_ARBIF_GPU_LOST_EVT!");
		kbase_gpu_lost(kbdev);
		break;
	case KBASE_VM_OS_SUSPEND_EVENT:
		kbase_arbiter_pm_vm_os_prepare_suspend(kbdev);
		break;
	case KBASE_VM_OS_RESUME_EVENT:
		kbase_arbiter_pm_vm_os_resume(kbdev);
		break;
	case KBASE_VM_GPU_IDLE_EVENT:
		switch (arb_vm_state->vm_state) {
		case KBASE_VM_STATE_ACTIVE:
			kbase_arbiter_pm_vm_set_state(kbdev,
				KBASE_VM_STATE_IDLE);
			kbase_arbif_gpu_idle(kbdev);
			break;
		default:
			break;
		}
		break;

	case KBASE_VM_REF_EVENT:
		switch (arb_vm_state->vm_state) {
		case KBASE_VM_STATE_STARTING:
		case KBASE_VM_STATE_IDLE:
			kbase_arbiter_pm_vm_set_state(kbdev,
			KBASE_VM_STATE_ACTIVE);
			kbase_arbif_gpu_active(kbdev);
			break;
		case KBASE_VM_STATE_STOPPING_IDLE:
			kbase_arbiter_pm_vm_set_state(kbdev,
				KBASE_VM_STATE_STOPPING_ACTIVE);
			break;
		default:
			break;
		}
		break;

	case KBASE_VM_GPU_INITIALIZED_EVT:
		switch (arb_vm_state->vm_state) {
		case KBASE_VM_STATE_INITIALIZING_WITH_GPU:
			lockdep_assert_held(&kbdev->pm.lock);
			if (kbdev->pm.active_count > 0) {
				kbase_arbiter_pm_vm_set_state(kbdev,
					KBASE_VM_STATE_ACTIVE);
				kbase_arbif_gpu_active(kbdev);
			} else {
				kbase_arbiter_pm_vm_set_state(kbdev,
					KBASE_VM_STATE_IDLE);
				kbase_arbif_gpu_idle(kbdev);
			}
			break;
		default:
			break;
		}
		break;

	default:
		dev_alert(kbdev->dev, "Got Unknown Event!");
		break;
	}
	mutex_unlock(&arb_vm_state->vm_state_lock);
}

KBASE_EXPORT_TEST_API(kbase_arbiter_pm_vm_event);

/**
 * kbase_arbiter_pm_vm_wait_gpu_assignment() - VM wait for a GPU assignment.
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * VM waits for a GPU assignment.
 */
static void kbase_arbiter_pm_vm_wait_gpu_assignment(struct kbase_device *kbdev)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;

	dev_dbg(kbdev->dev, "Waiting for GPU assignment...\n");
	wait_event(arb_vm_state->vm_state_wait,
		arb_vm_state->vm_state == KBASE_VM_STATE_IDLE ||
		arb_vm_state->vm_state == KBASE_VM_STATE_ACTIVE);
	dev_dbg(kbdev->dev, "Waiting for GPU assignment - done\n");
}

/**
 * kbase_arbiter_pm_vm_gpu_assigned_lockheld() - Check if VM holds VM state lock
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 *
 * Checks if the virtual machine holds VM state lock.
 *
 * Return: true if GPU is assigned, else false.
 */
static inline bool kbase_arbiter_pm_vm_gpu_assigned_lockheld(
	struct kbase_device *kbdev)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;

	lockdep_assert_held(&arb_vm_state->vm_state_lock);
	return (arb_vm_state->vm_state == KBASE_VM_STATE_IDLE ||
		arb_vm_state->vm_state == KBASE_VM_STATE_ACTIVE);
}

/**
 * kbase_arbiter_pm_ctx_active_handle_suspend() - Handle suspend operation for
 *                                                arbitration mode
 * @kbdev: The kbase device structure for the device (must be a valid pointer)
 * @suspend_handler: The handler code for how to handle a suspend
 *                   that might occur
 *
 * This function handles a suspend event from the driver,
 * communicating with the arbiter and waiting synchronously for the GPU
 * to be granted again depending on the VM state.
 *
 * Return: 0 on success else 1 suspend handler isn not possible.
 */
int kbase_arbiter_pm_ctx_active_handle_suspend(struct kbase_device *kbdev,
	enum kbase_pm_suspend_handler suspend_handler)
{
	struct kbase_arbiter_vm_state *arb_vm_state = kbdev->pm.arb_vm_state;
	int res = 0;

	if (kbdev->arb.arb_if) {
		mutex_lock(&arb_vm_state->vm_state_lock);
		while (!kbase_arbiter_pm_vm_gpu_assigned_lockheld(kbdev)) {
			/* Update VM state since we have GPU work to do */
			if (arb_vm_state->vm_state ==
						KBASE_VM_STATE_STOPPING_IDLE)
				kbase_arbiter_pm_vm_set_state(kbdev,
					KBASE_VM_STATE_STOPPING_ACTIVE);
			else if (arb_vm_state->vm_state ==
						KBASE_VM_STATE_STOPPED) {
				kbase_arbiter_pm_vm_set_state(kbdev,
					KBASE_VM_STATE_STOPPED_GPU_REQUESTED);
				kbase_arbif_gpu_request(kbdev);
				start_request_timer(kbdev);
			} else if (arb_vm_state->vm_state ==
					KBASE_VM_STATE_INITIALIZING_WITH_GPU)
				break;

			if (suspend_handler !=
				KBASE_PM_SUSPEND_HANDLER_NOT_POSSIBLE) {

				/* In case of GPU lost, even if
				 * active_count > 0, we no longer have GPU
				 * access
				 */
				if (kbase_pm_is_gpu_lost(kbdev))
					res = 1;

				switch (suspend_handler) {
				case KBASE_PM_SUSPEND_HANDLER_DONT_INCREASE:
					res = 1;
					break;
				case KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE:
					if (kbdev->pm.active_count == 0)
						res = 1;
					break;
				case KBASE_PM_SUSPEND_HANDLER_VM_GPU_GRANTED:
					break;
				default:
					WARN(1, "Unknown suspend_handler\n");
					res = 1;
					break;
				}
				break;
			}

			/* Need to synchronously wait for GPU assignment */
			atomic_inc(&kbdev->pm.gpu_users_waiting);
			mutex_unlock(&arb_vm_state->vm_state_lock);
			kbase_pm_unlock(kbdev);
			kbase_arbiter_pm_vm_wait_gpu_assignment(kbdev);
			kbase_pm_lock(kbdev);
			mutex_lock(&arb_vm_state->vm_state_lock);
			atomic_dec(&kbdev->pm.gpu_users_waiting);
		}
		mutex_unlock(&arb_vm_state->vm_state_lock);
	}
	return res;
}

/**
 * kbase_arbiter_pm_update_gpu_freq() - Updates GPU clock frequency received
 * from arbiter.
 * @arb_freq: Pointer to struchture holding GPU clock frequenecy data
 * @freq: New frequency value in KHz
 */
void kbase_arbiter_pm_update_gpu_freq(struct kbase_arbiter_freq *arb_freq,
	uint32_t freq)
{
	struct kbase_gpu_clk_notifier_data ndata;

	mutex_lock(&arb_freq->arb_freq_lock);
	if (arb_freq->arb_freq != freq) {
		ndata.new_rate = (unsigned long)freq * KHZ_TO_HZ;
		ndata.old_rate = (unsigned long)arb_freq->arb_freq * KHZ_TO_HZ;
		ndata.gpu_clk_handle = arb_freq;
		arb_freq->arb_freq = freq;
		arb_freq->freq_updated = true;
		if (arb_freq->nb)
			arb_freq->nb->notifier_call(arb_freq->nb,
						    POST_RATE_CHANGE, &ndata);
	}

	mutex_unlock(&arb_freq->arb_freq_lock);
}

/**
 * get_arb_gpu_clk() - Enumerate a GPU clock on the given index
 * @kbdev: kbase_device pointer
 * @index: GPU clock index
 *
 * Return: Pointer to structure holding GPU clock frequency data reported from
 * arbiter, only index 0 is valid.
 */
static void *get_arb_gpu_clk(struct kbase_device *kbdev,
		unsigned int index)
{
	if (index == 0)
		return &kbdev->arb.arb_freq;
	return NULL;
}

/**
 * get_arb_gpu_clk_rate() - Get the current rate of GPU clock frequency value
 * @kbdev:          kbase_device pointer
 * @gpu_clk_handle: Handle unique to the enumerated GPU clock
 *
 * Return: The GPU clock frequency value saved when gpu is granted from arbiter
 */
static unsigned long get_arb_gpu_clk_rate(struct kbase_device *kbdev,
		void *gpu_clk_handle)
{
	uint32_t freq;
	struct kbase_arbiter_freq *arb_dev_freq =
			(struct kbase_arbiter_freq *) gpu_clk_handle;

	mutex_lock(&arb_dev_freq->arb_freq_lock);
	/* Convert from KHz to Hz */
	freq = arb_dev_freq->arb_freq * KHZ_TO_HZ;
	mutex_unlock(&arb_dev_freq->arb_freq_lock);
	return freq;
}

/**
 * arb_gpu_clk_notifier_register() - Register a clock rate change notifier.
 * @kbdev:           kbase_device pointer
 * @gpu_clk_handle:  Handle unique to the enumerated GPU clock
 * @nb:              notifier block containing the callback function pointer
 *
 * This function registers a callback function that is invoked whenever the
 * frequency of the clock corresponding to @gpu_clk_handle changes.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int arb_gpu_clk_notifier_register(struct kbase_device *kbdev,
	void *gpu_clk_handle, struct notifier_block *nb)
{
	int ret = 0;
	struct kbase_arbiter_freq *arb_dev_freq =
		(struct kbase_arbiter_freq *)gpu_clk_handle;

	if (!arb_dev_freq->nb)
		arb_dev_freq->nb = nb;
	else
		ret = -EBUSY;

	return ret;
}

/**
 * arb_gpu_clk_notifier_unregister() - Unregister clock rate change notifier
 * @kbdev:           kbase_device pointer
 * @gpu_clk_handle:  Handle unique to the enumerated GPU clock
 * @nb:              notifier block containing the callback function pointer
 *
 * This function pointer is used to unregister a callback function that
 * was previously registered to get notified of a frequency change of the
 * clock corresponding to @gpu_clk_handle.
 */
static void arb_gpu_clk_notifier_unregister(struct kbase_device *kbdev,
	void *gpu_clk_handle, struct notifier_block *nb)
{
	struct kbase_arbiter_freq *arb_dev_freq =
		(struct kbase_arbiter_freq *)gpu_clk_handle;
	if (arb_dev_freq->nb == nb) {
		arb_dev_freq->nb = NULL;
	} else {
		dev_err(kbdev->dev, "%s - notifier did not match\n",
			 __func__);
	}
}

struct kbase_clk_rate_trace_op_conf arb_clk_rate_trace_ops = {
	.get_gpu_clk_rate = get_arb_gpu_clk_rate,
	.enumerate_gpu_clk = get_arb_gpu_clk,
	.gpu_clk_notifier_register = arb_gpu_clk_notifier_register,
	.gpu_clk_notifier_unregister = arb_gpu_clk_notifier_unregister
};
