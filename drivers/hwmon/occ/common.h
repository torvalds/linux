/* SPDX-License-Identifier: GPL-2.0 */

#ifndef OCC_COMMON_H
#define OCC_COMMON_H

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

struct occ {
	struct device *bus_dev;

	struct occ_response resp;

	u8 poll_cmd_data;		/* to perform OCC poll command */
	int (*send_cmd)(struct occ *occ, u8 *cmd);
};

int occ_setup(struct occ *occ, const char *name);

#endif /* OCC_COMMON_H */
