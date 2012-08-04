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


struct ath_dfs_pool_stats global_dfs_pool_stats = { 0 };

#define ATH9K_DFS_STAT(s, p) \
	len += snprintf(buf + len, size - len, "%28s : %10u\n", s, \
			sc->debug.stats.dfs_stats.p);
#define ATH9K_DFS_POOL_STAT(s, p) \
	len += snprintf(buf + len, size - len, "%28s : %10u\n", s, \
			global_dfs_pool_stats.p);

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
	len += snprintf(buf + len, size - len, "Pulse detector statistics:\n");
	ATH9K_DFS_STAT("pulse events reported   ", pulses_total);
	ATH9K_DFS_STAT("invalid pulse events    ", pulses_no_dfs);
	ATH9K_DFS_STAT("DFS pulses detected     ", pulses_detected);
	ATH9K_DFS_STAT("Datalen discards        ", datalen_discards);
	ATH9K_DFS_STAT("RSSI discards           ", rssi_discards);
	ATH9K_DFS_STAT("BW info discards        ", bwinfo_discards);
	ATH9K_DFS_STAT("Primary channel pulses  ", pri_phy_errors);
	ATH9K_DFS_STAT("Secondary channel pulses", ext_phy_errors);
	ATH9K_DFS_STAT("Dual channel pulses     ", dc_phy_errors);
	len += snprintf(buf + len, size - len, "Radar detector statistics "
			"(current DFS region: %d)\n", sc->dfs_detector->region);
	ATH9K_DFS_STAT("Pulse events processed  ", pulses_processed);
	ATH9K_DFS_STAT("Radars detected         ", radar_detected);
	len += snprintf(buf + len, size - len, "Global Pool statistics:\n");
	ATH9K_DFS_POOL_STAT("Pool references         ", pool_reference);
	ATH9K_DFS_POOL_STAT("Pulses allocated        ", pulse_allocated);
	ATH9K_DFS_POOL_STAT("Pulses alloc error      ", pulse_alloc_error);
	ATH9K_DFS_POOL_STAT("Pulses in use           ", pulse_used);
	ATH9K_DFS_POOL_STAT("Seqs. allocated         ", pseq_allocated);
	ATH9K_DFS_POOL_STAT("Seqs. alloc error       ", pseq_alloc_error);
	ATH9K_DFS_POOL_STAT("Seqs. in use            ", pseq_used);

	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

/* magic number to prevent accidental reset of DFS statistics */
#define DFS_STATS_RESET_MAGIC	0x80000000
static ssize_t write_file_dfs(struct file *file, const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct ath_softc *sc = file->private_data;
	unsigned long val;
	char buf[32];
	ssize_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	if (val == DFS_STATS_RESET_MAGIC)
		memset(&sc->debug.stats.dfs_stats, 0,
		       sizeof(sc->debug.stats.dfs_stats));
	return count;
}

static const struct file_operations fops_dfs_stats = {
	.read = read_file_dfs,
	.write = write_file_dfs,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath9k_dfs_init_debug(struct ath_softc *sc)
{
	debugfs_create_file("dfs_stats", S_IRUSR,
			    sc->debug.debugfs_phy, sc, &fops_dfs_stats);
}
