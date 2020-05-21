/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Linaro Ltd. */

#ifndef __VENUS_DBGFS_H__
#define __VENUS_DBGFS_H__

struct venus_core;

void venus_dbgfs_init(struct venus_core *core);
void venus_dbgfs_deinit(struct venus_core *core);

#endif
