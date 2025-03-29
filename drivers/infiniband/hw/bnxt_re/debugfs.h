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

#define CC_CONFIG_GEN_EXT(x, y)	(((x) << 16) | (y))
#define CC_CONFIG_GEN0_EXT0  CC_CONFIG_GEN_EXT(0, 0)

#define BNXT_RE_CC_PARAM_GEN0  14

struct bnxt_re_cc_param {
	struct bnxt_re_dev *rdev;
	struct dentry *dentry;
	u32 offset;
	u8 cc_gen;
};

struct bnxt_re_dbg_cc_config_params {
	struct bnxt_re_cc_param	gen0_parms[BNXT_RE_CC_PARAM_GEN0];
};
#endif
