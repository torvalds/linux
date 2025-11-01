/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_DEVLINK_H_
#define _BNGE_DEVLINK_H_

enum bnge_dl_version_type {
	BNGE_VERSION_FIXED,
	BNGE_VERSION_RUNNING,
	BNGE_VERSION_STORED,
};

void bnge_devlink_free(struct bnge_dev *bd);
struct bnge_dev *bnge_devlink_alloc(struct pci_dev *pdev);
void bnge_devlink_register(struct bnge_dev *bd);
void bnge_devlink_unregister(struct bnge_dev *bd);

#endif /* _BNGE_DEVLINK_H_ */
