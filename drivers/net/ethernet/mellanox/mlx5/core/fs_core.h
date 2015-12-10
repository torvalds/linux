/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _MLX5_FS_CORE_
#define _MLX5_FS_CORE_

#include <linux/mlx5/fs.h>

enum fs_node_type {
	FS_TYPE_NAMESPACE,
	FS_TYPE_PRIO,
	FS_TYPE_FLOW_TABLE,
	FS_TYPE_FLOW_GROUP,
	FS_TYPE_FLOW_ENTRY,
	FS_TYPE_FLOW_DEST
};

enum fs_flow_table_type {
	FS_FT_NIC_RX	 = 0x0,
};

enum fs_fte_status {
	FS_FTE_STATUS_EXISTING = 1UL << 0,
};

struct fs_node {
	struct list_head	list;
	struct list_head	children;
	enum fs_node_type	type;
};

struct mlx5_flow_rule {
	struct fs_node				node;
	struct mlx5_flow_destination		dest_attr;
};

struct mlx5_flow_table {
	struct fs_node			node;
	u32				id;
	enum fs_flow_table_type		type;
};

struct fs_fte {
	struct fs_node			node;
	u32				val[MLX5_ST_SZ_DW(fte_match_param)];
	u32				dests_size;
	u32				flow_tag;
	u32				index;
	u32				action;
};

#endif
