// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <linux/kvm_host.h>

#include <asm/kvm_hyp.h>

size_t __ro_after_init				kvm_hyp_nr_s2mpus;
struct s2mpu __ro_after_init			*kvm_hyp_s2mpus;

const struct kvm_iommu_ops kvm_s2mpu_ops = (struct kvm_iommu_ops){};
