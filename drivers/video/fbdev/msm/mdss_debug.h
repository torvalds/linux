/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2012-2018, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef MDSS_DEBUG_H
#define MDSS_DEBUG_H

#include <linux/stdarg.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/mdss_io_util.h>

#include "mdss.h"
#include "mdss_mdp_trace.h"

#define MISR_POLL_SLEEP		2000
#define MISR_POLL_TIMEOUT	32000
#define MISR_CRC_BATCH_CFG	0x101
#define DATA_LIMITER (-1)
#define XLOG_TOUT_DATA_LIMITER (NULL)
#define XLOG_FUNC_ENTRY	0x1111
#define XLOG_FUNC_EXIT	0x2222
#define MDSS_REG_BLOCK_NAME_LEN (5)

#define DEBUG_FLAGS_DSPP BIT(0)

enum mdss_dbg_reg_dump_flag {
	MDSS_DBG_DUMP_IN_LOG = BIT(0),
	MDSS_DBG_DUMP_IN_MEM = BIT(1),
};

enum mdss_dbg_xlog_flag {
	MDSS_XLOG_DEFAULT = BIT(0),
	MDSS_XLOG_IOMMU = BIT(1),
	MDSS_XLOG_DBG = BIT(6),
	MDSS_XLOG_ALL = BIT(7)
};

#define TEST_MASK(id, tp)	((id << 4) | (tp << 1) | BIT(0))
struct debug_bus {
	u32 wr_addr;
	u32 block_id;
	u32 test_id;
};

struct vbif_debug_bus {
	u32 disable_bus_addr;
	u32 block_bus_addr;
	u32 bit_offset;
	u32 block_cnt;
	u32 test_pnt_start;
	u32 test_pnt_cnt;
};

#define MDSS_XLOG(...) mdss_xlog(__func__, __LINE__, MDSS_XLOG_DEFAULT, \
		##__VA_ARGS__, DATA_LIMITER)

#define MDSS_XLOG_TOUT_HANDLER(...)	\
	mdss_xlog_tout_handler_default(false, __func__, ##__VA_ARGS__, \
		XLOG_TOUT_DATA_LIMITER)

#define MDSS_XLOG_TOUT_HANDLER_WQ(...)	\
	mdss_xlog_tout_handler_default(true, __func__, ##__VA_ARGS__, \
		XLOG_TOUT_DATA_LIMITER)

#define MDSS_XLOG_DBG(...) mdss_xlog(__func__, __LINE__, MDSS_XLOG_DBG, \
		##__VA_ARGS__, DATA_LIMITER)

#define MDSS_XLOG_ALL(...) mdss_xlog(__func__, __LINE__, MDSS_XLOG_ALL,	\
		##__VA_ARGS__, DATA_LIMITER)

#define MDSS_XLOG_IOMMU(...) mdss_xlog(__func__, __LINE__, MDSS_XLOG_IOMMU, \
		##__VA_ARGS__, DATA_LIMITER)

#define ATRACE_END(name) trace_tracing_mark_write(current->tgid, name, 0)
#define ATRACE_BEGIN(name) trace_tracing_mark_write(current->tgid, name, 1)
#define ATRACE_FUNC() ATRACE_BEGIN(__func__)

#define ATRACE_INT(name, value) \
	trace_mdp_trace_counter(current->tgid, name, value)

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_FB_MSM_MDSS)

#define MDSS_DEBUG_BASE_MAX 10

struct mdss_debug_base {
	struct list_head head; /* head of this node */
	struct list_head dump_list; /* head to the list with dump ranges */
	struct mdss_debug_data *mdd;
	char name[80];
	void __iomem *base;
	size_t off;
	size_t cnt;
	u8 cmd_data_type;
	size_t max_offset;
	char *buf;
	size_t buf_len;
	u32 *reg_dump; /* address for the mem dump if no ranges used */
};

struct mdss_debug_data {
	struct dentry *root;
	struct dentry *perf;
	struct dentry *bordercolor;
	struct dentry *postproc;
	struct list_head base_list;
};

struct dump_offset {
	u32 start;
	u32 end;
};

struct range_dump_node {
	struct list_head head; /* head of this node */
	u32 *reg_dump; /* address for the mem dump */
	char range_name[40]; /* name of this range */
	struct dump_offset offset; /* range to dump */
	uint32_t xin_id; /* client xin id */
};

int mdss_debugfs_init(struct mdss_data_type *mdata);
int mdss_debugfs_remove(struct mdss_data_type *mdata);
int mdss_debug_register_base(const char *name, void __iomem *base,
	size_t max_offset, struct mdss_debug_base **dbg_blk);
void mdss_debug_register_dump_range(struct platform_device *pdev,
	struct mdss_debug_base *blk_base, const char *ranges_prop,
	const char *name_prop, const char *xin_prop);
int panel_debug_register_base(const char *name, void __iomem *base,
				    size_t max_offset);
int mdss_misr_set(struct mdss_data_type *mdata,
			struct mdp_misr *req,
			struct mdss_mdp_ctl *ctl);
int mdss_misr_get(struct mdss_data_type *mdata,
			struct mdp_misr *resp,
			struct mdss_mdp_ctl *ctl,
			bool is_video_mode);
void mdss_misr_disable(struct mdss_data_type *mdata,
			struct mdp_misr *req,
			struct mdss_mdp_ctl *ctl);
void mdss_misr_crc_collect(struct mdss_data_type *mdata, int block_id,
	bool is_video_mode);

int mdss_create_xlog_debug(struct mdss_debug_data *mdd);
void mdss_xlog(const char *name, int line, int flag, ...);
void mdss_xlog_tout_handler_default(bool queue, const char *name, ...);
u32 get_dump_range(struct dump_offset *range_node, size_t max_offset);
void mdss_dump_reg(const char *dump_name, u32 reg_dump_flag, char *addr,
	int len, u32 **dump_mem, bool from_isr);
void mdss_mdp_debug_mid(u32 mid);
void mdss_dump_dsi_debug_bus(u32 bus_dump_flag, u32 **dump_mem);
#else
struct mdss_debug_base;
struct dump_offset;

static inline int mdss_debugfs_init(struct mdss_data_type *mdata) { return 0; }
static inline int mdss_debugfs_remove(struct mdss_data_type *mdata)
{
	return 0;
}
static inline int mdss_debug_register_base(const char *name, void __iomem *base,
	size_t max_offset, struct mdss_debug_base **dbg_blk) { return 0; }
static inline void mdss_debug_register_dump_range(struct platform_device *pdev,
	struct mdss_debug_base *blk_base, const char *ranges_prop,
	const char *name_prop, const char *xin_prop) { }
static inline int panel_debug_register_base(const char *name,
					void __iomem *base,
					size_t max_offset)
{ return 0; }
static inline int mdss_misr_set(struct mdss_data_type *mdata,
					struct mdp_misr *req,
					struct mdss_mdp_ctl *ctl)
{ return 0; }
static inline int mdss_misr_get(struct mdss_data_type *mdata,
					struct mdp_misr *resp,
					struct mdss_mdp_ctl *ctl,
					bool is_video_mode)
{ return 0; }
static inline void mdss_misr_disable(struct mdss_data_type *mdata,
					struct mdp_misr *req,
					struct mdss_mdp_ctl *ctl)
{ return; }

static inline void mdss_misr_crc_collect(struct mdss_data_type *mdata,
					int block_id, bool is_video_mode) { }

static inline int create_xlog_debug(struct mdss_data_type *mdata) { return 0; }
static inline void mdss_xlog_dump(void) { }
static inline void mdss_xlog(const char *name, int line, int flag, ...) { }

static inline void mdss_dsi_debug_check_te(struct mdss_panel_data *pdata) { }
static inline void mdss_xlog_tout_handler_default(bool queue,
	const char *name, ...) { }
static inline u32 get_dump_range(struct dump_offset *range_node,
						size_t max_offset)
{
return 0;
}
static inline void mdss_dump_reg(const char *dump_name, u32 reg_dump_flag,
			char *addr, int len, u32 **dump_mem, bool from_isr)
{ }
static inline void mdss_mdp_debug_mid(u32 mid) { }
#endif

int mdss_dump_misr_data(char **buf, u32 size);

static inline int mdss_debug_register_io(const char *name,
		struct dss_io_data *io_data, struct mdss_debug_base **dbg_blk)
{
	return mdss_debug_register_base(name, io_data->base, io_data->len,
		dbg_blk);
}

#endif /* MDSS_DEBUG_H */
