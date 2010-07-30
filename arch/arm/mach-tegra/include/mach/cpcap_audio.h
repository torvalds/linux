/*
 * arch/arm/mach-tegra/include/mach/cpcap_audio.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Iliyan Malchev <malchev@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ARCH_ARM_MACH_TEGRA_CPCAP_AUDIO_H_
#define __ARCH_ARM_MACH_TEGRA_CPCAP_AUDIO_H_

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/platform_device.h>

struct cpcap_audio_config_table {
	int reg;
	int val;
	int mask;
	int slave_or;
};

struct cpcap_audio_path {
	const char *name;
	int gpio;
	const struct cpcap_audio_config_table *table;
	int table_len;
};

struct cpcap_audio_platform_data {
	bool master;
	const struct cpcap_audio_path *speaker;
	const struct cpcap_audio_path *headset;
	const struct cpcap_audio_path *mic1;
	const struct cpcap_audio_path *mic2;
};

#endif/*__ARCH_ARM_MACH_TEGRA_CPCAP_AUDIO_H_*/
