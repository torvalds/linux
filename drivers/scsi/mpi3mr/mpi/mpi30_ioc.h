/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright 2016-2023 Broadcom Inc. All rights reserved.
 */
#ifndef MPI30_IOC_H
#define MPI30_IOC_H     1
struct mpi3_ioc_init_request {
	__le16                   host_tag;
	u8                       ioc_use_only02;
	u8                       function;
	__le16                   ioc_use_only04;
	u8                       ioc_use_only06;
	u8                       msg_flags;
	__le16                   change_count;
	__le16                   reserved0a;
	union mpi3_version_union    mpi_version;
	__le64                   time_stamp;
	u8                       reserved18;
	u8                       who_init;
	__le16                   reserved1a;
	__le16                   reply_free_queue_depth;
	__le16                   reserved1e;
	__le64                   reply_free_queue_address;
	__le32                   reserved28;
	__le16                   sense_buffer_free_queue_depth;
	__le16                   sense_buffer_length;
	__le64                   sense_buffer_free_queue_address;
	__le64                   driver_information_address;
};
#define MPI3_IOCINIT_MSGFLAGS_WRITESAMEDIVERT_SUPPORTED		(0x08)
#define MPI3_IOCINIT_MSGFLAGS_SCSIIOSTATUSREPLY_SUPPORTED	(0x04)
#define MPI3_IOCINIT_MSGFLAGS_HOSTMETADATA_MASK          (0x03)
#define MPI3_IOCINIT_MSGFLAGS_HOSTMETADATA_SHIFT	(0)
#define MPI3_IOCINIT_MSGFLAGS_HOSTMETADATA_NOT_USED      (0x00)
#define MPI3_IOCINIT_MSGFLAGS_HOSTMETADATA_SEPARATED     (0x01)
#define MPI3_IOCINIT_MSGFLAGS_HOSTMETADATA_INLINE        (0x02)
#define MPI3_IOCINIT_MSGFLAGS_HOSTMETADATA_BOTH          (0x03)
#define MPI3_WHOINIT_NOT_INITIALIZED                     (0x00)
#define MPI3_WHOINIT_ROM_BIOS                            (0x02)
#define MPI3_WHOINIT_HOST_DRIVER                         (0x03)
#define MPI3_WHOINIT_MANUFACTURER                        (0x04)

#define MPI3_IOCINIT_DRIVERCAP_OSEXPOSURE_MASK              (0x00000003)
#define MPI3_IOCINIT_DRIVERCAP_OSEXPOSURE_SHIFT		    (0)
#define MPI3_IOCINIT_DRIVERCAP_OSEXPOSURE_NO_GUIDANCE       (0x00000000)
#define MPI3_IOCINIT_DRIVERCAP_OSEXPOSURE_NO_SPECIAL        (0x00000001)
#define MPI3_IOCINIT_DRIVERCAP_OSEXPOSURE_REPORT_AS_HDD     (0x00000002)
#define MPI3_IOCINIT_DRIVERCAP_OSEXPOSURE_REPORT_AS_SSD     (0x00000003)

struct mpi3_ioc_facts_request {
	__le16                 host_tag;
	u8                     ioc_use_only02;
	u8                     function;
	__le16                 ioc_use_only04;
	u8                     ioc_use_only06;
	u8                     msg_flags;
	__le16                 change_count;
	__le16                 reserved0a;
	__le32                 reserved0c;
	union mpi3_sge_union      sgl;
};

struct mpi3_ioc_facts_data {
	__le16                     ioc_facts_data_length;
	__le16                     reserved02;
	union mpi3_version_union      mpi_version;
	struct mpi3_comp_image_version fw_version;
	__le32                     ioc_capabilities;
	u8                         ioc_number;
	u8                         who_init;
	__le16                     max_msix_vectors;
	__le16                     max_outstanding_requests;
	__le16                     product_id;
	__le16                     ioc_request_frame_size;
	__le16                     reply_frame_size;
	__le16                     ioc_exceptions;
	__le16                     max_persistent_id;
	u8                         sge_modifier_mask;
	u8                         sge_modifier_value;
	u8                         sge_modifier_shift;
	u8                         protocol_flags;
	__le16                     max_sas_initiators;
	__le16                     max_data_length;
	__le16                     max_sas_expanders;
	__le16                     max_enclosures;
	__le16                     min_dev_handle;
	__le16                     max_dev_handle;
	__le16                     max_pcie_switches;
	__le16                     max_nvme;
	__le16                     reserved38;
	__le16                     max_vds;
	__le16                     max_host_pds;
	__le16                     max_adv_host_pds;
	__le16                     max_raid_pds;
	__le16                     max_posted_cmd_buffers;
	__le32                     flags;
	__le16                     max_operational_request_queues;
	__le16                     max_operational_reply_queues;
	__le16                     shutdown_timeout;
	__le16                     reserved4e;
	__le32                     diag_trace_size;
	__le32                     diag_fw_size;
	__le32                     diag_driver_size;
	u8                         max_host_pd_ns_count;
	u8                         max_adv_host_pd_ns_count;
	u8                         max_raidpd_ns_count;
	u8                         max_devices_per_throttle_group;
	__le16                     io_throttle_data_length;
	__le16                     max_io_throttle_group;
	__le16                     io_throttle_low;
	__le16                     io_throttle_high;
	__le32			   diag_fdl_size;
	__le32			   diag_tty_size;
};
#define MPI3_IOCFACTS_CAPABILITY_NON_SUPERVISOR_MASK          (0x80000000)
#define MPI3_IOCFACTS_CAPABILITY_NON_SUPERVISOR_SHIFT		(31)
#define MPI3_IOCFACTS_CAPABILITY_SUPERVISOR_IOC               (0x00000000)
#define MPI3_IOCFACTS_CAPABILITY_NON_SUPERVISOR_IOC           (0x80000000)
#define MPI3_IOCFACTS_CAPABILITY_INT_COALESCE_MASK            (0x00000600)
#define MPI3_IOCFACTS_CAPABILITY_INT_COALESCE_SHIFT		(9)
#define MPI3_IOCFACTS_CAPABILITY_INT_COALESCE_FIXED_THRESHOLD (0x00000000)
#define MPI3_IOCFACTS_CAPABILITY_INT_COALESCE_OUTSTANDING_IO  (0x00000200)
#define MPI3_IOCFACTS_CAPABILITY_COMPLETE_RESET_SUPPORTED     (0x00000100)
#define MPI3_IOCFACTS_CAPABILITY_SEG_DIAG_TRACE_SUPPORTED     (0x00000080)
#define MPI3_IOCFACTS_CAPABILITY_SEG_DIAG_FW_SUPPORTED        (0x00000040)
#define MPI3_IOCFACTS_CAPABILITY_SEG_DIAG_DRIVER_SUPPORTED    (0x00000020)
#define MPI3_IOCFACTS_CAPABILITY_ADVANCED_HOST_PD_SUPPORTED   (0x00000010)
#define MPI3_IOCFACTS_CAPABILITY_RAID_SUPPORTED               (0x00000008)
#define MPI3_IOCFACTS_CAPABILITY_MULTIPATH_SUPPORTED          (0x00000002)
#define MPI3_IOCFACTS_CAPABILITY_COALESCE_CTRL_SUPPORTED      (0x00000001)
#define MPI3_IOCFACTS_PID_TYPE_MASK                           (0xf000)
#define MPI3_IOCFACTS_PID_TYPE_SHIFT                          (12)
#define MPI3_IOCFACTS_PID_PRODUCT_MASK                        (0x0f00)
#define MPI3_IOCFACTS_PID_PRODUCT_SHIFT                       (8)
#define MPI3_IOCFACTS_PID_FAMILY_MASK                         (0x00ff)
#define MPI3_IOCFACTS_PID_FAMILY_SHIFT                        (0)
#define MPI3_IOCFACTS_EXCEPT_SECURITY_REKEY                   (0x2000)
#define MPI3_IOCFACTS_EXCEPT_SAS_DISABLED                     (0x1000)
#define MPI3_IOCFACTS_EXCEPT_SAFE_MODE                        (0x0800)
#define MPI3_IOCFACTS_EXCEPT_SECURITY_KEY_MASK                (0x0700)
#define MPI3_IOCFACTS_EXCEPT_SECURITY_KEY_SHIFT			(8)
#define MPI3_IOCFACTS_EXCEPT_SECURITY_KEY_NONE                (0x0000)
#define MPI3_IOCFACTS_EXCEPT_SECURITY_KEY_LOCAL_VIA_MGMT      (0x0100)
#define MPI3_IOCFACTS_EXCEPT_SECURITY_KEY_EXT_VIA_MGMT        (0x0200)
#define MPI3_IOCFACTS_EXCEPT_SECURITY_KEY_DRIVE_EXT_VIA_MGMT  (0x0300)
#define MPI3_IOCFACTS_EXCEPT_SECURITY_KEY_LOCAL_VIA_OOB       (0x0400)
#define MPI3_IOCFACTS_EXCEPT_SECURITY_KEY_EXT_VIA_OOB         (0x0500)
#define MPI3_IOCFACTS_EXCEPT_SECURITY_KEY_DRIVE_EXT_VIA_OOB   (0x0600)
#define MPI3_IOCFACTS_EXCEPT_PCIE_DISABLED                    (0x0080)
#define MPI3_IOCFACTS_EXCEPT_PARTIAL_MEMORY_FAILURE           (0x0040)
#define MPI3_IOCFACTS_EXCEPT_MANUFACT_CHECKSUM_FAIL           (0x0020)
#define MPI3_IOCFACTS_EXCEPT_FW_CHECKSUM_FAIL                 (0x0010)
#define MPI3_IOCFACTS_EXCEPT_CONFIG_CHECKSUM_FAIL             (0x0008)
#define MPI3_IOCFACTS_EXCEPT_BLOCKING_BOOT_EVENT              (0x0004)
#define MPI3_IOCFACTS_EXCEPT_SECURITY_SELFTEST_FAILURE        (0x0002)
#define MPI3_IOCFACTS_EXCEPT_BOOTSTAT_MASK                    (0x0001)
#define MPI3_IOCFACTS_EXCEPT_BOOTSTAT_SHIFT			(0)
#define MPI3_IOCFACTS_EXCEPT_BOOTSTAT_PRIMARY                 (0x0000)
#define MPI3_IOCFACTS_EXCEPT_BOOTSTAT_SECONDARY               (0x0001)
#define MPI3_IOCFACTS_PROTOCOL_SAS                            (0x0010)
#define MPI3_IOCFACTS_PROTOCOL_SATA                           (0x0008)
#define MPI3_IOCFACTS_PROTOCOL_NVME                           (0x0004)
#define MPI3_IOCFACTS_PROTOCOL_SCSI_INITIATOR                 (0x0002)
#define MPI3_IOCFACTS_PROTOCOL_SCSI_TARGET                    (0x0001)
#define MPI3_IOCFACTS_MAX_DATA_LENGTH_NOT_REPORTED            (0x0000)
#define MPI3_IOCFACTS_FLAGS_SIGNED_NVDATA_REQUIRED            (0x00010000)
#define MPI3_IOCFACTS_FLAGS_DMA_ADDRESS_WIDTH_MASK            (0x0000ff00)
#define MPI3_IOCFACTS_FLAGS_DMA_ADDRESS_WIDTH_SHIFT           (8)
#define MPI3_IOCFACTS_FLAGS_INITIAL_PORT_ENABLE_MASK          (0x00000030)
#define MPI3_IOCFACTS_FLAGS_INITIAL_PORT_ENABLE_SHIFT		(4)
#define MPI3_IOCFACTS_FLAGS_INITIAL_PORT_ENABLE_NOT_STARTED   (0x00000000)
#define MPI3_IOCFACTS_FLAGS_INITIAL_PORT_ENABLE_IN_PROGRESS   (0x00000010)
#define MPI3_IOCFACTS_FLAGS_INITIAL_PORT_ENABLE_COMPLETE      (0x00000020)
#define MPI3_IOCFACTS_FLAGS_PERSONALITY_MASK                  (0x0000000f)
#define MPI3_IOCFACTS_FLAGS_PERSONALITY_SHIFT			(0)
#define MPI3_IOCFACTS_FLAGS_PERSONALITY_EHBA                  (0x00000000)
#define MPI3_IOCFACTS_FLAGS_PERSONALITY_RAID_DDR              (0x00000002)
#define MPI3_IOCFACTS_IO_THROTTLE_DATA_LENGTH_NOT_REQUIRED    (0x0000)
#define MPI3_IOCFACTS_MAX_IO_THROTTLE_GROUP_NOT_REQUIRED      (0x0000)
#define MPI3_IOCFACTS_DIAGFDLSIZE_NOT_SUPPORTED		      (0x00000000)
#define MPI3_IOCFACTS_DIAGTTYSIZE_NOT_SUPPORTED               (0x00000000)
struct mpi3_mgmt_passthrough_request {
	__le16                 host_tag;
	u8                     ioc_use_only02;
	u8                     function;
	__le16                 ioc_use_only04;
	u8                     ioc_use_only06;
	u8                     msg_flags;
	__le16                 change_count;
	__le16                 reserved0a;
	__le32                 reserved0c[5];
	union mpi3_sge_union      command_sgl;
	union mpi3_sge_union      response_sgl;
};

struct mpi3_create_request_queue_request {
	__le16             host_tag;
	u8                 ioc_use_only02;
	u8                 function;
	__le16             ioc_use_only04;
	u8                 ioc_use_only06;
	u8                 msg_flags;
	__le16             change_count;
	u8                 flags;
	u8                 burst;
	__le16             size;
	__le16             queue_id;
	__le16             reply_queue_id;
	__le16             reserved12;
	__le32             reserved14;
	__le64             base_address;
};

#define MPI3_CREATE_REQUEST_QUEUE_FLAGS_SEGMENTED_MASK          (0x80)
#define MPI3_CREATE_REQUEST_QUEUE_FLAGS_SEGMENTED_SHIFT		(7)
#define MPI3_CREATE_REQUEST_QUEUE_FLAGS_SEGMENTED_SEGMENTED     (0x80)
#define MPI3_CREATE_REQUEST_QUEUE_FLAGS_SEGMENTED_CONTIGUOUS    (0x00)
#define MPI3_CREATE_REQUEST_QUEUE_SIZE_MINIMUM                  (2)
struct mpi3_delete_request_queue_request {
	__le16             host_tag;
	u8                 ioc_use_only02;
	u8                 function;
	__le16             ioc_use_only04;
	u8                 ioc_use_only06;
	u8                 msg_flags;
	__le16             change_count;
	__le16             queue_id;
};

struct mpi3_create_reply_queue_request {
	__le16             host_tag;
	u8                 ioc_use_only02;
	u8                 function;
	__le16             ioc_use_only04;
	u8                 ioc_use_only06;
	u8                 msg_flags;
	__le16             change_count;
	u8                 flags;
	u8                 reserved0b;
	__le16             size;
	__le16             queue_id;
	__le16             msix_index;
	__le16             reserved12;
	__le32             reserved14;
	__le64             base_address;
};

#define MPI3_CREATE_REPLY_QUEUE_FLAGS_SEGMENTED_MASK            (0x80)
#define MPI3_CREATE_REPLY_QUEUE_FLAGS_SEGMENTED_SHIFT		(7)
#define MPI3_CREATE_REPLY_QUEUE_FLAGS_SEGMENTED_SEGMENTED       (0x80)
#define MPI3_CREATE_REPLY_QUEUE_FLAGS_SEGMENTED_CONTIGUOUS      (0x00)
#define MPI3_CREATE_REPLY_QUEUE_FLAGS_COALESCE_DISABLE          (0x02)
#define MPI3_CREATE_REPLY_QUEUE_FLAGS_INT_ENABLE_MASK           (0x01)
#define MPI3_CREATE_REPLY_QUEUE_FLAGS_INT_ENABLE_SHIFT		(0)
#define MPI3_CREATE_REPLY_QUEUE_FLAGS_INT_ENABLE_DISABLE        (0x00)
#define MPI3_CREATE_REPLY_QUEUE_FLAGS_INT_ENABLE_ENABLE         (0x01)
#define MPI3_CREATE_REPLY_QUEUE_SIZE_MINIMUM                    (2)
struct mpi3_delete_reply_queue_request {
	__le16             host_tag;
	u8                 ioc_use_only02;
	u8                 function;
	__le16             ioc_use_only04;
	u8                 ioc_use_only06;
	u8                 msg_flags;
	__le16             change_count;
	__le16             queue_id;
};

struct mpi3_port_enable_request {
	__le16             host_tag;
	u8                 ioc_use_only02;
	u8                 function;
	__le16             ioc_use_only04;
	u8                 ioc_use_only06;
	u8                 msg_flags;
	__le16             change_count;
	__le16             reserved0a;
};

#define MPI3_EVENT_LOG_DATA                         (0x01)
#define MPI3_EVENT_CHANGE                           (0x02)
#define MPI3_EVENT_GPIO_INTERRUPT                   (0x04)
#define MPI3_EVENT_CABLE_MGMT                       (0x06)
#define MPI3_EVENT_DEVICE_ADDED                     (0x07)
#define MPI3_EVENT_DEVICE_INFO_CHANGED              (0x08)
#define MPI3_EVENT_PREPARE_FOR_RESET                (0x09)
#define MPI3_EVENT_COMP_IMAGE_ACT_START             (0x0a)
#define MPI3_EVENT_ENCL_DEVICE_ADDED                (0x0b)
#define MPI3_EVENT_ENCL_DEVICE_STATUS_CHANGE        (0x0c)
#define MPI3_EVENT_DEVICE_STATUS_CHANGE             (0x0d)
#define MPI3_EVENT_ENERGY_PACK_CHANGE               (0x0e)
#define MPI3_EVENT_SAS_DISCOVERY                    (0x11)
#define MPI3_EVENT_SAS_BROADCAST_PRIMITIVE          (0x12)
#define MPI3_EVENT_SAS_NOTIFY_PRIMITIVE             (0x13)
#define MPI3_EVENT_SAS_INIT_DEVICE_STATUS_CHANGE    (0x14)
#define MPI3_EVENT_SAS_INIT_TABLE_OVERFLOW          (0x15)
#define MPI3_EVENT_SAS_TOPOLOGY_CHANGE_LIST         (0x16)
#define MPI3_EVENT_SAS_PHY_COUNTER                  (0x18)
#define MPI3_EVENT_SAS_DEVICE_DISCOVERY_ERROR       (0x19)
#define MPI3_EVENT_PCIE_TOPOLOGY_CHANGE_LIST        (0x20)
#define MPI3_EVENT_PCIE_ENUMERATION                 (0x22)
#define MPI3_EVENT_PCIE_ERROR_THRESHOLD             (0x23)
#define MPI3_EVENT_HARD_RESET_RECEIVED              (0x40)
#define MPI3_EVENT_DIAGNOSTIC_BUFFER_STATUS_CHANGE  (0x50)
#define MPI3_EVENT_MIN_PRODUCT_SPECIFIC             (0x60)
#define MPI3_EVENT_MAX_PRODUCT_SPECIFIC             (0x7f)
#define MPI3_EVENT_NOTIFY_EVENTMASK_WORDS           (4)
struct mpi3_event_notification_request {
	__le16             host_tag;
	u8                 ioc_use_only02;
	u8                 function;
	__le16             ioc_use_only04;
	u8                 ioc_use_only06;
	u8                 msg_flags;
	__le16             change_count;
	__le16             reserved0a;
	__le16             sas_broadcast_primitive_masks;
	__le16             sas_notify_primitive_masks;
	__le32             event_masks[MPI3_EVENT_NOTIFY_EVENTMASK_WORDS];
};

struct mpi3_event_notification_reply {
	__le16             host_tag;
	u8                 ioc_use_only02;
	u8                 function;
	__le16             ioc_use_only04;
	u8                 ioc_use_only06;
	u8                 msg_flags;
	__le16             ioc_use_only08;
	__le16             ioc_status;
	__le32             ioc_log_info;
	u8                 event_data_length;
	u8                 event;
	__le16             ioc_change_count;
	__le32             event_context;
	__le32             event_data[1];
};

#define MPI3_EVENT_NOTIFY_MSGFLAGS_ACK_MASK                        (0x01)
#define MPI3_EVENT_NOTIFY_MSGFLAGS_ACK_SHIFT			    (0)
#define MPI3_EVENT_NOTIFY_MSGFLAGS_ACK_REQUIRED                    (0x01)
#define MPI3_EVENT_NOTIFY_MSGFLAGS_ACK_NOT_REQUIRED                (0x00)
#define MPI3_EVENT_NOTIFY_MSGFLAGS_EVENT_ORIGINALITY_MASK          (0x02)
#define MPI3_EVENT_NOTIFY_MSGFLAGS_EVENT_ORIGINALITY_SHIFT	    (1)
#define MPI3_EVENT_NOTIFY_MSGFLAGS_EVENT_ORIGINALITY_ORIGINAL      (0x00)
#define MPI3_EVENT_NOTIFY_MSGFLAGS_EVENT_ORIGINALITY_REPLAY        (0x02)
struct mpi3_event_data_gpio_interrupt {
	u8                 gpio_num;
	u8                 reserved01[3];
};
struct mpi3_event_data_cable_management {
	__le32             active_cable_power_requirement;
	u8                 status;
	u8                 receptacle_id;
	__le16             reserved06;
};

#define MPI3_EVENT_CABLE_MGMT_ACT_CABLE_PWR_INVALID     (0xffffffff)
#define MPI3_EVENT_CABLE_MGMT_STATUS_INSUFFICIENT_POWER        (0x00)
#define MPI3_EVENT_CABLE_MGMT_STATUS_PRESENT                   (0x01)
#define MPI3_EVENT_CABLE_MGMT_STATUS_DEGRADED                  (0x02)
struct mpi3_event_ack_request {
	__le16             host_tag;
	u8                 ioc_use_only02;
	u8                 function;
	__le16             ioc_use_only04;
	u8                 ioc_use_only06;
	u8                 msg_flags;
	__le16             change_count;
	__le16             reserved0a;
	u8                 event;
	u8                 reserved0d[3];
	__le32             event_context;
};

struct mpi3_event_data_prepare_for_reset {
	u8                 reason_code;
	u8                 reserved01;
	__le16             reserved02;
};

#define MPI3_EVENT_PREPARE_RESET_RC_START                (0x01)
#define MPI3_EVENT_PREPARE_RESET_RC_ABORT                (0x02)
struct mpi3_event_data_comp_image_activation {
	__le32            reserved00;
};

struct mpi3_event_data_device_status_change {
	__le16             task_tag;
	u8                 reason_code;
	u8                 io_unit_port;
	__le16             parent_dev_handle;
	__le16             dev_handle;
	__le64             wwid;
	u8                 lun[8];
};

#define MPI3_EVENT_DEV_STAT_RC_MOVED                                (0x01)
#define MPI3_EVENT_DEV_STAT_RC_HIDDEN                               (0x02)
#define MPI3_EVENT_DEV_STAT_RC_NOT_HIDDEN                           (0x03)
#define MPI3_EVENT_DEV_STAT_RC_ASYNC_NOTIFICATION                   (0x04)
#define MPI3_EVENT_DEV_STAT_RC_INT_DEVICE_RESET_STRT                (0x20)
#define MPI3_EVENT_DEV_STAT_RC_INT_DEVICE_RESET_CMP                 (0x21)
#define MPI3_EVENT_DEV_STAT_RC_INT_TASK_ABORT_STRT                  (0x22)
#define MPI3_EVENT_DEV_STAT_RC_INT_TASK_ABORT_CMP                   (0x23)
#define MPI3_EVENT_DEV_STAT_RC_INT_IT_NEXUS_RESET_STRT              (0x24)
#define MPI3_EVENT_DEV_STAT_RC_INT_IT_NEXUS_RESET_CMP               (0x25)
#define MPI3_EVENT_DEV_STAT_RC_PCIE_HOT_RESET_FAILED                (0x30)
#define MPI3_EVENT_DEV_STAT_RC_EXPANDER_REDUCED_FUNC_STRT           (0x40)
#define MPI3_EVENT_DEV_STAT_RC_EXPANDER_REDUCED_FUNC_CMP            (0x41)
#define MPI3_EVENT_DEV_STAT_RC_VD_NOT_RESPONDING                    (0x50)
struct mpi3_event_data_energy_pack_change {
	__le32             reserved00;
	__le16             shutdown_timeout;
	__le16             reserved06;
};

struct mpi3_event_data_sas_discovery {
	u8                 flags;
	u8                 reason_code;
	u8                 io_unit_port;
	u8                 reserved03;
	__le32             discovery_status;
};

#define MPI3_EVENT_SAS_DISC_FLAGS_DEVICE_CHANGE                 (0x02)
#define MPI3_EVENT_SAS_DISC_FLAGS_IN_PROGRESS                   (0x01)
#define MPI3_EVENT_SAS_DISC_RC_STARTED                          (0x01)
#define MPI3_EVENT_SAS_DISC_RC_COMPLETED                        (0x02)
#define MPI3_SAS_DISC_STATUS_MAX_ENCLOSURES_EXCEED            (0x80000000)
#define MPI3_SAS_DISC_STATUS_MAX_EXPANDERS_EXCEED             (0x40000000)
#define MPI3_SAS_DISC_STATUS_MAX_DEVICES_EXCEED               (0x20000000)
#define MPI3_SAS_DISC_STATUS_MAX_TOPO_PHYS_EXCEED             (0x10000000)
#define MPI3_SAS_DISC_STATUS_INVALID_CEI                      (0x00010000)
#define MPI3_SAS_DISC_STATUS_FECEI_MISMATCH                   (0x00008000)
#define MPI3_SAS_DISC_STATUS_MULTIPLE_DEVICES_IN_SLOT         (0x00004000)
#define MPI3_SAS_DISC_STATUS_NECEI_MISMATCH                   (0x00002000)
#define MPI3_SAS_DISC_STATUS_TOO_MANY_SLOTS                   (0x00001000)
#define MPI3_SAS_DISC_STATUS_EXP_MULTI_SUBTRACTIVE            (0x00000800)
#define MPI3_SAS_DISC_STATUS_MULTI_PORT_DOMAIN                (0x00000400)
#define MPI3_SAS_DISC_STATUS_TABLE_TO_SUBTRACTIVE_LINK        (0x00000200)
#define MPI3_SAS_DISC_STATUS_UNSUPPORTED_DEVICE               (0x00000100)
#define MPI3_SAS_DISC_STATUS_TABLE_LINK                       (0x00000080)
#define MPI3_SAS_DISC_STATUS_SUBTRACTIVE_LINK                 (0x00000040)
#define MPI3_SAS_DISC_STATUS_SMP_CRC_ERROR                    (0x00000020)
#define MPI3_SAS_DISC_STATUS_SMP_FUNCTION_FAILED              (0x00000010)
#define MPI3_SAS_DISC_STATUS_SMP_TIMEOUT                      (0x00000008)
#define MPI3_SAS_DISC_STATUS_MULTIPLE_PORTS                   (0x00000004)
#define MPI3_SAS_DISC_STATUS_INVALID_SAS_ADDRESS              (0x00000002)
#define MPI3_SAS_DISC_STATUS_LOOP_DETECTED                    (0x00000001)
struct mpi3_event_data_sas_broadcast_primitive {
	u8                 phy_num;
	u8                 io_unit_port;
	u8                 port_width;
	u8                 primitive;
};

#define MPI3_EVENT_BROADCAST_PRIMITIVE_CHANGE                 (0x01)
#define MPI3_EVENT_BROADCAST_PRIMITIVE_SES                    (0x02)
#define MPI3_EVENT_BROADCAST_PRIMITIVE_EXPANDER               (0x03)
#define MPI3_EVENT_BROADCAST_PRIMITIVE_ASYNCHRONOUS_EVENT     (0x04)
#define MPI3_EVENT_BROADCAST_PRIMITIVE_RESERVED3              (0x05)
#define MPI3_EVENT_BROADCAST_PRIMITIVE_RESERVED4              (0x06)
#define MPI3_EVENT_BROADCAST_PRIMITIVE_CHANGE0_RESERVED       (0x07)
#define MPI3_EVENT_BROADCAST_PRIMITIVE_CHANGE1_RESERVED       (0x08)
struct mpi3_event_data_sas_notify_primitive {
	u8                 phy_num;
	u8                 io_unit_port;
	u8                 reserved02;
	u8                 primitive;
};

#define MPI3_EVENT_NOTIFY_PRIMITIVE_ENABLE_SPINUP         (0x01)
#define MPI3_EVENT_NOTIFY_PRIMITIVE_POWER_LOSS_EXPECTED   (0x02)
#define MPI3_EVENT_NOTIFY_PRIMITIVE_RESERVED1             (0x03)
#define MPI3_EVENT_NOTIFY_PRIMITIVE_RESERVED2             (0x04)
struct mpi3_event_sas_topo_phy_entry {
	__le16             attached_dev_handle;
	u8                 link_rate;
	u8                 status;
};

#define MPI3_EVENT_SAS_TOPO_LR_CURRENT_MASK                 (0xf0)
#define MPI3_EVENT_SAS_TOPO_LR_CURRENT_SHIFT                (4)
#define MPI3_EVENT_SAS_TOPO_LR_PREV_MASK                    (0x0f)
#define MPI3_EVENT_SAS_TOPO_LR_PREV_SHIFT                   (0)
#define MPI3_EVENT_SAS_TOPO_LR_UNKNOWN_LINK_RATE            (0x00)
#define MPI3_EVENT_SAS_TOPO_LR_PHY_DISABLED                 (0x01)
#define MPI3_EVENT_SAS_TOPO_LR_NEGOTIATION_FAILED           (0x02)
#define MPI3_EVENT_SAS_TOPO_LR_SATA_OOB_COMPLETE            (0x03)
#define MPI3_EVENT_SAS_TOPO_LR_PORT_SELECTOR                (0x04)
#define MPI3_EVENT_SAS_TOPO_LR_SMP_RESET_IN_PROGRESS        (0x05)
#define MPI3_EVENT_SAS_TOPO_LR_UNSUPPORTED_PHY              (0x06)
#define MPI3_EVENT_SAS_TOPO_LR_RATE_6_0                     (0x0a)
#define MPI3_EVENT_SAS_TOPO_LR_RATE_12_0                    (0x0b)
#define MPI3_EVENT_SAS_TOPO_LR_RATE_22_5                    (0x0c)
#define MPI3_EVENT_SAS_TOPO_PHY_STATUS_MASK                 (0xc0)
#define MPI3_EVENT_SAS_TOPO_PHY_STATUS_SHIFT                (6)
#define MPI3_EVENT_SAS_TOPO_PHY_STATUS_ACCESSIBLE           (0x00)
#define MPI3_EVENT_SAS_TOPO_PHY_STATUS_NO_EXIST             (0x40)
#define MPI3_EVENT_SAS_TOPO_PHY_STATUS_VACANT               (0x80)
#define MPI3_EVENT_SAS_TOPO_PHY_RC_MASK                     (0x0f)
#define MPI3_EVENT_SAS_TOPO_PHY_RC_SHIFT		    (0)
#define MPI3_EVENT_SAS_TOPO_PHY_RC_TARG_NOT_RESPONDING      (0x02)
#define MPI3_EVENT_SAS_TOPO_PHY_RC_PHY_CHANGED              (0x03)
#define MPI3_EVENT_SAS_TOPO_PHY_RC_NO_CHANGE                (0x04)
#define MPI3_EVENT_SAS_TOPO_PHY_RC_DELAY_NOT_RESPONDING     (0x05)
#define MPI3_EVENT_SAS_TOPO_PHY_RC_RESPONDING               (0x06)
struct mpi3_event_data_sas_topology_change_list {
	__le16                             enclosure_handle;
	__le16                             expander_dev_handle;
	u8                                 num_phys;
	u8                                 reserved05[3];
	u8                                 num_entries;
	u8                                 start_phy_num;
	u8                                 exp_status;
	u8                                 io_unit_port;
	struct mpi3_event_sas_topo_phy_entry   phy_entry[] __counted_by(num_entries);
};

#define MPI3_EVENT_SAS_TOPO_ES_NO_EXPANDER              (0x00)
#define MPI3_EVENT_SAS_TOPO_ES_NOT_RESPONDING           (0x02)
#define MPI3_EVENT_SAS_TOPO_ES_RESPONDING               (0x03)
#define MPI3_EVENT_SAS_TOPO_ES_DELAY_NOT_RESPONDING     (0x04)
struct mpi3_event_data_sas_phy_counter {
	__le64             time_stamp;
	__le32             reserved08;
	u8                 phy_event_code;
	u8                 phy_num;
	__le16             reserved0e;
	__le32             phy_event_info;
	u8                 counter_type;
	u8                 threshold_window;
	u8                 time_units;
	u8                 reserved17;
	__le32             event_threshold;
	__le16             threshold_flags;
	__le16             reserved1e;
};

struct mpi3_event_data_sas_device_disc_err {
	__le16             dev_handle;
	u8                 reason_code;
	u8                 io_unit_port;
	__le32             reserved04;
	__le64             sas_address;
};

#define MPI3_EVENT_SAS_DISC_ERR_RC_SMP_FAILED          (0x01)
#define MPI3_EVENT_SAS_DISC_ERR_RC_SMP_TIMEOUT         (0x02)
struct mpi3_event_data_pcie_enumeration {
	u8                 flags;
	u8                 reason_code;
	u8                 io_unit_port;
	u8                 reserved03;
	__le32             enumeration_status;
};

#define MPI3_EVENT_PCIE_ENUM_FLAGS_DEVICE_CHANGE            (0x02)
#define MPI3_EVENT_PCIE_ENUM_FLAGS_IN_PROGRESS              (0x01)
#define MPI3_EVENT_PCIE_ENUM_RC_STARTED                     (0x01)
#define MPI3_EVENT_PCIE_ENUM_RC_COMPLETED                   (0x02)
#define MPI3_EVENT_PCIE_ENUM_ES_MAX_SWITCH_DEPTH_EXCEED     (0x80000000)
#define MPI3_EVENT_PCIE_ENUM_ES_MAX_SWITCHES_EXCEED         (0x40000000)
#define MPI3_EVENT_PCIE_ENUM_ES_MAX_DEVICES_EXCEED          (0x20000000)
#define MPI3_EVENT_PCIE_ENUM_ES_RESOURCES_EXHAUSTED         (0x10000000)
struct mpi3_event_pcie_topo_port_entry {
	__le16             attached_dev_handle;
	u8                 port_status;
	u8                 reserved03;
	u8                 current_port_info;
	u8                 reserved05;
	u8                 previous_port_info;
	u8                 reserved07;
};

#define MPI3_EVENT_PCIE_TOPO_PS_NOT_RESPONDING          (0x02)
#define MPI3_EVENT_PCIE_TOPO_PS_PORT_CHANGED            (0x03)
#define MPI3_EVENT_PCIE_TOPO_PS_NO_CHANGE               (0x04)
#define MPI3_EVENT_PCIE_TOPO_PS_DELAY_NOT_RESPONDING    (0x05)
#define MPI3_EVENT_PCIE_TOPO_PS_RESPONDING              (0x06)
#define MPI3_EVENT_PCIE_TOPO_PI_LANES_MASK              (0xf0)
#define MPI3_EVENT_PCIE_TOPO_PI_LANES_SHIFT		(4)
#define MPI3_EVENT_PCIE_TOPO_PI_LANES_UNKNOWN           (0x00)
#define MPI3_EVENT_PCIE_TOPO_PI_LANES_1                 (0x10)
#define MPI3_EVENT_PCIE_TOPO_PI_LANES_2                 (0x20)
#define MPI3_EVENT_PCIE_TOPO_PI_LANES_4                 (0x30)
#define MPI3_EVENT_PCIE_TOPO_PI_LANES_8                 (0x40)
#define MPI3_EVENT_PCIE_TOPO_PI_LANES_16                (0x50)
#define MPI3_EVENT_PCIE_TOPO_PI_RATE_MASK               (0x0f)
#define MPI3_EVENT_PCIE_TOPO_PI_RATE_SHIFT		(0)
#define MPI3_EVENT_PCIE_TOPO_PI_RATE_UNKNOWN            (0x00)
#define MPI3_EVENT_PCIE_TOPO_PI_RATE_DISABLED           (0x01)
#define MPI3_EVENT_PCIE_TOPO_PI_RATE_2_5                (0x02)
#define MPI3_EVENT_PCIE_TOPO_PI_RATE_5_0                (0x03)
#define MPI3_EVENT_PCIE_TOPO_PI_RATE_8_0                (0x04)
#define MPI3_EVENT_PCIE_TOPO_PI_RATE_16_0               (0x05)
#define MPI3_EVENT_PCIE_TOPO_PI_RATE_32_0               (0x06)
struct mpi3_event_data_pcie_topology_change_list {
	__le16                                 enclosure_handle;
	__le16                                 switch_dev_handle;
	u8                                     num_ports;
	u8                                     reserved05[3];
	u8                                     num_entries;
	u8                                     start_port_num;
	u8                                     switch_status;
	u8                                     io_unit_port;
	__le32                                 reserved0c;
	struct mpi3_event_pcie_topo_port_entry     port_entry[] __counted_by(num_entries);
};

#define MPI3_EVENT_PCIE_TOPO_SS_NO_PCIE_SWITCH          (0x00)
#define MPI3_EVENT_PCIE_TOPO_SS_NOT_RESPONDING          (0x02)
#define MPI3_EVENT_PCIE_TOPO_SS_RESPONDING              (0x03)
#define MPI3_EVENT_PCIE_TOPO_SS_DELAY_NOT_RESPONDING    (0x04)
struct mpi3_event_data_pcie_error_threshold {
	__le64                                 timestamp;
	u8                                     reason_code;
	u8                                     port;
	__le16                                 switch_dev_handle;
	u8                                     error;
	u8                                     action;
	__le16                                 threshold_count;
	__le16                                 attached_dev_handle;
	__le16                                 reserved12;
	__le32                                 reserved14;
};

#define MPI3_EVENT_PCI_ERROR_RC_THRESHOLD_EXCEEDED          (0x00)
#define MPI3_EVENT_PCI_ERROR_RC_ESCALATION                  (0x01)
struct mpi3_event_data_sas_init_dev_status_change {
	u8                 reason_code;
	u8                 io_unit_port;
	__le16             dev_handle;
	__le32             reserved04;
	__le64             sas_address;
};

#define MPI3_EVENT_SAS_INIT_RC_ADDED                (0x01)
#define MPI3_EVENT_SAS_INIT_RC_NOT_RESPONDING       (0x02)
struct mpi3_event_data_sas_init_table_overflow {
	__le16             max_init;
	__le16             current_init;
	__le32             reserved04;
	__le64             sas_address;
};

struct mpi3_event_data_hard_reset_received {
	u8                 reserved00;
	u8                 io_unit_port;
	__le16             reserved02;
};

struct mpi3_event_data_diag_buffer_status_change {
	u8                 type;
	u8                 reason_code;
	__le16             reserved02;
	__le32             reserved04;
};

#define MPI3_EVENT_DIAG_BUFFER_STATUS_CHANGE_RC_RELEASED             (0x01)
#define MPI3_EVENT_DIAG_BUFFER_STATUS_CHANGE_RC_PAUSED               (0x02)
#define MPI3_EVENT_DIAG_BUFFER_STATUS_CHANGE_RC_RESUMED              (0x03)
#define MPI3_PEL_LOCALE_FLAGS_NON_BLOCKING_BOOT_EVENT   (0x0200)
#define MPI3_PEL_LOCALE_FLAGS_BLOCKING_BOOT_EVENT       (0x0100)
#define MPI3_PEL_LOCALE_FLAGS_PCIE                      (0x0080)
#define MPI3_PEL_LOCALE_FLAGS_CONFIGURATION             (0x0040)
#define MPI3_PEL_LOCALE_FLAGS_CONTROLER                 (0x0020)
#define MPI3_PEL_LOCALE_FLAGS_SAS                       (0x0010)
#define MPI3_PEL_LOCALE_FLAGS_EPACK                     (0x0008)
#define MPI3_PEL_LOCALE_FLAGS_ENCLOSURE                 (0x0004)
#define MPI3_PEL_LOCALE_FLAGS_PD                        (0x0002)
#define MPI3_PEL_LOCALE_FLAGS_VD                        (0x0001)
#define MPI3_PEL_CLASS_DEBUG                            (0x00)
#define MPI3_PEL_CLASS_PROGRESS                         (0x01)
#define MPI3_PEL_CLASS_INFORMATIONAL                    (0x02)
#define MPI3_PEL_CLASS_WARNING                          (0x03)
#define MPI3_PEL_CLASS_CRITICAL                         (0x04)
#define MPI3_PEL_CLASS_FATAL                            (0x05)
#define MPI3_PEL_CLASS_FAULT                            (0x06)
#define MPI3_PEL_CLEARTYPE_CLEAR                        (0x00)
#define MPI3_PEL_WAITTIME_INFINITE_WAIT                 (0x00)
#define MPI3_PEL_ACTION_GET_SEQNUM                      (0x01)
#define MPI3_PEL_ACTION_MARK_CLEAR                      (0x02)
#define MPI3_PEL_ACTION_GET_LOG                         (0x03)
#define MPI3_PEL_ACTION_GET_COUNT                       (0x04)
#define MPI3_PEL_ACTION_WAIT                            (0x05)
#define MPI3_PEL_ACTION_ABORT                           (0x06)
#define MPI3_PEL_ACTION_GET_PRINT_STRINGS               (0x07)
#define MPI3_PEL_ACTION_ACKNOWLEDGE                     (0x08)
#define MPI3_PEL_STATUS_SUCCESS                         (0x00)
#define MPI3_PEL_STATUS_NOT_FOUND                       (0x01)
#define MPI3_PEL_STATUS_ABORTED                         (0x02)
#define MPI3_PEL_STATUS_NOT_READY                       (0x03)
struct mpi3_pel_seq {
	__le32                             newest;
	__le32                             oldest;
	__le32                             clear;
	__le32                             shutdown;
	__le32                             boot;
	__le32                             last_acknowledged;
};

struct mpi3_pel_entry {
	__le64                             time_stamp;
	__le32                             sequence_number;
	__le16                             log_code;
	__le16                             arg_type;
	__le16                             locale;
	u8                                 class;
	u8                                 flags;
	u8                                 ext_num;
	u8                                 num_exts;
	u8                                 arg_data_size;
	u8                                 fixed_format_strings_size;
	__le32                             reserved18[2];
	__le32                             pel_info[24];
};

#define MPI3_PEL_FLAGS_COMPLETE_RESET_NEEDED                  (0x02)
#define MPI3_PEL_FLAGS_ACK_NEEDED                             (0x01)
struct mpi3_pel_list {
	__le32                             log_count;
	__le32                             reserved04;
	struct mpi3_pel_entry                  entry[1];
};

struct mpi3_pel_arg_map {
	u8                                 arg_type;
	u8                                 length;
	__le16                             start_location;
};

#define MPI3_PEL_ARG_MAP_ARG_TYPE_APPEND_STRING                (0x00)
#define MPI3_PEL_ARG_MAP_ARG_TYPE_INTEGER                      (0x01)
#define MPI3_PEL_ARG_MAP_ARG_TYPE_STRING                       (0x02)
#define MPI3_PEL_ARG_MAP_ARG_TYPE_BIT_FIELD                    (0x03)
struct mpi3_pel_print_string {
	__le16                             log_code;
	__le16                             string_length;
	u8                                 num_arg_map;
	u8                                 reserved05[3];
	struct mpi3_pel_arg_map                arg_map[1];
};

struct mpi3_pel_print_string_list {
	__le32                             num_print_strings;
	__le32                             residual_bytes_remain;
	__le32                             reserved08[2];
	struct mpi3_pel_print_string           print_string[1];
};

#ifndef MPI3_PEL_ACTION_SPECIFIC_MAX
#define MPI3_PEL_ACTION_SPECIFIC_MAX               (1)
#endif
struct mpi3_pel_request {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             change_count;
	u8                                 action;
	u8                                 reserved0b;
	__le32                             action_specific[MPI3_PEL_ACTION_SPECIFIC_MAX];
};

struct mpi3_pel_req_action_get_sequence_numbers {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             change_count;
	u8                                 action;
	u8                                 reserved0b;
	__le32                             reserved0c[5];
	union mpi3_sge_union                  sgl;
};

struct mpi3_pel_req_action_clear_log_marker {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             change_count;
	u8                                 action;
	u8                                 reserved0b;
	u8                                 clear_type;
	u8                                 reserved0d[3];
};

struct mpi3_pel_req_action_get_log {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             change_count;
	u8                                 action;
	u8                                 reserved0b;
	__le32                             starting_sequence_number;
	__le16                             locale;
	u8                                 class;
	u8                                 reserved13;
	__le32                             reserved14[3];
	union mpi3_sge_union                  sgl;
};

struct mpi3_pel_req_action_get_count {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             change_count;
	u8                                 action;
	u8                                 reserved0b;
	__le32                             starting_sequence_number;
	__le16                             locale;
	u8                                 class;
	u8                                 reserved13;
	__le32                             reserved14[3];
	union mpi3_sge_union                  sgl;
};

struct mpi3_pel_req_action_wait {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             change_count;
	u8                                 action;
	u8                                 reserved0b;
	__le32                             starting_sequence_number;
	__le16                             locale;
	u8                                 class;
	u8                                 reserved13;
	__le16                             wait_time;
	__le16                             reserved16;
	__le32                             reserved18[2];
};

struct mpi3_pel_req_action_abort {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             change_count;
	u8                                 action;
	u8                                 reserved0b;
	__le32                             reserved0c;
	__le16                             abort_host_tag;
	__le16                             reserved12;
	__le32                             reserved14;
};

struct mpi3_pel_req_action_get_print_strings {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             change_count;
	u8                                 action;
	u8                                 reserved0b;
	__le32                             reserved0c;
	__le16                             start_log_code;
	__le16                             reserved12;
	__le32                             reserved14[3];
	union mpi3_sge_union                  sgl;
};

struct mpi3_pel_req_action_acknowledge {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             change_count;
	u8                                 action;
	u8                                 reserved0b;
	__le32                             sequence_number;
	__le32                             reserved10;
};

#define MPI3_PELACKNOWLEDGE_MSGFLAGS_SAFE_MODE_EXIT_MASK                     (0x03)
#define MPI3_PELACKNOWLEDGE_MSGFLAGS_SAFE_MODE_EXIT_SHIFT			(0)
#define MPI3_PELACKNOWLEDGE_MSGFLAGS_SAFE_MODE_EXIT_NO_GUIDANCE              (0x00)
#define MPI3_PELACKNOWLEDGE_MSGFLAGS_SAFE_MODE_EXIT_CONTINUE_OP              (0x01)
#define MPI3_PELACKNOWLEDGE_MSGFLAGS_SAFE_MODE_EXIT_TRANSITION_TO_FAULT      (0x02)
struct mpi3_pel_reply {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             ioc_use_only08;
	__le16                             ioc_status;
	__le32                             ioc_log_info;
	u8                                 action;
	u8                                 reserved11;
	__le16                             reserved12;
	__le16                             pe_log_status;
	__le16                             reserved16;
	__le32                             transfer_length;
};

struct mpi3_ci_download_request {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             change_count;
	u8                                 action;
	u8                                 reserved0b;
	__le32                             signature1;
	__le32                             total_image_size;
	__le32                             image_offset;
	__le32                             segment_size;
	__le32                             reserved1c;
	union mpi3_sge_union                  sgl;
};

#define MPI3_CI_DOWNLOAD_MSGFLAGS_LAST_SEGMENT                 (0x80)
#define MPI3_CI_DOWNLOAD_MSGFLAGS_FORCE_FMC_ENABLE             (0x40)
#define MPI3_CI_DOWNLOAD_MSGFLAGS_SIGNED_NVDATA                (0x20)
#define MPI3_CI_DOWNLOAD_MSGFLAGS_WRITE_CACHE_FLUSH_MASK       (0x03)
#define MPI3_CI_DOWNLOAD_MSGFLAGS_WRITE_CACHE_FLUSH_SHIFT	(0)
#define MPI3_CI_DOWNLOAD_MSGFLAGS_WRITE_CACHE_FLUSH_FAST       (0x00)
#define MPI3_CI_DOWNLOAD_MSGFLAGS_WRITE_CACHE_FLUSH_MEDIUM     (0x01)
#define MPI3_CI_DOWNLOAD_MSGFLAGS_WRITE_CACHE_FLUSH_SLOW       (0x02)
#define MPI3_CI_DOWNLOAD_ACTION_DOWNLOAD                       (0x01)
#define MPI3_CI_DOWNLOAD_ACTION_ONLINE_ACTIVATION              (0x02)
#define MPI3_CI_DOWNLOAD_ACTION_OFFLINE_ACTIVATION             (0x03)
#define MPI3_CI_DOWNLOAD_ACTION_GET_STATUS                     (0x04)
#define MPI3_CI_DOWNLOAD_ACTION_CANCEL_OFFLINE_ACTIVATION      (0x05)
struct mpi3_ci_download_reply {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             ioc_use_only08;
	__le16                             ioc_status;
	__le32                             ioc_log_info;
	u8                                 flags;
	u8                                 cache_dirty;
	u8                                 pending_count;
	u8                                 reserved13;
};

#define MPI3_CI_DOWNLOAD_FLAGS_DOWNLOAD_IN_PROGRESS                  (0x80)
#define MPI3_CI_DOWNLOAD_FLAGS_ACTIVATION_FAILURE                    (0x40)
#define MPI3_CI_DOWNLOAD_FLAGS_OFFLINE_ACTIVATION_REQUIRED           (0x20)
#define MPI3_CI_DOWNLOAD_FLAGS_KEY_UPDATE_PENDING                    (0x10)
#define MPI3_CI_DOWNLOAD_FLAGS_ACTIVATION_STATUS_MASK                (0x0e)
#define MPI3_CI_DOWNLOAD_FLAGS_ACTIVATION_STATUS_SHIFT			(1)
#define MPI3_CI_DOWNLOAD_FLAGS_ACTIVATION_STATUS_NOT_NEEDED          (0x00)
#define MPI3_CI_DOWNLOAD_FLAGS_ACTIVATION_STATUS_AWAITING            (0x02)
#define MPI3_CI_DOWNLOAD_FLAGS_ACTIVATION_STATUS_ONLINE_PENDING      (0x04)
#define MPI3_CI_DOWNLOAD_FLAGS_ACTIVATION_STATUS_OFFLINE_PENDING     (0x06)
#define MPI3_CI_DOWNLOAD_FLAGS_COMPATIBLE                            (0x01)
struct mpi3_ci_upload_request {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             change_count;
	__le16                             reserved0a;
	__le32                             signature1;
	__le32                             reserved10;
	__le32                             image_offset;
	__le32                             segment_size;
	__le32                             reserved1c;
	union mpi3_sge_union                  sgl;
};

#define MPI3_CI_UPLOAD_MSGFLAGS_LOCATION_MASK                        (0x01)
#define MPI3_CI_UPLOAD_MSGFLAGS_LOCATION_SHIFT				(0)
#define MPI3_CI_UPLOAD_MSGFLAGS_LOCATION_PRIMARY                     (0x00)
#define MPI3_CI_UPLOAD_MSGFLAGS_LOCATION_SECONDARY                   (0x01)
#define MPI3_CI_UPLOAD_MSGFLAGS_FORMAT_MASK                          (0x02)
#define MPI3_CI_UPLOAD_MSGFLAGS_FORMAT_SHIFT				(1)
#define MPI3_CI_UPLOAD_MSGFLAGS_FORMAT_FLASH                         (0x00)
#define MPI3_CI_UPLOAD_MSGFLAGS_FORMAT_EXECUTABLE                    (0x02)
#define MPI3_CTRL_OP_FORCE_FULL_DISCOVERY                            (0x01)
#define MPI3_CTRL_OP_LOOKUP_MAPPING                                  (0x02)
#define MPI3_CTRL_OP_UPDATE_TIMESTAMP                                (0x04)
#define MPI3_CTRL_OP_GET_TIMESTAMP                                   (0x05)
#define MPI3_CTRL_OP_GET_IOC_CHANGE_COUNT                            (0x06)
#define MPI3_CTRL_OP_CHANGE_PROFILE                                  (0x07)
#define MPI3_CTRL_OP_REMOVE_DEVICE                                   (0x10)
#define MPI3_CTRL_OP_CLOSE_PERSISTENT_CONNECTION                     (0x11)
#define MPI3_CTRL_OP_HIDDEN_ACK                                      (0x12)
#define MPI3_CTRL_OP_CLEAR_DEVICE_COUNTERS                           (0x13)
#define MPI3_CTRL_OP_SEND_SAS_PRIMITIVE                              (0x20)
#define MPI3_CTRL_OP_SAS_PHY_CONTROL                                 (0x21)
#define MPI3_CTRL_OP_READ_INTERNAL_BUS                               (0x23)
#define MPI3_CTRL_OP_WRITE_INTERNAL_BUS                              (0x24)
#define MPI3_CTRL_OP_PCIE_LINK_CONTROL                               (0x30)
#define MPI3_CTRL_OP_LOOKUP_MAPPING_PARAM8_LOOKUP_METHOD_INDEX       (0x00)
#define MPI3_CTRL_OP_UPDATE_TIMESTAMP_PARAM64_TIMESTAMP_INDEX        (0x00)
#define MPI3_CTRL_OP_CHANGE_PROFILE_PARAM8_PROFILE_ID_INDEX          (0x00)
#define MPI3_CTRL_OP_REMOVE_DEVICE_PARAM16_DEVHANDLE_INDEX           (0x00)
#define MPI3_CTRL_OP_CLOSE_PERSIST_CONN_PARAM16_DEVHANDLE_INDEX      (0x00)
#define MPI3_CTRL_OP_HIDDEN_ACK_PARAM16_DEVHANDLE_INDEX              (0x00)
#define MPI3_CTRL_OP_CLEAR_DEVICE_COUNTERS_PARAM16_DEVHANDLE_INDEX   (0x00)
#define MPI3_CTRL_OP_SEND_SAS_PRIM_PARAM8_PHY_INDEX                  (0x00)
#define MPI3_CTRL_OP_SEND_SAS_PRIM_PARAM8_PRIMSEQ_INDEX              (0x01)
#define MPI3_CTRL_OP_SEND_SAS_PRIM_PARAM32_PRIMITIVE_INDEX           (0x00)
#define MPI3_CTRL_OP_SAS_PHY_CONTROL_PARAM8_ACTION_INDEX             (0x00)
#define MPI3_CTRL_OP_SAS_PHY_CONTROL_PARAM8_PHY_INDEX                (0x01)
#define MPI3_CTRL_OP_READ_INTERNAL_BUS_PARAM64_ADDRESS_INDEX         (0x00)
#define MPI3_CTRL_OP_WRITE_INTERNAL_BUS_PARAM64_ADDRESS_INDEX        (0x00)
#define MPI3_CTRL_OP_WRITE_INTERNAL_BUS_PARAM32_VALUE_INDEX          (0x00)
#define MPI3_CTRL_OP_PCIE_LINK_CONTROL_PARAM8_ACTION_INDEX           (0x00)
#define MPI3_CTRL_OP_PCIE_LINK_CONTROL_PARAM8_LINK_INDEX             (0x01)
#define MPI3_CTRL_LOOKUP_METHOD_WWID_ADDRESS                         (0x01)
#define MPI3_CTRL_LOOKUP_METHOD_ENCLOSURE_SLOT                       (0x02)
#define MPI3_CTRL_LOOKUP_METHOD_SAS_DEVICE_NAME                      (0x03)
#define MPI3_CTRL_LOOKUP_METHOD_PERSISTENT_ID                        (0x04)
#define MPI3_CTRL_LOOKUP_METHOD_WWIDADDR_PARAM16_DEVH_INDEX             (0)
#define MPI3_CTRL_LOOKUP_METHOD_WWIDADDR_PARAM64_WWID_INDEX             (0)
#define MPI3_CTRL_LOOKUP_METHOD_ENCLSLOT_PARAM16_SLOTNUM_INDEX          (0)
#define MPI3_CTRL_LOOKUP_METHOD_ENCLSLOT_PARAM64_ENCLOSURELID_INDEX     (0)
#define MPI3_CTRL_LOOKUP_METHOD_SASDEVNAME_PARAM16_DEVH_INDEX           (0)
#define MPI3_CTRL_LOOKUP_METHOD_SASDEVNAME_PARAM64_DEVNAME_INDEX        (0)
#define MPI3_CTRL_LOOKUP_METHOD_PERSISTID_PARAM16_DEVH_INDEX            (0)
#define MPI3_CTRL_LOOKUP_METHOD_PERSISTID_PARAM16_PERSISTENT_ID_INDEX   (1)
#define MPI3_CTRL_LOOKUP_METHOD_VALUE16_DEVH_INDEX                      (0)
#define MPI3_CTRL_GET_TIMESTAMP_VALUE64_TIMESTAMP_INDEX                 (0)
#define MPI3_CTRL_GET_IOC_CHANGE_COUNT_VALUE16_CHANGECOUNT_INDEX        (0)
#define MPI3_CTRL_READ_INTERNAL_BUS_VALUE32_VALUE_INDEX                 (0)
#define MPI3_CTRL_PRIMFLAGS_SINGLE                                   (0x01)
#define MPI3_CTRL_PRIMFLAGS_TRIPLE                                   (0x03)
#define MPI3_CTRL_PRIMFLAGS_REDUNDANT                                (0x06)
#define MPI3_CTRL_ACTION_NOP                                         (0x00)
#define MPI3_CTRL_ACTION_LINK_RESET                                  (0x01)
#define MPI3_CTRL_ACTION_HARD_RESET                                  (0x02)
#define MPI3_CTRL_ACTION_CLEAR_ERROR_LOG                             (0x05)
struct mpi3_iounit_control_request {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             change_count;
	u8                                 reserved0a;
	u8                                 operation;
	__le32                             reserved0c;
	__le64                             param64[2];
	__le32                             param32[4];
	__le16                             param16[4];
	u8                                 param8[8];
};

struct mpi3_iounit_control_reply {
	__le16                             host_tag;
	u8                                 ioc_use_only02;
	u8                                 function;
	__le16                             ioc_use_only04;
	u8                                 ioc_use_only06;
	u8                                 msg_flags;
	__le16                             ioc_use_only08;
	__le16                             ioc_status;
	__le32                             ioc_log_info;
	__le64                             value64[2];
	__le32                             value32[4];
	__le16                             value16[4];
	u8                                 value8[8];
};
#endif
