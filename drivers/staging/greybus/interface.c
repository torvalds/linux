/*
 * Greybus interface code
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/delay.h>

#include "greybus.h"
#include "greybus_trace.h"

#define GB_INTERFACE_MODE_SWITCH_TIMEOUT	2000

#define GB_INTERFACE_DEVICE_ID_BAD	0xff

#define GB_INTERFACE_AUTOSUSPEND_MS			3000

/* Time required for interface to enter standby before disabling REFCLK */
#define GB_INTERFACE_SUSPEND_HIBERNATE_DELAY_MS			20

/* Don't-care selector index */
#define DME_SELECTOR_INDEX_NULL		0

/* DME attributes */
/* FIXME: remove ES2 support and DME_T_TST_SRC_INCREMENT */
#define DME_T_TST_SRC_INCREMENT		0x4083

#define DME_DDBL1_MANUFACTURERID	0x5003
#define DME_DDBL1_PRODUCTID		0x5004

#define DME_TOSHIBA_ARA_VID		0x6000
#define DME_TOSHIBA_ARA_PID		0x6001
#define DME_TOSHIBA_ARA_SN0		0x6002
#define DME_TOSHIBA_ARA_SN1		0x6003
#define DME_TOSHIBA_ARA_INIT_STATUS	0x6101

/* DDBL1 Manufacturer and Product ids */
#define TOSHIBA_DMID			0x0126
#define TOSHIBA_ES2_BRIDGE_DPID		0x1000
#define TOSHIBA_ES3_APBRIDGE_DPID	0x1001
#define TOSHIBA_ES3_GBPHY_DPID	0x1002

static int gb_interface_hibernate_link(struct gb_interface *intf);
static int gb_interface_refclk_set(struct gb_interface *intf, bool enable);

static int gb_interface_dme_attr_get(struct gb_interface *intf,
							u16 attr, u32 *val)
{
	return gb_svc_dme_peer_get(intf->hd->svc, intf->interface_id,
					attr, DME_SELECTOR_INDEX_NULL, val);
}

static int gb_interface_read_ara_dme(struct gb_interface *intf)
{
	u32 sn0, sn1;
	int ret;

	/*
	 * Unless this is a Toshiba bridge, bail out until we have defined
	 * standard Ara attributes.
	 */
	if (intf->ddbl1_manufacturer_id != TOSHIBA_DMID) {
		dev_err(&intf->dev, "unknown manufacturer %08x\n",
				intf->ddbl1_manufacturer_id);
		return -ENODEV;
	}

	ret = gb_interface_dme_attr_get(intf, DME_TOSHIBA_ARA_VID,
					&intf->vendor_id);
	if (ret)
		return ret;

	ret = gb_interface_dme_attr_get(intf, DME_TOSHIBA_ARA_PID,
					&intf->product_id);
	if (ret)
		return ret;

	ret = gb_interface_dme_attr_get(intf, DME_TOSHIBA_ARA_SN0, &sn0);
	if (ret)
		return ret;

	ret = gb_interface_dme_attr_get(intf, DME_TOSHIBA_ARA_SN1, &sn1);
	if (ret)
		return ret;

	intf->serial_number = (u64)sn1 << 32 | sn0;

	return 0;
}

static int gb_interface_read_dme(struct gb_interface *intf)
{
	int ret;

	/* DME attributes have already been read */
	if (intf->dme_read)
		return 0;

	ret = gb_interface_dme_attr_get(intf, DME_DDBL1_MANUFACTURERID,
					&intf->ddbl1_manufacturer_id);
	if (ret)
		return ret;

	ret = gb_interface_dme_attr_get(intf, DME_DDBL1_PRODUCTID,
					&intf->ddbl1_product_id);
	if (ret)
		return ret;

	if (intf->ddbl1_manufacturer_id == TOSHIBA_DMID &&
			intf->ddbl1_product_id == TOSHIBA_ES2_BRIDGE_DPID) {
		intf->quirks |= GB_INTERFACE_QUIRK_NO_ARA_IDS;
		intf->quirks |= GB_INTERFACE_QUIRK_NO_INIT_STATUS;
	}

	ret = gb_interface_read_ara_dme(intf);
	if (ret)
		return ret;

	intf->dme_read = true;

	return 0;
}

static int gb_interface_route_create(struct gb_interface *intf)
{
	struct gb_svc *svc = intf->hd->svc;
	u8 intf_id = intf->interface_id;
	u8 device_id;
	int ret;

	/* Allocate an interface device id. */
	ret = ida_simple_get(&svc->device_id_map,
			     GB_SVC_DEVICE_ID_MIN, GB_SVC_DEVICE_ID_MAX + 1,
			     GFP_KERNEL);
	if (ret < 0) {
		dev_err(&intf->dev, "failed to allocate device id: %d\n", ret);
		return ret;
	}
	device_id = ret;

	ret = gb_svc_intf_device_id(svc, intf_id, device_id);
	if (ret) {
		dev_err(&intf->dev, "failed to set device id %u: %d\n",
				device_id, ret);
		goto err_ida_remove;
	}

	/* FIXME: Hard-coded AP device id. */
	ret = gb_svc_route_create(svc, svc->ap_intf_id, GB_SVC_DEVICE_ID_AP,
				  intf_id, device_id);
	if (ret) {
		dev_err(&intf->dev, "failed to create route: %d\n", ret);
		goto err_svc_id_free;
	}

	intf->device_id = device_id;

	return 0;

err_svc_id_free:
	/*
	 * XXX Should we tell SVC that this id doesn't belong to interface
	 * XXX anymore.
	 */
err_ida_remove:
	ida_simple_remove(&svc->device_id_map, device_id);

	return ret;
}

static void gb_interface_route_destroy(struct gb_interface *intf)
{
	struct gb_svc *svc = intf->hd->svc;

	if (intf->device_id == GB_INTERFACE_DEVICE_ID_BAD)
		return;

	gb_svc_route_destroy(svc, svc->ap_intf_id, intf->interface_id);
	ida_simple_remove(&svc->device_id_map, intf->device_id);
	intf->device_id = GB_INTERFACE_DEVICE_ID_BAD;
}

/* Locking: Caller holds the interface mutex. */
static int gb_interface_legacy_mode_switch(struct gb_interface *intf)
{
	int ret;

	dev_info(&intf->dev, "legacy mode switch detected\n");

	/* Mark as disconnected to prevent I/O during disable. */
	intf->disconnected = true;
	gb_interface_disable(intf);
	intf->disconnected = false;

	ret = gb_interface_enable(intf);
	if (ret) {
		dev_err(&intf->dev, "failed to re-enable interface: %d\n", ret);
		gb_interface_deactivate(intf);
	}

	return ret;
}

void gb_interface_mailbox_event(struct gb_interface *intf, u16 result,
								u32 mailbox)
{
	mutex_lock(&intf->mutex);

	if (result) {
		dev_warn(&intf->dev,
				"mailbox event with UniPro error: 0x%04x\n",
				result);
		goto err_disable;
	}

	if (mailbox != GB_SVC_INTF_MAILBOX_GREYBUS) {
		dev_warn(&intf->dev,
				"mailbox event with unexpected value: 0x%08x\n",
				mailbox);
		goto err_disable;
	}

	if (intf->quirks & GB_INTERFACE_QUIRK_LEGACY_MODE_SWITCH) {
		gb_interface_legacy_mode_switch(intf);
		goto out_unlock;
	}

	if (!intf->mode_switch) {
		dev_warn(&intf->dev, "unexpected mailbox event: 0x%08x\n",
				mailbox);
		goto err_disable;
	}

	dev_info(&intf->dev, "mode switch detected\n");

	complete(&intf->mode_switch_completion);

out_unlock:
	mutex_unlock(&intf->mutex);

	return;

err_disable:
	gb_interface_disable(intf);
	gb_interface_deactivate(intf);
	mutex_unlock(&intf->mutex);
}

static void gb_interface_mode_switch_work(struct work_struct *work)
{
	struct gb_interface *intf;
	struct gb_control *control;
	unsigned long timeout;
	int ret;

	intf = container_of(work, struct gb_interface, mode_switch_work);

	mutex_lock(&intf->mutex);
	/* Make sure interface is still enabled. */
	if (!intf->enabled) {
		dev_dbg(&intf->dev, "mode switch aborted\n");
		intf->mode_switch = false;
		mutex_unlock(&intf->mutex);
		goto out_interface_put;
	}

	/*
	 * Prepare the control device for mode switch and make sure to get an
	 * extra reference before it goes away during interface disable.
	 */
	control = gb_control_get(intf->control);
	gb_control_mode_switch_prepare(control);
	gb_interface_disable(intf);
	mutex_unlock(&intf->mutex);

	timeout = msecs_to_jiffies(GB_INTERFACE_MODE_SWITCH_TIMEOUT);
	ret = wait_for_completion_interruptible_timeout(
			&intf->mode_switch_completion, timeout);

	/* Finalise control-connection mode switch. */
	gb_control_mode_switch_complete(control);
	gb_control_put(control);

	if (ret < 0) {
		dev_err(&intf->dev, "mode switch interrupted\n");
		goto err_deactivate;
	} else if (ret == 0) {
		dev_err(&intf->dev, "mode switch timed out\n");
		goto err_deactivate;
	}

	/* Re-enable (re-enumerate) interface if still active. */
	mutex_lock(&intf->mutex);
	intf->mode_switch = false;
	if (intf->active) {
		ret = gb_interface_enable(intf);
		if (ret) {
			dev_err(&intf->dev, "failed to re-enable interface: %d\n",
					ret);
			gb_interface_deactivate(intf);
		}
	}
	mutex_unlock(&intf->mutex);

out_interface_put:
	gb_interface_put(intf);

	return;

err_deactivate:
	mutex_lock(&intf->mutex);
	intf->mode_switch = false;
	gb_interface_deactivate(intf);
	mutex_unlock(&intf->mutex);

	gb_interface_put(intf);
}

int gb_interface_request_mode_switch(struct gb_interface *intf)
{
	int ret = 0;

	mutex_lock(&intf->mutex);
	if (intf->mode_switch) {
		ret = -EBUSY;
		goto out_unlock;
	}

	intf->mode_switch = true;
	reinit_completion(&intf->mode_switch_completion);

	/*
	 * Get a reference to the interface device, which will be put once the
	 * mode switch is complete.
	 */
	get_device(&intf->dev);

	if (!queue_work(system_long_wq, &intf->mode_switch_work)) {
		put_device(&intf->dev);
		ret = -EBUSY;
		goto out_unlock;
	}

out_unlock:
	mutex_unlock(&intf->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(gb_interface_request_mode_switch);

/*
 * T_TstSrcIncrement is written by the module on ES2 as a stand-in for the
 * init-status attribute DME_TOSHIBA_INIT_STATUS. The AP needs to read and
 * clear it after reading a non-zero value from it.
 *
 * FIXME: This is module-hardware dependent and needs to be extended for every
 * type of module we want to support.
 */
static int gb_interface_read_and_clear_init_status(struct gb_interface *intf)
{
	struct gb_host_device *hd = intf->hd;
	unsigned long bootrom_quirks;
	int ret;
	u32 value;
	u16 attr;
	u8 init_status;

	/*
	 * ES2 bridges use T_TstSrcIncrement for the init status.
	 *
	 * FIXME: Remove ES2 support
	 */
	if (intf->quirks & GB_INTERFACE_QUIRK_NO_INIT_STATUS)
		attr = DME_T_TST_SRC_INCREMENT;
	else
		attr = DME_TOSHIBA_ARA_INIT_STATUS;

	ret = gb_svc_dme_peer_get(hd->svc, intf->interface_id, attr,
				  DME_SELECTOR_INDEX_NULL, &value);
	if (ret)
		return ret;

	/*
	 * A nonzero init status indicates the module has finished
	 * initializing.
	 */
	if (!value) {
		dev_err(&intf->dev, "invalid init status\n");
		return -ENODEV;
	}

	/*
	 * Extract the init status.
	 *
	 * For ES2: We need to check lowest 8 bits of 'value'.
	 * For ES3: We need to check highest 8 bits out of 32 of 'value'.
	 *
	 * FIXME: Remove ES2 support
	 */
	if (intf->quirks & GB_INTERFACE_QUIRK_NO_INIT_STATUS)
		init_status = value & 0xff;
	else
		init_status = value >> 24;

	/*
	 * Check if the interface is executing the quirky ES3 bootrom that,
	 * for example, requires E2EFC, CSD and CSV to be disabled.
	 */
	bootrom_quirks = GB_INTERFACE_QUIRK_NO_CPORT_FEATURES |
				GB_INTERFACE_QUIRK_FORCED_DISABLE |
				GB_INTERFACE_QUIRK_LEGACY_MODE_SWITCH;
	switch (init_status) {
	case GB_INIT_BOOTROM_UNIPRO_BOOT_STARTED:
	case GB_INIT_BOOTROM_FALLBACK_UNIPRO_BOOT_STARTED:
		intf->quirks |= bootrom_quirks;
		break;
	default:
		intf->quirks &= ~bootrom_quirks;
	}

	/* Clear the init status. */
	return gb_svc_dme_peer_set(hd->svc, intf->interface_id, attr,
				   DME_SELECTOR_INDEX_NULL, 0);
}

/* interface sysfs attributes */
#define gb_interface_attr(field, type)					\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct gb_interface *intf = to_gb_interface(dev);		\
	return scnprintf(buf, PAGE_SIZE, type"\n", intf->field);	\
}									\
static DEVICE_ATTR_RO(field)

gb_interface_attr(ddbl1_manufacturer_id, "0x%08x");
gb_interface_attr(ddbl1_product_id, "0x%08x");
gb_interface_attr(interface_id, "%u");
gb_interface_attr(vendor_id, "0x%08x");
gb_interface_attr(product_id, "0x%08x");
gb_interface_attr(serial_number, "0x%016llx");

static ssize_t voltage_now_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gb_interface *intf = to_gb_interface(dev);
	int ret;
	u32 measurement;

	ret = gb_svc_pwrmon_intf_sample_get(intf->hd->svc, intf->interface_id,
					    GB_SVC_PWRMON_TYPE_VOL,
					    &measurement);
	if (ret) {
		dev_err(&intf->dev, "failed to get voltage sample (%d)\n", ret);
		return ret;
	}

	return sprintf(buf, "%u\n", measurement);
}
static DEVICE_ATTR_RO(voltage_now);

static ssize_t current_now_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gb_interface *intf = to_gb_interface(dev);
	int ret;
	u32 measurement;

	ret = gb_svc_pwrmon_intf_sample_get(intf->hd->svc, intf->interface_id,
					    GB_SVC_PWRMON_TYPE_CURR,
					    &measurement);
	if (ret) {
		dev_err(&intf->dev, "failed to get current sample (%d)\n", ret);
		return ret;
	}

	return sprintf(buf, "%u\n", measurement);
}
static DEVICE_ATTR_RO(current_now);

static ssize_t power_now_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gb_interface *intf = to_gb_interface(dev);
	int ret;
	u32 measurement;

	ret = gb_svc_pwrmon_intf_sample_get(intf->hd->svc, intf->interface_id,
					    GB_SVC_PWRMON_TYPE_PWR,
					    &measurement);
	if (ret) {
		dev_err(&intf->dev, "failed to get power sample (%d)\n", ret);
		return ret;
	}

	return sprintf(buf, "%u\n", measurement);
}
static DEVICE_ATTR_RO(power_now);

static ssize_t power_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gb_interface *intf = to_gb_interface(dev);

	if (intf->active)
		return scnprintf(buf, PAGE_SIZE, "on\n");
	else
		return scnprintf(buf, PAGE_SIZE, "off\n");
}

static ssize_t power_state_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t len)
{
	struct gb_interface *intf = to_gb_interface(dev);
	bool activate;
	int ret = 0;

	if (kstrtobool(buf, &activate))
		return -EINVAL;

	mutex_lock(&intf->mutex);

	if (activate == intf->active)
		goto unlock;

	if (activate) {
		ret = gb_interface_activate(intf);
		if (ret) {
			dev_err(&intf->dev,
				"failed to activate interface: %d\n", ret);
			goto unlock;
		}

		ret = gb_interface_enable(intf);
		if (ret) {
			dev_err(&intf->dev,
				"failed to enable interface: %d\n", ret);
			gb_interface_deactivate(intf);
			goto unlock;
		}
	} else {
		gb_interface_disable(intf);
		gb_interface_deactivate(intf);
	}

unlock:
	mutex_unlock(&intf->mutex);

	if (ret)
		return ret;

	return len;
}
static DEVICE_ATTR_RW(power_state);

static const char *gb_interface_type_string(struct gb_interface *intf)
{
	static const char * const types[] = {
		[GB_INTERFACE_TYPE_INVALID] = "invalid",
		[GB_INTERFACE_TYPE_UNKNOWN] = "unknown",
		[GB_INTERFACE_TYPE_DUMMY] = "dummy",
		[GB_INTERFACE_TYPE_UNIPRO] = "unipro",
		[GB_INTERFACE_TYPE_GREYBUS] = "greybus",
	};

	return types[intf->type];
}

static ssize_t interface_type_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct gb_interface *intf = to_gb_interface(dev);

	return sprintf(buf, "%s\n", gb_interface_type_string(intf));
}
static DEVICE_ATTR_RO(interface_type);

static struct attribute *interface_unipro_attrs[] = {
	&dev_attr_ddbl1_manufacturer_id.attr,
	&dev_attr_ddbl1_product_id.attr,
	NULL
};

static struct attribute *interface_greybus_attrs[] = {
	&dev_attr_vendor_id.attr,
	&dev_attr_product_id.attr,
	&dev_attr_serial_number.attr,
	NULL
};

static struct attribute *interface_power_attrs[] = {
	&dev_attr_voltage_now.attr,
	&dev_attr_current_now.attr,
	&dev_attr_power_now.attr,
	&dev_attr_power_state.attr,
	NULL
};

static struct attribute *interface_common_attrs[] = {
	&dev_attr_interface_id.attr,
	&dev_attr_interface_type.attr,
	NULL
};

static umode_t interface_unipro_is_visible(struct kobject *kobj,
						struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct gb_interface *intf = to_gb_interface(dev);

	switch (intf->type) {
	case GB_INTERFACE_TYPE_UNIPRO:
	case GB_INTERFACE_TYPE_GREYBUS:
		return attr->mode;
	default:
		return 0;
	}
}

static umode_t interface_greybus_is_visible(struct kobject *kobj,
						struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct gb_interface *intf = to_gb_interface(dev);

	switch (intf->type) {
	case GB_INTERFACE_TYPE_GREYBUS:
		return attr->mode;
	default:
		return 0;
	}
}

static umode_t interface_power_is_visible(struct kobject *kobj,
						struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct gb_interface *intf = to_gb_interface(dev);

	switch (intf->type) {
	case GB_INTERFACE_TYPE_UNIPRO:
	case GB_INTERFACE_TYPE_GREYBUS:
		return attr->mode;
	default:
		return 0;
	}
}

static const struct attribute_group interface_unipro_group = {
	.is_visible	= interface_unipro_is_visible,
	.attrs		= interface_unipro_attrs,
};

static const struct attribute_group interface_greybus_group = {
	.is_visible	= interface_greybus_is_visible,
	.attrs		= interface_greybus_attrs,
};

static const struct attribute_group interface_power_group = {
	.is_visible	= interface_power_is_visible,
	.attrs		= interface_power_attrs,
};

static const struct attribute_group interface_common_group = {
	.attrs		= interface_common_attrs,
};

static const struct attribute_group *interface_groups[] = {
	&interface_unipro_group,
	&interface_greybus_group,
	&interface_power_group,
	&interface_common_group,
	NULL
};

static void gb_interface_release(struct device *dev)
{
	struct gb_interface *intf = to_gb_interface(dev);

	trace_gb_interface_release(intf);

	kfree(intf);
}

#ifdef CONFIG_PM_RUNTIME
static int gb_interface_suspend(struct device *dev)
{
	struct gb_interface *intf = to_gb_interface(dev);
	int ret, timesync_ret;

	ret = gb_control_interface_suspend_prepare(intf->control);
	if (ret)
		return ret;

	gb_timesync_interface_remove(intf);

	ret = gb_control_suspend(intf->control);
	if (ret)
		goto err_hibernate_abort;

	ret = gb_interface_hibernate_link(intf);
	if (ret)
		return ret;

	/* Delay to allow interface to enter standby before disabling refclk */
	msleep(GB_INTERFACE_SUSPEND_HIBERNATE_DELAY_MS);

	ret = gb_interface_refclk_set(intf, false);
	if (ret)
		return ret;

	return 0;

err_hibernate_abort:
	gb_control_interface_hibernate_abort(intf->control);

	timesync_ret = gb_timesync_interface_add(intf);
	if (timesync_ret) {
		dev_err(dev, "failed to add to timesync: %d\n", timesync_ret);
		return timesync_ret;
	}

	return ret;
}

static int gb_interface_resume(struct device *dev)
{
	struct gb_interface *intf = to_gb_interface(dev);
	struct gb_svc *svc = intf->hd->svc;
	int ret;

	ret = gb_interface_refclk_set(intf, true);
	if (ret)
		return ret;

	ret = gb_svc_intf_resume(svc, intf->interface_id);
	if (ret)
		return ret;

	ret = gb_control_resume(intf->control);
	if (ret)
		return ret;

	ret = gb_timesync_interface_add(intf);
	if (ret) {
		dev_err(dev, "failed to add to timesync: %d\n", ret);
		return ret;
	}

	ret = gb_timesync_schedule_synchronous(intf);
	if (ret) {
		dev_err(dev, "failed to synchronize FrameTime: %d\n", ret);
		return ret;
	}

	return 0;
}

static int gb_interface_runtime_idle(struct device *dev)
{
	pm_runtime_mark_last_busy(dev);
	pm_request_autosuspend(dev);

	return 0;
}
#endif

static const struct dev_pm_ops gb_interface_pm_ops = {
	SET_RUNTIME_PM_OPS(gb_interface_suspend, gb_interface_resume,
			   gb_interface_runtime_idle)
};

struct device_type greybus_interface_type = {
	.name =		"greybus_interface",
	.release =	gb_interface_release,
	.pm =		&gb_interface_pm_ops,
};

/*
 * A Greybus module represents a user-replaceable component on an Ara
 * phone.  An interface is the physical connection on that module.  A
 * module may have more than one interface.
 *
 * Create a gb_interface structure to represent a discovered interface.
 * The position of interface within the Endo is encoded in "interface_id"
 * argument.
 *
 * Returns a pointer to the new interfce or a null pointer if a
 * failure occurs due to memory exhaustion.
 */
struct gb_interface *gb_interface_create(struct gb_module *module,
					 u8 interface_id)
{
	struct gb_host_device *hd = module->hd;
	struct gb_interface *intf;

	intf = kzalloc(sizeof(*intf), GFP_KERNEL);
	if (!intf)
		return NULL;

	intf->hd = hd;		/* XXX refcount? */
	intf->module = module;
	intf->interface_id = interface_id;
	INIT_LIST_HEAD(&intf->bundles);
	INIT_LIST_HEAD(&intf->manifest_descs);
	mutex_init(&intf->mutex);
	INIT_WORK(&intf->mode_switch_work, gb_interface_mode_switch_work);
	init_completion(&intf->mode_switch_completion);

	/* Invalid device id to start with */
	intf->device_id = GB_INTERFACE_DEVICE_ID_BAD;

	intf->dev.parent = &module->dev;
	intf->dev.bus = &greybus_bus_type;
	intf->dev.type = &greybus_interface_type;
	intf->dev.groups = interface_groups;
	intf->dev.dma_mask = module->dev.dma_mask;
	device_initialize(&intf->dev);
	dev_set_name(&intf->dev, "%s.%u", dev_name(&module->dev),
			interface_id);

	pm_runtime_set_autosuspend_delay(&intf->dev,
					 GB_INTERFACE_AUTOSUSPEND_MS);

	trace_gb_interface_create(intf);

	return intf;
}

static int gb_interface_vsys_set(struct gb_interface *intf, bool enable)
{
	struct gb_svc *svc = intf->hd->svc;
	int ret;

	dev_dbg(&intf->dev, "%s - %d\n", __func__, enable);

	ret = gb_svc_intf_vsys_set(svc, intf->interface_id, enable);
	if (ret) {
		dev_err(&intf->dev, "failed to set v_sys: %d\n", ret);
		return ret;
	}

	return 0;
}

static int gb_interface_refclk_set(struct gb_interface *intf, bool enable)
{
	struct gb_svc *svc = intf->hd->svc;
	int ret;

	dev_dbg(&intf->dev, "%s - %d\n", __func__, enable);

	ret = gb_svc_intf_refclk_set(svc, intf->interface_id, enable);
	if (ret) {
		dev_err(&intf->dev, "failed to set refclk: %d\n", ret);
		return ret;
	}

	return 0;
}

static int gb_interface_unipro_set(struct gb_interface *intf, bool enable)
{
	struct gb_svc *svc = intf->hd->svc;
	int ret;

	dev_dbg(&intf->dev, "%s - %d\n", __func__, enable);

	ret = gb_svc_intf_unipro_set(svc, intf->interface_id, enable);
	if (ret) {
		dev_err(&intf->dev, "failed to set UniPro: %d\n", ret);
		return ret;
	}

	return 0;
}

static int gb_interface_activate_operation(struct gb_interface *intf,
					   enum gb_interface_type *intf_type)
{
	struct gb_svc *svc = intf->hd->svc;
	u8 type;
	int ret;

	dev_dbg(&intf->dev, "%s\n", __func__);

	ret = gb_svc_intf_activate(svc, intf->interface_id, &type);
	if (ret) {
		dev_err(&intf->dev, "failed to activate: %d\n", ret);
		return ret;
	}

	switch (type) {
	case GB_SVC_INTF_TYPE_DUMMY:
		*intf_type = GB_INTERFACE_TYPE_DUMMY;
		/* FIXME: handle as an error for now */
		return -ENODEV;
	case GB_SVC_INTF_TYPE_UNIPRO:
		*intf_type = GB_INTERFACE_TYPE_UNIPRO;
		dev_err(&intf->dev, "interface type UniPro not supported\n");
		/* FIXME: handle as an error for now */
		return -ENODEV;
	case GB_SVC_INTF_TYPE_GREYBUS:
		*intf_type = GB_INTERFACE_TYPE_GREYBUS;
		break;
	default:
		dev_err(&intf->dev, "unknown interface type: %u\n", type);
		*intf_type = GB_INTERFACE_TYPE_UNKNOWN;
		return -ENODEV;
	}

	return 0;
}

static int gb_interface_hibernate_link(struct gb_interface *intf)
{
	struct gb_svc *svc = intf->hd->svc;

	return gb_svc_intf_set_power_mode_hibernate(svc, intf->interface_id);
}

static int _gb_interface_activate(struct gb_interface *intf,
				  enum gb_interface_type *type)
{
	int ret;

	*type = GB_INTERFACE_TYPE_UNKNOWN;

	if (intf->ejected || intf->removed)
		return -ENODEV;

	ret = gb_interface_vsys_set(intf, true);
	if (ret)
		return ret;

	ret = gb_interface_refclk_set(intf, true);
	if (ret)
		goto err_vsys_disable;

	ret = gb_interface_unipro_set(intf, true);
	if (ret)
		goto err_refclk_disable;

	ret = gb_interface_activate_operation(intf, type);
	if (ret) {
		switch (*type) {
		case GB_INTERFACE_TYPE_UNIPRO:
		case GB_INTERFACE_TYPE_GREYBUS:
			goto err_hibernate_link;
		default:
			goto err_unipro_disable;
		}
	}

	ret = gb_interface_read_dme(intf);
	if (ret)
		goto err_hibernate_link;

	ret = gb_interface_route_create(intf);
	if (ret)
		goto err_hibernate_link;

	intf->active = true;

	trace_gb_interface_activate(intf);

	return 0;

err_hibernate_link:
	gb_interface_hibernate_link(intf);
err_unipro_disable:
	gb_interface_unipro_set(intf, false);
err_refclk_disable:
	gb_interface_refclk_set(intf, false);
err_vsys_disable:
	gb_interface_vsys_set(intf, false);

	return ret;
}

/*
 * At present, we assume a UniPro-only module to be a Greybus module that
 * failed to send its mailbox poke. There is some reason to believe that this
 * is because of a bug in the ES3 bootrom.
 *
 * FIXME: Check if this is a Toshiba bridge before retrying?
 */
static int _gb_interface_activate_es3_hack(struct gb_interface *intf,
					   enum gb_interface_type *type)
{
	int retries = 3;
	int ret;

	while (retries--) {
		ret = _gb_interface_activate(intf, type);
		if (ret == -ENODEV && *type == GB_INTERFACE_TYPE_UNIPRO)
			continue;

		break;
	}

	return ret;
}

/*
 * Activate an interface.
 *
 * Locking: Caller holds the interface mutex.
 */
int gb_interface_activate(struct gb_interface *intf)
{
	enum gb_interface_type type;
	int ret;

	switch (intf->type) {
	case GB_INTERFACE_TYPE_INVALID:
	case GB_INTERFACE_TYPE_GREYBUS:
		ret = _gb_interface_activate_es3_hack(intf, &type);
		break;
	default:
		ret = _gb_interface_activate(intf, &type);
	}

	/* Make sure type is detected correctly during reactivation. */
	if (intf->type != GB_INTERFACE_TYPE_INVALID) {
		if (type != intf->type) {
			dev_err(&intf->dev, "failed to detect interface type\n");

			if (!ret)
				gb_interface_deactivate(intf);

			return -EIO;
		}
	} else {
		intf->type = type;
	}

	return ret;
}

/*
 * Deactivate an interface.
 *
 * Locking: Caller holds the interface mutex.
 */
void gb_interface_deactivate(struct gb_interface *intf)
{
	if (!intf->active)
		return;

	trace_gb_interface_deactivate(intf);

	/* Abort any ongoing mode switch. */
	if (intf->mode_switch)
		complete(&intf->mode_switch_completion);

	gb_interface_route_destroy(intf);
	gb_interface_hibernate_link(intf);
	gb_interface_unipro_set(intf, false);
	gb_interface_refclk_set(intf, false);
	gb_interface_vsys_set(intf, false);

	intf->active = false;
}

/*
 * Enable an interface by enabling its control connection, fetching the
 * manifest and other information over it, and finally registering its child
 * devices.
 *
 * Locking: Caller holds the interface mutex.
 */
int gb_interface_enable(struct gb_interface *intf)
{
	struct gb_control *control;
	struct gb_bundle *bundle, *tmp;
	int ret, size;
	void *manifest;

	ret = gb_interface_read_and_clear_init_status(intf);
	if (ret) {
		dev_err(&intf->dev, "failed to clear init status: %d\n", ret);
		return ret;
	}

	/* Establish control connection */
	control = gb_control_create(intf);
	if (IS_ERR(control)) {
		dev_err(&intf->dev, "failed to create control device: %ld\n",
				PTR_ERR(control));
		return PTR_ERR(control);
	}
	intf->control = control;

	ret = gb_control_enable(intf->control);
	if (ret)
		goto err_put_control;

	/* Get manifest size using control protocol on CPort */
	size = gb_control_get_manifest_size_operation(intf);
	if (size <= 0) {
		dev_err(&intf->dev, "failed to get manifest size: %d\n", size);

		if (size)
			ret = size;
		else
			ret =  -EINVAL;

		goto err_disable_control;
	}

	manifest = kmalloc(size, GFP_KERNEL);
	if (!manifest) {
		ret = -ENOMEM;
		goto err_disable_control;
	}

	/* Get manifest using control protocol on CPort */
	ret = gb_control_get_manifest_operation(intf, manifest, size);
	if (ret) {
		dev_err(&intf->dev, "failed to get manifest: %d\n", ret);
		goto err_free_manifest;
	}

	/*
	 * Parse the manifest and build up our data structures representing
	 * what's in it.
	 */
	if (!gb_manifest_parse(intf, manifest, size)) {
		dev_err(&intf->dev, "failed to parse manifest\n");
		ret = -EINVAL;
		goto err_destroy_bundles;
	}

	ret = gb_control_get_bundle_versions(intf->control);
	if (ret)
		goto err_destroy_bundles;

	/* Register the control device and any bundles */
	ret = gb_control_add(intf->control);
	if (ret)
		goto err_destroy_bundles;

	ret = gb_timesync_interface_add(intf);
	if (ret) {
		dev_err(&intf->dev, "failed to add to timesync: %d\n", ret);
		goto err_destroy_bundles;
	}

	pm_runtime_use_autosuspend(&intf->dev);
	pm_runtime_get_noresume(&intf->dev);
	pm_runtime_set_active(&intf->dev);
	pm_runtime_enable(&intf->dev);

	list_for_each_entry_safe_reverse(bundle, tmp, &intf->bundles, links) {
		ret = gb_bundle_add(bundle);
		if (ret) {
			gb_bundle_destroy(bundle);
			continue;
		}
	}

	kfree(manifest);

	intf->enabled = true;

	pm_runtime_put(&intf->dev);

	trace_gb_interface_enable(intf);

	return 0;

err_destroy_bundles:
	list_for_each_entry_safe(bundle, tmp, &intf->bundles, links)
		gb_bundle_destroy(bundle);
err_free_manifest:
	kfree(manifest);
err_disable_control:
	gb_control_disable(intf->control);
err_put_control:
	gb_control_put(intf->control);
	intf->control = NULL;

	return ret;
}

/*
 * Disable an interface and destroy its bundles.
 *
 * Locking: Caller holds the interface mutex.
 */
void gb_interface_disable(struct gb_interface *intf)
{
	struct gb_bundle *bundle;
	struct gb_bundle *next;

	if (!intf->enabled)
		return;

	trace_gb_interface_disable(intf);

	pm_runtime_get_sync(&intf->dev);

	/* Set disconnected flag to avoid I/O during connection tear down. */
	if (intf->quirks & GB_INTERFACE_QUIRK_FORCED_DISABLE)
		intf->disconnected = true;

	list_for_each_entry_safe(bundle, next, &intf->bundles, links)
		gb_bundle_destroy(bundle);

	if (!intf->mode_switch && !intf->disconnected)
		gb_control_interface_deactivate_prepare(intf->control);

	gb_timesync_interface_remove(intf);
	gb_control_del(intf->control);
	gb_control_disable(intf->control);
	gb_control_put(intf->control);
	intf->control = NULL;

	intf->enabled = false;

	pm_runtime_disable(&intf->dev);
	pm_runtime_set_suspended(&intf->dev);
	pm_runtime_dont_use_autosuspend(&intf->dev);
	pm_runtime_put_noidle(&intf->dev);
}

/* Enable TimeSync on an Interface control connection. */
int gb_interface_timesync_enable(struct gb_interface *intf, u8 count,
				 u64 frame_time, u32 strobe_delay, u32 refclk)
{
	return gb_control_timesync_enable(intf->control, count,
					  frame_time, strobe_delay,
					  refclk);
}

/* Disable TimeSync on an Interface control connection. */
int gb_interface_timesync_disable(struct gb_interface *intf)
{
	return gb_control_timesync_disable(intf->control);
}

/* Transmit the Authoritative FrameTime via an Interface control connection. */
int gb_interface_timesync_authoritative(struct gb_interface *intf,
					u64 *frame_time)
{
	return gb_control_timesync_authoritative(intf->control,
						frame_time);
}

/* Register an interface. */
int gb_interface_add(struct gb_interface *intf)
{
	int ret;

	ret = device_add(&intf->dev);
	if (ret) {
		dev_err(&intf->dev, "failed to register interface: %d\n", ret);
		return ret;
	}

	trace_gb_interface_add(intf);

	dev_info(&intf->dev, "Interface added (%s)\n",
			gb_interface_type_string(intf));

	switch (intf->type) {
	case GB_INTERFACE_TYPE_GREYBUS:
		dev_info(&intf->dev, "Ara VID=0x%08x, PID=0x%08x\n",
				intf->vendor_id, intf->product_id);
		/* fall-through */
	case GB_INTERFACE_TYPE_UNIPRO:
		dev_info(&intf->dev, "DDBL1 Manufacturer=0x%08x, Product=0x%08x\n",
				intf->ddbl1_manufacturer_id,
				intf->ddbl1_product_id);
		break;
	default:
		break;
	}

	return 0;
}

/* Deregister an interface. */
void gb_interface_del(struct gb_interface *intf)
{
	if (device_is_registered(&intf->dev)) {
		trace_gb_interface_del(intf);

		device_del(&intf->dev);
		dev_info(&intf->dev, "Interface removed\n");
	}
}

void gb_interface_put(struct gb_interface *intf)
{
	put_device(&intf->dev);
}
