/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef DPU_DBG_H_
#define DPU_DBG_H_

#include <stdarg.h>
#include <linux/debugfs.h>
#include <linux/list.h>

enum dpu_dbg_dump_flag {
	DPU_DBG_DUMP_IN_LOG = BIT(0),
	DPU_DBG_DUMP_IN_MEM = BIT(1),
};

#if defined(CONFIG_DEBUG_FS)

/**
 * dpu_dbg_init_dbg_buses - initialize debug bus dumping support for the chipset
 * @hwversion:		Chipset revision
 */
void dpu_dbg_init_dbg_buses(u32 hwversion);

/**
 * dpu_dbg_init - initialize global dpu debug facilities: regdump
 * @dev:		device handle
 * Returns:		0 or -ERROR
 */
int dpu_dbg_init(struct device *dev);

/**
 * dpu_dbg_debugfs_register - register entries at the given debugfs dir
 * @debugfs_root:	debugfs root in which to create dpu debug entries
 * Returns:	0 or -ERROR
 */
int dpu_dbg_debugfs_register(struct dentry *debugfs_root);

/**
 * dpu_dbg_destroy - destroy the global dpu debug facilities
 * Returns:	none
 */
void dpu_dbg_destroy(void);

/**
 * dpu_dbg_dump - trigger dumping of all dpu_dbg facilities
 * @queue_work:	  whether to queue the dumping work to the work_struct
 * @name:	  string indicating origin of dump
 * @dump_dbgbus:  dump the dpu debug bus
 * @dump_vbif_rt: dump the vbif rt bus
 * Returns:	none
 */
void dpu_dbg_dump(bool queue_work, const char *name, bool dump_dbgbus_dpu,
		  bool dump_dbgbus_vbif_rt);

/**
 * dpu_dbg_set_dpu_top_offset - set the target specific offset from mdss base
 *	address of the top registers. Used for accessing debug bus controls.
 * @blk_off: offset from mdss base of the top block
 */
void dpu_dbg_set_dpu_top_offset(u32 blk_off);

#else

static inline void dpu_dbg_init_dbg_buses(u32 hwversion)
{
}

static inline int dpu_dbg_init(struct device *dev)
{
	return 0;
}

static inline int dpu_dbg_debugfs_register(struct dentry *debugfs_root)
{
	return 0;
}

static inline void dpu_dbg_destroy(void)
{
}

static inline void dpu_dbg_dump(bool queue_work, const char *name,
				bool dump_dbgbus_dpu, bool dump_dbgbus_vbif_rt)
{
}

static inline void dpu_dbg_set_dpu_top_offset(u32 blk_off)
{
}

#endif /* defined(CONFIG_DEBUG_FS) */


#endif /* DPU_DBG_H_ */
