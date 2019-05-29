/*
 * OLPC-specific OFW device tree support code.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com
 *
 *  Adapted for sparc by David S. Miller davem@davemloft.net
 *  Adapted for x86/OLPC by Andres Salomon <dilinger@queued.net>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_pdt.h>
#include <asm/olpc.h>
#include <asm/olpc_ofw.h>

static phandle __init olpc_dt_getsibling(phandle node)
{
	const void *args[] = { (void *)node };
	void *res[] = { &node };

	if ((s32)node == -1)
		return 0;

	if (olpc_ofw("peer", args, res) || (s32)node == -1)
		return 0;

	return node;
}

static phandle __init olpc_dt_getchild(phandle node)
{
	const void *args[] = { (void *)node };
	void *res[] = { &node };

	if ((s32)node == -1)
		return 0;

	if (olpc_ofw("child", args, res) || (s32)node == -1) {
		pr_err("PROM: %s: fetching child failed!\n", __func__);
		return 0;
	}

	return node;
}

static int __init olpc_dt_getproplen(phandle node, const char *prop)
{
	const void *args[] = { (void *)node, prop };
	int len;
	void *res[] = { &len };

	if ((s32)node == -1)
		return -1;

	if (olpc_ofw("getproplen", args, res)) {
		pr_err("PROM: %s: getproplen failed!\n", __func__);
		return -1;
	}

	return len;
}

static int __init olpc_dt_getproperty(phandle node, const char *prop,
		char *buf, int bufsize)
{
	int plen;

	plen = olpc_dt_getproplen(node, prop);
	if (plen > bufsize || plen < 1) {
		return -1;
	} else {
		const void *args[] = { (void *)node, prop, buf, (void *)plen };
		void *res[] = { &plen };

		if (olpc_ofw("getprop", args, res)) {
			pr_err("PROM: %s: getprop failed!\n", __func__);
			return -1;
		}
	}

	return plen;
}

static int __init olpc_dt_nextprop(phandle node, char *prev, char *buf)
{
	const void *args[] = { (void *)node, prev, buf };
	int success;
	void *res[] = { &success };

	buf[0] = '\0';

	if ((s32)node == -1)
		return -1;

	if (olpc_ofw("nextprop", args, res) || success != 1)
		return -1;

	return 0;
}

static int __init olpc_dt_pkg2path(phandle node, char *buf,
		const int buflen, int *len)
{
	const void *args[] = { (void *)node, buf, (void *)buflen };
	void *res[] = { len };

	if ((s32)node == -1)
		return -1;

	if (olpc_ofw("package-to-path", args, res) || *len < 1)
		return -1;

	return 0;
}

static unsigned int prom_early_allocated __initdata;

void * __init prom_early_alloc(unsigned long size)
{
	static u8 *mem;
	static size_t free_mem;
	void *res;

	if (free_mem < size) {
		const size_t chunk_size = max(PAGE_SIZE, size);

		/*
		 * To mimimize the number of allocations, grab at least
		 * PAGE_SIZE of memory (that's an arbitrary choice that's
		 * fast enough on the platforms we care about while minimizing
		 * wasted bootmem) and hand off chunks of it to callers.
		 */
		res = memblock_alloc(chunk_size, SMP_CACHE_BYTES);
		if (!res)
			panic("%s: Failed to allocate %zu bytes\n", __func__,
			      chunk_size);
		BUG_ON(!res);
		prom_early_allocated += chunk_size;
		memset(res, 0, chunk_size);
		free_mem = chunk_size;
		mem = res;
	}

	/* allocate from the local cache */
	free_mem -= size;
	res = mem;
	mem += size;
	return res;
}

static struct of_pdt_ops prom_olpc_ops __initdata = {
	.nextprop = olpc_dt_nextprop,
	.getproplen = olpc_dt_getproplen,
	.getproperty = olpc_dt_getproperty,
	.getchild = olpc_dt_getchild,
	.getsibling = olpc_dt_getsibling,
	.pkg2path = olpc_dt_pkg2path,
};

static phandle __init olpc_dt_finddevice(const char *path)
{
	phandle node;
	const void *args[] = { path };
	void *res[] = { &node };

	if (olpc_ofw("finddevice", args, res)) {
		pr_err("olpc_dt: finddevice failed!\n");
		return 0;
	}

	if ((s32) node == -1)
		return 0;

	return node;
}

static int __init olpc_dt_interpret(const char *words)
{
	int result;
	const void *args[] = { words };
	void *res[] = { &result };

	if (olpc_ofw("interpret", args, res)) {
		pr_err("olpc_dt: interpret failed!\n");
		return -1;
	}

	return result;
}

/*
 * Extract board revision directly from OFW device tree.
 * We can't use olpc_platform_info because that hasn't been set up yet.
 */
static u32 __init olpc_dt_get_board_revision(void)
{
	phandle node;
	__be32 rev;
	int r;

	node = olpc_dt_finddevice("/");
	if (!node)
		return 0;

	r = olpc_dt_getproperty(node, "board-revision-int",
				(char *) &rev, sizeof(rev));
	if (r < 0)
		return 0;

	return be32_to_cpu(rev);
}

static int __init olpc_dt_compatible_match(phandle node, const char *compat)
{
	char buf[64], *p;
	int plen, len;

	plen = olpc_dt_getproperty(node, "compatible", buf, sizeof(buf));
	if (plen <= 0)
		return 0;

	len = strlen(compat);
	for (p = buf; p < buf + plen; p += strlen(p) + 1) {
		if (strcmp(p, compat) == 0)
			return 1;
	}

	return 0;
}

void __init olpc_dt_fixup(void)
{
	phandle node;
	u32 board_rev;

	node = olpc_dt_finddevice("/battery@0");
	if (!node)
		return;

	board_rev = olpc_dt_get_board_revision();
	if (!board_rev)
		return;

	if (board_rev >= olpc_board_pre(0xd0)) {
		/* XO-1.5 */

		if (olpc_dt_compatible_match(node, "olpc,xo1.5-battery"))
			return;

		/* Add olpc,xo1.5-battery compatible marker to battery node */
		olpc_dt_interpret("\" /battery@0\" find-device");
		olpc_dt_interpret("  \" olpc,xo1.5-battery\" +compatible");
		olpc_dt_interpret("device-end");

		if (olpc_dt_compatible_match(node, "olpc,xo1-battery")) {
			/*
			 * If we have a olpc,xo1-battery compatible, then we're
			 * running a new enough firmware that already has
			 * the dcon node.
			 */
			return;
		}

		/* Add dcon device */
		olpc_dt_interpret("\" /pci/display@1\" find-device");
		olpc_dt_interpret("  new-device");
		olpc_dt_interpret("    \" dcon\" device-name");
		olpc_dt_interpret("    \" olpc,xo1-dcon\" +compatible");
		olpc_dt_interpret("  finish-device");
		olpc_dt_interpret("device-end");
	} else {
		/* XO-1 */

		if (olpc_dt_compatible_match(node, "olpc,xo1-battery")) {
			/*
			 * If we have a olpc,xo1-battery compatible, then we're
			 * running a new enough firmware that already has
			 * the dcon and RTC nodes.
			 */
			return;
		}

		/* Add dcon device, mark RTC as olpc,xo1-rtc */
		olpc_dt_interpret("\" /pci/display@1,1\" find-device");
		olpc_dt_interpret("  new-device");
		olpc_dt_interpret("    \" dcon\" device-name");
		olpc_dt_interpret("    \" olpc,xo1-dcon\" +compatible");
		olpc_dt_interpret("  finish-device");
		olpc_dt_interpret("device-end");

		olpc_dt_interpret("\" /rtc\" find-device");
		olpc_dt_interpret(" \" olpc,xo1-rtc\" +compatible");
		olpc_dt_interpret("device-end");
	}

	/* Add olpc,xo1-battery compatible marker to battery node */
	olpc_dt_interpret("\" /battery@0\" find-device");
	olpc_dt_interpret("  \" olpc,xo1-battery\" +compatible");
	olpc_dt_interpret("device-end");
}

void __init olpc_dt_build_devicetree(void)
{
	phandle root;

	if (!olpc_ofw_is_installed())
		return;

	olpc_dt_fixup();

	root = olpc_dt_getsibling(0);
	if (!root) {
		pr_err("PROM: unable to get root node from OFW!\n");
		return;
	}
	of_pdt_build_devicetree(root, &prom_olpc_ops);

	pr_info("PROM DT: Built device tree with %u bytes of memory.\n",
			prom_early_allocated);
}
