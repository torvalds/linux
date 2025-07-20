/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */
#ifndef _XE_CONFIGFS_H_
#define _XE_CONFIGFS_H_

#include <linux/types.h>

struct pci_dev;

#if IS_ENABLED(CONFIG_CONFIGFS_FS)
int xe_configfs_init(void);
void xe_configfs_exit(void);
bool xe_configfs_get_survivability_mode(struct pci_dev *pdev);
void xe_configfs_clear_survivability_mode(struct pci_dev *pdev);
#else
static inline int xe_configfs_init(void) { return 0; };
static inline void xe_configfs_exit(void) {};
static inline bool xe_configfs_get_survivability_mode(struct pci_dev *pdev) { return false; };
static inline void xe_configfs_clear_survivability_mode(struct pci_dev *pdev) {};
#endif

#endif
