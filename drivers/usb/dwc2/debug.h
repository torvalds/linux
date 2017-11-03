// SPDX-License-Identifier: GPL-2.0
/**
 * debug.h - Designware USB2 DRD controller debug header
 *
 * Copyright (C) 2015 Intel Corporation
 * Mian Yousaf Kaukab <yousaf.kaukab@intel.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "core.h"

#ifdef CONFIG_DEBUG_FS
int dwc2_debugfs_init(struct dwc2_hsotg *hsotg);
void dwc2_debugfs_exit(struct dwc2_hsotg *hsotg);
#else
static inline int dwc2_debugfs_init(struct dwc2_hsotg *hsotg)
{  return 0;  }
static inline void dwc2_debugfs_exit(struct dwc2_hsotg *hsotg)
{  }
#endif
