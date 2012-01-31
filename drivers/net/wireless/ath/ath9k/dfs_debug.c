/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
 * Copyright (c) 2011 Neratec Solutions AG
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

#include <linux/debugfs.h>
#include <linux/export.h>

#include "ath9k.h"
#include "dfs_debug.h"

#define ATH9K_DFS_STAT(s, p) \
	len += snprintf(buf + len, size - len, "%28s : %10u\n", s, \
			sc->debug.stats.dfs_stats.p);

static ssize_t read_file_dfs(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	struct ath9k_hw_version *hw_ver = &sc->sc_ah->hw_version;
	char *buf;
	unsigned int len = 0, size = 8000;
	ssize_t retval = 0;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len += snprintf(buf + len, size - len, "DFS support for "
			"macVersion = 0x%x, macRev = 0x%x: %s\n",
			hw_ver->macVersion, hw_ver->macRev,
			(sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_DFS) ?
					"enabled" : "disabled");
	ATH9K_DFS_STAT("DFS pulses detected     ", pulses_detected);
	ATH9K_DFS_STAT("Datalen discards        ", datalen_discards);
	ATH9K_DFS_STAT("RSSI discards           ", rssi_discards);
	ATH9K_DFS_STAT("BW info discards        ", bwinfo_discards);
	ATH9K_DFS_STAT("Primary channel pulses  ", pri_phy_errors);
	ATH9K_DFS_STAT("Secondary channel pulses", ext_phy_errors);
	ATH9K_DFS_STAT("Dual channel pulses     ", dc_phy_errors);

	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static int ath9k_dfs_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return 0;
}

static const struct file_operations fops_dfs_stats = {
	.read = read_file_dfs,
	.open = ath9k_dfs_debugfs_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath9k_dfs_init_debug(struct ath_softc *sc)
{
	debugfs_create_file("dfs_stats", S_IRUSR,
			    sc->debug.debugfs_phy, sc, &fops_dfs_stats);
}
