// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <linux/kvm_host.h>

int pkvm_iommu_driver_init(enum pkvm_iommu_driver_id id, void *data, size_t size)
{
	return kvm_call_hyp_nvhe(__pkvm_iommu_driver_init, id, data, size);
}
