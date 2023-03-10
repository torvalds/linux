// SPDX-License-Identifier: GPL-2.0
/* Author: Dan Scally <djrscally@gmail.com> */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/pci.h>
#include <linux/property.h>
#include <media/v4l2-fwnode.h>

#include "cio2-bridge.h"

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
static const struct cio2_sensor_config cio2_supported_sensors[] = {
	/* Omnivision OV5693 */
	CIO2_SENSOR_CONFIG("INT33BE", 1, 419200000),
	/* Omnivision OV8865 */
	CIO2_SENSOR_CONFIG("INT347A", 1, 360000000),
	/* Omnivision OV7251 */
	CIO2_SENSOR_CONFIG("INT347E", 1, 319200000),
	/* Omnivision OV2680 */
	CIO2_SENSOR_CONFIG("OVTI2680", 0),
};

static const struct cio2_property_names prop_names = {
	.clock_frequency = "clock-frequency",
	.rotation = "rotation",
	.orientation = "orientation",
	.bus_type = "bus-type",
	.data_lanes = "data-lanes",
	.remote_endpoint = "remote-endpoint",
	.link_frequencies = "link-frequencies",
};

static const char * const cio2_vcm_types[] = {
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

static int cio2_bridge_read_acpi_buffer(struct acpi_device *adev, char *id,
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

static u32 cio2_bridge_parse_rotation(struct cio2_sensor *sensor)
{
	switch (sensor->ssdb.degree) {
	case CIO2_SENSOR_ROTATION_NORMAL:
		return 0;
	case CIO2_SENSOR_ROTATION_INVERTED:
		return 180;
	default:
		dev_warn(&sensor->adev->dev,
			 "Unknown rotation %d. Assume 0 degree rotation\n",
			 sensor->ssdb.degree);
		return 0;
	}
}

static enum v4l2_fwnode_orientation cio2_bridge_parse_orientation(struct cio2_sensor *sensor)
{
	switch (sensor->pld->panel) {
	case ACPI_PLD_PANEL_FRONT:
		return V4L2_FWNODE_ORIENTATION_FRONT;
	case ACPI_PLD_PANEL_BACK:
		return V4L2_FWNODE_ORIENTATION_BACK;
	case ACPI_PLD_PANEL_TOP:
	case ACPI_PLD_PANEL_LEFT:
	case ACPI_PLD_PANEL_RIGHT:
	case ACPI_PLD_PANEL_UNKNOWN:
		return V4L2_FWNODE_ORIENTATION_EXTERNAL;
	default:
		dev_warn(&sensor->adev->dev, "Unknown _PLD panel value %d\n",
			 sensor->pld->panel);
		return V4L2_FWNODE_ORIENTATION_EXTERNAL;
	}
}

static void cio2_bridge_create_fwnode_properties(
	struct cio2_sensor *sensor,
	struct cio2_bridge *bridge,
	const struct cio2_sensor_config *cfg)
{
	u32 rotation;
	enum v4l2_fwnode_orientation orientation;

	rotation = cio2_bridge_parse_rotation(sensor);
	orientation = cio2_bridge_parse_orientation(sensor);

	sensor->prop_names = prop_names;

	sensor->local_ref[0] = SOFTWARE_NODE_REFERENCE(&sensor->swnodes[SWNODE_CIO2_ENDPOINT]);
	sensor->remote_ref[0] = SOFTWARE_NODE_REFERENCE(&sensor->swnodes[SWNODE_SENSOR_ENDPOINT]);

	sensor->dev_properties[0] = PROPERTY_ENTRY_U32(
					sensor->prop_names.clock_frequency,
					sensor->ssdb.mclkspeed);
	sensor->dev_properties[1] = PROPERTY_ENTRY_U32(
					sensor->prop_names.rotation,
					rotation);
	sensor->dev_properties[2] = PROPERTY_ENTRY_U32(
					sensor->prop_names.orientation,
					orientation);
	if (sensor->ssdb.vcmtype) {
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
					bridge->data_lanes,
					sensor->ssdb.lanes);
	sensor->ep_properties[2] = PROPERTY_ENTRY_REF_ARRAY(
					sensor->prop_names.remote_endpoint,
					sensor->local_ref);

	if (cfg->nr_link_freqs > 0)
		sensor->ep_properties[3] = PROPERTY_ENTRY_U64_ARRAY_LEN(
			sensor->prop_names.link_frequencies,
			cfg->link_freqs,
			cfg->nr_link_freqs);

	sensor->cio2_properties[0] = PROPERTY_ENTRY_U32_ARRAY_LEN(
					sensor->prop_names.data_lanes,
					bridge->data_lanes,
					sensor->ssdb.lanes);
	sensor->cio2_properties[1] = PROPERTY_ENTRY_REF_ARRAY(
					sensor->prop_names.remote_endpoint,
					sensor->remote_ref);
}

static void cio2_bridge_init_swnode_names(struct cio2_sensor *sensor)
{
	snprintf(sensor->node_names.remote_port,
		 sizeof(sensor->node_names.remote_port),
		 SWNODE_GRAPH_PORT_NAME_FMT, sensor->ssdb.link);
	snprintf(sensor->node_names.port,
		 sizeof(sensor->node_names.port),
		 SWNODE_GRAPH_PORT_NAME_FMT, 0); /* Always port 0 */
	snprintf(sensor->node_names.endpoint,
		 sizeof(sensor->node_names.endpoint),
		 SWNODE_GRAPH_ENDPOINT_NAME_FMT, 0); /* And endpoint 0 */
}

static void cio2_bridge_init_swnode_group(struct cio2_sensor *sensor)
{
	struct software_node *nodes = sensor->swnodes;

	sensor->group[SWNODE_SENSOR_HID] = &nodes[SWNODE_SENSOR_HID];
	sensor->group[SWNODE_SENSOR_PORT] = &nodes[SWNODE_SENSOR_PORT];
	sensor->group[SWNODE_SENSOR_ENDPOINT] = &nodes[SWNODE_SENSOR_ENDPOINT];
	sensor->group[SWNODE_CIO2_PORT] = &nodes[SWNODE_CIO2_PORT];
	sensor->group[SWNODE_CIO2_ENDPOINT] = &nodes[SWNODE_CIO2_ENDPOINT];
	if (sensor->ssdb.vcmtype)
		sensor->group[SWNODE_VCM] =  &nodes[SWNODE_VCM];
}

static void cio2_bridge_create_connection_swnodes(struct cio2_bridge *bridge,
						  struct cio2_sensor *sensor)
{
	struct software_node *nodes = sensor->swnodes;
	char vcm_name[ACPI_ID_LEN + 4];

	cio2_bridge_init_swnode_names(sensor);

	nodes[SWNODE_SENSOR_HID] = NODE_SENSOR(sensor->name,
					       sensor->dev_properties);
	nodes[SWNODE_SENSOR_PORT] = NODE_PORT(sensor->node_names.port,
					      &nodes[SWNODE_SENSOR_HID]);
	nodes[SWNODE_SENSOR_ENDPOINT] = NODE_ENDPOINT(
						sensor->node_names.endpoint,
						&nodes[SWNODE_SENSOR_PORT],
						sensor->ep_properties);
	nodes[SWNODE_CIO2_PORT] = NODE_PORT(sensor->node_names.remote_port,
					    &bridge->cio2_hid_node);
	nodes[SWNODE_CIO2_ENDPOINT] = NODE_ENDPOINT(
						sensor->node_names.endpoint,
						&nodes[SWNODE_CIO2_PORT],
						sensor->cio2_properties);
	if (sensor->ssdb.vcmtype) {
		/* append ssdb.link to distinguish VCM nodes with same HID */
		snprintf(vcm_name, sizeof(vcm_name), "%s-%u",
			 cio2_vcm_types[sensor->ssdb.vcmtype - 1],
			 sensor->ssdb.link);
		nodes[SWNODE_VCM] = NODE_VCM(vcm_name);
	}

	cio2_bridge_init_swnode_group(sensor);
}

static void cio2_bridge_instantiate_vcm_i2c_client(struct cio2_sensor *sensor)
{
	struct i2c_board_info board_info = { };
	char name[16];

	if (!sensor->ssdb.vcmtype)
		return;

	snprintf(name, sizeof(name), "%s-VCM", acpi_dev_name(sensor->adev));
	board_info.dev_name = name;
	strscpy(board_info.type, cio2_vcm_types[sensor->ssdb.vcmtype - 1],
		ARRAY_SIZE(board_info.type));
	board_info.swnode = &sensor->swnodes[SWNODE_VCM];

	sensor->vcm_i2c_client =
		i2c_acpi_new_device_by_fwnode(acpi_fwnode_handle(sensor->adev),
					      1, &board_info);
	if (IS_ERR(sensor->vcm_i2c_client)) {
		dev_warn(&sensor->adev->dev, "Error instantiation VCM i2c-client: %ld\n",
			 PTR_ERR(sensor->vcm_i2c_client));
		sensor->vcm_i2c_client = NULL;
	}
}

static void cio2_bridge_unregister_sensors(struct cio2_bridge *bridge)
{
	struct cio2_sensor *sensor;
	unsigned int i;

	for (i = 0; i < bridge->n_sensors; i++) {
		sensor = &bridge->sensors[i];
		software_node_unregister_node_group(sensor->group);
		ACPI_FREE(sensor->pld);
		acpi_dev_put(sensor->adev);
		i2c_unregister_device(sensor->vcm_i2c_client);
	}
}

static int cio2_bridge_connect_sensor(const struct cio2_sensor_config *cfg,
				      struct cio2_bridge *bridge,
				      struct pci_dev *cio2)
{
	struct fwnode_handle *fwnode, *primary;
	struct cio2_sensor *sensor;
	struct acpi_device *adev;
	acpi_status status;
	int ret;

	for_each_acpi_dev_match(adev, cfg->hid, NULL, -1) {
		if (!adev->status.enabled)
			continue;

		if (bridge->n_sensors >= CIO2_NUM_PORTS) {
			acpi_dev_put(adev);
			dev_err(&cio2->dev, "Exceeded available CIO2 ports\n");
			return -EINVAL;
		}

		sensor = &bridge->sensors[bridge->n_sensors];

		ret = cio2_bridge_read_acpi_buffer(adev, "SSDB",
						   &sensor->ssdb,
						   sizeof(sensor->ssdb));
		if (ret)
			goto err_put_adev;

		snprintf(sensor->name, sizeof(sensor->name), "%s-%u",
			 cfg->hid, sensor->ssdb.link);

		if (sensor->ssdb.vcmtype > ARRAY_SIZE(cio2_vcm_types)) {
			dev_warn(&adev->dev, "Unknown VCM type %d\n",
				 sensor->ssdb.vcmtype);
			sensor->ssdb.vcmtype = 0;
		}

		status = acpi_get_physical_device_location(adev->handle, &sensor->pld);
		if (ACPI_FAILURE(status)) {
			ret = -ENODEV;
			goto err_put_adev;
		}

		if (sensor->ssdb.lanes > CIO2_MAX_LANES) {
			dev_err(&adev->dev,
				"Number of lanes in SSDB is invalid\n");
			ret = -EINVAL;
			goto err_free_pld;
		}

		cio2_bridge_create_fwnode_properties(sensor, bridge, cfg);
		cio2_bridge_create_connection_swnodes(bridge, sensor);

		ret = software_node_register_node_group(sensor->group);
		if (ret)
			goto err_free_pld;

		fwnode = software_node_fwnode(&sensor->swnodes[
						      SWNODE_SENSOR_HID]);
		if (!fwnode) {
			ret = -ENODEV;
			goto err_free_swnodes;
		}

		sensor->adev = acpi_dev_get(adev);

		primary = acpi_fwnode_handle(adev);
		primary->secondary = fwnode;

		cio2_bridge_instantiate_vcm_i2c_client(sensor);

		dev_info(&cio2->dev, "Found supported sensor %s\n",
			 acpi_dev_name(adev));

		bridge->n_sensors++;
	}

	return 0;

err_free_swnodes:
	software_node_unregister_node_group(sensor->group);
err_free_pld:
	ACPI_FREE(sensor->pld);
err_put_adev:
	acpi_dev_put(adev);
	return ret;
}

static int cio2_bridge_connect_sensors(struct cio2_bridge *bridge,
				       struct pci_dev *cio2)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(cio2_supported_sensors); i++) {
		const struct cio2_sensor_config *cfg =
			&cio2_supported_sensors[i];

		ret = cio2_bridge_connect_sensor(cfg, bridge, cio2);
		if (ret)
			goto err_unregister_sensors;
	}

	return 0;

err_unregister_sensors:
	cio2_bridge_unregister_sensors(bridge);
	return ret;
}

/*
 * The VCM cannot be probed until the PMIC is completely setup. We cannot rely
 * on -EPROBE_DEFER for this, since the consumer<->supplier relations between
 * the VCM and regulators/clks are not described in ACPI, instead they are
 * passed as board-data to the PMIC drivers. Since -PROBE_DEFER does not work
 * for the clks/regulators the VCM i2c-clients must not be instantiated until
 * the PMIC is fully setup.
 *
 * The sensor/VCM ACPI device has an ACPI _DEP on the PMIC, check this using the
 * acpi_dev_ready_for_enumeration() helper, like the i2c-core-acpi code does
 * for the sensors.
 */
static int cio2_bridge_sensors_are_ready(void)
{
	struct acpi_device *adev;
	bool ready = true;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cio2_supported_sensors); i++) {
		const struct cio2_sensor_config *cfg =
			&cio2_supported_sensors[i];

		for_each_acpi_dev_match(adev, cfg->hid, NULL, -1) {
			if (!adev->status.enabled)
				continue;

			if (!acpi_dev_ready_for_enumeration(adev))
				ready = false;
		}
	}

	return ready;
}

int cio2_bridge_init(struct pci_dev *cio2)
{
	struct device *dev = &cio2->dev;
	struct fwnode_handle *fwnode;
	struct cio2_bridge *bridge;
	unsigned int i;
	int ret;

	if (!cio2_bridge_sensors_are_ready())
		return -EPROBE_DEFER;

	bridge = kzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return -ENOMEM;

	strscpy(bridge->cio2_node_name, CIO2_HID,
		sizeof(bridge->cio2_node_name));
	bridge->cio2_hid_node.name = bridge->cio2_node_name;

	ret = software_node_register(&bridge->cio2_hid_node);
	if (ret < 0) {
		dev_err(dev, "Failed to register the CIO2 HID node\n");
		goto err_free_bridge;
	}

	/*
	 * Map the lane arrangement, which is fixed for the IPU3 (meaning we
	 * only need one, rather than one per sensor). We include it as a
	 * member of the struct cio2_bridge rather than a global variable so
	 * that it survives if the module is unloaded along with the rest of
	 * the struct.
	 */
	for (i = 0; i < CIO2_MAX_LANES; i++)
		bridge->data_lanes[i] = i + 1;

	ret = cio2_bridge_connect_sensors(bridge, cio2);
	if (ret || bridge->n_sensors == 0)
		goto err_unregister_cio2;

	dev_info(dev, "Connected %d cameras\n", bridge->n_sensors);

	fwnode = software_node_fwnode(&bridge->cio2_hid_node);
	if (!fwnode) {
		dev_err(dev, "Error getting fwnode from cio2 software_node\n");
		ret = -ENODEV;
		goto err_unregister_sensors;
	}

	set_secondary_fwnode(dev, fwnode);

	return 0;

err_unregister_sensors:
	cio2_bridge_unregister_sensors(bridge);
err_unregister_cio2:
	software_node_unregister(&bridge->cio2_hid_node);
err_free_bridge:
	kfree(bridge);

	return ret;
}
