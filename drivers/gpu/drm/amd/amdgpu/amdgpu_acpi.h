/* SPDX-License-Identifier: GPL-2.0 OR MIT
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __AMDGPU_ACPI_H__
#define __AMDGPU_ACPI_H__

#include <linux/types.h>
#include <linux/mutex_types.h>

struct amdgpu_device;
struct acpi_device;
struct amdgpu_dm_backlight_caps;

#define MAX_UMA_OPTION_NAME	28
#define MAX_UMA_OPTION_ENTRIES	19

#define AMDGPU_UMA_FLAG_AUTO	BIT(1)
#define AMDGPU_UMA_FLAG_CUSTOM	BIT(0)

/* ATCS Device/Driver State */
#define AMDGPU_ATCS_PSC_DEV_STATE_D0		0
#define AMDGPU_ATCS_PSC_DEV_STATE_D3_HOT	3
#define AMDGPU_ATCS_PSC_DRV_STATE_OPR		0
#define AMDGPU_ATCS_PSC_DRV_STATE_NOT_OPR	1

enum amdgpu_ss {
	AMDGPU_SS_DRV_LOAD,
	AMDGPU_SS_DEV_D0,
	AMDGPU_SS_DEV_D3,
	AMDGPU_SS_DRV_UNLOAD
};

/**
 * struct amdgpu_uma_carveout_option - single UMA carveout option
 * @name: Name of the carveout option
 * @memory_carved_mb: Amount of memory carved in MB
 * @flags: ATCS flags supported by this option
 */
struct amdgpu_uma_carveout_option {
	char name[MAX_UMA_OPTION_NAME];
	uint32_t memory_carved_mb;
	uint8_t flags;
};

/**
 * struct amdgpu_uma_carveout_info - table of available UMA carveout options
 * @num_entries: Number of available options
 * @uma_option_index: The index of the option currently applied
 * @update_lock: Lock to serialize changes to the option
 * @entries: The array of carveout options
 */
struct amdgpu_uma_carveout_info {
	uint8_t num_entries;
	uint8_t uma_option_index;
	struct mutex update_lock;
	struct amdgpu_uma_carveout_option entries[MAX_UMA_OPTION_ENTRIES];
};

struct amdgpu_numa_info {
	uint64_t size;
	int pxm;
	int nid;
};

#if defined(CONFIG_ACPI)
int amdgpu_acpi_init(struct amdgpu_device *adev);
void amdgpu_acpi_fini(struct amdgpu_device *adev);
bool amdgpu_acpi_is_pcie_performance_request_supported(struct amdgpu_device *adev);
bool amdgpu_acpi_is_power_shift_control_supported(void);
bool amdgpu_acpi_is_set_uma_allocation_size_supported(void);
int amdgpu_acpi_pcie_performance_request(struct amdgpu_device *adev,
						u8 perf_req, bool advertise);
int amdgpu_acpi_power_shift_control(struct amdgpu_device *adev,
				    u8 dev_state, bool drv_state);
int amdgpu_acpi_smart_shift_update(struct amdgpu_device *adev,
				   enum amdgpu_ss ss_state);
int amdgpu_acpi_set_uma_allocation_size(struct amdgpu_device *adev, u8 index, u8 type);
int amdgpu_acpi_pcie_notify_device_ready(struct amdgpu_device *adev);
int amdgpu_acpi_get_tmr_info(struct amdgpu_device *adev, u64 *tmr_offset,
			     u64 *tmr_size);
int amdgpu_acpi_get_mem_info(struct amdgpu_device *adev, int xcc_id,
			     struct amdgpu_numa_info *numa_info);

void amdgpu_acpi_get_backlight_caps(struct amdgpu_dm_backlight_caps *caps);
bool amdgpu_acpi_should_gpu_reset(struct amdgpu_device *adev);
void amdgpu_acpi_detect(void);
void amdgpu_acpi_release(void);
#else
static inline int amdgpu_acpi_init(struct amdgpu_device *adev) { return 0; }
static inline int amdgpu_acpi_get_tmr_info(struct amdgpu_device *adev,
					   u64 *tmr_offset, u64 *tmr_size)
{
	return -EINVAL;
}
static inline int amdgpu_acpi_get_mem_info(struct amdgpu_device *adev,
					   int xcc_id,
					   struct amdgpu_numa_info *numa_info)
{
	return -EINVAL;
}
static inline void amdgpu_acpi_fini(struct amdgpu_device *adev) { }
static inline bool amdgpu_acpi_should_gpu_reset(struct amdgpu_device *adev) { return false; }
static inline void amdgpu_acpi_detect(void) { }
static inline void amdgpu_acpi_release(void) { }
static inline bool amdgpu_acpi_is_power_shift_control_supported(void) { return false; }
static inline bool amdgpu_acpi_is_set_uma_allocation_size_supported(void) { return false; }
static inline int amdgpu_acpi_power_shift_control(struct amdgpu_device *adev,
						  u8 dev_state, bool drv_state) { return 0; }
static inline int amdgpu_acpi_smart_shift_update(struct amdgpu_device *adev,
						 enum amdgpu_ss ss_state)
{
	return 0;
}
static inline int amdgpu_acpi_set_uma_allocation_size(struct amdgpu_device *adev, u8 index, u8 type)
{
	return -EINVAL;
}
static inline void amdgpu_acpi_get_backlight_caps(struct amdgpu_dm_backlight_caps *caps) { }
#endif

#if defined(CONFIG_ACPI) && defined(CONFIG_SUSPEND)
bool amdgpu_acpi_is_s3_active(struct amdgpu_device *adev);
bool amdgpu_acpi_is_s0ix_active(struct amdgpu_device *adev);
#else
static inline bool amdgpu_acpi_is_s0ix_active(struct amdgpu_device *adev) { return false; }
static inline bool amdgpu_acpi_is_s3_active(struct amdgpu_device *adev) { return false; }
#endif

#if defined(CONFIG_DRM_AMD_ISP)
int amdgpu_acpi_get_isp4_dev(struct acpi_device **dev);
#endif
#endif /* __AMDGPU_ACPI_H__ */
