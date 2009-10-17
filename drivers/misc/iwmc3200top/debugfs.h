/*
 * iwmc3200top - Intel Wireless MultiCom 3200 Top Driver
 * drivers/misc/iwmc3200top/debufs.h
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

#ifndef __DEBUGFS_H__
#define __DEBUGFS_H__


#ifdef CONFIG_IWMC3200TOP_DEBUGFS

struct iwmct_debugfs {
	const char *name;
	struct dentry *dir_drv;
	struct dir_drv_files {
	} dbgfs_drv_files;
};

void iwmct_dbgfs_register(struct iwmct_priv *priv, const char *name);
void iwmct_dbgfs_unregister(struct iwmct_debugfs *dbgfs);

#else /* CONFIG_IWMC3200TOP_DEBUGFS */

struct iwmct_debugfs;

static inline void
iwmct_dbgfs_register(struct iwmct_priv *priv, const char *name)
{}

static inline void
iwmct_dbgfs_unregister(struct iwmct_debugfs *dbgfs)
{}

#endif /* CONFIG_IWMC3200TOP_DEBUGFS */

#endif /* __DEBUGFS_H__ */

