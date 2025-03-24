// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Copyright (c) 2024, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * Description: Debugfs header
 */

#ifndef __BNXT_RE_DEBUGFS__
#define __BNXT_RE_DEBUGFS__

void bnxt_re_debug_add_qpinfo(struct bnxt_re_dev *rdev, struct bnxt_re_qp *qp);
void bnxt_re_debug_rem_qpinfo(struct bnxt_re_dev *rdev, struct bnxt_re_qp *qp);

void bnxt_re_debugfs_add_pdev(struct bnxt_re_dev *rdev);
void bnxt_re_debugfs_rem_pdev(struct bnxt_re_dev *rdev);

void bnxt_re_register_debugfs(void);
void bnxt_re_unregister_debugfs(void);

#endif
