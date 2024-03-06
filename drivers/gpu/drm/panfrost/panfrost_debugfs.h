/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2023 Collabora ltd.
 * Copyright 2023 Amazon.com, Inc. or its affiliates.
 */

#ifndef PANFROST_DEBUGFS_H
#define PANFROST_DEBUGFS_H

#ifdef CONFIG_DEBUG_FS
void panfrost_debugfs_init(struct drm_minor *minor);
#endif

#endif  /* PANFROST_DEBUGFS_H */
