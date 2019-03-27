/*
 * hostapd / VLAN ioctl API
 * Copyright 2003, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#include <sys/ioctl.h>

#include "utils/common.h"
#include "common/linux_vlan.h"
#include "vlan_util.h"


int vlan_rem(const char *if_name)
{
	int fd;
	struct vlan_ioctl_args if_request;

	wpa_printf(MSG_DEBUG, "VLAN: vlan_rem(%s)", if_name);
	if ((os_strlen(if_name) + 1) > sizeof(if_request.device1)) {
		wpa_printf(MSG_ERROR, "VLAN: Interface name too long: '%s'",
			   if_name);
		return -1;
	}

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: socket(AF_INET,SOCK_STREAM) "
			   "failed: %s", __func__, strerror(errno));
		return -1;
	}

	os_memset(&if_request, 0, sizeof(if_request));

	os_strlcpy(if_request.device1, if_name, sizeof(if_request.device1));
	if_request.cmd = DEL_VLAN_CMD;

	if (ioctl(fd, SIOCSIFVLAN, &if_request) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: DEL_VLAN_CMD failed for %s: "
			   "%s", __func__, if_name, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}


/*
	Add a vlan interface with VLAN ID 'vid' and tagged interface
	'if_name'.

	returns -1 on error
	returns 1 if the interface already exists
	returns 0 otherwise
*/
int vlan_add(const char *if_name, int vid, const char *vlan_if_name)
{
	int fd;
	struct vlan_ioctl_args if_request;

	wpa_printf(MSG_DEBUG, "VLAN: vlan_add(if_name=%s, vid=%d)",
		   if_name, vid);
	ifconfig_up(if_name);

	if ((os_strlen(if_name) + 1) > sizeof(if_request.device1)) {
		wpa_printf(MSG_ERROR, "VLAN: Interface name too long: '%s'",
			   if_name);
		return -1;
	}

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		wpa_printf(MSG_ERROR, "VLAN: %s: socket(AF_INET,SOCK_STREAM) "
			   "failed: %s", __func__, strerror(errno));
		return -1;
	}

	os_memset(&if_request, 0, sizeof(if_request));

	/* Determine if a suitable vlan device already exists. */

	os_snprintf(if_request.device1, sizeof(if_request.device1), "vlan%d",
		    vid);

	if_request.cmd = GET_VLAN_VID_CMD;

	if (ioctl(fd, SIOCSIFVLAN, &if_request) == 0 &&
	    if_request.u.VID == vid) {
		if_request.cmd = GET_VLAN_REALDEV_NAME_CMD;

		if (ioctl(fd, SIOCSIFVLAN, &if_request) == 0 &&
		    os_strncmp(if_request.u.device2, if_name,
			       sizeof(if_request.u.device2)) == 0) {
			close(fd);
			wpa_printf(MSG_DEBUG,
				   "VLAN: vlan_add: if_name %s exists already",
				   if_request.device1);
			return 1;
		}
	}

	/* A suitable vlan device does not already exist, add one. */

	os_memset(&if_request, 0, sizeof(if_request));
	os_strlcpy(if_request.device1, if_name, sizeof(if_request.device1));
	if_request.u.VID = vid;
	if_request.cmd = ADD_VLAN_CMD;

	if (ioctl(fd, SIOCSIFVLAN, &if_request) < 0) {
		wpa_printf(MSG_ERROR,
			   "VLAN: %s: ADD_VLAN_CMD failed for %s: %s",
			   __func__, if_request.device1, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}


int vlan_set_name_type(unsigned int name_type)
{
	int fd;
	struct vlan_ioctl_args if_request;

	wpa_printf(MSG_DEBUG, "VLAN: vlan_set_name_type(name_type=%u)",
		   name_type);
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		wpa_printf(MSG_ERROR,
			   "VLAN: %s: socket(AF_INET,SOCK_STREAM) failed: %s",
			   __func__, strerror(errno));
		return -1;
	}

	os_memset(&if_request, 0, sizeof(if_request));

	if_request.u.name_type = name_type;
	if_request.cmd = SET_VLAN_NAME_TYPE_CMD;
	if (ioctl(fd, SIOCSIFVLAN, &if_request) < 0) {
		wpa_printf(MSG_ERROR,
			   "VLAN: %s: SET_VLAN_NAME_TYPE_CMD name_type=%u failed: %s",
			   __func__, name_type, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}
