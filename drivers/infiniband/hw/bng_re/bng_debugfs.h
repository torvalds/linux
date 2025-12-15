/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2025 Broadcom.

#ifndef __BNG_RE_DEBUGFS__
#define __BNG_RE_DEBUGFS__

void bng_re_debugfs_add_pdev(struct bng_re_dev *rdev);
void bng_re_debugfs_rem_pdev(struct bng_re_dev *rdev);

void bng_re_register_debugfs(void);
void bng_re_unregister_debugfs(void);
#endif
