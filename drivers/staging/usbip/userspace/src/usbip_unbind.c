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

#include <stdio.h>

#include <getopt.h>
#include <unistd.h>

#include "usbip_common.h"
#include "utils.h"
#include "usbip.h"

static const char usbip_unbind_usage_string[] =
	"usbip unbind <args>\n"
	"    -b, --busid=<busid>    Unbind " USBIP_HOST_DRV_NAME ".ko from "
	"device on <busid>\n";

void usbip_unbind_usage(void)
{
	printf("usage: %s", usbip_unbind_usage_string);
}

static int use_device_by_other(char *busid)
{
	int rc;
	int config;

	/* read and write the same config value to kick probing */
	config = read_bConfigurationValue(busid);
	if (config < 0) {
		dbg("read bConfigurationValue of %s, failed", busid);
		return -1;
	}

	rc = modify_match_busid(busid, 0);
	if (rc < 0) {
		dbg("del %s to match_busid, failed", busid);
		return -1;
	}

	rc = write_bConfigurationValue(busid, config);
	if (rc < 0) {
		dbg("read bConfigurationValue of %s, failed", busid);
		return -1;
	}

	info("bind %s to other drivers than usbip, complete!", busid);

	return 0;
}

int usbip_unbind(int argc, char *argv[])
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
			ret = use_device_by_other(optarg);
			goto out;
		default:
			goto err_out;
		}
	}

err_out:
	usbip_unbind_usage();
out:
	return ret;
}
