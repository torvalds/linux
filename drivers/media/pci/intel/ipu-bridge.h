/* SPDX-License-Identifier: GPL-2.0 */
/* Author: Dan Scally <djrscally@gmail.com> */
#ifndef __IPU_BRIDGE_H
#define __IPU_BRIDGE_H

#include <linux/property.h>
#include <linux/types.h>

#include "ipu3/ipu3-cio2.h"

struct i2c_client;

#define IPU_HID				"INT343E"
#define IPU_MAX_LANES				4
#define MAX_NUM_LINK_FREQS			3

/* Values are educated guesses as we don't have a spec */
#define IPU_SENSOR_ROTATION_NORMAL		0
#define IPU_SENSOR_ROTATION_INVERTED		1

#define IPU_SENSOR_CONFIG(_HID, _NR, ...)	\
	(const struct ipu_sensor_config) {	\
		.hid = _HID,			\
		.nr_link_freqs = _NR,		\
		.link_freqs = { __VA_ARGS__ }	\
	}

#define NODE_SENSOR(_HID, _PROPS)		\
	(const struct software_node) {		\
		.name = _HID,			\
		.properties = _PROPS,		\
	}

#define NODE_PORT(_PORT, _SENSOR_NODE)		\
	(const struct software_node) {		\
		.name = _PORT,			\
		.parent = _SENSOR_NODE,		\
	}

#define NODE_ENDPOINT(_EP, _PORT, _PROPS)	\
	(const struct software_node) {		\
		.name = _EP,			\
		.parent = _PORT,		\
		.properties = _PROPS,		\
	}

#define NODE_VCM(_TYPE)				\
	(const struct software_node) {		\
		.name = _TYPE,			\
	}

enum ipu_sensor_swnodes {
	SWNODE_SENSOR_HID,
	SWNODE_SENSOR_PORT,
	SWNODE_SENSOR_ENDPOINT,
	SWNODE_IPU_PORT,
	SWNODE_IPU_ENDPOINT,
	/* Must be last because it is optional / maybe empty */
	SWNODE_VCM,
	SWNODE_COUNT
};

/* Data representation as it is in ACPI SSDB buffer */
struct ipu_sensor_ssdb {
	u8 version;
	u8 sku;
	u8 guid_csi2[16];
	u8 devfunction;
	u8 bus;
	u32 dphylinkenfuses;
	u32 clockdiv;
	u8 link;
	u8 lanes;
	u32 csiparams[10];
	u32 maxlanespeed;
	u8 sensorcalibfileidx;
	u8 sensorcalibfileidxInMBZ[3];
	u8 romtype;
	u8 vcmtype;
	u8 platforminfo;
	u8 platformsubinfo;
	u8 flash;
	u8 privacyled;
	u8 degree;
	u8 mipilinkdefined;
	u32 mclkspeed;
	u8 controllogicid;
	u8 reserved1[3];
	u8 mclkport;
	u8 reserved2[13];
} __packed;

struct ipu_property_names {
	char clock_frequency[16];
	char rotation[9];
	char orientation[12];
	char bus_type[9];
	char data_lanes[11];
	char remote_endpoint[16];
	char link_frequencies[17];
};

struct ipu_node_names {
	char port[7];
	char endpoint[11];
	char remote_port[7];
};

struct ipu_sensor_config {
	const char *hid;
	const u8 nr_link_freqs;
	const u64 link_freqs[MAX_NUM_LINK_FREQS];
};

struct ipu_sensor {
	/* append ssdb.link(u8) in "-%u" format as suffix of HID */
	char name[ACPI_ID_LEN + 4];
	struct acpi_device *adev;
	struct i2c_client *vcm_i2c_client;

	/* SWNODE_COUNT + 1 for terminating NULL */
	const struct software_node *group[SWNODE_COUNT + 1];
	struct software_node swnodes[SWNODE_COUNT];
	struct ipu_node_names node_names;

	struct ipu_sensor_ssdb ssdb;
	struct acpi_pld_info *pld;

	struct ipu_property_names prop_names;
	struct property_entry ep_properties[5];
	struct property_entry dev_properties[5];
	struct property_entry ipu_properties[3];
	struct software_node_ref_args local_ref[1];
	struct software_node_ref_args remote_ref[1];
	struct software_node_ref_args vcm_ref[1];
};

struct ipu_bridge {
	char ipu_node_name[ACPI_ID_LEN];
	struct software_node ipu_hid_node;
	u32 data_lanes[4];
	unsigned int n_sensors;
	struct ipu_sensor sensors[CIO2_NUM_PORTS];
};

#if IS_ENABLED(CONFIG_IPU_BRIDGE)
int ipu_bridge_init(struct pci_dev *ipu);
#else
static inline int ipu_bridge_init(struct pci_dev *ipu) { return 0; }
#endif

#endif
