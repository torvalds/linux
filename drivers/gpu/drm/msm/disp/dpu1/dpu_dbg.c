/* Copyright (c) 2009-2018, The Linux Foundation. All rights reserved.
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

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/dma-buf.h>
#include <linux/slab.h>
#include <linux/list_sort.h>
#include <linux/pm_runtime.h>

#include "dpu_dbg.h"
#include "disp/dpu1/dpu_hw_catalog.h"


#define DEFAULT_DBGBUS_DPU	DPU_DBG_DUMP_IN_MEM
#define DEFAULT_DBGBUS_VBIFRT	DPU_DBG_DUMP_IN_MEM
#define REG_BASE_NAME_LEN	80

#define DBGBUS_FLAGS_DSPP	BIT(0)
#define DBGBUS_DSPP_STATUS	0x34C

#define DBGBUS_NAME_DPU		"dpu"
#define DBGBUS_NAME_VBIF_RT	"vbif_rt"

/* offsets from dpu top address for the debug buses */
#define DBGBUS_SSPP0	0x188
#define DBGBUS_AXI_INTF	0x194
#define DBGBUS_SSPP1	0x298
#define DBGBUS_DSPP	0x348
#define DBGBUS_PERIPH	0x418

#define TEST_MASK(id, tp)	((id << 4) | (tp << 1) | BIT(0))

/* following offsets are with respect to MDP VBIF base for DBG BUS access */
#define MMSS_VBIF_CLKON			0x4
#define MMSS_VBIF_TEST_BUS_OUT_CTRL	0x210
#define MMSS_VBIF_TEST_BUS_OUT		0x230

/* Vbif error info */
#define MMSS_VBIF_PND_ERR		0x190
#define MMSS_VBIF_SRC_ERR		0x194
#define MMSS_VBIF_XIN_HALT_CTRL1	0x204
#define MMSS_VBIF_ERR_INFO		0X1a0
#define MMSS_VBIF_ERR_INFO_1		0x1a4
#define MMSS_VBIF_CLIENT_NUM		14

/**
 * struct dpu_dbg_reg_base - register region base.
 *	may sub-ranges: sub-ranges are used for dumping
 *	or may not have sub-ranges: dumping is base -> max_offset
 * @reg_base_head: head of this node
 * @name: register base name
 * @base: base pointer
 * @off: cached offset of region for manual register dumping
 * @cnt: cached range of region for manual register dumping
 * @max_offset: length of region
 * @buf: buffer used for manual register dumping
 * @buf_len:  buffer length used for manual register dumping
 * @cb: callback for external dump function, null if not defined
 * @cb_ptr: private pointer to callback function
 */
struct dpu_dbg_reg_base {
	struct list_head reg_base_head;
	char name[REG_BASE_NAME_LEN];
	void __iomem *base;
	size_t off;
	size_t cnt;
	size_t max_offset;
	char *buf;
	size_t buf_len;
	void (*cb)(void *ptr);
	void *cb_ptr;
};

struct dpu_debug_bus_entry {
	u32 wr_addr;
	u32 block_id;
	u32 test_id;
	void (*analyzer)(void __iomem *mem_base,
				struct dpu_debug_bus_entry *entry, u32 val);
};

struct vbif_debug_bus_entry {
	u32 disable_bus_addr;
	u32 block_bus_addr;
	u32 bit_offset;
	u32 block_cnt;
	u32 test_pnt_start;
	u32 test_pnt_cnt;
};

struct dpu_dbg_debug_bus_common {
	char *name;
	u32 enable_mask;
	bool include_in_deferred_work;
	u32 flags;
	u32 entries_size;
	u32 *dumped_content;
};

struct dpu_dbg_dpu_debug_bus {
	struct dpu_dbg_debug_bus_common cmn;
	struct dpu_debug_bus_entry *entries;
	u32 top_blk_off;
};

struct dpu_dbg_vbif_debug_bus {
	struct dpu_dbg_debug_bus_common cmn;
	struct vbif_debug_bus_entry *entries;
};

/**
 * struct dpu_dbg_base - global dpu debug base structure
 * @reg_base_list: list of register dumping regions
 * @dev: device pointer
 * @dump_work: work struct for deferring register dump work to separate thread
 * @dbgbus_dpu: debug bus structure for the dpu
 * @dbgbus_vbif_rt: debug bus structure for the realtime vbif
 */
static struct dpu_dbg_base {
	struct list_head reg_base_list;
	struct device *dev;

	struct work_struct dump_work;

	struct dpu_dbg_dpu_debug_bus dbgbus_dpu;
	struct dpu_dbg_vbif_debug_bus dbgbus_vbif_rt;
} dpu_dbg_base;

static void _dpu_debug_bus_xbar_dump(void __iomem *mem_base,
		struct dpu_debug_bus_entry *entry, u32 val)
{
	dev_err(dpu_dbg_base.dev, "xbar 0x%x %d %d 0x%x\n",
			entry->wr_addr, entry->block_id, entry->test_id, val);
}

static void _dpu_debug_bus_lm_dump(void __iomem *mem_base,
		struct dpu_debug_bus_entry *entry, u32 val)
{
	if (!(val & 0xFFF000))
		return;

	dev_err(dpu_dbg_base.dev, "lm 0x%x %d %d 0x%x\n",
			entry->wr_addr, entry->block_id, entry->test_id, val);
}

static void _dpu_debug_bus_ppb0_dump(void __iomem *mem_base,
		struct dpu_debug_bus_entry *entry, u32 val)
{
	if (!(val & BIT(15)))
		return;

	dev_err(dpu_dbg_base.dev, "ppb0 0x%x %d %d 0x%x\n",
			entry->wr_addr, entry->block_id, entry->test_id, val);
}

static void _dpu_debug_bus_ppb1_dump(void __iomem *mem_base,
		struct dpu_debug_bus_entry *entry, u32 val)
{
	if (!(val & BIT(15)))
		return;

	dev_err(dpu_dbg_base.dev, "ppb1 0x%x %d %d 0x%x\n",
			entry->wr_addr, entry->block_id, entry->test_id, val);
}

static struct dpu_debug_bus_entry dbg_bus_dpu_8998[] = {

	/* Unpack 0 sspp 0*/
	{ DBGBUS_SSPP0, 50, 2 },
	{ DBGBUS_SSPP0, 60, 2 },
	{ DBGBUS_SSPP0, 70, 2 },
	{ DBGBUS_SSPP0, 85, 2 },

	/* Upack 0 sspp 1*/
	{ DBGBUS_SSPP1, 50, 2 },
	{ DBGBUS_SSPP1, 60, 2 },
	{ DBGBUS_SSPP1, 70, 2 },
	{ DBGBUS_SSPP1, 85, 2 },

	/* scheduler */
	{ DBGBUS_DSPP, 130, 0 },
	{ DBGBUS_DSPP, 130, 1 },
	{ DBGBUS_DSPP, 130, 2 },
	{ DBGBUS_DSPP, 130, 3 },
	{ DBGBUS_DSPP, 130, 4 },
	{ DBGBUS_DSPP, 130, 5 },

	/* qseed */
	{ DBGBUS_SSPP0, 6, 0},
	{ DBGBUS_SSPP0, 6, 1},
	{ DBGBUS_SSPP0, 26, 0},
	{ DBGBUS_SSPP0, 26, 1},
	{ DBGBUS_SSPP1, 6, 0},
	{ DBGBUS_SSPP1, 6, 1},
	{ DBGBUS_SSPP1, 26, 0},
	{ DBGBUS_SSPP1, 26, 1},

	/* scale */
	{ DBGBUS_SSPP0, 16, 0},
	{ DBGBUS_SSPP0, 16, 1},
	{ DBGBUS_SSPP0, 36, 0},
	{ DBGBUS_SSPP0, 36, 1},
	{ DBGBUS_SSPP1, 16, 0},
	{ DBGBUS_SSPP1, 16, 1},
	{ DBGBUS_SSPP1, 36, 0},
	{ DBGBUS_SSPP1, 36, 1},

	/* fetch sspp0 */

	/* vig 0 */
	{ DBGBUS_SSPP0, 0, 0 },
	{ DBGBUS_SSPP0, 0, 1 },
	{ DBGBUS_SSPP0, 0, 2 },
	{ DBGBUS_SSPP0, 0, 3 },
	{ DBGBUS_SSPP0, 0, 4 },
	{ DBGBUS_SSPP0, 0, 5 },
	{ DBGBUS_SSPP0, 0, 6 },
	{ DBGBUS_SSPP0, 0, 7 },

	{ DBGBUS_SSPP0, 1, 0 },
	{ DBGBUS_SSPP0, 1, 1 },
	{ DBGBUS_SSPP0, 1, 2 },
	{ DBGBUS_SSPP0, 1, 3 },
	{ DBGBUS_SSPP0, 1, 4 },
	{ DBGBUS_SSPP0, 1, 5 },
	{ DBGBUS_SSPP0, 1, 6 },
	{ DBGBUS_SSPP0, 1, 7 },

	{ DBGBUS_SSPP0, 2, 0 },
	{ DBGBUS_SSPP0, 2, 1 },
	{ DBGBUS_SSPP0, 2, 2 },
	{ DBGBUS_SSPP0, 2, 3 },
	{ DBGBUS_SSPP0, 2, 4 },
	{ DBGBUS_SSPP0, 2, 5 },
	{ DBGBUS_SSPP0, 2, 6 },
	{ DBGBUS_SSPP0, 2, 7 },

	{ DBGBUS_SSPP0, 4, 0 },
	{ DBGBUS_SSPP0, 4, 1 },
	{ DBGBUS_SSPP0, 4, 2 },
	{ DBGBUS_SSPP0, 4, 3 },
	{ DBGBUS_SSPP0, 4, 4 },
	{ DBGBUS_SSPP0, 4, 5 },
	{ DBGBUS_SSPP0, 4, 6 },
	{ DBGBUS_SSPP0, 4, 7 },

	{ DBGBUS_SSPP0, 5, 0 },
	{ DBGBUS_SSPP0, 5, 1 },
	{ DBGBUS_SSPP0, 5, 2 },
	{ DBGBUS_SSPP0, 5, 3 },
	{ DBGBUS_SSPP0, 5, 4 },
	{ DBGBUS_SSPP0, 5, 5 },
	{ DBGBUS_SSPP0, 5, 6 },
	{ DBGBUS_SSPP0, 5, 7 },

	/* vig 2 */
	{ DBGBUS_SSPP0, 20, 0 },
	{ DBGBUS_SSPP0, 20, 1 },
	{ DBGBUS_SSPP0, 20, 2 },
	{ DBGBUS_SSPP0, 20, 3 },
	{ DBGBUS_SSPP0, 20, 4 },
	{ DBGBUS_SSPP0, 20, 5 },
	{ DBGBUS_SSPP0, 20, 6 },
	{ DBGBUS_SSPP0, 20, 7 },

	{ DBGBUS_SSPP0, 21, 0 },
	{ DBGBUS_SSPP0, 21, 1 },
	{ DBGBUS_SSPP0, 21, 2 },
	{ DBGBUS_SSPP0, 21, 3 },
	{ DBGBUS_SSPP0, 21, 4 },
	{ DBGBUS_SSPP0, 21, 5 },
	{ DBGBUS_SSPP0, 21, 6 },
	{ DBGBUS_SSPP0, 21, 7 },

	{ DBGBUS_SSPP0, 22, 0 },
	{ DBGBUS_SSPP0, 22, 1 },
	{ DBGBUS_SSPP0, 22, 2 },
	{ DBGBUS_SSPP0, 22, 3 },
	{ DBGBUS_SSPP0, 22, 4 },
	{ DBGBUS_SSPP0, 22, 5 },
	{ DBGBUS_SSPP0, 22, 6 },
	{ DBGBUS_SSPP0, 22, 7 },

	{ DBGBUS_SSPP0, 24, 0 },
	{ DBGBUS_SSPP0, 24, 1 },
	{ DBGBUS_SSPP0, 24, 2 },
	{ DBGBUS_SSPP0, 24, 3 },
	{ DBGBUS_SSPP0, 24, 4 },
	{ DBGBUS_SSPP0, 24, 5 },
	{ DBGBUS_SSPP0, 24, 6 },
	{ DBGBUS_SSPP0, 24, 7 },

	{ DBGBUS_SSPP0, 25, 0 },
	{ DBGBUS_SSPP0, 25, 1 },
	{ DBGBUS_SSPP0, 25, 2 },
	{ DBGBUS_SSPP0, 25, 3 },
	{ DBGBUS_SSPP0, 25, 4 },
	{ DBGBUS_SSPP0, 25, 5 },
	{ DBGBUS_SSPP0, 25, 6 },
	{ DBGBUS_SSPP0, 25, 7 },

	/* dma 2 */
	{ DBGBUS_SSPP0, 30, 0 },
	{ DBGBUS_SSPP0, 30, 1 },
	{ DBGBUS_SSPP0, 30, 2 },
	{ DBGBUS_SSPP0, 30, 3 },
	{ DBGBUS_SSPP0, 30, 4 },
	{ DBGBUS_SSPP0, 30, 5 },
	{ DBGBUS_SSPP0, 30, 6 },
	{ DBGBUS_SSPP0, 30, 7 },

	{ DBGBUS_SSPP0, 31, 0 },
	{ DBGBUS_SSPP0, 31, 1 },
	{ DBGBUS_SSPP0, 31, 2 },
	{ DBGBUS_SSPP0, 31, 3 },
	{ DBGBUS_SSPP0, 31, 4 },
	{ DBGBUS_SSPP0, 31, 5 },
	{ DBGBUS_SSPP0, 31, 6 },
	{ DBGBUS_SSPP0, 31, 7 },

	{ DBGBUS_SSPP0, 32, 0 },
	{ DBGBUS_SSPP0, 32, 1 },
	{ DBGBUS_SSPP0, 32, 2 },
	{ DBGBUS_SSPP0, 32, 3 },
	{ DBGBUS_SSPP0, 32, 4 },
	{ DBGBUS_SSPP0, 32, 5 },
	{ DBGBUS_SSPP0, 32, 6 },
	{ DBGBUS_SSPP0, 32, 7 },

	{ DBGBUS_SSPP0, 33, 0 },
	{ DBGBUS_SSPP0, 33, 1 },
	{ DBGBUS_SSPP0, 33, 2 },
	{ DBGBUS_SSPP0, 33, 3 },
	{ DBGBUS_SSPP0, 33, 4 },
	{ DBGBUS_SSPP0, 33, 5 },
	{ DBGBUS_SSPP0, 33, 6 },
	{ DBGBUS_SSPP0, 33, 7 },

	{ DBGBUS_SSPP0, 34, 0 },
	{ DBGBUS_SSPP0, 34, 1 },
	{ DBGBUS_SSPP0, 34, 2 },
	{ DBGBUS_SSPP0, 34, 3 },
	{ DBGBUS_SSPP0, 34, 4 },
	{ DBGBUS_SSPP0, 34, 5 },
	{ DBGBUS_SSPP0, 34, 6 },
	{ DBGBUS_SSPP0, 34, 7 },

	{ DBGBUS_SSPP0, 35, 0 },
	{ DBGBUS_SSPP0, 35, 1 },
	{ DBGBUS_SSPP0, 35, 2 },
	{ DBGBUS_SSPP0, 35, 3 },

	/* dma 0 */
	{ DBGBUS_SSPP0, 40, 0 },
	{ DBGBUS_SSPP0, 40, 1 },
	{ DBGBUS_SSPP0, 40, 2 },
	{ DBGBUS_SSPP0, 40, 3 },
	{ DBGBUS_SSPP0, 40, 4 },
	{ DBGBUS_SSPP0, 40, 5 },
	{ DBGBUS_SSPP0, 40, 6 },
	{ DBGBUS_SSPP0, 40, 7 },

	{ DBGBUS_SSPP0, 41, 0 },
	{ DBGBUS_SSPP0, 41, 1 },
	{ DBGBUS_SSPP0, 41, 2 },
	{ DBGBUS_SSPP0, 41, 3 },
	{ DBGBUS_SSPP0, 41, 4 },
	{ DBGBUS_SSPP0, 41, 5 },
	{ DBGBUS_SSPP0, 41, 6 },
	{ DBGBUS_SSPP0, 41, 7 },

	{ DBGBUS_SSPP0, 42, 0 },
	{ DBGBUS_SSPP0, 42, 1 },
	{ DBGBUS_SSPP0, 42, 2 },
	{ DBGBUS_SSPP0, 42, 3 },
	{ DBGBUS_SSPP0, 42, 4 },
	{ DBGBUS_SSPP0, 42, 5 },
	{ DBGBUS_SSPP0, 42, 6 },
	{ DBGBUS_SSPP0, 42, 7 },

	{ DBGBUS_SSPP0, 44, 0 },
	{ DBGBUS_SSPP0, 44, 1 },
	{ DBGBUS_SSPP0, 44, 2 },
	{ DBGBUS_SSPP0, 44, 3 },
	{ DBGBUS_SSPP0, 44, 4 },
	{ DBGBUS_SSPP0, 44, 5 },
	{ DBGBUS_SSPP0, 44, 6 },
	{ DBGBUS_SSPP0, 44, 7 },

	{ DBGBUS_SSPP0, 45, 0 },
	{ DBGBUS_SSPP0, 45, 1 },
	{ DBGBUS_SSPP0, 45, 2 },
	{ DBGBUS_SSPP0, 45, 3 },
	{ DBGBUS_SSPP0, 45, 4 },
	{ DBGBUS_SSPP0, 45, 5 },
	{ DBGBUS_SSPP0, 45, 6 },
	{ DBGBUS_SSPP0, 45, 7 },

	/* fetch sspp1 */
	/* vig 1 */
	{ DBGBUS_SSPP1, 0, 0 },
	{ DBGBUS_SSPP1, 0, 1 },
	{ DBGBUS_SSPP1, 0, 2 },
	{ DBGBUS_SSPP1, 0, 3 },
	{ DBGBUS_SSPP1, 0, 4 },
	{ DBGBUS_SSPP1, 0, 5 },
	{ DBGBUS_SSPP1, 0, 6 },
	{ DBGBUS_SSPP1, 0, 7 },

	{ DBGBUS_SSPP1, 1, 0 },
	{ DBGBUS_SSPP1, 1, 1 },
	{ DBGBUS_SSPP1, 1, 2 },
	{ DBGBUS_SSPP1, 1, 3 },
	{ DBGBUS_SSPP1, 1, 4 },
	{ DBGBUS_SSPP1, 1, 5 },
	{ DBGBUS_SSPP1, 1, 6 },
	{ DBGBUS_SSPP1, 1, 7 },

	{ DBGBUS_SSPP1, 2, 0 },
	{ DBGBUS_SSPP1, 2, 1 },
	{ DBGBUS_SSPP1, 2, 2 },
	{ DBGBUS_SSPP1, 2, 3 },
	{ DBGBUS_SSPP1, 2, 4 },
	{ DBGBUS_SSPP1, 2, 5 },
	{ DBGBUS_SSPP1, 2, 6 },
	{ DBGBUS_SSPP1, 2, 7 },

	{ DBGBUS_SSPP1, 4, 0 },
	{ DBGBUS_SSPP1, 4, 1 },
	{ DBGBUS_SSPP1, 4, 2 },
	{ DBGBUS_SSPP1, 4, 3 },
	{ DBGBUS_SSPP1, 4, 4 },
	{ DBGBUS_SSPP1, 4, 5 },
	{ DBGBUS_SSPP1, 4, 6 },
	{ DBGBUS_SSPP1, 4, 7 },

	{ DBGBUS_SSPP1, 5, 0 },
	{ DBGBUS_SSPP1, 5, 1 },
	{ DBGBUS_SSPP1, 5, 2 },
	{ DBGBUS_SSPP1, 5, 3 },
	{ DBGBUS_SSPP1, 5, 4 },
	{ DBGBUS_SSPP1, 5, 5 },
	{ DBGBUS_SSPP1, 5, 6 },
	{ DBGBUS_SSPP1, 5, 7 },

	/* vig 3 */
	{ DBGBUS_SSPP1, 20, 0 },
	{ DBGBUS_SSPP1, 20, 1 },
	{ DBGBUS_SSPP1, 20, 2 },
	{ DBGBUS_SSPP1, 20, 3 },
	{ DBGBUS_SSPP1, 20, 4 },
	{ DBGBUS_SSPP1, 20, 5 },
	{ DBGBUS_SSPP1, 20, 6 },
	{ DBGBUS_SSPP1, 20, 7 },

	{ DBGBUS_SSPP1, 21, 0 },
	{ DBGBUS_SSPP1, 21, 1 },
	{ DBGBUS_SSPP1, 21, 2 },
	{ DBGBUS_SSPP1, 21, 3 },
	{ DBGBUS_SSPP1, 21, 4 },
	{ DBGBUS_SSPP1, 21, 5 },
	{ DBGBUS_SSPP1, 21, 6 },
	{ DBGBUS_SSPP1, 21, 7 },

	{ DBGBUS_SSPP1, 22, 0 },
	{ DBGBUS_SSPP1, 22, 1 },
	{ DBGBUS_SSPP1, 22, 2 },
	{ DBGBUS_SSPP1, 22, 3 },
	{ DBGBUS_SSPP1, 22, 4 },
	{ DBGBUS_SSPP1, 22, 5 },
	{ DBGBUS_SSPP1, 22, 6 },
	{ DBGBUS_SSPP1, 22, 7 },

	{ DBGBUS_SSPP1, 24, 0 },
	{ DBGBUS_SSPP1, 24, 1 },
	{ DBGBUS_SSPP1, 24, 2 },
	{ DBGBUS_SSPP1, 24, 3 },
	{ DBGBUS_SSPP1, 24, 4 },
	{ DBGBUS_SSPP1, 24, 5 },
	{ DBGBUS_SSPP1, 24, 6 },
	{ DBGBUS_SSPP1, 24, 7 },

	{ DBGBUS_SSPP1, 25, 0 },
	{ DBGBUS_SSPP1, 25, 1 },
	{ DBGBUS_SSPP1, 25, 2 },
	{ DBGBUS_SSPP1, 25, 3 },
	{ DBGBUS_SSPP1, 25, 4 },
	{ DBGBUS_SSPP1, 25, 5 },
	{ DBGBUS_SSPP1, 25, 6 },
	{ DBGBUS_SSPP1, 25, 7 },

	/* dma 3 */
	{ DBGBUS_SSPP1, 30, 0 },
	{ DBGBUS_SSPP1, 30, 1 },
	{ DBGBUS_SSPP1, 30, 2 },
	{ DBGBUS_SSPP1, 30, 3 },
	{ DBGBUS_SSPP1, 30, 4 },
	{ DBGBUS_SSPP1, 30, 5 },
	{ DBGBUS_SSPP1, 30, 6 },
	{ DBGBUS_SSPP1, 30, 7 },

	{ DBGBUS_SSPP1, 31, 0 },
	{ DBGBUS_SSPP1, 31, 1 },
	{ DBGBUS_SSPP1, 31, 2 },
	{ DBGBUS_SSPP1, 31, 3 },
	{ DBGBUS_SSPP1, 31, 4 },
	{ DBGBUS_SSPP1, 31, 5 },
	{ DBGBUS_SSPP1, 31, 6 },
	{ DBGBUS_SSPP1, 31, 7 },

	{ DBGBUS_SSPP1, 32, 0 },
	{ DBGBUS_SSPP1, 32, 1 },
	{ DBGBUS_SSPP1, 32, 2 },
	{ DBGBUS_SSPP1, 32, 3 },
	{ DBGBUS_SSPP1, 32, 4 },
	{ DBGBUS_SSPP1, 32, 5 },
	{ DBGBUS_SSPP1, 32, 6 },
	{ DBGBUS_SSPP1, 32, 7 },

	{ DBGBUS_SSPP1, 33, 0 },
	{ DBGBUS_SSPP1, 33, 1 },
	{ DBGBUS_SSPP1, 33, 2 },
	{ DBGBUS_SSPP1, 33, 3 },
	{ DBGBUS_SSPP1, 33, 4 },
	{ DBGBUS_SSPP1, 33, 5 },
	{ DBGBUS_SSPP1, 33, 6 },
	{ DBGBUS_SSPP1, 33, 7 },

	{ DBGBUS_SSPP1, 34, 0 },
	{ DBGBUS_SSPP1, 34, 1 },
	{ DBGBUS_SSPP1, 34, 2 },
	{ DBGBUS_SSPP1, 34, 3 },
	{ DBGBUS_SSPP1, 34, 4 },
	{ DBGBUS_SSPP1, 34, 5 },
	{ DBGBUS_SSPP1, 34, 6 },
	{ DBGBUS_SSPP1, 34, 7 },

	{ DBGBUS_SSPP1, 35, 0 },
	{ DBGBUS_SSPP1, 35, 1 },
	{ DBGBUS_SSPP1, 35, 2 },

	/* dma 1 */
	{ DBGBUS_SSPP1, 40, 0 },
	{ DBGBUS_SSPP1, 40, 1 },
	{ DBGBUS_SSPP1, 40, 2 },
	{ DBGBUS_SSPP1, 40, 3 },
	{ DBGBUS_SSPP1, 40, 4 },
	{ DBGBUS_SSPP1, 40, 5 },
	{ DBGBUS_SSPP1, 40, 6 },
	{ DBGBUS_SSPP1, 40, 7 },

	{ DBGBUS_SSPP1, 41, 0 },
	{ DBGBUS_SSPP1, 41, 1 },
	{ DBGBUS_SSPP1, 41, 2 },
	{ DBGBUS_SSPP1, 41, 3 },
	{ DBGBUS_SSPP1, 41, 4 },
	{ DBGBUS_SSPP1, 41, 5 },
	{ DBGBUS_SSPP1, 41, 6 },
	{ DBGBUS_SSPP1, 41, 7 },

	{ DBGBUS_SSPP1, 42, 0 },
	{ DBGBUS_SSPP1, 42, 1 },
	{ DBGBUS_SSPP1, 42, 2 },
	{ DBGBUS_SSPP1, 42, 3 },
	{ DBGBUS_SSPP1, 42, 4 },
	{ DBGBUS_SSPP1, 42, 5 },
	{ DBGBUS_SSPP1, 42, 6 },
	{ DBGBUS_SSPP1, 42, 7 },

	{ DBGBUS_SSPP1, 44, 0 },
	{ DBGBUS_SSPP1, 44, 1 },
	{ DBGBUS_SSPP1, 44, 2 },
	{ DBGBUS_SSPP1, 44, 3 },
	{ DBGBUS_SSPP1, 44, 4 },
	{ DBGBUS_SSPP1, 44, 5 },
	{ DBGBUS_SSPP1, 44, 6 },
	{ DBGBUS_SSPP1, 44, 7 },

	{ DBGBUS_SSPP1, 45, 0 },
	{ DBGBUS_SSPP1, 45, 1 },
	{ DBGBUS_SSPP1, 45, 2 },
	{ DBGBUS_SSPP1, 45, 3 },
	{ DBGBUS_SSPP1, 45, 4 },
	{ DBGBUS_SSPP1, 45, 5 },
	{ DBGBUS_SSPP1, 45, 6 },
	{ DBGBUS_SSPP1, 45, 7 },

	/* cursor 1 */
	{ DBGBUS_SSPP1, 80, 0 },
	{ DBGBUS_SSPP1, 80, 1 },
	{ DBGBUS_SSPP1, 80, 2 },
	{ DBGBUS_SSPP1, 80, 3 },
	{ DBGBUS_SSPP1, 80, 4 },
	{ DBGBUS_SSPP1, 80, 5 },
	{ DBGBUS_SSPP1, 80, 6 },
	{ DBGBUS_SSPP1, 80, 7 },

	{ DBGBUS_SSPP1, 81, 0 },
	{ DBGBUS_SSPP1, 81, 1 },
	{ DBGBUS_SSPP1, 81, 2 },
	{ DBGBUS_SSPP1, 81, 3 },
	{ DBGBUS_SSPP1, 81, 4 },
	{ DBGBUS_SSPP1, 81, 5 },
	{ DBGBUS_SSPP1, 81, 6 },
	{ DBGBUS_SSPP1, 81, 7 },

	{ DBGBUS_SSPP1, 82, 0 },
	{ DBGBUS_SSPP1, 82, 1 },
	{ DBGBUS_SSPP1, 82, 2 },
	{ DBGBUS_SSPP1, 82, 3 },
	{ DBGBUS_SSPP1, 82, 4 },
	{ DBGBUS_SSPP1, 82, 5 },
	{ DBGBUS_SSPP1, 82, 6 },
	{ DBGBUS_SSPP1, 82, 7 },

	{ DBGBUS_SSPP1, 83, 0 },
	{ DBGBUS_SSPP1, 83, 1 },
	{ DBGBUS_SSPP1, 83, 2 },
	{ DBGBUS_SSPP1, 83, 3 },
	{ DBGBUS_SSPP1, 83, 4 },
	{ DBGBUS_SSPP1, 83, 5 },
	{ DBGBUS_SSPP1, 83, 6 },
	{ DBGBUS_SSPP1, 83, 7 },

	{ DBGBUS_SSPP1, 84, 0 },
	{ DBGBUS_SSPP1, 84, 1 },
	{ DBGBUS_SSPP1, 84, 2 },
	{ DBGBUS_SSPP1, 84, 3 },
	{ DBGBUS_SSPP1, 84, 4 },
	{ DBGBUS_SSPP1, 84, 5 },
	{ DBGBUS_SSPP1, 84, 6 },
	{ DBGBUS_SSPP1, 84, 7 },

	/* dspp */
	{ DBGBUS_DSPP, 13, 0 },
	{ DBGBUS_DSPP, 19, 0 },
	{ DBGBUS_DSPP, 14, 0 },
	{ DBGBUS_DSPP, 14, 1 },
	{ DBGBUS_DSPP, 14, 3 },
	{ DBGBUS_DSPP, 20, 0 },
	{ DBGBUS_DSPP, 20, 1 },
	{ DBGBUS_DSPP, 20, 3 },

	/* ppb_0 */
	{ DBGBUS_DSPP, 31, 0, _dpu_debug_bus_ppb0_dump },
	{ DBGBUS_DSPP, 33, 0, _dpu_debug_bus_ppb0_dump },
	{ DBGBUS_DSPP, 35, 0, _dpu_debug_bus_ppb0_dump },
	{ DBGBUS_DSPP, 42, 0, _dpu_debug_bus_ppb0_dump },

	/* ppb_1 */
	{ DBGBUS_DSPP, 32, 0, _dpu_debug_bus_ppb1_dump },
	{ DBGBUS_DSPP, 34, 0, _dpu_debug_bus_ppb1_dump },
	{ DBGBUS_DSPP, 36, 0, _dpu_debug_bus_ppb1_dump },
	{ DBGBUS_DSPP, 43, 0, _dpu_debug_bus_ppb1_dump },

	/* lm_lut */
	{ DBGBUS_DSPP, 109, 0 },
	{ DBGBUS_DSPP, 105, 0 },
	{ DBGBUS_DSPP, 103, 0 },

	/* tear-check */
	{ DBGBUS_PERIPH, 63, 0 },
	{ DBGBUS_PERIPH, 64, 0 },
	{ DBGBUS_PERIPH, 65, 0 },
	{ DBGBUS_PERIPH, 73, 0 },
	{ DBGBUS_PERIPH, 74, 0 },

	/* crossbar */
	{ DBGBUS_DSPP, 0, 0, _dpu_debug_bus_xbar_dump },

	/* rotator */
	{ DBGBUS_DSPP, 9, 0},

	/* blend */
	/* LM0 */
	{ DBGBUS_DSPP, 63, 0},
	{ DBGBUS_DSPP, 63, 1},
	{ DBGBUS_DSPP, 63, 2},
	{ DBGBUS_DSPP, 63, 3},
	{ DBGBUS_DSPP, 63, 4},
	{ DBGBUS_DSPP, 63, 5},
	{ DBGBUS_DSPP, 63, 6},
	{ DBGBUS_DSPP, 63, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 64, 0},
	{ DBGBUS_DSPP, 64, 1},
	{ DBGBUS_DSPP, 64, 2},
	{ DBGBUS_DSPP, 64, 3},
	{ DBGBUS_DSPP, 64, 4},
	{ DBGBUS_DSPP, 64, 5},
	{ DBGBUS_DSPP, 64, 6},
	{ DBGBUS_DSPP, 64, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 65, 0},
	{ DBGBUS_DSPP, 65, 1},
	{ DBGBUS_DSPP, 65, 2},
	{ DBGBUS_DSPP, 65, 3},
	{ DBGBUS_DSPP, 65, 4},
	{ DBGBUS_DSPP, 65, 5},
	{ DBGBUS_DSPP, 65, 6},
	{ DBGBUS_DSPP, 65, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 66, 0},
	{ DBGBUS_DSPP, 66, 1},
	{ DBGBUS_DSPP, 66, 2},
	{ DBGBUS_DSPP, 66, 3},
	{ DBGBUS_DSPP, 66, 4},
	{ DBGBUS_DSPP, 66, 5},
	{ DBGBUS_DSPP, 66, 6},
	{ DBGBUS_DSPP, 66, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 67, 0},
	{ DBGBUS_DSPP, 67, 1},
	{ DBGBUS_DSPP, 67, 2},
	{ DBGBUS_DSPP, 67, 3},
	{ DBGBUS_DSPP, 67, 4},
	{ DBGBUS_DSPP, 67, 5},
	{ DBGBUS_DSPP, 67, 6},
	{ DBGBUS_DSPP, 67, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 68, 0},
	{ DBGBUS_DSPP, 68, 1},
	{ DBGBUS_DSPP, 68, 2},
	{ DBGBUS_DSPP, 68, 3},
	{ DBGBUS_DSPP, 68, 4},
	{ DBGBUS_DSPP, 68, 5},
	{ DBGBUS_DSPP, 68, 6},
	{ DBGBUS_DSPP, 68, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 69, 0},
	{ DBGBUS_DSPP, 69, 1},
	{ DBGBUS_DSPP, 69, 2},
	{ DBGBUS_DSPP, 69, 3},
	{ DBGBUS_DSPP, 69, 4},
	{ DBGBUS_DSPP, 69, 5},
	{ DBGBUS_DSPP, 69, 6},
	{ DBGBUS_DSPP, 69, 7, _dpu_debug_bus_lm_dump },

	/* LM1 */
	{ DBGBUS_DSPP, 70, 0},
	{ DBGBUS_DSPP, 70, 1},
	{ DBGBUS_DSPP, 70, 2},
	{ DBGBUS_DSPP, 70, 3},
	{ DBGBUS_DSPP, 70, 4},
	{ DBGBUS_DSPP, 70, 5},
	{ DBGBUS_DSPP, 70, 6},
	{ DBGBUS_DSPP, 70, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 71, 0},
	{ DBGBUS_DSPP, 71, 1},
	{ DBGBUS_DSPP, 71, 2},
	{ DBGBUS_DSPP, 71, 3},
	{ DBGBUS_DSPP, 71, 4},
	{ DBGBUS_DSPP, 71, 5},
	{ DBGBUS_DSPP, 71, 6},
	{ DBGBUS_DSPP, 71, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 72, 0},
	{ DBGBUS_DSPP, 72, 1},
	{ DBGBUS_DSPP, 72, 2},
	{ DBGBUS_DSPP, 72, 3},
	{ DBGBUS_DSPP, 72, 4},
	{ DBGBUS_DSPP, 72, 5},
	{ DBGBUS_DSPP, 72, 6},
	{ DBGBUS_DSPP, 72, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 73, 0},
	{ DBGBUS_DSPP, 73, 1},
	{ DBGBUS_DSPP, 73, 2},
	{ DBGBUS_DSPP, 73, 3},
	{ DBGBUS_DSPP, 73, 4},
	{ DBGBUS_DSPP, 73, 5},
	{ DBGBUS_DSPP, 73, 6},
	{ DBGBUS_DSPP, 73, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 74, 0},
	{ DBGBUS_DSPP, 74, 1},
	{ DBGBUS_DSPP, 74, 2},
	{ DBGBUS_DSPP, 74, 3},
	{ DBGBUS_DSPP, 74, 4},
	{ DBGBUS_DSPP, 74, 5},
	{ DBGBUS_DSPP, 74, 6},
	{ DBGBUS_DSPP, 74, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 75, 0},
	{ DBGBUS_DSPP, 75, 1},
	{ DBGBUS_DSPP, 75, 2},
	{ DBGBUS_DSPP, 75, 3},
	{ DBGBUS_DSPP, 75, 4},
	{ DBGBUS_DSPP, 75, 5},
	{ DBGBUS_DSPP, 75, 6},
	{ DBGBUS_DSPP, 75, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 76, 0},
	{ DBGBUS_DSPP, 76, 1},
	{ DBGBUS_DSPP, 76, 2},
	{ DBGBUS_DSPP, 76, 3},
	{ DBGBUS_DSPP, 76, 4},
	{ DBGBUS_DSPP, 76, 5},
	{ DBGBUS_DSPP, 76, 6},
	{ DBGBUS_DSPP, 76, 7, _dpu_debug_bus_lm_dump },

	/* LM2 */
	{ DBGBUS_DSPP, 77, 0},
	{ DBGBUS_DSPP, 77, 1},
	{ DBGBUS_DSPP, 77, 2},
	{ DBGBUS_DSPP, 77, 3},
	{ DBGBUS_DSPP, 77, 4},
	{ DBGBUS_DSPP, 77, 5},
	{ DBGBUS_DSPP, 77, 6},
	{ DBGBUS_DSPP, 77, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 78, 0},
	{ DBGBUS_DSPP, 78, 1},
	{ DBGBUS_DSPP, 78, 2},
	{ DBGBUS_DSPP, 78, 3},
	{ DBGBUS_DSPP, 78, 4},
	{ DBGBUS_DSPP, 78, 5},
	{ DBGBUS_DSPP, 78, 6},
	{ DBGBUS_DSPP, 78, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 79, 0},
	{ DBGBUS_DSPP, 79, 1},
	{ DBGBUS_DSPP, 79, 2},
	{ DBGBUS_DSPP, 79, 3},
	{ DBGBUS_DSPP, 79, 4},
	{ DBGBUS_DSPP, 79, 5},
	{ DBGBUS_DSPP, 79, 6},
	{ DBGBUS_DSPP, 79, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 80, 0},
	{ DBGBUS_DSPP, 80, 1},
	{ DBGBUS_DSPP, 80, 2},
	{ DBGBUS_DSPP, 80, 3},
	{ DBGBUS_DSPP, 80, 4},
	{ DBGBUS_DSPP, 80, 5},
	{ DBGBUS_DSPP, 80, 6},
	{ DBGBUS_DSPP, 80, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 81, 0},
	{ DBGBUS_DSPP, 81, 1},
	{ DBGBUS_DSPP, 81, 2},
	{ DBGBUS_DSPP, 81, 3},
	{ DBGBUS_DSPP, 81, 4},
	{ DBGBUS_DSPP, 81, 5},
	{ DBGBUS_DSPP, 81, 6},
	{ DBGBUS_DSPP, 81, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 82, 0},
	{ DBGBUS_DSPP, 82, 1},
	{ DBGBUS_DSPP, 82, 2},
	{ DBGBUS_DSPP, 82, 3},
	{ DBGBUS_DSPP, 82, 4},
	{ DBGBUS_DSPP, 82, 5},
	{ DBGBUS_DSPP, 82, 6},
	{ DBGBUS_DSPP, 82, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 83, 0},
	{ DBGBUS_DSPP, 83, 1},
	{ DBGBUS_DSPP, 83, 2},
	{ DBGBUS_DSPP, 83, 3},
	{ DBGBUS_DSPP, 83, 4},
	{ DBGBUS_DSPP, 83, 5},
	{ DBGBUS_DSPP, 83, 6},
	{ DBGBUS_DSPP, 83, 7, _dpu_debug_bus_lm_dump },

	/* csc */
	{ DBGBUS_SSPP0, 7, 0},
	{ DBGBUS_SSPP0, 7, 1},
	{ DBGBUS_SSPP0, 27, 0},
	{ DBGBUS_SSPP0, 27, 1},
	{ DBGBUS_SSPP1, 7, 0},
	{ DBGBUS_SSPP1, 7, 1},
	{ DBGBUS_SSPP1, 27, 0},
	{ DBGBUS_SSPP1, 27, 1},

	/* pcc */
	{ DBGBUS_SSPP0, 3,  3},
	{ DBGBUS_SSPP0, 23, 3},
	{ DBGBUS_SSPP0, 33, 3},
	{ DBGBUS_SSPP0, 43, 3},
	{ DBGBUS_SSPP1, 3,  3},
	{ DBGBUS_SSPP1, 23, 3},
	{ DBGBUS_SSPP1, 33, 3},
	{ DBGBUS_SSPP1, 43, 3},

	/* spa */
	{ DBGBUS_SSPP0, 8,  0},
	{ DBGBUS_SSPP0, 28, 0},
	{ DBGBUS_SSPP1, 8,  0},
	{ DBGBUS_SSPP1, 28, 0},
	{ DBGBUS_DSPP, 13, 0},
	{ DBGBUS_DSPP, 19, 0},

	/* igc */
	{ DBGBUS_SSPP0, 9,  0},
	{ DBGBUS_SSPP0, 9,  1},
	{ DBGBUS_SSPP0, 9,  3},
	{ DBGBUS_SSPP0, 29, 0},
	{ DBGBUS_SSPP0, 29, 1},
	{ DBGBUS_SSPP0, 29, 3},
	{ DBGBUS_SSPP0, 17, 0},
	{ DBGBUS_SSPP0, 17, 1},
	{ DBGBUS_SSPP0, 17, 3},
	{ DBGBUS_SSPP0, 37, 0},
	{ DBGBUS_SSPP0, 37, 1},
	{ DBGBUS_SSPP0, 37, 3},
	{ DBGBUS_SSPP0, 46, 0},
	{ DBGBUS_SSPP0, 46, 1},
	{ DBGBUS_SSPP0, 46, 3},

	{ DBGBUS_SSPP1, 9,  0},
	{ DBGBUS_SSPP1, 9,  1},
	{ DBGBUS_SSPP1, 9,  3},
	{ DBGBUS_SSPP1, 29, 0},
	{ DBGBUS_SSPP1, 29, 1},
	{ DBGBUS_SSPP1, 29, 3},
	{ DBGBUS_SSPP1, 17, 0},
	{ DBGBUS_SSPP1, 17, 1},
	{ DBGBUS_SSPP1, 17, 3},
	{ DBGBUS_SSPP1, 37, 0},
	{ DBGBUS_SSPP1, 37, 1},
	{ DBGBUS_SSPP1, 37, 3},
	{ DBGBUS_SSPP1, 46, 0},
	{ DBGBUS_SSPP1, 46, 1},
	{ DBGBUS_SSPP1, 46, 3},

	{ DBGBUS_DSPP, 14, 0},
	{ DBGBUS_DSPP, 14, 1},
	{ DBGBUS_DSPP, 14, 3},
	{ DBGBUS_DSPP, 20, 0},
	{ DBGBUS_DSPP, 20, 1},
	{ DBGBUS_DSPP, 20, 3},

	{ DBGBUS_PERIPH, 60, 0},
};

static struct dpu_debug_bus_entry dbg_bus_dpu_sdm845[] = {

	/* Unpack 0 sspp 0*/
	{ DBGBUS_SSPP0, 50, 2 },
	{ DBGBUS_SSPP0, 60, 2 },
	{ DBGBUS_SSPP0, 70, 2 },

	/* Upack 0 sspp 1*/
	{ DBGBUS_SSPP1, 50, 2 },
	{ DBGBUS_SSPP1, 60, 2 },
	{ DBGBUS_SSPP1, 70, 2 },

	/* scheduler */
	{ DBGBUS_DSPP, 130, 0 },
	{ DBGBUS_DSPP, 130, 1 },
	{ DBGBUS_DSPP, 130, 2 },
	{ DBGBUS_DSPP, 130, 3 },
	{ DBGBUS_DSPP, 130, 4 },
	{ DBGBUS_DSPP, 130, 5 },

	/* qseed */
	{ DBGBUS_SSPP0, 6, 0},
	{ DBGBUS_SSPP0, 6, 1},
	{ DBGBUS_SSPP0, 26, 0},
	{ DBGBUS_SSPP0, 26, 1},
	{ DBGBUS_SSPP1, 6, 0},
	{ DBGBUS_SSPP1, 6, 1},
	{ DBGBUS_SSPP1, 26, 0},
	{ DBGBUS_SSPP1, 26, 1},

	/* scale */
	{ DBGBUS_SSPP0, 16, 0},
	{ DBGBUS_SSPP0, 16, 1},
	{ DBGBUS_SSPP0, 36, 0},
	{ DBGBUS_SSPP0, 36, 1},
	{ DBGBUS_SSPP1, 16, 0},
	{ DBGBUS_SSPP1, 16, 1},
	{ DBGBUS_SSPP1, 36, 0},
	{ DBGBUS_SSPP1, 36, 1},

	/* fetch sspp0 */

	/* vig 0 */
	{ DBGBUS_SSPP0, 0, 0 },
	{ DBGBUS_SSPP0, 0, 1 },
	{ DBGBUS_SSPP0, 0, 2 },
	{ DBGBUS_SSPP0, 0, 3 },
	{ DBGBUS_SSPP0, 0, 4 },
	{ DBGBUS_SSPP0, 0, 5 },
	{ DBGBUS_SSPP0, 0, 6 },
	{ DBGBUS_SSPP0, 0, 7 },

	{ DBGBUS_SSPP0, 1, 0 },
	{ DBGBUS_SSPP0, 1, 1 },
	{ DBGBUS_SSPP0, 1, 2 },
	{ DBGBUS_SSPP0, 1, 3 },
	{ DBGBUS_SSPP0, 1, 4 },
	{ DBGBUS_SSPP0, 1, 5 },
	{ DBGBUS_SSPP0, 1, 6 },
	{ DBGBUS_SSPP0, 1, 7 },

	{ DBGBUS_SSPP0, 2, 0 },
	{ DBGBUS_SSPP0, 2, 1 },
	{ DBGBUS_SSPP0, 2, 2 },
	{ DBGBUS_SSPP0, 2, 3 },
	{ DBGBUS_SSPP0, 2, 4 },
	{ DBGBUS_SSPP0, 2, 5 },
	{ DBGBUS_SSPP0, 2, 6 },
	{ DBGBUS_SSPP0, 2, 7 },

	{ DBGBUS_SSPP0, 4, 0 },
	{ DBGBUS_SSPP0, 4, 1 },
	{ DBGBUS_SSPP0, 4, 2 },
	{ DBGBUS_SSPP0, 4, 3 },
	{ DBGBUS_SSPP0, 4, 4 },
	{ DBGBUS_SSPP0, 4, 5 },
	{ DBGBUS_SSPP0, 4, 6 },
	{ DBGBUS_SSPP0, 4, 7 },

	{ DBGBUS_SSPP0, 5, 0 },
	{ DBGBUS_SSPP0, 5, 1 },
	{ DBGBUS_SSPP0, 5, 2 },
	{ DBGBUS_SSPP0, 5, 3 },
	{ DBGBUS_SSPP0, 5, 4 },
	{ DBGBUS_SSPP0, 5, 5 },
	{ DBGBUS_SSPP0, 5, 6 },
	{ DBGBUS_SSPP0, 5, 7 },

	/* vig 2 */
	{ DBGBUS_SSPP0, 20, 0 },
	{ DBGBUS_SSPP0, 20, 1 },
	{ DBGBUS_SSPP0, 20, 2 },
	{ DBGBUS_SSPP0, 20, 3 },
	{ DBGBUS_SSPP0, 20, 4 },
	{ DBGBUS_SSPP0, 20, 5 },
	{ DBGBUS_SSPP0, 20, 6 },
	{ DBGBUS_SSPP0, 20, 7 },

	{ DBGBUS_SSPP0, 21, 0 },
	{ DBGBUS_SSPP0, 21, 1 },
	{ DBGBUS_SSPP0, 21, 2 },
	{ DBGBUS_SSPP0, 21, 3 },
	{ DBGBUS_SSPP0, 21, 4 },
	{ DBGBUS_SSPP0, 21, 5 },
	{ DBGBUS_SSPP0, 21, 6 },
	{ DBGBUS_SSPP0, 21, 7 },

	{ DBGBUS_SSPP0, 22, 0 },
	{ DBGBUS_SSPP0, 22, 1 },
	{ DBGBUS_SSPP0, 22, 2 },
	{ DBGBUS_SSPP0, 22, 3 },
	{ DBGBUS_SSPP0, 22, 4 },
	{ DBGBUS_SSPP0, 22, 5 },
	{ DBGBUS_SSPP0, 22, 6 },
	{ DBGBUS_SSPP0, 22, 7 },

	{ DBGBUS_SSPP0, 24, 0 },
	{ DBGBUS_SSPP0, 24, 1 },
	{ DBGBUS_SSPP0, 24, 2 },
	{ DBGBUS_SSPP0, 24, 3 },
	{ DBGBUS_SSPP0, 24, 4 },
	{ DBGBUS_SSPP0, 24, 5 },
	{ DBGBUS_SSPP0, 24, 6 },
	{ DBGBUS_SSPP0, 24, 7 },

	{ DBGBUS_SSPP0, 25, 0 },
	{ DBGBUS_SSPP0, 25, 1 },
	{ DBGBUS_SSPP0, 25, 2 },
	{ DBGBUS_SSPP0, 25, 3 },
	{ DBGBUS_SSPP0, 25, 4 },
	{ DBGBUS_SSPP0, 25, 5 },
	{ DBGBUS_SSPP0, 25, 6 },
	{ DBGBUS_SSPP0, 25, 7 },

	/* dma 2 */
	{ DBGBUS_SSPP0, 30, 0 },
	{ DBGBUS_SSPP0, 30, 1 },
	{ DBGBUS_SSPP0, 30, 2 },
	{ DBGBUS_SSPP0, 30, 3 },
	{ DBGBUS_SSPP0, 30, 4 },
	{ DBGBUS_SSPP0, 30, 5 },
	{ DBGBUS_SSPP0, 30, 6 },
	{ DBGBUS_SSPP0, 30, 7 },

	{ DBGBUS_SSPP0, 31, 0 },
	{ DBGBUS_SSPP0, 31, 1 },
	{ DBGBUS_SSPP0, 31, 2 },
	{ DBGBUS_SSPP0, 31, 3 },
	{ DBGBUS_SSPP0, 31, 4 },
	{ DBGBUS_SSPP0, 31, 5 },
	{ DBGBUS_SSPP0, 31, 6 },
	{ DBGBUS_SSPP0, 31, 7 },

	{ DBGBUS_SSPP0, 32, 0 },
	{ DBGBUS_SSPP0, 32, 1 },
	{ DBGBUS_SSPP0, 32, 2 },
	{ DBGBUS_SSPP0, 32, 3 },
	{ DBGBUS_SSPP0, 32, 4 },
	{ DBGBUS_SSPP0, 32, 5 },
	{ DBGBUS_SSPP0, 32, 6 },
	{ DBGBUS_SSPP0, 32, 7 },

	{ DBGBUS_SSPP0, 33, 0 },
	{ DBGBUS_SSPP0, 33, 1 },
	{ DBGBUS_SSPP0, 33, 2 },
	{ DBGBUS_SSPP0, 33, 3 },
	{ DBGBUS_SSPP0, 33, 4 },
	{ DBGBUS_SSPP0, 33, 5 },
	{ DBGBUS_SSPP0, 33, 6 },
	{ DBGBUS_SSPP0, 33, 7 },

	{ DBGBUS_SSPP0, 34, 0 },
	{ DBGBUS_SSPP0, 34, 1 },
	{ DBGBUS_SSPP0, 34, 2 },
	{ DBGBUS_SSPP0, 34, 3 },
	{ DBGBUS_SSPP0, 34, 4 },
	{ DBGBUS_SSPP0, 34, 5 },
	{ DBGBUS_SSPP0, 34, 6 },
	{ DBGBUS_SSPP0, 34, 7 },

	{ DBGBUS_SSPP0, 35, 0 },
	{ DBGBUS_SSPP0, 35, 1 },
	{ DBGBUS_SSPP0, 35, 2 },
	{ DBGBUS_SSPP0, 35, 3 },

	/* dma 0 */
	{ DBGBUS_SSPP0, 40, 0 },
	{ DBGBUS_SSPP0, 40, 1 },
	{ DBGBUS_SSPP0, 40, 2 },
	{ DBGBUS_SSPP0, 40, 3 },
	{ DBGBUS_SSPP0, 40, 4 },
	{ DBGBUS_SSPP0, 40, 5 },
	{ DBGBUS_SSPP0, 40, 6 },
	{ DBGBUS_SSPP0, 40, 7 },

	{ DBGBUS_SSPP0, 41, 0 },
	{ DBGBUS_SSPP0, 41, 1 },
	{ DBGBUS_SSPP0, 41, 2 },
	{ DBGBUS_SSPP0, 41, 3 },
	{ DBGBUS_SSPP0, 41, 4 },
	{ DBGBUS_SSPP0, 41, 5 },
	{ DBGBUS_SSPP0, 41, 6 },
	{ DBGBUS_SSPP0, 41, 7 },

	{ DBGBUS_SSPP0, 42, 0 },
	{ DBGBUS_SSPP0, 42, 1 },
	{ DBGBUS_SSPP0, 42, 2 },
	{ DBGBUS_SSPP0, 42, 3 },
	{ DBGBUS_SSPP0, 42, 4 },
	{ DBGBUS_SSPP0, 42, 5 },
	{ DBGBUS_SSPP0, 42, 6 },
	{ DBGBUS_SSPP0, 42, 7 },

	{ DBGBUS_SSPP0, 44, 0 },
	{ DBGBUS_SSPP0, 44, 1 },
	{ DBGBUS_SSPP0, 44, 2 },
	{ DBGBUS_SSPP0, 44, 3 },
	{ DBGBUS_SSPP0, 44, 4 },
	{ DBGBUS_SSPP0, 44, 5 },
	{ DBGBUS_SSPP0, 44, 6 },
	{ DBGBUS_SSPP0, 44, 7 },

	{ DBGBUS_SSPP0, 45, 0 },
	{ DBGBUS_SSPP0, 45, 1 },
	{ DBGBUS_SSPP0, 45, 2 },
	{ DBGBUS_SSPP0, 45, 3 },
	{ DBGBUS_SSPP0, 45, 4 },
	{ DBGBUS_SSPP0, 45, 5 },
	{ DBGBUS_SSPP0, 45, 6 },
	{ DBGBUS_SSPP0, 45, 7 },

	/* fetch sspp1 */
	/* vig 1 */
	{ DBGBUS_SSPP1, 0, 0 },
	{ DBGBUS_SSPP1, 0, 1 },
	{ DBGBUS_SSPP1, 0, 2 },
	{ DBGBUS_SSPP1, 0, 3 },
	{ DBGBUS_SSPP1, 0, 4 },
	{ DBGBUS_SSPP1, 0, 5 },
	{ DBGBUS_SSPP1, 0, 6 },
	{ DBGBUS_SSPP1, 0, 7 },

	{ DBGBUS_SSPP1, 1, 0 },
	{ DBGBUS_SSPP1, 1, 1 },
	{ DBGBUS_SSPP1, 1, 2 },
	{ DBGBUS_SSPP1, 1, 3 },
	{ DBGBUS_SSPP1, 1, 4 },
	{ DBGBUS_SSPP1, 1, 5 },
	{ DBGBUS_SSPP1, 1, 6 },
	{ DBGBUS_SSPP1, 1, 7 },

	{ DBGBUS_SSPP1, 2, 0 },
	{ DBGBUS_SSPP1, 2, 1 },
	{ DBGBUS_SSPP1, 2, 2 },
	{ DBGBUS_SSPP1, 2, 3 },
	{ DBGBUS_SSPP1, 2, 4 },
	{ DBGBUS_SSPP1, 2, 5 },
	{ DBGBUS_SSPP1, 2, 6 },
	{ DBGBUS_SSPP1, 2, 7 },

	{ DBGBUS_SSPP1, 4, 0 },
	{ DBGBUS_SSPP1, 4, 1 },
	{ DBGBUS_SSPP1, 4, 2 },
	{ DBGBUS_SSPP1, 4, 3 },
	{ DBGBUS_SSPP1, 4, 4 },
	{ DBGBUS_SSPP1, 4, 5 },
	{ DBGBUS_SSPP1, 4, 6 },
	{ DBGBUS_SSPP1, 4, 7 },

	{ DBGBUS_SSPP1, 5, 0 },
	{ DBGBUS_SSPP1, 5, 1 },
	{ DBGBUS_SSPP1, 5, 2 },
	{ DBGBUS_SSPP1, 5, 3 },
	{ DBGBUS_SSPP1, 5, 4 },
	{ DBGBUS_SSPP1, 5, 5 },
	{ DBGBUS_SSPP1, 5, 6 },
	{ DBGBUS_SSPP1, 5, 7 },

	/* vig 3 */
	{ DBGBUS_SSPP1, 20, 0 },
	{ DBGBUS_SSPP1, 20, 1 },
	{ DBGBUS_SSPP1, 20, 2 },
	{ DBGBUS_SSPP1, 20, 3 },
	{ DBGBUS_SSPP1, 20, 4 },
	{ DBGBUS_SSPP1, 20, 5 },
	{ DBGBUS_SSPP1, 20, 6 },
	{ DBGBUS_SSPP1, 20, 7 },

	{ DBGBUS_SSPP1, 21, 0 },
	{ DBGBUS_SSPP1, 21, 1 },
	{ DBGBUS_SSPP1, 21, 2 },
	{ DBGBUS_SSPP1, 21, 3 },
	{ DBGBUS_SSPP1, 21, 4 },
	{ DBGBUS_SSPP1, 21, 5 },
	{ DBGBUS_SSPP1, 21, 6 },
	{ DBGBUS_SSPP1, 21, 7 },

	{ DBGBUS_SSPP1, 22, 0 },
	{ DBGBUS_SSPP1, 22, 1 },
	{ DBGBUS_SSPP1, 22, 2 },
	{ DBGBUS_SSPP1, 22, 3 },
	{ DBGBUS_SSPP1, 22, 4 },
	{ DBGBUS_SSPP1, 22, 5 },
	{ DBGBUS_SSPP1, 22, 6 },
	{ DBGBUS_SSPP1, 22, 7 },

	{ DBGBUS_SSPP1, 24, 0 },
	{ DBGBUS_SSPP1, 24, 1 },
	{ DBGBUS_SSPP1, 24, 2 },
	{ DBGBUS_SSPP1, 24, 3 },
	{ DBGBUS_SSPP1, 24, 4 },
	{ DBGBUS_SSPP1, 24, 5 },
	{ DBGBUS_SSPP1, 24, 6 },
	{ DBGBUS_SSPP1, 24, 7 },

	{ DBGBUS_SSPP1, 25, 0 },
	{ DBGBUS_SSPP1, 25, 1 },
	{ DBGBUS_SSPP1, 25, 2 },
	{ DBGBUS_SSPP1, 25, 3 },
	{ DBGBUS_SSPP1, 25, 4 },
	{ DBGBUS_SSPP1, 25, 5 },
	{ DBGBUS_SSPP1, 25, 6 },
	{ DBGBUS_SSPP1, 25, 7 },

	/* dma 3 */
	{ DBGBUS_SSPP1, 30, 0 },
	{ DBGBUS_SSPP1, 30, 1 },
	{ DBGBUS_SSPP1, 30, 2 },
	{ DBGBUS_SSPP1, 30, 3 },
	{ DBGBUS_SSPP1, 30, 4 },
	{ DBGBUS_SSPP1, 30, 5 },
	{ DBGBUS_SSPP1, 30, 6 },
	{ DBGBUS_SSPP1, 30, 7 },

	{ DBGBUS_SSPP1, 31, 0 },
	{ DBGBUS_SSPP1, 31, 1 },
	{ DBGBUS_SSPP1, 31, 2 },
	{ DBGBUS_SSPP1, 31, 3 },
	{ DBGBUS_SSPP1, 31, 4 },
	{ DBGBUS_SSPP1, 31, 5 },
	{ DBGBUS_SSPP1, 31, 6 },
	{ DBGBUS_SSPP1, 31, 7 },

	{ DBGBUS_SSPP1, 32, 0 },
	{ DBGBUS_SSPP1, 32, 1 },
	{ DBGBUS_SSPP1, 32, 2 },
	{ DBGBUS_SSPP1, 32, 3 },
	{ DBGBUS_SSPP1, 32, 4 },
	{ DBGBUS_SSPP1, 32, 5 },
	{ DBGBUS_SSPP1, 32, 6 },
	{ DBGBUS_SSPP1, 32, 7 },

	{ DBGBUS_SSPP1, 33, 0 },
	{ DBGBUS_SSPP1, 33, 1 },
	{ DBGBUS_SSPP1, 33, 2 },
	{ DBGBUS_SSPP1, 33, 3 },
	{ DBGBUS_SSPP1, 33, 4 },
	{ DBGBUS_SSPP1, 33, 5 },
	{ DBGBUS_SSPP1, 33, 6 },
	{ DBGBUS_SSPP1, 33, 7 },

	{ DBGBUS_SSPP1, 34, 0 },
	{ DBGBUS_SSPP1, 34, 1 },
	{ DBGBUS_SSPP1, 34, 2 },
	{ DBGBUS_SSPP1, 34, 3 },
	{ DBGBUS_SSPP1, 34, 4 },
	{ DBGBUS_SSPP1, 34, 5 },
	{ DBGBUS_SSPP1, 34, 6 },
	{ DBGBUS_SSPP1, 34, 7 },

	{ DBGBUS_SSPP1, 35, 0 },
	{ DBGBUS_SSPP1, 35, 1 },
	{ DBGBUS_SSPP1, 35, 2 },

	/* dma 1 */
	{ DBGBUS_SSPP1, 40, 0 },
	{ DBGBUS_SSPP1, 40, 1 },
	{ DBGBUS_SSPP1, 40, 2 },
	{ DBGBUS_SSPP1, 40, 3 },
	{ DBGBUS_SSPP1, 40, 4 },
	{ DBGBUS_SSPP1, 40, 5 },
	{ DBGBUS_SSPP1, 40, 6 },
	{ DBGBUS_SSPP1, 40, 7 },

	{ DBGBUS_SSPP1, 41, 0 },
	{ DBGBUS_SSPP1, 41, 1 },
	{ DBGBUS_SSPP1, 41, 2 },
	{ DBGBUS_SSPP1, 41, 3 },
	{ DBGBUS_SSPP1, 41, 4 },
	{ DBGBUS_SSPP1, 41, 5 },
	{ DBGBUS_SSPP1, 41, 6 },
	{ DBGBUS_SSPP1, 41, 7 },

	{ DBGBUS_SSPP1, 42, 0 },
	{ DBGBUS_SSPP1, 42, 1 },
	{ DBGBUS_SSPP1, 42, 2 },
	{ DBGBUS_SSPP1, 42, 3 },
	{ DBGBUS_SSPP1, 42, 4 },
	{ DBGBUS_SSPP1, 42, 5 },
	{ DBGBUS_SSPP1, 42, 6 },
	{ DBGBUS_SSPP1, 42, 7 },

	{ DBGBUS_SSPP1, 44, 0 },
	{ DBGBUS_SSPP1, 44, 1 },
	{ DBGBUS_SSPP1, 44, 2 },
	{ DBGBUS_SSPP1, 44, 3 },
	{ DBGBUS_SSPP1, 44, 4 },
	{ DBGBUS_SSPP1, 44, 5 },
	{ DBGBUS_SSPP1, 44, 6 },
	{ DBGBUS_SSPP1, 44, 7 },

	{ DBGBUS_SSPP1, 45, 0 },
	{ DBGBUS_SSPP1, 45, 1 },
	{ DBGBUS_SSPP1, 45, 2 },
	{ DBGBUS_SSPP1, 45, 3 },
	{ DBGBUS_SSPP1, 45, 4 },
	{ DBGBUS_SSPP1, 45, 5 },
	{ DBGBUS_SSPP1, 45, 6 },
	{ DBGBUS_SSPP1, 45, 7 },

	/* dspp */
	{ DBGBUS_DSPP, 13, 0 },
	{ DBGBUS_DSPP, 19, 0 },
	{ DBGBUS_DSPP, 14, 0 },
	{ DBGBUS_DSPP, 14, 1 },
	{ DBGBUS_DSPP, 14, 3 },
	{ DBGBUS_DSPP, 20, 0 },
	{ DBGBUS_DSPP, 20, 1 },
	{ DBGBUS_DSPP, 20, 3 },

	/* ppb_0 */
	{ DBGBUS_DSPP, 31, 0, _dpu_debug_bus_ppb0_dump },
	{ DBGBUS_DSPP, 33, 0, _dpu_debug_bus_ppb0_dump },
	{ DBGBUS_DSPP, 35, 0, _dpu_debug_bus_ppb0_dump },
	{ DBGBUS_DSPP, 42, 0, _dpu_debug_bus_ppb0_dump },

	/* ppb_1 */
	{ DBGBUS_DSPP, 32, 0, _dpu_debug_bus_ppb1_dump },
	{ DBGBUS_DSPP, 34, 0, _dpu_debug_bus_ppb1_dump },
	{ DBGBUS_DSPP, 36, 0, _dpu_debug_bus_ppb1_dump },
	{ DBGBUS_DSPP, 43, 0, _dpu_debug_bus_ppb1_dump },

	/* lm_lut */
	{ DBGBUS_DSPP, 109, 0 },
	{ DBGBUS_DSPP, 105, 0 },
	{ DBGBUS_DSPP, 103, 0 },

	/* crossbar */
	{ DBGBUS_DSPP, 0, 0, _dpu_debug_bus_xbar_dump },

	/* rotator */
	{ DBGBUS_DSPP, 9, 0},

	/* blend */
	/* LM0 */
	{ DBGBUS_DSPP, 63, 1},
	{ DBGBUS_DSPP, 63, 2},
	{ DBGBUS_DSPP, 63, 3},
	{ DBGBUS_DSPP, 63, 4},
	{ DBGBUS_DSPP, 63, 5},
	{ DBGBUS_DSPP, 63, 6},
	{ DBGBUS_DSPP, 63, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 64, 1},
	{ DBGBUS_DSPP, 64, 2},
	{ DBGBUS_DSPP, 64, 3},
	{ DBGBUS_DSPP, 64, 4},
	{ DBGBUS_DSPP, 64, 5},
	{ DBGBUS_DSPP, 64, 6},
	{ DBGBUS_DSPP, 64, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 65, 1},
	{ DBGBUS_DSPP, 65, 2},
	{ DBGBUS_DSPP, 65, 3},
	{ DBGBUS_DSPP, 65, 4},
	{ DBGBUS_DSPP, 65, 5},
	{ DBGBUS_DSPP, 65, 6},
	{ DBGBUS_DSPP, 65, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 66, 1},
	{ DBGBUS_DSPP, 66, 2},
	{ DBGBUS_DSPP, 66, 3},
	{ DBGBUS_DSPP, 66, 4},
	{ DBGBUS_DSPP, 66, 5},
	{ DBGBUS_DSPP, 66, 6},
	{ DBGBUS_DSPP, 66, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 67, 1},
	{ DBGBUS_DSPP, 67, 2},
	{ DBGBUS_DSPP, 67, 3},
	{ DBGBUS_DSPP, 67, 4},
	{ DBGBUS_DSPP, 67, 5},
	{ DBGBUS_DSPP, 67, 6},
	{ DBGBUS_DSPP, 67, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 68, 1},
	{ DBGBUS_DSPP, 68, 2},
	{ DBGBUS_DSPP, 68, 3},
	{ DBGBUS_DSPP, 68, 4},
	{ DBGBUS_DSPP, 68, 5},
	{ DBGBUS_DSPP, 68, 6},
	{ DBGBUS_DSPP, 68, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 69, 1},
	{ DBGBUS_DSPP, 69, 2},
	{ DBGBUS_DSPP, 69, 3},
	{ DBGBUS_DSPP, 69, 4},
	{ DBGBUS_DSPP, 69, 5},
	{ DBGBUS_DSPP, 69, 6},
	{ DBGBUS_DSPP, 69, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 84, 1},
	{ DBGBUS_DSPP, 84, 2},
	{ DBGBUS_DSPP, 84, 3},
	{ DBGBUS_DSPP, 84, 4},
	{ DBGBUS_DSPP, 84, 5},
	{ DBGBUS_DSPP, 84, 6},
	{ DBGBUS_DSPP, 84, 7, _dpu_debug_bus_lm_dump },


	{ DBGBUS_DSPP, 85, 1},
	{ DBGBUS_DSPP, 85, 2},
	{ DBGBUS_DSPP, 85, 3},
	{ DBGBUS_DSPP, 85, 4},
	{ DBGBUS_DSPP, 85, 5},
	{ DBGBUS_DSPP, 85, 6},
	{ DBGBUS_DSPP, 85, 7, _dpu_debug_bus_lm_dump },


	{ DBGBUS_DSPP, 86, 1},
	{ DBGBUS_DSPP, 86, 2},
	{ DBGBUS_DSPP, 86, 3},
	{ DBGBUS_DSPP, 86, 4},
	{ DBGBUS_DSPP, 86, 5},
	{ DBGBUS_DSPP, 86, 6},
	{ DBGBUS_DSPP, 86, 7, _dpu_debug_bus_lm_dump },


	{ DBGBUS_DSPP, 87, 1},
	{ DBGBUS_DSPP, 87, 2},
	{ DBGBUS_DSPP, 87, 3},
	{ DBGBUS_DSPP, 87, 4},
	{ DBGBUS_DSPP, 87, 5},
	{ DBGBUS_DSPP, 87, 6},
	{ DBGBUS_DSPP, 87, 7, _dpu_debug_bus_lm_dump },

	/* LM1 */
	{ DBGBUS_DSPP, 70, 1},
	{ DBGBUS_DSPP, 70, 2},
	{ DBGBUS_DSPP, 70, 3},
	{ DBGBUS_DSPP, 70, 4},
	{ DBGBUS_DSPP, 70, 5},
	{ DBGBUS_DSPP, 70, 6},
	{ DBGBUS_DSPP, 70, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 71, 1},
	{ DBGBUS_DSPP, 71, 2},
	{ DBGBUS_DSPP, 71, 3},
	{ DBGBUS_DSPP, 71, 4},
	{ DBGBUS_DSPP, 71, 5},
	{ DBGBUS_DSPP, 71, 6},
	{ DBGBUS_DSPP, 71, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 72, 1},
	{ DBGBUS_DSPP, 72, 2},
	{ DBGBUS_DSPP, 72, 3},
	{ DBGBUS_DSPP, 72, 4},
	{ DBGBUS_DSPP, 72, 5},
	{ DBGBUS_DSPP, 72, 6},
	{ DBGBUS_DSPP, 72, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 73, 1},
	{ DBGBUS_DSPP, 73, 2},
	{ DBGBUS_DSPP, 73, 3},
	{ DBGBUS_DSPP, 73, 4},
	{ DBGBUS_DSPP, 73, 5},
	{ DBGBUS_DSPP, 73, 6},
	{ DBGBUS_DSPP, 73, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 74, 1},
	{ DBGBUS_DSPP, 74, 2},
	{ DBGBUS_DSPP, 74, 3},
	{ DBGBUS_DSPP, 74, 4},
	{ DBGBUS_DSPP, 74, 5},
	{ DBGBUS_DSPP, 74, 6},
	{ DBGBUS_DSPP, 74, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 75, 1},
	{ DBGBUS_DSPP, 75, 2},
	{ DBGBUS_DSPP, 75, 3},
	{ DBGBUS_DSPP, 75, 4},
	{ DBGBUS_DSPP, 75, 5},
	{ DBGBUS_DSPP, 75, 6},
	{ DBGBUS_DSPP, 75, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 76, 1},
	{ DBGBUS_DSPP, 76, 2},
	{ DBGBUS_DSPP, 76, 3},
	{ DBGBUS_DSPP, 76, 4},
	{ DBGBUS_DSPP, 76, 5},
	{ DBGBUS_DSPP, 76, 6},
	{ DBGBUS_DSPP, 76, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 88, 1},
	{ DBGBUS_DSPP, 88, 2},
	{ DBGBUS_DSPP, 88, 3},
	{ DBGBUS_DSPP, 88, 4},
	{ DBGBUS_DSPP, 88, 5},
	{ DBGBUS_DSPP, 88, 6},
	{ DBGBUS_DSPP, 88, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 89, 1},
	{ DBGBUS_DSPP, 89, 2},
	{ DBGBUS_DSPP, 89, 3},
	{ DBGBUS_DSPP, 89, 4},
	{ DBGBUS_DSPP, 89, 5},
	{ DBGBUS_DSPP, 89, 6},
	{ DBGBUS_DSPP, 89, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 90, 1},
	{ DBGBUS_DSPP, 90, 2},
	{ DBGBUS_DSPP, 90, 3},
	{ DBGBUS_DSPP, 90, 4},
	{ DBGBUS_DSPP, 90, 5},
	{ DBGBUS_DSPP, 90, 6},
	{ DBGBUS_DSPP, 90, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 91, 1},
	{ DBGBUS_DSPP, 91, 2},
	{ DBGBUS_DSPP, 91, 3},
	{ DBGBUS_DSPP, 91, 4},
	{ DBGBUS_DSPP, 91, 5},
	{ DBGBUS_DSPP, 91, 6},
	{ DBGBUS_DSPP, 91, 7, _dpu_debug_bus_lm_dump },

	/* LM2 */
	{ DBGBUS_DSPP, 77, 0},
	{ DBGBUS_DSPP, 77, 1},
	{ DBGBUS_DSPP, 77, 2},
	{ DBGBUS_DSPP, 77, 3},
	{ DBGBUS_DSPP, 77, 4},
	{ DBGBUS_DSPP, 77, 5},
	{ DBGBUS_DSPP, 77, 6},
	{ DBGBUS_DSPP, 77, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 78, 0},
	{ DBGBUS_DSPP, 78, 1},
	{ DBGBUS_DSPP, 78, 2},
	{ DBGBUS_DSPP, 78, 3},
	{ DBGBUS_DSPP, 78, 4},
	{ DBGBUS_DSPP, 78, 5},
	{ DBGBUS_DSPP, 78, 6},
	{ DBGBUS_DSPP, 78, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 79, 0},
	{ DBGBUS_DSPP, 79, 1},
	{ DBGBUS_DSPP, 79, 2},
	{ DBGBUS_DSPP, 79, 3},
	{ DBGBUS_DSPP, 79, 4},
	{ DBGBUS_DSPP, 79, 5},
	{ DBGBUS_DSPP, 79, 6},
	{ DBGBUS_DSPP, 79, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 80, 0},
	{ DBGBUS_DSPP, 80, 1},
	{ DBGBUS_DSPP, 80, 2},
	{ DBGBUS_DSPP, 80, 3},
	{ DBGBUS_DSPP, 80, 4},
	{ DBGBUS_DSPP, 80, 5},
	{ DBGBUS_DSPP, 80, 6},
	{ DBGBUS_DSPP, 80, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 81, 0},
	{ DBGBUS_DSPP, 81, 1},
	{ DBGBUS_DSPP, 81, 2},
	{ DBGBUS_DSPP, 81, 3},
	{ DBGBUS_DSPP, 81, 4},
	{ DBGBUS_DSPP, 81, 5},
	{ DBGBUS_DSPP, 81, 6},
	{ DBGBUS_DSPP, 81, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 82, 0},
	{ DBGBUS_DSPP, 82, 1},
	{ DBGBUS_DSPP, 82, 2},
	{ DBGBUS_DSPP, 82, 3},
	{ DBGBUS_DSPP, 82, 4},
	{ DBGBUS_DSPP, 82, 5},
	{ DBGBUS_DSPP, 82, 6},
	{ DBGBUS_DSPP, 82, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 83, 0},
	{ DBGBUS_DSPP, 83, 1},
	{ DBGBUS_DSPP, 83, 2},
	{ DBGBUS_DSPP, 83, 3},
	{ DBGBUS_DSPP, 83, 4},
	{ DBGBUS_DSPP, 83, 5},
	{ DBGBUS_DSPP, 83, 6},
	{ DBGBUS_DSPP, 83, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 92, 1},
	{ DBGBUS_DSPP, 92, 2},
	{ DBGBUS_DSPP, 92, 3},
	{ DBGBUS_DSPP, 92, 4},
	{ DBGBUS_DSPP, 92, 5},
	{ DBGBUS_DSPP, 92, 6},
	{ DBGBUS_DSPP, 92, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 93, 1},
	{ DBGBUS_DSPP, 93, 2},
	{ DBGBUS_DSPP, 93, 3},
	{ DBGBUS_DSPP, 93, 4},
	{ DBGBUS_DSPP, 93, 5},
	{ DBGBUS_DSPP, 93, 6},
	{ DBGBUS_DSPP, 93, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 94, 1},
	{ DBGBUS_DSPP, 94, 2},
	{ DBGBUS_DSPP, 94, 3},
	{ DBGBUS_DSPP, 94, 4},
	{ DBGBUS_DSPP, 94, 5},
	{ DBGBUS_DSPP, 94, 6},
	{ DBGBUS_DSPP, 94, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 95, 1},
	{ DBGBUS_DSPP, 95, 2},
	{ DBGBUS_DSPP, 95, 3},
	{ DBGBUS_DSPP, 95, 4},
	{ DBGBUS_DSPP, 95, 5},
	{ DBGBUS_DSPP, 95, 6},
	{ DBGBUS_DSPP, 95, 7, _dpu_debug_bus_lm_dump },

	/* LM5 */
	{ DBGBUS_DSPP, 110, 1},
	{ DBGBUS_DSPP, 110, 2},
	{ DBGBUS_DSPP, 110, 3},
	{ DBGBUS_DSPP, 110, 4},
	{ DBGBUS_DSPP, 110, 5},
	{ DBGBUS_DSPP, 110, 6},
	{ DBGBUS_DSPP, 110, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 111, 1},
	{ DBGBUS_DSPP, 111, 2},
	{ DBGBUS_DSPP, 111, 3},
	{ DBGBUS_DSPP, 111, 4},
	{ DBGBUS_DSPP, 111, 5},
	{ DBGBUS_DSPP, 111, 6},
	{ DBGBUS_DSPP, 111, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 112, 1},
	{ DBGBUS_DSPP, 112, 2},
	{ DBGBUS_DSPP, 112, 3},
	{ DBGBUS_DSPP, 112, 4},
	{ DBGBUS_DSPP, 112, 5},
	{ DBGBUS_DSPP, 112, 6},
	{ DBGBUS_DSPP, 112, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 113, 1},
	{ DBGBUS_DSPP, 113, 2},
	{ DBGBUS_DSPP, 113, 3},
	{ DBGBUS_DSPP, 113, 4},
	{ DBGBUS_DSPP, 113, 5},
	{ DBGBUS_DSPP, 113, 6},
	{ DBGBUS_DSPP, 113, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 114, 1},
	{ DBGBUS_DSPP, 114, 2},
	{ DBGBUS_DSPP, 114, 3},
	{ DBGBUS_DSPP, 114, 4},
	{ DBGBUS_DSPP, 114, 5},
	{ DBGBUS_DSPP, 114, 6},
	{ DBGBUS_DSPP, 114, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 115, 1},
	{ DBGBUS_DSPP, 115, 2},
	{ DBGBUS_DSPP, 115, 3},
	{ DBGBUS_DSPP, 115, 4},
	{ DBGBUS_DSPP, 115, 5},
	{ DBGBUS_DSPP, 115, 6},
	{ DBGBUS_DSPP, 115, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 116, 1},
	{ DBGBUS_DSPP, 116, 2},
	{ DBGBUS_DSPP, 116, 3},
	{ DBGBUS_DSPP, 116, 4},
	{ DBGBUS_DSPP, 116, 5},
	{ DBGBUS_DSPP, 116, 6},
	{ DBGBUS_DSPP, 116, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 117, 1},
	{ DBGBUS_DSPP, 117, 2},
	{ DBGBUS_DSPP, 117, 3},
	{ DBGBUS_DSPP, 117, 4},
	{ DBGBUS_DSPP, 117, 5},
	{ DBGBUS_DSPP, 117, 6},
	{ DBGBUS_DSPP, 117, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 118, 1},
	{ DBGBUS_DSPP, 118, 2},
	{ DBGBUS_DSPP, 118, 3},
	{ DBGBUS_DSPP, 118, 4},
	{ DBGBUS_DSPP, 118, 5},
	{ DBGBUS_DSPP, 118, 6},
	{ DBGBUS_DSPP, 118, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 119, 1},
	{ DBGBUS_DSPP, 119, 2},
	{ DBGBUS_DSPP, 119, 3},
	{ DBGBUS_DSPP, 119, 4},
	{ DBGBUS_DSPP, 119, 5},
	{ DBGBUS_DSPP, 119, 6},
	{ DBGBUS_DSPP, 119, 7, _dpu_debug_bus_lm_dump },

	{ DBGBUS_DSPP, 120, 1},
	{ DBGBUS_DSPP, 120, 2},
	{ DBGBUS_DSPP, 120, 3},
	{ DBGBUS_DSPP, 120, 4},
	{ DBGBUS_DSPP, 120, 5},
	{ DBGBUS_DSPP, 120, 6},
	{ DBGBUS_DSPP, 120, 7, _dpu_debug_bus_lm_dump },

	/* csc */
	{ DBGBUS_SSPP0, 7, 0},
	{ DBGBUS_SSPP0, 7, 1},
	{ DBGBUS_SSPP0, 27, 0},
	{ DBGBUS_SSPP0, 27, 1},
	{ DBGBUS_SSPP1, 7, 0},
	{ DBGBUS_SSPP1, 7, 1},
	{ DBGBUS_SSPP1, 27, 0},
	{ DBGBUS_SSPP1, 27, 1},

	/* pcc */
	{ DBGBUS_SSPP0, 3,  3},
	{ DBGBUS_SSPP0, 23, 3},
	{ DBGBUS_SSPP0, 33, 3},
	{ DBGBUS_SSPP0, 43, 3},
	{ DBGBUS_SSPP1, 3,  3},
	{ DBGBUS_SSPP1, 23, 3},
	{ DBGBUS_SSPP1, 33, 3},
	{ DBGBUS_SSPP1, 43, 3},

	/* spa */
	{ DBGBUS_SSPP0, 8,  0},
	{ DBGBUS_SSPP0, 28, 0},
	{ DBGBUS_SSPP1, 8,  0},
	{ DBGBUS_SSPP1, 28, 0},
	{ DBGBUS_DSPP, 13, 0},
	{ DBGBUS_DSPP, 19, 0},

	/* igc */
	{ DBGBUS_SSPP0, 17, 0},
	{ DBGBUS_SSPP0, 17, 1},
	{ DBGBUS_SSPP0, 17, 3},
	{ DBGBUS_SSPP0, 37, 0},
	{ DBGBUS_SSPP0, 37, 1},
	{ DBGBUS_SSPP0, 37, 3},
	{ DBGBUS_SSPP0, 46, 0},
	{ DBGBUS_SSPP0, 46, 1},
	{ DBGBUS_SSPP0, 46, 3},

	{ DBGBUS_SSPP1, 17, 0},
	{ DBGBUS_SSPP1, 17, 1},
	{ DBGBUS_SSPP1, 17, 3},
	{ DBGBUS_SSPP1, 37, 0},
	{ DBGBUS_SSPP1, 37, 1},
	{ DBGBUS_SSPP1, 37, 3},
	{ DBGBUS_SSPP1, 46, 0},
	{ DBGBUS_SSPP1, 46, 1},
	{ DBGBUS_SSPP1, 46, 3},

	{ DBGBUS_DSPP, 14, 0},
	{ DBGBUS_DSPP, 14, 1},
	{ DBGBUS_DSPP, 14, 3},
	{ DBGBUS_DSPP, 20, 0},
	{ DBGBUS_DSPP, 20, 1},
	{ DBGBUS_DSPP, 20, 3},

	/* intf0-3 */
	{ DBGBUS_PERIPH, 0, 0},
	{ DBGBUS_PERIPH, 1, 0},
	{ DBGBUS_PERIPH, 2, 0},
	{ DBGBUS_PERIPH, 3, 0},

	/* te counter wrapper */
	{ DBGBUS_PERIPH, 60, 0},

	/* dsc0 */
	{ DBGBUS_PERIPH, 47, 0},
	{ DBGBUS_PERIPH, 47, 1},
	{ DBGBUS_PERIPH, 47, 2},
	{ DBGBUS_PERIPH, 47, 3},
	{ DBGBUS_PERIPH, 47, 4},
	{ DBGBUS_PERIPH, 47, 5},
	{ DBGBUS_PERIPH, 47, 6},
	{ DBGBUS_PERIPH, 47, 7},

	/* dsc1 */
	{ DBGBUS_PERIPH, 48, 0},
	{ DBGBUS_PERIPH, 48, 1},
	{ DBGBUS_PERIPH, 48, 2},
	{ DBGBUS_PERIPH, 48, 3},
	{ DBGBUS_PERIPH, 48, 4},
	{ DBGBUS_PERIPH, 48, 5},
	{ DBGBUS_PERIPH, 48, 6},
	{ DBGBUS_PERIPH, 48, 7},

	/* dsc2 */
	{ DBGBUS_PERIPH, 51, 0},
	{ DBGBUS_PERIPH, 51, 1},
	{ DBGBUS_PERIPH, 51, 2},
	{ DBGBUS_PERIPH, 51, 3},
	{ DBGBUS_PERIPH, 51, 4},
	{ DBGBUS_PERIPH, 51, 5},
	{ DBGBUS_PERIPH, 51, 6},
	{ DBGBUS_PERIPH, 51, 7},

	/* dsc3 */
	{ DBGBUS_PERIPH, 52, 0},
	{ DBGBUS_PERIPH, 52, 1},
	{ DBGBUS_PERIPH, 52, 2},
	{ DBGBUS_PERIPH, 52, 3},
	{ DBGBUS_PERIPH, 52, 4},
	{ DBGBUS_PERIPH, 52, 5},
	{ DBGBUS_PERIPH, 52, 6},
	{ DBGBUS_PERIPH, 52, 7},

	/* tear-check */
	{ DBGBUS_PERIPH, 63, 0 },
	{ DBGBUS_PERIPH, 64, 0 },
	{ DBGBUS_PERIPH, 65, 0 },
	{ DBGBUS_PERIPH, 73, 0 },
	{ DBGBUS_PERIPH, 74, 0 },

	/* cdwn */
	{ DBGBUS_PERIPH, 80, 0},
	{ DBGBUS_PERIPH, 80, 1},
	{ DBGBUS_PERIPH, 80, 2},

	{ DBGBUS_PERIPH, 81, 0},
	{ DBGBUS_PERIPH, 81, 1},
	{ DBGBUS_PERIPH, 81, 2},

	{ DBGBUS_PERIPH, 82, 0},
	{ DBGBUS_PERIPH, 82, 1},
	{ DBGBUS_PERIPH, 82, 2},
	{ DBGBUS_PERIPH, 82, 3},
	{ DBGBUS_PERIPH, 82, 4},
	{ DBGBUS_PERIPH, 82, 5},
	{ DBGBUS_PERIPH, 82, 6},
	{ DBGBUS_PERIPH, 82, 7},

	/* hdmi */
	{ DBGBUS_PERIPH, 68, 0},
	{ DBGBUS_PERIPH, 68, 1},
	{ DBGBUS_PERIPH, 68, 2},
	{ DBGBUS_PERIPH, 68, 3},
	{ DBGBUS_PERIPH, 68, 4},
	{ DBGBUS_PERIPH, 68, 5},

	/* edp */
	{ DBGBUS_PERIPH, 69, 0},
	{ DBGBUS_PERIPH, 69, 1},
	{ DBGBUS_PERIPH, 69, 2},
	{ DBGBUS_PERIPH, 69, 3},
	{ DBGBUS_PERIPH, 69, 4},
	{ DBGBUS_PERIPH, 69, 5},

	/* dsi0 */
	{ DBGBUS_PERIPH, 70, 0},
	{ DBGBUS_PERIPH, 70, 1},
	{ DBGBUS_PERIPH, 70, 2},
	{ DBGBUS_PERIPH, 70, 3},
	{ DBGBUS_PERIPH, 70, 4},
	{ DBGBUS_PERIPH, 70, 5},

	/* dsi1 */
	{ DBGBUS_PERIPH, 71, 0},
	{ DBGBUS_PERIPH, 71, 1},
	{ DBGBUS_PERIPH, 71, 2},
	{ DBGBUS_PERIPH, 71, 3},
	{ DBGBUS_PERIPH, 71, 4},
	{ DBGBUS_PERIPH, 71, 5},
};

static struct vbif_debug_bus_entry vbif_dbg_bus_msm8998[] = {
	{0x214, 0x21c, 16, 2, 0x0, 0xd},     /* arb clients */
	{0x214, 0x21c, 16, 2, 0x80, 0xc0},   /* arb clients */
	{0x214, 0x21c, 16, 2, 0x100, 0x140}, /* arb clients */
	{0x214, 0x21c, 0, 16, 0x0, 0xf},     /* xin blocks - axi side */
	{0x214, 0x21c, 0, 16, 0x80, 0xa4},   /* xin blocks - axi side */
	{0x214, 0x21c, 0, 15, 0x100, 0x124}, /* xin blocks - axi side */
	{0x21c, 0x214, 0, 14, 0, 0xc}, /* xin blocks - clock side */
};

/**
 * _dpu_dbg_enable_power - use callback to turn power on for hw register access
 * @enable: whether to turn power on or off
 */
static inline void _dpu_dbg_enable_power(int enable)
{
	if (enable)
		pm_runtime_get_sync(dpu_dbg_base.dev);
	else
		pm_runtime_put_sync(dpu_dbg_base.dev);
}

static void _dpu_dbg_dump_dpu_dbg_bus(struct dpu_dbg_dpu_debug_bus *bus)
{
	bool in_log, in_mem;
	u32 **dump_mem = NULL;
	u32 *dump_addr = NULL;
	u32 status = 0;
	struct dpu_debug_bus_entry *head;
	dma_addr_t dma = 0;
	int list_size;
	int i;
	u32 offset;
	void __iomem *mem_base = NULL;
	struct dpu_dbg_reg_base *reg_base;

	if (!bus || !bus->cmn.entries_size)
		return;

	list_for_each_entry(reg_base, &dpu_dbg_base.reg_base_list,
			reg_base_head)
		if (strlen(reg_base->name) &&
			!strcmp(reg_base->name, bus->cmn.name))
			mem_base = reg_base->base + bus->top_blk_off;

	if (!mem_base) {
		pr_err("unable to find mem_base for %s\n", bus->cmn.name);
		return;
	}

	dump_mem = &bus->cmn.dumped_content;

	/* will keep in memory 4 entries of 4 bytes each */
	list_size = (bus->cmn.entries_size * 4 * 4);

	in_log = (bus->cmn.enable_mask & DPU_DBG_DUMP_IN_LOG);
	in_mem = (bus->cmn.enable_mask & DPU_DBG_DUMP_IN_MEM);

	if (!in_log && !in_mem)
		return;

	dev_info(dpu_dbg_base.dev, "======== start %s dump =========\n",
			bus->cmn.name);

	if (in_mem) {
		if (!(*dump_mem))
			*dump_mem = dma_alloc_coherent(dpu_dbg_base.dev,
				list_size, &dma, GFP_KERNEL);

		if (*dump_mem) {
			dump_addr = *dump_mem;
			dev_info(dpu_dbg_base.dev,
				"%s: start_addr:0x%pK len:0x%x\n",
				__func__, dump_addr, list_size);
		} else {
			in_mem = false;
			pr_err("dump_mem: allocation fails\n");
		}
	}

	_dpu_dbg_enable_power(true);
	for (i = 0; i < bus->cmn.entries_size; i++) {
		head = bus->entries + i;
		writel_relaxed(TEST_MASK(head->block_id, head->test_id),
				mem_base + head->wr_addr);
		wmb(); /* make sure test bits were written */

		if (bus->cmn.flags & DBGBUS_FLAGS_DSPP) {
			offset = DBGBUS_DSPP_STATUS;
			/* keep DSPP test point enabled */
			if (head->wr_addr != DBGBUS_DSPP)
				writel_relaxed(0xF, mem_base + DBGBUS_DSPP);
		} else {
			offset = head->wr_addr + 0x4;
		}

		status = readl_relaxed(mem_base + offset);

		if (in_log)
			dev_info(dpu_dbg_base.dev,
					"waddr=0x%x blk=%d tst=%d val=0x%x\n",
					head->wr_addr, head->block_id,
					head->test_id, status);

		if (dump_addr && in_mem) {
			dump_addr[i*4]     = head->wr_addr;
			dump_addr[i*4 + 1] = head->block_id;
			dump_addr[i*4 + 2] = head->test_id;
			dump_addr[i*4 + 3] = status;
		}

		if (head->analyzer)
			head->analyzer(mem_base, head, status);

		/* Disable debug bus once we are done */
		writel_relaxed(0, mem_base + head->wr_addr);
		if (bus->cmn.flags & DBGBUS_FLAGS_DSPP &&
						head->wr_addr != DBGBUS_DSPP)
			writel_relaxed(0x0, mem_base + DBGBUS_DSPP);
	}
	_dpu_dbg_enable_power(false);

	dev_info(dpu_dbg_base.dev, "======== end %s dump =========\n",
			bus->cmn.name);
}

static void _dpu_dbg_dump_vbif_debug_bus_entry(
		struct vbif_debug_bus_entry *head, void __iomem *mem_base,
		u32 *dump_addr, bool in_log)
{
	int i, j;
	u32 val;

	if (!dump_addr && !in_log)
		return;

	for (i = 0; i < head->block_cnt; i++) {
		writel_relaxed(1 << (i + head->bit_offset),
				mem_base + head->block_bus_addr);
		/* make sure that current bus blcok enable */
		wmb();
		for (j = head->test_pnt_start; j < head->test_pnt_cnt; j++) {
			writel_relaxed(j, mem_base + head->block_bus_addr + 4);
			/* make sure that test point is enabled */
			wmb();
			val = readl_relaxed(mem_base + MMSS_VBIF_TEST_BUS_OUT);
			if (dump_addr) {
				*dump_addr++ = head->block_bus_addr;
				*dump_addr++ = i;
				*dump_addr++ = j;
				*dump_addr++ = val;
			}
			if (in_log)
				dev_info(dpu_dbg_base.dev,
					"testpoint:%x arb/xin id=%d index=%d val=0x%x\n",
					head->block_bus_addr, i, j, val);
		}
	}
}

static void _dpu_dbg_dump_vbif_dbg_bus(struct dpu_dbg_vbif_debug_bus *bus)
{
	bool in_log, in_mem;
	u32 **dump_mem = NULL;
	u32 *dump_addr = NULL;
	u32 value, d0, d1;
	unsigned long reg, reg1, reg2;
	struct vbif_debug_bus_entry *head;
	dma_addr_t dma = 0;
	int i, list_size = 0;
	void __iomem *mem_base = NULL;
	struct vbif_debug_bus_entry *dbg_bus;
	u32 bus_size;
	struct dpu_dbg_reg_base *reg_base;

	if (!bus || !bus->cmn.entries_size)
		return;

	list_for_each_entry(reg_base, &dpu_dbg_base.reg_base_list,
			reg_base_head)
		if (strlen(reg_base->name) &&
			!strcmp(reg_base->name, bus->cmn.name))
			mem_base = reg_base->base;

	if (!mem_base) {
		pr_err("unable to find mem_base for %s\n", bus->cmn.name);
		return;
	}

	dbg_bus = bus->entries;
	bus_size = bus->cmn.entries_size;
	list_size = bus->cmn.entries_size;
	dump_mem = &bus->cmn.dumped_content;

	dev_info(dpu_dbg_base.dev, "======== start %s dump =========\n",
			bus->cmn.name);

	if (!dump_mem || !dbg_bus || !bus_size || !list_size)
		return;

	/* allocate memory for each test point */
	for (i = 0; i < bus_size; i++) {
		head = dbg_bus + i;
		list_size += (head->block_cnt * head->test_pnt_cnt);
	}

	/* 4 bytes * 4 entries for each test point*/
	list_size *= 16;

	in_log = (bus->cmn.enable_mask & DPU_DBG_DUMP_IN_LOG);
	in_mem = (bus->cmn.enable_mask & DPU_DBG_DUMP_IN_MEM);

	if (!in_log && !in_mem)
		return;

	if (in_mem) {
		if (!(*dump_mem))
			*dump_mem = dma_alloc_coherent(dpu_dbg_base.dev,
				list_size, &dma, GFP_KERNEL);

		if (*dump_mem) {
			dump_addr = *dump_mem;
			dev_info(dpu_dbg_base.dev,
				"%s: start_addr:0x%pK len:0x%x\n",
				__func__, dump_addr, list_size);
		} else {
			in_mem = false;
			pr_err("dump_mem: allocation fails\n");
		}
	}

	_dpu_dbg_enable_power(true);

	value = readl_relaxed(mem_base + MMSS_VBIF_CLKON);
	writel_relaxed(value | BIT(1), mem_base + MMSS_VBIF_CLKON);

	/* make sure that vbif core is on */
	wmb();

	/**
	 * Extract VBIF error info based on XIN halt and error status.
	 * If the XIN client is not in HALT state, or an error is detected,
	 * then retrieve the VBIF error info for it.
	 */
	reg = readl_relaxed(mem_base + MMSS_VBIF_XIN_HALT_CTRL1);
	reg1 = readl_relaxed(mem_base + MMSS_VBIF_PND_ERR);
	reg2 = readl_relaxed(mem_base + MMSS_VBIF_SRC_ERR);
	dev_err(dpu_dbg_base.dev,
			"XIN HALT:0x%lX, PND ERR:0x%lX, SRC ERR:0x%lX\n",
			reg, reg1, reg2);
	reg >>= 16;
	reg &= ~(reg1 | reg2);
	for (i = 0; i < MMSS_VBIF_CLIENT_NUM; i++) {
		if (!test_bit(0, &reg)) {
			writel_relaxed(i, mem_base + MMSS_VBIF_ERR_INFO);
			/* make sure reg write goes through */
			wmb();

			d0 = readl_relaxed(mem_base + MMSS_VBIF_ERR_INFO);
			d1 = readl_relaxed(mem_base + MMSS_VBIF_ERR_INFO_1);

			dev_err(dpu_dbg_base.dev,
					"Client:%d, errinfo=0x%X, errinfo1=0x%X\n",
					i, d0, d1);
		}
		reg >>= 1;
	}

	for (i = 0; i < bus_size; i++) {
		head = dbg_bus + i;

		writel_relaxed(0, mem_base + head->disable_bus_addr);
		writel_relaxed(BIT(0), mem_base + MMSS_VBIF_TEST_BUS_OUT_CTRL);
		/* make sure that other bus is off */
		wmb();

		_dpu_dbg_dump_vbif_debug_bus_entry(head, mem_base, dump_addr,
				in_log);
		if (dump_addr)
			dump_addr += (head->block_cnt * head->test_pnt_cnt * 4);
	}

	_dpu_dbg_enable_power(false);

	dev_info(dpu_dbg_base.dev, "======== end %s dump =========\n",
			bus->cmn.name);
}

/**
 * _dpu_dump_array - dump array of register bases
 * @name: string indicating origin of dump
 * @dump_dbgbus_dpu: whether to dump the dpu debug bus
 * @dump_dbgbus_vbif_rt: whether to dump the vbif rt debug bus
 */
static void _dpu_dump_array(const char *name, bool dump_dbgbus_dpu,
			    bool dump_dbgbus_vbif_rt)
{
	if (dump_dbgbus_dpu)
		_dpu_dbg_dump_dpu_dbg_bus(&dpu_dbg_base.dbgbus_dpu);

	if (dump_dbgbus_vbif_rt)
		_dpu_dbg_dump_vbif_dbg_bus(&dpu_dbg_base.dbgbus_vbif_rt);
}

/**
 * _dpu_dump_work - deferred dump work function
 * @work: work structure
 */
static void _dpu_dump_work(struct work_struct *work)
{
	_dpu_dump_array("dpudump_workitem",
		dpu_dbg_base.dbgbus_dpu.cmn.include_in_deferred_work,
		dpu_dbg_base.dbgbus_vbif_rt.cmn.include_in_deferred_work);
}

void dpu_dbg_dump(bool queue_work, const char *name, bool dump_dbgbus_dpu,
		  bool dump_dbgbus_vbif_rt)
{
	if (queue_work && work_pending(&dpu_dbg_base.dump_work))
		return;

	if (!queue_work) {
		_dpu_dump_array(name, dump_dbgbus_dpu, dump_dbgbus_vbif_rt);
		return;
	}

	/* schedule work to dump later */
	dpu_dbg_base.dbgbus_dpu.cmn.include_in_deferred_work = dump_dbgbus_dpu;
	dpu_dbg_base.dbgbus_vbif_rt.cmn.include_in_deferred_work =
			dump_dbgbus_vbif_rt;
	schedule_work(&dpu_dbg_base.dump_work);
}

/*
 * dpu_dbg_debugfs_open - debugfs open handler for debug dump
 * @inode: debugfs inode
 * @file: file handle
 */
static int dpu_dbg_debugfs_open(struct inode *inode, struct file *file)
{
	/* non-seekable */
	file->f_mode &= ~(FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);
	file->private_data = inode->i_private;
	return 0;
}

/**
 * dpu_dbg_dump_write - debugfs write handler for debug dump
 * @file: file handler
 * @user_buf: user buffer content from debugfs
 * @count: size of user buffer
 * @ppos: position offset of user buffer
 */
static ssize_t dpu_dbg_dump_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	_dpu_dump_array("dump_debugfs", true, true);
	return count;
}

static const struct file_operations dpu_dbg_dump_fops = {
	.open = dpu_dbg_debugfs_open,
	.write = dpu_dbg_dump_write,
};

int dpu_dbg_debugfs_register(struct dentry *debugfs_root)
{
	static struct dpu_dbg_base *dbg = &dpu_dbg_base;
	char debug_name[80] = "";

	if (!debugfs_root)
		return -EINVAL;

	debugfs_create_file("dump", 0600, debugfs_root, NULL,
			&dpu_dbg_dump_fops);

	if (dbg->dbgbus_dpu.entries) {
		dbg->dbgbus_dpu.cmn.name = DBGBUS_NAME_DPU;
		snprintf(debug_name, sizeof(debug_name), "%s_dbgbus",
				dbg->dbgbus_dpu.cmn.name);
		dbg->dbgbus_dpu.cmn.enable_mask = DEFAULT_DBGBUS_DPU;
		debugfs_create_u32(debug_name, 0600, debugfs_root,
				&dbg->dbgbus_dpu.cmn.enable_mask);
	}

	if (dbg->dbgbus_vbif_rt.entries) {
		dbg->dbgbus_vbif_rt.cmn.name = DBGBUS_NAME_VBIF_RT;
		snprintf(debug_name, sizeof(debug_name), "%s_dbgbus",
				dbg->dbgbus_vbif_rt.cmn.name);
		dbg->dbgbus_vbif_rt.cmn.enable_mask = DEFAULT_DBGBUS_VBIFRT;
		debugfs_create_u32(debug_name, 0600, debugfs_root,
				&dbg->dbgbus_vbif_rt.cmn.enable_mask);
	}

	return 0;
}

static void _dpu_dbg_debugfs_destroy(void)
{
}

void dpu_dbg_init_dbg_buses(u32 hwversion)
{
	static struct dpu_dbg_base *dbg = &dpu_dbg_base;

	memset(&dbg->dbgbus_dpu, 0, sizeof(dbg->dbgbus_dpu));
	memset(&dbg->dbgbus_vbif_rt, 0, sizeof(dbg->dbgbus_vbif_rt));

	if (IS_MSM8998_TARGET(hwversion)) {
		dbg->dbgbus_dpu.entries = dbg_bus_dpu_8998;
		dbg->dbgbus_dpu.cmn.entries_size = ARRAY_SIZE(dbg_bus_dpu_8998);
		dbg->dbgbus_dpu.cmn.flags = DBGBUS_FLAGS_DSPP;

		dbg->dbgbus_vbif_rt.entries = vbif_dbg_bus_msm8998;
		dbg->dbgbus_vbif_rt.cmn.entries_size =
				ARRAY_SIZE(vbif_dbg_bus_msm8998);
	} else if (IS_SDM845_TARGET(hwversion) || IS_SDM670_TARGET(hwversion)) {
		dbg->dbgbus_dpu.entries = dbg_bus_dpu_sdm845;
		dbg->dbgbus_dpu.cmn.entries_size =
				ARRAY_SIZE(dbg_bus_dpu_sdm845);
		dbg->dbgbus_dpu.cmn.flags = DBGBUS_FLAGS_DSPP;

		/* vbif is unchanged vs 8998 */
		dbg->dbgbus_vbif_rt.entries = vbif_dbg_bus_msm8998;
		dbg->dbgbus_vbif_rt.cmn.entries_size =
				ARRAY_SIZE(vbif_dbg_bus_msm8998);
	} else {
		pr_err("unsupported chipset id %X\n", hwversion);
	}
}

int dpu_dbg_init(struct device *dev)
{
	if (!dev) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	INIT_LIST_HEAD(&dpu_dbg_base.reg_base_list);
	dpu_dbg_base.dev = dev;

	INIT_WORK(&dpu_dbg_base.dump_work, _dpu_dump_work);

	return 0;
}

/**
 * dpu_dbg_destroy - destroy dpu debug facilities
 */
void dpu_dbg_destroy(void)
{
	_dpu_dbg_debugfs_destroy();
}

void dpu_dbg_set_dpu_top_offset(u32 blk_off)
{
	dpu_dbg_base.dbgbus_dpu.top_blk_off = blk_off;
}
