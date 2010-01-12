/*
 * iwmc3200top - Intel Wireless MultiCom 3200 Top Driver
 * drivers/misc/iwmc3200top/debufs.c
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * Author Name: Maxim Grabarnik <maxim.grabarnink@intel.com>
 *  -
 *
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio.h>
#include <linux/debugfs.h>

#include "iwmc3200top.h"
#include "fw-msg.h"
#include "log.h"
#include "debugfs.h"



/*      Constants definition        */
#define HEXADECIMAL_RADIX	16

/*      Functions definition        */


#define DEBUGFS_ADD(name, parent) do {					\
	dbgfs->dbgfs_##parent##_files.file_##name =			\
	debugfs_create_file(#name, 0644, dbgfs->dir_##parent, priv,	\
				&iwmct_dbgfs_##name##_ops);		\
} while (0)

#define DEBUGFS_RM(name)  do {		\
	debugfs_remove(name);		\
	name = NULL;			\
} while (0)

#define DEBUGFS_READ_FUNC(name)						\
ssize_t iwmct_dbgfs_##name##_read(struct file *file,			\
				  char __user *user_buf,		\
				  size_t count, loff_t *ppos);

#define DEBUGFS_WRITE_FUNC(name)					\
ssize_t iwmct_dbgfs_##name##_write(struct file *file,			\
				   const char __user *user_buf,		\
				   size_t count, loff_t *ppos);

#define DEBUGFS_READ_FILE_OPS(name)					\
	DEBUGFS_READ_FUNC(name)						\
	static const struct file_operations iwmct_dbgfs_##name##_ops = {  \
		.read = iwmct_dbgfs_##name##_read,			\
		.open = iwmct_dbgfs_open_file_generic,			\
	};

#define DEBUGFS_WRITE_FILE_OPS(name)					\
	DEBUGFS_WRITE_FUNC(name)					\
	static const struct file_operations iwmct_dbgfs_##name##_ops = {  \
		.write = iwmct_dbgfs_##name##_write,			\
		.open = iwmct_dbgfs_open_file_generic,			\
	};

#define DEBUGFS_READ_WRITE_FILE_OPS(name)				\
	DEBUGFS_READ_FUNC(name)						\
	DEBUGFS_WRITE_FUNC(name)					\
	static const struct file_operations iwmct_dbgfs_##name##_ops = {\
		.write = iwmct_dbgfs_##name##_write,			\
		.read = iwmct_dbgfs_##name##_read,			\
		.open = iwmct_dbgfs_open_file_generic,			\
	};


/*      Debugfs file ops definitions        */

/*
 * Create the debugfs files and directories
 *
 */
void iwmct_dbgfs_register(struct iwmct_priv *priv, const char *name)
{
	struct iwmct_debugfs *dbgfs;

	dbgfs = kzalloc(sizeof(struct iwmct_debugfs), GFP_KERNEL);
	if (!dbgfs) {
		LOG_ERROR(priv, DEBUGFS, "failed to allocate %zd bytes\n",
					sizeof(struct iwmct_debugfs));
		return;
	}

	priv->dbgfs = dbgfs;
	dbgfs->name = name;
	dbgfs->dir_drv = debugfs_create_dir(name, NULL);
	if (!dbgfs->dir_drv) {
		LOG_ERROR(priv, DEBUGFS, "failed to create debugfs dir\n");
		return;
	}

	return;
}

/**
 * Remove the debugfs files and directories
 *
 */
void iwmct_dbgfs_unregister(struct iwmct_debugfs *dbgfs)
{
	if (!dbgfs)
		return;

	DEBUGFS_RM(dbgfs->dir_drv);
	kfree(dbgfs);
	dbgfs = NULL;
}

