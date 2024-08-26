/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright 2016-2024 Broadcom Inc. All rights reserved.
 */
#ifndef MPI30_TOOL_H
#define MPI30_TOOL_H     1

#define MPI3_DIAG_BUFFER_TYPE_TRACE	(0x01)
#define MPI3_DIAG_BUFFER_TYPE_FW	(0x02)
#define MPI3_DIAG_BUFFER_ACTION_RELEASE	(0x01)

struct mpi3_diag_buffer_post_request {
	__le16                     host_tag;
	u8                         ioc_use_only02;
	u8                         function;
	__le16                     ioc_use_only04;
	u8                         ioc_use_only06;
	u8                         msg_flags;
	__le16                     change_count;
	__le16                     reserved0a;
	u8                         type;
	u8                         reserved0d;
	__le16                     reserved0e;
	__le64                     address;
	__le32                     length;
	__le32                     reserved1c;
};

struct mpi3_diag_buffer_manage_request {
	__le16                     host_tag;
	u8                         ioc_use_only02;
	u8                         function;
	__le16                     ioc_use_only04;
	u8                         ioc_use_only06;
	u8                         msg_flags;
	__le16                     change_count;
	__le16                     reserved0a;
	u8                         type;
	u8                         action;
	__le16                     reserved0e;
};


#endif
