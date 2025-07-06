/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_BO_EVICT_H_
#define _XE_BO_EVICT_H_

struct xe_device;

int xe_bo_evict_all(struct xe_device *xe);
int xe_bo_evict_all_user(struct xe_device *xe);
int xe_bo_notifier_prepare_all_pinned(struct xe_device *xe);
void xe_bo_notifier_unprepare_all_pinned(struct xe_device *xe);
int xe_bo_restore_early(struct xe_device *xe);
int xe_bo_restore_late(struct xe_device *xe);

void xe_bo_pci_dev_remove_all(struct xe_device *xe);

int xe_bo_pinned_init(struct xe_device *xe);
#endif
