// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <linux/kvm_host.h>

#include <asm/kvm_hyp.h>

const struct kvm_iommu_ops kvm_s2mpu_ops = (struct kvm_iommu_ops){};
