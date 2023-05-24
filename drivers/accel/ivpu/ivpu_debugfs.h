/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#ifndef __IVPU_DEBUGFS_H__
#define __IVPU_DEBUGFS_H__

struct drm_minor;

void ivpu_debugfs_init(struct drm_minor *minor);

#endif /* __IVPU_DEBUGFS_H__ */
