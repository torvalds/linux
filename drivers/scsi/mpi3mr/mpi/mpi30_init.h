/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright 2016-2023 Broadcom Inc. All rights reserved.
 */
#ifndef MPI30_INIT_H
#define MPI30_INIT_H     1
struct mpi3_scsi_io_cdb_eedp32 {
	u8                 cdb[20];
	__be32             primary_reference_tag;
	__le16             primary_application_tag;
	__le16             primary_application_tag_mask;
	__le32             transfer_length;
};

union mpi3_scsi_io_cdb_union {
	u8                         cdb32[32];
	struct mpi3_scsi_io_cdb_eedp32 eedp32;
	struct mpi3_sge_common         sge;
};

struct mpi3_scsi_io_request {
	__le16                     host_tag;
	u8                         ioc_use_only02;
	u8                         function;
	__le16                     ioc_use_only04;
	u8                         ioc_use_only06;
	u8                         msg_flags;
	__le16                     change_count;
	__le16                     dev_handle;
	__le32                     flags;
	__le32                     skip_count;
	__le32                     data_length;
	u8                         lun[8];
	union mpi3_scsi_io_cdb_union  cdb;
	union mpi3_sge_union          sgl[4];
};

#define MPI3_SCSIIO_MSGFLAGS_METASGL_VALID                  (0x80)
#define MPI3_SCSIIO_MSGFLAGS_DIVERT_TO_FIRMWARE             (0x40)
#define MPI3_SCSIIO_FLAGS_LARGE_CDB                         (0x60000000)
#define MPI3_SCSIIO_FLAGS_CDB_16_OR_LESS                    (0x00000000)
#define MPI3_SCSIIO_FLAGS_CDB_GREATER_THAN_16               (0x20000000)
#define MPI3_SCSIIO_FLAGS_CDB_IN_SEPARATE_BUFFER            (0x40000000)
#define MPI3_SCSIIO_FLAGS_TASKATTRIBUTE_MASK                (0x07000000)
#define MPI3_SCSIIO_FLAGS_TASKATTRIBUTE_SIMPLEQ             (0x00000000)
#define MPI3_SCSIIO_FLAGS_TASKATTRIBUTE_HEADOFQ             (0x01000000)
#define MPI3_SCSIIO_FLAGS_TASKATTRIBUTE_ORDEREDQ            (0x02000000)
#define MPI3_SCSIIO_FLAGS_TASKATTRIBUTE_ACAQ                (0x04000000)
#define MPI3_SCSIIO_FLAGS_CMDPRI_MASK                       (0x00f00000)
#define MPI3_SCSIIO_FLAGS_CMDPRI_SHIFT                      (20)
#define MPI3_SCSIIO_FLAGS_DATADIRECTION_MASK                (0x000c0000)
#define MPI3_SCSIIO_FLAGS_DATADIRECTION_NO_DATA_TRANSFER    (0x00000000)
#define MPI3_SCSIIO_FLAGS_DATADIRECTION_WRITE               (0x00040000)
#define MPI3_SCSIIO_FLAGS_DATADIRECTION_READ                (0x00080000)
#define MPI3_SCSIIO_FLAGS_DMAOPERATION_MASK                 (0x00030000)
#define MPI3_SCSIIO_FLAGS_DMAOPERATION_HOST_PI              (0x00010000)
#define MPI3_SCSIIO_FLAGS_DIVERT_REASON_MASK                (0x000000f0)
#define MPI3_SCSIIO_FLAGS_DIVERT_REASON_IO_THROTTLING       (0x00000010)
#define MPI3_SCSIIO_FLAGS_DIVERT_REASON_WRITE_SAME_TOO_LARGE (0x00000020)
#define MPI3_SCSIIO_FLAGS_DIVERT_REASON_PROD_SPECIFIC       (0x00000080)
#define MPI3_SCSIIO_METASGL_INDEX                           (3)
struct mpi3_scsi_io_reply {
	__le16                     host_tag;
	u8                         ioc_use_only02;
	u8                         function;
	__le16                     ioc_use_only04;
	u8                         ioc_use_only06;
	u8                         msg_flags;
	__le16                     ioc_use_only08;
	__le16                     ioc_status;
	__le32                     ioc_log_info;
	u8                         scsi_status;
	u8                         scsi_state;
	__le16                     dev_handle;
	__le32                     transfer_count;
	__le32                     sense_count;
	__le32                     response_data;
	__le16                     task_tag;
	__le16                     scsi_status_qualifier;
	__le32                     eedp_error_offset;
	__le16                     eedp_observed_app_tag;
	__le16                     eedp_observed_guard;
	__le32                     eedp_observed_ref_tag;
	__le64                     sense_data_buffer_address;
};

#define MPI3_SCSIIO_REPLY_MSGFLAGS_REFTAG_OBSERVED_VALID        (0x01)
#define MPI3_SCSIIO_REPLY_MSGFLAGS_APPTAG_OBSERVED_VALID        (0x02)
#define MPI3_SCSIIO_REPLY_MSGFLAGS_GUARD_OBSERVED_VALID         (0x04)
#define MPI3_SCSI_STATUS_GOOD                   (0x00)
#define MPI3_SCSI_STATUS_CHECK_CONDITION        (0x02)
#define MPI3_SCSI_STATUS_CONDITION_MET          (0x04)
#define MPI3_SCSI_STATUS_BUSY                   (0x08)
#define MPI3_SCSI_STATUS_INTERMEDIATE           (0x10)
#define MPI3_SCSI_STATUS_INTERMEDIATE_CONDMET   (0x14)
#define MPI3_SCSI_STATUS_RESERVATION_CONFLICT   (0x18)
#define MPI3_SCSI_STATUS_COMMAND_TERMINATED     (0x22)
#define MPI3_SCSI_STATUS_TASK_SET_FULL          (0x28)
#define MPI3_SCSI_STATUS_ACA_ACTIVE             (0x30)
#define MPI3_SCSI_STATUS_TASK_ABORTED           (0x40)
#define MPI3_SCSI_STATE_SENSE_MASK              (0x03)
#define MPI3_SCSI_STATE_SENSE_VALID             (0x00)
#define MPI3_SCSI_STATE_SENSE_FAILED            (0x01)
#define MPI3_SCSI_STATE_SENSE_BUFF_Q_EMPTY      (0x02)
#define MPI3_SCSI_STATE_SENSE_NOT_AVAILABLE     (0x03)
#define MPI3_SCSI_STATE_NO_SCSI_STATUS          (0x04)
#define MPI3_SCSI_STATE_TERMINATED              (0x08)
#define MPI3_SCSI_STATE_RESPONSE_DATA_VALID     (0x10)
#define MPI3_SCSI_RSP_RESPONSECODE_MASK         (0x000000ff)
#define MPI3_SCSI_RSP_RESPONSECODE_SHIFT        (0)
#define MPI3_SCSI_RSP_ARI2_MASK                 (0x0000ff00)
#define MPI3_SCSI_RSP_ARI2_SHIFT                (8)
#define MPI3_SCSI_RSP_ARI1_MASK                 (0x00ff0000)
#define MPI3_SCSI_RSP_ARI1_SHIFT                (16)
#define MPI3_SCSI_RSP_ARI0_MASK                 (0xff000000)
#define MPI3_SCSI_RSP_ARI0_SHIFT                (24)
#define MPI3_SCSI_TASKTAG_UNKNOWN               (0xffff)
#define MPI3_SCSITASKMGMT_MSGFLAGS_DO_NOT_SEND_TASK_IU      (0x08)
#define MPI3_SCSITASKMGMT_TASKTYPE_ABORT_TASK               (0x01)
#define MPI3_SCSITASKMGMT_TASKTYPE_ABORT_TASK_SET           (0x02)
#define MPI3_SCSITASKMGMT_TASKTYPE_TARGET_RESET             (0x03)
#define MPI3_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET       (0x05)
#define MPI3_SCSITASKMGMT_TASKTYPE_CLEAR_TASK_SET           (0x06)
#define MPI3_SCSITASKMGMT_TASKTYPE_QUERY_TASK               (0x07)
#define MPI3_SCSITASKMGMT_TASKTYPE_CLEAR_ACA                (0x08)
#define MPI3_SCSITASKMGMT_TASKTYPE_QUERY_TASK_SET           (0x09)
#define MPI3_SCSITASKMGMT_TASKTYPE_QUERY_ASYNC_EVENT        (0x0a)
#define MPI3_SCSITASKMGMT_TASKTYPE_I_T_NEXUS_RESET          (0x0b)
#define MPI3_SCSITASKMGMT_RSPCODE_TM_COMPLETE                (0x00)
#define MPI3_SCSITASKMGMT_RSPCODE_INVALID_FRAME              (0x02)
#define MPI3_SCSITASKMGMT_RSPCODE_TM_FUNCTION_NOT_SUPPORTED  (0x04)
#define MPI3_SCSITASKMGMT_RSPCODE_TM_FAILED                  (0x05)
#define MPI3_SCSITASKMGMT_RSPCODE_TM_SUCCEEDED               (0x08)
#define MPI3_SCSITASKMGMT_RSPCODE_TM_INVALID_LUN             (0x09)
#define MPI3_SCSITASKMGMT_RSPCODE_TM_OVERLAPPED_TAG          (0x0a)
#define MPI3_SCSITASKMGMT_RSPCODE_IO_QUEUED_ON_IOC           (0x80)
#define MPI3_SCSITASKMGMT_RSPCODE_TM_NVME_DENIED             (0x81)
#endif
