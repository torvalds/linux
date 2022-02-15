/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright 2016-2021 Broadcom Inc. All rights reserved.
 *
 */
#ifndef MPI30_PCI_H
#define MPI30_PCI_H     1
#ifndef MPI3_NVME_ENCAP_CMD_MAX
#define MPI3_NVME_ENCAP_CMD_MAX               (1)
#endif
struct mpi3_nvme_encapsulated_request {
	__le16                     host_tag;
	u8                         ioc_use_only02;
	u8                         function;
	__le16                     ioc_use_only04;
	u8                         ioc_use_only06;
	u8                         msg_flags;
	__le16                     change_count;
	__le16                     dev_handle;
	__le16                     encapsulated_command_length;
	__le16                     flags;
	__le32                     reserved10[4];
	__le32                     command[MPI3_NVME_ENCAP_CMD_MAX];
};

#define MPI3_NVME_FLAGS_FORCE_ADMIN_ERR_REPLY_MASK      (0x0002)
#define MPI3_NVME_FLAGS_FORCE_ADMIN_ERR_REPLY_FAIL_ONLY (0x0000)
#define MPI3_NVME_FLAGS_FORCE_ADMIN_ERR_REPLY_ALL       (0x0002)
#define MPI3_NVME_FLAGS_SUBMISSIONQ_MASK                (0x0001)
#define MPI3_NVME_FLAGS_SUBMISSIONQ_IO                  (0x0000)
#define MPI3_NVME_FLAGS_SUBMISSIONQ_ADMIN               (0x0001)
struct mpi3_nvme_encapsulated_error_reply {
	__le16                     host_tag;
	u8                         ioc_use_only02;
	u8                         function;
	__le16                     ioc_use_only04;
	u8                         ioc_use_only06;
	u8                         msg_flags;
	__le16                     ioc_use_only08;
	__le16                     ioc_status;
	__le32                     ioc_log_info;
	__le32                     nvme_completion_entry[4];
};
#endif
