/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-2021 Intel Corporation.
 */

#ifndef IOSM_IPC_DEBUGFS_H
#define IOSM_IPC_DEBUGFS_H

#ifdef CONFIG_WWAN_DEBUGFS
void ipc_debugfs_init(struct iosm_imem *ipc_imem);
void ipc_debugfs_deinit(struct iosm_imem *ipc_imem);
#else
static inline void ipc_debugfs_init(struct iosm_imem *ipc_imem) {}
static inline void ipc_debugfs_deinit(struct iosm_imem *ipc_imem) {}
#endif

#endif
