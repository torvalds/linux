
/*
 * drm_sysfs.c - Modifications to drm_sysfs_class.c to support
 *               extra sysfs attribute from DRM. Normal drm_sysfs_class
 *               does not allow adding attributes.
 *
 * Copyright (c) 2004 Jon Smirl <jonsmirl@gmail.com>
 * Copyright (c) 2003-2004 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (c) 2003-2004 IBM Corp.
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/gfp.h>
#include <linux/err.h>
#include <linux/export.h>

#include <drm/drm_edid.h>
#include <drm/drm_sysfs.h>
#include <drm/drm_core.h>
#include <drm/drmP.h>
#include "drm_internal.h"

#include "drm_crtc_internal.h"
#define to_drm_minor(d) dev_get_drvdata(d)
#define to_drm_connector(d) dev_get_drvdata(d)

static struct device_type drm_sysfs_device_minor = {
	.name = "drm_minor"
};

struct class *drm_class;

static const char * const audioformatstr[] = {
	"",
	"LPCM",		/*AUDIO_LPCM = 1,*/
	"AC3",		/*AUDIO_AC3,*/
	"MPEG1",	/*AUDIO_MPEG1,*/
	"MP3",		/*AUDIO_MP3,*/
	"MPEG2",	/*AUDIO_MPEG2,*/
	"AAC-LC",	/*AUDIO_AAC_LC, AAC*/
	"DTS",		/*AUDIO_DTS,*/
	"ATARC",	/*AUDIO_ATARC,*/
	"DSD",		/*AUDIO_DSD, One bit Audio */
	"E-AC3",	/*AUDIO_E_AC3,*/
	"DTS-HD",	/*AUDIO_DTS_HD,*/
	"MLP",		/*AUDIO_MLP,*/
	"DST",		/*AUDIO_DST,*/
	"WMA-PRO",	/*AUDIO_WMA_PRO*/
};

/**
 * __drm_class_suspend - internal DRM class suspend routine
 * @dev: Linux device to suspend
 * @state: power state to enter
 *
 * Just figures out what the actual struct drm_device associated with
 * @dev is and calls its suspend hook, if present.
 */
static int __drm_class_suspend(struct device *dev, pm_message_t state)
{
	if (dev->type == &drm_sysfs_device_minor) {
		struct drm_minor *drm_minor = to_drm_minor(dev);
		struct drm_device *drm_dev = drm_minor->dev;

		if (drm_minor->type == DRM_MINOR_LEGACY &&
		    !drm_core_check_feature(drm_dev, DRIVER_MODESET) &&
		    drm_dev->driver->suspend)
			return drm_dev->driver->suspend(drm_dev, state);
	}
	return 0;
}

/**
 * drm_class_suspend - internal DRM class suspend hook. Simply calls
 * __drm_class_suspend() with the correct pm state.
 * @dev: Linux device to suspend
 */
static int drm_class_suspend(struct device *dev)
{
	return __drm_class_suspend(dev, PMSG_SUSPEND);
}

/**
 * drm_class_freeze - internal DRM class freeze hook. Simply calls
 * __drm_class_suspend() with the correct pm state.
 * @dev: Linux device to freeze
 */
static int drm_class_freeze(struct device *dev)
{
	return __drm_class_suspend(dev, PMSG_FREEZE);
}

/**
 * drm_class_resume - DRM class resume hook
 * @dev: Linux device to resume
 *
 * Just figures out what the actual struct drm_device associated with
 * @dev is and calls its resume hook, if present.
 */
static int drm_class_resume(struct device *dev)
{
	if (dev->type == &drm_sysfs_device_minor) {
		struct drm_minor *drm_minor = to_drm_minor(dev);
		struct drm_device *drm_dev = drm_minor->dev;

		if (drm_minor->type == DRM_MINOR_LEGACY &&
		    !drm_core_check_feature(drm_dev, DRIVER_MODESET) &&
		    drm_dev->driver->resume)
			return drm_dev->driver->resume(drm_dev);
	}
	return 0;
}

static const struct dev_pm_ops drm_class_dev_pm_ops = {
	.suspend	= drm_class_suspend,
	.resume		= drm_class_resume,
	.freeze		= drm_class_freeze,
};

static char *drm_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "dri/%s", dev_name(dev));
}

static CLASS_ATTR_STRING(version, S_IRUGO,
		CORE_NAME " "
		__stringify(CORE_MAJOR) "."
		__stringify(CORE_MINOR) "."
		__stringify(CORE_PATCHLEVEL) " "
		CORE_DATE);

/**
 * drm_sysfs_init - initialize sysfs helpers
 *
 * This is used to create the DRM class, which is the implicit parent of any
 * other top-level DRM sysfs objects.
 *
 * You must call drm_sysfs_destroy() to release the allocated resources.
 *
 * Return: 0 on success, negative error code on failure.
 */
int drm_sysfs_init(void)
{
	int err;

	drm_class = class_create(THIS_MODULE, "drm");
	if (IS_ERR(drm_class))
		return PTR_ERR(drm_class);

	drm_class->pm = &drm_class_dev_pm_ops;

	err = class_create_file(drm_class, &class_attr_version.attr);
	if (err) {
		class_destroy(drm_class);
		drm_class = NULL;
		return err;
	}

	drm_class->devnode = drm_devnode;
	return 0;
}

/**
 * drm_sysfs_destroy - destroys DRM class
 *
 * Destroy the DRM device class.
 */
void drm_sysfs_destroy(void)
{
	if (IS_ERR_OR_NULL(drm_class))
		return;
	class_remove_file(drm_class, &class_attr_version.attr);
	class_destroy(drm_class);
	drm_class = NULL;
}

/*
 * Connector properties
 */
static ssize_t status_store(struct device *device,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_device *dev = connector->dev;
	enum drm_connector_status old_status;
	int ret;

	ret = mutex_lock_interruptible(&dev->mode_config.mutex);
	if (ret)
		return ret;

	old_status = connector->status;

	if (sysfs_streq(buf, "detect")) {
		connector->force = 0;
		connector->status = connector->funcs->detect(connector, true);
	} else if (sysfs_streq(buf, "on")) {
		connector->force = DRM_FORCE_ON;
	} else if (sysfs_streq(buf, "on-digital")) {
		connector->force = DRM_FORCE_ON_DIGITAL;
	} else if (sysfs_streq(buf, "off")) {
		connector->force = DRM_FORCE_OFF;
	} else
		ret = -EINVAL;

	if (ret == 0 && connector->force) {
		if (connector->force == DRM_FORCE_ON ||
		    connector->force == DRM_FORCE_ON_DIGITAL)
			connector->status = connector_status_connected;
		else
			connector->status = connector_status_disconnected;
		if (connector->funcs->force)
			connector->funcs->force(connector);
	}

	if (old_status != connector->status) {
		DRM_DEBUG_KMS("[CONNECTOR:%d:%s] status updated from %d to %d\n",
			      connector->base.id,
			      connector->name,
			      old_status, connector->status);

		dev->mode_config.delayed_event = true;
		if (dev->mode_config.poll_enabled)
			schedule_delayed_work(&dev->mode_config.output_poll_work,
					      0);
	}

	mutex_unlock(&dev->mode_config.mutex);

	return ret ? ret : count;
}

static ssize_t status_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			drm_get_connector_status_name(connector->status));
}

static ssize_t dpms_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	int dpms;

	dpms = READ_ONCE(connector->dpms);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			drm_get_dpms_name(dpms));
}

static ssize_t enabled_show(struct device *device,
			    struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);

	return snprintf(buf, PAGE_SIZE, "%s\n", connector->encoder ? "enabled" :
			"disabled");
}

static ssize_t content_protection_store(struct device *device,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	const int nms[] = {
		DRM_MODE_CONTENT_PROTECTION_DESIRED,
		DRM_MODE_CONTENT_PROTECTION_UNDESIRED
	};
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_device *dev = connector->dev;
	struct drm_property *prop;
	int ret, i, val = -1;

	for (i = 0; i < ARRAY_SIZE(nms); i++) {
		if (sysfs_streq(buf, drm_get_content_protection_name(nms[i])))
			val = nms[i];
	}
	if (val < 0)
		return -EINVAL;

	drm_modeset_lock_all(dev);

	prop = dev->mode_config.content_protection_property;
	if (!prop) {
		drm_modeset_unlock_all(dev);
		return count;
	}

	ret = drm_mode_connector_set_obj_prop(&connector->base, prop, val);

	drm_modeset_unlock_all(dev);
	return ret ? ret : count;
}

static ssize_t content_protection_show(struct device *device,
				       struct device_attribute *attr, char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_device *dev = connector->dev;
	struct drm_property *prop;
	uint64_t cp;
	int ret;

	drm_modeset_lock_all(dev);

	prop = dev->mode_config.content_protection_property;
	if (!prop) {
		drm_modeset_unlock_all(dev);
		return 0;
	}

	ret = drm_object_property_get_value(&connector->base, prop, &cp);
	drm_modeset_unlock_all(dev);
	if (ret)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%s\n",
			drm_get_content_protection_name((int)cp));
}

static int drm_get_audio_format(struct edid *edid,
			       char *audioformat, int len)
{
	int i, size = 0, num = 0;
	struct cea_sad *sads = NULL;

	memset(audioformat, 0, len);
	num = drm_edid_to_sad(edid, &sads);
	if (num <= 0)
		return 0;

	for (i = 0; i < num; i++) {
		if (sads[i].format < 1 || sads[i].format > 14) {
			DRM_ERROR("audio type unsupported.\n");
			continue;
		}
		size = strlen(audioformatstr[sads[i].format]);
		memcpy(audioformat, audioformatstr[sads[i].format], size);
		audioformat[size] = ',';
		audioformat += (size + 1);
	}
	kfree(sads);

	return num;
}

static ssize_t audioformat_show(struct device *device,
				struct device_attribute *attr,
				char *buf)
{
	char audioformat[200];
	int ret = 0;
	struct edid *edid;
	struct drm_connector *connector = to_drm_connector(device);

	if (!connector->edid_blob_ptr)
		return 0;

	edid = (struct edid *)connector->edid_blob_ptr->data;
	ret = drm_get_audio_format(edid, audioformat, 200);
	if (ret)
		return snprintf(buf, PAGE_SIZE, "%s\n", audioformat);

	return 0;
}

static ssize_t edid_show(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *attr, char *buf, loff_t off,
			 size_t count)
{
	struct device *connector_dev = container_of(kobj, struct device, kobj);
	struct drm_connector *connector = to_drm_connector(connector_dev);
	unsigned char *edid;
	size_t size;

	if (!connector->edid_blob_ptr)
		return 0;

	edid = connector->edid_blob_ptr->data;
	size = connector->edid_blob_ptr->length;
	if (!edid)
		return 0;

	if (off >= size)
		return 0;

	if (off + count > size)
		count = size - off;
	memcpy(buf, edid + off, count);

	return count;
}

static ssize_t modes_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_display_mode *mode;
	int written = 0;

	list_for_each_entry(mode, &connector->modes, head) {
		bool interlaced = !!(mode->flags & DRM_MODE_FLAG_INTERLACE);

		written += snprintf(buf + written, PAGE_SIZE - written,
				    "%dx%d%s%d\n",
				    mode->hdisplay, mode->vdisplay,
				    interlaced ? "i" : "p",
				    drm_mode_vrefresh(mode));
	}

	return written;
}

static ssize_t mode_show(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct drm_connector *connector = to_drm_connector(device);
	struct drm_display_mode *mode;
	struct drm_crtc_state *crtc_state;
	bool interlaced;
	int written = 0;

	if (!connector->state || !connector->state->crtc)
		return written;

	crtc_state = connector->state->crtc->state;
	if (!crtc_state)
		return written;

	mode = &crtc_state->mode;

	interlaced = !!(mode->flags & DRM_MODE_FLAG_INTERLACE);
	written += snprintf(buf + written, PAGE_SIZE - written,
			    "%dx%d%s%d\n",
			    mode->hdisplay, mode->vdisplay,
			    interlaced ? "i" : "p",
			    drm_mode_vrefresh(mode));

	return written;
}

static DEVICE_ATTR_RW(status);
static DEVICE_ATTR_RO(enabled);
static DEVICE_ATTR_RO(dpms);
static DEVICE_ATTR_RO(modes);
static DEVICE_ATTR_RO(mode);
static DEVICE_ATTR_RW(content_protection);
static DEVICE_ATTR_RO(audioformat);

static struct attribute *connector_dev_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_enabled.attr,
	&dev_attr_dpms.attr,
	&dev_attr_modes.attr,
	&dev_attr_mode.attr,
	&dev_attr_content_protection.attr,
	&dev_attr_audioformat.attr,
	NULL
};

static struct bin_attribute edid_attr = {
	.attr.name = "edid",
	.attr.mode = 0444,
	.size = 0,
	.read = edid_show,
};

static struct bin_attribute *connector_bin_attrs[] = {
	&edid_attr,
	NULL
};

static const struct attribute_group connector_dev_group = {
	.attrs = connector_dev_attrs,
	.bin_attrs = connector_bin_attrs,
};

static const struct attribute_group *connector_dev_groups[] = {
	&connector_dev_group,
	NULL
};

/**
 * drm_sysfs_connector_add - add a connector to sysfs
 * @connector: connector to add
 *
 * Create a connector device in sysfs, along with its associated connector
 * properties (so far, connection status, dpms, mode list & edid) and
 * generate a hotplug event so userspace knows there's a new connector
 * available.
 */
int drm_sysfs_connector_add(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	if (connector->kdev)
		return 0;

	connector->kdev =
		device_create_with_groups(drm_class, dev->primary->kdev, 0,
					  connector, connector_dev_groups,
					  "card%d-%s", dev->primary->index,
					  connector->name);
	DRM_DEBUG("adding \"%s\" to sysfs\n",
		  connector->name);

	if (IS_ERR(connector->kdev)) {
		DRM_ERROR("failed to register connector device: %ld\n", PTR_ERR(connector->kdev));
		return PTR_ERR(connector->kdev);
	}

	/* Let userspace know we have a new connector */
	drm_sysfs_hotplug_event(dev);

	return 0;
}

/**
 * drm_sysfs_connector_remove - remove an connector device from sysfs
 * @connector: connector to remove
 *
 * Remove @connector and its associated attributes from sysfs.  Note that
 * the device model core will take care of sending the "remove" uevent
 * at this time, so we don't need to do it.
 *
 * Note:
 * This routine should only be called if the connector was previously
 * successfully registered.  If @connector hasn't been registered yet,
 * you'll likely see a panic somewhere deep in sysfs code when called.
 */
void drm_sysfs_connector_remove(struct drm_connector *connector)
{
	if (!connector->kdev)
		return;
	DRM_DEBUG("removing \"%s\" from sysfs\n",
		  connector->name);

	device_unregister(connector->kdev);
	connector->kdev = NULL;
}

/**
 * drm_sysfs_hotplug_event - generate a DRM uevent
 * @dev: DRM device
 *
 * Send a uevent for the DRM device specified by @dev.  Currently we only
 * set HOTPLUG=1 in the uevent environment, but this could be expanded to
 * deal with other types of events.
 */
void drm_sysfs_hotplug_event(struct drm_device *dev)
{
	char *event_string = "HOTPLUG=1";
	char *envp[] = { event_string, NULL };

	DRM_DEBUG("generating hotplug event\n");

	kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL(drm_sysfs_hotplug_event);

static void drm_sysfs_release(struct device *dev)
{
	kfree(dev);
}

/**
 * drm_sysfs_minor_alloc() - Allocate sysfs device for given minor
 * @minor: minor to allocate sysfs device for
 *
 * This allocates a new sysfs device for @minor and returns it. The device is
 * not registered nor linked. The caller has to use device_add() and
 * device_del() to register and unregister it.
 *
 * Note that dev_get_drvdata() on the new device will return the minor.
 * However, the device does not hold a ref-count to the minor nor to the
 * underlying drm_device. This is unproblematic as long as you access the
 * private data only in sysfs callbacks. device_del() disables those
 * synchronously, so they cannot be called after you cleanup a minor.
 */
struct device *drm_sysfs_minor_alloc(struct drm_minor *minor)
{
	const char *minor_str;
	struct device *kdev;
	int r;

	if (minor->type == DRM_MINOR_CONTROL)
		minor_str = "controlD%d";
	else if (minor->type == DRM_MINOR_RENDER)
		minor_str = "renderD%d";
	else
		minor_str = "card%d";

	kdev = kzalloc(sizeof(*kdev), GFP_KERNEL);
	if (!kdev)
		return ERR_PTR(-ENOMEM);

	device_initialize(kdev);
	kdev->devt = MKDEV(DRM_MAJOR, minor->index);
	kdev->class = drm_class;
	kdev->type = &drm_sysfs_device_minor;
	kdev->parent = minor->dev->dev;
	kdev->release = drm_sysfs_release;
	dev_set_drvdata(kdev, minor);

	r = dev_set_name(kdev, minor_str, minor->index);
	if (r < 0)
		goto err_free;

	return kdev;

err_free:
	put_device(kdev);
	return ERR_PTR(r);
}

/**
 * drm_class_device_register - Register a struct device in the drm class.
 *
 * @dev: pointer to struct device to register.
 *
 * @dev should have all relevant members pre-filled with the exception
 * of the class member. In particular, the device_type member must
 * be set.
 */

int drm_class_device_register(struct device *dev)
{
	if (!drm_class || IS_ERR(drm_class))
		return -ENOENT;

	dev->class = drm_class;
	return device_register(dev);
}
EXPORT_SYMBOL_GPL(drm_class_device_register);

void drm_class_device_unregister(struct device *dev)
{
	return device_unregister(dev);
}
EXPORT_SYMBOL_GPL(drm_class_device_unregister);
