/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
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
 */

#ifndef __IWM_BUS_H__
#define __IWM_BUS_H__

#include "iwm.h"

struct iwm_if_ops {
	int (*enable)(struct iwm_priv *iwm);
	int (*disable)(struct iwm_priv *iwm);
	int (*send_chunk)(struct iwm_priv *iwm, u8* buf, int count);

	void (*debugfs_init)(struct iwm_priv *iwm, struct dentry *parent_dir);
	void (*debugfs_exit)(struct iwm_priv *iwm);

	const char *umac_name;
	const char *calib_lmac_name;
	const char *lmac_name;
};

static inline int iwm_bus_send_chunk(struct iwm_priv *iwm, u8 *buf, int count)
{
	return iwm->bus_ops->send_chunk(iwm, buf, count);
}

static inline int iwm_bus_enable(struct iwm_priv *iwm)
{
	return iwm->bus_ops->enable(iwm);
}

static inline int iwm_bus_disable(struct iwm_priv *iwm)
{
	return iwm->bus_ops->disable(iwm);
}

#endif
