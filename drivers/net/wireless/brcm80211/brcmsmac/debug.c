/*
 * Copyright (c) 2012 Broadcom Corporation
 * Copyright (c) 2012 Canonical Ltd.
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
#include <linux/if_ether.h>
#include <linux/if.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/ieee80211.h>
#include <linux/module.h>
#include <net/mac80211.h>

#include <defs.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include "types.h"
#include "main.h"
#include "debug.h"
#include "brcms_trace_events.h"

static struct dentry *root_folder;

void brcms_debugfs_init(void)
{
	root_folder = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (IS_ERR(root_folder))
		root_folder = NULL;
}

void brcms_debugfs_exit(void)
{
	if (!root_folder)
		return;

	debugfs_remove_recursive(root_folder);
	root_folder = NULL;
}

int brcms_debugfs_attach(struct brcms_pub *drvr)
{
	if (!root_folder)
		return -ENODEV;

	drvr->dbgfs_dir = debugfs_create_dir(
		 dev_name(&drvr->wlc->hw->d11core->dev), root_folder);
	return PTR_ERR_OR_ZERO(drvr->dbgfs_dir);
}

void brcms_debugfs_detach(struct brcms_pub *drvr)
{
	if (!IS_ERR_OR_NULL(drvr->dbgfs_dir))
		debugfs_remove_recursive(drvr->dbgfs_dir);
}

struct dentry *brcms_debugfs_get_devdir(struct brcms_pub *drvr)
{
	return drvr->dbgfs_dir;
}

static
ssize_t brcms_debugfs_hardware_read(struct file *f, char __user *data,
					size_t count, loff_t *ppos)
{
	char buf[128];
	int res;
	struct brcms_pub *drvr = f->private_data;

	/* only allow read from start */
	if (*ppos > 0)
		return 0;

	res = scnprintf(buf, sizeof(buf),
		"board vendor: %x\n"
		"board type: %x\n"
		"board revision: %x\n"
		"board flags: %x\n"
		"board flags2: %x\n"
		"firmware revision: %x\n",
		drvr->wlc->hw->d11core->bus->boardinfo.vendor,
		drvr->wlc->hw->d11core->bus->boardinfo.type,
		drvr->wlc->hw->boardrev,
		drvr->wlc->hw->boardflags,
		drvr->wlc->hw->boardflags2,
		drvr->wlc->ucode_rev
		);

	return simple_read_from_buffer(data, count, ppos, buf, res);
}

static const struct file_operations brcms_debugfs_hardware_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = brcms_debugfs_hardware_read
};

void brcms_debugfs_create_files(struct brcms_pub *drvr)
{
	struct dentry *dentry = drvr->dbgfs_dir;

	if (!IS_ERR_OR_NULL(dentry))
		debugfs_create_file("hardware", S_IRUGO, dentry,
				    drvr, &brcms_debugfs_hardware_ops);
}

#define __brcms_fn(fn)						\
void __brcms_ ##fn(struct device *dev, const char *fmt, ...)	\
{								\
	struct va_format vaf = {				\
		.fmt = fmt,					\
	};							\
	va_list args;						\
								\
	va_start(args, fmt);					\
	vaf.va = &args;						\
	dev_ ##fn(dev, "%pV", &vaf);				\
	trace_brcms_ ##fn(&vaf);				\
	va_end(args);						\
}

__brcms_fn(info)
__brcms_fn(warn)
__brcms_fn(err)
__brcms_fn(crit)

#if defined(CONFIG_BRCMDBG) || defined(CONFIG_BRCM_TRACING)
void __brcms_dbg(struct device *dev, u32 level, const char *func,
		 const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
#ifdef CONFIG_BRCMDBG
	if ((brcm_msg_level & level) && net_ratelimit())
		dev_err(dev, "%s %pV", func, &vaf);
#endif
	trace_brcms_dbg(level, func, &vaf);
	va_end(args);
}
#endif
