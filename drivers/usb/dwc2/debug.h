/* SPDX-License-Identifier: GPL-2.0 */
/*
 * debug.h - Designware USB2 DRD controller debug header
 *
 * Copyright (C) 2015 Intel Corporation
 * Mian Yousaf Kaukab <yousaf.kaukab@intel.com>
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
