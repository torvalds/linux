// SPDX-License-Identifier: GPL-2.0
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

#include <mali_kbase.h>
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_hwcnt_context.h>
#include <device/mali_kbase_device.h>
#include <backend/gpu/mali_kbase_irq_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <mali_kbase_regs_history_debugfs.h>
#include <csf/mali_kbase_csf_trace_buffer.h>
#include <csf/ipa_control/mali_kbase_csf_ipa_control.h>
#include <mali_kbase_reset_gpu.h>

/* Waiting timeout for GPU reset to complete */
#define GPU_RESET_TIMEOUT_MS (5000) /* 5 seconds */
#define DUMP_DWORDS_PER_LINE (4)
/* 16 characters needed for a 8 byte value in hex & 1 character for space */
#define DUMP_HEX_CHARS_PER_DWORD ((2 * 8) + 1)
#define DUMP_HEX_CHARS_PER_LINE  \
	(DUMP_DWORDS_PER_LINE * DUMP_HEX_CHARS_PER_DWORD)

static inline bool
kbase_csf_reset_state_is_silent(enum kbase_csf_reset_gpu_state state)
{
	return (state == KBASE_CSF_RESET_GPU_COMMITTED_SILENT);
}

static inline bool
kbase_csf_reset_state_is_committed(enum kbase_csf_reset_gpu_state state)
{
	return (state == KBASE_CSF_RESET_GPU_COMMITTED ||
		state == KBASE_CSF_RESET_GPU_COMMITTED_SILENT);
}

static inline bool
kbase_csf_reset_state_is_active(enum kbase_csf_reset_gpu_state state)
{
	return (state == KBASE_CSF_RESET_GPU_HAPPENING);
}

/**
 * DOC: Mechanism for coherent access to the HW with respect to GPU reset
 *
 * Access to the HW from non-atomic context outside of the reset thread must
 * use kbase_reset_gpu_prevent_and_wait() / kbase_reset_gpu_try_prevent().
 *
 * This currently works by taking the &kbase_device's csf.reset.sem, for
 * 'write' access by the GPU reset thread and 'read' access by every other
 * thread. The use of this rw_semaphore means:
 *
 * - there will be mutual exclusion (and thus waiting) between the thread doing
 *   reset ('writer') and threads trying to access the GPU for 'normal'
 *   operations ('readers')
 *
 * - multiple threads may prevent reset from happening without serializing each
 *   other prematurely. Note that at present the wait for reset to finish has
 *   to be done higher up in the driver than actual GPU access, at a point
 *   where it won't cause lock ordering issues. At such a point, some paths may
 *   actually lead to no GPU access, but we would prefer to avoid serializing
 *   at that level
 *
 * - lockdep (if enabled in the kernel) will check such uses for deadlock
 *
 * If instead &kbase_device's csf.reset.wait &wait_queue_head_t were used on
 * its own, we'd also need to add a &lockdep_map and appropriate lockdep calls
 * to make use of lockdep checking in all places where the &wait_queue_head_t
 * is waited upon or signaled.
 *
 * Indeed places where we wait on &kbase_device's csf.reset.wait (such as
 * kbase_reset_gpu_wait()) are the only places where we need extra call(s) to
 * lockdep, and they are made on the existing rw_semaphore.
 *
 * For non-atomic access, the &kbase_device's csf.reset.state member should be
 * checked instead, such as by using kbase_reset_gpu_is_active().
 *
 * Ideally the &rw_semaphore should be replaced in future with a single mutex
 * that protects any access to the GPU, via reset or otherwise.
 */

int kbase_reset_gpu_prevent_and_wait(struct kbase_device *kbdev)
{
	down_read(&kbdev->csf.reset.sem);

	if (atomic_read(&kbdev->csf.reset.state) ==
	    KBASE_CSF_RESET_GPU_FAILED) {
		up_read(&kbdev->csf.reset.sem);
		return -ENOMEM;
	}

	if (WARN_ON(kbase_reset_gpu_is_active(kbdev))) {
		up_read(&kbdev->csf.reset.sem);
		return -EFAULT;
	}

	return 0;
}
KBASE_EXPORT_TEST_API(kbase_reset_gpu_prevent_and_wait);

int kbase_reset_gpu_try_prevent(struct kbase_device *kbdev)
{
	if (!down_read_trylock(&kbdev->csf.reset.sem))
		return -EAGAIN;

	if (atomic_read(&kbdev->csf.reset.state) ==
	    KBASE_CSF_RESET_GPU_FAILED) {
		up_read(&kbdev->csf.reset.sem);
		return -ENOMEM;
	}

	if (WARN_ON(kbase_reset_gpu_is_active(kbdev))) {
		up_read(&kbdev->csf.reset.sem);
		return -EFAULT;
	}

	return 0;
}

void kbase_reset_gpu_allow(struct kbase_device *kbdev)
{
	up_read(&kbdev->csf.reset.sem);
}
KBASE_EXPORT_TEST_API(kbase_reset_gpu_allow);

void kbase_reset_gpu_assert_prevented(struct kbase_device *kbdev)
{
#if KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE
	lockdep_assert_held_read(&kbdev->csf.reset.sem);
#else
	lockdep_assert_held(&kbdev->csf.reset.sem);
#endif
	WARN_ON(kbase_reset_gpu_is_active(kbdev));
}

void kbase_reset_gpu_assert_failed_or_prevented(struct kbase_device *kbdev)
{
	if (atomic_read(&kbdev->csf.reset.state) == KBASE_CSF_RESET_GPU_FAILED)
		return;

#if KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE
	lockdep_assert_held_read(&kbdev->csf.reset.sem);
#else
	lockdep_assert_held(&kbdev->csf.reset.sem);
#endif
	WARN_ON(kbase_reset_gpu_is_active(kbdev));
}

/* Mark the reset as now happening, and synchronize with other threads that
 * might be trying to access the GPU
 */
static void kbase_csf_reset_begin_hw_access_sync(
	struct kbase_device *kbdev,
	enum kbase_csf_reset_gpu_state initial_reset_state)
{
	unsigned long hwaccess_lock_flags;
	unsigned long scheduler_spin_lock_flags;

	/* Note this is a WARN/atomic_set because it is a software issue for a
	 * race to be occurring here
	 */
	WARN_ON(!kbase_csf_reset_state_is_committed(initial_reset_state));

	down_write(&kbdev->csf.reset.sem);

	/* Threads in atomic context accessing the HW will hold one of these
	 * locks, so synchronize with them too.
	 */
	spin_lock_irqsave(&kbdev->hwaccess_lock, hwaccess_lock_flags);
	kbase_csf_scheduler_spin_lock(kbdev, &scheduler_spin_lock_flags);
	atomic_set(&kbdev->csf.reset.state, KBASE_RESET_GPU_HAPPENING);
	kbase_csf_scheduler_spin_unlock(kbdev, scheduler_spin_lock_flags);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_lock_flags);
}

/* Mark the reset as finished and allow others threads to once more access the
 * GPU
 */
static void kbase_csf_reset_end_hw_access(struct kbase_device *kbdev,
					  int err_during_reset,
					  bool firmware_inited)
{
	unsigned long hwaccess_lock_flags;
	unsigned long scheduler_spin_lock_flags;

	WARN_ON(!kbase_csf_reset_state_is_active(
		atomic_read(&kbdev->csf.reset.state)));

	/* Once again, we synchronize with atomic context threads accessing the
	 * HW, as otherwise any actions they defer could get lost
	 */
	spin_lock_irqsave(&kbdev->hwaccess_lock, hwaccess_lock_flags);
	kbase_csf_scheduler_spin_lock(kbdev, &scheduler_spin_lock_flags);

	if (!err_during_reset) {
		atomic_set(&kbdev->csf.reset.state,
			   KBASE_CSF_RESET_GPU_NOT_PENDING);
	} else {
		dev_err(kbdev->dev, "Reset failed to complete");
		atomic_set(&kbdev->csf.reset.state, KBASE_CSF_RESET_GPU_FAILED);
	}

	kbase_csf_scheduler_spin_unlock(kbdev, scheduler_spin_lock_flags);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, hwaccess_lock_flags);

	/* Invoke the scheduling tick after formally finishing the reset,
	 * otherwise the tick might start too soon and notice that reset
	 * is still in progress.
	 */
	up_write(&kbdev->csf.reset.sem);
	wake_up(&kbdev->csf.reset.wait);

	if (!err_during_reset && likely(firmware_inited))
		kbase_csf_scheduler_enable_tick_timer(kbdev);
}

static void kbase_csf_debug_dump_registers(struct kbase_device *kbdev)
{
	kbase_io_history_dump(kbdev);

	dev_err(kbdev->dev, "Register state:");
	dev_err(kbdev->dev, "  GPU_IRQ_RAWSTAT=0x%08x   GPU_STATUS=0x%08x  MCU_STATUS=0x%08x",
		kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_RAWSTAT)),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_STATUS)),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(MCU_STATUS)));
	dev_err(kbdev->dev, "  JOB_IRQ_RAWSTAT=0x%08x   MMU_IRQ_RAWSTAT=0x%08x   GPU_FAULTSTATUS=0x%08x",
		kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_RAWSTAT)),
		kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_RAWSTAT)),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_FAULTSTATUS)));
	dev_err(kbdev->dev, "  GPU_IRQ_MASK=0x%08x   JOB_IRQ_MASK=0x%08x   MMU_IRQ_MASK=0x%08x",
		kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK)),
		kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_MASK)),
		kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_MASK)));
	dev_err(kbdev->dev, "  PWR_OVERRIDE0=0x%08x   PWR_OVERRIDE1=0x%08x",
		kbase_reg_read(kbdev, GPU_CONTROL_REG(PWR_OVERRIDE0)),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(PWR_OVERRIDE1)));
	dev_err(kbdev->dev, "  SHADER_CONFIG=0x%08x   L2_MMU_CONFIG=0x%08x   TILER_CONFIG=0x%08x",
		kbase_reg_read(kbdev, GPU_CONTROL_REG(SHADER_CONFIG)),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(L2_MMU_CONFIG)),
		kbase_reg_read(kbdev, GPU_CONTROL_REG(TILER_CONFIG)));
}

static void kbase_csf_dump_firmware_trace_buffer(struct kbase_device *kbdev)
{
	u8 *buf, *line_str;
	unsigned int read_size;
	struct firmware_trace_buffer *tb =
		kbase_csf_firmware_get_trace_buffer(kbdev, FW_TRACE_BUF_NAME);

	if (tb == NULL) {
		dev_dbg(kbdev->dev, "Can't get the trace buffer, firmware trace dump skipped");
		return;
	}

	buf = kmalloc(PAGE_SIZE + DUMP_HEX_CHARS_PER_LINE + 1, GFP_KERNEL);
	if (buf == NULL) {
		dev_err(kbdev->dev, "Short of memory, firmware trace dump skipped");
		return;
	}
	line_str = &buf[PAGE_SIZE];

	dev_err(kbdev->dev, "Firmware trace buffer dump:");
	while ((read_size = kbase_csf_firmware_trace_buffer_read_data(tb, buf,
								PAGE_SIZE))) {
		u64 *ptr = (u64 *)buf;
		u32 num_dwords;

		for (num_dwords = read_size / sizeof(u64);
		     num_dwords >= DUMP_DWORDS_PER_LINE;
		     num_dwords -= DUMP_DWORDS_PER_LINE) {
			dev_err(kbdev->dev, "%016llx %016llx %016llx %016llx",
				ptr[0], ptr[1], ptr[2], ptr[3]);
			ptr += DUMP_DWORDS_PER_LINE;
		}

		if (num_dwords) {
			int pos = 0;

			while (num_dwords--) {
				pos += snprintf(line_str + pos,
						DUMP_HEX_CHARS_PER_DWORD + 1,
						"%016llx ", ptr[0]);
				ptr++;
			}

			dev_err(kbdev->dev, "%s", line_str);
		}
	}

	kfree(buf);
}

/**
 * kbase_csf_hwcnt_on_reset_error() - Sets HWCNT to appropriate state in the
 *                                    event of an error during GPU reset.
 * @kbdev: Pointer to KBase device
 */
static void kbase_csf_hwcnt_on_reset_error(struct kbase_device *kbdev)
{
	unsigned long flags;

	/* Treat this as an unrecoverable error for HWCNT */
	kbase_hwcnt_backend_csf_on_unrecoverable_error(&kbdev->hwcnt_gpu_iface);

	/* Re-enable counters to ensure matching enable/disable pair.
	 * This might reduce the hwcnt disable count to 0, and therefore
	 * trigger actual re-enabling of hwcnt.
	 * However, as the backend is now in the unrecoverable error state,
	 * re-enabling will immediately fail and put the context into the error
	 * state, preventing the hardware from being touched (which could have
	 * risked a hang).
	 */
	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);
}

static int kbase_csf_reset_gpu_now(struct kbase_device *kbdev,
				   bool firmware_inited, bool silent)
{
	unsigned long flags;
	int err;

	WARN_ON(kbdev->irq_reset_flush);
	/* The reset must now be happening otherwise other threads will not
	 * have been synchronized with to stop their access to the HW
	 */
#if KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE
	lockdep_assert_held_write(&kbdev->csf.reset.sem);
#elif KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE
	lockdep_assert_held_exclusive(&kbdev->csf.reset.sem);
#else
	lockdep_assert_held(&kbdev->csf.reset.sem);
#endif
	WARN_ON(!kbase_reset_gpu_is_active(kbdev));

	/* Reset the scheduler state before disabling the interrupts as suspend
	 * of active CSG slots would also be done as a part of reset.
	 */
	if (likely(firmware_inited))
		kbase_csf_scheduler_reset(kbdev);
	cancel_work_sync(&kbdev->csf.firmware_reload_work);

	dev_dbg(kbdev->dev, "Disable GPU hardware counters.\n");
	/* This call will block until counters are disabled.
	 */
	kbase_hwcnt_context_disable(kbdev->hwcnt_gpu_ctx);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	spin_lock(&kbdev->mmu_mask_change);
	kbase_pm_reset_start_locked(kbdev);

	dev_dbg(kbdev->dev,
		"We're about to flush out the IRQs and their bottom halves\n");
	kbdev->irq_reset_flush = true;

	/* Disable IRQ to avoid IRQ handlers to kick in after releasing the
	 * spinlock; this also clears any outstanding interrupts
	 */
	kbase_pm_disable_interrupts_nolock(kbdev);

	spin_unlock(&kbdev->mmu_mask_change);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	dev_dbg(kbdev->dev, "Ensure that any IRQ handlers have finished\n");
	/* Must be done without any locks IRQ handlers will take.
	 */
	kbase_synchronize_irqs(kbdev);

	dev_dbg(kbdev->dev, "Flush out any in-flight work items\n");
	kbase_flush_mmu_wqs(kbdev);

	dev_dbg(kbdev->dev,
		"The flush has completed so reset the active indicator\n");
	kbdev->irq_reset_flush = false;

	mutex_lock(&kbdev->pm.lock);
	if (!silent)
		dev_err(kbdev->dev, "Resetting GPU (allowing up to %d ms)",
								RESET_TIMEOUT);

	/* Output the state of some interesting registers to help in the
	 * debugging of GPU resets, and dump the firmware trace buffer
	 */
	if (!silent) {
		kbase_csf_debug_dump_registers(kbdev);
		if (likely(firmware_inited))
			kbase_csf_dump_firmware_trace_buffer(kbdev);
	}

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_ipa_control_handle_gpu_reset_pre(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* Tell hardware counters a reset is about to occur.
	 * If the backend is in an unrecoverable error state (e.g. due to
	 * firmware being unresponsive) this will transition the backend out of
	 * it, on the assumption a reset will fix whatever problem there was.
	 */
	kbase_hwcnt_backend_csf_on_before_reset(&kbdev->hwcnt_gpu_iface);

	/* Reset the GPU */
	err = kbase_pm_init_hw(kbdev, 0);

	mutex_unlock(&kbdev->pm.lock);

	if (WARN_ON(err)) {
		kbase_csf_hwcnt_on_reset_error(kbdev);
		return err;
	}

	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_ctx_sched_restore_all_as(kbdev);
	kbase_ipa_control_handle_gpu_reset_post(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);

	kbase_pm_enable_interrupts(kbdev);

	mutex_lock(&kbdev->pm.lock);
	kbase_pm_reset_complete(kbdev);
	/* Synchronously wait for the reload of firmware to complete */
	err = kbase_pm_wait_for_desired_state(kbdev);
	mutex_unlock(&kbdev->pm.lock);

	if (WARN_ON(err)) {
		kbase_csf_hwcnt_on_reset_error(kbdev);
		return err;
	}

	/* Re-enable GPU hardware counters */
	kbase_csf_scheduler_spin_lock(kbdev, &flags);
	kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
	kbase_csf_scheduler_spin_unlock(kbdev, flags);

	if (!silent)
		dev_err(kbdev->dev, "Reset complete");

	return 0;
}

static void kbase_csf_reset_gpu_worker(struct work_struct *data)
{
	struct kbase_device *kbdev = container_of(data, struct kbase_device,
						  csf.reset.work);
	bool firmware_inited;
	unsigned long flags;
	int err = 0;
	const enum kbase_csf_reset_gpu_state initial_reset_state =
		atomic_read(&kbdev->csf.reset.state);

	/* Ensure any threads (e.g. executing the CSF scheduler) have finished
	 * using the HW
	 */
	kbase_csf_reset_begin_hw_access_sync(kbdev, initial_reset_state);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	firmware_inited = kbdev->csf.firmware_inited;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (!kbase_pm_context_active_handle_suspend(kbdev,
			KBASE_PM_SUSPEND_HANDLER_DONT_REACTIVATE)) {
		bool silent =
			kbase_csf_reset_state_is_silent(initial_reset_state);

		err = kbase_csf_reset_gpu_now(kbdev, firmware_inited, silent);
		kbase_pm_context_idle(kbdev);
	}

	kbase_disjoint_state_down(kbdev);

	/* Allow other threads to once again use the GPU */
	kbase_csf_reset_end_hw_access(kbdev, err, firmware_inited);
}

bool kbase_prepare_to_reset_gpu(struct kbase_device *kbdev, unsigned int flags)
{
	if (flags & RESET_FLAGS_HWC_UNRECOVERABLE_ERROR)
		kbase_hwcnt_backend_csf_on_unrecoverable_error(
			&kbdev->hwcnt_gpu_iface);

	if (atomic_cmpxchg(&kbdev->csf.reset.state,
			KBASE_CSF_RESET_GPU_NOT_PENDING,
			KBASE_CSF_RESET_GPU_PREPARED) !=
			KBASE_CSF_RESET_GPU_NOT_PENDING)
		/* Some other thread is already resetting the GPU */
		return false;

	return true;
}
KBASE_EXPORT_TEST_API(kbase_prepare_to_reset_gpu);

bool kbase_prepare_to_reset_gpu_locked(struct kbase_device *kbdev,
				       unsigned int flags)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	return kbase_prepare_to_reset_gpu(kbdev, flags);
}

void kbase_reset_gpu(struct kbase_device *kbdev)
{
	/* Note this is a WARN/atomic_set because it is a software issue for
	 * a race to be occurring here
	 */
	if (WARN_ON(atomic_read(&kbdev->csf.reset.state) !=
		    KBASE_RESET_GPU_PREPARED))
		return;

	atomic_set(&kbdev->csf.reset.state, KBASE_CSF_RESET_GPU_COMMITTED);
	dev_err(kbdev->dev, "Preparing to soft-reset GPU\n");

	kbase_disjoint_state_up(kbdev);

	queue_work(kbdev->csf.reset.workq, &kbdev->csf.reset.work);
}
KBASE_EXPORT_TEST_API(kbase_reset_gpu);

void kbase_reset_gpu_locked(struct kbase_device *kbdev)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	kbase_reset_gpu(kbdev);
}

int kbase_reset_gpu_silent(struct kbase_device *kbdev)
{
	if (atomic_cmpxchg(&kbdev->csf.reset.state,
				KBASE_CSF_RESET_GPU_NOT_PENDING,
				KBASE_CSF_RESET_GPU_COMMITTED_SILENT) !=
				KBASE_CSF_RESET_GPU_NOT_PENDING) {
		/* Some other thread is already resetting the GPU */
		return -EAGAIN;
	}

	kbase_disjoint_state_up(kbdev);

	queue_work(kbdev->csf.reset.workq, &kbdev->csf.reset.work);

	return 0;
}

bool kbase_reset_gpu_is_active(struct kbase_device *kbdev)
{
	enum kbase_csf_reset_gpu_state reset_state =
		atomic_read(&kbdev->csf.reset.state);

	/* For CSF, the reset is considered active only when the reset worker
	 * is actually executing and other threads would have to wait for it to
	 * complete
	 */
	return kbase_csf_reset_state_is_active(reset_state);
}

int kbase_reset_gpu_wait(struct kbase_device *kbdev)
{
	const long wait_timeout =
		kbase_csf_timeout_in_jiffies(GPU_RESET_TIMEOUT_MS);
	long remaining;

	/* Inform lockdep we might be trying to wait on a reset (as
	 * would've been done with down_read() - which has no 'timeout'
	 * variant), then use wait_event_timeout() to implement the timed
	 * wait.
	 *
	 * in CONFIG_PROVE_LOCKING builds, this should catch potential 'time
	 * bound' deadlocks such as:
	 * - incorrect lock order with respect to others locks
	 * - current thread has prevented reset
	 * - current thread is executing the reset worker
	 */
	might_lock_read(&kbdev->csf.reset.sem);

	remaining = wait_event_timeout(
		kbdev->csf.reset.wait,
		(atomic_read(&kbdev->csf.reset.state) ==
		 KBASE_CSF_RESET_GPU_NOT_PENDING) ||
			(atomic_read(&kbdev->csf.reset.state) ==
			 KBASE_CSF_RESET_GPU_FAILED),
		wait_timeout);

	if (!remaining) {
		dev_warn(kbdev->dev, "Timed out waiting for the GPU reset to complete");
		return -ETIMEDOUT;
	} else if (atomic_read(&kbdev->csf.reset.state) ==
			KBASE_CSF_RESET_GPU_FAILED) {
		return -ENOMEM;
	}

	return 0;
}
KBASE_EXPORT_TEST_API(kbase_reset_gpu_wait);

int kbase_reset_gpu_init(struct kbase_device *kbdev)
{
	kbdev->csf.reset.workq = alloc_workqueue("Mali reset workqueue", 0, 1);
	if (kbdev->csf.reset.workq == NULL)
		return -ENOMEM;

	INIT_WORK(&kbdev->csf.reset.work, kbase_csf_reset_gpu_worker);

	init_waitqueue_head(&kbdev->csf.reset.wait);
	init_rwsem(&kbdev->csf.reset.sem);

	return 0;
}

void kbase_reset_gpu_term(struct kbase_device *kbdev)
{
	destroy_workqueue(kbdev->csf.reset.workq);
}
