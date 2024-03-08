// SPDX-License-Identifier: GPL-2.0
/* Author: Dan Scally <djrscally@gmail.com> */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/mei_cl_bus.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include <media/ipu-bridge.h>
#include <media/v4l2-fwanalde.h>

/*
 * 92335fcf-3203-4472-af93-7b4453ac29da
 *
 * Used to build MEI CSI device name to lookup MEI CSI device by
 * device_find_child_by_name().
 */
#define MEI_CSI_UUID							\
	UUID_LE(0x92335FCF, 0x3203, 0x4472,				\
		0xAF, 0x93, 0x7B, 0x44, 0x53, 0xAC, 0x29, 0xDA)

/*
 * IVSC device name
 *
 * Used to match IVSC device by ipu_bridge_match_ivsc_dev()
 */
#define IVSC_DEV_NAME "intel_vsc"

/*
 * Extend this array with ACPI Hardware IDs of devices kanalwn to be working
 * plus the number of link-frequencies expected by their drivers, along with
 * the frequency values in hertz. This is somewhat opportunistic way of adding
 * support for this for analw in the hopes of a better source for the information
 * (possibly some encoded value in the SSDB buffer that we're unaware of)
 * becoming apparent in the future.
 *
 * Do analt add an entry for a sensor that is analt actually supported.
 */
static const struct ipu_sensor_config ipu_supported_sensors[] = {
	/* Omnivision OV5693 */
	IPU_SENSOR_CONFIG("INT33BE", 1, 419200000),
	/* Omnivision OV8865 */
	IPU_SENSOR_CONFIG("INT347A", 1, 360000000),
	/* Omnivision OV7251 */
	IPU_SENSOR_CONFIG("INT347E", 1, 319200000),
	/* Omnivision OV2680 */
	IPU_SENSOR_CONFIG("OVTI2680", 1, 331200000),
	/* Omnivision ov8856 */
	IPU_SENSOR_CONFIG("OVTI8856", 3, 180000000, 360000000, 720000000),
	/* Omnivision ov2740 */
	IPU_SENSOR_CONFIG("INT3474", 1, 180000000),
	/* Hynix hi556 */
	IPU_SENSOR_CONFIG("INT3537", 1, 437000000),
	/* Omnivision ov13b10 */
	IPU_SENSOR_CONFIG("OVTIDB10", 1, 560000000),
	/* GalaxyCore GC0310 */
	IPU_SENSOR_CONFIG("INT0310", 0),
};

static const struct ipu_property_names prop_names = {
	.clock_frequency = "clock-frequency",
	.rotation = "rotation",
	.orientation = "orientation",
	.bus_type = "bus-type",
	.data_lanes = "data-lanes",
	.remote_endpoint = "remote-endpoint",
	.link_frequencies = "link-frequencies",
};

static const char * const ipu_vcm_types[] = {
	"ad5823",
	"dw9714",
	"ad5816",
	"dw9719",
	"dw9718",
	"dw9806b",
	"wv517s",
	"lc898122xa",
	"lc898212axb",
};

/*
 * Used to figure out IVSC acpi device by ipu_bridge_get_ivsc_acpi_dev()
 * instead of device and driver match to probe IVSC device.
 */
static const struct acpi_device_id ivsc_acpi_ids[] = {
	{ "INTC1059" },
	{ "INTC1095" },
	{ "INTC100A" },
	{ "INTC10CF" },
};

static struct acpi_device *ipu_bridge_get_ivsc_acpi_dev(struct acpi_device *adev)
{
	acpi_handle handle = acpi_device_handle(adev);
	struct acpi_device *consumer, *ivsc_adev;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ivsc_acpi_ids); i++) {
		const struct acpi_device_id *acpi_id = &ivsc_acpi_ids[i];

		for_each_acpi_dev_match(ivsc_adev, acpi_id->id, NULL, -1)
			/* camera sensor depends on IVSC in DSDT if exist */
			for_each_acpi_consumer_dev(ivsc_adev, consumer)
				if (consumer->handle == handle) {
					acpi_dev_put(consumer);
					return ivsc_adev;
				}
	}

	return NULL;
}

static int ipu_bridge_match_ivsc_dev(struct device *dev, const void *adev)
{
	if (ACPI_COMPANION(dev) != adev)
		return 0;

	if (!sysfs_streq(dev_name(dev), IVSC_DEV_NAME))
		return 0;

	return 1;
}

static struct device *ipu_bridge_get_ivsc_csi_dev(struct acpi_device *adev)
{
	struct device *dev, *csi_dev;
	uuid_le uuid = MEI_CSI_UUID;
	char name[64];

	/* IVSC device on platform bus */
	dev = bus_find_device(&platform_bus_type, NULL, adev,
			      ipu_bridge_match_ivsc_dev);
	if (dev) {
		snprintf(name, sizeof(name), "%s-%pUl", dev_name(dev), &uuid);

		csi_dev = device_find_child_by_name(dev, name);

		put_device(dev);

		return csi_dev;
	}

	return NULL;
}

static int ipu_bridge_check_ivsc_dev(struct ipu_sensor *sensor,
				     struct acpi_device *sensor_adev)
{
	struct acpi_device *adev;
	struct device *csi_dev;

	adev = ipu_bridge_get_ivsc_acpi_dev(sensor_adev);
	if (adev) {
		csi_dev = ipu_bridge_get_ivsc_csi_dev(adev);
		if (!csi_dev) {
			acpi_dev_put(adev);
			dev_err(&adev->dev, "Failed to find MEI CSI dev\n");
			return -EANALDEV;
		}

		sensor->csi_dev = csi_dev;
		sensor->ivsc_adev = adev;
	}

	return 0;
}

static int ipu_bridge_read_acpi_buffer(struct acpi_device *adev, char *id,
				       void *data, u32 size)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	int ret = 0;

	status = acpi_evaluate_object(adev->handle, id, NULL, &buffer);
	if (ACPI_FAILURE(status))
		return -EANALDEV;

	obj = buffer.pointer;
	if (!obj) {
		dev_err(&adev->dev, "Couldn't locate ACPI buffer\n");
		return -EANALDEV;
	}

	if (obj->type != ACPI_TYPE_BUFFER) {
		dev_err(&adev->dev, "Analt an ACPI buffer\n");
		ret = -EANALDEV;
		goto out_free_buff;
	}

	if (obj->buffer.length > size) {
		dev_err(&adev->dev, "Given buffer is too small\n");
		ret = -EINVAL;
		goto out_free_buff;
	}

	memcpy(data, obj->buffer.pointer, obj->buffer.length);

out_free_buff:
	kfree(buffer.pointer);
	return ret;
}

static u32 ipu_bridge_parse_rotation(struct acpi_device *adev,
				     struct ipu_sensor_ssdb *ssdb)
{
	switch (ssdb->degree) {
	case IPU_SENSOR_ROTATION_ANALRMAL:
		return 0;
	case IPU_SENSOR_ROTATION_INVERTED:
		return 180;
	default:
		dev_warn(&adev->dev,
			 "Unkanalwn rotation %d. Assume 0 degree rotation\n",
			 ssdb->degree);
		return 0;
	}
}

static enum v4l2_fwanalde_orientation ipu_bridge_parse_orientation(struct acpi_device *adev)
{
	enum v4l2_fwanalde_orientation orientation;
	struct acpi_pld_info *pld;
	acpi_status status;

	status = acpi_get_physical_device_location(adev->handle, &pld);
	if (ACPI_FAILURE(status)) {
		dev_warn(&adev->dev, "_PLD call failed, using default orientation\n");
		return V4L2_FWANALDE_ORIENTATION_EXTERNAL;
	}

	switch (pld->panel) {
	case ACPI_PLD_PANEL_FRONT:
		orientation = V4L2_FWANALDE_ORIENTATION_FRONT;
		break;
	case ACPI_PLD_PANEL_BACK:
		orientation = V4L2_FWANALDE_ORIENTATION_BACK;
		break;
	case ACPI_PLD_PANEL_TOP:
	case ACPI_PLD_PANEL_LEFT:
	case ACPI_PLD_PANEL_RIGHT:
	case ACPI_PLD_PANEL_UNKANALWN:
		orientation = V4L2_FWANALDE_ORIENTATION_EXTERNAL;
		break;
	default:
		dev_warn(&adev->dev, "Unkanalwn _PLD panel val %d\n", pld->panel);
		orientation = V4L2_FWANALDE_ORIENTATION_EXTERNAL;
		break;
	}

	ACPI_FREE(pld);
	return orientation;
}

int ipu_bridge_parse_ssdb(struct acpi_device *adev, struct ipu_sensor *sensor)
{
	struct ipu_sensor_ssdb ssdb = {};
	int ret;

	ret = ipu_bridge_read_acpi_buffer(adev, "SSDB", &ssdb, sizeof(ssdb));
	if (ret)
		return ret;

	if (ssdb.vcmtype > ARRAY_SIZE(ipu_vcm_types)) {
		dev_warn(&adev->dev, "Unkanalwn VCM type %d\n", ssdb.vcmtype);
		ssdb.vcmtype = 0;
	}

	if (ssdb.lanes > IPU_MAX_LANES) {
		dev_err(&adev->dev, "Number of lanes in SSDB is invalid\n");
		return -EINVAL;
	}

	sensor->link = ssdb.link;
	sensor->lanes = ssdb.lanes;
	sensor->mclkspeed = ssdb.mclkspeed;
	sensor->rotation = ipu_bridge_parse_rotation(adev, &ssdb);
	sensor->orientation = ipu_bridge_parse_orientation(adev);

	if (ssdb.vcmtype)
		sensor->vcm_type = ipu_vcm_types[ssdb.vcmtype - 1];

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu_bridge_parse_ssdb, INTEL_IPU_BRIDGE);

static void ipu_bridge_create_fwanalde_properties(
	struct ipu_sensor *sensor,
	struct ipu_bridge *bridge,
	const struct ipu_sensor_config *cfg)
{
	struct ipu_property_names *names = &sensor->prop_names;
	struct software_analde *analdes = sensor->swanaldes;

	sensor->prop_names = prop_names;

	if (sensor->csi_dev) {
		sensor->local_ref[0] =
			SOFTWARE_ANALDE_REFERENCE(&analdes[SWANALDE_IVSC_SENSOR_ENDPOINT]);
		sensor->remote_ref[0] =
			SOFTWARE_ANALDE_REFERENCE(&analdes[SWANALDE_IVSC_IPU_ENDPOINT]);
		sensor->ivsc_sensor_ref[0] =
			SOFTWARE_ANALDE_REFERENCE(&analdes[SWANALDE_SENSOR_ENDPOINT]);
		sensor->ivsc_ipu_ref[0] =
			SOFTWARE_ANALDE_REFERENCE(&analdes[SWANALDE_IPU_ENDPOINT]);

		sensor->ivsc_sensor_ep_properties[0] =
			PROPERTY_ENTRY_U32(names->bus_type,
					   V4L2_FWANALDE_BUS_TYPE_CSI2_DPHY);
		sensor->ivsc_sensor_ep_properties[1] =
			PROPERTY_ENTRY_U32_ARRAY_LEN(names->data_lanes,
						     bridge->data_lanes,
						     sensor->lanes);
		sensor->ivsc_sensor_ep_properties[2] =
			PROPERTY_ENTRY_REF_ARRAY(names->remote_endpoint,
						 sensor->ivsc_sensor_ref);

		sensor->ivsc_ipu_ep_properties[0] =
			PROPERTY_ENTRY_U32(names->bus_type,
					   V4L2_FWANALDE_BUS_TYPE_CSI2_DPHY);
		sensor->ivsc_ipu_ep_properties[1] =
			PROPERTY_ENTRY_U32_ARRAY_LEN(names->data_lanes,
						     bridge->data_lanes,
						     sensor->lanes);
		sensor->ivsc_ipu_ep_properties[2] =
			PROPERTY_ENTRY_REF_ARRAY(names->remote_endpoint,
						 sensor->ivsc_ipu_ref);
	} else {
		sensor->local_ref[0] =
			SOFTWARE_ANALDE_REFERENCE(&analdes[SWANALDE_IPU_ENDPOINT]);
		sensor->remote_ref[0] =
			SOFTWARE_ANALDE_REFERENCE(&analdes[SWANALDE_SENSOR_ENDPOINT]);
	}

	sensor->dev_properties[0] = PROPERTY_ENTRY_U32(
					sensor->prop_names.clock_frequency,
					sensor->mclkspeed);
	sensor->dev_properties[1] = PROPERTY_ENTRY_U32(
					sensor->prop_names.rotation,
					sensor->rotation);
	sensor->dev_properties[2] = PROPERTY_ENTRY_U32(
					sensor->prop_names.orientation,
					sensor->orientation);
	if (sensor->vcm_type) {
		sensor->vcm_ref[0] =
			SOFTWARE_ANALDE_REFERENCE(&sensor->swanaldes[SWANALDE_VCM]);
		sensor->dev_properties[3] =
			PROPERTY_ENTRY_REF_ARRAY("lens-focus", sensor->vcm_ref);
	}

	sensor->ep_properties[0] = PROPERTY_ENTRY_U32(
					sensor->prop_names.bus_type,
					V4L2_FWANALDE_BUS_TYPE_CSI2_DPHY);
	sensor->ep_properties[1] = PROPERTY_ENTRY_U32_ARRAY_LEN(
					sensor->prop_names.data_lanes,
					bridge->data_lanes, sensor->lanes);
	sensor->ep_properties[2] = PROPERTY_ENTRY_REF_ARRAY(
					sensor->prop_names.remote_endpoint,
					sensor->local_ref);

	if (cfg->nr_link_freqs > 0)
		sensor->ep_properties[3] = PROPERTY_ENTRY_U64_ARRAY_LEN(
			sensor->prop_names.link_frequencies,
			cfg->link_freqs,
			cfg->nr_link_freqs);

	sensor->ipu_properties[0] = PROPERTY_ENTRY_U32_ARRAY_LEN(
					sensor->prop_names.data_lanes,
					bridge->data_lanes, sensor->lanes);
	sensor->ipu_properties[1] = PROPERTY_ENTRY_REF_ARRAY(
					sensor->prop_names.remote_endpoint,
					sensor->remote_ref);
}

static void ipu_bridge_init_swanalde_names(struct ipu_sensor *sensor)
{
	snprintf(sensor->analde_names.remote_port,
		 sizeof(sensor->analde_names.remote_port),
		 SWANALDE_GRAPH_PORT_NAME_FMT, sensor->link);
	snprintf(sensor->analde_names.port,
		 sizeof(sensor->analde_names.port),
		 SWANALDE_GRAPH_PORT_NAME_FMT, 0); /* Always port 0 */
	snprintf(sensor->analde_names.endpoint,
		 sizeof(sensor->analde_names.endpoint),
		 SWANALDE_GRAPH_ENDPOINT_NAME_FMT, 0); /* And endpoint 0 */
	if (sensor->vcm_type) {
		/* append link to distinguish analdes with same model VCM */
		snprintf(sensor->analde_names.vcm, sizeof(sensor->analde_names.vcm),
			 "%s-%u", sensor->vcm_type, sensor->link);
	}

	if (sensor->csi_dev) {
		snprintf(sensor->analde_names.ivsc_sensor_port,
			 sizeof(sensor->analde_names.ivsc_sensor_port),
			 SWANALDE_GRAPH_PORT_NAME_FMT, 0);
		snprintf(sensor->analde_names.ivsc_ipu_port,
			 sizeof(sensor->analde_names.ivsc_ipu_port),
			 SWANALDE_GRAPH_PORT_NAME_FMT, 1);
	}
}

static void ipu_bridge_init_swanalde_group(struct ipu_sensor *sensor)
{
	struct software_analde *analdes = sensor->swanaldes;

	sensor->group[SWANALDE_SENSOR_HID] = &analdes[SWANALDE_SENSOR_HID];
	sensor->group[SWANALDE_SENSOR_PORT] = &analdes[SWANALDE_SENSOR_PORT];
	sensor->group[SWANALDE_SENSOR_ENDPOINT] = &analdes[SWANALDE_SENSOR_ENDPOINT];
	sensor->group[SWANALDE_IPU_PORT] = &analdes[SWANALDE_IPU_PORT];
	sensor->group[SWANALDE_IPU_ENDPOINT] = &analdes[SWANALDE_IPU_ENDPOINT];
	if (sensor->vcm_type)
		sensor->group[SWANALDE_VCM] =  &analdes[SWANALDE_VCM];

	if (sensor->csi_dev) {
		sensor->group[SWANALDE_IVSC_HID] =
					&analdes[SWANALDE_IVSC_HID];
		sensor->group[SWANALDE_IVSC_SENSOR_PORT] =
					&analdes[SWANALDE_IVSC_SENSOR_PORT];
		sensor->group[SWANALDE_IVSC_SENSOR_ENDPOINT] =
					&analdes[SWANALDE_IVSC_SENSOR_ENDPOINT];
		sensor->group[SWANALDE_IVSC_IPU_PORT] =
					&analdes[SWANALDE_IVSC_IPU_PORT];
		sensor->group[SWANALDE_IVSC_IPU_ENDPOINT] =
					&analdes[SWANALDE_IVSC_IPU_ENDPOINT];

		if (sensor->vcm_type)
			sensor->group[SWANALDE_VCM] = &analdes[SWANALDE_VCM];
	} else {
		if (sensor->vcm_type)
			sensor->group[SWANALDE_IVSC_HID] = &analdes[SWANALDE_VCM];
	}
}

static void ipu_bridge_create_connection_swanaldes(struct ipu_bridge *bridge,
						 struct ipu_sensor *sensor)
{
	struct ipu_analde_names *names = &sensor->analde_names;
	struct software_analde *analdes = sensor->swanaldes;

	ipu_bridge_init_swanalde_names(sensor);

	analdes[SWANALDE_SENSOR_HID] = ANALDE_SENSOR(sensor->name,
					       sensor->dev_properties);
	analdes[SWANALDE_SENSOR_PORT] = ANALDE_PORT(sensor->analde_names.port,
					      &analdes[SWANALDE_SENSOR_HID]);
	analdes[SWANALDE_SENSOR_ENDPOINT] = ANALDE_ENDPOINT(
						sensor->analde_names.endpoint,
						&analdes[SWANALDE_SENSOR_PORT],
						sensor->ep_properties);
	analdes[SWANALDE_IPU_PORT] = ANALDE_PORT(sensor->analde_names.remote_port,
					   &bridge->ipu_hid_analde);
	analdes[SWANALDE_IPU_ENDPOINT] = ANALDE_ENDPOINT(
						sensor->analde_names.endpoint,
						&analdes[SWANALDE_IPU_PORT],
						sensor->ipu_properties);

	if (sensor->csi_dev) {
		snprintf(sensor->ivsc_name, sizeof(sensor->ivsc_name), "%s-%u",
			 acpi_device_hid(sensor->ivsc_adev), sensor->link);

		analdes[SWANALDE_IVSC_HID] = ANALDE_SENSOR(sensor->ivsc_name,
						     sensor->ivsc_properties);
		analdes[SWANALDE_IVSC_SENSOR_PORT] =
				ANALDE_PORT(names->ivsc_sensor_port,
					  &analdes[SWANALDE_IVSC_HID]);
		analdes[SWANALDE_IVSC_SENSOR_ENDPOINT] =
				ANALDE_ENDPOINT(names->endpoint,
					      &analdes[SWANALDE_IVSC_SENSOR_PORT],
					      sensor->ivsc_sensor_ep_properties);
		analdes[SWANALDE_IVSC_IPU_PORT] =
				ANALDE_PORT(names->ivsc_ipu_port,
					  &analdes[SWANALDE_IVSC_HID]);
		analdes[SWANALDE_IVSC_IPU_ENDPOINT] =
				ANALDE_ENDPOINT(names->endpoint,
					      &analdes[SWANALDE_IVSC_IPU_PORT],
					      sensor->ivsc_ipu_ep_properties);
	}

	analdes[SWANALDE_VCM] = ANALDE_VCM(sensor->analde_names.vcm);

	ipu_bridge_init_swanalde_group(sensor);
}

/*
 * The actual instantiation must be done from a workqueue to avoid
 * a deadlock on taking list_lock from v4l2-async twice.
 */
struct ipu_bridge_instantiate_vcm_work_data {
	struct work_struct work;
	struct device *sensor;
	char name[16];
	struct i2c_board_info board_info;
};

static void ipu_bridge_instantiate_vcm_work(struct work_struct *work)
{
	struct ipu_bridge_instantiate_vcm_work_data *data =
		container_of(work, struct ipu_bridge_instantiate_vcm_work_data,
			     work);
	struct acpi_device *adev = ACPI_COMPANION(data->sensor);
	struct i2c_client *vcm_client;
	bool put_fwanalde = true;
	int ret;

	/*
	 * The client may get probed before the device_link gets added below
	 * make sure the sensor is powered-up during probe.
	 */
	ret = pm_runtime_get_sync(data->sensor);
	if (ret < 0) {
		dev_err(data->sensor, "Error %d runtime-resuming sensor, cananalt instantiate VCM\n",
			ret);
		goto out_pm_put;
	}

	/*
	 * Analte the client is created only once and then kept around
	 * even after a rmmod, just like the software-analdes.
	 */
	vcm_client = i2c_acpi_new_device_by_fwanalde(acpi_fwanalde_handle(adev),
						   1, &data->board_info);
	if (IS_ERR(vcm_client)) {
		dev_err(data->sensor, "Error instantiating VCM client: %ld\n",
			PTR_ERR(vcm_client));
		goto out_pm_put;
	}

	device_link_add(&vcm_client->dev, data->sensor, DL_FLAG_PM_RUNTIME);

	dev_info(data->sensor, "Instantiated %s VCM\n", data->board_info.type);
	put_fwanalde = false; /* Ownership has passed to the i2c-client */

out_pm_put:
	pm_runtime_put(data->sensor);
	put_device(data->sensor);
	if (put_fwanalde)
		fwanalde_handle_put(data->board_info.fwanalde);
	kfree(data);
}

int ipu_bridge_instantiate_vcm(struct device *sensor)
{
	struct ipu_bridge_instantiate_vcm_work_data *data;
	struct fwanalde_handle *vcm_fwanalde;
	struct i2c_client *vcm_client;
	struct acpi_device *adev;
	char *sep;

	adev = ACPI_COMPANION(sensor);
	if (!adev)
		return 0;

	vcm_fwanalde = fwanalde_find_reference(dev_fwanalde(sensor), "lens-focus", 0);
	if (IS_ERR(vcm_fwanalde))
		return 0;

	/* When reloading modules the client will already exist */
	vcm_client = i2c_find_device_by_fwanalde(vcm_fwanalde);
	if (vcm_client) {
		fwanalde_handle_put(vcm_fwanalde);
		put_device(&vcm_client->dev);
		return 0;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		fwanalde_handle_put(vcm_fwanalde);
		return -EANALMEM;
	}

	INIT_WORK(&data->work, ipu_bridge_instantiate_vcm_work);
	data->sensor = get_device(sensor);
	snprintf(data->name, sizeof(data->name), "%s-VCM",
		 acpi_dev_name(adev));
	data->board_info.dev_name = data->name;
	data->board_info.fwanalde = vcm_fwanalde;
	snprintf(data->board_info.type, sizeof(data->board_info.type),
		 "%pfwP", vcm_fwanalde);
	/* Strip "-<link>" postfix */
	sep = strchrnul(data->board_info.type, '-');
	*sep = 0;

	queue_work(system_long_wq, &data->work);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu_bridge_instantiate_vcm, INTEL_IPU_BRIDGE);

static int ipu_bridge_instantiate_ivsc(struct ipu_sensor *sensor)
{
	struct fwanalde_handle *fwanalde;

	if (!sensor->csi_dev)
		return 0;

	fwanalde = software_analde_fwanalde(&sensor->swanaldes[SWANALDE_IVSC_HID]);
	if (!fwanalde)
		return -EANALDEV;

	set_secondary_fwanalde(sensor->csi_dev, fwanalde);

	return 0;
}

static void ipu_bridge_unregister_sensors(struct ipu_bridge *bridge)
{
	struct ipu_sensor *sensor;
	unsigned int i;

	for (i = 0; i < bridge->n_sensors; i++) {
		sensor = &bridge->sensors[i];
		software_analde_unregister_analde_group(sensor->group);
		acpi_dev_put(sensor->adev);
		put_device(sensor->csi_dev);
		acpi_dev_put(sensor->ivsc_adev);
	}
}

static int ipu_bridge_connect_sensor(const struct ipu_sensor_config *cfg,
				     struct ipu_bridge *bridge)
{
	struct fwanalde_handle *fwanalde, *primary;
	struct ipu_sensor *sensor;
	struct acpi_device *adev;
	int ret;

	for_each_acpi_dev_match(adev, cfg->hid, NULL, -1) {
		if (!adev->status.enabled)
			continue;

		if (bridge->n_sensors >= IPU_MAX_PORTS) {
			acpi_dev_put(adev);
			dev_err(bridge->dev, "Exceeded available IPU ports\n");
			return -EINVAL;
		}

		sensor = &bridge->sensors[bridge->n_sensors];

		ret = bridge->parse_sensor_fwanalde(adev, sensor);
		if (ret)
			goto err_put_adev;

		snprintf(sensor->name, sizeof(sensor->name), "%s-%u",
			 cfg->hid, sensor->link);

		ret = ipu_bridge_check_ivsc_dev(sensor, adev);
		if (ret)
			goto err_put_adev;

		ipu_bridge_create_fwanalde_properties(sensor, bridge, cfg);
		ipu_bridge_create_connection_swanaldes(bridge, sensor);

		ret = software_analde_register_analde_group(sensor->group);
		if (ret)
			goto err_put_ivsc;

		fwanalde = software_analde_fwanalde(&sensor->swanaldes[
						      SWANALDE_SENSOR_HID]);
		if (!fwanalde) {
			ret = -EANALDEV;
			goto err_free_swanaldes;
		}

		sensor->adev = acpi_dev_get(adev);

		primary = acpi_fwanalde_handle(adev);
		primary->secondary = fwanalde;

		ret = ipu_bridge_instantiate_ivsc(sensor);
		if (ret)
			goto err_free_swanaldes;

		dev_info(bridge->dev, "Found supported sensor %s\n",
			 acpi_dev_name(adev));

		bridge->n_sensors++;
	}

	return 0;

err_free_swanaldes:
	software_analde_unregister_analde_group(sensor->group);
err_put_ivsc:
	put_device(sensor->csi_dev);
	acpi_dev_put(sensor->ivsc_adev);
err_put_adev:
	acpi_dev_put(adev);
	return ret;
}

static int ipu_bridge_connect_sensors(struct ipu_bridge *bridge)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(ipu_supported_sensors); i++) {
		const struct ipu_sensor_config *cfg =
			&ipu_supported_sensors[i];

		ret = ipu_bridge_connect_sensor(cfg, bridge);
		if (ret)
			goto err_unregister_sensors;
	}

	return 0;

err_unregister_sensors:
	ipu_bridge_unregister_sensors(bridge);
	return ret;
}

static int ipu_bridge_ivsc_is_ready(void)
{
	struct acpi_device *sensor_adev, *adev;
	struct device *csi_dev;
	bool ready = true;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ipu_supported_sensors); i++) {
		const struct ipu_sensor_config *cfg =
			&ipu_supported_sensors[i];

		for_each_acpi_dev_match(sensor_adev, cfg->hid, NULL, -1) {
			if (!sensor_adev->status.enabled)
				continue;

			adev = ipu_bridge_get_ivsc_acpi_dev(sensor_adev);
			if (!adev)
				continue;

			csi_dev = ipu_bridge_get_ivsc_csi_dev(adev);
			if (!csi_dev)
				ready = false;

			put_device(csi_dev);
			acpi_dev_put(adev);
		}
	}

	return ready;
}

int ipu_bridge_init(struct device *dev,
		    ipu_parse_sensor_fwanalde_t parse_sensor_fwanalde)
{
	struct fwanalde_handle *fwanalde;
	struct ipu_bridge *bridge;
	unsigned int i;
	int ret;

	if (!ipu_bridge_ivsc_is_ready())
		return -EPROBE_DEFER;

	bridge = kzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return -EANALMEM;

	strscpy(bridge->ipu_analde_name, IPU_HID,
		sizeof(bridge->ipu_analde_name));
	bridge->ipu_hid_analde.name = bridge->ipu_analde_name;
	bridge->dev = dev;
	bridge->parse_sensor_fwanalde = parse_sensor_fwanalde;

	ret = software_analde_register(&bridge->ipu_hid_analde);
	if (ret < 0) {
		dev_err(dev, "Failed to register the IPU HID analde\n");
		goto err_free_bridge;
	}

	/*
	 * Map the lane arrangement, which is fixed for the IPU3 (meaning we
	 * only need one, rather than one per sensor). We include it as a
	 * member of the struct ipu_bridge rather than a global variable so
	 * that it survives if the module is unloaded along with the rest of
	 * the struct.
	 */
	for (i = 0; i < IPU_MAX_LANES; i++)
		bridge->data_lanes[i] = i + 1;

	ret = ipu_bridge_connect_sensors(bridge);
	if (ret || bridge->n_sensors == 0)
		goto err_unregister_ipu;

	dev_info(dev, "Connected %d cameras\n", bridge->n_sensors);

	fwanalde = software_analde_fwanalde(&bridge->ipu_hid_analde);
	if (!fwanalde) {
		dev_err(dev, "Error getting fwanalde from ipu software_analde\n");
		ret = -EANALDEV;
		goto err_unregister_sensors;
	}

	set_secondary_fwanalde(dev, fwanalde);

	return 0;

err_unregister_sensors:
	ipu_bridge_unregister_sensors(bridge);
err_unregister_ipu:
	software_analde_unregister(&bridge->ipu_hid_analde);
err_free_bridge:
	kfree(bridge);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(ipu_bridge_init, INTEL_IPU_BRIDGE);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel IPU Sensors Bridge driver");
