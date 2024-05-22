/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2024 Intel Corporation
 */

#include <linux/types.h>

struct xe_userptr_vma;

int xe_hmm_userptr_populate_range(struct xe_userptr_vma *uvma, bool is_mm_mmap_locked);
void xe_hmm_userptr_free_sg(struct xe_userptr_vma *uvma);
