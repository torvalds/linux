/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include "runtime.h"

#ifdef CONFIG_IWLWIFI_DEBUGFS
void iwl_fwrt_dbgfs_register(struct iwl_fw_runtime *fwrt,
			    struct dentry *dbgfs_dir);

#else
static inline void iwl_fwrt_dbgfs_register(struct iwl_fw_runtime *fwrt,
					   struct dentry *dbgfs_dir) { }

#endif /* CONFIG_IWLWIFI_DEBUGFS */
