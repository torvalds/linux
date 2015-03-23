/*
 * UFD routines for Wi-Fi Protected Setup
 * Copyright (c) 2009, Masashi Honma <honma@ictec.co.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"
#include "common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>

#include "wps/wps.h"
#include "wps/wps_i.h"

#ifdef CONFIG_NATIVE_WINDOWS
#define UFD_DIR1 "%s\\SMRTNTKY"
#define UFD_DIR2 UFD_DIR1 "\\WFAWSC"
#define UFD_FILE UFD_DIR2 "\\%s"
#else /* CONFIG_NATIVE_WINDOWS */
#define UFD_DIR1 "%s/SMRTNTKY"
#define UFD_DIR2 UFD_DIR1 "/WFAWSC"
#define UFD_FILE UFD_DIR2 "/%s"
#endif /* CONFIG_NATIVE_WINDOWS */


struct wps_ufd_data {
	int ufd_fd;
};


static int dev_pwd_e_file_filter(const struct dirent *entry)
{
	unsigned int prefix;
	char ext[5];

	if (sscanf(entry->d_name, "%8x.%4s", &prefix, ext) != 2)
		return 0;
	if (prefix == 0)
		return 0;
	if (os_strcasecmp(ext, "WFA") != 0)
		return 0;

	return 1;
}


static int wps_get_dev_pwd_e_file_name(char *path, char *file_name)
{
	struct dirent **namelist;
	int i, file_num;

	file_num = scandir(path, &namelist, &dev_pwd_e_file_filter,
			   alphasort);
	if (file_num < 0) {
		wpa_printf(MSG_ERROR, "WPS: OOB file not found: %d (%s)",
			   errno, strerror(errno));
		return -1;
	}
	if (file_num == 0) {
		wpa_printf(MSG_ERROR, "WPS: OOB file not found");
		os_free(namelist);
		return -1;
	}
	os_strlcpy(file_name, namelist[0]->d_name, 13);
	for (i = 0; i < file_num; i++)
		os_free(namelist[i]);
	os_free(namelist);
	return 0;
}


static int get_file_name(struct wps_context *wps, int registrar,
			 const char *path, char *file_name)
{
	switch (wps->oob_conf.oob_method) {
	case OOB_METHOD_CRED:
		os_snprintf(file_name, 13, "00000000.WSC");
		break;
	case OOB_METHOD_DEV_PWD_E:
		if (registrar) {
			char temp[128];
			os_snprintf(temp, sizeof(temp), UFD_DIR2, path);
			if (wps_get_dev_pwd_e_file_name(temp, file_name) < 0)
				return -1;
		} else {
			u8 *mac_addr = wps->dev.mac_addr;

			os_snprintf(file_name, 13, "%02X%02X%02X%02X.WFA",
				    mac_addr[2], mac_addr[3], mac_addr[4],
				    mac_addr[5]);
		}
		break;
	case OOB_METHOD_DEV_PWD_R:
		os_snprintf(file_name, 13, "00000000.WFA");
		break;
	default:
		wpa_printf(MSG_ERROR, "WPS: Invalid USBA OOB method");
		return -1;
	}
	return 0;
}


static int ufd_mkdir(const char *path)
{
	if (mkdir(path, S_IRWXU) < 0 && errno != EEXIST) {
		wpa_printf(MSG_ERROR, "WPS (UFD): Failed to create directory "
			   "'%s': %d (%s)", path, errno, strerror(errno));
		return -1;
	}
	return 0;
}


static void * init_ufd(struct wps_context *wps,
		       struct oob_device_data *oob_dev, int registrar)
{
	int write_f;
	char temp[128];
	char *path = oob_dev->device_path;
	char filename[13];
	struct wps_ufd_data *data;
	int ufd_fd;

	if (path == NULL)
		return NULL;

	write_f = wps->oob_conf.oob_method == OOB_METHOD_DEV_PWD_E ?
		!registrar : registrar;

	if (get_file_name(wps, registrar, path, filename) < 0) {
		wpa_printf(MSG_ERROR, "WPS (UFD): Failed to get file name");
		return NULL;
	}

	if (write_f) {
		os_snprintf(temp, sizeof(temp), UFD_DIR1, path);
		if (ufd_mkdir(temp))
			return NULL;
		os_snprintf(temp, sizeof(temp), UFD_DIR2, path);
		if (ufd_mkdir(temp))
			return NULL;
	}

	os_snprintf(temp, sizeof(temp), UFD_FILE, path, filename);
	if (write_f)
		ufd_fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC,
			      S_IRUSR | S_IWUSR);
	else
		ufd_fd = open(temp, O_RDONLY);
	if (ufd_fd < 0) {
		wpa_printf(MSG_ERROR, "WPS (UFD): Failed to open %s: %s",
			   temp, strerror(errno));
		return NULL;
	}

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	data->ufd_fd = ufd_fd;
	return data;
}


static struct wpabuf * read_ufd(void *priv)
{
	struct wps_ufd_data *data = priv;
	struct wpabuf *buf;
	struct stat s;
	size_t file_size;

	if (fstat(data->ufd_fd, &s) < 0) {
		wpa_printf(MSG_ERROR, "WPS (UFD): Failed to get file size");
		return NULL;
	}

	file_size = s.st_size;
	buf = wpabuf_alloc(file_size);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR, "WPS (UFD): Failed to alloc read "
			   "buffer");
		return NULL;
	}

	if (read(data->ufd_fd, wpabuf_mhead(buf), file_size) !=
	    (int) file_size) {
		wpabuf_free(buf);
		wpa_printf(MSG_ERROR, "WPS (UFD): Failed to read");
		return NULL;
	}
	wpabuf_put(buf, file_size);
	return buf;
}


static int write_ufd(void *priv, struct wpabuf *buf)
{
	struct wps_ufd_data *data = priv;

	if (write(data->ufd_fd, wpabuf_mhead(buf), wpabuf_len(buf)) !=
	    (int) wpabuf_len(buf)) {
		wpa_printf(MSG_ERROR, "WPS (UFD): Failed to write");
		return -1;
	}
	return 0;
}


static void deinit_ufd(void *priv)
{
	struct wps_ufd_data *data = priv;
	close(data->ufd_fd);
	os_free(data);
}


struct oob_device_data oob_ufd_device_data = {
	.device_name	= NULL,
	.device_path	= NULL,
	.init_func	= init_ufd,
	.read_func	= read_ufd,
	.write_func	= write_ufd,
	.deinit_func	= deinit_ufd,
};
