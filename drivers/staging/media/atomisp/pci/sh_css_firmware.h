/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef _SH_CSS_FIRMWARE_H_
#define _SH_CSS_FIRMWARE_H_

#include <system_local.h>

#include <ia_css_err.h>
#include <ia_css_acc_types.h>

/* This is for the firmware loaded from user space */
struct  sh_css_fw_bi_file_h {
	char version[64];		/* branch tag + week day + time */
	int binary_nr;			/* Number of binaries */
	unsigned int h_size;		/* sizeof(struct sh_css_fw_bi_file_h) */
};

extern struct ia_css_fw_info     sh_css_sp_fw;
extern struct ia_css_blob_descr *sh_css_blob_info;
extern unsigned int sh_css_num_binaries;

char
*sh_css_get_fw_version(void);

struct device;
bool
sh_css_check_firmware_version(struct device *dev, const char *fw_data);

int
sh_css_load_firmware(struct device *dev, const char *fw_data,
		     unsigned int fw_size);

void sh_css_unload_firmware(void);

ia_css_ptr sh_css_load_blob(const unsigned char *blob, unsigned int size);

int
sh_css_load_blob_info(const char *fw, const struct ia_css_fw_info *bi,
		      struct ia_css_blob_descr *bd, unsigned int i);

#endif /* _SH_CSS_FIRMWARE_H_ */
