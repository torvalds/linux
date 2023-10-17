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
#include <media/v4l2-fwnode.h>

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
 * Extend this array with ACPI Hardware IDs of devices known to be working
 * plus the number of link-frequencies expected by their drivers, along with
 * the frequency values in hertz. This is somewhat opportunistic way of adding
 * support for this for now in the hopes of a better source for the information
 * (possibly some encoded value in the SSDB buffer that we're unaware of)
 * becoming apparent in the future.
 *
 * Do not add an entry for a sensor that is not actually supported.
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
	IPU_SENSOR_CONFIG("INT3474", 1, 360000000),
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
			return -ENODEV;
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
		return -ENODEV;

	obj = buffer.pointer;
	if (!obj) {
		dev_err(&adev->dev, "Couldn't locate ACPI buffer\n");
		return -ENODEV;
	}

	if (obj->type != ACPI_TYPE_BUFFER) {
		dev_err(&adev->dev, "Not an ACPI buffer\n");
		ret = -ENODEV;
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
	case IPU_SENSOR_ROTATION_NORMAL:
		return 0;
	case IPU_SENSOR_ROTATION_INVERTED:
		return 180;
	default:
		dev_warn(&adev->dev,
			 "Unknown rotation %d. Assume 0 degree rotation\n",
			 ssdb->degree);
		return 0;
	}
}

static enum v4l2_fwnode_orientation ipu_bridge_parse_orientation(struct acpi_device *adev)
{
	enum v4l2_fwnode_orientation orientation;
	struct acpi_pld_info *pld;
	acpi_status status;

	status = acpi_get_physical_device_location(adev->handle, &pld);
	if (ACPI_FAILURE(status)) {
		dev_warn(&adev->dev, "_PLD call failed, using default orientation\n");
		return V4L2_FWNODE_ORIENTATION_EXTERNAL;
	}

	switch (pld->panel) {
	case ACPI_PLD_PANEL_FRONT:
		orientation = V4L2_FWNODE_ORIENTATION_FRONT;
		break;
	case ACPI_PLD_PANEL_BACK:
		orientation = V4L2_FWNODE_ORIENTATION_BACK;
		break;
	case ACPI_PLD_PANEL_TOP:
	case ACPI_PLD_PANEL_LEFT:
	case ACPI_PLD_PANEL_RIGHT:
	case ACPI_PLD_PANEL_UNKNOWN:
		orientation = V4L2_FWNODE_ORIENTATION_EXTERNAL;
		break;
	default:
		dev_warn(&adev->dev, "Unknown _PLD panel val %d\n", pld->panel);
		orientation = V4L2_FWNODE_ORIENTATION_EXTERNAL;
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
		dev_warn(&adev->dev, "Unknown VCM type %d\n", ssdb.vcmtype);
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

static void ipu_bridge_create_fwnode_properties(
	struct ipu_sensor *sensor,
	struct ipu_bridge *bridge,
	const struct ipu_sensor_config *cfg)
{
	struct ipu_property_names *names = &sensor->prop_names;
	struct software_node *nodes = sensor->swnodes;

	sensor->prop_names = prop_names;

	if (sensor->csi_dev) {
		sensor->local_ref[0] =
			SOFTWARE_NODE_REFERENCE(&nodes[SWNODE_IVSC_SENSOR_ENDPOINT]);
		sensor->remote_ref[0] =
			SOFTWARE_NODE_REFERENCE(&nodes[SWNODE_IVSC_IPU_ENDPOINT]);
		sensor->ivsc_sensor_ref[0] =
			SOFTWARE_NODE_REFERENCE(&nodes[SWNODE_SENSOR_ENDPOINT]);
		sensor->ivsc_ipu_ref[0] =
			SOFTWARE_NODE_REFERENCE(&nodes[SWNODE_IPU_ENDPOINT]);

		sensor->ivsc_sensor_ep_properties[0] =
			PROPERTY_ENTRY_U32(names->bus_type,
					   V4L2_FWNODE_BUS_TYPE_CSI2_DPHY);
		sensor->ivsc_sensor_ep_properties[1] =
			PROPERTY_ENTRY_U32_ARRAY_LEN(names->data_lanes,
						     bridge->data_lanes,
						     sensor->lanes);
		sensor->ivsc_sensor_ep_properties[2] =
			PROPERTY_ENTRY_REF_ARRAY(names->remote_endpoint,
						 sensor->ivsc_sensor_ref);

		sensor->ivsc_ipu_ep_properties[0] =
			PROPERTY_ENTRY_U32(names->bus_type,
					   V4L2_FWNODE_BUS_TYPE_CSI2_DPHY);
		sensor->ivsc_ipu_ep_properties[1] =
			PROPERTY_ENTRY_U32_ARRAY_LEN(names->data_lanes,
						     bridge->data_lanes,
						     sensor->lanes);
		sensor->ivsc_ipu_ep_properties[2] =
			PROPERTY_ENTRY_REF_ARRAY(names->remote_endpoint,
						 sensor->ivsc_ipu_ref);
	} else {
		sensor->local_ref[0] =
			SOFTWARE_NODE_REFERENCE(&nodes[SWNODE_IPU_ENDPOINT]);
		sensor->remote_ref[0] =
			SOFTWARE_NODE_REFERENCE(&nodes[SWNODE_SENSOR_ENDPOINT]);
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
			SOFTWARE_NODE_REFERENCE(&sensor->swnodes[SWNODE_VCM]);
		sensor->dev_properties[3] =
			PROPERTY_ENTRY_REF_ARRAY("lens-focus", sensor->vcm_ref);
	}

	sensor->ep_properties[0] = PROPERTY_ENTRY_U32(
					sensor->prop_names.bus_type,
					V4L2_FWNODE_BUS_TYPE_CSI2_DPHY);
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

static void ipu_bridge_init_swnode_names(struct ipu_sensor *sensor)
{
	snprintf(sensor->node_names.remote_port,
		 sizeof(sensor->node_names.remote_port),
		 SWNODE_GRAPH_PORT_NAME_FMT, sensor->link);
	snprintf(sensor->node_names.port,
		 sizeof(sensor->node_names.port),
		 SWNODE_GRAPH_PORT_NAME_FMT, 0); /* Always port 0 */
	snprintf(sensor->node_names.endpoint,
		 sizeof(sensor->node_names.endpoint),
		 SWNODE_GRAPH_ENDPOINT_NAME_FMT, 0); /* And endpoint 0 */
	if (sensor->vcm_type) {
		/* append link to distinguish nodes with same model VCM */
		snprintf(sensor->node_names.vcm, sizeof(sensor->node_names.vcm),
			 "%s-%u", sensor->vcm_type, sensor->link);
	}

	if (sensor->csi_dev) {
		snprintf(sensor->node_names.ivsc_sensor_port,
			 sizeof(sensor->node_names.ivsc_sensor_port),
			 SWNODE_GRAPH_PORT_NAME_FMT, 0);
		snprintf(sensor->node_names.ivsc_ipu_port,
			 sizeof(sensor->node_names.ivsc_ipu_port),
			 SWNODE_GRAPH_PORT_NAME_FMT, 1);
	}
}

static void ipu_bridge_init_swnode_group(struct ipu_sensor *sensor)
{
	struct software_node *nodes = sensor->swnodes;

	sensor->group[SWNODE_SENSOR_HID] = &nodes[SWNODE_SENSOR_HID];
	sensor->group[SWNODE_SENSOR_PORT] = &nodes[SWNODE_SENSOR_PORT];
	sensor->group[SWNODE_SENSOR_ENDPOINT] = &nodes[SWNODE_SENSOR_ENDPOINT];
	sensor->group[SWNODE_IPU_PORT] = &nodes[SWNODE_IPU_PORT];
	sensor->group[SWNODE_IPU_ENDPOINT] = &nodes[SWNODE_IPU_ENDPOINT];
	if (sensor->vcm_type)
		sensor->group[SWNODE_VCM] =  &nodes[SWNODE_VCM];

	if (sensor->csi_dev) {
		sensor->group[SWNODE_IVSC_HID] =
					&nodes[SWNODE_IVSC_HID];
		sensor->group[SWNODE_IVSC_SENSOR_PORT] =
					&nodes[SWNODE_IVSC_SENSOR_PORT];
		sensor->group[SWNODE_IVSC_SENSOR_ENDPOINT] =
					&nodes[SWNODE_IVSC_SENSOR_ENDPOINT];
		sensor->group[SWNODE_IVSC_IPU_PORT] =
					&nodes[SWNODE_IVSC_IPU_PORT];
		sensor->group[SWNODE_IVSC_IPU_ENDPOINT] =
					&nodes[SWNODE_IVSC_IPU_ENDPOINT];

		if (sensor->vcm_type)
			sensor->group[SWNODE_VCM] = &nodes[SWNODE_VCM];
	} else {
		if (sensor->vcm_type)
			sensor->group[SWNODE_IVSC_HID] = &nodes[SWNODE_VCM];
	}
}

static void ipu_bridge_create_connection_swnodes(struct ipu_bridge *bridge,
						 struct ipu_sensor *sensor)
{
	struct ipu_node_names *names = &sensor->node_names;
	struct software_node *nodes = sensor->swnodes;

	ipu_bridge_init_swnode_names(sensor);

	nodes[SWNODE_SENSOR_HID] = NODE_SENSOR(sensor->name,
					       sensor->dev_properties);
	nodes[SWNODE_SENSOR_PORT] = NODE_PORT(sensor->node_names.port,
					      &nodes[SWNODE_SENSOR_HID]);
	nodes[SWNODE_SENSOR_ENDPOINT] = NODE_ENDPOINT(
						sensor->node_names.endpoint,
						&nodes[SWNODE_SENSOR_PORT],
						sensor->ep_properties);
	nodes[SWNODE_IPU_PORT] = NODE_PORT(sensor->node_names.remote_port,
					   &bridge->ipu_hid_node);
	nodes[SWNODE_IPU_ENDPOINT] = NODE_ENDPOINT(
						sensor->node_names.endpoint,
						&nodes[SWNODE_IPU_PORT],
						sensor->ipu_properties);

	if (sensor->csi_dev) {
		snprintf(sensor->ivsc_name, sizeof(sensor->ivsc_name), "%s-%u",
			 acpi_device_hid(sensor->ivsc_adev), sensor->link);

		nodes[SWNODE_IVSC_HID] = NODE_SENSOR(sensor->ivsc_name,
						     sensor->ivsc_properties);
		nodes[SWNODE_IVSC_SENSOR_PORT] =
				NODE_PORT(names->ivsc_sensor_port,
					  &nodes[SWNODE_IVSC_HID]);
		nodes[SWNODE_IVSC_SENSOR_ENDPOINT] =
				NODE_ENDPOINT(names->endpoint,
					      &nodes[SWNODE_IVSC_SENSOR_PORT],
					      sensor->ivsc_sensor_ep_properties);
		nodes[SWNODE_IVSC_IPU_PORT] =
				NODE_PORT(names->ivsc_ipu_port,
					  &nodes[SWNODE_IVSC_HID]);
		nodes[SWNODE_IVSC_IPU_ENDPOINT] =
				NODE_ENDPOINT(names->endpoint,
					      &nodes[SWNODE_IVSC_IPU_PORT],
					      sensor->ivsc_ipu_ep_properties);
	}

	nodes[SWNODE_VCM] = NODE_VCM(sensor->node_names.vcm);

	ipu_bridge_init_swnode_group(sensor);
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
	bool put_fwnode = true;
	int ret;

	/*
	 * The client may get probed before the device_link gets added below
	 * make sure the sensor is powered-up during probe.
	 */
	ret = pm_runtime_get_sync(data->sensor);
	if (ret < 0) {
		dev_err(data->sensor, "Error %d runtime-resuming sensor, cannot instantiate VCM\n",
			ret);
		goto out_pm_put;
	}

	/*
	 * Note the client is created only once and then kept around
	 * even after a rmmod, just like the software-nodes.
	 */
	vcm_client = i2c_acpi_new_device_by_fwnode(acpi_fwnode_handle(adev),
						   1, &data->board_info);
	if (IS_ERR(vcm_client)) {
		dev_err(data->sensor, "Error instantiating VCM client: %ld\n",
			PTR_ERR(vcm_client));
		goto out_pm_put;
	}

	device_link_add(&vcm_client->dev, data->sensor, DL_FLAG_PM_RUNTIME);

	dev_info(data->sensor, "Instantiated %s VCM\n", data->board_info.type);
	put_fwnode = false; /* Ownership has passed to the i2c-client */

out_pm_put:
	pm_runtime_put(data->sensor);
	put_device(data->sensor);
	if (put_fwnode)
		fwnode_handle_put(data->board_info.fwnode);
	kfree(data);
}

int ipu_bridge_instantiate_vcm(struct device *sensor)
{
	struct ipu_bridge_instantiate_vcm_work_data *data;
	struct fwnode_handle *vcm_fwnode;
	struct i2c_client *vcm_client;
	struct acpi_device *adev;
	char *sep;

	adev = ACPI_COMPANION(sensor);
	if (!adev)
		return 0;

	vcm_fwnode = fwnode_find_reference(dev_fwnode(sensor), "lens-focus", 0);
	if (IS_ERR(vcm_fwnode))
		return 0;

	/* When reloading modules the client will already exist */
	vcm_client = i2c_find_device_by_fwnode(vcm_fwnode);
	if (vcm_client) {
		fwnode_handle_put(vcm_fwnode);
		put_device(&vcm_client->dev);
		return 0;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		fwnode_handle_put(vcm_fwnode);
		return -ENOMEM;
	}

	INIT_WORK(&data->work, ipu_bridge_instantiate_vcm_work);
	data->sensor = get_device(sensor);
	snprintf(data->name, sizeof(data->name), "%s-VCM",
		 acpi_dev_name(adev));
	data->board_info.dev_name = data->name;
	data->board_info.fwnode = vcm_fwnode;
	snprintf(data->board_info.type, sizeof(data->board_info.type),
		 "%pfwP", vcm_fwnode);
	/* Strip "-<link>" postfix */
	sep = strchrnul(data->board_info.type, '-');
	*sep = 0;

	queue_work(system_long_wq, &data->work);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu_bridge_instantiate_vcm, INTEL_IPU_BRIDGE);

static int ipu_bridge_instantiate_ivsc(struct ipu_sensor *sensor)
{
	struct fwnode_handle *fwnode;

	if (!sensor->csi_dev)
		return 0;

	fwnode = software_node_fwnode(&sensor->swnodes[SWNODE_IVSC_HID]);
	if (!fwnode)
		return -ENODEV;

	set_secondary_fwnode(sensor->csi_dev, fwnode);

	return 0;
}

static void ipu_bridge_unregister_sensors(struct ipu_bridge *bridge)
{
	struct ipu_sensor *sensor;
	unsigned int i;

	for (i = 0; i < bridge->n_sensors; i++) {
		sensor = &bridge->sensors[i];
		software_node_unregister_node_group(sensor->group);
		acpi_dev_put(sensor->adev);
		put_device(sensor->csi_dev);
		acpi_dev_put(sensor->ivsc_adev);
	}
}

static int ipu_bridge_connect_sensor(const struct ipu_sensor_config *cfg,
				     struct ipu_bridge *bridge)
{
	struct fwnode_handle *fwnode, *primary;
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

		ret = bridge->parse_sensor_fwnode(adev, sensor);
		if (ret)
			goto err_put_adev;

		snprintf(sensor->name, sizeof(sensor->name), "%s-%u",
			 cfg->hid, sensor->link);

		ret = ipu_bridge_check_ivsc_dev(sensor, adev);
		if (ret)
			goto err_put_adev;

		ipu_bridge_create_fwnode_properties(sensor, bridge, cfg);
		ipu_bridge_create_connection_swnodes(bridge, sensor);

		ret = software_node_register_node_group(sensor->group);
		if (ret)
			goto err_put_ivsc;

		fwnode = software_node_fwnode(&sensor->swnodes[
						      SWNODE_SENSOR_HID]);
		if (!fwnode) {
			ret = -ENODEV;
			goto err_free_swnodes;
		}

		sensor->adev = acpi_dev_get(adev);

		primary = acpi_fwnode_handle(adev);
		primary->secondary = fwnode;

		ret = ipu_bridge_instantiate_ivsc(sensor);
		if (ret)
			goto err_free_swnodes;

		dev_info(bridge->dev, "Found supported sensor %s\n",
			 acpi_dev_name(adev));

		bridge->n_sensors++;
	}

	return 0;

err_free_swnodes:
	software_node_unregister_node_group(sensor->group);
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
		    ipu_parse_sensor_fwnode_t parse_sensor_fwnode)
{
	struct fwnode_handle *fwnode;
	struct ipu_bridge *bridge;
	unsigned int i;
	int ret;

	if (!ipu_bridge_ivsc_is_ready())
		return -EPROBE_DEFER;

	bridge = kzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return -ENOMEM;

	strscpy(bridge->ipu_node_name, IPU_HID,
		sizeof(bridge->ipu_node_name));
	bridge->ipu_hid_node.name = bridge->ipu_node_name;
	bridge->dev = dev;
	bridge->parse_sensor_fwnode = parse_sensor_fwnode;

	ret = software_node_register(&bridge->ipu_hid_node);
	if (ret < 0) {
		dev_err(dev, "Failed to register the IPU HID node\n");
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

	fwnode = software_node_fwnode(&bridge->ipu_hid_node);
	if (!fwnode) {
		dev_err(dev, "Error getting fwnode from ipu software_node\n");
		ret = -ENODEV;
		goto err_unregister_sensors;
	}

	set_secondary_fwnode(dev, fwnode);

	return 0;

err_unregister_sensors:
	ipu_bridge_unregister_sensors(bridge);
err_unregister_ipu:
	software_node_unregister(&bridge->ipu_hid_node);
err_free_bridge:
	kfree(bridge);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(ipu_bridge_init, INTEL_IPU_BRIDGE);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel IPU Sensors Bridge driver");
