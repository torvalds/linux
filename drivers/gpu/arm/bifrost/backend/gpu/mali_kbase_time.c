// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2014-2016, 2018-2022 ARM Limited. All rights reserved.
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
#include <mali_kbase_hwaccess_time.h>
#if MALI_USE_CSF
#include <csf/mali_kbase_csf_timeout.h>
#endif
#include <device/mali_kbase_device.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <mali_kbase_config_defaults.h>

void kbase_backend_get_gpu_time_norequest(struct kbase_device *kbdev,
					  u64 *cycle_counter,
					  u64 *system_time,
					  struct timespec64 *ts)
{
	u32 hi1, hi2;

	if (cycle_counter)
		*cycle_counter = kbase_backend_get_cycle_cnt(kbdev);

	if (system_time) {
		/* Read hi, lo, hi to ensure a coherent u64 */
		do {
			hi1 = kbase_reg_read(kbdev,
					     GPU_CONTROL_REG(TIMESTAMP_HI));
			*system_time = kbase_reg_read(kbdev,
					     GPU_CONTROL_REG(TIMESTAMP_LO));
			hi2 = kbase_reg_read(kbdev,
					     GPU_CONTROL_REG(TIMESTAMP_HI));
		} while (hi1 != hi2);
		*system_time |= (((u64) hi1) << 32);
	}

	/* Record the CPU's idea of current time */
	if (ts != NULL)
#if (KERNEL_VERSION(4, 17, 0) > LINUX_VERSION_CODE)
		*ts = ktime_to_timespec64(ktime_get_raw());
#else
		ktime_get_raw_ts64(ts);
#endif
}

#if !MALI_USE_CSF
/**
 * timedwait_cycle_count_active() - Timed wait till CYCLE_COUNT_ACTIVE is active
 *
 * @kbdev: Kbase device
 *
 * Return: true if CYCLE_COUNT_ACTIVE is active within the timeout.
 */
static bool timedwait_cycle_count_active(struct kbase_device *kbdev)
{
#if IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
	return true;
#else
	bool success = false;
	const unsigned int timeout = 100;
	const unsigned long remaining = jiffies + msecs_to_jiffies(timeout);

	while (time_is_after_jiffies(remaining)) {
		if ((kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_STATUS)) &
		     GPU_STATUS_CYCLE_COUNT_ACTIVE)) {
			success = true;
			break;
		}
	}
	return success;
#endif
}
#endif

void kbase_backend_get_gpu_time(struct kbase_device *kbdev, u64 *cycle_counter,
				u64 *system_time, struct timespec64 *ts)
{
#if !MALI_USE_CSF
	kbase_pm_request_gpu_cycle_counter(kbdev);
	WARN_ONCE(kbdev->pm.backend.l2_state != KBASE_L2_ON,
		  "L2 not powered up");
	WARN_ONCE((!timedwait_cycle_count_active(kbdev)),
		  "Timed out on CYCLE_COUNT_ACTIVE");
#endif
	kbase_backend_get_gpu_time_norequest(kbdev, cycle_counter, system_time,
					     ts);
#if !MALI_USE_CSF
	kbase_pm_release_gpu_cycle_counter(kbdev);
#endif
}

unsigned int kbase_get_timeout_ms(struct kbase_device *kbdev,
				  enum kbase_timeout_selector selector)
{
	/* Timeout calculation:
	 * dividing number of cycles by freq in KHz automatically gives value
	 * in milliseconds. nr_cycles will have to be multiplied by 1e3 to
	 * get result in microseconds, and 1e6 to get result in nanoseconds.
	 */

	u64 timeout, nr_cycles = 0;
	/* Default value to mean 'no cap' */
	u64 timeout_cap = U64_MAX;
	u64 freq_khz;

	/* Only for debug messages, safe default in case it's mis-maintained */
	const char *selector_str = "(unknown)";

	if (WARN(!kbdev->lowest_gpu_freq_khz,
		 "Lowest frequency uninitialized! Using reference frequency for scaling")) {
		freq_khz = DEFAULT_REF_TIMEOUT_FREQ_KHZ;
	} else {
		freq_khz = kbdev->lowest_gpu_freq_khz;
	}

	switch (selector) {
	case KBASE_TIMEOUT_SELECTOR_COUNT:
	default:
#if !MALI_USE_CSF
		WARN(1, "Invalid timeout selector used! Using default value");
		nr_cycles = JM_DEFAULT_TIMEOUT_CYCLES;
		break;
#else
		/* Use Firmware timeout if invalid selection */
		WARN(1,
		     "Invalid timeout selector used! Using CSF Firmware timeout");
		fallthrough;
	case CSF_FIRMWARE_TIMEOUT:
		selector_str = "CSF_FIRMWARE_TIMEOUT";
		nr_cycles = CSF_FIRMWARE_TIMEOUT_CYCLES;
		/* Setup a cap on CSF FW timeout to FIRMWARE_PING_INTERVAL_MS,
		 * if calculated timeout exceeds it. This should be adapted to
		 * a direct timeout comparison once the
		 * FIRMWARE_PING_INTERVAL_MS option is added to this timeout
		 * function. A compile-time check such as BUILD_BUG_ON can also
		 * be done once the firmware ping interval in cycles becomes
		 * available as a macro.
		 */
		timeout_cap = FIRMWARE_PING_INTERVAL_MS;
		break;
	case CSF_PM_TIMEOUT:
		selector_str = "CSF_PM_TIMEOUT";
		nr_cycles = CSF_PM_TIMEOUT_CYCLES;
		break;
	case CSF_GPU_RESET_TIMEOUT:
		selector_str = "CSF_GPU_RESET_TIMEOUT";
		nr_cycles = CSF_GPU_RESET_TIMEOUT_CYCLES;
		break;
	case CSF_CSG_SUSPEND_TIMEOUT:
		selector_str = "CSF_CSG_SUSPEND_TIMEOUT";
		nr_cycles = CSF_CSG_SUSPEND_TIMEOUT_CYCLES;
		break;
	case CSF_FIRMWARE_BOOT_TIMEOUT:
		selector_str = "CSF_FIRMWARE_BOOT_TIMEOUT";
		nr_cycles = CSF_FIRMWARE_BOOT_TIMEOUT_CYCLES;
		break;
	case CSF_SCHED_PROTM_PROGRESS_TIMEOUT:
		selector_str = "CSF_SCHED_PROTM_PROGRESS_TIMEOUT";
		nr_cycles = kbase_csf_timeout_get(kbdev);
		break;
#endif
	}

	timeout = div_u64(nr_cycles, freq_khz);
	if (timeout > timeout_cap) {
		dev_dbg(kbdev->dev, "Capped %s %llu to %llu", selector_str,
			(unsigned long long)timeout, (unsigned long long)timeout_cap);
		timeout = timeout_cap;
	}
	if (WARN(timeout > UINT_MAX,
		 "Capping excessive timeout %llums for %s at freq %llukHz to UINT_MAX ms",
		 (unsigned long long)timeout, selector_str, (unsigned long long)freq_khz))
		timeout = UINT_MAX;
	return (unsigned int)timeout;
}
KBASE_EXPORT_TEST_API(kbase_get_timeout_ms);

u64 kbase_backend_get_cycle_cnt(struct kbase_device *kbdev)
{
	u32 hi1, hi2, lo;

	/* Read hi, lo, hi to ensure a coherent u64 */
	do {
		hi1 = kbase_reg_read(kbdev,
					GPU_CONTROL_REG(CYCLE_COUNT_HI));
		lo = kbase_reg_read(kbdev,
					GPU_CONTROL_REG(CYCLE_COUNT_LO));
		hi2 = kbase_reg_read(kbdev,
					GPU_CONTROL_REG(CYCLE_COUNT_HI));
	} while (hi1 != hi2);

	return lo | (((u64) hi1) << 32);
}
