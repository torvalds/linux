// SPDX-License-Identifier: GPL-2.0

#include <linux/device.h>

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

	return 0;
}
