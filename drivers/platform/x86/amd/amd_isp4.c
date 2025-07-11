// SPDX-License-Identifier: GPL-2.0+
/*
 * AMD ISP platform driver for sensor i2-client instantiation
 *
 * Copyright 2025 Advanced Micro Devices, Inc.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/soc/amd/isp4_misc.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/units.h>

#define AMDISP_OV05C10_I2C_ADDR		0x10
#define AMDISP_OV05C10_HID		"OMNI5C10"
#define AMDISP_OV05C10_REMOTE_EP_NAME	"ov05c10_isp_4_1_1"
#define AMD_ISP_PLAT_DRV_NAME		"amd-isp4"

/*
 * AMD ISP platform info definition to initialize sensor
 * specific platform configuration to prepare the amdisp
 * platform.
 */
struct amdisp_platform_info {
	struct i2c_board_info board_info;
	const struct software_node **swnodes;
};

/*
 * AMD ISP platform definition to configure the device properties
 * missing in the ACPI table.
 */
struct amdisp_platform {
	const struct amdisp_platform_info *pinfo;
	struct i2c_board_info board_info;
	struct notifier_block i2c_nb;
	struct i2c_client *i2c_dev;
	struct mutex lock;	/* protects i2c client creation */
};

/* Top-level OV05C10 camera node property table */
static const struct property_entry ov05c10_camera_props[] = {
	PROPERTY_ENTRY_U32("clock-frequency", 24 * HZ_PER_MHZ),
	{ }
};

/* Root AMD ISP OV05C10 camera node definition */
static const struct software_node camera_node = {
	.name = AMDISP_OV05C10_HID,
	.properties = ov05c10_camera_props,
};

/*
 * AMD ISP OV05C10 Ports node definition. No properties defined for
 * ports node for OV05C10.
 */
static const struct software_node ports = {
	.name = "ports",
	.parent = &camera_node,
};

/*
 * AMD ISP OV05C10 Port node definition. No properties defined for
 * port node for OV05C10.
 */
static const struct software_node port_node = {
	.name = "port@",
	.parent = &ports,
};

/*
 * Remote endpoint AMD ISP node definition. No properties defined for
 * remote endpoint node for OV05C10.
 */
static const struct software_node remote_ep_isp_node = {
	.name = AMDISP_OV05C10_REMOTE_EP_NAME,
};

/*
 * Remote endpoint reference for isp node included in the
 * OV05C10 endpoint.
 */
static const struct software_node_ref_args ov05c10_refs[] = {
	SOFTWARE_NODE_REFERENCE(&remote_ep_isp_node),
};

/* OV05C10 supports one single link frequency */
static const u64 ov05c10_link_freqs[] = {
	925 * HZ_PER_MHZ,
};

/* OV05C10 supports only 2-lane configuration */
static const u32 ov05c10_data_lanes[] = {
	1,
	2,
};

/* OV05C10 endpoint node properties table */
static const struct property_entry ov05c10_endpoint_props[] = {
	PROPERTY_ENTRY_U32("bus-type", 4),
	PROPERTY_ENTRY_U32_ARRAY_LEN("data-lanes", ov05c10_data_lanes,
				     ARRAY_SIZE(ov05c10_data_lanes)),
	PROPERTY_ENTRY_U64_ARRAY_LEN("link-frequencies", ov05c10_link_freqs,
				     ARRAY_SIZE(ov05c10_link_freqs)),
	PROPERTY_ENTRY_REF_ARRAY("remote-endpoint", ov05c10_refs),
	{ }
};

/* AMD ISP endpoint node definition */
static const struct software_node endpoint_node = {
	.name = "endpoint",
	.parent = &port_node,
	.properties = ov05c10_endpoint_props,
};

/*
 * AMD ISP swnode graph uses 5 nodes and also its relationship is
 * fixed to align with the structure that v4l2 expects for successful
 * endpoint fwnode parsing.
 *
 * It is only the node property_entries that will vary for each platform
 * supporting different sensor modules.
 */
static const struct software_node *ov05c10_nodes[] = {
	&camera_node,
	&ports,
	&port_node,
	&endpoint_node,
	&remote_ep_isp_node,
	NULL
};

/* OV05C10 specific AMD ISP platform configuration */
static const struct amdisp_platform_info ov05c10_platform_config = {
	.board_info = {
		.dev_name = "ov05c10",
		I2C_BOARD_INFO("ov05c10", AMDISP_OV05C10_I2C_ADDR),
	},
	.swnodes = ov05c10_nodes,
};

static const struct acpi_device_id amdisp_sensor_ids[] = {
	{ AMDISP_OV05C10_HID, (kernel_ulong_t)&ov05c10_platform_config },
	{ }
};
MODULE_DEVICE_TABLE(acpi, amdisp_sensor_ids);

static inline bool is_isp_i2c_adapter(struct i2c_adapter *adap)
{
	return !strcmp(adap->name, AMDISP_I2C_ADAP_NAME);
}

static void instantiate_isp_i2c_client(struct amdisp_platform *isp4_platform,
				       struct i2c_adapter *adap)
{
	struct i2c_board_info *info = &isp4_platform->board_info;
	struct i2c_client *i2c_dev;

	guard(mutex)(&isp4_platform->lock);

	if (isp4_platform->i2c_dev)
		return;

	i2c_dev = i2c_new_client_device(adap, info);
	if (IS_ERR(i2c_dev)) {
		dev_err(&adap->dev, "error %pe registering isp i2c_client\n", i2c_dev);
		return;
	}
	isp4_platform->i2c_dev = i2c_dev;
}

static int isp_i2c_bus_notify(struct notifier_block *nb,
			      unsigned long action, void *data)
{
	struct amdisp_platform *isp4_platform =
		container_of(nb, struct amdisp_platform, i2c_nb);
	struct device *dev = data;
	struct i2c_client *client;
	struct i2c_adapter *adap;

	switch (action) {
	case BUS_NOTIFY_ADD_DEVICE:
		adap = i2c_verify_adapter(dev);
		if (!adap)
			break;
		if (is_isp_i2c_adapter(adap))
			instantiate_isp_i2c_client(isp4_platform, adap);
		break;
	case BUS_NOTIFY_REMOVED_DEVICE:
		client = i2c_verify_client(dev);
		if (!client)
			break;

		scoped_guard(mutex, &isp4_platform->lock) {
			if (isp4_platform->i2c_dev == client) {
				dev_dbg(&client->adapter->dev, "amdisp i2c_client removed\n");
				isp4_platform->i2c_dev = NULL;
			}
		}
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct amdisp_platform *prepare_amdisp_platform(struct device *dev,
						       const struct amdisp_platform_info *src)
{
	struct amdisp_platform *isp4_platform;
	int ret;

	isp4_platform = devm_kzalloc(dev, sizeof(*isp4_platform), GFP_KERNEL);
	if (!isp4_platform)
		return ERR_PTR(-ENOMEM);

	ret = devm_mutex_init(dev, &isp4_platform->lock);
	if (ret)
		return ERR_PTR(ret);

	isp4_platform->board_info.dev_name = src->board_info.dev_name;
	strscpy(isp4_platform->board_info.type, src->board_info.type);
	isp4_platform->board_info.addr = src->board_info.addr;
	isp4_platform->pinfo = src;

	ret = software_node_register_node_group(src->swnodes);
	if (ret)
		return ERR_PTR(ret);

	isp4_platform->board_info.swnode = src->swnodes[0];

	return isp4_platform;
}

static int try_to_instantiate_i2c_client(struct device *dev, void *data)
{
	struct i2c_adapter *adap = i2c_verify_adapter(dev);
	struct amdisp_platform *isp4_platform = data;

	if (!isp4_platform || !adap)
		return 0;
	if (!adap->owner)
		return 0;

	if (is_isp_i2c_adapter(adap))
		instantiate_isp_i2c_client(isp4_platform, adap);

	return 0;
}

static int amd_isp_probe(struct platform_device *pdev)
{
	const struct amdisp_platform_info *pinfo;
	struct amdisp_platform *isp4_platform;
	int ret;

	pinfo = device_get_match_data(&pdev->dev);
	if (!pinfo)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "failed to get valid ACPI data\n");

	isp4_platform = prepare_amdisp_platform(&pdev->dev, pinfo);
	if (IS_ERR(isp4_platform))
		return dev_err_probe(&pdev->dev, PTR_ERR(isp4_platform),
				     "failed to prepare AMD ISP platform fwnode\n");

	isp4_platform->i2c_nb.notifier_call = isp_i2c_bus_notify;
	ret = bus_register_notifier(&i2c_bus_type, &isp4_platform->i2c_nb);
	if (ret)
		goto error_unregister_sw_node;

	/* check if adapter is already registered and create i2c client instance */
	i2c_for_each_dev(isp4_platform, try_to_instantiate_i2c_client);

	platform_set_drvdata(pdev, isp4_platform);
	return 0;

error_unregister_sw_node:
	software_node_unregister_node_group(isp4_platform->pinfo->swnodes);
	return ret;
}

static void amd_isp_remove(struct platform_device *pdev)
{
	struct amdisp_platform *isp4_platform = platform_get_drvdata(pdev);

	bus_unregister_notifier(&i2c_bus_type, &isp4_platform->i2c_nb);
	i2c_unregister_device(isp4_platform->i2c_dev);
	software_node_unregister_node_group(isp4_platform->pinfo->swnodes);
}

static struct platform_driver amd_isp_platform_driver = {
	.driver	= {
		.name			= AMD_ISP_PLAT_DRV_NAME,
		.acpi_match_table	= amdisp_sensor_ids,
	},
	.probe	= amd_isp_probe,
	.remove	= amd_isp_remove,
};

module_platform_driver(amd_isp_platform_driver);

MODULE_AUTHOR("Benjamin Chan <benjamin.chan@amd.com>");
MODULE_AUTHOR("Pratap Nirujogi <pratap.nirujogi@amd.com>");
MODULE_DESCRIPTION("AMD ISP4 Platform Driver");
MODULE_LICENSE("GPL");
