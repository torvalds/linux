/*
 * Copyright (c) 2015 Qualcomm Atheros, Inc.
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

static int wil_fw_copy_crash_dump(struct wil6210_priv *wil, void *dest,
				  u32 size)
{
	int i;
	const struct fw_map *map;
	void *data;
	u32 host_min, dump_size, offset, len;

	if (wil_fw_get_crash_dump_bounds(wil, &dump_size, &host_min)) {
		wil_err(wil, "%s: fail to obtain crash dump size\n", __func__);
		return -EINVAL;
	}

	if (dump_size > size) {
		wil_err(wil, "%s: not enough space for dump. Need %d have %d\n",
			__func__, dump_size, size);
		return -EINVAL;
	}

	/* copy to crash dump area */
	for (i = 0; i < ARRAY_SIZE(fw_mapping); i++) {
		map = &fw_mapping[i];

		data = (void * __force)wil->csr + HOSTADDR(map->host);
		len = map->to - map->from;
		offset = map->host - host_min;

		wil_dbg_misc(wil, "%s() - dump %s, size %d, offset %d\n",
			     __func__, fw_mapping[i].name, len, offset);

		wil_memcpy_fromio_32((void * __force)(dest + offset),
				     (const void __iomem * __force)data, len);
	}

	return 0;
}

void wil_fw_core_dump(struct wil6210_priv *wil)
{
	void *fw_dump_data;
	u32 fw_dump_size;

	if (wil_fw_get_crash_dump_bounds(wil, &fw_dump_size, NULL)) {
		wil_err(wil, "%s: fail to get fw dump size\n", __func__);
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
	wil_info(wil, "%s: fw core dumped, size %d bytes\n", __func__,
		 fw_dump_size);
}
