/*
 * Copyright (c) 2012 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <linux/debugfs.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/devcoredump.h>

#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include "core.h"
#include "bus.h"
#include "fweh.h"
#include "debug.h"

int brcmf_debug_create_memdump(struct brcmf_bus *bus, const void *data,
			       size_t len)
{
	void *dump;
	size_t ramsize;
	int err;

	ramsize = brcmf_bus_get_ramsize(bus);
	if (!ramsize)
		return -ENOTSUPP;

	dump = vzalloc(len + ramsize);
	if (!dump)
		return -ENOMEM;

	if (data && len > 0)
		memcpy(dump, data, len);
	err = brcmf_bus_get_memdump(bus, dump + len, ramsize);
	if (err) {
		vfree(dump);
		return err;
	}

	dev_coredumpv(bus->dev, dump, len + ramsize, GFP_KERNEL);

	return 0;
}

struct dentry *brcmf_debugfs_get_devdir(struct brcmf_pub *drvr)
{
	return drvr->wiphy->debugfsdir;
}

int brcmf_debugfs_add_entry(struct brcmf_pub *drvr, const char *fn,
			    int (*read_fn)(struct seq_file *seq, void *data))
{
	struct dentry *e;

	WARN(!drvr->wiphy->debugfsdir, "wiphy not (yet) registered\n");
	e = debugfs_create_devm_seqfile(drvr->bus_if->dev, fn,
					drvr->wiphy->debugfsdir, read_fn);
	return PTR_ERR_OR_ZERO(e);
}
