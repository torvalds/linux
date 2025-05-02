/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * FacetimeHD camera driver
 *
 * Copyright (C) 2015 Sven Schnelle <svens@stackframe.org>
 *
 */

#ifndef _FTHD_SYSFS_H
#define _FTHD_SYSFS_H

struct fthd_private;

int fthd_debugfs_init(struct fthd_private *priv);
void fthd_debugfs_exit(struct fthd_private *priv);
#endif
