/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef AMD_IOMMU_H
#define AMD_IOMMU_H

int __init add_special_device(u8 type, u8 id, u16 *devid, bool cmd_line);

#ifdef CONFIG_DMI
void amd_iommu_apply_ivrs_quirks(void);
#else
static void amd_iommu_apply_ivrs_quirks(void) { }
#endif

#endif
