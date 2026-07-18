/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */
#ifndef _AMDXDNA_DEBUGFS_H_
#define _AMDXDNA_DEBUGFS_H_

#include "amdxdna_pci_drv.h"

#if defined(CONFIG_DEBUG_FS)
void amdxdna_debugfs_init(struct amdxdna_dev *xdna);
#else
static inline void amdxdna_debugfs_init(struct amdxdna_dev *xdna)
{
}
#endif /* CONFIG_DEBUG_FS */

#endif
