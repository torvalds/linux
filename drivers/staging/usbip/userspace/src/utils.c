/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <sysfs/libsysfs.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "usbip_common.h"
#include "utils.h"

int modify_match_busid(char *busid, int add)
{
	char bus_type[] = "usb";
	char attr_name[] = "match_busid";
	char buff[SYSFS_BUS_ID_SIZE + 4];
	char sysfs_mntpath[SYSFS_PATH_MAX];
	char match_busid_attr_path[SYSFS_PATH_MAX];
	struct sysfs_attribute *match_busid_attr;
	int rc, ret = 0;

	if (strnlen(busid, SYSFS_BUS_ID_SIZE) > SYSFS_BUS_ID_SIZE - 1) {
		dbg("busid is too long");
		return -1;
	}

	rc = sysfs_get_mnt_path(sysfs_mntpath, SYSFS_PATH_MAX);
	if (rc < 0) {
		err("sysfs must be mounted: %s", strerror(errno));
		return -1;
	}

	snprintf(match_busid_attr_path, sizeof(match_busid_attr_path),
		 "%s/%s/%s/%s/%s/%s", sysfs_mntpath, SYSFS_BUS_NAME, bus_type,
		 SYSFS_DRIVERS_NAME, USBIP_HOST_DRV_NAME, attr_name);

	match_busid_attr = sysfs_open_attribute(match_busid_attr_path);
	if (!match_busid_attr) {
		dbg("problem getting match_busid attribute: %s",
		    strerror(errno));
		return -1;
	}

	if (add)
		snprintf(buff, SYSFS_BUS_ID_SIZE + 4, "add %s", busid);
	else
		snprintf(buff, SYSFS_BUS_ID_SIZE + 4, "del %s", busid);

	dbg("write \"%s\" to %s", buff, match_busid_attr->path);

	rc = sysfs_write_attribute(match_busid_attr, buff, sizeof(buff));
	if (rc < 0) {
		dbg("failed to write match_busid: %s", strerror(errno));
		ret = -1;
	}

	sysfs_close_attribute(match_busid_attr);

	return ret;
}
