/*
 * drivers/staging/omapdrm/omap_drv.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "omap_drv.h"

#include "drm_crtc_helper.h"
#include "drm_fb_helper.h"

#define DRIVER_NAME		MODULE_NAME
#define DRIVER_DESC		"OMAP DRM"
#define DRIVER_DATE		"20110917"
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

struct drm_device *drm_device;

static int num_crtc = CONFIG_DRM_OMAP_NUM_CRTCS;

MODULE_PARM_DESC(num_crtc, "Number of overlays to use as CRTCs");
module_param(num_crtc, int, 0600);

/*
 * mode config funcs
 */

/* Notes about mapping DSS and DRM entities:
 *    CRTC:        overlay
 *    encoder:     manager.. with some extension to allow one primary CRTC
 *                 and zero or more video CRTC's to be mapped to one encoder?
 *    connector:   dssdev.. manager can be attached/detached from different
 *                 devices
 */

static void omap_fb_output_poll_changed(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	DBG("dev=%p", dev);
	if (priv->fbdev) {
		drm_fb_helper_hotplug_event(priv->fbdev);
	}
}

static struct drm_mode_config_funcs omap_mode_config_funcs = {
	.fb_create = omap_framebuffer_create,
	.output_poll_changed = omap_fb_output_poll_changed,
};

static int get_connector_type(struct omap_dss_device *dssdev)
{
	switch (dssdev->type) {
	case OMAP_DISPLAY_TYPE_HDMI:
		return DRM_MODE_CONNECTOR_HDMIA;
	case OMAP_DISPLAY_TYPE_DPI:
		if (!strcmp(dssdev->name, "dvi"))
			return DRM_MODE_CONNECTOR_DVID;
		/* fallthrough */
	default:
		return DRM_MODE_CONNECTOR_Unknown;
	}
}

#if 0 /* enable when dss2 supports hotplug */
static int omap_drm_notifier(struct notifier_block *nb,
		unsigned long evt, void *arg)
{
	switch (evt) {
	case OMAP_DSS_SIZE_CHANGE:
	case OMAP_DSS_HOTPLUG_CONNECT:
	case OMAP_DSS_HOTPLUG_DISCONNECT: {
		struct drm_device *dev = drm_device;
		DBG("hotplug event: evt=%d, dev=%p", evt, dev);
		if (dev) {
			drm_sysfs_hotplug_event(dev);
		}
		return NOTIFY_OK;
	}
	default:  /* don't care about other events for now */
		return NOTIFY_DONE;
	}
}
#endif

static void dump_video_chains(void)
{
	int i;

	DBG("dumping video chains: ");
	for (i = 0; i < omap_dss_get_num_overlays(); i++) {
		struct omap_overlay *ovl = omap_dss_get_overlay(i);
		struct omap_overlay_manager *mgr = ovl->manager;
		struct omap_dss_device *dssdev = mgr ? mgr->device : NULL;
		if (dssdev) {
			DBG("%d: %s -> %s -> %s", i, ovl->name, mgr->name,
						dssdev->name);
		} else if (mgr) {
			DBG("%d: %s -> %s", i, ovl->name, mgr->name);
		} else {
			DBG("%d: %s", i, ovl->name);
		}
	}
}

/* create encoders for each manager */
static int create_encoder(struct drm_device *dev,
		struct omap_overlay_manager *mgr)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_encoder *encoder = omap_encoder_init(dev, mgr);

	if (!encoder) {
		dev_err(dev->dev, "could not create encoder: %s\n",
				mgr->name);
		return -ENOMEM;
	}

	BUG_ON(priv->num_encoders >= ARRAY_SIZE(priv->encoders));

	priv->encoders[priv->num_encoders++] = encoder;

	return 0;
}

/* create connectors for each display device */
static int create_connector(struct drm_device *dev,
		struct omap_dss_device *dssdev)
{
	struct omap_drm_private *priv = dev->dev_private;
	static struct notifier_block *notifier;
	struct drm_connector *connector;
	int j;

	if (!dssdev->driver) {
		dev_warn(dev->dev, "%s has no driver.. skipping it\n",
				dssdev->name);
		return 0;
	}

	if (!(dssdev->driver->get_timings ||
				dssdev->driver->read_edid)) {
		dev_warn(dev->dev, "%s driver does not support "
			"get_timings or read_edid.. skipping it!\n",
			dssdev->name);
		return 0;
	}

	connector = omap_connector_init(dev,
			get_connector_type(dssdev), dssdev);

	if (!connector) {
		dev_err(dev->dev, "could not create connector: %s\n",
				dssdev->name);
		return -ENOMEM;
	}

	BUG_ON(priv->num_connectors >= ARRAY_SIZE(priv->connectors));

	priv->connectors[priv->num_connectors++] = connector;

#if 0 /* enable when dss2 supports hotplug */
	notifier = kzalloc(sizeof(struct notifier_block), GFP_KERNEL);
	notifier->notifier_call = omap_drm_notifier;
	omap_dss_add_notify(dssdev, notifier);
#else
	notifier = NULL;
#endif

	for (j = 0; j < priv->num_encoders; j++) {
		struct omap_overlay_manager *mgr =
			omap_encoder_get_manager(priv->encoders[j]);
		if (mgr->device == dssdev) {
			drm_mode_connector_attach_encoder(connector,
					priv->encoders[j]);
		}
	}

	return 0;
}

/* create up to max_overlays CRTCs mapping to overlays.. by default,
 * connect the overlays to different managers/encoders, giving priority
 * to encoders connected to connectors with a detected connection
 */
static int create_crtc(struct drm_device *dev, struct omap_overlay *ovl,
		int *j, unsigned int connected_connectors)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_overlay_manager *mgr = NULL;
	struct drm_crtc *crtc;

	if (ovl->manager) {
		DBG("disconnecting %s from %s", ovl->name,
					ovl->manager->name);
		ovl->unset_manager(ovl);
	}

	/* find next best connector, ones with detected connection first
	 */
	while (*j < priv->num_connectors && !mgr) {
		if (connected_connectors & (1 << *j)) {
			struct drm_encoder *encoder =
				omap_connector_attached_encoder(
						priv->connectors[*j]);
			if (encoder) {
				mgr = omap_encoder_get_manager(encoder);
			}
		}
		(*j)++;
	}

	/* if we couldn't find another connected connector, lets start
	 * looking at the unconnected connectors:
	 *
	 * note: it might not be immediately apparent, but thanks to
	 * the !mgr check in both this loop and the one above, the only
	 * way to enter this loop is with *j == priv->num_connectors,
	 * so idx can never go negative.
	 */
	while (*j < 2 * priv->num_connectors && !mgr) {
		int idx = *j - priv->num_connectors;
		if (!(connected_connectors & (1 << idx))) {
			struct drm_encoder *encoder =
				omap_connector_attached_encoder(
						priv->connectors[idx]);
			if (encoder) {
				mgr = omap_encoder_get_manager(encoder);
			}
		}
		(*j)++;
	}

	if (mgr) {
		DBG("connecting %s to %s", ovl->name, mgr->name);
		ovl->set_manager(ovl, mgr);
	}

	crtc = omap_crtc_init(dev, ovl, priv->num_crtcs);

	if (!crtc) {
		dev_err(dev->dev, "could not create CRTC: %s\n",
				ovl->name);
		return -ENOMEM;
	}

	BUG_ON(priv->num_crtcs >= ARRAY_SIZE(priv->crtcs));

	priv->crtcs[priv->num_crtcs++] = crtc;

	return 0;
}

static int match_dev_name(struct omap_dss_device *dssdev, void *data)
{
	return !strcmp(dssdev->name, data);
}

static unsigned int detect_connectors(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	unsigned int connected_connectors = 0;
	int i;

	for (i = 0; i < priv->num_connectors; i++) {
		struct drm_connector *connector = priv->connectors[i];
		if (omap_connector_detect(connector, true) ==
				connector_status_connected) {
			connected_connectors |= (1 << i);
		}
	}

	return connected_connectors;
}

static int omap_modeset_init(struct drm_device *dev)
{
	const struct omap_drm_platform_data *pdata = dev->dev->platform_data;
	struct omap_kms_platform_data *kms_pdata = NULL;
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_dss_device *dssdev = NULL;
	int i, j;
	unsigned int connected_connectors = 0;

	drm_mode_config_init(dev);

	if (pdata && pdata->kms_pdata) {
		kms_pdata = pdata->kms_pdata;

		/* if platform data is provided by the board file, use it to
		 * control which overlays, managers, and devices we own.
		 */
		for (i = 0; i < kms_pdata->mgr_cnt; i++) {
			struct omap_overlay_manager *mgr =
				omap_dss_get_overlay_manager(
						kms_pdata->mgr_ids[i]);
			create_encoder(dev, mgr);
		}

		for (i = 0; i < kms_pdata->dev_cnt; i++) {
			struct omap_dss_device *dssdev =
				omap_dss_find_device(
					(void *)kms_pdata->dev_names[i],
					match_dev_name);
			if (!dssdev) {
				dev_warn(dev->dev, "no such dssdev: %s\n",
						kms_pdata->dev_names[i]);
				continue;
			}
			create_connector(dev, dssdev);
		}

		connected_connectors = detect_connectors(dev);

		j = 0;
		for (i = 0; i < kms_pdata->ovl_cnt; i++) {
			struct omap_overlay *ovl =
				omap_dss_get_overlay(kms_pdata->ovl_ids[i]);
			create_crtc(dev, ovl, &j, connected_connectors);
		}
	} else {
		/* otherwise just grab up to CONFIG_DRM_OMAP_NUM_CRTCS and try
		 * to make educated guesses about everything else
		 */
		int max_overlays = min(omap_dss_get_num_overlays(), num_crtc);

		for (i = 0; i < omap_dss_get_num_overlay_managers(); i++) {
			create_encoder(dev, omap_dss_get_overlay_manager(i));
		}

		for_each_dss_dev(dssdev) {
			create_connector(dev, dssdev);
		}

		connected_connectors = detect_connectors(dev);

		j = 0;
		for (i = 0; i < max_overlays; i++) {
			create_crtc(dev, omap_dss_get_overlay(i),
					&j, connected_connectors);
		}
	}

	/* for now keep the mapping of CRTCs and encoders static.. */
	for (i = 0; i < priv->num_encoders; i++) {
		struct drm_encoder *encoder = priv->encoders[i];
		struct omap_overlay_manager *mgr =
				omap_encoder_get_manager(encoder);

		encoder->possible_crtcs = 0;

		for (j = 0; j < priv->num_crtcs; j++) {
			struct omap_overlay *ovl =
					omap_crtc_get_overlay(priv->crtcs[j]);
			if (ovl->manager == mgr) {
				encoder->possible_crtcs |= (1 << j);
			}
		}

		DBG("%s: possible_crtcs=%08x", mgr->name,
					encoder->possible_crtcs);
	}

	dump_video_chains();

	dev->mode_config.min_width = 256;
	dev->mode_config.min_height = 256;

	/* note: eventually will need some cpu_is_omapXYZ() type stuff here
	 * to fill in these limits properly on different OMAP generations..
	 */
	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;

	dev->mode_config.funcs = &omap_mode_config_funcs;

	return 0;
}

static void omap_modeset_free(struct drm_device *dev)
{
	drm_mode_config_cleanup(dev);
}

/*
 * drm ioctl funcs
 */


static int ioctl_get_param(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_omap_param *args = data;

	DBG("%p: param=%llu", dev, args->param);

	switch (args->param) {
	case OMAP_PARAM_CHIPSET_ID:
		args->value = GET_OMAP_TYPE;
		break;
	default:
		DBG("unknown parameter %lld", args->param);
		return -EINVAL;
	}

	return 0;
}

static int ioctl_set_param(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_omap_param *args = data;

	switch (args->param) {
	default:
		DBG("unknown parameter %lld", args->param);
		return -EINVAL;
	}

	return 0;
}

static int ioctl_gem_new(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_omap_gem_new *args = data;
	DBG("%p:%p: size=0x%08x, flags=%08x", dev, file_priv,
			args->size.bytes, args->flags);
	return omap_gem_new_handle(dev, file_priv, args->size,
			args->flags, &args->handle);
}

static int ioctl_gem_cpu_prep(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_omap_gem_cpu_prep *args = data;
	struct drm_gem_object *obj;
	int ret;

	VERB("%p:%p: handle=%d, op=%x", dev, file_priv, args->handle, args->op);

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (!obj) {
		return -ENOENT;
	}

	ret = omap_gem_op_sync(obj, args->op);

	if (!ret) {
		ret = omap_gem_op_start(obj, args->op);
	}

	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static int ioctl_gem_cpu_fini(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_omap_gem_cpu_fini *args = data;
	struct drm_gem_object *obj;
	int ret;

	VERB("%p:%p: handle=%d", dev, file_priv, args->handle);

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (!obj) {
		return -ENOENT;
	}

	/* XXX flushy, flushy */
	ret = 0;

	if (!ret) {
		ret = omap_gem_op_finish(obj, args->op);
	}

	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static int ioctl_gem_info(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_omap_gem_info *args = data;
	struct drm_gem_object *obj;
	int ret = 0;

	DBG("%p:%p: handle=%d", dev, file_priv, args->handle);

	obj = drm_gem_object_lookup(dev, file_priv, args->handle);
	if (!obj) {
		return -ENOENT;
	}

	args->size = omap_gem_mmap_size(obj);
	args->offset = omap_gem_mmap_offset(obj);

	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

struct drm_ioctl_desc ioctls[DRM_COMMAND_END - DRM_COMMAND_BASE] = {
	DRM_IOCTL_DEF_DRV(OMAP_GET_PARAM, ioctl_get_param, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(OMAP_SET_PARAM, ioctl_set_param, DRM_UNLOCKED|DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(OMAP_GEM_NEW, ioctl_gem_new, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(OMAP_GEM_CPU_PREP, ioctl_gem_cpu_prep, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(OMAP_GEM_CPU_FINI, ioctl_gem_cpu_fini, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(OMAP_GEM_INFO, ioctl_gem_info, DRM_UNLOCKED|DRM_AUTH),
};

/*
 * drm driver funcs
 */

/**
 * load - setup chip and create an initial config
 * @dev: DRM device
 * @flags: startup flags
 *
 * The driver load routine has to do several things:
 *   - initialize the memory manager
 *   - allocate initial config memory
 *   - setup the DRM framebuffer with the allocated memory
 */
static int dev_load(struct drm_device *dev, unsigned long flags)
{
	struct omap_drm_private *priv;
	int ret;

	DBG("load: dev=%p", dev);

	drm_device = dev;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(dev->dev, "could not allocate priv\n");
		return -ENOMEM;
	}

	dev->dev_private = priv;

	omap_gem_init(dev);

	ret = omap_modeset_init(dev);
	if (ret) {
		dev_err(dev->dev, "omap_modeset_init failed: ret=%d\n", ret);
		dev->dev_private = NULL;
		kfree(priv);
		return ret;
	}

	priv->fbdev = omap_fbdev_init(dev);
	if (!priv->fbdev) {
		dev_warn(dev->dev, "omap_fbdev_init failed\n");
		/* well, limp along without an fbdev.. maybe X11 will work? */
	}

	drm_kms_helper_poll_init(dev);

	ret = drm_vblank_init(dev, priv->num_crtcs);
	if (ret) {
		dev_warn(dev->dev, "could not init vblank\n");
	}

	return 0;
}

static int dev_unload(struct drm_device *dev)
{
	DBG("unload: dev=%p", dev);

	drm_vblank_cleanup(dev);
	drm_kms_helper_poll_fini(dev);

	omap_fbdev_free(dev);
	omap_modeset_free(dev);
	omap_gem_deinit(dev);

	kfree(dev->dev_private);
	dev->dev_private = NULL;

	return 0;
}

static int dev_open(struct drm_device *dev, struct drm_file *file)
{
	file->driver_priv = NULL;

	DBG("open: dev=%p, file=%p", dev, file);

	return 0;
}

static int dev_firstopen(struct drm_device *dev)
{
	DBG("firstopen: dev=%p", dev);
	return 0;
}

/**
 * lastclose - clean up after all DRM clients have exited
 * @dev: DRM device
 *
 * Take care of cleaning up after all DRM clients have exited.  In the
 * mode setting case, we want to restore the kernel's initial mode (just
 * in case the last client left us in a bad state).
 */
static void dev_lastclose(struct drm_device *dev)
{
	/* we don't support vga-switcheroo.. so just make sure the fbdev
	 * mode is active
	 */
	struct omap_drm_private *priv = dev->dev_private;
	int ret;

	DBG("lastclose: dev=%p", dev);

	ret = drm_fb_helper_restore_fbdev_mode(priv->fbdev);
	if (ret)
		DBG("failed to restore crtc mode");
}

static void dev_preclose(struct drm_device *dev, struct drm_file *file)
{
	DBG("preclose: dev=%p", dev);
}

static void dev_postclose(struct drm_device *dev, struct drm_file *file)
{
	DBG("postclose: dev=%p, file=%p", dev, file);
}

/**
 * enable_vblank - enable vblank interrupt events
 * @dev: DRM device
 * @crtc: which irq to enable
 *
 * Enable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 *
 * RETURNS
 * Zero on success, appropriate errno if the given @crtc's vblank
 * interrupt cannot be enabled.
 */
static int dev_enable_vblank(struct drm_device *dev, int crtc)
{
	DBG("enable_vblank: dev=%p, crtc=%d", dev, crtc);
	return 0;
}

/**
 * disable_vblank - disable vblank interrupt events
 * @dev: DRM device
 * @crtc: which irq to enable
 *
 * Disable vblank interrupts for @crtc.  If the device doesn't have
 * a hardware vblank counter, this routine should be a no-op, since
 * interrupts will have to stay on to keep the count accurate.
 */
static void dev_disable_vblank(struct drm_device *dev, int crtc)
{
	DBG("disable_vblank: dev=%p, crtc=%d", dev, crtc);
}

static irqreturn_t dev_irq_handler(DRM_IRQ_ARGS)
{
	return IRQ_HANDLED;
}

static void dev_irq_preinstall(struct drm_device *dev)
{
	DBG("irq_preinstall: dev=%p", dev);
}

static int dev_irq_postinstall(struct drm_device *dev)
{
	DBG("irq_postinstall: dev=%p", dev);
	return 0;
}

static void dev_irq_uninstall(struct drm_device *dev)
{
	DBG("irq_uninstall: dev=%p", dev);
}

static struct vm_operations_struct omap_gem_vm_ops = {
	.fault = omap_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static struct drm_driver omap_drm_driver = {
		.driver_features =
				DRIVER_HAVE_IRQ | DRIVER_MODESET | DRIVER_GEM,
		.load = dev_load,
		.unload = dev_unload,
		.open = dev_open,
		.firstopen = dev_firstopen,
		.lastclose = dev_lastclose,
		.preclose = dev_preclose,
		.postclose = dev_postclose,
		.get_vblank_counter = drm_vblank_count,
		.enable_vblank = dev_enable_vblank,
		.disable_vblank = dev_disable_vblank,
		.irq_preinstall = dev_irq_preinstall,
		.irq_postinstall = dev_irq_postinstall,
		.irq_uninstall = dev_irq_uninstall,
		.irq_handler = dev_irq_handler,
		.reclaim_buffers = drm_core_reclaim_buffers,
#ifdef CONFIG_DEBUG_FS
		.debugfs_init = omap_debugfs_init,
		.debugfs_cleanup = omap_debugfs_cleanup,
#endif
		.gem_init_object = omap_gem_init_object,
		.gem_free_object = omap_gem_free_object,
		.gem_vm_ops = &omap_gem_vm_ops,
		.dumb_create = omap_gem_dumb_create,
		.dumb_map_offset = omap_gem_dumb_map_offset,
		.dumb_destroy = omap_gem_dumb_destroy,
		.ioctls = ioctls,
		.num_ioctls = DRM_OMAP_NUM_IOCTLS,
		.fops = {
				.owner = THIS_MODULE,
				.open = drm_open,
				.unlocked_ioctl = drm_ioctl,
				.release = drm_release,
				.mmap = omap_gem_mmap,
				.poll = drm_poll,
				.fasync = drm_fasync,
				.read = drm_read,
				.llseek = noop_llseek,
		},
		.name = DRIVER_NAME,
		.desc = DRIVER_DESC,
		.date = DRIVER_DATE,
		.major = DRIVER_MAJOR,
		.minor = DRIVER_MINOR,
		.patchlevel = DRIVER_PATCHLEVEL,
};

static int pdev_suspend(struct platform_device *pDevice, pm_message_t state)
{
	DBG("");
	return 0;
}

static int pdev_resume(struct platform_device *device)
{
	DBG("");
	return 0;
}

static void pdev_shutdown(struct platform_device *device)
{
	DBG("");
}

static int pdev_probe(struct platform_device *device)
{
	DBG("%s", device->name);
	return drm_platform_init(&omap_drm_driver, device);
}

static int pdev_remove(struct platform_device *device)
{
	DBG("");
	drm_platform_exit(&omap_drm_driver, device);
	return 0;
}

struct platform_driver pdev = {
		.driver = {
			.name = DRIVER_NAME,
			.owner = THIS_MODULE,
		},
		.probe = pdev_probe,
		.remove = pdev_remove,
		.suspend = pdev_suspend,
		.resume = pdev_resume,
		.shutdown = pdev_shutdown,
};

static int __init omap_drm_init(void)
{
	DBG("init");
	return platform_driver_register(&pdev);
}

static void __exit omap_drm_fini(void)
{
	DBG("fini");
	platform_driver_unregister(&pdev);
}

/* need late_initcall() so we load after dss_driver's are loaded */
late_initcall(omap_drm_init);
module_exit(omap_drm_fini);

MODULE_AUTHOR("Rob Clark <rob@ti.com>");
MODULE_DESCRIPTION("OMAP DRM Display Driver");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_LICENSE("GPL v2");
