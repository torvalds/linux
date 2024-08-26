/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DebugFS interface for the NVMe target.
 * Copyright (c) 2022-2024 Shadow
 * Copyright (c) 2024 SUSE LLC
 */
#ifndef NVMET_DEBUGFS_H
#define NVMET_DEBUGFS_H

#include <linux/types.h>

#ifdef CONFIG_NVME_TARGET_DEBUGFS
int nvmet_debugfs_subsys_setup(struct nvmet_subsys *subsys);
void nvmet_debugfs_subsys_free(struct nvmet_subsys *subsys);
int nvmet_debugfs_ctrl_setup(struct nvmet_ctrl *ctrl);
void nvmet_debugfs_ctrl_free(struct nvmet_ctrl *ctrl);

int __init nvmet_init_debugfs(void);
void nvmet_exit_debugfs(void);
#else
static inline int nvmet_debugfs_subsys_setup(struct nvmet_subsys *subsys)
{
	return 0;
}
static inline void nvmet_debugfs_subsys_free(struct nvmet_subsys *subsys){}

static inline int nvmet_debugfs_ctrl_setup(struct nvmet_ctrl *ctrl)
{
	return 0;
}
static inline void nvmet_debugfs_ctrl_free(struct nvmet_ctrl *ctrl) {}

static inline int __init nvmet_init_debugfs(void)
{
    return 0;
}

static inline void nvmet_exit_debugfs(void) {}

#endif

#endif /* NVMET_DEBUGFS_H */
