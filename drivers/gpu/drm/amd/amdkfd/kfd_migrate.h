/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2020-2021 Advanced Micro Devices, Inc.
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
 *
 */

#ifndef KFD_MIGRATE_H_
#define KFD_MIGRATE_H_

#include <linux/rwsem.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched/mm.h>
#include <linux/hmm.h>
#include "kfd_priv.h"
#include "kfd_svm.h"

#if defined(CONFIG_DEVICE_PRIVATE)
int svm_migrate_init(struct amdgpu_device *adev);
void svm_migrate_fini(struct amdgpu_device *adev);

#else
static inline int svm_migrate_init(struct amdgpu_device *adev)
{
	DRM_WARN_ONCE("DEVICE_PRIVATE kernel config option is not enabled, "
		      "add CONFIG_DEVICE_PRIVATE=y in config file to fix\n");
	return -ENODEV;
}
static inline void svm_migrate_fini(struct amdgpu_device *adev) {}
#endif
#endif /* KFD_MIGRATE_H_ */
