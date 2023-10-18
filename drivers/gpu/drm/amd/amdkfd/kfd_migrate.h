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

#if IS_ENABLED(CONFIG_HSA_AMD_SVM)

#include <linux/rwsem.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched/mm.h>
#include <linux/hmm.h>
#include "kfd_priv.h"
#include "kfd_svm.h"

enum MIGRATION_COPY_DIR {
	FROM_RAM_TO_VRAM = 0,
	FROM_VRAM_TO_RAM
};

int svm_migrate_to_vram(struct svm_range *prange,  uint32_t best_loc,
			unsigned long start, unsigned long last,
			struct mm_struct *mm, uint32_t trigger);

int svm_migrate_vram_to_ram(struct svm_range *prange, struct mm_struct *mm,
			unsigned long start, unsigned long last,
			uint32_t trigger, struct page *fault_page);

unsigned long
svm_migrate_addr_to_pfn(struct amdgpu_device *adev, unsigned long addr);

#endif /* IS_ENABLED(CONFIG_HSA_AMD_SVM) */

#endif /* KFD_MIGRATE_H_ */
