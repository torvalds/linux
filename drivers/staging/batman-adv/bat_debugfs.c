/*
 * Copyright (C) 2010 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include <linux/debugfs.h>

#include "main.h"
#include "bat_debugfs.h"
#include "translation-table.h"
#include "originator.h"
#include "hard-interface.h"
#include "vis.h"
#include "icmp_socket.h"

static struct dentry *bat_debugfs;

void debugfs_init(void)
{
	bat_debugfs = debugfs_create_dir(DEBUGFS_BAT_SUBDIR, NULL);
}

void debugfs_destroy(void)
{
	if (bat_debugfs) {
		debugfs_remove_recursive(bat_debugfs);
		bat_debugfs = NULL;
	}
}

int debugfs_add_meshif(struct net_device *dev)
{
	struct bat_priv *bat_priv = netdev_priv(dev);

	if (!bat_debugfs)
		goto out;

	bat_priv->debug_dir = debugfs_create_dir(dev->name, bat_debugfs);
	if (!bat_priv->debug_dir)
		goto out;

	bat_socket_setup(bat_priv);

	return 0;
out:
#ifdef CONFIG_DEBUG_FS
	return -ENOMEM;
#else
	return 0;
#endif /* CONFIG_DEBUG_FS */
}

void debugfs_del_meshif(struct net_device *dev)
{
	struct bat_priv *bat_priv = netdev_priv(dev);

	if (bat_debugfs) {
		debugfs_remove_recursive(bat_priv->debug_dir);
		bat_priv->debug_dir = NULL;
	}
}
