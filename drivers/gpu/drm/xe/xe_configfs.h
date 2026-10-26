/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */
#ifndef _XE_CONFIGFS_H_
#define _XE_CONFIGFS_H_

#include <linux/limits.h>
#include <linux/types.h>

#include "xe_defaults.h"
#include "xe_hw_engine_types.h"
#include "xe_module.h"

struct pci_dev;

int xe_configfs_init(void);
void xe_configfs_exit(void);
void xe_configfs_check_device(struct pci_dev *pdev);
bool xe_configfs_get_survivability_mode(struct pci_dev *pdev);
bool xe_configfs_primary_gt_allowed(struct pci_dev *pdev);
bool xe_configfs_media_gt_allowed(struct pci_dev *pdev);
u64 xe_configfs_get_engines_allowed(struct pci_dev *pdev);
bool xe_configfs_get_psmi_enabled(struct pci_dev *pdev);
bool xe_configfs_get_enable_multi_queue(struct pci_dev *pdev);
u32 xe_configfs_get_migrate_ulls_period_ms(struct pci_dev *pdev);
bool xe_configfs_get_disable_vram_page_offline(struct pci_dev *pdev);
u32 xe_configfs_get_ctx_restore_mid_bb(struct pci_dev *pdev,
				       enum xe_engine_class class,
				       const u32 **cs);
u32 xe_configfs_get_ctx_restore_post_bb(struct pci_dev *pdev,
					enum xe_engine_class class,
					const u32 **cs);
#ifdef CONFIG_PCI_IOV
unsigned int xe_configfs_get_max_vfs(struct pci_dev *pdev);
bool xe_configfs_admin_only_pf(struct pci_dev *pdev);
#endif

#endif
