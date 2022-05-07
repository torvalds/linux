/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_FB_H__
#define __VS_FB_H__

struct vs_gem_object *vs_fb_get_gem_obj(struct drm_framebuffer *fb,
					unsigned char plane);

void vs_mode_config_init(struct drm_device *dev);
#endif /* __VS_FB_H__ */
