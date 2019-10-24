// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2012 Broadcom Corporation
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
