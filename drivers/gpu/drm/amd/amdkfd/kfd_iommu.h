/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2018-2022 Advanced Micro Devices, Inc.
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

#ifndef __KFD_IOMMU_H__
#define __KFD_IOMMU_H__

#include <linux/kconfig.h>

#if IS_REACHABLE(CONFIG_AMD_IOMMU_V2)

#define KFD_SUPPORT_IOMMU_V2

int kfd_iommu_check_device(struct kfd_dev *kfd);
int kfd_iommu_device_init(struct kfd_dev *kfd);

int kfd_iommu_bind_process_to_device(struct kfd_process_device *pdd);
void kfd_iommu_unbind_process(struct kfd_process *p);

void kfd_iommu_suspend(struct kfd_dev *kfd);
int kfd_iommu_resume(struct kfd_dev *kfd);

int kfd_iommu_add_perf_counters(struct kfd_topology_device *kdev);

#else

static inline int kfd_iommu_check_device(struct kfd_dev *kfd)
{
	return -ENODEV;
}
static inline int kfd_iommu_device_init(struct kfd_dev *kfd)
{
#if IS_MODULE(CONFIG_AMD_IOMMU_V2)
	WARN_ONCE(1, "iommu_v2 module is not usable by built-in KFD");
#endif
	return 0;
}

static inline int kfd_iommu_bind_process_to_device(
	struct kfd_process_device *pdd)
{
	return 0;
}
static inline void kfd_iommu_unbind_process(struct kfd_process *p)
{
	/* empty */
}

static inline void kfd_iommu_suspend(struct kfd_dev *kfd)
{
	/* empty */
}
static inline int kfd_iommu_resume(struct kfd_dev *kfd)
{
	return 0;
}

static inline int kfd_iommu_add_perf_counters(struct kfd_topology_device *kdev)
{
	return 0;
}

#endif /* IS_REACHABLE(CONFIG_AMD_IOMMU_V2) */

#endif /* __KFD_IOMMU_H__ */
