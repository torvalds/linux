/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014, Patrik Jakobsson
 * All Rights Reserved.
 *
 * Authors: Patrik Jakobsson <patrik.r.jakobsson@gmail.com>
 */

#ifndef __BLITTER_H
#define __BLITTER_H

struct drm_psb_private;

extern int gma_blt_wait_idle(struct drm_psb_private *dev_priv);

#endif
