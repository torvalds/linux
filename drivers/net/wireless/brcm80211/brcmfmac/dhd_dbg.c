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
#include <linux/if_ether.h>
#include <linux/if.h>
#include <linux/ieee80211.h>

#include <defs.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include "dhd.h"
#include "dhd_bus.h"

static struct dentry *root_folder;

void brcmf_debugfs_init(void)
{
	root_folder = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (IS_ERR(root_folder))
		root_folder = NULL;
}

void brcmf_debugfs_exit(void)
{
	if (!root_folder)
		return;

	debugfs_remove_recursive(root_folder);
	root_folder = NULL;
}

int brcmf_debugfs_attach(struct brcmf_pub *drvr)
{
	if (!root_folder)
		return -ENODEV;

	drvr->dbgfs_dir = debugfs_create_dir(dev_name(drvr->dev), root_folder);
	return PTR_RET(drvr->dbgfs_dir);
}

void brcmf_debugfs_detach(struct brcmf_pub *drvr)
{
	if (!IS_ERR_OR_NULL(drvr->dbgfs_dir))
		debugfs_remove_recursive(drvr->dbgfs_dir);
}

struct dentry *brcmf_debugfs_get_devdir(struct brcmf_pub *drvr)
{
	return drvr->dbgfs_dir;
}
