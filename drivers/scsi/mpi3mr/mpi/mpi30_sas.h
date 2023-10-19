/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright 2016-2022 Broadcom Inc. All rights reserved.
 */
#ifndef MPI30_SAS_H
#define MPI30_SAS_H     1
#define MPI3_SAS_DEVICE_INFO_SSP_TARGET             (0x00000100)
#define MPI3_SAS_DEVICE_INFO_STP_SATA_TARGET        (0x00000080)
#define MPI3_SAS_DEVICE_INFO_SMP_TARGET             (0x00000040)
#define MPI3_SAS_DEVICE_INFO_SSP_INITIATOR          (0x00000020)
#define MPI3_SAS_DEVICE_INFO_STP_INITIATOR          (0x00000010)
#define MPI3_SAS_DEVICE_INFO_SMP_INITIATOR          (0x00000008)
#define MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_MASK       (0x00000007)
#define MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_NO_DEVICE  (0x00000000)
#define MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_END_DEVICE (0x00000001)
#define MPI3_SAS_DEVICE_INFO_DEVICE_TYPE_EXPANDER   (0x00000002)
struct mpi3_smp_passthrough_request {
	__le16                     host_tag;
	u8                         ioc_use_only02;
	u8                         function;
	__le16                     ioc_use_only04;
	u8                         ioc_use_only06;
	u8                         msg_flags;
	__le16                     change_count;
	u8                         reserved0a;
	u8                         io_unit_port;
	__le32                     reserved0c[3];
	__le64                     sas_address;
	struct mpi3_sge_common         request_sge;
	struct mpi3_sge_common         response_sge;
};

struct mpi3_smp_passthrough_reply {
	__le16                     host_tag;
	u8                         ioc_use_only02;
	u8                         function;
	__le16                     ioc_use_only04;
	u8                         ioc_use_only06;
	u8                         msg_flags;
	__le16                     ioc_use_only08;
	__le16                     ioc_status;
	__le32                     ioc_log_info;
	__le16                     response_data_length;
	__le16                     reserved12;
};
#endif
