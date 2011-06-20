/*
 *
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include <sysfs/libsysfs.h>

#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <libgen.h>
#include <unistd.h>

#include "usbip_common.h"
#include "utils.h"

int modify_match_busid(char *busid, int add)
{
	int fd;
	int ret;
	char buff[SYSFS_BUS_ID_SIZE + 4];
	char sysfs_mntpath[SYSFS_PATH_MAX];
	char match_busid_path[SYSFS_PATH_MAX];

	ret = sysfs_get_mnt_path(sysfs_mntpath, SYSFS_PATH_MAX);
	if (ret < 0) {
		err("sysfs must be mounted");
		return -1;
	}

	snprintf(match_busid_path, sizeof(match_busid_path),
		 "%s/%s/usb/%s/%s/match_busid", sysfs_mntpath, SYSFS_BUS_NAME,
		 SYSFS_DRIVERS_NAME, USBIP_HOST_DRV_NAME);

	/* BUS_IS_SIZE includes NULL termination? */
	if (strnlen(busid, SYSFS_BUS_ID_SIZE) > SYSFS_BUS_ID_SIZE - 1) {
		dbg("busid is too long");
		return -1;
	}

	fd = open(match_busid_path, O_WRONLY);
	if (fd < 0)
		return -1;

	if (add)
		snprintf(buff, SYSFS_BUS_ID_SIZE + 4, "add %s", busid);
	else
		snprintf(buff, SYSFS_BUS_ID_SIZE + 4, "del %s", busid);

	dbg("write \"%s\" to %s", buff, match_busid_path);

	ret = write(fd, buff, sizeof(buff));
	if (ret < 0) {
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}
