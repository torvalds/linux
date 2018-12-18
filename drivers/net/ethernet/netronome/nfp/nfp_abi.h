/* SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause) */
/*
 * Copyright (C) 2018 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
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

#ifndef __NFP_ABI__
#define __NFP_ABI__ 1

#include <linux/types.h>

#define NFP_MBOX_SYM_NAME		"_abi_nfd_pf%u_mbox"
#define NFP_MBOX_SYM_MIN_SIZE		16 /* When no data needed */

#define NFP_MBOX_CMD		0x00
#define NFP_MBOX_RET		0x04
#define NFP_MBOX_DATA_LEN	0x08
#define NFP_MBOX_RESERVED	0x0c
#define NFP_MBOX_DATA		0x10

/**
 * enum nfp_mbox_cmd - PF mailbox commands
 *
 * @NFP_MBOX_NO_CMD:	null command
 * Used to indicate previous command has finished.
 *
 * @NFP_MBOX_POOL_GET:	get shared buffer pool info/config
 * Input  - struct nfp_shared_buf_pool_id
 * Output - struct nfp_shared_buf_pool_info_get
 *
 * @NFP_MBOX_POOL_SET:	set shared buffer pool info/config
 * Input  - struct nfp_shared_buf_pool_info_set
 * Output - None
 *
 * @NFP_MBOX_PCIE_ABM_ENABLE:	enable PCIe-side advanced buffer management
 * Enable advanced buffer management of the PCIe block.  If ABM is disabled
 * PCIe block maintains a very short queue of buffers and does tail drop.
 * ABM allows more advanced buffering and priority control.
 * Input  - None
 * Output - None
 *
 * @NFP_MBOX_PCIE_ABM_DISABLE:	disable PCIe-side advanced buffer management
 * Input  - None
 * Output - None
 */
enum nfp_mbox_cmd {
	NFP_MBOX_NO_CMD			= 0x00,

	NFP_MBOX_POOL_GET		= 0x01,
	NFP_MBOX_POOL_SET		= 0x02,

	NFP_MBOX_PCIE_ABM_ENABLE	= 0x03,
	NFP_MBOX_PCIE_ABM_DISABLE	= 0x04,
};

#define NFP_SHARED_BUF_COUNT_SYM_NAME	"_abi_nfd_pf%u_sb_cnt"
#define NFP_SHARED_BUF_TABLE_SYM_NAME	"_abi_nfd_pf%u_sb_tbl"

/**
 * struct nfp_shared_buf - NFP shared buffer description
 * @id:				numerical user-visible id of the shared buffer
 * @size:			size in bytes of the buffer
 * @ingress_pools_count:	number of ingress pools
 * @egress_pools_count:		number of egress pools
 * @ingress_tc_count:		number of ingress trafic classes
 * @egress_tc_count:		number of egress trafic classes
 * @pool_size_unit:		pool size may be in credits, each credit is
 *				@pool_size_unit bytes
 */
struct nfp_shared_buf {
	__le32 id;
	__le32 size;
	__le16 ingress_pools_count;
	__le16 egress_pools_count;
	__le16 ingress_tc_count;
	__le16 egress_tc_count;

	__le32 pool_size_unit;
};

/**
 * struct nfp_shared_buf_pool_id - shared buffer pool identification
 * @shared_buf:		shared buffer id
 * @pool:		pool index
 */
struct nfp_shared_buf_pool_id {
	__le32 shared_buf;
	__le32 pool;
};

/**
 * struct nfp_shared_buf_pool_info_get - struct devlink_sb_pool_info mirror
 * @pool_type:		one of enum devlink_sb_pool_type
 * @size:		pool size in units of SB's @pool_size_unit
 * @threshold_type:	one of enum devlink_sb_threshold_type
 */
struct nfp_shared_buf_pool_info_get {
	__le32 pool_type;
	__le32 size;
	__le32 threshold_type;
};

/**
 * struct nfp_shared_buf_pool_info_set - packed args of sb_pool_set
 * @id:			pool identification info
 * @size:		pool size in units of SB's @pool_size_unit
 * @threshold_type:	one of enum devlink_sb_threshold_type
 */
struct nfp_shared_buf_pool_info_set {
	struct nfp_shared_buf_pool_id id;
	__le32 size;
	__le32 threshold_type;
};

#endif
