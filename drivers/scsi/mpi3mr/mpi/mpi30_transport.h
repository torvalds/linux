/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright 2016-2023 Broadcom Inc. All rights reserved.
 */
#ifndef MPI30_TRANSPORT_H
#define MPI30_TRANSPORT_H     1
struct mpi3_version_struct {
	u8         dev;
	u8         unit;
	u8         minor;
	u8         major;
};

union mpi3_version_union {
	struct mpi3_version_struct     mpi3_version;
	__le32                     word;
};

#define MPI3_VERSION_MAJOR                                              (3)
#define MPI3_VERSION_MINOR                                              (0)
#define MPI3_VERSION_UNIT                                               (31)
#define MPI3_VERSION_DEV                                                (0)
#define MPI3_DEVHANDLE_INVALID                                          (0xffff)
struct mpi3_sysif_oper_queue_indexes {
	__le16         producer_index;
	__le16         reserved02;
	__le16         consumer_index;
	__le16         reserved06;
};

struct mpi3_sysif_registers {
	__le64                             ioc_information;
	union mpi3_version_union              version;
	__le32                             reserved0c[2];
	__le32                             ioc_configuration;
	__le32                             reserved18;
	__le32                             ioc_status;
	__le32                             reserved20;
	__le32                             admin_queue_num_entries;
	__le64                             admin_request_queue_address;
	__le64                             admin_reply_queue_address;
	__le32                             reserved38[2];
	__le32                             coalesce_control;
	__le32                             reserved44[1007];
	__le16                             admin_request_queue_pi;
	__le16                             reserved1002;
	__le16                             admin_reply_queue_ci;
	__le16                             reserved1006;
	struct mpi3_sysif_oper_queue_indexes   oper_queue_indexes[383];
	__le32                             reserved1c00;
	__le32                             write_sequence;
	__le32                             host_diagnostic;
	__le32                             reserved1c0c;
	__le32                             fault;
	__le32                             fault_info[3];
	__le32                             reserved1c20[4];
	__le64                             hcb_address;
	__le32                             hcb_size;
	__le32                             reserved1c3c;
	__le32                             reply_free_host_index;
	__le32                             sense_buffer_free_host_index;
	__le32                             reserved1c48[2];
	__le64                             diag_rw_data;
	__le64                             diag_rw_address;
	__le16                             diag_rw_control;
	__le16                             diag_rw_status;
	__le32                             reserved1c64[35];
	__le32                             scratchpad[4];
	__le32                             reserved1d00[192];
	__le32                             device_assigned_registers[2048];
};

#define MPI3_SYSIF_IOC_INFO_LOW_OFFSET                                  (0x00000000)
#define MPI3_SYSIF_IOC_INFO_HIGH_OFFSET                                 (0x00000004)
#define MPI3_SYSIF_IOC_INFO_LOW_TIMEOUT_MASK                            (0xff000000)
#define MPI3_SYSIF_IOC_INFO_LOW_TIMEOUT_SHIFT                           (24)
#define MPI3_SYSIF_IOC_INFO_LOW_HCB_DISABLED                            (0x00000001)
#define MPI3_SYSIF_IOC_CONFIG_OFFSET                                    (0x00000014)
#define MPI3_SYSIF_IOC_CONFIG_OPER_RPY_ENT_SZ                           (0x00f00000)
#define MPI3_SYSIF_IOC_CONFIG_OPER_RPY_ENT_SZ_SHIFT                     (20)
#define MPI3_SYSIF_IOC_CONFIG_OPER_REQ_ENT_SZ                           (0x000f0000)
#define MPI3_SYSIF_IOC_CONFIG_OPER_REQ_ENT_SZ_SHIFT                     (16)
#define MPI3_SYSIF_IOC_CONFIG_SHUTDOWN_MASK                             (0x0000c000)
#define MPI3_SYSIF_IOC_CONFIG_SHUTDOWN_NO                               (0x00000000)
#define MPI3_SYSIF_IOC_CONFIG_SHUTDOWN_NORMAL                           (0x00004000)
#define MPI3_SYSIF_IOC_CONFIG_DEVICE_SHUTDOWN_SEND_REQ                  (0x00002000)
#define MPI3_SYSIF_IOC_CONFIG_DIAG_SAVE                                 (0x00000010)
#define MPI3_SYSIF_IOC_CONFIG_ENABLE_IOC                                (0x00000001)
#define MPI3_SYSIF_IOC_STATUS_OFFSET                                    (0x0000001c)
#define MPI3_SYSIF_IOC_STATUS_RESET_HISTORY                             (0x00000010)
#define MPI3_SYSIF_IOC_STATUS_SHUTDOWN_MASK                             (0x0000000c)
#define MPI3_SYSIF_IOC_STATUS_SHUTDOWN_SHIFT                            (0x00000002)
#define MPI3_SYSIF_IOC_STATUS_SHUTDOWN_NONE                             (0x00000000)
#define MPI3_SYSIF_IOC_STATUS_SHUTDOWN_IN_PROGRESS                      (0x00000004)
#define MPI3_SYSIF_IOC_STATUS_SHUTDOWN_COMPLETE                         (0x00000008)
#define MPI3_SYSIF_IOC_STATUS_FAULT                                     (0x00000002)
#define MPI3_SYSIF_IOC_STATUS_READY                                     (0x00000001)
#define MPI3_SYSIF_ADMIN_Q_NUM_ENTRIES_OFFSET                           (0x00000024)
#define MPI3_SYSIF_ADMIN_Q_NUM_ENTRIES_REQ_MASK                         (0x0fff)
#define MPI3_SYSIF_ADMIN_Q_NUM_ENTRIES_REPLY_OFFSET                     (0x00000026)
#define MPI3_SYSIF_ADMIN_Q_NUM_ENTRIES_REPLY_MASK                       (0x0fff0000)
#define MPI3_SYSIF_ADMIN_Q_NUM_ENTRIES_REPLY_SHIFT                      (16)
#define MPI3_SYSIF_ADMIN_REQ_Q_ADDR_LOW_OFFSET                          (0x00000028)
#define MPI3_SYSIF_ADMIN_REQ_Q_ADDR_HIGH_OFFSET                         (0x0000002c)
#define MPI3_SYSIF_ADMIN_REPLY_Q_ADDR_LOW_OFFSET                        (0x00000030)
#define MPI3_SYSIF_ADMIN_REPLY_Q_ADDR_HIGH_OFFSET                       (0x00000034)
#define MPI3_SYSIF_COALESCE_CONTROL_OFFSET                              (0x00000040)
#define MPI3_SYSIF_COALESCE_CONTROL_ENABLE_MASK                         (0xc0000000)
#define MPI3_SYSIF_COALESCE_CONTROL_ENABLE_NO_CHANGE                    (0x00000000)
#define MPI3_SYSIF_COALESCE_CONTROL_ENABLE_DISABLE                      (0x40000000)
#define MPI3_SYSIF_COALESCE_CONTROL_ENABLE_ENABLE                       (0xc0000000)
#define MPI3_SYSIF_COALESCE_CONTROL_VALID                               (0x20000000)
#define MPI3_SYSIF_COALESCE_CONTROL_MSIX_IDX_MASK                       (0x01ff0000)
#define MPI3_SYSIF_COALESCE_CONTROL_MSIX_IDX_SHIFT                      (16)
#define MPI3_SYSIF_COALESCE_CONTROL_TIMEOUT_MASK                        (0x0000ff00)
#define MPI3_SYSIF_COALESCE_CONTROL_TIMEOUT_SHIFT                       (8)
#define MPI3_SYSIF_COALESCE_CONTROL_DEPTH_MASK                          (0x000000ff)
#define MPI3_SYSIF_COALESCE_CONTROL_DEPTH_SHIFT                         (0)
#define MPI3_SYSIF_ADMIN_REQ_Q_PI_OFFSET                                (0x00001000)
#define MPI3_SYSIF_ADMIN_REPLY_Q_CI_OFFSET                              (0x00001004)
#define MPI3_SYSIF_OPER_REQ_Q_PI_OFFSET                                 (0x00001008)
#define MPI3_SYSIF_OPER_REQ_Q_N_PI_OFFSET(N)                            (MPI3_SYSIF_OPER_REQ_Q_PI_OFFSET + (((N) - 1) * 8))
#define MPI3_SYSIF_OPER_REPLY_Q_CI_OFFSET                               (0x0000100c)
#define MPI3_SYSIF_OPER_REPLY_Q_N_CI_OFFSET(N)                          (MPI3_SYSIF_OPER_REPLY_Q_CI_OFFSET + (((N) - 1) * 8))
#define MPI3_SYSIF_WRITE_SEQUENCE_OFFSET                                (0x00001c04)
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_MASK                        (0x0000000f)
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_FLUSH                       (0x0)
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_1ST                         (0xf)
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_2ND                         (0x4)
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_3RD                         (0xb)
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_4TH                         (0x2)
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_5TH                         (0x7)
#define MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_6TH                         (0xd)
#define MPI3_SYSIF_HOST_DIAG_OFFSET                                     (0x00001c08)
#define MPI3_SYSIF_HOST_DIAG_RESET_ACTION_MASK                          (0x00000700)
#define MPI3_SYSIF_HOST_DIAG_RESET_ACTION_NO_RESET                      (0x00000000)
#define MPI3_SYSIF_HOST_DIAG_RESET_ACTION_SOFT_RESET                    (0x00000100)
#define MPI3_SYSIF_HOST_DIAG_RESET_ACTION_HOST_CONTROL_BOOT_RESET       (0x00000200)
#define MPI3_SYSIF_HOST_DIAG_RESET_ACTION_COMPLETE_RESET                (0x00000300)
#define MPI3_SYSIF_HOST_DIAG_RESET_ACTION_DIAG_FAULT                    (0x00000700)
#define MPI3_SYSIF_HOST_DIAG_SAVE_IN_PROGRESS                           (0x00000080)
#define MPI3_SYSIF_HOST_DIAG_SECURE_BOOT                                (0x00000040)
#define MPI3_SYSIF_HOST_DIAG_CLEAR_INVALID_FW_IMAGE                     (0x00000020)
#define MPI3_SYSIF_HOST_DIAG_INVALID_FW_IMAGE                           (0x00000010)
#define MPI3_SYSIF_HOST_DIAG_HCBENABLE                                  (0x00000008)
#define MPI3_SYSIF_HOST_DIAG_HCBMODE                                    (0x00000004)
#define MPI3_SYSIF_HOST_DIAG_DIAG_RW_ENABLE                             (0x00000002)
#define MPI3_SYSIF_HOST_DIAG_DIAG_WRITE_ENABLE                          (0x00000001)
#define MPI3_SYSIF_FAULT_OFFSET                                         (0x00001c10)
#define MPI3_SYSIF_FAULT_FUNC_AREA_MASK                                 (0xff000000)
#define MPI3_SYSIF_FAULT_FUNC_AREA_SHIFT                                (24)
#define MPI3_SYSIF_FAULT_FUNC_AREA_MPI_DEFINED                          (0x00000000)
#define MPI3_SYSIF_FAULT_CODE_MASK                                      (0x0000ffff)
#define MPI3_SYSIF_FAULT_CODE_DIAG_FAULT_RESET                          (0x0000f000)
#define MPI3_SYSIF_FAULT_CODE_CI_ACTIVATION_RESET                       (0x0000f001)
#define MPI3_SYSIF_FAULT_CODE_SOFT_RESET_IN_PROGRESS                    (0x0000f002)
#define MPI3_SYSIF_FAULT_CODE_COMPLETE_RESET_NEEDED                     (0x0000f003)
#define MPI3_SYSIF_FAULT_CODE_SOFT_RESET_NEEDED                         (0x0000f004)
#define MPI3_SYSIF_FAULT_CODE_POWER_CYCLE_REQUIRED                      (0x0000f005)
#define MPI3_SYSIF_FAULT_CODE_TEMP_THRESHOLD_EXCEEDED                   (0x0000f006)
#define MPI3_SYSIF_FAULT_INFO0_OFFSET                                   (0x00001c14)
#define MPI3_SYSIF_FAULT_INFO1_OFFSET                                   (0x00001c18)
#define MPI3_SYSIF_FAULT_INFO2_OFFSET                                   (0x00001c1c)
#define MPI3_SYSIF_HCB_ADDRESS_LOW_OFFSET                               (0x00001c30)
#define MPI3_SYSIF_HCB_ADDRESS_HIGH_OFFSET                              (0x00001c34)
#define MPI3_SYSIF_HCB_SIZE_OFFSET                                      (0x00001c38)
#define MPI3_SYSIF_HCB_SIZE_SIZE_MASK                                   (0xfffff000)
#define MPI3_SYSIF_HCB_SIZE_SIZE_SHIFT                                  (12)
#define MPI3_SYSIF_HCB_SIZE_HCDW_ENABLE                                 (0x00000001)
#define MPI3_SYSIF_REPLY_FREE_HOST_INDEX_OFFSET                         (0x00001c40)
#define MPI3_SYSIF_SENSE_BUF_FREE_HOST_INDEX_OFFSET                     (0x00001c44)
#define MPI3_SYSIF_DIAG_RW_DATA_LOW_OFFSET                              (0x00001c50)
#define MPI3_SYSIF_DIAG_RW_DATA_HIGH_OFFSET                             (0x00001c54)
#define MPI3_SYSIF_DIAG_RW_ADDRESS_LOW_OFFSET                           (0x00001c58)
#define MPI3_SYSIF_DIAG_RW_ADDRESS_HIGH_OFFSET                          (0x00001c5c)
#define MPI3_SYSIF_DIAG_RW_CONTROL_OFFSET                               (0x00001c60)
#define MPI3_SYSIF_DIAG_RW_CONTROL_LEN_MASK                             (0x00000030)
#define MPI3_SYSIF_DIAG_RW_CONTROL_LEN_1BYTE                            (0x00000000)
#define MPI3_SYSIF_DIAG_RW_CONTROL_LEN_2BYTES                           (0x00000010)
#define MPI3_SYSIF_DIAG_RW_CONTROL_LEN_4BYTES                           (0x00000020)
#define MPI3_SYSIF_DIAG_RW_CONTROL_LEN_8BYTES                           (0x00000030)
#define MPI3_SYSIF_DIAG_RW_CONTROL_RESET                                (0x00000004)
#define MPI3_SYSIF_DIAG_RW_CONTROL_DIR_MASK                             (0x00000002)
#define MPI3_SYSIF_DIAG_RW_CONTROL_DIR_READ                             (0x00000000)
#define MPI3_SYSIF_DIAG_RW_CONTROL_DIR_WRITE                            (0x00000002)
#define MPI3_SYSIF_DIAG_RW_CONTROL_START                                (0x00000001)
#define MPI3_SYSIF_DIAG_RW_STATUS_OFFSET                                (0x00001c62)
#define MPI3_SYSIF_DIAG_RW_STATUS_STATUS_MASK                           (0x0000000e)
#define MPI3_SYSIF_DIAG_RW_STATUS_STATUS_SUCCESS                        (0x00000000)
#define MPI3_SYSIF_DIAG_RW_STATUS_STATUS_INV_ADDR                       (0x00000002)
#define MPI3_SYSIF_DIAG_RW_STATUS_STATUS_ACC_ERR                        (0x00000004)
#define MPI3_SYSIF_DIAG_RW_STATUS_STATUS_PAR_ERR                        (0x00000006)
#define MPI3_SYSIF_DIAG_RW_STATUS_BUSY                                  (0x00000001)
#define MPI3_SYSIF_SCRATCHPAD0_OFFSET                                   (0x00001cf0)
#define MPI3_SYSIF_SCRATCHPAD1_OFFSET                                   (0x00001cf4)
#define MPI3_SYSIF_SCRATCHPAD2_OFFSET                                   (0x00001cf8)
#define MPI3_SYSIF_SCRATCHPAD3_OFFSET                                   (0x00001cfc)
#define MPI3_SYSIF_DEVICE_ASSIGNED_REGS_OFFSET                          (0x00002000)
#define MPI3_SYSIF_DIAG_SAVE_TIMEOUT                                    (60)
struct mpi3_default_reply_descriptor {
	__le32             descriptor_type_dependent1[2];
	__le16             request_queue_ci;
	__le16             request_queue_id;
	__le16             descriptor_type_dependent2;
	__le16             reply_flags;
};

#define MPI3_REPLY_DESCRIPT_FLAGS_PHASE_MASK                       (0x0001)
#define MPI3_REPLY_DESCRIPT_FLAGS_TYPE_MASK                        (0xf000)
#define MPI3_REPLY_DESCRIPT_FLAGS_TYPE_ADDRESS_REPLY               (0x0000)
#define MPI3_REPLY_DESCRIPT_FLAGS_TYPE_SUCCESS                     (0x1000)
#define MPI3_REPLY_DESCRIPT_FLAGS_TYPE_TARGET_COMMAND_BUFFER       (0x2000)
#define MPI3_REPLY_DESCRIPT_FLAGS_TYPE_STATUS                      (0x3000)
#define MPI3_REPLY_DESCRIPT_REQUEST_QUEUE_ID_INVALID               (0xffff)
struct mpi3_address_reply_descriptor {
	__le64             reply_frame_address;
	__le16             request_queue_ci;
	__le16             request_queue_id;
	__le16             reserved0c;
	__le16             reply_flags;
};

struct mpi3_success_reply_descriptor {
	__le32             reserved00[2];
	__le16             request_queue_ci;
	__le16             request_queue_id;
	__le16             host_tag;
	__le16             reply_flags;
};

struct mpi3_target_command_buffer_reply_descriptor {
	__le32             reserved00;
	__le16             initiator_dev_handle;
	u8                 phy_num;
	u8                 reserved07;
	__le16             request_queue_ci;
	__le16             request_queue_id;
	__le16             io_index;
	__le16             reply_flags;
};

struct mpi3_status_reply_descriptor {
	__le16             ioc_status;
	__le16             reserved02;
	__le32             ioc_log_info;
	__le16             request_queue_ci;
	__le16             request_queue_id;
	__le16             host_tag;
	__le16             reply_flags;
};

#define MPI3_REPLY_DESCRIPT_STATUS_IOCSTATUS_LOGINFOAVAIL               (0x8000)
#define MPI3_REPLY_DESCRIPT_STATUS_IOCSTATUS_STATUS_MASK                (0x7fff)
#define MPI3_REPLY_DESCRIPT_STATUS_IOCLOGINFO_TYPE_MASK                 (0xf0000000)
#define MPI3_REPLY_DESCRIPT_STATUS_IOCLOGINFO_TYPE_NO_INFO              (0x00000000)
#define MPI3_REPLY_DESCRIPT_STATUS_IOCLOGINFO_TYPE_SAS                  (0x30000000)
#define MPI3_REPLY_DESCRIPT_STATUS_IOCLOGINFO_DATA_MASK                 (0x0fffffff)
union mpi3_reply_descriptors_union {
	struct mpi3_default_reply_descriptor               default_reply;
	struct mpi3_address_reply_descriptor               address_reply;
	struct mpi3_success_reply_descriptor               success;
	struct mpi3_target_command_buffer_reply_descriptor target_command_buffer;
	struct mpi3_status_reply_descriptor                status;
	__le32                                         words[4];
};

struct mpi3_sge_common {
	__le64             address;
	__le32             length;
	u8                 reserved0c[3];
	u8                 flags;
};

struct mpi3_sge_bit_bucket {
	__le64             reserved00;
	__le32             length;
	u8                 reserved0c[3];
	u8                 flags;
};

struct mpi3_sge_extended_eedp {
	u8                 user_data_size;
	u8                 reserved01;
	__le16             eedp_flags;
	__le32             secondary_reference_tag;
	__le16             secondary_application_tag;
	__le16             application_tag_translation_mask;
	__le16             reserved0c;
	u8                 extended_operation;
	u8                 flags;
};

union mpi3_sge_union {
	struct mpi3_sge_common                 simple;
	struct mpi3_sge_common                  chain;
	struct mpi3_sge_common             last_chain;
	struct mpi3_sge_bit_bucket             bit_bucket;
	struct mpi3_sge_extended_eedp          eedp;
	__le32                             words[4];
};

#define MPI3_SGE_FLAGS_ELEMENT_TYPE_MASK        (0xf0)
#define MPI3_SGE_FLAGS_ELEMENT_TYPE_SIMPLE      (0x00)
#define MPI3_SGE_FLAGS_ELEMENT_TYPE_BIT_BUCKET  (0x10)
#define MPI3_SGE_FLAGS_ELEMENT_TYPE_CHAIN       (0x20)
#define MPI3_SGE_FLAGS_ELEMENT_TYPE_LAST_CHAIN  (0x30)
#define MPI3_SGE_FLAGS_ELEMENT_TYPE_EXTENDED    (0xf0)
#define MPI3_SGE_FLAGS_END_OF_LIST              (0x08)
#define MPI3_SGE_FLAGS_END_OF_BUFFER            (0x04)
#define MPI3_SGE_FLAGS_DLAS_MASK                (0x03)
#define MPI3_SGE_FLAGS_DLAS_SYSTEM              (0x00)
#define MPI3_SGE_FLAGS_DLAS_IOC_UDP             (0x01)
#define MPI3_SGE_FLAGS_DLAS_IOC_CTL             (0x02)
#define MPI3_SGE_EXT_OPER_EEDP                  (0x00)
#define MPI3_EEDPFLAGS_INCR_PRI_REF_TAG             (0x8000)
#define MPI3_EEDPFLAGS_INCR_SEC_REF_TAG             (0x4000)
#define MPI3_EEDPFLAGS_INCR_PRI_APP_TAG             (0x2000)
#define MPI3_EEDPFLAGS_INCR_SEC_APP_TAG             (0x1000)
#define MPI3_EEDPFLAGS_ESC_PASSTHROUGH              (0x0800)
#define MPI3_EEDPFLAGS_CHK_REF_TAG                  (0x0400)
#define MPI3_EEDPFLAGS_CHK_APP_TAG                  (0x0200)
#define MPI3_EEDPFLAGS_CHK_GUARD                    (0x0100)
#define MPI3_EEDPFLAGS_ESC_MODE_MASK                (0x00c0)
#define MPI3_EEDPFLAGS_ESC_MODE_DO_NOT_DISABLE      (0x0040)
#define MPI3_EEDPFLAGS_ESC_MODE_APPTAG_DISABLE      (0x0080)
#define MPI3_EEDPFLAGS_ESC_MODE_APPTAG_REFTAG_DISABLE   (0x00c0)
#define MPI3_EEDPFLAGS_HOST_GUARD_MASK              (0x0030)
#define MPI3_EEDPFLAGS_HOST_GUARD_T10_CRC           (0x0000)
#define MPI3_EEDPFLAGS_HOST_GUARD_IP_CHKSUM         (0x0010)
#define MPI3_EEDPFLAGS_HOST_GUARD_OEM_SPECIFIC      (0x0020)
#define MPI3_EEDPFLAGS_PT_REF_TAG                   (0x0008)
#define MPI3_EEDPFLAGS_EEDP_OP_MASK                 (0x0007)
#define MPI3_EEDPFLAGS_EEDP_OP_CHECK                (0x0001)
#define MPI3_EEDPFLAGS_EEDP_OP_STRIP                (0x0002)
#define MPI3_EEDPFLAGS_EEDP_OP_CHECK_REMOVE         (0x0003)
#define MPI3_EEDPFLAGS_EEDP_OP_INSERT               (0x0004)
#define MPI3_EEDPFLAGS_EEDP_OP_REPLACE              (0x0006)
#define MPI3_EEDPFLAGS_EEDP_OP_CHECK_REGEN          (0x0007)
#define MPI3_EEDP_UDS_512                           (0x01)
#define MPI3_EEDP_UDS_520                           (0x02)
#define MPI3_EEDP_UDS_4080                          (0x03)
#define MPI3_EEDP_UDS_4088                          (0x04)
#define MPI3_EEDP_UDS_4096                          (0x05)
#define MPI3_EEDP_UDS_4104                          (0x06)
#define MPI3_EEDP_UDS_4160                          (0x07)
struct mpi3_request_header {
	__le16             host_tag;
	u8                 ioc_use_only02;
	u8                 function;
	__le16             ioc_use_only04;
	u8                 ioc_use_only06;
	u8                 msg_flags;
	__le16             change_count;
	__le16             function_dependent;
};

struct mpi3_default_reply {
	__le16             host_tag;
	u8                 ioc_use_only02;
	u8                 function;
	__le16             ioc_use_only04;
	u8                 ioc_use_only06;
	u8                 msg_flags;
	__le16             ioc_use_only08;
	__le16             ioc_status;
	__le32             ioc_log_info;
};

#define MPI3_HOST_TAG_INVALID                       (0xffff)
#define MPI3_FUNCTION_IOC_FACTS                     (0x01)
#define MPI3_FUNCTION_IOC_INIT                      (0x02)
#define MPI3_FUNCTION_PORT_ENABLE                   (0x03)
#define MPI3_FUNCTION_EVENT_NOTIFICATION            (0x04)
#define MPI3_FUNCTION_EVENT_ACK                     (0x05)
#define MPI3_FUNCTION_CI_DOWNLOAD                   (0x06)
#define MPI3_FUNCTION_CI_UPLOAD                     (0x07)
#define MPI3_FUNCTION_IO_UNIT_CONTROL               (0x08)
#define MPI3_FUNCTION_PERSISTENT_EVENT_LOG          (0x09)
#define MPI3_FUNCTION_MGMT_PASSTHROUGH              (0x0a)
#define MPI3_FUNCTION_CONFIG                        (0x10)
#define MPI3_FUNCTION_SCSI_IO                       (0x20)
#define MPI3_FUNCTION_SCSI_TASK_MGMT                (0x21)
#define MPI3_FUNCTION_SMP_PASSTHROUGH               (0x22)
#define MPI3_FUNCTION_NVME_ENCAPSULATED             (0x24)
#define MPI3_FUNCTION_TARGET_ASSIST                 (0x30)
#define MPI3_FUNCTION_TARGET_STATUS_SEND            (0x31)
#define MPI3_FUNCTION_TARGET_MODE_ABORT             (0x32)
#define MPI3_FUNCTION_TARGET_CMD_BUF_POST_BASE      (0x33)
#define MPI3_FUNCTION_TARGET_CMD_BUF_POST_LIST      (0x34)
#define MPI3_FUNCTION_CREATE_REQUEST_QUEUE          (0x70)
#define MPI3_FUNCTION_DELETE_REQUEST_QUEUE          (0x71)
#define MPI3_FUNCTION_CREATE_REPLY_QUEUE            (0x72)
#define MPI3_FUNCTION_DELETE_REPLY_QUEUE            (0x73)
#define MPI3_FUNCTION_TOOLBOX                       (0x80)
#define MPI3_FUNCTION_DIAG_BUFFER_POST              (0x81)
#define MPI3_FUNCTION_DIAG_BUFFER_MANAGE            (0x82)
#define MPI3_FUNCTION_DIAG_BUFFER_UPLOAD            (0x83)
#define MPI3_FUNCTION_MIN_IOC_USE_ONLY              (0xc0)
#define MPI3_FUNCTION_MAX_IOC_USE_ONLY              (0xef)
#define MPI3_FUNCTION_MIN_PRODUCT_SPECIFIC          (0xf0)
#define MPI3_FUNCTION_MAX_PRODUCT_SPECIFIC          (0xff)
#define MPI3_IOCSTATUS_LOG_INFO_AVAIL_MASK          (0x8000)
#define MPI3_IOCSTATUS_LOG_INFO_AVAILABLE           (0x8000)
#define MPI3_IOCSTATUS_STATUS_MASK                  (0x7fff)
#define MPI3_IOCSTATUS_SUCCESS                      (0x0000)
#define MPI3_IOCSTATUS_INVALID_FUNCTION             (0x0001)
#define MPI3_IOCSTATUS_BUSY                         (0x0002)
#define MPI3_IOCSTATUS_INVALID_SGL                  (0x0003)
#define MPI3_IOCSTATUS_INTERNAL_ERROR               (0x0004)
#define MPI3_IOCSTATUS_INSUFFICIENT_RESOURCES       (0x0006)
#define MPI3_IOCSTATUS_INVALID_FIELD                (0x0007)
#define MPI3_IOCSTATUS_INVALID_STATE                (0x0008)
#define MPI3_IOCSTATUS_INSUFFICIENT_POWER           (0x000a)
#define MPI3_IOCSTATUS_INVALID_CHANGE_COUNT         (0x000b)
#define MPI3_IOCSTATUS_ALLOWED_CMD_BLOCK            (0x000c)
#define MPI3_IOCSTATUS_SUPERVISOR_ONLY              (0x000d)
#define MPI3_IOCSTATUS_FAILURE                      (0x001f)
#define MPI3_IOCSTATUS_CONFIG_INVALID_ACTION        (0x0020)
#define MPI3_IOCSTATUS_CONFIG_INVALID_TYPE          (0x0021)
#define MPI3_IOCSTATUS_CONFIG_INVALID_PAGE          (0x0022)
#define MPI3_IOCSTATUS_CONFIG_INVALID_DATA          (0x0023)
#define MPI3_IOCSTATUS_CONFIG_NO_DEFAULTS           (0x0024)
#define MPI3_IOCSTATUS_CONFIG_CANT_COMMIT           (0x0025)
#define MPI3_IOCSTATUS_SCSI_RECOVERED_ERROR         (0x0040)
#define MPI3_IOCSTATUS_SCSI_TM_NOT_SUPPORTED        (0x0041)
#define MPI3_IOCSTATUS_SCSI_INVALID_DEVHANDLE       (0x0042)
#define MPI3_IOCSTATUS_SCSI_DEVICE_NOT_THERE        (0x0043)
#define MPI3_IOCSTATUS_SCSI_DATA_OVERRUN            (0x0044)
#define MPI3_IOCSTATUS_SCSI_DATA_UNDERRUN           (0x0045)
#define MPI3_IOCSTATUS_SCSI_IO_DATA_ERROR           (0x0046)
#define MPI3_IOCSTATUS_SCSI_PROTOCOL_ERROR          (0x0047)
#define MPI3_IOCSTATUS_SCSI_TASK_TERMINATED         (0x0048)
#define MPI3_IOCSTATUS_SCSI_RESIDUAL_MISMATCH       (0x0049)
#define MPI3_IOCSTATUS_SCSI_TASK_MGMT_FAILED        (0x004a)
#define MPI3_IOCSTATUS_SCSI_IOC_TERMINATED          (0x004b)
#define MPI3_IOCSTATUS_SCSI_EXT_TERMINATED          (0x004c)
#define MPI3_IOCSTATUS_EEDP_GUARD_ERROR             (0x004d)
#define MPI3_IOCSTATUS_EEDP_REF_TAG_ERROR           (0x004e)
#define MPI3_IOCSTATUS_EEDP_APP_TAG_ERROR           (0x004f)
#define MPI3_IOCSTATUS_TARGET_INVALID_IO_INDEX      (0x0062)
#define MPI3_IOCSTATUS_TARGET_ABORTED               (0x0063)
#define MPI3_IOCSTATUS_TARGET_NO_CONN_RETRYABLE     (0x0064)
#define MPI3_IOCSTATUS_TARGET_NO_CONNECTION         (0x0065)
#define MPI3_IOCSTATUS_TARGET_XFER_COUNT_MISMATCH   (0x006a)
#define MPI3_IOCSTATUS_TARGET_DATA_OFFSET_ERROR     (0x006d)
#define MPI3_IOCSTATUS_TARGET_TOO_MUCH_WRITE_DATA   (0x006e)
#define MPI3_IOCSTATUS_TARGET_IU_TOO_SHORT          (0x006f)
#define MPI3_IOCSTATUS_TARGET_ACK_NAK_TIMEOUT       (0x0070)
#define MPI3_IOCSTATUS_TARGET_NAK_RECEIVED          (0x0071)
#define MPI3_IOCSTATUS_SAS_SMP_REQUEST_FAILED       (0x0090)
#define MPI3_IOCSTATUS_SAS_SMP_DATA_OVERRUN         (0x0091)
#define MPI3_IOCSTATUS_DIAGNOSTIC_RELEASED          (0x00a0)
#define MPI3_IOCSTATUS_CI_UNSUPPORTED               (0x00b0)
#define MPI3_IOCSTATUS_CI_UPDATE_SEQUENCE           (0x00b1)
#define MPI3_IOCSTATUS_CI_VALIDATION_FAILED         (0x00b2)
#define MPI3_IOCSTATUS_CI_KEY_UPDATE_PENDING        (0x00b3)
#define MPI3_IOCSTATUS_CI_KEY_UPDATE_NOT_POSSIBLE   (0x00b4)
#define MPI3_IOCSTATUS_SECURITY_KEY_REQUIRED        (0x00c0)
#define MPI3_IOCSTATUS_SECURITY_VIOLATION           (0x00c1)
#define MPI3_IOCSTATUS_INVALID_QUEUE_ID             (0x0f00)
#define MPI3_IOCSTATUS_INVALID_QUEUE_SIZE           (0x0f01)
#define MPI3_IOCSTATUS_INVALID_MSIX_VECTOR          (0x0f02)
#define MPI3_IOCSTATUS_INVALID_REPLY_QUEUE_ID       (0x0f03)
#define MPI3_IOCSTATUS_INVALID_QUEUE_DELETION       (0x0f04)
#define MPI3_IOCLOGINFO_TYPE_MASK               (0xf0000000)
#define MPI3_IOCLOGINFO_TYPE_SHIFT              (28)
#define MPI3_IOCLOGINFO_TYPE_NONE               (0x0)
#define MPI3_IOCLOGINFO_TYPE_SAS                (0x3)
#define MPI3_IOCLOGINFO_LOG_DATA_MASK           (0x0fffffff)
#endif
