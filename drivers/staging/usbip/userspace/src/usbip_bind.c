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

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

#include "usbip_common.h"
#include "utils.h"
#include "usbip.h"

static const char usbip_bind_usage_string[] =
	"usbip bind <args>\n"
	"    -b, --busid=<busid>    Bind " USBIP_HOST_DRV_NAME ".ko to device "
	"on <busid>\n";

void usbip_bind_usage(void)
{
	printf("usage: %s", usbip_bind_usage_string);
}

static const char unbind_path_format[] = "/sys/bus/usb/devices/%s/driver/unbind";

/* buggy driver may cause dead lock */
static int unbind_interface_busid(char *busid)
{
	char unbind_path[SYSFS_PATH_MAX];
	int fd;
	int ret;

	snprintf(unbind_path, sizeof(unbind_path), unbind_path_format, busid);

	fd = open(unbind_path, O_WRONLY);
	if (fd < 0) {
		dbg("opening unbind_path failed: %d", fd);
		return -1;
	}

	ret = write(fd, busid, strnlen(busid, BUS_ID_SIZE));
	if (ret < 0) {
		dbg("write to unbind_path failed: %d", ret);
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

static int unbind_interface(char *busid, int configvalue, int interface)
{
	char inf_busid[BUS_ID_SIZE];
	dbg("unbinding interface");

	snprintf(inf_busid, BUS_ID_SIZE, "%s:%d.%d", busid, configvalue, interface);

	return unbind_interface_busid(inf_busid);
}

static int unbind(char *busid)
{
	int configvalue = 0;
	int ninterface = 0;
	int devclass = 0;
	int i;
	int failed = 0;

	configvalue = read_bConfigurationValue(busid);
	ninterface  = read_bNumInterfaces(busid);
	devclass  = read_bDeviceClass(busid);

	if (configvalue < 0 || ninterface < 0 || devclass < 0) {
		dbg("read config and ninf value, removed?");
		return -1;
	}

	if (devclass == 0x09) {
		dbg("skip unbinding of hub");
		return -1;
	}

	for (i = 0; i < ninterface; i++) {
		char driver[PATH_MAX];
		int ret;

		memset(&driver, 0, sizeof(driver));

		getdriver(busid, configvalue, i, driver, PATH_MAX-1);

		dbg(" %s:%d.%d	-> %s ", busid, configvalue, i, driver);

		if (!strncmp("none", driver, PATH_MAX))
			continue; /* unbound interface */

#if 0
		if (!strncmp("usbip", driver, PATH_MAX))
			continue; /* already bound to usbip */
#endif

		/* unbinding */
		ret = unbind_interface(busid, configvalue, i);
		if (ret < 0) {
			dbg("unbind driver at %s:%d.%d failed",
			    busid, configvalue, i);
			failed = 1;
		}
	}

	if (failed)
		return -1;
	else
		return 0;
}

static const char bind_path_format[] = "/sys/bus/usb/drivers/%s/bind";

static int bind_interface_busid(char *busid, char *driver)
{
	char bind_path[PATH_MAX];
	int fd;
	int ret;

	snprintf(bind_path, sizeof(bind_path), bind_path_format, driver);

	fd = open(bind_path, O_WRONLY);
	if (fd < 0)
		return -1;

	ret = write(fd, busid, strnlen(busid, BUS_ID_SIZE));
	if (ret < 0) {
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

static int bind_interface(char *busid, int configvalue, int interface, char *driver)
{
	char inf_busid[BUS_ID_SIZE];

	snprintf(inf_busid, BUS_ID_SIZE, "%s:%d.%d", busid, configvalue, interface);

	return bind_interface_busid(inf_busid, driver);
}

/* call at unbound state */
static int bind_to_usbip(char *busid)
{
	int configvalue = 0;
	int ninterface = 0;
	int i;
	int failed = 0;

	configvalue = read_bConfigurationValue(busid);
	ninterface  = read_bNumInterfaces(busid);

	if (configvalue < 0 || ninterface < 0) {
		dbg("read config and ninf value, removed?");
		return -1;
	}

	for (i = 0; i < ninterface; i++) {
		int ret;

		ret = bind_interface(busid, configvalue, i,
				     USBIP_HOST_DRV_NAME);
		if (ret < 0) {
			dbg("bind usbip at %s:%d.%d, failed",
			    busid, configvalue, i);
			failed = 1;
			/* need to contine binding at other interfaces */
		}
	}

	if (failed)
		return -1;
	else
		return 0;
}

static int use_device_by_usbip(char *busid)
{
	int ret;

	ret = unbind(busid);
	if (ret < 0) {
		dbg("unbind drivers of %s, failed", busid);
		return -1;
	}

	ret = modify_match_busid(busid, 1);
	if (ret < 0) {
		dbg("add %s to match_busid, failed", busid);
		return -1;
	}

	ret = bind_to_usbip(busid);
	if (ret < 0) {
		dbg("bind usbip to %s, failed", busid);
		modify_match_busid(busid, 0);
		return -1;
	}

	dbg("bind %s complete!", busid);

	return 0;
}

int usbip_bind(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "busid", required_argument, NULL, 'b' },
		{ NULL, 0, NULL, 0 }
	};
	int opt;
	int ret = -1;

	for (;;) {
		opt = getopt_long(argc, argv, "b:", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'b':
			ret = use_device_by_usbip(optarg);
			goto out;
		default:
			goto err_out;
		}
	}

err_out:
	usbip_bind_usage();
out:
	return ret;
}
