/*
 * drivers/gpu/drm/omapdrm/omap_drv.c
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

#include <linux/wait.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>

#include "omap_dmm_tiler.h"
#include "omap_drv.h"

#define DRIVER_NAME		MODULE_NAME
#define DRIVER_DESC		"OMAP DRM"
#define DRIVER_DATE		"20110917"
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

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
	if (priv->fbdev)
		drm_fb_helper_hotplug_event(priv->fbdev);
}

struct omap_atomic_state_commit {
	struct work_struct work;
	struct drm_device *dev;
	struct drm_atomic_state *state;
	u32 crtcs;
};

static void omap_atomic_wait_for_completion(struct drm_device *dev,
					    struct drm_atomic_state *old_state)
{
	struct drm_crtc_state *old_crtc_state;
	struct drm_crtc *crtc;
	unsigned int i;
	int ret;

	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		if (!crtc->state->enable)
			continue;

		ret = omap_crtc_wait_pending(crtc);

		if (!ret)
			dev_warn(dev->dev,
				 "atomic complete timeout (pipe %u)!\n", i);
	}
}

static void omap_atomic_complete(struct omap_atomic_state_commit *commit)
{
	struct drm_device *dev = commit->dev;
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_atomic_state *old_state = commit->state;

	/* Apply the atomic update. */
	dispc_runtime_get();

	drm_atomic_helper_commit_modeset_disables(dev, old_state);
	drm_atomic_helper_commit_planes(dev, old_state, 0);
	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	omap_atomic_wait_for_completion(dev, old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);

	dispc_runtime_put();

	drm_atomic_state_free(old_state);

	/* Complete the commit, wake up any waiter. */
	spin_lock(&priv->commit.lock);
	priv->commit.pending &= ~commit->crtcs;
	spin_unlock(&priv->commit.lock);

	wake_up_all(&priv->commit.wait);

	kfree(commit);
}

static void omap_atomic_work(struct work_struct *work)
{
	struct omap_atomic_state_commit *commit =
		container_of(work, struct omap_atomic_state_commit, work);

	omap_atomic_complete(commit);
}

static bool omap_atomic_is_pending(struct omap_drm_private *priv,
				   struct omap_atomic_state_commit *commit)
{
	bool pending;

	spin_lock(&priv->commit.lock);
	pending = priv->commit.pending & commit->crtcs;
	spin_unlock(&priv->commit.lock);

	return pending;
}

static int omap_atomic_commit(struct drm_device *dev,
			      struct drm_atomic_state *state, bool nonblock)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_atomic_state_commit *commit;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int i, ret;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		return ret;

	/* Allocate the commit object. */
	commit = kzalloc(sizeof(*commit), GFP_KERNEL);
	if (commit == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	INIT_WORK(&commit->work, omap_atomic_work);
	commit->dev = dev;
	commit->state = state;

	/* Wait until all affected CRTCs have completed previous commits and
	 * mark them as pending.
	 */
	for_each_crtc_in_state(state, crtc, crtc_state, i)
		commit->crtcs |= drm_crtc_mask(crtc);

	wait_event(priv->commit.wait, !omap_atomic_is_pending(priv, commit));

	spin_lock(&priv->commit.lock);
	priv->commit.pending |= commit->crtcs;
	spin_unlock(&priv->commit.lock);

	/* Swap the state, this is the point of no return. */
	drm_atomic_helper_swap_state(state, true);

	if (nonblock)
		schedule_work(&commit->work);
	else
		omap_atomic_complete(commit);

	return 0;

error:
	drm_atomic_helper_cleanup_planes(dev, state);
	return ret;
}

static const struct drm_mode_config_funcs omap_mode_config_funcs = {
	.fb_create = omap_framebuffer_create,
	.output_poll_changed = omap_fb_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = omap_atomic_commit,
};

static int get_connector_type(struct omap_dss_device *dssdev)
{
	switch (dssdev->type) {
	case OMAP_DISPLAY_TYPE_HDMI:
		return DRM_MODE_CONNECTOR_HDMIA;
	case OMAP_DISPLAY_TYPE_DVI:
		return DRM_MODE_CONNECTOR_DVID;
	case OMAP_DISPLAY_TYPE_DSI:
		return DRM_MODE_CONNECTOR_DSI;
	default:
		return DRM_MODE_CONNECTOR_Unknown;
	}
}

static bool channel_used(struct drm_device *dev, enum omap_channel channel)
{
	struct omap_drm_private *priv = dev->dev_private;
	int i;

	for (i = 0; i < priv->num_crtcs; i++) {
		struct drm_crtc *crtc = priv->crtcs[i];

		if (omap_crtc_channel(crtc) == channel)
			return true;
	}

	return false;
}
static void omap_disconnect_dssdevs(void)
{
	struct omap_dss_device *dssdev = NULL;

	for_each_dss_dev(dssdev)
		dssdev->driver->disconnect(dssdev);
}

static int omap_connect_dssdevs(void)
{
	int r;
	struct omap_dss_device *dssdev = NULL;
	bool no_displays = true;

	for_each_dss_dev(dssdev) {
		r = dssdev->driver->connect(dssdev);
		if (r == -EPROBE_DEFER) {
			omap_dss_put_device(dssdev);
			goto cleanup;
		} else if (r) {
			dev_warn(dssdev->dev, "could not connect display: %s\n",
				dssdev->name);
		} else {
			no_displays = false;
		}
	}

	if (no_displays)
		return -EPROBE_DEFER;

	return 0;

cleanup:
	/*
	 * if we are deferring probe, we disconnect the devices we previously
	 * connected
	 */
	omap_disconnect_dssdevs();

	return r;
}

static int omap_modeset_create_crtc(struct drm_device *dev, int id,
				    enum omap_channel channel)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_plane *plane;
	struct drm_crtc *crtc;

	plane = omap_plane_init(dev, id, DRM_PLANE_TYPE_PRIMARY);
	if (IS_ERR(plane))
		return PTR_ERR(plane);

	crtc = omap_crtc_init(dev, plane, channel, id);

	BUG_ON(priv->num_crtcs >= ARRAY_SIZE(priv->crtcs));
	priv->crtcs[id] = crtc;
	priv->num_crtcs++;

	priv->planes[id] = plane;
	priv->num_planes++;

	return 0;
}

static int omap_modeset_init_properties(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;

	if (priv->has_dmm) {
		dev->mode_config.rotation_property =
			drm_mode_create_rotation_property(dev,
				DRM_ROTATE_0 | DRM_ROTATE_90 |
				DRM_ROTATE_180 | DRM_ROTATE_270 |
				DRM_REFLECT_X | DRM_REFLECT_Y);
		if (!dev->mode_config.rotation_property)
			return -ENOMEM;
	}

	priv->zorder_prop = drm_property_create_range(dev, 0, "zorder", 0, 3);
	if (!priv->zorder_prop)
		return -ENOMEM;

	return 0;
}

static int omap_modeset_init(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_dss_device *dssdev = NULL;
	int num_ovls = dss_feat_get_num_ovls();
	int num_mgrs = dss_feat_get_num_mgrs();
	int num_crtcs;
	int i, id = 0;
	int ret;

	drm_mode_config_init(dev);

	omap_drm_irq_install(dev);

	ret = omap_modeset_init_properties(dev);
	if (ret < 0)
		return ret;

	/*
	 * We usually don't want to create a CRTC for each manager, at least
	 * not until we have a way to expose private planes to userspace.
	 * Otherwise there would not be enough video pipes left for drm planes.
	 * We use the num_crtc argument to limit the number of crtcs we create.
	 */
	num_crtcs = min3(num_crtc, num_mgrs, num_ovls);

	dssdev = NULL;

	for_each_dss_dev(dssdev) {
		struct drm_connector *connector;
		struct drm_encoder *encoder;
		enum omap_channel channel;
		struct omap_dss_device *out;

		if (!omapdss_device_is_connected(dssdev))
			continue;

		encoder = omap_encoder_init(dev, dssdev);

		if (!encoder) {
			dev_err(dev->dev, "could not create encoder: %s\n",
					dssdev->name);
			return -ENOMEM;
		}

		connector = omap_connector_init(dev,
				get_connector_type(dssdev), dssdev, encoder);

		if (!connector) {
			dev_err(dev->dev, "could not create connector: %s\n",
					dssdev->name);
			return -ENOMEM;
		}

		BUG_ON(priv->num_encoders >= ARRAY_SIZE(priv->encoders));
		BUG_ON(priv->num_connectors >= ARRAY_SIZE(priv->connectors));

		priv->encoders[priv->num_encoders++] = encoder;
		priv->connectors[priv->num_connectors++] = connector;

		drm_mode_connector_attach_encoder(connector, encoder);

		/*
		 * if we have reached the limit of the crtcs we are allowed to
		 * create, let's not try to look for a crtc for this
		 * panel/encoder and onwards, we will, of course, populate the
		 * the possible_crtcs field for all the encoders with the final
		 * set of crtcs we create
		 */
		if (id == num_crtcs)
			continue;

		/*
		 * get the recommended DISPC channel for this encoder. For now,
		 * we only try to get create a crtc out of the recommended, the
		 * other possible channels to which the encoder can connect are
		 * not considered.
		 */

		out = omapdss_find_output_from_display(dssdev);
		channel = out->dispc_channel;
		omap_dss_put_device(out);

		/*
		 * if this channel hasn't already been taken by a previously
		 * allocated crtc, we create a new crtc for it
		 */
		if (!channel_used(dev, channel)) {
			ret = omap_modeset_create_crtc(dev, id, channel);
			if (ret < 0) {
				dev_err(dev->dev,
					"could not create CRTC (channel %u)\n",
					channel);
				return ret;
			}

			id++;
		}
	}

	/*
	 * we have allocated crtcs according to the need of the panels/encoders,
	 * adding more crtcs here if needed
	 */
	for (; id < num_crtcs; id++) {

		/* find a free manager for this crtc */
		for (i = 0; i < num_mgrs; i++) {
			if (!channel_used(dev, i))
				break;
		}

		if (i == num_mgrs) {
			/* this shouldn't really happen */
			dev_err(dev->dev, "no managers left for crtc\n");
			return -ENOMEM;
		}

		ret = omap_modeset_create_crtc(dev, id, i);
		if (ret < 0) {
			dev_err(dev->dev,
				"could not create CRTC (channel %u)\n", i);
			return ret;
		}
	}

	/*
	 * Create normal planes for the remaining overlays:
	 */
	for (; id < num_ovls; id++) {
		struct drm_plane *plane;

		plane = omap_plane_init(dev, id, DRM_PLANE_TYPE_OVERLAY);
		if (IS_ERR(plane))
			return PTR_ERR(plane);

		BUG_ON(priv->num_planes >= ARRAY_SIZE(priv->planes));
		priv->planes[priv->num_planes++] = plane;
	}

	for (i = 0; i < priv->num_encoders; i++) {
		struct drm_encoder *encoder = priv->encoders[i];
		struct omap_dss_device *dssdev =
					omap_encoder_get_dssdev(encoder);
		struct omap_dss_device *output;

		output = omapdss_find_output_from_display(dssdev);

		/* figure out which crtc's we can connect the encoder to: */
		encoder->possible_crtcs = 0;
		for (id = 0; id < priv->num_crtcs; id++) {
			struct drm_crtc *crtc = priv->crtcs[id];
			enum omap_channel crtc_channel;

			crtc_channel = omap_crtc_channel(crtc);

			if (output->dispc_channel == crtc_channel) {
				encoder->possible_crtcs |= (1 << id);
				break;
			}
		}

		omap_dss_put_device(output);
	}

	DBG("registered %d planes, %d crtcs, %d encoders and %d connectors\n",
		priv->num_planes, priv->num_crtcs, priv->num_encoders,
		priv->num_connectors);

	dev->mode_config.min_width = 32;
	dev->mode_config.min_height = 32;

	/* note: eventually will need some cpu_is_omapXYZ() type stuff here
	 * to fill in these limits properly on different OMAP generations..
	 */
	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;

	dev->mode_config.funcs = &omap_mode_config_funcs;

	drm_mode_config_reset(dev);

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
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_omap_param *args = data;

	DBG("%p: param=%llu", dev, args->param);

	switch (args->param) {
	case OMAP_PARAM_CHIPSET_ID:
		args->value = priv->omaprev;
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

#define OMAP_BO_USER_MASK	0x00ffffff	/* flags settable by userspace */

static int ioctl_gem_new(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_omap_gem_new *args = data;
	u32 flags = args->flags & OMAP_BO_USER_MASK;

	VERB("%p:%p: size=0x%08x, flags=%08x", dev, file_priv,
	     args->size.bytes, flags);

	return omap_gem_new_handle(dev, file_priv, args->size, flags,
				   &args->handle);
}

static int ioctl_gem_cpu_prep(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_omap_gem_cpu_prep *args = data;
	struct drm_gem_object *obj;
	int ret;

	VERB("%p:%p: handle=%d, op=%x", dev, file_priv, args->handle, args->op);

	obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!obj)
		return -ENOENT;

	ret = omap_gem_op_sync(obj, args->op);

	if (!ret)
		ret = omap_gem_op_start(obj, args->op);

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

	obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!obj)
		return -ENOENT;

	/* XXX flushy, flushy */
	ret = 0;

	if (!ret)
		ret = omap_gem_op_finish(obj, args->op);

	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static int ioctl_gem_info(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_omap_gem_info *args = data;
	struct drm_gem_object *obj;
	int ret = 0;

	VERB("%p:%p: handle=%d", dev, file_priv, args->handle);

	obj = drm_gem_object_lookup(file_priv, args->handle);
	if (!obj)
		return -ENOENT;

	args->size = omap_gem_mmap_size(obj);
	args->offset = omap_gem_mmap_offset(obj);

	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static const struct drm_ioctl_desc ioctls[DRM_COMMAND_END - DRM_COMMAND_BASE] = {
	DRM_IOCTL_DEF_DRV(OMAP_GET_PARAM, ioctl_get_param, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(OMAP_SET_PARAM, ioctl_set_param, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(OMAP_GEM_NEW, ioctl_gem_new, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(OMAP_GEM_CPU_PREP, ioctl_gem_cpu_prep, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(OMAP_GEM_CPU_FINI, ioctl_gem_cpu_fini, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(OMAP_GEM_INFO, ioctl_gem_info, DRM_AUTH),
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
	struct omap_drm_platform_data *pdata = dev->dev->platform_data;
	struct omap_drm_private *priv;
	unsigned int i;
	int ret;

	DBG("load: dev=%p", dev);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->omaprev = pdata->omaprev;

	dev->dev_private = priv;

	priv->wq = alloc_ordered_workqueue("omapdrm", 0);
	init_waitqueue_head(&priv->commit.wait);
	spin_lock_init(&priv->commit.lock);

	spin_lock_init(&priv->list_lock);
	INIT_LIST_HEAD(&priv->obj_list);

	omap_gem_init(dev);

	ret = omap_modeset_init(dev);
	if (ret) {
		dev_err(dev->dev, "omap_modeset_init failed: ret=%d\n", ret);
		dev->dev_private = NULL;
		kfree(priv);
		return ret;
	}

	/* Initialize vblank handling, start with all CRTCs disabled. */
	ret = drm_vblank_init(dev, priv->num_crtcs);
	if (ret)
		dev_warn(dev->dev, "could not init vblank\n");

	for (i = 0; i < priv->num_crtcs; i++)
		drm_crtc_vblank_off(priv->crtcs[i]);

	priv->fbdev = omap_fbdev_init(dev);

	/* store off drm_device for use in pm ops */
	dev_set_drvdata(dev->dev, dev);

	drm_kms_helper_poll_init(dev);

	return 0;
}

static int dev_unload(struct drm_device *dev)
{
	struct omap_drm_private *priv = dev->dev_private;

	DBG("unload: dev=%p", dev);

	drm_kms_helper_poll_fini(dev);

	if (priv->fbdev)
		omap_fbdev_free(dev);

	omap_modeset_free(dev);
	omap_gem_deinit(dev);

	destroy_workqueue(priv->wq);

	drm_vblank_cleanup(dev);
	omap_drm_irq_uninstall(dev);

	kfree(dev->dev_private);
	dev->dev_private = NULL;

	dev_set_drvdata(dev->dev, NULL);

	return 0;
}

static int dev_open(struct drm_device *dev, struct drm_file *file)
{
	file->driver_priv = NULL;

	DBG("open: dev=%p, file=%p", dev, file);

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
	int i;

	/* we don't support vga_switcheroo.. so just make sure the fbdev
	 * mode is active
	 */
	struct omap_drm_private *priv = dev->dev_private;
	int ret;

	DBG("lastclose: dev=%p", dev);

	if (dev->mode_config.rotation_property) {
		/* need to restore default rotation state.. not sure
		 * if there is a cleaner way to restore properties to
		 * default state?  Maybe a flag that properties should
		 * automatically be restored to default state on
		 * lastclose?
		 */
		for (i = 0; i < priv->num_crtcs; i++) {
			drm_object_property_set_value(&priv->crtcs[i]->base,
					dev->mode_config.rotation_property, 0);
		}

		for (i = 0; i < priv->num_planes; i++) {
			drm_object_property_set_value(&priv->planes[i]->base,
					dev->mode_config.rotation_property, 0);
		}
	}

	if (priv->fbdev) {
		ret = drm_fb_helper_restore_fbdev_mode_unlocked(priv->fbdev);
		if (ret)
			DBG("failed to restore crtc mode");
	}
}

static const struct vm_operations_struct omap_gem_vm_ops = {
	.fault = omap_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct file_operations omapdriver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.unlocked_ioctl = drm_ioctl,
	.release = drm_release,
	.mmap = omap_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.llseek = noop_llseek,
};

static struct drm_driver omap_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM  | DRIVER_PRIME |
		DRIVER_ATOMIC,
	.load = dev_load,
	.unload = dev_unload,
	.open = dev_open,
	.lastclose = dev_lastclose,
	.get_vblank_counter = drm_vblank_no_hw_counter,
	.enable_vblank = omap_irq_enable_vblank,
	.disable_vblank = omap_irq_disable_vblank,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init = omap_debugfs_init,
	.debugfs_cleanup = omap_debugfs_cleanup,
#endif
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = omap_gem_prime_export,
	.gem_prime_import = omap_gem_prime_import,
	.gem_free_object = omap_gem_free_object,
	.gem_vm_ops = &omap_gem_vm_ops,
	.dumb_create = omap_gem_dumb_create,
	.dumb_map_offset = omap_gem_dumb_map_offset,
	.dumb_destroy = drm_gem_dumb_destroy,
	.ioctls = ioctls,
	.num_ioctls = DRM_OMAP_NUM_IOCTLS,
	.fops = &omapdriver_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int pdev_probe(struct platform_device *device)
{
	int r;

	if (omapdss_is_initialized() == false)
		return -EPROBE_DEFER;

	omap_crtc_pre_init();

	r = omap_connect_dssdevs();
	if (r) {
		omap_crtc_pre_uninit();
		return r;
	}

	DBG("%s", device->name);
	return drm_platform_init(&omap_drm_driver, device);
}

static int pdev_remove(struct platform_device *device)
{
	DBG("");

	drm_put_dev(platform_get_drvdata(device));

	omap_disconnect_dssdevs();
	omap_crtc_pre_uninit();

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int omap_drm_suspend_all_displays(void)
{
	struct omap_dss_device *dssdev = NULL;

	for_each_dss_dev(dssdev) {
		if (!dssdev->driver)
			continue;

		if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE) {
			dssdev->driver->disable(dssdev);
			dssdev->activate_after_resume = true;
		} else {
			dssdev->activate_after_resume = false;
		}
	}

	return 0;
}

static int omap_drm_resume_all_displays(void)
{
	struct omap_dss_device *dssdev = NULL;

	for_each_dss_dev(dssdev) {
		if (!dssdev->driver)
			continue;

		if (dssdev->activate_after_resume) {
			dssdev->driver->enable(dssdev);
			dssdev->activate_after_resume = false;
		}
	}

	return 0;
}

static int omap_drm_suspend(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	drm_kms_helper_poll_disable(drm_dev);

	drm_modeset_lock_all(drm_dev);
	omap_drm_suspend_all_displays();
	drm_modeset_unlock_all(drm_dev);

	return 0;
}

static int omap_drm_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	drm_modeset_lock_all(drm_dev);
	omap_drm_resume_all_displays();
	drm_modeset_unlock_all(drm_dev);

	drm_kms_helper_poll_enable(drm_dev);

	return omap_gem_resume(dev);
}
#endif

static SIMPLE_DEV_PM_OPS(omapdrm_pm_ops, omap_drm_suspend, omap_drm_resume);

static struct platform_driver pdev = {
	.driver = {
		.name = DRIVER_NAME,
		.pm = &omapdrm_pm_ops,
	},
	.probe = pdev_probe,
	.remove = pdev_remove,
};

static struct platform_driver * const drivers[] = {
	&omap_dmm_driver,
	&pdev,
};

static int __init omap_drm_init(void)
{
	DBG("init");

	return platform_register_drivers(drivers, ARRAY_SIZE(drivers));
}

static void __exit omap_drm_fini(void)
{
	DBG("fini");

	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}

/* need late_initcall() so we load after dss_driver's are loaded */
late_initcall(omap_drm_init);
module_exit(omap_drm_fini);

MODULE_AUTHOR("Rob Clark <rob@ti.com>");
MODULE_DESCRIPTION("OMAP DRM Display Driver");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_LICENSE("GPL v2");
