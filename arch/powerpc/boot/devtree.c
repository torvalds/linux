/*
 * devtree.c - convenience functions for device tree manipulation
 * Copyright 2007 David Gibson, IBM Corporation.
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 *
 * Authors: David Gibson <david@gibson.dropbear.id.au>
 *	    Scott Wood <scottwood@freescale.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "string.h"
#include "stdio.h"
#include "ops.h"

void dt_fixup_memory(u64 start, u64 size)
{
	void *root, *memory;
	int naddr, nsize, i;
	u32 memreg[4];

	root = finddevice("/");
	if (getprop(root, "#address-cells", &naddr, sizeof(naddr)) < 0)
		naddr = 2;
	if (naddr < 1 || naddr > 2)
		fatal("Can't cope with #address-cells == %d in /\n\r", naddr);

	if (getprop(root, "#size-cells", &nsize, sizeof(nsize)) < 0)
		nsize = 1;
	if (nsize < 1 || nsize > 2)
		fatal("Can't cope with #size-cells == %d in /\n\r", nsize);

	i = 0;
	if (naddr == 2)
		memreg[i++] = start >> 32;
	memreg[i++] = start & 0xffffffff;
	if (nsize == 2)
		memreg[i++] = size >> 32;
	memreg[i++] = size & 0xffffffff;

	memory = finddevice("/memory");
	if (! memory) {
		memory = create_node(NULL, "memory");
		setprop_str(memory, "device_type", "memory");
	}

	printf("Memory <- <0x%x", memreg[0]);
	for (i = 1; i < (naddr + nsize); i++)
		printf(" 0x%x", memreg[i]);
	printf("> (%ldMB)\n\r", (unsigned long)(size >> 20));

	setprop(memory, "reg", memreg, (naddr + nsize)*sizeof(u32));
}

#define MHZ(x)	((x + 500000) / 1000000)

void dt_fixup_cpu_clocks(u32 cpu, u32 tb, u32 bus)
{
	void *devp = NULL;

	printf("CPU clock-frequency <- 0x%x (%dMHz)\n\r", cpu, MHZ(cpu));
	printf("CPU timebase-frequency <- 0x%x (%dMHz)\n\r", tb, MHZ(tb));
	if (bus > 0)
		printf("CPU bus-frequency <- 0x%x (%dMHz)\n\r", bus, MHZ(bus));

	while ((devp = find_node_by_devtype(devp, "cpu"))) {
		setprop_val(devp, "clock-frequency", cpu);
		setprop_val(devp, "timebase-frequency", tb);
		if (bus > 0)
			setprop_val(devp, "bus-frequency", bus);
	}

	timebase_period_ns = 1000000000 / tb;
}

void dt_fixup_clock(const char *path, u32 freq)
{
	void *devp = finddevice(path);

	if (devp) {
		printf("%s: clock-frequency <- %x (%dMHz)\n\r", path, freq, MHZ(freq));
		setprop_val(devp, "clock-frequency", freq);
	}
}

void dt_fixup_mac_address_by_alias(const char *alias, const u8 *addr)
{
	void *devp = find_node_by_alias(alias);

	if (devp) {
		printf("%s: local-mac-address <-"
		       " %02x:%02x:%02x:%02x:%02x:%02x\n\r", alias,
		       addr[0], addr[1], addr[2],
		       addr[3], addr[4], addr[5]);

		setprop(devp, "local-mac-address", addr, 6);
	}
}

void dt_fixup_mac_address(u32 index, const u8 *addr)
{
	void *devp = find_node_by_prop_value(NULL, "linux,network-index",
	                                     (void*)&index, sizeof(index));

	if (devp) {
		printf("ENET%d: local-mac-address <-"
		       " %02x:%02x:%02x:%02x:%02x:%02x\n\r", index,
		       addr[0], addr[1], addr[2],
		       addr[3], addr[4], addr[5]);

		setprop(devp, "local-mac-address", addr, 6);
	}
}

void __dt_fixup_mac_addresses(u32 startindex, ...)
{
	va_list ap;
	u32 index = startindex;
	const u8 *addr;

	va_start(ap, startindex);

	while ((addr = va_arg(ap, const u8 *)))
		dt_fixup_mac_address(index++, addr);

	va_end(ap);
}

#define MAX_ADDR_CELLS 4

void dt_get_reg_format(void *node, u32 *naddr, u32 *nsize)
{
	if (getprop(node, "#address-cells", naddr, 4) != 4)
		*naddr = 2;
	if (getprop(node, "#size-cells", nsize, 4) != 4)
		*nsize = 1;
}

static void copy_val(u32 *dest, u32 *src, int naddr)
{
	int pad = MAX_ADDR_CELLS - naddr;

	memset(dest, 0, pad * 4);
	memcpy(dest + pad, src, naddr * 4);
}

static int sub_reg(u32 *reg, u32 *sub)
{
	int i, borrow = 0;

	for (i = MAX_ADDR_CELLS - 1; i >= 0; i--) {
		int prev_borrow = borrow;
		borrow = reg[i] < sub[i] + prev_borrow;
		reg[i] -= sub[i] + prev_borrow;
	}

	return !borrow;
}

static int add_reg(u32 *reg, u32 *add, int naddr)
{
	int i, carry = 0;

	for (i = MAX_ADDR_CELLS - 1; i >= MAX_ADDR_CELLS - naddr; i--) {
		u64 tmp = (u64)reg[i] + add[i] + carry;
		carry = tmp >> 32;
		reg[i] = (u32)tmp;
	}

	return !carry;
}

/* It is assumed that if the first byte of reg fits in a
 * range, then the whole reg block fits.
 */
static int compare_reg(u32 *reg, u32 *range, u32 *rangesize)
{
	int i;
	u32 end;

	for (i = 0; i < MAX_ADDR_CELLS; i++) {
		if (reg[i] < range[i])
			return 0;
		if (reg[i] > range[i])
			break;
	}

	for (i = 0; i < MAX_ADDR_CELLS; i++) {
		end = range[i] + rangesize[i];

		if (reg[i] < end)
			break;
		if (reg[i] > end)
			return 0;
	}

	return reg[i] != end;
}

/* reg must be MAX_ADDR_CELLS */
static int find_range(u32 *reg, u32 *ranges, int nregaddr,
                      int naddr, int nsize, int buflen)
{
	int nrange = nregaddr + naddr + nsize;
	int i;

	for (i = 0; i + nrange <= buflen; i += nrange) {
		u32 range_addr[MAX_ADDR_CELLS];
		u32 range_size[MAX_ADDR_CELLS];

		copy_val(range_addr, ranges + i, nregaddr);
		copy_val(range_size, ranges + i + nregaddr + naddr, nsize);

		if (compare_reg(reg, range_addr, range_size))
			return i;
	}

	return -1;
}

/* Currently only generic buses without special encodings are supported.
 * In particular, PCI is not supported.  Also, only the beginning of the
 * reg block is tracked; size is ignored except in ranges.
 */
static u32 prop_buf[MAX_PROP_LEN / 4];

static int dt_xlate(void *node, int res, int reglen, unsigned long *addr,
		unsigned long *size)
{
	u32 last_addr[MAX_ADDR_CELLS];
	u32 this_addr[MAX_ADDR_CELLS];
	void *parent;
	u64 ret_addr, ret_size;
	u32 naddr, nsize, prev_naddr, prev_nsize;
	int buflen, offset;

	parent = get_parent(node);
	if (!parent)
		return 0;

	dt_get_reg_format(parent, &naddr, &nsize);

	if (nsize > 2)
		return 0;

	offset = (naddr + nsize) * res;

	if (reglen < offset + naddr + nsize ||
	    MAX_PROP_LEN < (offset + naddr + nsize) * 4)
		return 0;

	copy_val(last_addr, prop_buf + offset, naddr);

	ret_size = prop_buf[offset + naddr];
	if (nsize == 2) {
		ret_size <<= 32;
		ret_size |= prop_buf[offset + naddr + 1];
	}

	for (;;) {
		prev_naddr = naddr;
		prev_nsize = nsize;
		node = parent;

		parent = get_parent(node);
		if (!parent)
			break;

		dt_get_reg_format(parent, &naddr, &nsize);

		buflen = getprop(node, "ranges", prop_buf,
				sizeof(prop_buf));
		if (buflen == 0)
			continue;
		if (buflen < 0 || buflen > sizeof(prop_buf))
			return 0;

		offset = find_range(last_addr, prop_buf, prev_naddr,
		                    naddr, prev_nsize, buflen / 4);

		if (offset < 0)
			return 0;

		copy_val(this_addr, prop_buf + offset, prev_naddr);

		if (!sub_reg(last_addr, this_addr))
			return 0;

		copy_val(this_addr, prop_buf + offset + prev_naddr, naddr);

		if (!add_reg(last_addr, this_addr, naddr))
			return 0;
	}

	if (naddr > 2)
		return 0;

	ret_addr = ((u64)last_addr[2] << 32) | last_addr[3];

	if (sizeof(void *) == 4 &&
	    (ret_addr >= 0x100000000ULL || ret_size > 0x100000000ULL ||
	     ret_addr + ret_size > 0x100000000ULL))
		return 0;

	*addr = ret_addr;
	if (size)
		*size = ret_size;

	return 1;
}

int dt_xlate_reg(void *node, int res, unsigned long *addr, unsigned long *size)
{
	int reglen;

	reglen = getprop(node, "reg", prop_buf, sizeof(prop_buf)) / 4;
	return dt_xlate(node, res, reglen, addr, size);
}

int dt_xlate_addr(void *node, u32 *buf, int buflen, unsigned long *xlated_addr)
{

	if (buflen > sizeof(prop_buf))
		return 0;

	memcpy(prop_buf, buf, buflen);
	return dt_xlate(node, 0, buflen / 4, xlated_addr, NULL);
}

int dt_is_compatible(void *node, const char *compat)
{
	char *buf = (char *)prop_buf;
	int len, pos;

	len = getprop(node, "compatible", buf, MAX_PROP_LEN);
	if (len < 0)
		return 0;

	for (pos = 0; pos < len; pos++) {
		if (!strcmp(buf + pos, compat))
			return 1;

		pos += strnlen(&buf[pos], len - pos);
	}

	return 0;
}

int dt_get_virtual_reg(void *node, void **addr, int nres)
{
	unsigned long xaddr;
	int n;

	n = getprop(node, "virtual-reg", addr, nres * 4);
	if (n > 0)
		return n / 4;

	for (n = 0; n < nres; n++) {
		if (!dt_xlate_reg(node, n, &xaddr, NULL))
			break;

		addr[n] = (void *)xaddr;
	}

	return n;
}

