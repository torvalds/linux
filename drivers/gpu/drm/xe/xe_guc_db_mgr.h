/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GUC_DB_MGR_H_
#define _XE_GUC_DB_MGR_H_

struct drm_printer;
struct xe_guc_db_mgr;

int xe_guc_db_mgr_init(struct xe_guc_db_mgr *dbm, unsigned int count);

int xe_guc_db_mgr_reserve_id_locked(struct xe_guc_db_mgr *dbm);
void xe_guc_db_mgr_release_id_locked(struct xe_guc_db_mgr *dbm, unsigned int id);

int xe_guc_db_mgr_reserve_range(struct xe_guc_db_mgr *dbm, unsigned int count, unsigned int spare);
void xe_guc_db_mgr_release_range(struct xe_guc_db_mgr *dbm, unsigned int start, unsigned int count);

void xe_guc_db_mgr_print(struct xe_guc_db_mgr *dbm, struct drm_printer *p, int indent);

#endif
