/*
 * Copyright (C) 2011 Google, Inc.
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

#ifndef __LINUX_PERSISTENT_RAM_H__
#define __LINUX_PERSISTENT_RAM_H__

#include <linux/kernel.h>
#include <linux/types.h>

struct persistent_ram_buffer;

struct persistent_ram_zone {
	struct list_head node;
	struct persistent_ram_buffer *buffer;
	size_t buffer_size;
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	char *par_buffer;
	char *par_header;
	struct rs_control *rs_decoder;
	int corrected_bytes;
	int bad_blocks;
#endif
	char *old_log;
	size_t old_log_size;
	size_t old_log_footer_size;
	bool early;
};

int persistent_ram_init_ringbuffer(struct persistent_ram_zone *prz,
	void __iomem *buffer, size_t buffer_size);

int persistent_ram_write(struct persistent_ram_zone *prz, const void *s,
	unsigned int count);

size_t persistent_ram_old_size(struct persistent_ram_zone *prz);
void *persistent_ram_old(struct persistent_ram_zone *prz);
void persistent_ram_free_old(struct persistent_ram_zone *prz);
ssize_t persistent_ram_ecc_string(struct persistent_ram_zone *prz,
	char *str, size_t len);

#endif
