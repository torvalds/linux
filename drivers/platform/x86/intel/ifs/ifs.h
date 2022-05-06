/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2022 Intel Corporation. */

#ifndef _IFS_H_
#define _IFS_H_

#include <linux/device.h>
#include <linux/miscdevice.h>

#define MSR_COPY_SCAN_HASHES			0x000002c2
#define MSR_SCAN_HASHES_STATUS			0x000002c3
#define MSR_AUTHENTICATE_AND_COPY_CHUNK		0x000002c4
#define MSR_CHUNKS_AUTHENTICATION_STATUS	0x000002c5

/* MSR_SCAN_HASHES_STATUS bit fields */
union ifs_scan_hashes_status {
	u64	data;
	struct {
		u32	chunk_size	:16;
		u32	num_chunks	:8;
		u32	rsvd1		:8;
		u32	error_code	:8;
		u32	rsvd2		:11;
		u32	max_core_limit	:12;
		u32	valid		:1;
	};
};

/* MSR_CHUNKS_AUTH_STATUS bit fields */
union ifs_chunks_auth_status {
	u64	data;
	struct {
		u32	valid_chunks	:8;
		u32	total_chunks	:8;
		u32	rsvd1		:16;
		u32	error_code	:8;
		u32	rsvd2		:24;
	};
};

/**
 * struct ifs_data - attributes related to intel IFS driver
 * @integrity_cap_bit: MSR_INTEGRITY_CAPS bit enumerating this test
 * @loaded_version: stores the currently loaded ifs image version.
 * @loaded: If a valid test binary has been loaded into the memory
 * @loading_error: Error occurred on another CPU while loading image
 * @valid_chunks: number of chunks which could be validated.
 */
struct ifs_data {
	int	integrity_cap_bit;
	int	loaded_version;
	bool	loaded;
	bool	loading_error;
	int	valid_chunks;
};

struct ifs_work {
	struct work_struct w;
	struct device *dev;
};

struct ifs_device {
	struct ifs_data data;
	struct miscdevice misc;
};

static inline struct ifs_data *ifs_get_data(struct device *dev)
{
	struct miscdevice *m = dev_get_drvdata(dev);
	struct ifs_device *d = container_of(m, struct ifs_device, misc);

	return &d->data;
}

void ifs_load_firmware(struct device *dev);

#endif
