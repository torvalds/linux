/**
 * Copyright (c) 2014 Redpine Signals Inc.
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

#ifndef __RSI_DEBUGFS_H__
#define __RSI_DEBUGFS_H__

#include "rsi_main.h"
#include <linux/debugfs.h>

#ifndef CONFIG_RSI_DEBUGFS
static inline int rsi_init_dbgfs(struct rsi_hw *adapter)
{
	return 0;
}

static inline void rsi_remove_dbgfs(struct rsi_hw *adapter)
{
	return;
}
#else
struct rsi_dbg_files {
	const char *name;
	umode_t perms;
	const struct file_operations fops;
};

struct rsi_debugfs {
	struct dentry *subdir;
	struct rsi_dbg_ops *dfs_get_ops;
	struct dentry *rsi_files[MAX_DEBUGFS_ENTRIES];
};
int rsi_init_dbgfs(struct rsi_hw *adapter);
void rsi_remove_dbgfs(struct rsi_hw *adapter);
#endif
#endif
