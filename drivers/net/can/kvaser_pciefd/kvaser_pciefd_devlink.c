// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/* kvaser_pciefd devlink functions
 *
 * Copyright (C) 2025 KVASER AB, Sweden. All rights reserved.
 */
#include "kvaser_pciefd.h"

#include <net/devlink.h>

static int kvaser_pciefd_devlink_info_get(struct devlink *devlink,
					  struct devlink_info_req *req,
					  struct netlink_ext_ack *extack)
{
	struct kvaser_pciefd *pcie = devlink_priv(devlink);
	char buf[] = "xxx.xxx.xxxxx";
	int ret;

	if (pcie->fw_version.major) {
		snprintf(buf, sizeof(buf), "%u.%u.%u",
			 pcie->fw_version.major,
			 pcie->fw_version.minor,
			 pcie->fw_version.build);
		ret = devlink_info_version_running_put(req,
						       DEVLINK_INFO_VERSION_GENERIC_FW,
						       buf);
		if (ret)
			return ret;
	}

	return 0;
}

const struct devlink_ops kvaser_pciefd_devlink_ops = {
	.info_get = kvaser_pciefd_devlink_info_get,
};
