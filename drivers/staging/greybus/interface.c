/*
 * Greybus interface code
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include "greybus.h"


#define GB_INTERFACE_DEVICE_ID_BAD	0xff

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

	return gb_interface_read_ara_dme(intf);
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
	 * Check if the interface is executing the quirky ES3 bootrom that
	 * requires E2EFC, CSD and CSV to be disabled.
	 */
	switch (init_status) {
	case GB_INIT_BOOTROM_UNIPRO_BOOT_STARTED:
	case GB_INIT_BOOTROM_FALLBACK_UNIPRO_BOOT_STARTED:
		intf->quirks |= GB_INTERFACE_QUIRK_NO_CPORT_FEATURES;
		break;
	default:
		intf->quirks &= ~GB_INTERFACE_QUIRK_NO_CPORT_FEATURES;
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

static struct attribute *interface_attrs[] = {
	&dev_attr_ddbl1_manufacturer_id.attr,
	&dev_attr_ddbl1_product_id.attr,
	&dev_attr_interface_id.attr,
	&dev_attr_vendor_id.attr,
	&dev_attr_product_id.attr,
	&dev_attr_serial_number.attr,
	&dev_attr_voltage_now.attr,
	&dev_attr_current_now.attr,
	&dev_attr_power_now.attr,
	NULL,
};
ATTRIBUTE_GROUPS(interface);

static void gb_interface_release(struct device *dev)
{
	struct gb_interface *intf = to_gb_interface(dev);

	kfree(intf);
}

struct device_type greybus_interface_type = {
	.name =		"greybus_interface",
	.release =	gb_interface_release,
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

	return intf;
}

static int gb_interface_vsys_set(struct gb_interface *intf, bool enable)
{
	struct gb_svc *svc = intf->hd->svc;
	int ret;

	dev_dbg(&intf->dev, "%s - %d\n", __func__, enable);

	ret = gb_svc_intf_vsys_set(svc, intf->interface_id, enable);
	if (ret) {
		dev_err(&intf->dev, "failed to enable v_sys: %d\n", ret);
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
		dev_err(&intf->dev, "failed to enable refclk: %d\n", ret);
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
		dev_err(&intf->dev, "failed to enable UniPro: %d\n", ret);
		return ret;
	}

	return 0;
}

static int gb_interface_activate_operation(struct gb_interface *intf)
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
		dev_info(&intf->dev, "dummy interface detected\n");
		/* FIXME: handle as an error for now */
		return -ENODEV;
	case GB_SVC_INTF_TYPE_UNIPRO:
		dev_err(&intf->dev, "interface type UniPro not supported\n");
		/* FIXME: check if this is a Toshiba bridge before retrying? */
		return -EAGAIN;
	case GB_SVC_INTF_TYPE_GREYBUS:
		break;
	default:
		dev_err(&intf->dev, "unknown interface type: %u\n", type);
		return -ENODEV;
	}

	return 0;
}

static int gb_interface_hibernate_link(struct gb_interface *intf)
{
	dev_dbg(&intf->dev, "%s\n", __func__);

	/* FIXME: implement */

	return 0;
}

/*
 * Activate an interface.
 *
 * Locking: Caller holds the interface mutex.
 */
int gb_interface_activate(struct gb_interface *intf)
{
	int ret;

	if (intf->ejected)
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

	ret = gb_interface_activate_operation(intf);
	if (ret)
		goto err_unipro_disable;

	ret = gb_interface_read_dme(intf);
	if (ret)
		goto err_hibernate_link;

	ret = gb_interface_route_create(intf);
	if (ret)
		goto err_hibernate_link;

	intf->active = true;

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
 * Deactivate an interface.
 *
 * Locking: Caller holds the interface mutex.
 */
void gb_interface_deactivate(struct gb_interface *intf)
{
	if (!intf->active)
		return;

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

	list_for_each_entry_safe_reverse(bundle, tmp, &intf->bundles, links) {
		ret = gb_bundle_add(bundle);
		if (ret) {
			gb_bundle_destroy(bundle);
			continue;
		}
	}

	kfree(manifest);

	intf->enabled = true;

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

	/*
	 * Disable the control-connection early to avoid operation timeouts
	 * when the interface is already gone.
	 */
	if (intf->disconnected)
		gb_control_disable(intf->control);

	list_for_each_entry_safe(bundle, next, &intf->bundles, links)
		gb_bundle_destroy(bundle);

	gb_control_del(intf->control);
	gb_control_disable(intf->control);
	gb_control_put(intf->control);
	intf->control = NULL;

	intf->enabled = false;
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

	dev_info(&intf->dev, "Interface added: VID=0x%08x, PID=0x%08x\n",
		 intf->vendor_id, intf->product_id);
	dev_info(&intf->dev, "DDBL1 Manufacturer=0x%08x, Product=0x%08x\n",
		 intf->ddbl1_manufacturer_id, intf->ddbl1_product_id);

	return 0;
}

/* Deregister an interface. */
void gb_interface_del(struct gb_interface *intf)
{
	if (device_is_registered(&intf->dev)) {
		device_del(&intf->dev);
		dev_info(&intf->dev, "Interface removed\n");
	}
}

void gb_interface_put(struct gb_interface *intf)
{
	put_device(&intf->dev);
}
