// SPDX-License-Identifier: GPL-2.0
/*
 * Code to build software firmware node graph for atomisp2 connected sensors
 * from ACPI tables.
 *
 * Copyright (C) 2023 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on drivers/media/pci/intel/ipu3/cio2-bridge.c written by:
 * Dan Scally <djrscally@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/property.h>

#include <media/ipu-bridge.h>
#include <media/v4l2-fwnode.h>

#include "atomisp_cmd.h"
#include "atomisp_csi2.h"
#include "atomisp_internal.h"

#define PMC_CLK_RATE_19_2MHZ			19200000

/*
 * 79234640-9e10-4fea-a5c1-b5aa8b19756f
 * This _DSM GUID returns information about the GPIO lines mapped to a sensor.
 * Function number 1 returns a count of the GPIO lines that are mapped.
 * Subsequent functions return 32 bit ints encoding information about the GPIO.
 */
static const guid_t intel_sensor_gpio_info_guid =
	GUID_INIT(0x79234640, 0x9e10, 0x4fea,
		  0xa5, 0xc1, 0xb5, 0xaa, 0x8b, 0x19, 0x75, 0x6f);

#define INTEL_GPIO_DSM_TYPE_SHIFT			0
#define INTEL_GPIO_DSM_TYPE_MASK			GENMASK(7, 0)
#define INTEL_GPIO_DSM_PIN_SHIFT			8
#define INTEL_GPIO_DSM_PIN_MASK				GENMASK(15, 8)
#define INTEL_GPIO_DSM_SENSOR_ON_VAL_SHIFT		24
#define INTEL_GPIO_DSM_SENSOR_ON_VAL_MASK		GENMASK(31, 24)

#define INTEL_GPIO_DSM_TYPE(x) \
	(((x) & INTEL_GPIO_DSM_TYPE_MASK) >> INTEL_GPIO_DSM_TYPE_SHIFT)
#define INTEL_GPIO_DSM_PIN(x) \
	(((x) & INTEL_GPIO_DSM_PIN_MASK) >> INTEL_GPIO_DSM_PIN_SHIFT)
#define INTEL_GPIO_DSM_SENSOR_ON_VAL(x) \
	(((x) & INTEL_GPIO_DSM_SENSOR_ON_VAL_MASK) >> INTEL_GPIO_DSM_SENSOR_ON_VAL_SHIFT)

/*
 * 822ace8f-2814-4174-a56b-5f029fe079ee
 * This _DSM GUID returns a string from the sensor device, which acts as a
 * module identifier.
 */
static const guid_t intel_sensor_module_guid =
	GUID_INIT(0x822ace8f, 0x2814, 0x4174,
		  0xa5, 0x6b, 0x5f, 0x02, 0x9f, 0xe0, 0x79, 0xee);

/*
 * dc2f6c4f-045b-4f1d-97b9-882a6860a4be
 * This _DSM GUID returns a package with n*2 strings, with each set of 2 strings
 * forming a key, value pair for settings like e.g. "CsiLanes" = "1".
 */
static const guid_t atomisp_dsm_guid =
	GUID_INIT(0xdc2f6c4f, 0x045b, 0x4f1d,
		  0x97, 0xb9, 0x88, 0x2a, 0x68, 0x60, 0xa4, 0xbe);

/*
 * 75c9a639-5c8a-4a00-9f48-a9c3b5da789f
 * This _DSM GUID returns a string giving the VCM type e.g. "AD5823".
 */
static const guid_t vcm_dsm_guid =
	GUID_INIT(0x75c9a639, 0x5c8a, 0x4a00,
		  0x9f, 0x48, 0xa9, 0xc3, 0xb5, 0xda, 0x78, 0x9f);

struct atomisp_sensor_config {
	int lanes;
	bool vcm;
};

#define ATOMISP_SENSOR_CONFIG(_HID, _LANES, _VCM)			\
{									\
	.id = _HID,							\
	.driver_data = (long)&((const struct atomisp_sensor_config) {	\
		.lanes = _LANES,					\
		.vcm = _VCM,						\
	})								\
}

/*
 * gmin_cfg parsing code. This is a cleaned up version of the gmin_cfg parsing
 * code from atomisp_gmin_platform.c.
 * Once all sensors are moved to v4l2-async probing atomisp_gmin_platform.c can
 * be removed and the duplication of this code goes away.
 */
struct gmin_cfg_var {
	const char *acpi_dev_name;
	const char *key;
	const char *val;
};

static struct gmin_cfg_var lenovo_ideapad_miix_310_vars[] = {
	/* _DSM contains the wrong CsiPort! */
	{ "OVTI2680:01", "CsiPort", "0" },
	{}
};

static struct gmin_cfg_var xiaomi_mipad2_vars[] = {
	/* _DSM contains the wrong CsiPort for the front facing OV5693 sensor */
	{ "INT33BE:00", "CsiPort", "0" },
	/* _DSM contains the wrong CsiLanes for the back facing T4KA3 sensor */
	{ "XMCC0003:00", "CsiLanes", "4" },
	{}
};

static const struct dmi_system_id gmin_cfg_dmi_overrides[] = {
	{
		/* Lenovo Ideapad Miix 310 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "MIIX 310-10"),
		},
		.driver_data = lenovo_ideapad_miix_310_vars,
	},
	{
		/* Xiaomi Mipad2 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Xiaomi Inc"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Mipad2"),
		},
		.driver_data = xiaomi_mipad2_vars,
	},
	{}
};

static char *gmin_cfg_get_dsm(struct acpi_device *adev, const char *key)
{
	union acpi_object *obj, *key_el, *val_el;
	char *val = NULL;
	int i;

	obj = acpi_evaluate_dsm_typed(adev->handle, &atomisp_dsm_guid, 0, 0,
				      NULL, ACPI_TYPE_PACKAGE);
	if (!obj)
		return NULL;

	for (i = 0; i < obj->package.count - 1; i += 2) {
		key_el = &obj->package.elements[i + 0];
		val_el = &obj->package.elements[i + 1];

		if (key_el->type != ACPI_TYPE_STRING || val_el->type != ACPI_TYPE_STRING)
			break;

		if (!strcmp(key_el->string.pointer, key)) {
			val = kstrdup(val_el->string.pointer, GFP_KERNEL);
			if (!val)
				break;

			acpi_handle_info(adev->handle, "%s: Using DSM entry %s=%s\n",
					 dev_name(&adev->dev), key, val);
			break;
		}
	}

	ACPI_FREE(obj);
	return val;
}

static char *gmin_cfg_get_dmi_override(struct acpi_device *adev, const char *key)
{
	const struct dmi_system_id *id;
	struct gmin_cfg_var *gv;

	id = dmi_first_match(gmin_cfg_dmi_overrides);
	if (!id)
		return NULL;

	for (gv = id->driver_data; gv->acpi_dev_name; gv++) {
		if (strcmp(gv->acpi_dev_name, acpi_dev_name(adev)))
			continue;

		if (strcmp(key, gv->key))
			continue;

		acpi_handle_info(adev->handle, "%s: Using DMI entry %s=%s\n",
				 dev_name(&adev->dev), key, gv->val);
		return kstrdup(gv->val, GFP_KERNEL);
	}

	return NULL;
}

static char *gmin_cfg_get(struct acpi_device *adev, const char *key)
{
	char *val;

	val = gmin_cfg_get_dmi_override(adev, key);
	if (val)
		return val;

	return gmin_cfg_get_dsm(adev, key);
}

static int gmin_cfg_get_int(struct acpi_device *adev, const char *key, int default_val)
{
	char *str_val;
	long int_val;
	int ret;

	str_val = gmin_cfg_get(adev, key);
	if (!str_val)
		goto out_use_default;

	ret = kstrtoul(str_val, 0, &int_val);
	kfree(str_val);
	if (ret)
		goto out_use_default;

	return int_val;

out_use_default:
	acpi_handle_info(adev->handle, "%s: Using default %s=%d\n",
			 dev_name(&adev->dev), key, default_val);
	return default_val;
}

static int atomisp_csi2_get_pmc_clk_nr_from_acpi_pr0(struct acpi_device *adev)
{
	/* ACPI_PATH_SEGMENT_LENGTH is guaranteed to be big enough for name + 0 term. */
	char name[ACPI_PATH_SEGMENT_LENGTH];
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer b_name = { sizeof(name), name };
	union acpi_object *package, *element;
	int i, ret = -ENOENT;
	acpi_handle rhandle;
	acpi_status status;
	u8 clock_num;

	status = acpi_evaluate_object_typed(adev->handle, "_PR0", NULL, &buffer, ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(status))
		return -ENOENT;

	package = buffer.pointer;
	for (i = 0; i < package->package.count; i++) {
		element = &package->package.elements[i];

		if (element->type != ACPI_TYPE_LOCAL_REFERENCE)
			continue;

		rhandle = element->reference.handle;
		if (!rhandle)
			continue;

		acpi_get_name(rhandle, ACPI_SINGLE_NAME, &b_name);

		if (str_has_prefix(name, "CLK") && !kstrtou8(&name[3], 10, &clock_num) &&
		    clock_num <= 4) {
			ret = clock_num;
			break;
		}
	}

	ACPI_FREE(buffer.pointer);

	if (ret < 0)
		acpi_handle_warn(adev->handle, "%s: Could not find PMC clk in _PR0\n",
				 dev_name(&adev->dev));

	return ret;
}

static int atomisp_csi2_set_pmc_clk_freq(struct acpi_device *adev, int clock_num)
{
	struct clk *clk;
	char name[14];
	int ret;

	if (clock_num < 0)
		return 0;

	snprintf(name, sizeof(name), "pmc_plt_clk_%d", clock_num);

	clk = clk_get(NULL, name);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		acpi_handle_err(adev->handle, "%s: Error getting clk %s: %d\n",
				dev_name(&adev->dev), name, ret);
		return ret;
	}

	/*
	 * The firmware might enable the clock at boot, to change
	 * the rate we must ensure the clock is disabled.
	 */
	ret = clk_prepare_enable(clk);
	if (!ret)
		clk_disable_unprepare(clk);
	if (!ret)
		ret = clk_set_rate(clk, PMC_CLK_RATE_19_2MHZ);
	if (ret)
		acpi_handle_err(adev->handle, "%s: Error setting clk-rate for %s: %d\n",
				dev_name(&adev->dev), name, ret);

	clk_put(clk);
	return ret;
}

static int atomisp_csi2_get_port(struct acpi_device *adev, int clock_num)
{
	int port;

	/*
	 * Compare clock-number to the PMC-clock used for CsiPort 1
	 * in the CHT/BYT reference designs.
	 */
	if (IS_ISP2401)
		port = clock_num == 4 ? 1 : 0;
	else
		port = clock_num == 0 ? 1 : 0;

	/* Intel DSM or DMI quirk overrides _PR0 CLK derived default */
	return gmin_cfg_get_int(adev, "CsiPort", port);
}

/* Note this always returns 1 to continue looping so that res_count is accurate */
static int atomisp_csi2_handle_acpi_gpio_res(struct acpi_resource *ares, void *_data)
{
	struct atomisp_csi2_acpi_gpio_parsing_data *data = _data;
	struct acpi_resource_gpio *agpio;
	const char *name;
	bool active_low;
	unsigned int i;
	u32 settings = 0;
	u16 pin;

	if (!acpi_gpio_get_io_resource(ares, &agpio))
		return 1; /* Not a GPIO, continue the loop */

	data->res_count++;

	pin = agpio->pin_table[0];
	for (i = 0; i < data->settings_count; i++) {
		if (INTEL_GPIO_DSM_PIN(data->settings[i]) == pin) {
			settings = data->settings[i];
			break;
		}
	}

	if (i == data->settings_count) {
		acpi_handle_warn(data->adev->handle,
				 "%s: Could not find DSM GPIO settings for pin %u\n",
				 dev_name(&data->adev->dev), pin);
		return 1;
	}

	switch (INTEL_GPIO_DSM_TYPE(settings)) {
	case 0:
		name = "reset-gpios";
		break;
	case 1:
		name = "powerdown-gpios";
		break;
	default:
		acpi_handle_warn(data->adev->handle, "%s: Unknown GPIO type 0x%02lx for pin %u\n",
				 dev_name(&data->adev->dev),
				 INTEL_GPIO_DSM_TYPE(settings), pin);
		return 1;
	}

	/*
	 * Both reset and power-down need to be logical false when the sensor
	 * is on (sensor should not be in reset and not be powered-down). So
	 * when the sensor-on-value (which is the physical pin value) is high,
	 * then the signal is active-low.
	 */
	active_low = INTEL_GPIO_DSM_SENSOR_ON_VAL(settings);

	i = data->map_count;
	if (i == CSI2_MAX_ACPI_GPIOS)
		return 1;

	/* res_count is already incremented */
	data->map->params[i].crs_entry_index = data->res_count - 1;
	data->map->params[i].active_low = active_low;
	data->map->mapping[i].name = name;
	data->map->mapping[i].data = &data->map->params[i];
	data->map->mapping[i].size = 1;
	data->map_count++;

	acpi_handle_info(data->adev->handle, "%s: %s crs %d %s pin %u active-%s\n",
			 dev_name(&data->adev->dev), name,
			 data->res_count - 1, agpio->resource_source.string_ptr,
			 pin, active_low ? "low" : "high");

	return 1;
}

/*
 * Helper function to create an ACPI GPIO lookup table for sensor reset and
 * powerdown signals on Intel Bay Trail (BYT) and Cherry Trail (CHT) devices,
 * including setting the correct polarity for the GPIO.
 *
 * This uses the "79234640-9e10-4fea-a5c1-b5aa8b19756f" DSM method directly
 * on the sensor device's ACPI node. This is different from later Intel
 * hardware which has a separate INT3472 acpi_device with this info.
 *
 * This function must be called before creating the sw-noded describing
 * the fwnode graph endpoint. And sensor drivers used on these devices
 * must return -EPROBE_DEFER when there is no endpoint description yet.
 * Together this guarantees that the GPIO lookups are in place before
 * the sensor driver tries to get GPIOs with gpiod_get().
 *
 * Note this code uses the same DSM GUID as the int3472_gpio_guid in
 * the INT3472 discrete.c code and there is some overlap, but there are
 * enough differences that it is difficult to share the code.
 */
static int atomisp_csi2_add_gpio_mappings(struct acpi_device *adev)
{
	struct atomisp_csi2_acpi_gpio_parsing_data data = { };
	LIST_HEAD(resource_list);
	union acpi_object *obj;
	unsigned int i, j;
	int ret;

	obj = acpi_evaluate_dsm_typed(adev->handle, &intel_sensor_module_guid,
				      0x00, 1, NULL, ACPI_TYPE_STRING);
	if (obj) {
		acpi_handle_info(adev->handle, "%s: Sensor module id: '%s'\n",
				 dev_name(&adev->dev), obj->string.pointer);
		ACPI_FREE(obj);
	}

	/*
	 * First get the GPIO-settings count and then get count GPIO-settings
	 * values. Note the order of these may differ from the order in which
	 * the GPIOs are listed on the ACPI resources! So we first store them all
	 * and then enumerate the ACPI resources and match them up by pin number.
	 */
	obj = acpi_evaluate_dsm_typed(adev->handle,
				      &intel_sensor_gpio_info_guid, 0x00, 1,
				      NULL, ACPI_TYPE_INTEGER);
	if (!obj) {
		acpi_handle_err(adev->handle, "%s: No _DSM entry for GPIO pin count\n",
				dev_name(&adev->dev));
		return -EIO;
	}

	data.settings_count = obj->integer.value;
	ACPI_FREE(obj);

	if (data.settings_count > CSI2_MAX_ACPI_GPIOS) {
		acpi_handle_err(adev->handle, "%s: Too many GPIOs %u > %u\n",
				dev_name(&adev->dev), data.settings_count,
				CSI2_MAX_ACPI_GPIOS);
		return -EOVERFLOW;
	}

	for (i = 0; i < data.settings_count; i++) {
		/*
		 * i + 2 because the index of this _DSM function is 1-based
		 * and the first function is just a count.
		 */
		obj = acpi_evaluate_dsm_typed(adev->handle,
					      &intel_sensor_gpio_info_guid,
					      0x00, i + 2,
					      NULL, ACPI_TYPE_INTEGER);
		if (!obj) {
			acpi_handle_err(adev->handle, "%s: No _DSM entry for pin %u\n",
					dev_name(&adev->dev), i);
			return -EIO;
		}

		data.settings[i] = obj->integer.value;
		ACPI_FREE(obj);
	}

	/* Since we match up by pin-number the pin-numbers must be unique */
	for (i = 0; i < data.settings_count; i++) {
		for (j = i + 1; j < data.settings_count; j++) {
			if (INTEL_GPIO_DSM_PIN(data.settings[i]) !=
			    INTEL_GPIO_DSM_PIN(data.settings[j]))
				continue;

			acpi_handle_err(adev->handle, "%s: Duplicate pin number %lu\n",
					dev_name(&adev->dev),
					INTEL_GPIO_DSM_PIN(data.settings[i]));
			return -EIO;
		}
	}

	data.map = kzalloc(sizeof(*data.map), GFP_KERNEL);
	if (!data.map)
		return -ENOMEM;

	/* Now parse the ACPI resources and build the lookup table */
	data.adev = adev;
	ret = acpi_dev_get_resources(adev, &resource_list,
				     atomisp_csi2_handle_acpi_gpio_res, &data);
	if (ret < 0)
		return ret;

	acpi_dev_free_resource_list(&resource_list);

	if (data.map_count != data.settings_count ||
	    data.res_count != data.settings_count)
		acpi_handle_warn(adev->handle, "%s: ACPI GPIO resources vs DSM GPIO-info count mismatch (dsm: %d res: %d map %d\n",
				 dev_name(&adev->dev), data.settings_count,
				 data.res_count, data.map_count);

	ret = acpi_dev_add_driver_gpios(adev, data.map->mapping);
	if (ret)
		acpi_handle_err(adev->handle, "%s: Error adding driver GPIOs: %d\n",
				dev_name(&adev->dev), ret);

	return ret;
}

static char *atomisp_csi2_get_vcm_type(struct acpi_device *adev)
{
	union acpi_object *obj;
	char *vcm_type;

	obj = acpi_evaluate_dsm_typed(adev->handle, &vcm_dsm_guid, 0, 0,
				      NULL, ACPI_TYPE_STRING);
	if (!obj)
		return NULL;

	vcm_type = kstrdup(obj->string.pointer, GFP_KERNEL);
	ACPI_FREE(obj);

	if (!vcm_type)
		return NULL;

	string_lower(vcm_type, vcm_type);
	return vcm_type;
}

static const struct acpi_device_id atomisp_sensor_configs[] = {
	/*
	 * FIXME ov5693 modules have a VCM, but for unknown reasons
	 * the sensor fails to start streaming when instantiating
	 * an i2c-client for the VCM, so it is disabled for now.
	 */
	ATOMISP_SENSOR_CONFIG("INT33BE", 2, false),	/* OV5693 */
	{}
};

static int atomisp_csi2_parse_sensor_fwnode(struct acpi_device *adev,
					    struct ipu_sensor *sensor)
{
	const struct acpi_device_id *id;
	int ret, clock_num;
	bool vcm = false;
	int lanes = 1;

	id = acpi_match_acpi_device(atomisp_sensor_configs, adev);
	if (id) {
		struct atomisp_sensor_config *cfg =
			(struct atomisp_sensor_config *)id->driver_data;

		lanes = cfg->lanes;
		vcm = cfg->vcm;
	}

	/*
	 * ACPI takes care of turning the PMC clock on and off, but on BYT
	 * the clock defaults to 25 MHz instead of the expected 19.2 MHz.
	 * Get the PMC-clock number from ACPI PR0 method and set it to 19.2 MHz.
	 * The PMC-clock number is also used to determine the default CSI port.
	 */
	clock_num = atomisp_csi2_get_pmc_clk_nr_from_acpi_pr0(adev);

	ret = atomisp_csi2_set_pmc_clk_freq(adev, clock_num);
	if (ret)
		return ret;

	sensor->link = atomisp_csi2_get_port(adev, clock_num);
	if (sensor->link >= ATOMISP_CAMERA_NR_PORTS) {
		acpi_handle_err(adev->handle, "%s: Invalid port: %u\n",
				dev_name(&adev->dev), sensor->link);
		return -EINVAL;
	}

	sensor->lanes = gmin_cfg_get_int(adev, "CsiLanes", lanes);
	if (sensor->lanes > IPU_MAX_LANES) {
		acpi_handle_err(adev->handle, "%s: Invalid lane-count: %d\n",
				dev_name(&adev->dev), sensor->lanes);
		return -EINVAL;
	}

	ret = atomisp_csi2_add_gpio_mappings(adev);
	if (ret)
		return ret;

	sensor->mclkspeed = PMC_CLK_RATE_19_2MHZ;
	sensor->rotation = 0;
	sensor->orientation = (sensor->link == 1) ?
		V4L2_FWNODE_ORIENTATION_BACK : V4L2_FWNODE_ORIENTATION_FRONT;

	if (vcm)
		sensor->vcm_type = atomisp_csi2_get_vcm_type(adev);

	return 0;
}

int atomisp_csi2_bridge_init(struct atomisp_device *isp)
{
	struct device *dev = isp->dev;
	struct fwnode_handle *fwnode;

	/*
	 * This function is intended to run only once and then leave
	 * the created nodes attached even after a rmmod, therefore:
	 * 1. The bridge memory is leaked deliberately on success
	 * 2. If a secondary fwnode is already set exit early.
	 */
	fwnode = dev_fwnode(dev);
	if (fwnode && fwnode->secondary)
		return 0;

	return ipu_bridge_init(dev, atomisp_csi2_parse_sensor_fwnode);
}

/******* V4L2 sub-device asynchronous registration callbacks***********/

struct sensor_async_subdev {
	struct v4l2_async_connection asd;
	int port;
};

#define to_sensor_asd(a)	container_of(a, struct sensor_async_subdev, asd)
#define notifier_to_atomisp(n)	container_of(n, struct atomisp_device, notifier)

/* .bound() notifier callback when a match is found */
static int atomisp_notifier_bound(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_connection *asd)
{
	struct atomisp_device *isp = notifier_to_atomisp(notifier);
	struct sensor_async_subdev *s_asd = to_sensor_asd(asd);
	int ret;

	if (s_asd->port >= ATOMISP_CAMERA_NR_PORTS) {
		dev_err(isp->dev, "port %d not supported\n", s_asd->port);
		return -EINVAL;
	}

	if (isp->sensor_subdevs[s_asd->port]) {
		dev_err(isp->dev, "port %d already has a sensor attached\n", s_asd->port);
		return -EBUSY;
	}

	ret = ipu_bridge_instantiate_vcm(sd->dev);
	if (ret)
		return ret;

	isp->sensor_subdevs[s_asd->port] = sd;
	return 0;
}

/* The .unbind callback */
static void atomisp_notifier_unbind(struct v4l2_async_notifier *notifier,
				    struct v4l2_subdev *sd,
				    struct v4l2_async_connection *asd)
{
	struct atomisp_device *isp = notifier_to_atomisp(notifier);
	struct sensor_async_subdev *s_asd = to_sensor_asd(asd);

	isp->sensor_subdevs[s_asd->port] = NULL;
}

/* .complete() is called after all subdevices have been located */
static int atomisp_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct atomisp_device *isp = notifier_to_atomisp(notifier);

	return atomisp_register_device_nodes(isp);
}

static const struct v4l2_async_notifier_operations atomisp_async_ops = {
	.bound = atomisp_notifier_bound,
	.unbind = atomisp_notifier_unbind,
	.complete = atomisp_notifier_complete,
};

int atomisp_csi2_bridge_parse_firmware(struct atomisp_device *isp)
{
	int i, mipi_port, ret;

	v4l2_async_nf_init(&isp->notifier, &isp->v4l2_dev);
	isp->notifier.ops = &atomisp_async_ops;

	for (i = 0; i < ATOMISP_CAMERA_NR_PORTS; i++) {
		struct v4l2_fwnode_endpoint vep = {
			.bus_type = V4L2_MBUS_CSI2_DPHY,
		};
		struct sensor_async_subdev *s_asd;
		struct fwnode_handle *ep;

		ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(isp->dev), i, 0,
						     FWNODE_GRAPH_ENDPOINT_NEXT);
		if (!ep)
			continue;

		ret = v4l2_fwnode_endpoint_parse(ep, &vep);
		if (ret)
			goto err_parse;

		if (vep.base.port >= ATOMISP_CAMERA_NR_PORTS) {
			dev_err(isp->dev, "port %d not supported\n", vep.base.port);
			ret = -EINVAL;
			goto err_parse;
		}

		mipi_port = atomisp_port_to_mipi_port(isp, vep.base.port);
		isp->sensor_lanes[mipi_port] = vep.bus.mipi_csi2.num_data_lanes;

		s_asd = v4l2_async_nf_add_fwnode_remote(&isp->notifier, ep,
							struct sensor_async_subdev);
		if (IS_ERR(s_asd)) {
			ret = PTR_ERR(s_asd);
			goto err_parse;
		}

		s_asd->port = vep.base.port;

		fwnode_handle_put(ep);
		continue;

err_parse:
		fwnode_handle_put(ep);
		return ret;
	}

	return 0;
}
