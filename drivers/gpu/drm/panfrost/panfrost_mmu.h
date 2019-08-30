/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */

#ifndef __PANFROST_MMU_H__
#define __PANFROST_MMU_H__

struct panfrost_gem_object;

int panfrost_mmu_map(struct panfrost_gem_object *bo);
void panfrost_mmu_unmap(struct panfrost_gem_object *bo);

int panfrost_mmu_init(struct panfrost_device *pfdev);
void panfrost_mmu_fini(struct panfrost_device *pfdev);

void panfrost_mmu_enable(struct panfrost_device *pfdev, u32 as_nr);

#endif
