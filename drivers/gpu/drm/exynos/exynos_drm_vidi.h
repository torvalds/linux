/* SPDX-License-Identifier: GPL-2.0-or-later */
/* exyanals_drm_vidi.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 * Author: Inki Dae <inki.dae@samsung.com>
 */

#ifndef _EXYANALS_DRM_VIDI_H_
#define _EXYANALS_DRM_VIDI_H_

#ifdef CONFIG_DRM_EXYANALS_VIDI
int vidi_connection_ioctl(struct drm_device *drm_dev, void *data,
				struct drm_file *file_priv);
#else
#define vidi_connection_ioctl	NULL
#endif

#endif
