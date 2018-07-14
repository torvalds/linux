/* SPDX-License-Identifier: GPL-2.0 */
/*
 * intel-pasid.h - PASID idr, table and entry header
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */

#ifndef __INTEL_PASID_H
#define __INTEL_PASID_H

#define PASID_MIN			0x1
#define PASID_MAX			0x100000

extern u32 intel_pasid_max_id;
int intel_pasid_alloc_id(void *ptr, int start, int end, gfp_t gfp);
void intel_pasid_free_id(int pasid);
void *intel_pasid_lookup_id(int pasid);

#endif /* __INTEL_PASID_H */
