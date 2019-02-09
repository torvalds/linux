/*
 * Copyright (c) 2015,2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "wil6210.h"
#include <linux/devcoredump.h>

static int wil_fw_get_crash_dump_bounds(struct wil6210_priv *wil,
					u32 *out_dump_size, u32 *out_host_min)
{
	int i;
	const struct fw_map *map;
	u32 host_min, host_max, tmp_max;

	if (!out_dump_size)
		return -EINVAL;

	/* calculate the total size of the unpacked crash dump */
	BUILD_BUG_ON(ARRAY_SIZE(fw_mapping) == 0);
	map = &fw_mapping[0];
	host_min = map->host;
	host_max = map->host + (map->to - map->from);

	for (i = 1; i < ARRAY_SIZE(fw_mapping); i++) {
		map = &fw_mapping[i];

		if (!map->crash_dump)
			continue;

		if (map->host < host_min)
			host_min = map->host;

		tmp_max = map->host + (map->to - map->from);
		if (tmp_max > host_max)
			host_max = tmp_max;
	}

	*out_dump_size = host_max - host_min;
	if (out_host_min)
		*out_host_min = host_min;

	return 0;
}

int wil_fw_copy_crash_dump(struct wil6210_priv *wil, void *dest, u32 size)
{
	int i;
	const struct fw_map *map;
	void *data;
	u32 host_min, dump_size, offset, len;

	if (wil_fw_get_crash_dump_bounds(wil, &dump_size, &host_min)) {
		wil_err(wil, "fail to obtain crash dump size\n");
		return -EINVAL;
	}

	if (dump_size > size) {
		wil_err(wil, "not enough space for dump. Need %d have %d\n",
			dump_size, size);
		return -EINVAL;
	}

	set_bit(wil_status_collecting_dumps, wil->status);
	if (test_bit(wil_status_suspending, wil->status) ||
	    test_bit(wil_status_suspended, wil->status) ||
	    test_bit(wil_status_resetting, wil->status)) {
		wil_err(wil, "cannot collect fw dump during suspend/reset\n");
		clear_bit(wil_status_collecting_dumps, wil->status);
		return -EINVAL;
	}

	/* copy to crash dump area */
	for (i = 0; i < ARRAY_SIZE(fw_mapping); i++) {
		map = &fw_mapping[i];

		if (!map->crash_dump)
			continue;

		data = (void * __force)wil->csr + HOSTADDR(map->host);
		len = map->to - map->from;
		offset = map->host - host_min;

		wil_dbg_misc(wil,
			     "fw_copy_crash_dump: - dump %s, size %d, offset %d\n",
			     fw_mapping[i].name, len, offset);

		wil_memcpy_fromio_32((void * __force)(dest + offset),
				     (const void __iomem * __force)data, len);
	}

	clear_bit(wil_status_collecting_dumps, wil->status);

	return 0;
}

void wil_fw_core_dump(struct wil6210_priv *wil)
{
	void *fw_dump_data;
	u32 fw_dump_size;

	if (wil_fw_get_crash_dump_bounds(wil, &fw_dump_size, NULL)) {
		wil_err(wil, "fail to get fw dump size\n");
		return;
	}

	fw_dump_data = vzalloc(fw_dump_size);
	if (!fw_dump_data)
		return;

	if (wil_fw_copy_crash_dump(wil, fw_dump_data, fw_dump_size)) {
		vfree(fw_dump_data);
		return;
	}
	/* fw_dump_data will be free in device coredump release function
	 * after 5 min
	 */
	dev_coredumpv(wil_to_dev(wil), fw_dump_data, fw_dump_size, GFP_KERNEL);
	wil_info(wil, "fw core dumped, size %d bytes\n", fw_dump_size);
}
