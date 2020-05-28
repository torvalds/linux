/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "../../include/linux/atomisp.h"
#include "ia_css_version.h"
#include "ia_css_version_data.h"
#include "ia_css_err.h"
#include "sh_css_firmware.h"

int
ia_css_get_version(char *version, int max_size) {
	char *css_version;

	if (!atomisp_hw_is_isp2401)
		css_version = ISP2400_CSS_VERSION_STRING;
	else
		css_version = ISP2401_CSS_VERSION_STRING;

	if (max_size <= (int)strlen(css_version) + (int)strlen(sh_css_get_fw_version()) + 5)
		return -EINVAL;
	strcpy(version, css_version);
	strcat(version, "FW:");
	strcat(version, sh_css_get_fw_version());
	strcat(version, "; ");
	return 0;
}
