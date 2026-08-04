/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 LeapIO Tech Inc.
 *
 * LeapRAID storage and RAID controller driver.
 */
#ifndef LEAPRAID_H
#define LEAPRAID_H

/* Doorbell register definitions. */
#define LEAPRAID_DB_RESET       0x00000000
#define LEAPRAID_DB_READY       0x10000000
#define LEAPRAID_DB_OPERATIONAL 0x20000000
#define LEAPRAID_DB_FAULT       0x40000000

#define LEAPRAID_DB_MASK        0xF0000000

#define LEAPRAID_DB_OVER_TEMPERATURE    0x2810

#define LEAPRAID_DB_USED                0x08000000
#define LEAPRAID_DB_DATA_MASK           0x0000FFFF
#define LEAPRAID_DB_FUNC_SHIFT          24
#define LEAPRAID_DB_ADD_DWORDS_SHIFT    16

/* Maximum number of retries waiting for doorbell to become ready. */
#define LEAPRAID_DB_RETRY_COUNT_MAX    10
/* Maximum number of retries waiting for doorbell to become operational. */
#define LEAPRAID_DB_WAIT_OP_SHORT   10
#define LEAPRAID_DB_WAIT_OP_LONG   200

/* Maximum number of retries waiting for host to end recovery. */
#define LEAPRAID_WAIT_SHOST_RECOVERY   400

/* Diagnostic register definitions. */
#define LEAPRAID_DIAG_WRITE_ENABLE      0x00000080
#define LEAPRAID_DIAG_RESET             0x00000004

/* Interrupt status register definitions. */
#define LEAPRAID_HOST2ADAPTER_DB_STATUS 0x80000000
#define LEAPRAID_ADAPTER2HOST_DB_STATUS 0x00000001

/* The number of debug register. */
#define LEAPRAID_DEBUGLOG_SZ_MAX    16

/* Reply post host register definitions. */
#define REP_POST_HOST_IDX_REG_CNT 16
#define LEAPRAID_RPHI_MSIX_IDX_SHIFT    24

/* Virtual PHY flags. */
#define LEAPRAID_SAS_PHYINFO_VPHY       0x00001000

/* Linux driver init firmware. */
#define LEAPRAID_WHOINIT_LINUX_DRIVER    0x04

/* Reply descriptor post queue array mode. */
#define LEAPRAID_ADAPTER_INIT_MSGFLG_RDPQ_ARRAY_MODE    0x01

/* Request description flags. */
#define LEAPRAID_REQ_DESC_FLG_SCSI_IO   0x00
#define LEAPRAID_REQ_DESC_FLG_HPR       0x06
#define LEAPRAID_REQ_DESC_FLG_DFLT_TYPE 0x08

/* Reply description flags. */
#define LEAPRAID_RPY_DESC_FLG_TYPE_MASK                 0x0F
#define LEAPRAID_RPY_DESC_FLG_SCSI_IO_SUCCESS           0x00
#define LEAPRAID_RPY_DESC_FLG_ADDRESS_REPLY             0x01
#define LEAPRAID_RPY_DESC_FLG_FP_SCSI_IO_SUCCESS        0x06
#define LEAPRAID_RPY_DESC_FLG_UNUSED                    0x0F

/* Request and reply messages share the same set of function codes.
 *
 * Note: SCSIIO (SCSI I/O) represents SCSI I/O operations and is used
 * consistently throughout the driver for all SCSI command handling.
 */
#define LEAPRAID_FUNC_SCSIIO                    0x00
#define LEAPRAID_FUNC_SCSI_TMF                  0x01
#define LEAPRAID_FUNC_ADAPTER_INIT              0x02
#define LEAPRAID_FUNC_GET_ADAPTER_FEATURES      0x03
#define LEAPRAID_FUNC_CONFIG_OP                 0x04
#define LEAPRAID_FUNC_SCAN_DEV                  0x06
#define LEAPRAID_FUNC_EVENT_NOTIFY              0x07
#define LEAPRAID_FUNC_FW_DOWNLOAD               0x09
#define LEAPRAID_FUNC_FW_UPLOAD                 0x12
#define LEAPRAID_FUNC_SCSIIO_RAID_PASSTHROUGH   0x16
#define LEAPRAID_FUNC_SCSI_ENC_PROCESSOR        0x18
#define LEAPRAID_FUNC_SMP_PASSTHROUGH           0x1A
#define LEAPRAID_FUNC_SAS_IO_UNIT_CTRL          0x1B
#define LEAPRAID_FUNC_SCSIIO_SATA_PASSTHROUGH   0x1C
#define LEAPRAID_FUNC_ADAPTER_UNIT_RESET        0x40
#define LEAPRAID_FUNC_HANDSHAKE                 0x42
#define LEAPRAID_FUNC_LOGBUF_INIT               0x57

/* Adapter status values. */
#define LEAPRAID_ADAPTER_STATUS_MASK                    0x7FFF
#define LEAPRAID_ADAPTER_STATUS_SUCCESS                 0x0000
#define LEAPRAID_ADAPTER_STATUS_BUSY                    0x0002
#define LEAPRAID_ADAPTER_STATUS_INTERNAL_ERROR          0x0004
#define LEAPRAID_ADAPTER_STATUS_INSUFFICIENT_RESOURCES  0x0006
#define LEAPRAID_ADAPTER_STATUS_CONFIG_INVALID_ACTION   0x0020
#define LEAPRAID_ADAPTER_STATUS_CONFIG_INVALID_TYPE     0x0021
#define LEAPRAID_ADAPTER_STATUS_CONFIG_INVALID_PAGE     0x0022
#define LEAPRAID_ADAPTER_STATUS_CONFIG_INVALID_DATA     0x0023
#define LEAPRAID_ADAPTER_STATUS_CONFIG_NO_DEFAULTS      0x0024
#define LEAPRAID_ADAPTER_STATUS_CONFIG_CANT_COMMIT      0x0025
#define LEAPRAID_ADAPTER_STATUS_SCSI_RECOVERED_ERROR    0x0040
#define LEAPRAID_ADAPTER_STATUS_SCSI_DEVICE_NOT_THERE   0x0043
#define LEAPRAID_ADAPTER_STATUS_SCSI_DATA_OVERRUN       0x0044
#define LEAPRAID_ADAPTER_STATUS_SCSI_DATA_UNDERRUN      0x0045
#define LEAPRAID_ADAPTER_STATUS_SCSI_IO_DATA_ERROR      0x0046
#define LEAPRAID_ADAPTER_STATUS_SCSI_PROTOCOL_ERROR     0x0047
#define LEAPRAID_ADAPTER_STATUS_SCSI_TASK_TERMINATED    0x0048
#define LEAPRAID_ADAPTER_STATUS_SCSI_RESIDUAL_MISMATCH  0x0049
#define LEAPRAID_ADAPTER_STATUS_SCSI_TASK_MGMT_FAILED   0x004A
#define LEAPRAID_ADAPTER_STATUS_SCSI_ADAPTER_TERMINATED 0x004B
#define LEAPRAID_ADAPTER_STATUS_SCSI_EXT_TERMINATED     0x004C

/* SGE flags. */
#define LEAPRAID_SGE_FLG_LAST_ONE       0x80
#define LEAPRAID_SGE_FLG_EOB            0x40
#define LEAPRAID_SGE_FLG_EOL            0x01
#define LEAPRAID_SGE_FLG_SHIFT          24
#define LEAPRAID_SGE_FLG_SIMPLE_ONE     0x10
#define LEAPRAID_SGE_FLG_SYSTEM_ADDR    0x00
#define LEAPRAID_SGE_FLG_H2C            0x04
#define LEAPRAID_SGE_FLG_32             0x00
#define LEAPRAID_SGE_FLG_64             0x02

#define LEAPRAID_IEEE_SGE_FLG_EOL               0x40
#define LEAPRAID_IEEE_SGE_FLG_SIMPLE_ONE        0x00
#define LEAPRAID_IEEE_SGE_FLG_CHAIN_ONE         0x80
#define LEAPRAID_IEEE_SGE_FLG_SYSTEM_ADDR       0x00

#define LEAPRAID_SGE_NO_DATA_ADDR -1
#define LEAPRAID_SGE_OFFSET_SIZE  4

/* The type of page. */
#define LEAPRAID_CFG_PT_IO_UNIT         0x00
#define LEAPRAID_CFG_PT_ADAPTER         0x01
#define LEAPRAID_CFG_PT_BIOS            0x02
#define LEAPRAID_CFG_PT_RAID_VOLUME     0x08
#define LEAPRAID_CFG_PT_MANUFACTURING   0x09
#define LEAPRAID_CFG_PT_RAID_PHYSDISK   0x0A
#define LEAPRAID_CFG_PT_EXTENDED        0x0F

/* The type of extended page. */
#define LEAPRAID_CFG_EXTPT_SAS_IO_UNIT  0x10
#define LEAPRAID_CFG_EXTPT_SAS_EXP      0x11
#define LEAPRAID_CFG_EXTPT_SAS_DEV      0x12
#define LEAPRAID_CFG_EXTPT_SAS_PHY      0x13
#define LEAPRAID_CFG_EXTPT_ENC          0x15
#define LEAPRAID_CFG_EXTPT_RAID_CONFIG  0x16

/* Config page address. */
#define LEAPRAID_SAS_CFG_PGAD_GET_NEXT_LOOP     0x00000000
#define LEAPRAID_SAS_ENC_CFG_PGAD_HDL           0x10000000
#define LEAPRAID_SAS_DEV_CFG_PGAD_HDL           0x20000000
#define LEAPRAID_SAS_EXP_CFG_PGAD_HDL_PHY_NUM   0x10000000
#define LEAPRAID_SAS_EXP_CFD_PGAD_HDL           0x20000000
#define LEAPRAID_SAS_EXP_CFG_PGAD_PHYNUM_SHIFT  16
#define LEAPRAID_RAID_VOL_CFG_PGAD_HDL          0x10000000
#define LEAPRAID_SAS_PHY_CFG_PGAD_PHY_NUMBER    0x00000000
#define LEAPRAID_PHYSDISK_CFG_PGAD_PHYSDISKNUM  0x10000000

/* Config page operations. */
#define LEAPRAID_CFG_ACT_PAGE_HEADER    0x00
#define LEAPRAID_CFG_ACT_PAGE_READ_CUR  0x01
#define LEAPRAID_CFG_ACT_PAGE_WRITE_CUR 0x02

/* BIOS page number. */
#define LEAPRAID_CFG_PAGE_NUM_BIOS2     0x2
#define LEAPRAID_CFG_PAGE_NUM_BIOS3     0x3

/* Manufacturing page number. */
#define LEAPRAID_CFG_PAGE_NUM_MANU0     0x0

/* SAS device page number. */
#define LEAPRAID_CFG_PAGE_NUM_DEV0      0x0

/* SAS device page 0 flags. */
#define LEAPRAID_SAS_DEV_P0_FLG_ENC_LEVEL_VALID 0x0002
#define LEAPRAID_SAS_DEV_P0_FLG_DEV_PRESENT     0x0001
#define LEAPRAID_SAS_DEV_P0_CON_NAME_LEN        4

/* SAS I/O unit page number. */
#define LEAPRAID_CFG_PAGE_NUM_IOUNIT0   0x0
#define LEAPRAID_CFG_PAGE_NUM_IOUNIT1   0x1

/* SAS expander page number. */
#define LEAPRAID_CFG_PAGE_NUM_EXP0      0x0
#define LEAPRAID_CFG_PAGE_NUM_EXP1      0x1

/* SAS enclosure page number. */
#define LEAPRAID_CFG_PAGE_NUM_ENC0      0x0

/* SAS PHY page number. */
#define LEAPRAID_CFG_PAGE_NUM_PHY0      0x0

/* RAID volume page number. */
#define LEAPRAID_CFG_PAGE_NUM_VOL0      0x0
#define LEAPRAID_CFG_PAGE_NUM_VOL1      0x1

/* Physical disk page number. */
#define LEAPRAID_CFG_PAGE_NUM_PD0       0x0

#define LEAPRAID_CFG_UNIT_SIZE  4

/* Raid volume type and state. */
#define LEAPRAID_VOL_STATE_ONLINE       0x03
#define LEAPRAID_VOL_STATE_DEGRADED     0x04
#define LEAPRAID_VOL_STATE_OPTIMAL      0x05
#define LEAPRAID_VOL_TYPE_RAID0         0x00
#define LEAPRAID_VOL_TYPE_RAID1E        0x01
#define LEAPRAID_VOL_TYPE_RAID1         0x02
#define LEAPRAID_VOL_TYPE_RAID10        0x05
#define LEAPRAID_VOL_TYPE_UNKNOWN       0xFF

/* Raid volume element flags. */
#define LEAPRAID_RAIDCFG_P0_EFLG_MASK_ELEMENT_TYPE      0x000F
#define LEAPRAID_RAIDCFG_P0_EFLG_VOL_PHYS_DISK_ELEMENT  0x0001
#define LEAPRAID_RAIDCFG_P0_EFLG_HOT_SPARE_ELEMENT      0x0002
#define LEAPRAID_RAIDCFG_P0_EFLG_OCE_ELEMENT            0x0003

/* SAS negotiated link rates. */
#define LEAPRAID_SAS_NEG_LINK_RATE_MASK_PHYSICAL        0x0F
#define LEAPRAID_SAS_NEG_LINK_RATE_UNKNOWN_LINK_RATE    0x00
#define LEAPRAID_SAS_NEG_LINK_RATE_PHY_DISABLED         0x01
#define LEAPRAID_SAS_NEG_LINK_RATE_NEGOTIATION_FAILED   0x02
#define LEAPRAID_SAS_NEG_LINK_RATE_SATA_OOB_COMPLETE    0x03
#define LEAPRAID_SAS_NEG_LINK_RATE_PORT_SELECTOR        0x04
#define LEAPRAID_SAS_NEG_LINK_RATE_SMP_RESETTING        0x05
#define LEAPRAID_SAS_NEG_LINK_RATE_SHIFT                4

#define LEAPRAID_SAS_NEG_LINK_RATE_1_5  0x08
#define LEAPRAID_SAS_NEG_LINK_RATE_3_0  0x09
#define LEAPRAID_SAS_NEG_LINK_RATE_6_0  0x0A
#define LEAPRAID_SAS_NEG_LINK_RATE_12_0 0x0B

#define LEAPRAID_SAS_PRATE_MIN_RATE_MASK        0x0F
#define LEAPRAID_SAS_HWRATE_MIN_RATE_MASK       0x0F

/*
 * Control flags for a SCSI I/O Request.
 */
#define LEAPRAID_SCSIIO_CTRL_CDB_32BYTE         4
#define LEAPRAID_SCSIIO_CTRL_CDB_LEN_SHIFT      26
#define LEAPRAID_SCSIIO_CTRL_NODATATRANSFER     0x00000000
#define LEAPRAID_SCSIIO_CTRL_WRITE              0x01000000
#define LEAPRAID_SCSIIO_CTRL_READ               0x02000000
#define LEAPRAID_SCSIIO_CTRL_BIDIRECTIONAL      0x03000000
#define LEAPRAID_SCSIIO_CTRL_SIMPLEQ            0x00000000
#define LEAPRAID_SCSIIO_CTRL_ORDEREDQ           0x00000200
#define LEAPRAID_SCSIIO_CTRL_CMDPRI             0x00000800

/* SCSI state and status. */
#define LEAPRAID_SCSI_STATUS_BUSY                       0x08
#define LEAPRAID_SCSI_STATUS_RESERVATION_CONFLICT       0x18
#define LEAPRAID_SCSI_STATUS_TASK_SET_FULL              0x28

#define LEAPRAID_SCSI_STATE_RESPONSE_INFO_VALID 0x10
#define LEAPRAID_SCSI_STATE_TERMINATED          0x08
#define LEAPRAID_SCSI_STATE_NO_SCSI_STATUS      0x04
#define LEAPRAID_SCSI_STATE_AUTOSENSE_FAILED    0x02
#define LEAPRAID_SCSI_STATE_AUTOSENSE_VALID     0x01

/* SCSI task management defines. */
#define LEAPRAID_TM_TASKTYPE_ABORT_TASK         0x01
#define LEAPRAID_TM_TASKTYPE_ABRT_TASK_SET      0x02
#define LEAPRAID_TM_TASKTYPE_TARGET_RESET       0x03
#define LEAPRAID_TM_TASKTYPE_LOGICAL_UNIT_RESET 0x05
#define LEAPRAID_TM_TASKTYPE_CLEAR_TASK_SET     0x06
#define LEAPRAID_TM_TASKTYPE_QUERY_TASK         0x07
#define LEAPRAID_TM_TASKTYPE_CLEAR_ACA          0x08
#define LEAPRAID_TM_TASKTYPE_QUERY_TASK_SET     0x09
#define LEAPRAID_TM_TASKTYPE_QUERY_ASYNC_EVENT  0x0A

#define LEAPRAID_TM_MSGFLAGS_LINK_RESET         0x00
#define LEAPRAID_TM_RSP_INVALID_FRAME           0x02

/* SCSI enclosure processor request defines. */
#define LEAPRAID_SEP_REQ_ACT_WRITE_STATUS               0x00
#define LEAPRAID_SEP_REQ_FLG_DEVHDL_ADDRESS             0x00
#define LEAPRAID_SEP_REQ_FLG_ENCLOSURE_SLOT_ADDRESS     0x01
#define LEAPRAID_SEP_REQ_SLOTSTATUS_PREDICTED_FAULT     0x00000040

/* The capabilities of the adapter. */
#define LEAPRAID_ADAPTER_FEATURES_CAP_ATOMIC_REQ                0x00080000
#define LEAPRAID_ADAPTER_FEATURES_CAP_INTEGRATED_RAID           0x00001000

/* Event code definitions for the firmware. */
#define LEAPRAID_EVT_SAS_DEV_STATUS_CHANGE      0x000F
#define LEAPRAID_EVT_SAS_TOPO_CHANGE_LIST       0x001C
#define LEAPRAID_EVT_SAS_ENCL_DEV_STATUS_CHANGE 0x001D
#define LEAPRAID_EVT_IR_CHANGE          0x0020
#define LEAPRAID_EVT_TURN_ON_PFA_LED    0xFFFC
#define LEAPRAID_EVT_SCAN_DEV_DONE      0xFFFD
#define LEAPRAID_EVT_REMOVE_DEAD_DEV    0xFFFF
#define LEAPRAID_MAX_EVENT_NUM          128

#define LEAPRAID_EVT_SAS_DEV_STAT_RC_INTERNAL_DEV_RESET         0x08
#define LEAPRAID_EVT_SAS_DEV_STAT_RC_CMP_INTERNAL_DEV_RESET     0x0E

/* RAID configuration change event. */
#define LEAPRAID_EVT_IR_RC_VOLUME_ADD                   0x01
#define LEAPRAID_EVT_IR_RC_VOLUME_DELETE                0x02
#define LEAPRAID_EVT_IR_RC_PD_HIDDEN_TO_ADD             0x03
#define LEAPRAID_EVT_IR_RC_PD_UNHIDDEN_TO_DELETE        0x04
#define LEAPRAID_EVT_IR_RC_PD_CREATED_TO_HIDE           0x05
#define LEAPRAID_EVT_IR_RC_PD_DELETED_TO_EXPOSE         0x06
#define LEAPRAID_EVT_IR_RC_VOLUME_HIDE                  0x0A
#define LEAPRAID_EVT_IR_RC_VOLUME_UNHIDE                0x0B

/* SAS topology change event. */
#define LEAPRAID_EVT_SAS_TOPO_ES_NO_EXPANDER    0x00
#define LEAPRAID_EVT_SAS_TOPO_ES_ADDED          0x01
#define LEAPRAID_EVT_SAS_TOPO_ES_NOT_RESPONDING 0x02
#define LEAPRAID_EVT_SAS_TOPO_ES_RESPONDING     0x03

#define LEAPRAID_EVT_SAS_TOPO_RC_MASK                   0x0F
#define LEAPRAID_EVT_SAS_TOPO_RC_TARG_ADDED             0x01
#define LEAPRAID_EVT_SAS_TOPO_RC_TARG_NOT_RESPONDING    0x02

/* Enclosure device status change event. */
#define LEAPRAID_EVT_SAS_ENCL_RC_ADDED          0x01
#define LEAPRAID_EVT_SAS_ENCL_RC_NOT_RESPONDING 0x02

/* Device type and identifiers. */
#define LEAPRAID_DEVTYP_SEP             0x00004000
#define LEAPRAID_DEVTYP_SSP_TGT         0x00000400
#define LEAPRAID_DEVTYP_STP_TGT         0x00000200
#define LEAPRAID_DEVTYP_SMP_TGT         0x00000100
#define LEAPRAID_DEVTYP_SATA_DEV        0x00000080
#define LEAPRAID_DEVTYP_SSP_INIT        0x00000040
#define LEAPRAID_DEVTYP_STP_INIT        0x00000020
#define LEAPRAID_DEVTYP_SMP_INIT        0x00000010
#define LEAPRAID_DEVTYP_SATA_HOST       0x00000008

#define LEAPRAID_DEVTYP_MASK_DEV_TYPE   0x00000007
#define LEAPRAID_DEVTYP_NO_DEV          0x00000000
#define LEAPRAID_DEVTYP_END_DEV         0x00000001
#define LEAPRAID_DEVTYP_EDGE_EXPANDER   0x00000002
#define LEAPRAID_DEVTYP_FANOUT_EXPANDER 0x00000003

/* SAS control operation. */
#define LEAPRAID_SAS_OP_PHY_LINK_RESET  0x06
#define LEAPRAID_SAS_OP_PHY_HARD_RESET  0x07
#define LEAPRAID_SAS_OP_SET_PARAMETER   0x0F

/* Boot device defines */
#define LEAPRAID_BOOTDEV_FORM_MASK      0x0F
#define LEAPRAID_BOOTDEV_FORM_NONE      0x00
#define LEAPRAID_BOOTDEV_FORM_SAS_WWID  0x05
#define LEAPRAID_BOOTDEV_FORM_ENC_SLOT  0x06
#define LEAPRAID_BOOTDEV_FORM_DEV_NAME  0x07

/**
 * struct leapraid_reg_base - Register layout of the LeapRAID controller
 *
 * @db: Doorbell register used to signal commands or status to firmware.
 * @ws: Write sequence register for synchronizing doorbell operations.
 * @host_diag: Diagnostic register used for status or debug reporting.
 * @r1: Reserved.
 * @host_int_status: Interrupt status register reporting active interrupts.
 * @host_int_mask: Interrupt mask register enabling or disabling sources.
 * @r2: Reserved.
 * @rep_msg_host_idx: Reply message index for the next available reply slot.
 * @r3: Reserved.
 * @debug_log: DebugLog registers for firmware debug and diagnostic output.
 * @r4: Reserved.
 * @atomic_req_desc_post: Atomic register for single descriptor posting.
 * @adapter_log_buf_pos: Adapter log buffer write position.
 * @host_log_buf_pos: Host log buffer write position.
 * @r5: Reserved.
 * @rep_post_reg_idx: Array of reply post index registers, one per queue.
 *                    The number of entries is defined by
 *                    REP_POST_HOST_IDX_REG_CNT.
 */
struct leapraid_reg_base {
	__le32 db;
	__le32 ws;
	__le32 host_diag;
	__le32 r1[9];
	__le32 host_int_status;
	__le32 host_int_mask;
	__le32 r2[4];
	__le32 rep_msg_host_idx;
	__le32 r3[13];
	__le32 debug_log[LEAPRAID_DEBUGLOG_SZ_MAX];
	__le32 r4[2];
	__le32 atomic_req_desc_post;
	__le32 adapter_log_buf_pos;
	__le32 host_log_buf_pos;
	__le32 r5[142];
	struct leapraid_rep_post_reg_idx {
		__le32 idx;
		__le32 r1;
		__le32 r2;
		__le32 r3;
	} rep_post_reg_idx[REP_POST_HOST_IDX_REG_CNT];
} __packed;

/**
 * struct leapraid_atomic_req_desc - Atomic request descriptor
 *
 * @flg: Descriptor flag indicating the type of request (e.g. SCSI I/O).
 * @msix_idx: MSI-X vector index used for interrupt routing.
 * @taskid: Unique task identifier associated with this request.
 */
struct leapraid_atomic_req_desc {
	u8 flg;
	u8 msix_idx;
	__le16 taskid;
};

/**
 * union leapraid_rep_desc_union - Unified reply descriptor format
 *
 * @dflt_rep: Default reply descriptor containing basic completion info.
 * @dflt_rep.rep_flg: Reply flag indicating reply type or status.
 * @dflt_rep.msix_idx: MSI-X index for interrupt routing.
 * @dflt_rep.taskid: Task identifier matching the submitted request.
 * @r1: Reserved.
 *
 * @addr_rep: Address reply descriptor used when firmware returns a
 *            memory address associated with the reply.
 * @addr_rep.rep_flg: Reply flag indicating reply type or status.
 * @addr_rep.msix_idx: MSI-X index for interrupt routing.
 * @addr_rep.taskid: Task identifier matching the submitted request.
 * @addr_rep.rep_frame_addr: Physical address of the reply frame.
 *
 * @words: Raw 64-bit representation of the reply descriptor.
 * @u: Alternative access using 32-bit low/high words.
 * @u.low: Lower 32 bits of the descriptor.
 * @u.high: Upper 32 bits of the descriptor.
 */
union leapraid_rep_desc_union {
	struct leapraid_rep_desc {
		u8 rep_flg;
		u8 msix_idx;
		__le16 taskid;
		u8 r1[4];
	} dflt_rep;
	struct leapraid_add_rep_desc {
		u8 rep_flg;
		u8 msix_idx;
		__le16 taskid;
		__le32 rep_frame_addr;
	} addr_rep;
	__le64 words;
	struct {
		u32 low;
		u32 high;
	} u;
}  __packed __aligned(4);

/**
 * struct leapraid_req - Generic request header
 *
 * @func_dep1: Function-dependent parameter (low 16 bits).
 * @r1: Reserved.
 * @func: Function code identifying the command type.
 * @r2: Reserved.
 */
struct leapraid_req {
	__le16 func_dep1;
	u8 r1;
	u8 func;
	u8 r2[8];
};

/**
 * struct leapraid_rep - Generic reply header
 *
 * @r1: Reserved.
 * @msg_len: Length of the reply message in bytes.
 * @function: Function code corresponding to the request.
 * @r2: Reserved.
 * @adapter_status: Status code reported by the adapter.
 * @r3: Reserved.
 */
struct leapraid_rep {
	u8 r1[2];
	u8 msg_len;
	u8 function;
	u8 r2[10];
	__le16 adapter_status;
	u8 r3[4];
};

/**
 * struct leapraid_sge_simple32 - 32-bit simple scatter-gather entry
 *
 * @flg_and_len: Combined field for flags and segment length.
 * @addr: 32-bit physical address of the data buffer.
 */
struct leapraid_sge_simple32 {
	__le32 flg_and_len;
	__le32 addr;
};

/**
 * struct leapraid_sge_simple64 - 64-bit simple scatter-gather entry
 *
 * @flg_and_len: Combined field for flags and segment length.
 * @addr: 64-bit physical address of the data buffer.
 */
struct leapraid_sge_simple64 {
	__le32 flg_and_len;
	__le64 addr;
} __packed __aligned(4);

/**
 * struct leapraid_sge_simple_union - Unified 32/64-bit SGE representation
 *
 * @flg_and_len: Combined field for flags and segment length.
 * @u.addr32: 32-bit address field.
 * @u.addr64: 64-bit address field.
 */
struct leapraid_sge_simple_union {
	__le32 flg_and_len;
	union {
		__le32 addr32;
		__le64 addr64;
	} __packed __aligned(4) u;
} __packed __aligned(4);

/**
 * struct leapraid_sge_chain_union - Chained scatter-gather entry
 *
 * @len: Length of the chain descriptor.
 * @next_chain_offset: Offset to the next SGE chain.
 * @flg: Flags indicating chain or termination properties.
 * @u.addr32: 32-bit physical address.
 * @u.addr64: 64-bit physical address.
 */
struct leapraid_sge_chain_union {
	__le16 len;
	u8 next_chain_offset;
	u8 flg;
	union {
		__le32 addr32;
		__le64 addr64;
	} __packed __aligned(4) u;
} __packed __aligned(4);

/**
 * struct leapraid_ieee_sge_simple32 - IEEE 32-bit simple SGE format
 *
 * @addr: 32-bit physical address of the data buffer.
 * @flg_and_len: Combined field for flags and data length.
 */
struct leapraid_ieee_sge_simple32 {
	__le32 addr;
	__le32 flg_and_len;
};

/**
 * struct leapraid_ieee_sge_simple64 - IEEE 64-bit simple SGE format
 *
 * @addr: 64-bit physical address of the data buffer.
 * @len: Length of the data segment.
 * @r1: Reserved.
 * @flg: Flags indicating transfer properties.
 */
struct leapraid_ieee_sge_simple64 {
	__le64 addr;
	__le32 len;
	u8 r1[3];
	u8 flg;
} __packed __aligned(4);

/**
 * union leapraid_ieee_sge_simple_union - Unified IEEE SGE format
 *
 * @simple32: IEEE 32-bit simple SGE entry.
 * @simple64: IEEE 64-bit simple SGE entry.
 */
union leapraid_ieee_sge_simple_union {
	struct leapraid_ieee_sge_simple32 simple32;
	struct leapraid_ieee_sge_simple64 simple64;
};

/**
 * union leapraid_ieee_sge_chain_union - Unified IEEE SGE chain format
 *
 * @chain32: IEEE 32-bit chain SGE entry.
 * @chain64: IEEE 64-bit chain SGE entry.
 */
union leapraid_ieee_sge_chain_union {
	struct leapraid_ieee_sge_simple32 chain32;
	struct leapraid_ieee_sge_simple64 chain64;
};

/**
 * struct leapraid_chain64_ieee_sg - 64-bit IEEE chain SGE descriptor
 *
 * @addr: Physical address of the next chain segment.
 * @len: Length of the current SGE.
 * @r1: Reserved.
 * @next_chain_offset: Offset to the next chain element.
 * @flg: Flags that describe SGE attributes.
 */
struct leapraid_chain64_ieee_sg {
	__le64 addr;
	__le32 len;
	u8 r1[2];
	u8 next_chain_offset;
	u8 flg;
} __packed __aligned(4);

/**
 * union leapraid_ieee_sge_io_union - IEEE-style SGE union for I/O
 *
 * @ieee_simple: Simple IEEE SGE descriptor.
 * @ieee_chain: IEEE chain SGE descriptor.
 */
union leapraid_ieee_sge_io_union {
	struct leapraid_ieee_sge_simple64 ieee_simple;
	struct leapraid_chain64_ieee_sg ieee_chain;
};

/**
 * union leapraid_simple_sge_union - Union of simple SGE descriptors
 *
 * @leapraid_simple: LeapRAID simple SGE.
 * @ieee_simple: IEEE-style simple SGE.
 */
union leapraid_simple_sge_union {
	struct leapraid_sge_simple_union leapraid_simple;
	union leapraid_ieee_sge_simple_union ieee_simple;
};

/**
 * union leapraid_sge_io_union - Combined SGE union for all I/O types
 *
 * @leapraid_simple: LeapRAID simple SGE format.
 * @leapraid_chain: LeapRAID chain SGE format.
 * @ieee_simple: IEEE simple SGE format.
 * @ieee_chain: IEEE chain SGE format.
 */
union leapraid_sge_io_union {
	struct leapraid_sge_simple_union leapraid_simple;
	struct leapraid_sge_chain_union leapraid_chain;
	union leapraid_ieee_sge_simple_union ieee_simple;
	union leapraid_ieee_sge_chain_union ieee_chain;
};

/**
 * struct leapraid_cfg_pg_header - Standard configuration page header
 *
 * @r1: Reserved.
 * @page_len: Length of the page in 4-byte units.
 * @page_num: Page number.
 * @page_type: Page type.
 */
struct leapraid_cfg_pg_header {
	u8 r1;
	u8 page_len;
	u8 page_num;
	u8 page_type;
};

/**
 * struct leapraid_cfg_ext_pg_header - Extended configuration page header
 *
 * @r1: Reserved.
 * @r2: Reserved.
 * @page_num: Page number.
 * @page_type: Page type.
 * @ext_page_len: Extended page length.
 * @ext_page_type: Extended page type.
 * @r3: Reserved.
 */
struct leapraid_cfg_ext_pg_header {
	u8 r1;
	u8 r2;
	u8 page_num;
	u8 page_type;
	__le16 ext_page_len;
	u8 ext_page_type;
	u8 r3;
};

/**
 * struct leapraid_cfg_req - Configuration request message
 *
 * @action: Requested action type.
 * @sgl_flag: SGL flag field.
 * @chain_offset: Offset to next chain SGE.
 * @func: Function code.
 * @ext_page_len: Extended page length.
 * @ext_page_type: Extended page type.
 * @msg_flag: Message flags.
 * @r1: Reserved.
 * @header: Configuration page header.
 * @page_addr: Address of the page buffer.
 * @page_buf_sge: SGE describing the page buffer.
 */
struct leapraid_cfg_req {
	u8 action;
	u8 sgl_flag;
	u8 chain_offset;
	u8 func;
	__le16 ext_page_len;
	u8 ext_page_type;
	u8 msg_flag;
	u8 r1[12];
	struct leapraid_cfg_pg_header header;
	__le32 page_addr;
	union leapraid_sge_io_union page_buf_sge;
};

/**
 * struct leapraid_cfg_rep - Configuration reply message
 *
 * @action: Action type from the request.
 * @r1: Reserved.
 * @msg_len: Message length in bytes.
 * @func: Function code.
 * @ext_page_len: Extended page length.
 * @ext_page_type: Extended page type.
 * @msg_flag: Message flags.
 * @r2: Reserved.
 * @adapter_status: Adapter status code.
 * @r3: Reserved.
 * @header: Configuration page header.
 */
struct leapraid_cfg_rep {
	u8 action;
	u8 r1;
	u8 msg_len;
	u8 func;
	__le16 ext_page_len;
	u8 ext_page_type;
	u8 msg_flag;
	u8 r2[6];
	__le16 adapter_status;
	u8 r3[4];
	struct leapraid_cfg_pg_header header;
};

/**
 * struct leapraid_boot_dev_format_sas_wwid - Boot device identified by wwid
 *
 * @sas_addr: SAS address of the device.
 * @lun: Logical unit number.
 * @r1: Reserved.
 */
struct leapraid_boot_dev_format_sas_wwid {
	__le64 sas_addr;
	u8 lun[8];
	u8 r1[8];
} __packed __aligned(4);

/**
 * struct leapraid_boot_dev_format_enc_slot - identified by enclosure
 *
 * @enc_lid: Enclosure logical ID.
 * @r1: Reserved.
 * @slot_num: Slot number in the enclosure.
 * @r2: Reserved.
 */
struct leapraid_boot_dev_format_enc_slot {
	__le64 enc_lid;
	u8 r1[8];
	__le16 slot_num;
	u8 r2[6];
} __packed __aligned(4);

/**
 * struct leapraid_boot_dev_format_dev_name - Boot device by device name
 *
 * @dev_name: Device name identifier.
 * @lun: Logical unit number.
 * @r1: Reserved.
 */
struct leapraid_boot_dev_format_dev_name {
	__le64 dev_name;
	u8 lun[8];
	u8 r1[8];
} __packed __aligned(4);

/**
 * union leapraid_boot_dev_format - Boot device format union
 *
 * @sas_wwid: Format using SAS WWID and LUN.
 * @enc_slot: Format using enclosure slot and ID.
 * @dev_name: Format using device name and LUN.
 */
union leapraid_boot_dev_format {
	struct leapraid_boot_dev_format_sas_wwid sas_wwid;
	struct leapraid_boot_dev_format_enc_slot enc_slot;
	struct leapraid_boot_dev_format_dev_name dev_name;
};

/**
 * struct leapraid_manufacturing_p0 - Manufacturing configuration page 0
 *
 * @header: Configuration page header.
 * @chip_name: Chip name as a 16-byte ASCII string.
 * @chip_revision: Chip revision as an 8-byte ASCII string.
 * @board_name: Board name as a 16-byte ASCII string.
 * @board_assembly: Board assembly as a 16-byte ASCII string.
 * @board_tracer_number: Board tracer number as a 16-byte ASCII string.
 */
struct leapraid_manufacturing_p0 {
	struct leapraid_cfg_pg_header header;
	u8 chip_name[16];
	u8 chip_revision[8];
	u8 board_name[16];
	u8 board_assembly[16];
	u8 board_tracer_number[16];
};

/**
 * struct leapraid_bios_page2 - BIOS configuration page 2
 *
 * @header: Configuration page header.
 * @r1: Reserved.
 * @requested_boot_dev_form: Format type of the requested boot device.
 * @r2: Reserved.
 * @requested_boot_dev: Boot device requested by BIOS or user.
 * @requested_alt_boot_dev_form: Format of the alternate boot device.
 * @r3: Reserved.
 * @requested_alt_boot_dev: Alternate boot device requested.
 * @current_boot_dev_form: Format type of the active boot device.
 * @r4: Reserved.
 * @current_boot_dev: Currently active boot device in use.
 */
struct leapraid_bios_page2 {
	struct leapraid_cfg_pg_header header;
	u8 r1[24];
	u8 requested_boot_dev_form;
	u8 r2[3];
	union leapraid_boot_dev_format requested_boot_dev;
	u8 requested_alt_boot_dev_form;
	u8 r3[3];
	union leapraid_boot_dev_format requested_alt_boot_dev;
	u8 current_boot_dev_form;
	u8 r4[3];
	union leapraid_boot_dev_format current_boot_dev;
};

/**
 * struct leapraid_bios_page3 - BIOS configuration page 3
 *
 * @header: Configuration page header.
 * @r1: Reserved.
 * @bios_version: BIOS firmware version number.
 * @r2: Reserved.
 */
struct leapraid_bios_page3 {
	struct leapraid_cfg_pg_header header;
	u8 r1[4];
	__le32 bios_version;
	u8 r2[84];
};

/**
 * struct leapraid_raidvol0_phys_disk - Physical disk in RAID volume
 *
 * @r1: Reserved.
 * @phys_disk_num: Physical disk number within the RAID volume.
 * @r2: Reserved.
 */
struct leapraid_raidvol0_phys_disk {
	u8 r1[2];
	u8 phys_disk_num;
	u8 r2;
};

/**
 * struct leapraid_raidvol_p0 - RAID volume configuration page 0
 *
 * @header: Configuration page header.
 * @dev_hdl: Device handle for the RAID volume.
 * @volume_state: State of the RAID volume.
 * @volume_type: RAID type.
 * @r1: Reserved.
 * @num_phys_disks: Number of physical disks in the volume.
 * @r2: Reserved.
 * @phys_disk: Array of physical disks in this volume.
 */
struct leapraid_raidvol_p0 {
	struct leapraid_cfg_pg_header header;
	__le16 dev_hdl;
	u8 volume_state;
	u8 volume_type;
	u8 r1[28];
	u8 num_phys_disks;
	u8 r2[3];
	struct leapraid_raidvol0_phys_disk phys_disk[];
};

/**
 * struct leapraid_raidvol_p1 - RAID volume configuration page 1
 *
 * @header: Configuration page header.
 * @dev_hdl: Device handle of the RAID volume.
 * @r1: Reserved.
 * @wwid: World-wide identifier for the volume.
 * @r2: Reserved.
 */
struct leapraid_raidvol_p1 {
	struct leapraid_cfg_pg_header header;
	__le16 dev_hdl;
	u8 r1[42];
	__le64 wwid;
	u8 r2[8];
} __packed __aligned(4);

/**
 * struct leapraid_raidpd_p0 - Physical disk configuration page 0
 *
 * @header: Configuration page header.
 * @dev_hdl: Device handle of the physical disk.
 * @r1: Reserved.
 * @phys_disk_num: Physical disk number.
 * @r2: Reserved.
 */
struct leapraid_raidpd_p0 {
	struct leapraid_cfg_pg_header header;
	__le16 dev_hdl;
	u8 r1;
	u8 phys_disk_num;
	u8 r2[112];
};

/**
 * struct leapraid_sas_io_unit0_phy_info - PHY info for SAS I/O unit
 *
 * @port: Port number the PHY belongs to.
 * @port_flg: Flags describing port status.
 * @phy_flg: Flags describing PHY status.
 * @neg_link_rate: Negotiated link rate of the PHY.
 * @controller_phy_dev_info: Controller PHY device info.
 * @attached_dev_hdl: Handle of attached device.
 * @controller_dev_hdl: Handle of the controller device.
 * @r1: Reserved.
 */
struct leapraid_sas_io_unit0_phy_info {
	u8 port;
	u8 port_flg;
	u8 phy_flg;
	u8 neg_link_rate;
	__le32 controller_phy_dev_info;
	__le16 attached_dev_hdl;
	__le16 controller_dev_hdl;
	u8 r1[8];
};

/**
 * struct leapraid_sas_io_unit_p0 - SAS I/O unit configuration page 0
 *
 * @header: Extended configuration page header.
 * @r1: Reserved.
 * @phy_num: Number of PHYs in this unit.
 * @r2: Reserved.
 * @phy_info: Array of PHY information.
 */
struct leapraid_sas_io_unit_p0 {
	struct leapraid_cfg_ext_pg_header header;
	u8 r1[4];
	u8 phy_num;
	u8 r2[3];
	struct leapraid_sas_io_unit0_phy_info phy_info[];
};

/**
 * struct leapraid_exp_p0 - SAS expander page 0
 *
 * @header: Extended page header.
 * @physical_port: Physical port number.
 * @r1: Reserved.
 * @enc_hdl: Enclosure handle.
 * @sas_address: SAS address of the expander.
 * @r2: Reserved.
 * @dev_hdl: Device handle of this expander.
 * @parent_dev_hdl: Device handle of parent expander.
 * @r3: Reserved.
 * @phy_num: Number of PHYs.
 * @r4: Reserved.
 */
struct leapraid_exp_p0 {
	struct leapraid_cfg_ext_pg_header header;
	u8 physical_port;
	u8 r1;
	__le16 enc_hdl;
	__le64 sas_address;
	u8 r2[4];
	__le16 dev_hdl;
	__le16 parent_dev_hdl;
	u8 r3[4];
	u8 phy_num;
	u8 r4[27];
} __packed __aligned(4);

/**
 * struct leapraid_exp_p1 - SAS expander page 1
 *
 * @header: Extended page header.
 * @r1: Reserved.
 * @p_link_rate: PHY link rate.
 * @hw_link_rate: Hardware supported link rate.
 * @attached_dev_hdl: Attached device handle.
 * @r2: Reserved.
 * @neg_link_rate: Negotiated link rate.
 * @r3: Reserved.
 */
struct leapraid_exp_p1 {
	struct leapraid_cfg_ext_pg_header header;
	u8 r1[8];
	u8 p_link_rate;
	u8 hw_link_rate;
	__le16 attached_dev_hdl;
	u8 r2[11];
	u8 neg_link_rate;
	u8 r3[12];
};

/**
 * struct leapraid_sas_dev_p0 - SAS device page 0
 *
 * @header: Extended configuration page header.
 * @slot: Slot number.
 * @enc_hdl: Enclosure handle.
 * @sas_address: SAS address.
 * @parent_dev_hdl: Parent device handle.
 * @phy_num: Number of PHYs.
 * @r1: Reserved.
 * @dev_hdl: Device handle.
 * @r2: Reserved.
 * @dev_info: Device information.
 * @flg: Flags.
 * @physical_port: Physical port number.
 * @max_port_connections: Maximum port connections.
 * @dev_name: Device name.
 * @port_groups: Number of port groups.
 * @r3: Reserved.
 * @enc_level: Enclosure level.
 * @connector_name: Connector identifier.
 * @r4: Reserved.
 */
struct leapraid_sas_dev_p0 {
	struct leapraid_cfg_ext_pg_header header;
	__le16 slot;
	__le16 enc_hdl;
	__le64 sas_address;
	__le16 parent_dev_hdl;
	u8 phy_num;
	u8 r1;
	__le16 dev_hdl;
	u8 r2[2];
	__le32 dev_info;
	__le16 flg;
	u8 physical_port;
	u8 max_port_connections;
	__le64 dev_name;
	u8 port_groups;
	u8 r3[2];
	u8 enc_level;
	u8 connector_name[LEAPRAID_SAS_DEV_P0_CON_NAME_LEN];
	u8 r4[4];
} __packed __aligned(4);

/**
 * struct leapraid_sas_phy_p0 - SAS PHY configuration page 0
 *
 * @header: Extended configuration page header.
 * @r1: Reserved.
 * @attached_dev_hdl: Handle of attached device.
 * @r2: Reserved.
 * @p_link_rate: PHY link rate.
 * @hw_link_rate: Hardware supported link rate.
 * @r3: Reserved.
 * @phy_info: PHY information.
 * @neg_link_rate: Negotiated link rate.
 * @r4: Reserved.
 */
struct leapraid_sas_phy_p0 {
	struct leapraid_cfg_ext_pg_header header;
	u8 r1[4];
	__le16 attached_dev_hdl;
	u8 r2[6];
	u8 p_link_rate;
	u8 hw_link_rate;
	u8 r3[2];
	__le32 phy_info;
	u8 neg_link_rate;
	u8 r4[3];
};

/**
 * struct leapraid_enc_p0 - SAS enclosure page 0
 *
 * @header: Extended configuration page header.
 * @r1: Reserved.
 * @enc_lid: Enclosure logical ID.
 * @r2: Reserved.
 * @enc_hdl: Enclosure handle.
 * @r3: Reserved.
 */
struct leapraid_enc_p0 {
	struct leapraid_cfg_ext_pg_header header;
	u8 r1[4];
	__le64 enc_lid;
	u8 r2[2];
	__le16 enc_hdl;
	u8 r3[15];
} __packed __aligned(4);

/**
 * struct leapraid_raid_cfg_p0_element - RAID configuration element
 *
 * @element_flg: Element flags.
 * @vol_dev_hdl: Volume device handle.
 * @r1: Reserved.
 * @phys_disk_dev_hdl: Physical disk device handle.
 */
struct leapraid_raid_cfg_p0_element {
	__le16 element_flg;
	__le16 vol_dev_hdl;
	u8 r1[2];
	__le16 phys_disk_dev_hdl;
};

/**
 * struct leapraid_raid_cfg_p0 - RAID configuration page 0
 *
 * @header: Extended configuration page header.
 * @r1: Reserved.
 * @cfg_num: Configuration number.
 * @r2: Reserved.
 * @elements_num: Number of RAID elements.
 * @r3: Reserved.
 * @cfg_element: Array of RAID elements.
 */
struct leapraid_raid_cfg_p0 {
	struct leapraid_cfg_ext_pg_header header;
	u8 r1[3];
	u8 cfg_num;
	u8 r2[32];
	u8 elements_num;
	u8 r3[3];
	struct leapraid_raid_cfg_p0_element cfg_element[];
};

/**
 * union leapraid_mpi_scsi_io_cdb_union - SCSI I/O CDB or simple SGE
 *
 * @cdb32: 32-byte SCSI command descriptor block.
 * @sge: Simple SGE format.
 */
union leapraid_mpi_scsi_io_cdb_union {
	u8 cdb32[32];
	struct leapraid_sge_simple_union sge;
};

/**
 * struct leapraid_mpi_scsiio_req - MPI SCSI I/O request
 *
 * @dev_hdl: Device handle for the target.
 * @chain_offset: Offset for chained SGE.
 * @func: Function code.
 * @r1: Reserved.
 * @msg_flg: Message flags.
 * @r2: Reserved.
 * @sense_buffer_low_add: Lower 32-bit address of sense buffer.
 * @dma_flag: DMA flags.
 * @r3: Reserved.
 * @sense_buffer_len: Sense buffer length.
 * @r4: Reserved.
 * @sgl_offset0..3: SGL offsets.
 * @skip_count: Bytes to skip before transfer.
 * @data_len: Length of data transfer.
 * @bi_dir_data_len: Bi-directional transfer length.
 * @io_flg: I/O flags.
 * @eedp_flag: End-to-end data protection flags.
 * @eedp_block_size: End-to-end data protection block size.
 * @r5: Reserved.
 * @secondary_ref_tag: Secondary reference tag.
 * @secondary_app_tag: Secondary application tag.
 * @app_tag_trans_mask: Application tag mask.
 * @lun: Logical Unit Number.
 * @ctrl: Control flags.
 * @cdb: SCSI Command Descriptor Block or simple SGE.
 * @sgl: Scatter-gather list.
 */
struct leapraid_mpi_scsiio_req {
	__le16 dev_hdl;
	u8 chain_offset;
	u8 func;
	u8 r1[3];
	u8 msg_flg;
	u8 r2[4];
	__le32 sense_buffer_low_add;
	u8 dma_flag;
	u8 r3;
	u8 sense_buffer_len;
	u8 r4;
	u8 sgl_offset0;
	u8 sgl_offset1;
	u8 sgl_offset2;
	u8 sgl_offset3;
	__le32 skip_count;
	__le32 data_len;
	__le32 bi_dir_data_len;
	__le16 io_flg;
	__le16 eedp_flag;
	__le16 eedp_block_size;
	u8 r5[2];
	__le32 secondary_ref_tag;
	__le16 secondary_app_tag;
	__le16 app_tag_trans_mask;
	u8 lun[8];
	__le32 ctrl;
	union leapraid_mpi_scsi_io_cdb_union cdb;
	union leapraid_sge_io_union sgl;
};

/**
 * union leapraid_scsi_io_cdb_union - SCSI I/O CDB or IEEE simple SGE
 *
 * @cdb32: 32-byte SCSI CDB.
 * @sge: IEEE simple 64-bit SGE.
 */
union leapraid_scsi_io_cdb_union {
	u8 cdb32[32];
	struct leapraid_ieee_sge_simple64 sge;
};

/**
 * struct leapraid_scsiio_req - SCSI I/O request
 *
 * @dev_hdl: Device handle.
 * @chain_offset: Offset for chained SGE.
 * @func: Function code.
 * @r1: Reserved.
 * @msg_flg: Message flags.
 * @r2: Reserved.
 * @sense_buffer_low_add: Lower 32-bit address of sense buffer.
 * @dma_flag: DMA flag.
 * @r3: Reserved.
 * @sense_buffer_len: Sense buffer length.
 * @r4: Reserved.
 * @sgl_offset0-3: SGL offsets.
 * @skip_count: Bytes to skip before transfer.
 * @data_len: Length of data transfer.
 * @bi_dir_data_len: Bi-directional transfer length.
 * @io_flg: I/O flags.
 * @eedp_flag: End-to-end data protection flags.
 * @eedp_block_size: End-to-end data protection block size.
 * @r5: Reserved.
 * @secondary_ref_tag: Secondary reference tag.
 * @secondary_app_tag: Secondary application tag.
 * @app_tag_trans_mask: Application tag mask.
 * @lun: Logical Unit Number.
 * @ctrl: Control flags.
 * @cdb: SCSI Command Descriptor Block or simple SGE.
 * @sgl: Scatter-gather list.
 */
struct leapraid_scsiio_req {
	__le16 dev_hdl;
	u8 chain_offset;
	u8 func;
	u8 r1[3];
	u8 msg_flg;
	u8 r2[4];
	__le32 sense_buffer_low_add;
	u8 dma_flag;
	u8 r3;
	u8 sense_buffer_len;
	u8 r4;
	u8 sgl_offset0;
	u8 sgl_offset1;
	u8 sgl_offset2;
	u8 sgl_offset3;
	__le32 skip_count;
	__le32 data_len;
	__le32 bi_dir_data_len;
	__le16 io_flg;
	__le16 eedp_flag;
	__le16 eedp_block_size;
	u8 r5[2];
	__le32 secondary_ref_tag;
	__le16 secondary_app_tag;
	__le16 app_tag_trans_mask;
	u8 lun[8];
	__le32 ctrl;
	union leapraid_scsi_io_cdb_union cdb;
	union leapraid_ieee_sge_io_union sgl;
};

/**
 * struct leapraid_scsiio_rep - SCSI I/O response
 *
 * @dev_hdl: Device handle.
 * @msg_len: Length of response message.
 * @func: Function code.
 * @r1: Reserved.
 * @msg_flg: Message flags.
 * @r2: Reserved.
 * @scsi_status: SCSI status.
 * @scsi_state: SCSI state.
 * @adapter_status: Adapter status.
 * @r3: Reserved.
 * @transfer_count: Number of bytes transferred.
 * @sense_count: Number of sense bytes.
 * @resp_info: Additional response info.
 * @task_tag: Task identifier.
 * @scsi_status_qualifier: SCSI status qualifier.
 * @bi_dir_trans_count: Bi-directional transfer count.
 * @r4: Reserved.
 */
struct leapraid_scsiio_rep {
	__le16 dev_hdl;
	u8 msg_len;
	u8 func;
	u8 r1[3];
	u8 msg_flg;
	u8 r2[4];
	u8 scsi_status;
	u8 scsi_state;
	__le16 adapter_status;
	u8 r3[4];
	__le32 transfer_count;
	__le32 sense_count;
	__le32 resp_info;
	__le16 task_tag;
	__le16 scsi_status_qualifier;
	__le32 bi_dir_trans_count;
	__le32 r4[3];
};

/**
 * struct leapraid_scsi_tm_req - SCSI Task Management request
 *
 * @dev_hdl: Device handle.
 * @chain_offset: Offset for chained SGE.
 * @func: Function code.
 * @r1: Reserved.
 * @task_type: Task management function type.
 * @r2: Reserved.
 * @msg_flg: Message flags.
 * @r3: Reserved.
 * @lun: Logical Unit Number.
 * @r4: Reserved.
 * @task_mid: Task identifier.
 * @r5: Reserved.
 */
struct leapraid_scsi_tm_req {
	__le16 dev_hdl;
	u8 chain_offset;
	u8 func;
	u8 r1;
	u8 task_type;
	u8 r2;
	u8 msg_flg;
	u8 r3[4];
	u8 lun[8];
	u8 r4[28];
	__le16 task_mid;
	u8 r5[2];
};

/**
 * struct leapraid_scsi_tm_rep - SCSI Task Management response
 *
 * @dev_hdl: Device handle.
 * @msg_len: Length of response message.
 * @func: Function code.
 * @resp_code: Response code.
 * @task_type: Task management type.
 * @r1: Reserved.
 * @msg_flag: Message flags.
 * @r2: Reserved.
 * @adapter_status: Adapter status.
 * @r3: Reserved.
 * @termination_count: Count of terminated tasks.
 * @response_info: Additional response info.
 */
struct leapraid_scsi_tm_rep {
	__le16 dev_hdl;
	u8 msg_len;
	u8 func;
	u8 resp_code;
	u8 task_type;
	u8 r1;
	u8 msg_flag;
	u8 r2[6];
	__le16 adapter_status;
	u8 r3[4];
	__le32 termination_count;
	__le32 response_info;
};

/**
 * struct leapraid_sep_req - SEP (SCSI Enclosure Processor) request
 *
 * @dev_hdl: Device handle.
 * @chain_offset: Offset for chained SGE.
 * @func: Function code.
 * @act: Action to perform.
 * @flg: Flags.
 * @r1: Reserved.
 * @msg_flag: Message flags.
 * @r2: Reserved.
 * @slot_status: Slot status.
 * @r3: Reserved.
 * @slot: Slot number.
 * @enc_hdl: Enclosure handle.
 */
struct leapraid_sep_req {
	__le16 dev_hdl;
	u8 chain_offset;
	u8 func;
	u8 act;
	u8 flg;
	u8 r1;
	u8 msg_flag;
	u8 r2[4];
	__le32 slot_status;
	u8 r3[12];
	__le16 slot;
	__le16 enc_hdl;
};

/**
 * struct leapraid_sep_rep - SEP response
 *
 * @dev_hdl: Device handle.
 * @msg_len: Message length.
 * @func: Function code.
 * @act: Action performed.
 * @flg: Flags.
 * @msg_flag: Message flags.
 * @r1: Reserved.
 * @adapter_status: Adapter status.
 * @r2: Reserved.
 * @slot_status: Slot status.
 * @r3: Reserved.
 * @slot: Slot number.
 * @enc_hdl: Enclosure handle.
 */
struct leapraid_sep_rep {
	__le16 dev_hdl;
	u8 msg_len;
	u8 func;
	u8 act;
	u8 flg;
	u8 r1;
	u8 msg_flag;
	u8 r2[6];
	__le16 adapter_status;
	u8 r3[4];
	__le32 slot_status;
	u8 r4[4];
	__le16 slot;
	__le16 enc_hdl;
};

/**
 * struct leapraid_adapter_init_req - Adapter initialization request
 *
 * @who_init: Initiator of the initialization.
 * @r1: Reserved.
 * @chain_offset: Chain offset.
 * @func: Function code.
 * @r2: Reserved.
 * @msg_flg: Message flags.
 * @r3: Reserved.
 * @msg_ver: Message version.
 * @header_ver: Header version.
 * @host_buf_addr: Host buffer address (non adapter-ref).
 * @r4: Reserved.
 * @host_buf_size: Host buffer size (non adapter-ref).
 * @host_msix_vectors: Number of host MSI-X vectors.
 * @r6: Reserved.
 * @req_frame_size: Request frame size.
 * @rep_desc_qd: Reply descriptor queue depth.
 * @rep_msg_qd: Reply message queue depth.
 * @sense_buffer_add_high: High 32-bit of sense buffer address.
 * @rep_msg_dma_high: High 32-bit of reply message DMA address.
 * @task_desc_base_addr: Base address of task descriptors.
 * @rep_desc_q_arr_addr: Address of reply descriptor queue array.
 * @rep_msg_addr_dma: Reply message DMA address.
 * @time_stamp: Timestamp.
 */
struct leapraid_adapter_init_req {
	u8 who_init;
	u8 r1;
	u8 chain_offset;
	u8 func;
	u8 r2[3];
	u8 msg_flg;
	__le32 driver_ver;
	__le16 msg_ver;
	__le16 header_ver;
	__le32 host_buf_addr;
	u8 r4[2];
	u8 host_buf_size;
	u8 host_msix_vectors;
	u8 r6[2];
	__le16 req_frame_size;
	__le16 rep_desc_qd;
	__le16 rep_msg_qd;
	__le32 sense_buffer_add_high;
	__le32 rep_msg_dma_high;
	__le64 task_desc_base_addr;
	__le64 rep_desc_q_arr_addr;
	__le64 rep_msg_addr_dma;
	__le64 time_stamp;
} __packed __aligned(4);

/**
 * struct leapraid_rep_desc_q_arr - Reply descriptor queue array
 *
 * @rep_desc_base_addr: Base address of the reply descriptors.
 * @r1: Reserved.
 */
struct leapraid_rep_desc_q_arr {
	__le64 rep_desc_base_addr;
	__le64 r1;
} __packed __aligned(4);

/**
 * struct leapraid_adapter_init_rep - Adapter initialization reply
 *
 * @who_init: Initiator of the initialization.
 * @r1: Reserved.
 * @msg_len: Length of reply message.
 * @func: Function code.
 * @r2: Reserved.
 * @msg_flag: Message flags.
 * @r3: Reserved.
 * @adapter_status: Adapter status.
 * @r4: Reserved.
 */
struct leapraid_adapter_init_rep {
	u8 who_init;
	u8 r1;
	u8 msg_len;
	u8 func;
	u8 r2[3];
	u8 msg_flag;
	u8 r3[6];
	__le16 adapter_status;
	u8 r4[4];
};

/**
 * struct leapraid_adapter_log_req - Adapter log request
 *
 * @action: Action code.
 * @type: Log type.
 * @chain_offset: Offset for chained SGE.
 * @func: Function code.
 * r1: Reserved.
 * @msg_flag: Message flags.
 * r2: Reserved.
 * @mbox: Mailbox for command-specific parameters.
 * @sge: Scatter-gather entry for data buffer.
 */
struct leapraid_adapter_log_req {
	u8 action;
	u8 type;
	u8 chain_offset;
	u8 func;
	u8 r1[3];
	u8 msg_flag;
	u8 r2[4];
	union {
		u8	b[12];
		__le16	s[6];
		__le32	w[3];
	} mbox;
	struct leapraid_sge_simple64 sge;
} __packed __aligned(4);

/**
 * struct leapraid_adapter_log_rep - Adapter log reply
 *
 * @action: Action code echoed.
 * @type: Log type echoed.
 * @msg_len: Length of message.
 * @func: Function code.
 * @r1: Reserved.
 * @msg_flag: Message flags.
 * @r2: Reserved.
 * @adapter_status: Status returned by adapter.
 */
struct leapraid_adapter_log_rep {
	u8 action;
	u8 type;
	u8 msg_len;
	u8 func;
	u8 r1[3];
	u8 msg_flag;
	u8 r2[6];
	__le16 adapter_status;
};

/**
 * struct leapraid_adapter_features_req - Request adapter features
 *
 * @r1: Reserved.
 * @chain_offset: Offset for chained SGE.
 * @func: Function code.
 * @r2: Reserved.
 * @msg_flag: Message flags.
 * @r3: Reserved.
 */
struct leapraid_adapter_features_req {
	u8 r1[2];
	u8 chain_offset;
	u8 func;
	u8 r2[3];
	u8 msg_flag;
	u8 r3[4];
};

/**
 * struct leapraid_adapter_features_rep - Adapter features reply
 *
 * @msg_ver: Message version.
 * @msg_len: Length of reply message.
 * @func: Function code.
 * @header_ver: Header version.
 * @r1: Reserved.
 * @msg_flag: Message flags.
 * @r2: Reserved.
 * @adapter_status: Adapter status.
 * @r3: Reserved.
 * @who_init: Who initialized the adapter.
 * @r4: Reserved.
 * @max_msix_vectors: Max MSI-X vectors supported.
 * @req_slot: Number of request slots.
 * @r5: Reserved.
 * @adapter_caps: Adapter capabilities.
 * @fw_version: Firmware version.
 * @sas_wide_max_qdepth: Max wide SAS queue depth.
 * @sas_narrow_max_qdepth: Max narrow SAS queue depth.
 * @r6: Reserved.
 * @hp_slot: Number of high-priority slots.
 * @r7: Reserved.
 * @max_volumes: Maximum supported volumes.
 * @max_dev_hdl: Maximum device handle.
 * @r8: Reserved.
 * @min_dev_hdl: Minimum device handle.
 * @r9: Reserved.
 */
struct leapraid_adapter_features_rep {
	__le16 msg_ver;
	u8 msg_len;
	u8 func;
	u16 header_ver;
	u8 r1;
	u8 msg_flag;
	u8 r2[6];
	u16 adapter_status;
	u8 r3[4];
	u8 sata_max_qdepth;
	u8 who_init;
	u8 r4;
	u8 max_msix_vectors;
	__le16 req_slot;
	__le16 product_id;
	__le32 adapter_caps;
	__le32 fw_version;
	__le16 sas_wide_max_qdepth;
	__le16 sas_narrow_max_qdepth;
	u8 r6[10];
	__le16 hp_slot;
	u8 r7[3];
	u8 max_volumes;
	__le16 max_dev_hdl;
	u8 r8[2];
	__le16 min_dev_hdl;
	u8 r9[6];
};

/**
 * struct leapraid_scan_dev_req - Request to scan devices
 *
 * @r1: Reserved.
 * @chain_offset: Offset for chained SGE.
 * @func: Function code.
 * @r2: Reserved.
 * @msg_flag: Message flags.
 * @r3: Reserved.
 */
struct leapraid_scan_dev_req {
	u8 r1[2];
	u8 chain_offset;
	u8 func;
	u8 r2[3];
	u8 msg_flag;
	u8 r3[4];
};

/**
 * struct leapraid_scan_dev_rep - Scan devices reply
 *
 * @r1: Reserved.
 * @msg_len: Length of message.
 * @func: Function code.
 * @r2: Reserved.
 * @msg_flag: Message flags.
 * @r3: Reserved.
 * @adapter_status: Adapter status.
 * @r4: Reserved.
 */
struct leapraid_scan_dev_rep {
	u8 r1[2];
	u8 msg_len;
	u8 func;
	u8 r2[3];
	u8 msg_flag;
	u8 r3[6];
	__le16 adapter_status;
	u8 r4[4];
};

/**
 * struct leapraid_evt_notify_req - Event notification request
 *
 * @r1: Reserved.
 * @chain_offset: Offset for chained SGE.
 * @func: Function code.
 * @r2: Reserved.
 * @msg_flag: Message flags.
 * @r3: Reserved.
 * @evt_masks: Event masks to enable notifications.
 * @r4: Reserved.
 */
struct leapraid_evt_notify_req {
	u8 r1[2];
	u8 chain_offset;
	u8 func;
	u8 r2[3];
	u8 msg_flag;
	u8 r3[12];
	__le32 evt_masks[4];
	u8 r4[8];
};

/**
 * struct leapraid_evt_notify_rep - Event notification reply
 *
 * @evt_data_len: Length of event data.
 * @msg_len: Length of message.
 * @func: Function code.
 * @r1: Reserved.
 * @r2: Reserved.
 * @msg_flag: Message flags.
 * @r3: Reserved.
 * @adapter_status: Adapter status.
 * @r4: Reserved.
 * @evt: Event code.
 * @r5: Reserved.
 * @evt_data: Event data array.
 */
struct leapraid_evt_notify_rep {
	__le16 evt_data_len;
	u8 msg_len;
	u8 func;
	u8 r1[2];
	u8 r2;
	u8 msg_flag;
	u8 r3[6];
	__le16 adapter_status;
	u8 r4[4];
	__le16 evt;
	u8 r5[6];
	__le32 evt_data[];
};

/**
 * struct leapraid_evt_data_sas_dev_status_change - SAS device status change
 *
 * @task_tag: Task identifier.
 * @reason_code: Reason for status change.
 * @physical_port: Physical port number.
 * @r1: Reserved.
 * @dev_hdl: Device handle.
 * @r2: Reserved.
 * @sas_address: SAS address of device.
 * @lun: Logical unit Number.
 */
struct leapraid_evt_data_sas_dev_status_change {
	__le16 task_tag;
	u8 reason_code;
	u8 physical_port;
	u8 r1[2];
	__le16 dev_hdl;
	u8 r2[4];
	__le64 sas_address;
	u8 lun[8];
} __packed __aligned(4);
/**
 * struct leapraid_evt_data_ir_change - IR (Integrated RAID) change event data
 *
 * @r1: Reserved.
 * @reason_code: Reason for IR change.
 * @r2: Reserved.
 * @vol_dev_hdl: Volume device handle.
 * @phys_disk_dev_hdl: Physical disk device handle.
 */
struct leapraid_evt_data_ir_change {
	u8 r1;
	u8 reason_code;
	u8 r2[2];
	__le16 vol_dev_hdl;
	__le16 phys_disk_dev_hdl;
};

/**
 * struct leapraid_evt_data_sas_disc - SAS discovery event data
 *
 * @r1: Reserved.
 * @reason_code: Reason for discovery event.
 * @physical_port: Physical port number where event occurred.
 * @r2: Reserved.
 */
struct leapraid_evt_data_sas_disc {
	u8 r1;
	u8 reason_code;
	u8 physical_port;
	u8 r2[5];
};

/**
 * struct leapraid_evt_sas_topo_phy_entry - SAS topology PHY entry
 *
 * @attached_dev_hdl: Device handle attached to PHY.
 * @link_rate: Current link rate.
 * @phy_status: PHY status flags.
 */
struct leapraid_evt_sas_topo_phy_entry {
	__le16 attached_dev_hdl;
	u8 link_rate;
	u8 phy_status;
};

/**
 * struct leapraid_evt_data_sas_topo_change_list - SAS topology change list
 *
 * @encl_hdl: Enclosure handle.
 * @exp_dev_hdl: Expander device handle.
 * @num_phys: Number of PHYs in this entry.
 * @r1: Reserved.
 * @entry_num: Number of PHY elements.
 * @start_phy_num: Start PHY number.
 * @exp_status: Expander status.
 * @physical_port: Physical port number.
 * @phy: Array of SAS PHY entries.
 */
struct leapraid_evt_data_sas_topo_change_list {
	__le16 encl_hdl;
	__le16 exp_dev_hdl;
	u8 num_phys;
	u8 r1[3];
	u8 entry_num;
	u8 start_phy_num;
	u8 exp_status;
	u8 physical_port;
	struct leapraid_evt_sas_topo_phy_entry phy[];
};

/**
 * struct leapraid_evt_data_sas_enc_dev_status_change -
 * SAS enclosure device status
 *
 * @enc_hdl: Enclosure handle.
 * @reason_code: Reason code for status change.
 * @physical_port: Physical port number.
 * @encl_logical_id: Enclosure logical ID.
 * @num_slots: Number of slots in enclosure.
 * @start_slot: First affected slot.
 * @phy_bits: Bitmap of affected PHYs.
 */
struct leapraid_evt_data_sas_enc_dev_status_change {
	__le16 enc_hdl;
	u8 reason_code;
	u8 physical_port;
	__le64 encl_logical_id;
	__le16 num_slots;
	__le16 start_slot;
	__le32 phy_bits;
} __packed __aligned(4);

/**
 * struct leapraid_io_unit_ctrl_req - I/O unit control request
 *
 * @op: Operation code.
 * @r1: Reserved.
 * @chain_offset: SGE chain offset.
 * @func: Function code.
 * @dev_hdl: Device handle.
 * @adapter_para: Adapter parameter selector.
 * @msg_flag: Message flags.
 * @r2: Reserved.
 * @phy_num: PHY number.
 * @r3: Reserved.
 * @adapter_para_value: Value for adapter parameter.
 * @adapter_para_value2: Optional second parameter value.
 * @r4: Reserved.
 */
struct leapraid_io_unit_ctrl_req {
	u8 op;
	u8 r1;
	u8 chain_offset;
	u8 func;
	__le16 dev_hdl;
	u8 adapter_para;
	u8 msg_flag;
	u8 r2[6];
	u8 phy_num;
	u8 r3[17];
	__le32 adapter_para_value;
	__le32 adapter_para_value2;
	u8 r4[4];
};

/**
 * struct leapraid_io_unit_ctrl_rep - I/O unit control reply
 *
 * @op: Operation code echoed.
 * @r1: Reserved.
 * @func: Function code.
 * @dev_hdl: Device handle.
 * @r2: Reserved.
 */
struct leapraid_io_unit_ctrl_rep {
	u8 op;
	u8 r1[2];
	u8 func;
	__le16 dev_hdl;
	u8 r2[14];
};

/**
 * struct leapraid_raid_act_req - RAID action request
 *
 * @act: RAID action code.
 * @r1: Reserved.
 * @func: Function code.
 * @r2: Reserved.
 * @phys_disk_num: Number of physical disks involved.
 * @r3: Reserved.
 * @action_data_sge: SGE describing action-specific data.
 */
struct leapraid_raid_act_req {
	u8 act;
	u8 r1[2];
	u8 func;
	u8 r2[2];
	u8 phys_disk_num;
	u8 r3[13];
	struct leapraid_sge_simple_union action_data_sge;
};

/**
 * struct leapraid_raid_act_rep - RAID action reply
 *
 * @act: RAID action code echoed.
 * @r1: Reserved.
 * @func: Function code.
 * @vol_dev_hdl: Volume device handle.
 * @r2: Reserved
 * @adapter_status: Status returned by adapter.
 * @r3: Reserved.
 */
struct leapraid_raid_act_rep {
	u8 act;
	u8 r1[2];
	u8 func;
	__le16 vol_dev_hdl;
	u8 r2[8];
	__le16 adapter_status;
	u8 r3[76];
};

/**
 * struct leapraid_smp_passthrough_req - SMP passthrough request
 *
 * @passthrough_flg: Passthrough flags.
 * @physical_port: Target PHY port.
 * @r1: Reserved.
 * @func: Function code.
 * @req_data_len: Request data length.
 * @r2: Reserved.
 * @sas_address: SAS address of target device.
 * @r3: Reserved.
 * @sgl: Scatter-gather list describing request buffer.
 */
struct leapraid_smp_passthrough_req {
	u8 passthrough_flg;
	u8 physical_port;
	u8 r1;
	u8 func;
	__le16 req_data_len;
	u8 r2[10];
	__le64 sas_address;
	u8 r3[8];
	union leapraid_simple_sge_union sgl;
} __packed __aligned(4);

/**
 * struct leapraid_smp_passthrough_rep - SMP passthrough reply
 *
 * @passthrough_flg: Passthrough flags echoed.
 * @physical_port: Target PHY port.
 * @r1: Reserved.
 * @func: Function code.
 * @resp_data_len: Length of response data.
 * @r2: Reserved.
 * @adapter_status: Adapter status.
 * @r3: Reserved.
 */
struct leapraid_smp_passthrough_rep {
	u8 passthrough_flg;
	u8 physical_port;
	u8 r1;
	u8 func;
	__le16 resp_data_len;
	u8 r2[8];
	__le16 adapter_status;
	u8 r3[12];
};

/**
 * struct leapraid_sas_io_unit_ctrl_req - SAS I/O unit control request
 *
 * @op: Operation code.
 * @r1: Reserved.
 * @func: Function code.
 * @dev_hdl: Device handle.
 * @r2: Reserved.
 */
struct leapraid_sas_io_unit_ctrl_req {
	u8 op;
	u8 r1[2];
	u8 func;
	__le16 dev_hdl;
	u8 r2[38];
};

#endif /* LEAPRAID_H */
