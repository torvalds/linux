// SPDX-License-Identifier: GPL-2.0

#include <linux/device.h>
#include <linux/kernel.h>

#include "common.h"

static int occ_poll(struct occ *occ)
{
	u16 checksum = occ->poll_cmd_data + 1;
	u8 cmd[8];

	/* big endian */
	cmd[0] = 0;			/* sequence number */
	cmd[1] = 0;			/* cmd type */
	cmd[2] = 0;			/* data length msb */
	cmd[3] = 1;			/* data length lsb */
	cmd[4] = occ->poll_cmd_data;	/* data */
	cmd[5] = checksum >> 8;		/* checksum msb */
	cmd[6] = checksum & 0xFF;	/* checksum lsb */
	cmd[7] = 0;

	return occ->send_cmd(occ, cmd);
}

/* only need to do this once at startup, as OCC won't change sensors on us */
static void occ_parse_poll_response(struct occ *occ)
{
	unsigned int i, old_offset, offset = 0, size = 0;
	struct occ_sensor *sensor;
	struct occ_sensors *sensors = &occ->sensors;
	struct occ_response *resp = &occ->resp;
	struct occ_poll_response *poll =
		(struct occ_poll_response *)&resp->data[0];
	struct occ_poll_response_header *header = &poll->header;
	struct occ_sensor_data_block *block = &poll->block;

	dev_info(occ->bus_dev, "OCC found, code level: %.16s\n",
		 header->occ_code_level);

	for (i = 0; i < header->num_sensor_data_blocks; ++i) {
		block = (struct occ_sensor_data_block *)((u8 *)block + offset);
		old_offset = offset;
		offset = (block->header.num_sensors *
			  block->header.sensor_length) + sizeof(block->header);
		size += offset;

		/* validate all the length/size fields */
		if ((size + sizeof(*header)) >= OCC_RESP_DATA_BYTES) {
			dev_warn(occ->bus_dev, "exceeded response buffer\n");
			return;
		}

		dev_dbg(occ->bus_dev, " %04x..%04x: %.4s (%d sensors)\n",
			old_offset, offset - 1, block->header.eye_catcher,
			block->header.num_sensors);

		/* match sensor block type */
		if (strncmp(block->header.eye_catcher, "TEMP", 4) == 0)
			sensor = &sensors->temp;
		else if (strncmp(block->header.eye_catcher, "FREQ", 4) == 0)
			sensor = &sensors->freq;
		else if (strncmp(block->header.eye_catcher, "POWR", 4) == 0)
			sensor = &sensors->power;
		else if (strncmp(block->header.eye_catcher, "CAPS", 4) == 0)
			sensor = &sensors->caps;
		else if (strncmp(block->header.eye_catcher, "EXTN", 4) == 0)
			sensor = &sensors->extended;
		else {
			dev_warn(occ->bus_dev, "sensor not supported %.4s\n",
				 block->header.eye_catcher);
			continue;
		}

		sensor->num_sensors = block->header.num_sensors;
		sensor->version = block->header.sensor_format;
		sensor->data = &block->data;
	}

	dev_dbg(occ->bus_dev, "Max resp size: %u+%zd=%zd\n", size,
		sizeof(*header), size + sizeof(*header));
}

int occ_setup(struct occ *occ, const char *name)
{
	int rc;

	rc = occ_poll(occ);
	if (rc == -ESHUTDOWN) {
		dev_info(occ->bus_dev, "host is not ready\n");
		return rc;
	} else if (rc < 0) {
		dev_err(occ->bus_dev, "failed to get OCC poll response: %d\n",
			rc);
		return rc;
	}

	occ_parse_poll_response(occ);

	return 0;
}
