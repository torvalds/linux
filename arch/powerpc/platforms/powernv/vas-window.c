/*
 * Copyright 2016-17 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) "vas: " fmt

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/io.h>

#include "vas.h"

/*
 * Compute the paste address region for the window @window using the
 * ->paste_base_addr and ->paste_win_id_shift we got from device tree.
 */
static void compute_paste_address(struct vas_window *window, u64 *addr, int *len)
{
	int winid;
	u64 base, shift;

	base = window->vinst->paste_base_addr;
	shift = window->vinst->paste_win_id_shift;
	winid = window->winid;

	*addr  = base + (winid << shift);
	if (len)
		*len = PAGE_SIZE;

	pr_debug("Txwin #%d: Paste addr 0x%llx\n", winid, *addr);
}

static inline void get_hvwc_mmio_bar(struct vas_window *window,
			u64 *start, int *len)
{
	u64 pbaddr;

	pbaddr = window->vinst->hvwc_bar_start;
	*start = pbaddr + window->winid * VAS_HVWC_SIZE;
	*len = VAS_HVWC_SIZE;
}

static inline void get_uwc_mmio_bar(struct vas_window *window,
			u64 *start, int *len)
{
	u64 pbaddr;

	pbaddr = window->vinst->uwc_bar_start;
	*start = pbaddr + window->winid * VAS_UWC_SIZE;
	*len = VAS_UWC_SIZE;
}

/*
 * Map the paste bus address of the given send window into kernel address
 * space. Unlike MMIO regions (map_mmio_region() below), paste region must
 * be mapped cache-able and is only applicable to send windows.
 */
void *map_paste_region(struct vas_window *txwin)
{
	int len;
	void *map;
	char *name;
	u64 start;

	name = kasprintf(GFP_KERNEL, "window-v%d-w%d", txwin->vinst->vas_id,
				txwin->winid);
	if (!name)
		goto free_name;

	txwin->paste_addr_name = name;
	compute_paste_address(txwin, &start, &len);

	if (!request_mem_region(start, len, name)) {
		pr_devel("%s(): request_mem_region(0x%llx, %d) failed\n",
				__func__, start, len);
		goto free_name;
	}

	map = ioremap_cache(start, len);
	if (!map) {
		pr_devel("%s(): ioremap_cache(0x%llx, %d) failed\n", __func__,
				start, len);
		goto free_name;
	}

	pr_devel("Mapped paste addr 0x%llx to kaddr 0x%p\n", start, map);
	return map;

free_name:
	kfree(name);
	return ERR_PTR(-ENOMEM);
}


static void *map_mmio_region(char *name, u64 start, int len)
{
	void *map;

	if (!request_mem_region(start, len, name)) {
		pr_devel("%s(): request_mem_region(0x%llx, %d) failed\n",
				__func__, start, len);
		return NULL;
	}

	map = ioremap(start, len);
	if (!map) {
		pr_devel("%s(): ioremap(0x%llx, %d) failed\n", __func__, start,
				len);
		return NULL;
	}

	return map;
}

static void unmap_region(void *addr, u64 start, int len)
{
	iounmap(addr);
	release_mem_region((phys_addr_t)start, len);
}

/*
 * Unmap the paste address region for a window.
 */
void unmap_paste_region(struct vas_window *window)
{
	int len;
	u64 busaddr_start;

	if (window->paste_kaddr) {
		compute_paste_address(window, &busaddr_start, &len);
		unmap_region(window->paste_kaddr, busaddr_start, len);
		window->paste_kaddr = NULL;
		kfree(window->paste_addr_name);
		window->paste_addr_name = NULL;
	}
}

/*
 * Unmap the MMIO regions for a window.
 */
static void unmap_winctx_mmio_bars(struct vas_window *window)
{
	int len;
	u64 busaddr_start;

	if (window->hvwc_map) {
		get_hvwc_mmio_bar(window, &busaddr_start, &len);
		unmap_region(window->hvwc_map, busaddr_start, len);
		window->hvwc_map = NULL;
	}

	if (window->uwc_map) {
		get_uwc_mmio_bar(window, &busaddr_start, &len);
		unmap_region(window->uwc_map, busaddr_start, len);
		window->uwc_map = NULL;
	}
}

/*
 * Find the Hypervisor Window Context (HVWC) MMIO Base Address Region and the
 * OS/User Window Context (UWC) MMIO Base Address Region for the given window.
 * Map these bus addresses and save the mapped kernel addresses in @window.
 */
int map_winctx_mmio_bars(struct vas_window *window)
{
	int len;
	u64 start;

	get_hvwc_mmio_bar(window, &start, &len);
	window->hvwc_map = map_mmio_region("HVWCM_Window", start, len);

	get_uwc_mmio_bar(window, &start, &len);
	window->uwc_map = map_mmio_region("UWCM_Window", start, len);

	if (!window->hvwc_map || !window->uwc_map) {
		unmap_winctx_mmio_bars(window);
		return -1;
	}

	return 0;
}

/* stub for now */
int vas_win_close(struct vas_window *window)
{
	return -1;
}
