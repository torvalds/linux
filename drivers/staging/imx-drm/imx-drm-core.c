/*
 * Freescale i.MX drm driver
 *
 * Copyright (C) 2011 Sascha Hauer, Pengutronix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/component.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>

#include "imx-drm.h"

#define MAX_CRTC	4

struct crtc_cookie {
	void *cookie;
	int id;
	struct list_head list;
};

struct imx_drm_crtc;

struct imx_drm_device {
	struct drm_device			*drm;
	struct device				*dev;
	struct imx_drm_crtc			*crtc[MAX_CRTC];
	struct list_head			encoder_list;
	struct list_head			connector_list;
	struct mutex				mutex;
	int					pipes;
	struct drm_fbdev_cma			*fbhelper;
};

struct imx_drm_crtc {
	struct drm_crtc				*crtc;
	struct imx_drm_device			*imxdrm;
	int					pipe;
	struct imx_drm_crtc_helper_funcs	imx_drm_helper_funcs;
	struct module				*owner;
	struct crtc_cookie			cookie;
	int					mux_id;
};

struct imx_drm_encoder {
	struct drm_encoder			*encoder;
	struct list_head			list;
	struct module				*owner;
	struct list_head			possible_crtcs;
};

struct imx_drm_connector {
	struct drm_connector			*connector;
	struct list_head			list;
	struct module				*owner;
};

static struct imx_drm_device *__imx_drm_device(void);

int imx_drm_crtc_id(struct imx_drm_crtc *crtc)
{
	return crtc->pipe;
}
EXPORT_SYMBOL_GPL(imx_drm_crtc_id);

static void imx_drm_driver_lastclose(struct drm_device *drm)
{
	struct imx_drm_device *imxdrm = drm->dev_private;

	if (imxdrm->fbhelper)
		drm_fbdev_cma_restore_mode(imxdrm->fbhelper);
}

static int imx_drm_driver_unload(struct drm_device *drm)
{
	struct imx_drm_device *imxdrm = drm->dev_private;

	component_unbind_all(drm->dev, drm);

	imx_drm_device_put();

	drm_vblank_cleanup(drm);
	drm_kms_helper_poll_fini(drm);
	drm_mode_config_cleanup(drm);

	return 0;
}

struct imx_drm_crtc *imx_drm_find_crtc(struct drm_crtc *crtc)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();
	unsigned i;

	for (i = 0; i < MAX_CRTC; i++)
		if (imxdrm->crtc[i] && imxdrm->crtc[i]->crtc == crtc)
			return imxdrm->crtc[i];

	return NULL;
}

int imx_drm_panel_format_pins(struct drm_encoder *encoder,
		u32 interface_pix_fmt, int hsync_pin, int vsync_pin)
{
	struct imx_drm_crtc_helper_funcs *helper;
	struct imx_drm_crtc *imx_crtc;

	imx_crtc = imx_drm_find_crtc(encoder->crtc);
	if (!imx_crtc)
		return -EINVAL;

	helper = &imx_crtc->imx_drm_helper_funcs;
	if (helper->set_interface_pix_fmt)
		return helper->set_interface_pix_fmt(encoder->crtc,
				encoder->encoder_type, interface_pix_fmt,
				hsync_pin, vsync_pin);
	return 0;
}
EXPORT_SYMBOL_GPL(imx_drm_panel_format_pins);

int imx_drm_panel_format(struct drm_encoder *encoder, u32 interface_pix_fmt)
{
	return imx_drm_panel_format_pins(encoder, interface_pix_fmt, 2, 3);
}
EXPORT_SYMBOL_GPL(imx_drm_panel_format);

int imx_drm_crtc_vblank_get(struct imx_drm_crtc *imx_drm_crtc)
{
	return drm_vblank_get(imx_drm_crtc->crtc->dev, imx_drm_crtc->pipe);
}
EXPORT_SYMBOL_GPL(imx_drm_crtc_vblank_get);

void imx_drm_crtc_vblank_put(struct imx_drm_crtc *imx_drm_crtc)
{
	drm_vblank_put(imx_drm_crtc->crtc->dev, imx_drm_crtc->pipe);
}
EXPORT_SYMBOL_GPL(imx_drm_crtc_vblank_put);

void imx_drm_handle_vblank(struct imx_drm_crtc *imx_drm_crtc)
{
	drm_handle_vblank(imx_drm_crtc->crtc->dev, imx_drm_crtc->pipe);
}
EXPORT_SYMBOL_GPL(imx_drm_handle_vblank);

static int imx_drm_enable_vblank(struct drm_device *drm, int crtc)
{
	struct imx_drm_device *imxdrm = drm->dev_private;
	struct imx_drm_crtc *imx_drm_crtc = imxdrm->crtc[crtc];
	int ret;

	if (!imx_drm_crtc)
		return -EINVAL;

	if (!imx_drm_crtc->imx_drm_helper_funcs.enable_vblank)
		return -ENOSYS;

	ret = imx_drm_crtc->imx_drm_helper_funcs.enable_vblank(
			imx_drm_crtc->crtc);

	return ret;
}

static void imx_drm_disable_vblank(struct drm_device *drm, int crtc)
{
	struct imx_drm_device *imxdrm = drm->dev_private;
	struct imx_drm_crtc *imx_drm_crtc = imxdrm->crtc[crtc];

	if (!imx_drm_crtc)
		return;

	if (!imx_drm_crtc->imx_drm_helper_funcs.disable_vblank)
		return;

	imx_drm_crtc->imx_drm_helper_funcs.disable_vblank(imx_drm_crtc->crtc);
}

static void imx_drm_driver_preclose(struct drm_device *drm,
		struct drm_file *file)
{
	int i;

	if (!file->is_master)
		return;

	for (i = 0; i < MAX_CRTC; i++)
		imx_drm_disable_vblank(drm, i);
}

static const struct file_operations imx_drm_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap = drm_gem_cma_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.llseek = noop_llseek,
};

int imx_drm_connector_mode_valid(struct drm_connector *connector,
	struct drm_display_mode *mode)
{
	return MODE_OK;
}
EXPORT_SYMBOL(imx_drm_connector_mode_valid);

static struct imx_drm_device *imx_drm_device;

static struct imx_drm_device *__imx_drm_device(void)
{
	return imx_drm_device;
}

struct drm_device *imx_drm_device_get(void)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();
	struct imx_drm_encoder *enc;
	struct imx_drm_connector *con;
	struct imx_drm_crtc *crtc;

	list_for_each_entry(enc, &imxdrm->encoder_list, list) {
		if (!try_module_get(enc->owner)) {
			dev_err(imxdrm->dev, "could not get module %s\n",
					module_name(enc->owner));
			goto unwind_enc;
		}
	}

	list_for_each_entry(con, &imxdrm->connector_list, list) {
		if (!try_module_get(con->owner)) {
			dev_err(imxdrm->dev, "could not get module %s\n",
					module_name(con->owner));
			goto unwind_con;
		}
	}

	list_for_each_entry(crtc, &imxdrm->crtc_list, list) {
		if (!try_module_get(crtc->owner)) {
			dev_err(imxdrm->dev, "could not get module %s\n",
					module_name(crtc->owner));
			goto unwind_crtc;
		}
	}

	return imxdrm->drm;

unwind_crtc:
	list_for_each_entry_continue_reverse(crtc, &imxdrm->crtc_list, list)
		module_put(crtc->owner);
unwind_con:
	list_for_each_entry_continue_reverse(con, &imxdrm->connector_list, list)
		module_put(con->owner);
unwind_enc:
	list_for_each_entry_continue_reverse(enc, &imxdrm->encoder_list, list)
		module_put(enc->owner);

	mutex_unlock(&imxdrm->mutex);

	return NULL;

}
EXPORT_SYMBOL_GPL(imx_drm_device_get);

void imx_drm_device_put(void)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();
	struct imx_drm_encoder *enc;
	struct imx_drm_connector *con;
	struct imx_drm_crtc *crtc;

	mutex_lock(&imxdrm->mutex);

	list_for_each_entry(crtc, &imxdrm->crtc_list, list)
		module_put(crtc->owner);

	list_for_each_entry(con, &imxdrm->connector_list, list)
		module_put(con->owner);

	list_for_each_entry(enc, &imxdrm->encoder_list, list)
		module_put(enc->owner);

	mutex_unlock(&imxdrm->mutex);
}
EXPORT_SYMBOL_GPL(imx_drm_device_put);

static int drm_mode_group_reinit(struct drm_device *dev)
{
	struct drm_mode_group *group = &dev->primary->mode_group;
	uint32_t *id_list = group->id_list;
	int ret;

	ret = drm_mode_group_init_legacy_group(dev, group);
	if (ret < 0)
		return ret;

	kfree(id_list);
	return 0;
}

/*
 * register an encoder to the drm core
 */
static int imx_drm_encoder_register(struct imx_drm_encoder *imx_drm_encoder)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();

	INIT_LIST_HEAD(&imx_drm_encoder->possible_crtcs);

	drm_encoder_init(imxdrm->drm, imx_drm_encoder->encoder,
			imx_drm_encoder->encoder->funcs,
			imx_drm_encoder->encoder->encoder_type);

	drm_mode_group_reinit(imxdrm->drm);

	return 0;
}

/*
 * unregister an encoder from the drm core
 */
static void imx_drm_encoder_unregister(struct imx_drm_encoder
		*imx_drm_encoder)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();

	drm_encoder_cleanup(imx_drm_encoder->encoder);

	drm_mode_group_reinit(imxdrm->drm);
}

/*
 * register a connector to the drm core
 */
static int imx_drm_connector_register(
		struct imx_drm_connector *imx_drm_connector)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();

	drm_connector_init(imxdrm->drm, imx_drm_connector->connector,
			imx_drm_connector->connector->funcs,
			imx_drm_connector->connector->connector_type);
	drm_mode_group_reinit(imxdrm->drm);

	return drm_sysfs_connector_add(imx_drm_connector->connector);
}

/*
 * unregister a connector from the drm core
 */
static void imx_drm_connector_unregister(
		struct imx_drm_connector *imx_drm_connector)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();

	drm_sysfs_connector_remove(imx_drm_connector->connector);
	drm_connector_cleanup(imx_drm_connector->connector);

	drm_mode_group_reinit(imxdrm->drm);
}

/*
 * Main DRM initialisation. This binds, initialises and registers
 * with DRM the subcomponents of the driver.
 */
static int imx_drm_driver_load(struct drm_device *drm, unsigned long flags)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();
	int ret;

	imxdrm->drm = drm;

	drm->dev_private = imxdrm;

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = true, we can use the vblank feature.
	 *
	 * P.S. note that we wouldn't use drm irq handler but
	 *      just specific driver own one instead because
	 *      drm framework supports only one irq handler and
	 *      drivers can well take care of their interrupts
	 */
	drm->irq_enabled = true;

	drm_mode_config_init(drm);
	imx_drm_mode_config_init(drm);

	mutex_lock(&imxdrm->mutex);

	drm_kms_helper_poll_init(drm);

	/* setup the grouping for the legacy output */
	ret = drm_mode_group_init_legacy_group(drm,
			&drm->primary->mode_group);
	if (ret)
		goto err_kms;

	ret = drm_vblank_init(drm, MAX_CRTC);
	if (ret)
		goto err_kms;

	/*
	 * with vblank_disable_allowed = true, vblank interrupt will be disabled
	 * by drm timer once a current process gives up ownership of
	 * vblank event.(after drm_vblank_put function is called)
	 */
	drm->vblank_disable_allowed = true;

	if (!imx_drm_device_get()) {
		ret = -EINVAL;
		goto err_vblank;
	}

	platform_set_drvdata(drm->platformdev, drm);
	mutex_unlock(&imxdrm->mutex);

	/* Now try and bind all our sub-components */
	ret = component_bind_all(drm->dev, drm);
	if (ret)
		goto err_relock;
	return 0;

err_relock:
	mutex_lock(&imxdrm->mutex);
err_vblank:
	drm_vblank_cleanup(drm);
err_kms:
	drm_kms_helper_poll_fini(drm);
	drm_mode_config_cleanup(drm);
	mutex_unlock(&imxdrm->mutex);

	return ret;
}

static void imx_drm_update_possible_crtcs(void)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();
	struct imx_drm_crtc *imx_drm_crtc;
	struct imx_drm_encoder *enc;
	struct crtc_cookie *cookie;

	list_for_each_entry(enc, &imxdrm->encoder_list, list) {
		u32 possible_crtcs = 0;

		list_for_each_entry(cookie, &enc->possible_crtcs, list) {
			list_for_each_entry(imx_drm_crtc, &imxdrm->crtc_list, list) {
				if (imx_drm_crtc->cookie.cookie == cookie->cookie &&
						imx_drm_crtc->cookie.id == cookie->id) {
					possible_crtcs |= 1 << imx_drm_crtc->pipe;
				}
			}
		}
		enc->encoder->possible_crtcs = possible_crtcs;
		enc->encoder->possible_clones = possible_crtcs;
	}
}

/*
 * imx_drm_add_crtc - add a new crtc
 *
 * The return value if !NULL is a cookie for the caller to pass to
 * imx_drm_remove_crtc later.
 */
int imx_drm_add_crtc(struct drm_crtc *crtc,
		struct imx_drm_crtc **new_crtc,
		const struct imx_drm_crtc_helper_funcs *imx_drm_helper_funcs,
		struct module *owner, void *cookie, int id)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();
	struct imx_drm_crtc *imx_drm_crtc;
	int ret;

	mutex_lock(&imxdrm->mutex);

	/*
	 * The vblank arrays are dimensioned by MAX_CRTC - we can't
	 * pass IDs greater than this to those functions.
	 */
	if (imxdrm->pipes >= MAX_CRTC) {
		ret = -EINVAL;
		goto err_busy;
	}

	if (imxdrm->drm->open_count) {
		ret = -EBUSY;
		goto err_busy;
	}

	imx_drm_crtc = kzalloc(sizeof(*imx_drm_crtc), GFP_KERNEL);
	if (!imx_drm_crtc) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	imx_drm_crtc->imx_drm_helper_funcs = *imx_drm_helper_funcs;
	imx_drm_crtc->pipe = imxdrm->pipes++;
	imx_drm_crtc->cookie.cookie = cookie;
	imx_drm_crtc->cookie.id = id;
	imx_drm_crtc->mux_id = imx_drm_crtc->pipe;
	imx_drm_crtc->crtc = crtc;
	imx_drm_crtc->imxdrm = imxdrm;

	imx_drm_crtc->owner = owner;

	imxdrm->crtc[imx_drm_crtc->pipe] = imx_drm_crtc;

	*new_crtc = imx_drm_crtc;

	ret = drm_mode_crtc_set_gamma_size(imx_drm_crtc->crtc, 256);
	if (ret)
		goto err_register;

	drm_crtc_helper_add(crtc,
			imx_drm_crtc->imx_drm_helper_funcs.crtc_helper_funcs);

	drm_crtc_init(imxdrm->drm, crtc,
			imx_drm_crtc->imx_drm_helper_funcs.crtc_funcs);

	drm_mode_group_reinit(imxdrm->drm);

	imx_drm_update_possible_crtcs();

	mutex_unlock(&imxdrm->mutex);

	return 0;

err_register:
	imxdrm->crtc[imx_drm_crtc->pipe] = NULL;
	kfree(imx_drm_crtc);
err_alloc:
err_busy:
	mutex_unlock(&imxdrm->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(imx_drm_add_crtc);

/*
 * imx_drm_remove_crtc - remove a crtc
 */
int imx_drm_remove_crtc(struct imx_drm_crtc *imx_drm_crtc)
{
	struct imx_drm_device *imxdrm = imx_drm_crtc->imxdrm;

	mutex_lock(&imxdrm->mutex);

	drm_crtc_cleanup(imx_drm_crtc->crtc);

	imxdrm->crtc[imx_drm_crtc->pipe] = NULL;

	drm_mode_group_reinit(imxdrm->drm);

	mutex_unlock(&imxdrm->mutex);

	kfree(imx_drm_crtc);

	return 0;
}
EXPORT_SYMBOL_GPL(imx_drm_remove_crtc);

/*
 * imx_drm_add_encoder - add a new encoder
 */
int imx_drm_add_encoder(struct drm_encoder *encoder,
		struct imx_drm_encoder **newenc, struct module *owner)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();
	struct imx_drm_encoder *imx_drm_encoder;
	int ret;

	mutex_lock(&imxdrm->mutex);

	if (imxdrm->drm->open_count) {
		ret = -EBUSY;
		goto err_busy;
	}

	imx_drm_encoder = kzalloc(sizeof(*imx_drm_encoder), GFP_KERNEL);
	if (!imx_drm_encoder) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	imx_drm_encoder->encoder = encoder;
	imx_drm_encoder->owner = owner;

	ret = imx_drm_encoder_register(imx_drm_encoder);
	if (ret) {
		ret = -ENOMEM;
		goto err_register;
	}

	list_add_tail(&imx_drm_encoder->list, &imxdrm->encoder_list);

	*newenc = imx_drm_encoder;

	mutex_unlock(&imxdrm->mutex);

	return 0;

err_register:
	kfree(imx_drm_encoder);
err_alloc:
err_busy:
	mutex_unlock(&imxdrm->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(imx_drm_add_encoder);

int imx_drm_encoder_add_possible_crtcs(
		struct imx_drm_encoder *imx_drm_encoder,
		struct device_node *np)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();
	struct of_phandle_args args;
	struct crtc_cookie *c;
	int ret = 0;
	int i;

	if (!list_empty(&imx_drm_encoder->possible_crtcs))
		return -EBUSY;

	for (i = 0; !ret; i++) {
		ret = of_parse_phandle_with_args(np, "crtcs",
				"#crtc-cells", i, &args);
		if (ret < 0)
			break;

		c = kzalloc(sizeof(*c), GFP_KERNEL);
		if (!c) {
			of_node_put(args.np);
			return -ENOMEM;
		}

		c->cookie = args.np;
		c->id = args.args_count > 0 ? args.args[0] : 0;

		of_node_put(args.np);

		mutex_lock(&imxdrm->mutex);

		list_add_tail(&c->list, &imx_drm_encoder->possible_crtcs);

		mutex_unlock(&imxdrm->mutex);
	}

	imx_drm_update_possible_crtcs();

	return 0;
}
EXPORT_SYMBOL_GPL(imx_drm_encoder_add_possible_crtcs);

int imx_drm_encoder_get_mux_id(struct drm_encoder *encoder)
{
	struct imx_drm_crtc *imx_crtc = imx_drm_find_crtc(encoder->crtc);

	return imx_crtc ? imx_crtc->mux_id : -EINVAL;
}
EXPORT_SYMBOL_GPL(imx_drm_encoder_get_mux_id);

/*
 * imx_drm_remove_encoder - remove an encoder
 */
int imx_drm_remove_encoder(struct imx_drm_encoder *imx_drm_encoder)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();
	struct crtc_cookie *c, *tmp;

	mutex_lock(&imxdrm->mutex);

	imx_drm_encoder_unregister(imx_drm_encoder);

	list_del(&imx_drm_encoder->list);

	list_for_each_entry_safe(c, tmp, &imx_drm_encoder->possible_crtcs,
			list)
		kfree(c);

	mutex_unlock(&imxdrm->mutex);

	kfree(imx_drm_encoder);

	return 0;
}
EXPORT_SYMBOL_GPL(imx_drm_remove_encoder);

/*
 * imx_drm_add_connector - add a connector
 */
int imx_drm_add_connector(struct drm_connector *connector,
		struct imx_drm_connector **new_con,
		struct module *owner)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();
	struct imx_drm_connector *imx_drm_connector;
	int ret;

	mutex_lock(&imxdrm->mutex);

	if (imxdrm->drm->open_count) {
		ret = -EBUSY;
		goto err_busy;
	}

	imx_drm_connector = kzalloc(sizeof(*imx_drm_connector), GFP_KERNEL);
	if (!imx_drm_connector) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	imx_drm_connector->connector = connector;
	imx_drm_connector->owner = owner;

	ret = imx_drm_connector_register(imx_drm_connector);
	if (ret)
		goto err_register;

	list_add_tail(&imx_drm_connector->list, &imxdrm->connector_list);

	*new_con = imx_drm_connector;

	mutex_unlock(&imxdrm->mutex);

	return 0;

err_register:
	kfree(imx_drm_connector);
err_alloc:
err_busy:
	mutex_unlock(&imxdrm->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(imx_drm_add_connector);

void imx_drm_fb_helper_set(struct drm_fbdev_cma *fbdev_helper)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();

	imxdrm->fbhelper = fbdev_helper;
}
EXPORT_SYMBOL_GPL(imx_drm_fb_helper_set);

/*
 * imx_drm_remove_connector - remove a connector
 */
int imx_drm_remove_connector(struct imx_drm_connector *imx_drm_connector)
{
	struct imx_drm_device *imxdrm = __imx_drm_device();

	mutex_lock(&imxdrm->mutex);

	imx_drm_connector_unregister(imx_drm_connector);

	list_del(&imx_drm_connector->list);

	mutex_unlock(&imxdrm->mutex);

	kfree(imx_drm_connector);

	return 0;
}
EXPORT_SYMBOL_GPL(imx_drm_remove_connector);

static const struct drm_ioctl_desc imx_drm_ioctls[] = {
	/* none so far */
};

static struct drm_driver imx_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME,
	.load			= imx_drm_driver_load,
	.unload			= imx_drm_driver_unload,
	.lastclose		= imx_drm_driver_lastclose,
	.preclose		= imx_drm_driver_preclose,
	.gem_free_object	= drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.dumb_create		= drm_gem_cma_dumb_create,
	.dumb_map_offset	= drm_gem_cma_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,

	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,
	.get_vblank_counter	= drm_vblank_count,
	.enable_vblank		= imx_drm_enable_vblank,
	.disable_vblank		= imx_drm_disable_vblank,
	.ioctls			= imx_drm_ioctls,
	.num_ioctls		= ARRAY_SIZE(imx_drm_ioctls),
	.fops			= &imx_drm_driver_fops,
	.name			= "imx-drm",
	.desc			= "i.MX DRM graphics",
	.date			= "20120507",
	.major			= 1,
	.minor			= 0,
	.patchlevel		= 0,
};

static int compare_parent_of(struct device *dev, void *data)
{
	struct of_phandle_args *args = data;
	return dev->parent && dev->parent->of_node == args->np;
}

static int compare_of(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int imx_drm_add_components(struct device *master, struct master *m)
{
	struct device_node *np = master->of_node;
	unsigned i;
	int ret;

	for (i = 0; ; i++) {
		struct of_phandle_args args;

		ret = of_parse_phandle_with_fixed_args(np, "crtcs", 1,
						       i, &args);
		if (ret)
			break;

		ret = component_master_add_child(m, compare_parent_of, &args);
		of_node_put(args.np);

		if (ret)
			return ret;
	}

	for (i = 0; ; i++) {
		struct device_node *node;

		node = of_parse_phandle(np, "connectors", i);
		if (!node)
			break;

		ret = component_master_add_child(m, compare_of, node);
		of_node_put(node);

		if (ret)
			return ret;
	}
	return 0;
}

static int imx_drm_bind(struct device *dev)
{
	return drm_platform_init(&imx_drm_driver, to_platform_device(dev));
}

static void imx_drm_unbind(struct device *dev)
{
	drm_put_dev(dev_get_drvdata(dev));
}

static const struct component_master_ops imx_drm_ops = {
	.add_components = imx_drm_add_components,
	.bind = imx_drm_bind,
	.unbind = imx_drm_unbind,
};

static int imx_drm_platform_probe(struct platform_device *pdev)
{
	int ret;

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	imx_drm_device->dev = &pdev->dev;

	return component_master_add(&pdev->dev, &imx_drm_ops);
}

static int imx_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &imx_drm_ops);
	return 0;
}

static const struct of_device_id imx_drm_dt_ids[] = {
	{ .compatible = "fsl,imx-drm", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx_drm_dt_ids);

static struct platform_driver imx_drm_pdrv = {
	.probe		= imx_drm_platform_probe,
	.remove		= imx_drm_platform_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "imx-drm",
		.of_match_table = imx_drm_dt_ids,
	},
};

static int __init imx_drm_init(void)
{
	int ret;

	imx_drm_device = kzalloc(sizeof(*imx_drm_device), GFP_KERNEL);
	if (!imx_drm_device)
		return -ENOMEM;

	mutex_init(&imx_drm_device->mutex);
	INIT_LIST_HEAD(&imx_drm_device->connector_list);
	INIT_LIST_HEAD(&imx_drm_device->encoder_list);

	ret = platform_driver_register(&imx_drm_pdrv);
	if (ret)
		goto err_pdrv;

	return 0;

err_pdrv:
	kfree(imx_drm_device);

	return ret;
}

static void __exit imx_drm_exit(void)
{
	platform_driver_unregister(&imx_drm_pdrv);

	kfree(imx_drm_device);
}

module_init(imx_drm_init);
module_exit(imx_drm_exit);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("i.MX drm driver core");
MODULE_LICENSE("GPL");
