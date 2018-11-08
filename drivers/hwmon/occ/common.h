/* SPDX-License-Identifier: GPL-2.0 */

#ifndef OCC_COMMON_H
#define OCC_COMMON_H

#include <linux/mutex.h>

struct device;

#define OCC_RESP_DATA_BYTES		4089

/*
 * Same response format for all OCC versions.
 * Allocate the largest possible response.
 */
struct occ_response {
	u8 seq_no;
	u8 cmd_type;
	u8 return_status;
	__be16 data_length;
	u8 data[OCC_RESP_DATA_BYTES];
	__be16 checksum;
} __packed;

struct occ_sensor_data_block_header {
	u8 eye_catcher[4];
	u8 reserved;
	u8 sensor_format;
	u8 sensor_length;
	u8 num_sensors;
} __packed;

struct occ_sensor_data_block {
	struct occ_sensor_data_block_header header;
	u32 data;
} __packed;

struct occ_poll_response_header {
	u8 status;
	u8 ext_status;
	u8 occs_present;
	u8 config_data;
	u8 occ_state;
	u8 mode;
	u8 ips_status;
	u8 error_log_id;
	__be32 error_log_start_address;
	__be16 error_log_length;
	u16 reserved;
	u8 occ_code_level[16];
	u8 eye_catcher[6];
	u8 num_sensor_data_blocks;
	u8 sensor_data_block_header_version;
} __packed;

struct occ_poll_response {
	struct occ_poll_response_header header;
	struct occ_sensor_data_block block;
} __packed;

struct occ_sensor {
	u8 num_sensors;
	u8 version;
	void *data;	/* pointer to sensor data start within response */
};

/*
 * OCC only provides one sensor data block of each type, but any number of
 * sensors within that block.
 */
struct occ_sensors {
	struct occ_sensor temp;
	struct occ_sensor freq;
	struct occ_sensor power;
	struct occ_sensor caps;
	struct occ_sensor extended;
};

struct occ {
	struct device *bus_dev;

	struct occ_response resp;
	struct occ_sensors sensors;

	int powr_sample_time_us;	/* average power sample time */
	u8 poll_cmd_data;		/* to perform OCC poll command */
	int (*send_cmd)(struct occ *occ, u8 *cmd);

	unsigned long last_update;
	struct mutex lock;		/* lock OCC access */
};

int occ_setup(struct occ *occ, const char *name);

#endif /* OCC_COMMON_H */
