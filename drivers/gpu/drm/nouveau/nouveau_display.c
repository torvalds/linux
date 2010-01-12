/*
 * Copyright (C) 2008 Maarten Maathuis.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "drm_crtc_helper.h"
#include "nouveau_drv.h"
#include "nouveau_fb.h"
#include "nouveau_fbcon.h"

static void
nouveau_user_framebuffer_destroy(struct drm_framebuffer *drm_fb)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(drm_fb);
	struct drm_device *dev = drm_fb->dev;

	if (drm_fb->fbdev)
		nouveau_fbcon_remove(dev, drm_fb);

	if (fb->nvbo) {
		mutex_lock(&dev->struct_mutex);
		drm_gem_object_unreference(fb->nvbo->gem);
		mutex_unlock(&dev->struct_mutex);
	}

	drm_framebuffer_cleanup(drm_fb);
	kfree(fb);
}

static int
nouveau_user_framebuffer_create_handle(struct drm_framebuffer *drm_fb,
				       struct drm_file *file_priv,
				       unsigned int *handle)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(drm_fb);

	return drm_gem_handle_create(file_priv, fb->nvbo->gem, handle);
}

static const struct drm_framebuffer_funcs nouveau_framebuffer_funcs = {
	.destroy = nouveau_user_framebuffer_destroy,
	.create_handle = nouveau_user_framebuffer_create_handle,
};

struct drm_framebuffer *
nouveau_framebuffer_create(struct drm_device *dev, struct nouveau_bo *nvbo,
			   struct drm_mode_fb_cmd *mode_cmd)
{
	struct nouveau_framebuffer *fb;
	int ret;

	fb = kzalloc(sizeof(struct nouveau_framebuffer), GFP_KERNEL);
	if (!fb)
		return NULL;

	ret = drm_framebuffer_init(dev, &fb->base, &nouveau_framebuffer_funcs);
	if (ret) {
		kfree(fb);
		return NULL;
	}

	drm_helper_mode_fill_fb_struct(&fb->base, mode_cmd);

	fb->nvbo = nvbo;
	return &fb->base;
}

static struct drm_framebuffer *
nouveau_user_framebuffer_create(struct drm_device *dev,
				struct drm_file *file_priv,
				struct drm_mode_fb_cmd *mode_cmd)
{
	struct drm_framebuffer *fb;
	struct drm_gem_object *gem;

	gem = drm_gem_object_lookup(dev, file_priv, mode_cmd->handle);
	if (!gem)
		return NULL;

	fb = nouveau_framebuffer_create(dev, nouveau_gem_object(gem), mode_cmd);
	if (!fb) {
		drm_gem_object_unreference(gem);
		return NULL;
	}

	return fb;
}

const struct drm_mode_config_funcs nouveau_mode_config_funcs = {
	.fb_create = nouveau_user_framebuffer_create,
	.fb_changed = nouveau_fbcon_probe,
};

