/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_REGION_LMEM_H
#define __INTEL_REGION_LMEM_H

struct intel_gt;

struct intel_memory_region *
intel_gt_setup_fake_lmem(struct intel_gt *gt);

#endif /* !__INTEL_REGION_LMEM_H */
