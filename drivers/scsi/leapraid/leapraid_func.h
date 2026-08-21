/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 LeapIO Tech Inc.
 *
 * LeapRAID storage and RAID controller driver.
 */
#ifndef LEAPRAID_FUNC_H_INCLUDED
#define LEAPRAID_FUNC_H_INCLUDED

#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/poll.h>
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <scsi/scsi.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsicam.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport_sas.h>

#include "leapraid.h"

/* Request and reply buffer size. */
#define LEAPRAID_REQUEST_SIZE                   128
#define LEAPRAID_REPLY_SIZE                     128
#define LEAPRAID_CHAIN_SEG_SIZE                 128
#define LEAPRAID_MAX_SGES_IN_CHAIN              7
#define LEAPRAID_DEFAULT_CHAINS_PER_IO          19
#define LEAPRAID_DEFAULT_DIX_CHAINS_PER_IO      \
	(2 * LEAPRAID_DEFAULT_CHAINS_PER_IO) /* TODO DIX */
#define LEAPRAID_IEEE_SGE64_ENTRY_SIZE          16
#define LEAPRAID_REP_DESC_CHUNK_SIZE            16
#define LEAPRAID_REP_DESC_ENTRY_SIZE            8
#define LEAPRAID_REP_MSG_ADDR_SIZE              4
#define LEAPRAID_REP_RQ_CNT_SIZE                16

#define LEAPRAID_SYS_LOG_BUF_SIZE       0x200000
#define LEAPRAID_SYS_LOG_BUF_RESERVE    0x1000

/* Driver version and name. */
#define LEAPRAID_DRIVER_NAME            "LeapRAID"
#define LEAPRAID_NAME_LENGTH            48
#define LEAPRAID_BOARD_NAME_LENGTH      17
#define LEAPRAID_AUTHOR                 "LeapIO Inc."
#define LEAPRAID_DESCRIPTION            "LeapRAID Driver"
#define LEAPRAID_DRIVER_VERSION         "2.00.01.09"
#define LEAPRAID_MAJOR_VERSION          2
#define LEAPRAID_MINOR_VERSION          0
#define LEAPRAID_BUILD_VERSION          1
#define LEAPRAID_RELEASE_VERSION        9
#define LEAPRAID_MSG_VERSION            0x1021
#define LEAPRAID_HEADER_VERSION         0x0000

/* Device ID. */
#define LEAPRAID_VENDOR_ID      0xD405
#define LEAPRAID_DEVID_HBA      0x8200
#define LEAPRAID_SUBVENDOR_ID   0xD405
#define LEAPRAID_SUBDEVID_HBA   0x8200

#define LEAPRAID_PCI_VENDOR_ID_MASK     0xFFFF

 /* RAID virtual channel ID. */
#define RAID_CHANNEL    1

/* Scatter-gather (SG) segment limits. */
#define LEAPRAID_MAX_PHYS_SEGMENTS      SG_CHUNK_SIZE

#define LEAPRAID_KDUMP_MIN_PHYS_SEGMENTS        32
#define LEAPRAID_SG_DEPTH                       LEAPRAID_MAX_PHYS_SEGMENTS

/* Firmware and config page operations. */
#define LEAPRAID_CFG_REQ_RETRY_TIMES    2

/* Hardware access helpers. */
#define leapraid_readl(addr) readl(addr)
#define leapraid_check_reset(status) \
	(!((status) & LEAPRAID_CMD_RESET))

/* Polling intervals. */
#define LEAPRAID_PCIE_LOG_POLLING_INTERVAL      1
#define LEAPRAID_FAULT_POLLING_INTERVAL         1000

/* Init mask. */
#define LEAPRAID_RESET_IRQ_MASK 0x40000000
#define LEAPRAID_REPLY_INT_MASK 0x00000008
#define LEAPRAID_TO_SYS_DB_MASK 0x00000001

/* Queue depth. */
#define LEAPRAID_SATA_QUEUE_DEPTH       32
#define LEAPRAID_SAS_QUEUE_DEPTH        64
#define LEAPRAID_RAID_QUEUE_DEPTH       64

/* Target probe flag. */
#define LEAPRAID_NO_ULD_ATTACH_FLAG 1

/* SCSI device and queue limits. */
#define LEAPRAID_MAX_SECTORS            2048
#define LEAPRAID_MAX_CDB_LEN            32
#define LEAPRAID_MAX_LUNS               16384
#define LEAPRAID_CAN_QUEUE_MIN          1
#define LEAPRAID_THIS_ID_NONE           -1
#define LEAPRAID_CMD_PER_LUN            128
#define LEAPRAID_MAX_SEGMENT_SIZE       0xffffffff

/* SCSI sense and ASC/ASCQ and disk geometry configuration. */
#define DESC_FORMAT_THRESHOLD                   0x72
#define SENSE_KEY_MASK                          0x0F
#define SCSI_SENSE_RESPONSE_CODE_MASK           0x7F
#define ASC_FAILURE_PREDICTION_THRESHOLD_EXCEEDED       0x5D
#define LEAPRAID_LARGE_DISK_THRESHOLD           0x200000UL
#define LEAPRAID_LARGE_DISK_HEADS               255
#define LEAPRAID_LARGE_DISK_SECTORS             63
#define LEAPRAID_SMALL_DISK_HEADS               64
#define LEAPRAID_SMALL_DISK_SECTORS             32

/* SMP (Serial Management Protocol). */
#define LEAPRAID_SMP_PT_FLAG_SGL_PTR            0x80
#define LEAPRAID_SMP_FRAME_HEADER_SIZE          4
#define LEAPRAID_SCSI_HOST_SHIFT                16
#define LEAPRAID_SCSI_DRIVER_SHIFT              24

/* SCSI ASC/ASCQ definitions. */
#define LEAPRAID_SCSI_ASCQ_DEFAULT              0x00
#define LEAPRAID_SCSI_ASC_POWER_ON_RESET        0x29
#define LEAPRAID_SCSI_ASC_INVALID_CMD_CODE      0x20
#define LEAPRAID_SCSI_ASCQ_POWER_ON_RESET       0x07

/* VPD Page 0x89 (ATA Information). */
#define LEAPRAID_VPD_PAGE_ATA_INFO      0x89
#define LEAPRAID_VPD_PG89_MAX_LEN       255
#define LEAPRAID_VPD_PG89_MIN_LEN       214

/* Byte index for NCQ support flag in VPD page 0x89. */
#define LEAPRAID_VPD_PG89_NCQ_BYTE_IDX  213
#define LEAPRAID_VPD_PG89_NCQ_BIT_SHIFT 4
#define LEAPRAID_VPD_PG89_NCQ_BIT_MASK  0x1

/* Readiness polling: max retries, sleep µs between. */
#define LEAPRAID_ADAPTER_READY_MAX_RETRY        15000
#define LEAPRAID_ADAPTER_READY_SLEEP_MIN_US     1000
#define LEAPRAID_ADAPTER_READY_SLEEP_MAX_US     1100

/* Doorbell wait parameters. */
#define LEAPRAID_DB_WAIT_MAX_RETRY      20000
#define LEAPRAID_DB_WAIT_DELAY_US       500

/* Basic data size definitions. */
#define LEAPRAID_DWORDS_BYTE_SIZE       4
#define LEAPRAID_WORD_BYTE_SIZE         2

/* SGL threshold and chain offset. */
#define LEAPRAID_SGL_INLINE_THRESHOLD   2
#define LEAPRAID_CHAIN_OFFSET_DWORDS    7

/* MSI-X group size and mask. */
#define LEAPRAID_MSIX_GROUP_SIZE        8
#define LEAPRAID_MSIX_GROUP_MASK        7

/* Basic constants and limits.   */
#define LEAPRAID_BUSY_LIMIT             1
#define LEAPRAID_INVALID_HOST_DIAG_VAL  0xFFFFFFFF

/* Retry/Sleep configuration. */
#define LEAPRAID_WRSEQ_FLUSH_KEY_VALUE  0x0
#define LEAPRAID_WRSEQ_1ST_KEY_VALUE    0xF
#define LEAPRAID_WRSEQ_2ND_KEY_VALUE    0x4
#define LEAPRAID_WRSEQ_3RD_KEY_VALUE    0xB
#define LEAPRAID_WRSEQ_4TH_KEY_VALUE    0x2
#define LEAPRAID_WRSEQ_5TH_KEY_VALUE    0x7
#define LEAPRAID_WRSEQ_6TH_KEY_VALUE    0xD
#define LEAPRAID_UNLOCK_RETRY_LIMIT     20
#define LEAPRAID_UNLOCK_SLEEP_MS        100
#define LEAPRAID_MSLEEP_NORMAL_MS       100
#define LEAPRAID_MSLEEP_EXTRA_LONG_MS   500
#define LEAPRAID_IO_POLL_DELAY_US       500

/* Device/Volume configuration. */
#define LEAPRAID_MAX_VOLUMES_DEFAULT    32
#define LEAPRAID_MAX_DEV_HANDLE_DEFAULT 2048
#define LEAPRAID_INVALID_DEV_HANDLE     0xFFFF

/* Commands queue depth. */
#define LEAPRAID_DEFAULT_CMD_QD_OFFSET  64
#define LEAPRAID_REPLY_QD_ALIGNMENT     16
/* Task ID offset. */
#define LEAPRAID_TASKID_OFFSET_CTRL_CMD         1
#define LEAPRAID_TASKID_OFFSET_CFG_OP_CMD               1
#define LEAPRAID_TASKID_OFFSET_TRANSPORT_CMD            2
#define LEAPRAID_TASKID_OFFSET_ENC_CMD                  3
#define LEAPRAID_TASKID_OFFSET_NOTIFY_EVENT_CMD         4

/* Task ID offset for high-priority. */
#define LEAPRAID_HP_TASKID_OFFSET_CTL_CMD       0
#define LEAPRAID_HP_TASKID_OFFSET_TM_CMD        1

/* Event/Boot configuration. */
#define LEAPRAID_EVT_MASK_COUNT 4
#define LEAPRAID_BOOT_DEV_SIZE  24

/* Commands timeout. */
#define LEAPRAID_UNIFIED_TIMEOUT               30
#define LEAPRAID_CFG_OP_TIMEOUT                LEAPRAID_UNIFIED_TIMEOUT
#define LEAPRAID_CTL_CMD_TIMEOUT               LEAPRAID_UNIFIED_TIMEOUT
#define LEAPRAID_SCAN_DEV_CMD_TIMEOUT          300
#define LEAPRAID_ENC_CMD_TIMEOUT               LEAPRAID_UNIFIED_TIMEOUT
#define LEAPRAID_IO_CMD_TIMEOUT                LEAPRAID_UNIFIED_TIMEOUT
#define LEAPRAID_NOTIFY_EVENT_CMD_TIMEOUT      LEAPRAID_UNIFIED_TIMEOUT
#define LEAPRAID_TM_CMD_TIMEOUT                LEAPRAID_UNIFIED_TIMEOUT
#define LEAPRAID_TRANSPORT_CMD_TIMEOUT         LEAPRAID_UNIFIED_TIMEOUT

/* Host DMA cap. */
#define DMA_32_BITS    32
#define DMA_64_BITS    64
#define LEAPRAID_DMA_ALIGN    16

/* Values used to represent an invalid. */
#define LEAPRAID_INVALID_INDEX         -1
#define LEAPRAID_INVALID_MSIX_VECTORS  -1
#define LEAPRAID_INVALID_INITIAL_VALUE -1
#define LEAPRAID_OPERATION_FAILED      -1

/* Version shift and mask. */
#define LEAPRAID_VER_MAJOR_SHIFT 24
#define LEAPRAID_VER_MINOR_SHIFT 16
#define LEAPRAID_VER_BUILD_SHIFT 8
#define LEAPRAID_VER_MASK        0xFF

/* Interrupt type. */
#define LEAPRAID_INTERRUPT_MODE_MSIX 0
#define LEAPRAID_INTERRUPT_MODE_MSI  1
#define LEAPRAID_INTERRUPT_MODE_LEGACY 2

/* SMP link reset. */
#define SMP_PHY_CONTROL_LINK_RESET    0x01
#define SMP_PHY_CONTROL_HARD_RESET    0x02
#define SMP_PHY_CONTROL_DISABLE       0x03

/**
 * struct leapraid_adapter_features - Adapter features/capabilities.
 *
 * @req_slot: Number of request slots supported by the adapter.
 * @hp_slot: Number of high-priority slots supported by the adapter.
 * @adapter_caps: Adapter capabilities.
 * @fw_version: Firmware version of the adapter.
 * @max_dev_handle: Maximum device handle supported by the adapter.
 * @min_dev_handle: Minimum device handle supported by the adapter.
 */
struct leapraid_adapter_features {
	u16 req_slot;
	u16 hp_slot;
	u32 adapter_caps;
	u32 fw_version;
	u8 max_msix_vectors;
	u8 max_volumes;
	u16 max_dev_handle;
	u16 min_dev_handle;
	u16 msg_ver;
	u16 product_id;
};

/**
 * struct leapraid_adapter_attr - Adapter attributes and capabilities
 *
 * @id: Adapter identifier.
 * @raid_support: Indicates if RAID is supported.
 * @bios_version: Version of the adapter BIOS.
 * @enable_mp: Indicates if multipath (MP) support is enabled.
 * @wideport_max_queue_depth: Maximum queue depth for wide ports.
 * @narrowport_max_queue_depth: Maximum queue depth for narrow ports.
 * @sata_max_queue_depth: Maximum queue depth for SATA.
 * @raid_volume_max_queue_depth: Maximum queue depth for RAID volumes.
 * @features: Detailed features of the adapter.
 * @adapter_total_qd: Total queue depth available on the adapter.
 * @io_qd: Queue depth allocated for I/O operations.
 * @rep_msg_qd: Queue depth for reply messages.
 * @rep_desc_qd: Queue depth for reply descriptors.
 * @rep_desc_q_seg_cnt: Number of segments in a reply descriptor queue.
 * @rq_cnt: Number of request queues.
 * @task_desc_dma_size: Size of task descriptor DMA memory.
 * @use_32_dma_mask: Indicates if 32-bit DMA mask is used.
 * @name: Adapter name string.
 * @board_name: Board name retrieved from manufacturing page 0.
 */
struct leapraid_adapter_attr {
	u8 id;
	u8 raid_support;
	u32 bios_version;
	u8 enable_mp;
	u32 wideport_max_queue_depth;
	u32 narrowport_max_queue_depth;
	u32 sata_max_queue_depth;
	u32 raid_volume_max_queue_depth;
	struct leapraid_adapter_features features;
	u32 adapter_total_qd;
	u32 io_qd;
	u32 rep_msg_qd;
	u32 rep_desc_qd;
	u32 rep_desc_q_seg_cnt;
	u16 rq_cnt;
	u32 task_desc_dma_size;
	u8 use_32_dma_mask;
	char name[LEAPRAID_NAME_LENGTH];
	char board_name[LEAPRAID_BOARD_NAME_LENGTH];
};

/**
 * struct leapraid_io_req_tracker - Track a SCSI I/O request for the adapter
 *
 * @taskid: Unique task ID for this I/O request.
 * @scmd: Pointer to the associated SCSI command.
 * @chain_list: List of chain frames associated with this request.
 * @msix_io: MSI-X vector assigned to this I/O request.
 * @chain: Pointer to the chain memory for this request.
 * @chain_dma: DMA address of the chain memory.
 */
struct leapraid_io_req_tracker {
	u16 taskid;
	struct scsi_cmnd *scmd;
	struct list_head chain_list;
	u16 msix_io;
	void *chain;
	dma_addr_t chain_dma;
};

/**
 * struct leapraid_task_tracker - Tracks a task in the adapter
 *
 * @taskid: Unique task ID for this tracker.
 * @cb_idx: Callback index associated with this task.
 * @tracker_list: Linked list node to chain this tracker in lists.
 */
struct leapraid_task_tracker {
	u16 taskid;
	u8 cb_idx;
	struct list_head tracker_list;
};

/**
 * struct leapraid_rep_desc_maint - Maintains reply descriptor memory
 *
 * @rep_desc: Pointer to the reply descriptor.
 * @rep_desc_dma: DMA address of the reply descriptor.
 */
struct leapraid_rep_desc_maint {
	union leapraid_rep_desc_union *rep_desc;
	dma_addr_t rep_desc_dma;
};

/**
 * struct leapraid_rep_desc_seg_maint -
 * Maintains reply descriptor segment memory
 *
 * @rep_desc_seg: Pointer to the reply descriptor segment.
 * @rep_desc_seg_dma: DMA address of the reply descriptor segment.
 * @rep_desc_maint: Pointer to the main reply descriptor structure.
 */
struct leapraid_rep_desc_seg_maint {
	void *rep_desc_seg;
	dma_addr_t rep_desc_seg_dma;
	struct leapraid_rep_desc_maint *rep_desc_maint;
};

/**
 * struct leapraid_mem_desc - Memory descriptor for LeapRAID adapter
 *
 * @task_desc: Pointer to task descriptor.
 * @task_desc_dma: DMA address of task descriptor.
 * @sg_chain_pool: DMA pool for SGL chain allocations.
 * @sg_chain_pool_size: Size of the sg_chain_pool.
 * @taskid_to_uniq_tag: Mapping from task ID to unique tag.
 * @sense_data: Buffer for SCSI sense data.
 * @sense_data_dma: DMA address of sense_data buffer.
 * @rep_msg: Buffer for reply message.
 * @rep_msg_dma: DMA address of reply message buffer.
 * @rep_msg_addr: Pointer to reply message address.
 * @rep_msg_addr_dma: DMA address of reply message address.
 * @rep_desc_seg_maint: Pointer to reply descriptor segment.
 * @rep_desc_q_arr: Pointer to reply descriptor queue array.
 * @rep_desc_q_arr_dma: DMA address of reply descriptor queue array.
 */
struct leapraid_mem_desc {
	void *task_desc;
	dma_addr_t task_desc_dma;
	struct dma_pool *sg_chain_pool;
	u16 sg_chain_pool_size;
	u16 *taskid_to_uniq_tag;
	u8 *sense_data;
	dma_addr_t sense_data_dma;
	u8 *rep_msg;
	dma_addr_t rep_msg_dma;
	__le32 *rep_msg_addr;
	dma_addr_t rep_msg_addr_dma;
	struct leapraid_rep_desc_seg_maint *rep_desc_seg_maint;
	struct leapraid_rep_desc_q_arr *rep_desc_q_arr;
	dma_addr_t rep_desc_q_arr_dma;
};

/* internal cmd description */
#define LEAPRAID_FIXED_INTER_CMDS       5
#define LEAPRAID_FIXED_HP_CMDS          2

#define LEAPRAID_CMD_NOT_USED           0x8000
#define LEAPRAID_CMD_DONE               0x0001
#define LEAPRAID_CMD_PENDING            0x0002
#define LEAPRAID_CMD_REPLY_VALID        0x0004
#define LEAPRAID_CMD_RESET              0x0008

/**
 * enum LEAPRAID_CB_INDEX - Callback index for LeapRAID driver
 *
 * @LEAPRAID_SCAN_DEV_CB_IDX: Scan device callback index.
 * @LEAPRAID_CONFIG_CB_IDX: Configuration callback index.
 * @LEAPRAID_TRANSPORT_CB_IDX: Transport callback index.
 * @LEAPRAID_SAS_CTRL_CB_IDX: SAS controller callback index.
 * @LEAPRAID_ENC_CB_IDX: Encryption callback index.
 * @LEAPRAID_NOTIFY_EVENT_CB_IDX: Notify event callback index.
 * @LEAPRAID_CTL_CB_IDX: Control callback index.
 * @LEAPRAID_TM_CB_IDX: Task management callback index.
 */
enum LEAPRAID_CB_INDEX {
	LEAPRAID_SCAN_DEV_CB_IDX	= 0x1,
	LEAPRAID_CONFIG_CB_IDX		= 0x2,
	LEAPRAID_TRANSPORT_CB_IDX	= 0x3,
	LEAPRAID_SAS_CTRL_CB_IDX	= 0x5,
	LEAPRAID_ENC_CB_IDX		= 0x6,
	LEAPRAID_NOTIFY_EVENT_CB_IDX	= 0x7,
	LEAPRAID_CTL_CB_IDX		= 0x8,
	LEAPRAID_TM_CB_IDX		= 0x9,
	LEAPRAID_NUM_CB_IDXS
};

/**
 * struct leapraid_default_reply - Default reply frame buffer
 * @pad: Raw reply data buffer returned by firmware.
 *
 * This structure represents a generic reply frame used by the firmware
 * when no specific reply format is required. The buffer size is defined
 * by LEAPRAID_REPLY_SIZE and stores the raw reply data.
 */
struct leapraid_default_reply {
	u8 pad[LEAPRAID_REPLY_SIZE];
};

/**
 * struct leapraid_sense_buffer - SCSI sense data buffer
 * @pad: Buffer used to store sense data returned by a SCSI command.
 *
 * This buffer holds the sense data provided by a device.The size is
 * defined by SCSI_SENSE_BUFFERSIZE.
 */
struct leapraid_sense_buffer {
	u8 pad[SCSI_SENSE_BUFFERSIZE];
};

/**
 * struct leapraid_driver_cmd - Driver command tracking structure
 *
 * @reply: Default reply structure returned by the adapter.
 * @done: Completion object used to signal command completion.
 * @status: Status code returned by the firmware.
 * @taskid: Unique task identifier for this command.
 * @hp_taskid: Task identifier for high-priority commands.
 * @inter_taskid: Task identifier for internal commands.
 * @cb_idx: Callback index used to identify completion context.
 * @async_scan_dev: True if this command is for asynchronous device scan.
 * @sense: Sense buffer holding error information from device.
 * @mutex: Mutex to protect access to this command structure.
 * @list: List node for linking driver commands into lists.
 */
struct leapraid_driver_cmd {
	struct leapraid_default_reply reply;
	struct completion done;
	u16 status;
	u16 taskid;
	u16 hp_taskid;
	u16 inter_taskid;
	u8 cb_idx;
	u8 async_scan_dev;
	struct leapraid_sense_buffer sense;
	struct mutex mutex; /* Mutex for driver cmd */
	struct list_head list;
};

/**
 * struct leapraid_driver_cmds - Collection of driver command objects
 *
 * @special_cmd_list: List head for tracking special driver commands.
 * @scan_dev_cmd: Command used for asynchronous device scan operations.
 * @cfg_op_cmd: Command for configuration operations.
 * @transport_cmd: Command for transport-level operations.
 * @enc_cmd: Command for enclosure management operations.
 * @notify_event_cmd: Command for asynchronous event notification handling.
 * @ctl_cmd: Command for generic control or maintenance operations.
 * @tm_cmd: Task management command.
 */
struct leapraid_driver_cmds {
	struct list_head special_cmd_list;
	struct leapraid_driver_cmd scan_dev_cmd;
	struct leapraid_driver_cmd cfg_op_cmd;
	struct leapraid_driver_cmd transport_cmd;
	struct leapraid_driver_cmd enc_cmd;
	struct leapraid_driver_cmd notify_event_cmd;
	struct leapraid_driver_cmd ctl_cmd;
	struct leapraid_driver_cmd tm_cmd;
};

/**
 * struct leapraid_dynamic_task_desc - Dynamic task descriptor
 *
 * @task_lock: Spinlock to protect concurrent access.
 * @hp_taskid: Current high-priority task ID.
 * @hp_cmd_qd: Fixed command queue depth for high-priority tasks.
 * @inter_taskid: Current internal task ID.
 * @inter_cmd_qd: Fixed command queue depth for internal tasks.
 */
struct leapraid_dynamic_task_desc {
	spinlock_t task_lock; /* protects dynamic task */
	u16 hp_taskid;
	u16 hp_cmd_qd;
	u16 inter_taskid;
	u16 inter_cmd_qd;
};

/**
 * struct leapraid_fw_evt_work - Firmware event work structure
 *
 * @list: Linked list node for queuing the work.
 * @adapter: Pointer to the associated LeapRAID adapter.
 * @work: Work structure used by the kernel workqueue.
 * @refcnt: Reference counter for managing the lifetime of this work.
 * @evt_data: Pointer to firmware event data.
 * @dev_handle: Device handle associated with the event.
 * @evt_type: Type of firmware event.
 * @ignore: Flag indicating whether the event should be ignored.
 */
struct leapraid_fw_evt_work {
	struct list_head list;
	struct leapraid_adapter *adapter;
	struct work_struct work;
	struct kref refcnt;
	void *evt_data;
	u16 dev_handle;
	u16 evt_type;
	u8 ignore;
};

/**
 * struct leapraid_fw_evt_struct - Firmware event handling structure
 *
 * @fw_evt_name: Name of the firmware event.
 * @fw_evt_thread: Workqueue used for processing firmware events.
 * @fw_evt_lock: Spinlock protecting access to the firmware event list.
 * @fw_evt_list: Linked list of pending firmware events.
 * @cur_evt: Pointer to the currently processing firmware event.
 * @cur_evt_task: Task currently executing @cur_evt work.
 * @fw_evt_cleanup: Flag indicating whether cleanup of events is in progress.
 * @leapraid_evt_masks: Array of event masks for filtering firmware events.
 */
struct leapraid_fw_evt_struct {
	u32 leapraid_evt_masks[4];
	char fw_evt_name[48];
	struct workqueue_struct *fw_evt_thread;
	spinlock_t fw_evt_lock; /* protects firmware event */
	struct list_head fw_evt_list;
	struct leapraid_fw_evt_work *cur_evt;
	struct task_struct *cur_evt_task;
	u8 fw_evt_cleanup;
};

/**
 * struct leapraid_rq - Represents a LeapRAID request queue
 *
 * @adapter: Pointer to the associated LeapRAID adapter.
 * @msix_idx: MSI-X vector index used by this queue.
 * @rep_post_host_idx: Index of the last processed reply descriptor.
 * @rep_desc: Pointer to the reply descriptor associated with this queue.
 * @name: Name of the request queue.
 * @busy: Atomic counter indicating if the queue is busy.
 */
struct leapraid_rq {
	struct leapraid_adapter *adapter;
	u8 msix_idx;
	u32 rep_post_host_idx;
	union leapraid_rep_desc_union *rep_desc;
	char name[LEAPRAID_NAME_LENGTH];
	atomic_t busy;
};

/**
 * struct leapraid_int_rq - Internal request queue for a CPU
 *
 * @affinity_hint: CPU affinity mask for the queue.
 * @rq: Underlying LeapRAID request queue structure.
 */
struct leapraid_int_rq {
	cpumask_var_t affinity_hint;
	struct leapraid_rq rq;
};

/**
 * struct leapraid_blk_mq_poll_rq - Polling request for LeapRAID blk-mq
 *
 * @busy: Atomic flag indicating request is being processed.
 * @pause: Atomic flag to temporarily suspend polling.
 * @rq: The underlying LeapRAID request structure.
 */
struct leapraid_blk_mq_poll_rq {
	atomic_t busy;
	atomic_t pause;
	struct leapraid_rq rq;
};

/**
 * struct leapraid_notification_desc - Notification descriptor for LeapRAID
 *
 * @iopoll_qdex: Index of the I/O polling queue.
 * @iopoll_qcnt: Count of I/O polling queues.
 * @msix_enable: Flag indicating MSI-X is enabled.
 * @irq_vectors_allocated: Flag indicating PCI IRQ vectors are allocated.
 * @irq_mode: Actual interrupt mode used for allocated PCI IRQ vectors.
 * @msix_cpu_map: CPU-ID indexed MSI-X queue map.
 * @msix_cpu_map_sz: Number of CPU-ID slots allocated in @msix_cpu_map.
 * @int_rqs: Array of interrupt request queues.
 * @int_rqs_allocated: Count of allocated interrupt request queues.
 * @blk_mq_poll_rqs: Array of blk-mq polling requests.
 */
struct leapraid_notification_desc {
	u32 iopoll_qdex;
	u32 iopoll_qcnt;
	u8 msix_enable;
	u8 irq_vectors_allocated;
	u8 irq_mode;
	u8 *msix_cpu_map;
	u32 msix_cpu_map_sz;
	struct leapraid_int_rq *int_rqs;
	u32 int_rqs_allocated;
	struct leapraid_blk_mq_poll_rq *blk_mq_poll_rqs;
};

/**
 * struct leapraid_overheat_desc - Overheat descriptor for LeapRAID
 *
 * @fault_overheat_wq: Workqueue for adapter thermal overheat operations.
 * @fault_overheat_work: Work structure for thermal overheat.
 * @thermal_alert: Flag indicating if adapter thermal alert is active.
 * @fault_overheat_wq_name: Name of the fault overheat workqueue.
 */
struct leapraid_overheat_desc {
	struct workqueue_struct *fault_overheat_wq;
	struct work_struct fault_overheat_work;
	atomic_t thermal_alert;
	char fault_overheat_wq_name[48];
};

/**
 * struct leapraid_reset_desc - Reset descriptor for LeapRAID
 *
 * @fault_reset_wq: Workqueue for fault reset operations.
 * @fault_reset_work: Delayed work structure for fault reset.
 * @fault_reset_wq_name: Name of the fault reset workqueue.
 * @host_diag_mutex: Mutex for host diagnostic operations.
 * @adapter_reset_lock: Spinlock for adapter reset operations.
 * @adapter_reset_mutex: Mutex for adapter reset operations.
 * @adapter_link_resetting: Flag indicating if adapter link is resetting.
 * @adapter_reset_results: Results of the adapter reset operation.
 * @pending_io_cnt: Count of pending I/O operations.
 * @reset_wait_queue: Wait queue for reset operations.
 * @reset_cnt: Counter for reset operations.
 */
struct leapraid_reset_desc {
	struct workqueue_struct *fault_reset_wq;
	struct delayed_work fault_reset_work;
	char fault_reset_wq_name[48];
	struct mutex host_diag_mutex; /* Mutex for operations. */
	spinlock_t adapter_reset_lock; /* Protects adapter reset state. */
	struct mutex adapter_reset_mutex; /* Serializes adapter reset. */
	u8 adapter_link_resetting;
	int adapter_reset_results;
	int pending_io_cnt;
	wait_queue_head_t reset_wait_queue;
	u32 reset_cnt;
};

/**
 * struct leapraid_scan_dev_desc - Scan device descriptor for LeapRAID
 *
 * @wait_scan_dev_done: Flag indicating if scan device operation is done.
 * @driver_loading: Flag indicating if driver is loading.
 * @first_scan_dev_fired: Flag indicating if first scan device operation fired.
 * @scan_dev_failed: Flag indicating if scan device operation failed.
 * @scan_start: Flag indicating if scan operation started.
 * @scan_start_failed: Count of failed scan start operations.
 */
struct leapraid_scan_dev_desc {
	u8 wait_scan_dev_done;
	u8 driver_loading;
	wait_queue_head_t wait_driver_loading;
	u8 first_scan_dev_fired;
	u8 scan_dev_failed;
	u8 scan_start;
	u16 scan_start_failed;
};

/**
 * struct leapraid_access_ctrl - Access control structure for LeapRAID
 *
 * @pci_access_lock: Mutex for PCI access control.
 * @shost_recovering: Flag indicating if host is recovering.
 * @shost_recover_async: Flag indicating if host is recovering
 *                       and the 0xFFFF event.
 * @shost_recover_wq: Wait queue for threads waiting on shost_recover_async.
 * @host_removing: Flag indicating if host is being removed.
 * @pcie_recovering: Flag indicating if PCIe is recovering.
 */
struct leapraid_access_ctrl {
	struct mutex pci_access_lock; /* serializes PCI register access. */
	u8 shost_recovering;
	wait_queue_head_t recovery_waitq;
	u8 shost_recover_async;
	wait_queue_head_t shost_recover_wq;
	u8 host_removing;
	u8 pcie_recovering;
};

/**
 * struct leapraid_fw_log_desc - Firmware log descriptor for LeapRAID
 *
 * @fw_log_buffer: Buffer for firmware log data.
 * @fw_log_buffer_dma: DMA address of the firmware log buffer.
 * @fw_log_wq_name: Name of the firmware log workqueue.
 * @fw_log_wq: Workqueue for firmware log operations.
 * @fw_log_work: Delayed work structure for firmware log.
 * @open_pcie_trace: Flag indicating if PCIe tracing is open.
 * @fw_log_init_flag: Flag indicating if firmware log is initialized.
 * @mmap_refcnt: Number of active user VMAs on fw_log_buffer.
 * @mmap_waitq: Waitqueue for fw_log_buffer VMA teardown.
 */
struct leapraid_fw_log_desc {
	u8 *fw_log_buffer;
	dma_addr_t fw_log_buffer_dma;
	char fw_log_wq_name[48];
	struct workqueue_struct *fw_log_wq;
	struct delayed_work fw_log_work;
	int open_pcie_trace;
	int fw_log_init_flag;
	atomic_t mmap_refcnt;
	wait_queue_head_t mmap_waitq;
};

#define LEAPRAID_CARD_PORT_FLG_DIRTY	0x01
#define LEAPRAID_CARD_PORT_FLG_NEW	0x02
#define LEAPRAID_DISABLE_MP_PORT_ID	0xFF
/**
 * struct leapraid_card_port - Card port structure for LeapRAID
 *
 * @list: List head for card port.
 * @vphys_list: List head for virtual PHY list.
 * @port_id: Port ID.
 * @sas_address: SAS address.
 * @phy_mask: Mask of PHY.
 * @vphys_mask: Mask of virtual PHY.
 * @flg: Flags for the port.
 */
struct leapraid_card_port {
	struct list_head list;
	struct list_head vphys_list;
	u8 port_id;
	u64 sas_address;
	u32 phy_mask;
	u32 vphys_mask;
	u8 flg;
};

/**
 * struct leapraid_card_phy - Card PHY structure for LeapRAID
 *
 * @port_siblings: List head for port siblings.
 * @card_port: Pointer to the card port.
 * @identify: SAS identify structure.
 * @remote_identify: Remote SAS identify structure.
 * @phy: SAS PHY structure.
 * @phy_id: PHY ID.
 * @hdl: Handle for the port.
 * @attached_hdl: Handle for the attached port.
 * @phy_is_assigned: Flag indicating if PHY is assigned.
 * @vphy: Flag indicating if virtual PHY.
 */
struct leapraid_card_phy {
	struct list_head port_siblings;
	struct leapraid_card_port *card_port;
	struct sas_identify identify;
	struct sas_identify remote_identify;
	struct sas_phy *phy;
	u8 phy_id;
	u16 hdl;
	u16 attached_hdl;
	u8 phy_is_assigned;
	u8 vphy;
};

/**
 * struct leapraid_topo_node - SAS topology node for LeapRAID
 *
 * @list: List head for linking nodes.
 * @sas_port_list: List of SAS ports.
 * @card_port: Associated card port.
 * @card_phy: Associated card PHY.
 * @rphy: SAS remote PHY device.
 * @parent_dev: Parent device pointer.
 * @sas_address: SAS address of this node.
 * @sas_address_parent: Parent node's SAS address.
 * @phys_num: Number of physical links.
 * @hdl: Handle identifier.
 * @enc_hdl: Enclosure handle.
 * @enc_lid: Enclosure logical identifier.
 * @resp: Response status flag.
 */
struct leapraid_topo_node {
	struct list_head list;
	struct list_head sas_port_list;
	struct leapraid_card_port *card_port;
	struct leapraid_card_phy *card_phy;
	struct sas_rphy *rphy;
	struct device *parent_dev;
	u64 sas_address;
	u64 sas_address_parent;
	u8 phys_num;
	u16 hdl;
	u16 enc_hdl;
	u64 enc_lid;
	u8 resp;
};

/**
 * struct leapraid_dev_topo - LeapRAID device topology management structure
 *
 * @topo_node_lock: Spinlock for protecting topology node operations.
 * @sas_dev_lock: Spinlock for SAS device list access.
 * @raid_volume_lock: Spinlock for RAID volume list access.
 * @enc_lock: Spinlock for enclosure device list access.
 * @sas_id: SAS domain identifier.
 * @card: Main card topology node.
 * @exp_list: List of expander devices.
 * @enc_list: List of enclosure devices.
 * @sas_dev_list: List of SAS devices.
 * @sas_dev_init_list: List of SAS devices being initialized.
 * @raid_volume_list: List of RAID volumes.
 * @card_port_list: List of card ports.
 * @pd_hdls: Array of physical disk handles.
 * @blocking_hdls: Array of blocking handles.
 */
struct leapraid_dev_topo {
	spinlock_t topo_node_lock; /* Protects topology node. */
	spinlock_t sas_dev_lock; /* Protects SAS device list access. */
	spinlock_t raid_volume_lock; /* Protects RAID volume list access. */
	spinlock_t enc_lock; /* Protects enclosure device list access. */
	int sas_id;
	struct leapraid_topo_node card;
	struct list_head exp_list;
	struct list_head enc_list;
	struct list_head sas_dev_list;
	struct list_head sas_dev_init_list;
	struct list_head raid_volume_list;
	struct list_head card_port_list;
	u16 pd_hdls_sz;
	unsigned long *pd_hdls;
	unsigned long *blocking_hdls;
};

/**
 * struct leapraid_boot_dev - Boot device structure for LeapRAID
 *
 * @dev: Device pointer.
 * @chnl: Channel number.
 * @form: Form factor.
 * @pg_dev: Config page device content.
 */
struct leapraid_boot_dev {
	void *dev;
	u8 chnl;
	u8 form;
	u8 pg_dev[24];
};

/**
 * struct leapraid_boot_devs - Boot device management structure
 *
 * @lock: Spinlock protecting boot device cache access.
 * @requested_boot_dev: Requested primary boot device.
 * @requested_alt_boot_dev: Requested alternate boot device.
 * @current_boot_dev: Currently active boot device.
 */
struct leapraid_boot_devs {
	spinlock_t lock; /* protects boot dev */
	struct leapraid_boot_dev requested_boot_dev;
	struct leapraid_boot_dev requested_alt_boot_dev;
	struct leapraid_boot_dev current_boot_dev;
};

/**
 * struct leapraid_adapter - Main LeapRAID adapter structure
 *
 * @list: List head for adapter management.
 * @shost: SCSI host structure.
 * @pdev: PCI device structure.
 * @iomem_base: I/O memory mapped base address.
 * @rep_msg_host_idx: Host index for reply messages.
 * @mask_int: Interrupt masking flag.
 * @adapter_attr: Adapter attributes.
 * @mem_desc: Memory descriptor.
 * @driver_cmds: Driver commands.
 * @dynamic_task_desc: Dynamic task descriptor.
 * @fw_evt_s: Firmware event structure.
 * @notification_desc: Notification descriptor.
 * @reset_desc: Reset descriptor.
 * @scan_dev_desc: Device scan descriptor.
 * @access_ctrl: Access control.
 * @fw_log_desc: Firmware log descriptor.
 * @dev_topo: Device topology.
 * @boot_devs: Boot devices.
 * @overheat_desc: Overheat processing descriptor.
 */
struct leapraid_adapter {
	struct list_head list;
	struct Scsi_Host *shost;
	struct pci_dev *pdev;
	struct leapraid_reg_base __iomem *iomem_base;
	u32 rep_msg_host_idx;
	u8 mask_int;

	struct leapraid_adapter_attr adapter_attr;
	struct leapraid_mem_desc mem_desc;
	struct leapraid_driver_cmds driver_cmds;
	struct leapraid_dynamic_task_desc dynamic_task_desc;
	struct leapraid_fw_evt_struct fw_evt_s;
	struct leapraid_notification_desc notification_desc;
	struct leapraid_reset_desc reset_desc;
	struct leapraid_scan_dev_desc scan_dev_desc;
	struct leapraid_access_ctrl access_ctrl;
	struct leapraid_fw_log_desc fw_log_desc;
	struct leapraid_dev_topo dev_topo;
	struct leapraid_boot_devs boot_devs;
	struct leapraid_overheat_desc overheat_desc;
};

union cfg_param_1 {
	u32 form;
	u32 size;
	u32 phy_number;
};

union cfg_param_2 {
	u32 handle;
	u32 form_specific;
};

enum config_page_action {
	GET_BIOS_PG2,
	GET_BIOS_PG3,
	GET_MANUFACTURING_PG0,
	GET_SAS_DEVICE_PG0,
	GET_SAS_IOUNIT_PG0,
	GET_SAS_IOUNIT_PG1,
	GET_SAS_EXPANDER_PG0,
	GET_SAS_EXPANDER_PG1,
	GET_SAS_ENCLOSURE_PG0,
	GET_PHY_PG0,
	GET_RAID_VOLUME_PG0,
	GET_RAID_VOLUME_PG1,
	GET_PHY_DISK_PG0,
};

/**
 * struct leapraid_enc_node - Enclosure node structure
 *
 * @list: List head for enclosure management.
 * @pg0: Enclosure page 0 data.
 */
struct leapraid_enc_node {
	struct list_head list;
	struct leapraid_enc_p0 pg0;
};

/**
 * struct leapraid_raid_volume - RAID volume structure
 *
 * @list: List head for volume management.
 * @refcnt: Reference count.
 * @starget: SCSI target structure.
 * @sdev: SCSI device structure.
 * @id: Volume ID.
 * @channel: SCSI channel.
 * @wwid: World wide Identifier.
 * @hdl: Volume handle.
 * @vol_type: Volume type.
 * @pd_num: Number of physical disks.
 * @resp: Response status.
 * @dev_info: Device information.
 */
struct leapraid_raid_volume {
	struct list_head list;
	struct kref refcnt;
	struct scsi_target *starget;
	struct scsi_device *sdev;
	unsigned int id;
	unsigned int channel;
	u64 wwid;
	u16 hdl;
	u8 vol_type;
	u8 pd_num;
	u8 resp;
	u32 dev_info;
};

static inline void leapraid_raid_volume_free(struct kref *ref)
{
	kfree(container_of(ref, struct leapraid_raid_volume, refcnt));
}

#define leapraid_raid_volume_get(volume) \
	kref_get(&(volume)->refcnt)
#define leapraid_raid_volume_put(volume) \
	kref_put(&(volume)->refcnt, leapraid_raid_volume_free)

#define LEAPRAID_TGT_FLG_RAID_MEMBER	0x01
#define LEAPRAID_TGT_FLG_VOLUME		0x02
#define LEAPRAID_NO_ULD_ATTACH		1
/**
 * struct leapraid_starget_priv - SCSI target private data
 *
 * @starget: SCSI target structure.
 * @sas_address: SAS address.
 * @hdl: Device handle.
 * @num_luns: Number of LUNs.
 * @flg: Flags.
 * @deleted: Deletion flag.
 * @tm_busy: Task management busy flag.
 * @card_port: Associated card port.
 * @sas_dev: SAS device structure.
 */
struct leapraid_starget_priv {
	struct scsi_target *starget;
	u64 sas_address;
	u16 hdl;
	int num_luns;
	u32 flg;
	u8 deleted;
	u8 tm_busy;
	struct leapraid_card_port *card_port;
	struct leapraid_sas_dev *sas_dev;
};

#define LEAPRAID_DEVICE_FLG_INIT	0x01
/**
 * struct leapraid_sdev_priv - SCSI device private data
 *
 * @starget_priv: Associated target private data.
 * @lun: Logical Unit Number.
 * @flg: Flags.
 * @ncq_cmd_prio_enable: Enables NCQ command priority for RT I/O.
 * @block: Block flag.
 * @deleted: Deletion flag.
 * @sep: SEP flag.
 */
struct leapraid_sdev_priv {
	struct leapraid_starget_priv *starget_priv;
	unsigned int lun;
	u32 flg;
	u8 ncq_cmd_prio_enable;
	u8 block;
	u8 deleted;
	u8 sep;
};

/**
 * struct leapraid_sas_dev - SAS device structure
 *
 * @list: List head for device management.
 * @starget: SCSI target structure.
 * @card_port: Associated card port.
 * @rphy: SAS remote PHY.
 * @refcnt: Reference count.
 * @id: Device ID.
 * @channel: SCSI channel.
 * @slot: Slot number.
 * @phy: PHY identifier.
 * @resp: Response status.
 * @led_on: LED state.
 * @sas_addr: SAS address.
 * @dev_name: Device name.
 * @hdl: Device handle.
 * @parent_sas_addr: Parent SAS address.
 * @enc_hdl: Enclosure handle.
 * @enc_lid: Enclosure logical ID.
 * @volume_hdl: Volume handle.
 * @volume_wwid: Volume WWID.
 * @dev_info: Device information.
 * @pend_sas_rphy_add: Pending SAS remote PHY addition flag.
 * @enc_level: Enclosure level.
 * @port_connection: Port connection.
 * @connector_name: Connector name.
 */
struct leapraid_sas_dev {
	struct list_head list;
	struct scsi_target *starget;
	struct leapraid_card_port *card_port;
	struct sas_rphy *rphy;
	struct kref refcnt;
	unsigned int id;
	unsigned int channel;
	u16 slot;
	u8 phy;
	u8 resp;
	u8 led_on;
	u64 sas_addr;
	u64 dev_name;
	u16 hdl;
	u64 parent_sas_addr;
	u16 enc_hdl;
	u64 enc_lid;
	u16 volume_hdl;
	u64 volume_wwid;
	u32 dev_info;
	u8 pend_sas_rphy_add;
	u8 enc_level;
	u8 port_connection;
	u8 connector_name[LEAPRAID_SAS_DEV_P0_CON_NAME_LEN + 1];
};

static inline void leapraid_sdev_free(struct kref *ref)
{
	kfree(container_of(ref, struct leapraid_sas_dev, refcnt));
}

#define leapraid_sdev_get(sdev)	kref_get(&(sdev)->refcnt)
#define leapraid_sdev_put(sdev)	kref_put(&(sdev)->refcnt, leapraid_sdev_free)

void leapraid_boot_dev_get(void *dev, u32 chnl);
void leapraid_boot_dev_put(void *dev, u32 chnl);

/**
 * struct leapraid_sas_port - SAS port structure
 *
 * @port_list: List head for port management.
 * @phy_list: List of PHYs in this port.
 * @port: SAS port structure.
 * @card_port: Associated card port.
 * @remote_identify: Remote device identification.
 * @rphy: SAS remote PHY.
 * @phys_num: Number of PHYs in this port.
 */
struct leapraid_sas_port {
	struct list_head port_list;
	struct list_head phy_list;
	struct sas_port *port;
	struct leapraid_card_port *card_port;
	struct sas_identify remote_identify;
	struct sas_rphy *rphy;
	u8 phys_num;
};

#define LEAPRAID_VPHY_FLG_DIRTY	0x01
/**
 * struct leapraid_vphy - Virtual PHY structure
 *
 * @list: List head for PHY management.
 * @sas_address: SAS address.
 * @phy_mask: PHY mask.
 * @flg: Flags.
 */
struct leapraid_vphy {
	struct list_head list;
	u64 sas_address;
	u32 phy_mask;
	u8 flg;
};

/**
 * struct leapraid_tgt_rst_list - Target reset tracking entry
 * @list: List node used to link reset entries.
 * @handle: Device handle of the target being reset.
 * @state: Current state of the target reset operation.
 *
 * Each entry represents a target device that is undergoing or has
 * undergone a target reset operation.
 */
struct leapraid_tgt_rst_list {
	struct list_head list;
	u16 handle;
	u16 state;
};

/**
 * struct sense_info - Parsed SCSI sense information
 * @sense_key: Sense key extracted from sense data.
 * @asc: Additional Sense Code (ASC).
 * @ascq: Additional Sense Code Qualifier (ASCQ).
 *
 * This structure stores key fields parsed from SCSI sense data to
 * simplify error handling and reporting.
 */
struct sense_info {
	u8 sense_key;
	u8 asc;
	u8 ascq;
};

/**
 * struct leapraid_fw_log_info - Firmware log position information
 * @user_position: Current log read position used by the driver.
 * @adapter_position: Current log write position maintained by firmware.
 *
 * This structure tracks firmware log buffer positions used for retrieving
 * log entries from the adapter.
 */
struct leapraid_fw_log_info {
	u32 user_position;
	u32 adapter_position;
};

/**
 * enum reset_type - Reset type enumeration
 *
 * @FULL_RESET: Full hardware reset.
 * @PART_RESET: Partial reset.
 */
enum reset_type {
	FULL_RESET,
	PART_RESET,
};

enum leapraid_card_port_checking_flg {
	CARD_PORT_FURTHER_CHECKING_NEEDED = 0,
	CARD_PORT_SKIP_CHECKING,
};

enum leapraid_port_checking_state {
	NEW_CARD_PORT = 0,
	SAME_PORT_WITH_NOTHING_CHANGED,
	SAME_PORT_WITH_PARTIALLY_CHANGED_PHYS,
	SAME_ADDR_WITH_PARTIALLY_CHANGED_PHYS,
	SAME_ADDR_ONLY,
};

/**
 * struct leapraid_card_port_feature - Card port feature
 *
 * @dirty_flg: Dirty flag indicator.
 * @same_addr: Same address flag.
 * @exact_phy: Exact PHY match flag.
 * @phy_overlap: PHY overlap bitmap.
 * @same_port: Same port flag.
 * @cur_chking_old_port: Current checking old port.
 * @expected_old_port: Expected old port.
 * @same_addr_port_count: Same address port count.
 * @checking_state: Port checking state.
 */
struct leapraid_card_port_feature {
	u8 dirty_flg;
	u8 same_addr;
	u8 exact_phy;
	u32 phy_overlap;
	u8 same_port;
	struct leapraid_card_port *cur_chking_old_port;
	struct leapraid_card_port *expected_old_port;
	int same_addr_port_count;
	enum leapraid_port_checking_state checking_state;
};

#define SMP_REPORT_MANUFACTURER_INFORMATION_FRAME_TYPE	0x40
#define SMP_REPORT_MANUFACTURER_INFORMATION_FUNC	0x01

/**
 * ref: SAS-2(INCITS 457-2010) 10.4.3.5
 */
struct leapraid_rep_manu_request {
	u8 smp_frame_type;
	u8 function;
	u8 allocated_response_length;
	u8 request_length;
};

/**
 * ref: SAS-2(INCITS 457-2010) 10.4.3.5
 */
struct leapraid_rep_manu_reply {
	u8 smp_frame_type;
	u8 function;
	u8 function_result;
	u8 response_length;
	u16 expander_change_count;
	u8 r1[2];
	u8 sas_format;
	u8 r2[3];
	u8 vendor_identification[SAS_EXPANDER_VENDOR_ID_LEN];
	u8 product_identification[SAS_EXPANDER_PRODUCT_ID_LEN];
	u8 product_revision_level[SAS_EXPANDER_PRODUCT_REV_LEN];
	u8 comp_vendor_identification[SAS_EXPANDER_COMPONENT_VENDOR_ID_LEN];
	u16 component_id;
	u8 component_revision_level;
	u8 r3;
	u8 vendor_specific[8];
};

extern struct list_head leapraid_adapter_list;
extern spinlock_t leapraid_adapter_lock;
extern char driver_name[LEAPRAID_NAME_LENGTH];

int leapraid_ctrl_init(struct leapraid_adapter *adapter);
void leapraid_remove_ctrl(struct leapraid_adapter *adapter);
void leapraid_check_scheduled_fault_start(struct leapraid_adapter *adapter);
void leapraid_check_scheduled_fault_stop(struct leapraid_adapter *adapter);
void leapraid_fw_log_start(struct leapraid_adapter *adapter);
void leapraid_fw_log_stop(struct leapraid_adapter *adapter);
int leapraid_set_pcie_and_notification(struct leapraid_adapter *adapter);
void leapraid_disable_controller(struct leapraid_adapter *adapter);
int leapraid_hard_reset_handler(struct leapraid_adapter *adapter,
				enum reset_type type);
void leapraid_mask_int(struct leapraid_adapter *adapter);
void leapraid_unmask_int(struct leapraid_adapter *adapter);
u32 leapraid_get_adapter_state(struct leapraid_adapter *adapter);
bool leapraid_pci_removed(struct leapraid_adapter *adapter);
int leapraid_check_adapter_is_op(struct leapraid_adapter *adapter, int wait,
				 const char *caller);
void *leapraid_get_task_desc(struct leapraid_adapter *adapter, u16 taskid);
void *leapraid_get_sense_buffer(struct leapraid_adapter *adapter, u16 taskid);
__le32 leapraid_get_sense_buffer_dma(struct leapraid_adapter *adapter,
				     u16 taskid);
void *leapraid_get_reply_vaddr(struct leapraid_adapter *adapter,
			       u32 phys_addr);
u16 leapraid_alloc_scsiio_taskid(struct leapraid_adapter *adapter,
				 struct scsi_cmnd *scmd);
void leapraid_free_taskid(struct leapraid_adapter *adapter, u16 taskid);
struct leapraid_io_req_tracker *leapraid_get_io_tracker_from_taskid(
		struct leapraid_adapter *adapter, u16 taskid);
struct scsi_cmnd *leapraid_get_scmd_from_taskid(
		struct leapraid_adapter *adapter, u16 taskid);
int leapraid_scan_dev(struct leapraid_adapter *adapter, bool async_scan_dev);
void leapraid_scan_dev_done(struct leapraid_adapter *adapter);
void leapraid_cleanup_lists(struct leapraid_adapter *adapter);
void leapraid_wait_cmds_done(struct leapraid_adapter *adapter);
void leapraid_clean_active_scsi_cmds(struct leapraid_adapter *adapter);
void leapraid_sync_irqs(struct leapraid_adapter *adapter, bool poll);
int leapraid_rep_queue_handler(struct leapraid_rq *rq);
int leapraid_blk_mq_poll(struct Scsi_Host *shost, unsigned int queue_num);
void leapraid_mq_polling_pause(struct leapraid_adapter *adapter);
void leapraid_mq_polling_resume(struct leapraid_adapter *adapter);
void leapraid_set_tm_flg(struct leapraid_adapter *adapter, u16 handle);
void leapraid_clear_tm_flg(struct leapraid_adapter *adapter, u16 handle);
void leapraid_async_turn_on_led(struct leapraid_adapter *adapter, u16 handle);
int leapraid_issue_locked_tm(struct leapraid_adapter *adapter, u16 handle,
			     uint channel, uint id, uint lun, u8 type,
			     u16 taskid_task, u8 tr_method);
int leapraid_issue_tm(struct leapraid_adapter *adapter, u16 handle,
		      uint channel, uint id, uint lun, u8 type,
		      u16 taskid_task, u8 tr_method);
bool leapraid_scsiio_done(struct leapraid_adapter *adapter, u16 taskid,
			  u8 msix_index, u32 rep);
int leapraid_get_volume_cap(struct leapraid_adapter *adapter,
			    struct leapraid_raid_volume *raid_volume);
int leapraid_internal_init_cmd_priv(
		struct leapraid_adapter *adapter,
		struct leapraid_io_req_tracker *io_tracker);
void leapraid_internal_exit_cmd_priv(
		struct leapraid_adapter *adapter,
		struct leapraid_io_req_tracker *io_tracker);
void leapraid_clean_active_fw_evt(struct leapraid_adapter *adapter);
bool leapraid_scmd_find_by_lun(struct leapraid_adapter *adapter,
			       uint id, unsigned int lun, uint channel);
bool leapraid_scmd_find_by_tgt(struct leapraid_adapter *adapter,
			       uint id, uint channel);
struct leapraid_vphy *leapraid_get_vphy_by_phy(struct leapraid_card_port *port,
					       u32 phy);
struct leapraid_raid_volume *leapraid_raid_volume_find_by_id(
		struct leapraid_adapter *adapter, uint id, uint channel);
struct leapraid_raid_volume *leapraid_raid_volume_find_by_hdl(
		struct leapraid_adapter *adapter, u16 handle);
struct leapraid_topo_node *leapraid_exp_find_by_sas_address(
		struct leapraid_adapter *adapter, u64 sas_address,
		struct leapraid_card_port *port);
struct leapraid_sas_dev *leapraid_hold_lock_get_sas_dev_by_addr_and_rphy(
		struct leapraid_adapter *adapter,
		u64 sas_address, struct sas_rphy *rphy);
struct leapraid_sas_dev *leapraid_get_sas_dev_by_addr(
		struct leapraid_adapter *adapter, u64 sas_address,
		struct leapraid_card_port *port);
struct leapraid_sas_dev *leapraid_get_sas_dev_by_hdl(
		struct leapraid_adapter *adapter, u16 handle);
struct leapraid_sas_dev *leapraid_get_sas_dev_from_tgt(
		struct leapraid_adapter *adapter,
		struct leapraid_starget_priv *tgt_priv);
struct leapraid_sas_dev *leapraid_hold_lock_get_sas_dev_from_tgt(
		struct leapraid_adapter *adapter,
		 struct leapraid_starget_priv *tgt_priv);
struct leapraid_sas_dev *leapraid_hold_lock_get_sas_dev_by_hdl(
		struct leapraid_adapter *adapter, u16 handle);
struct leapraid_sas_dev *leapraid_hold_lock_get_sas_dev_by_addr(
		struct leapraid_adapter *adapter, u64 sas_address,
		struct leapraid_card_port *port);
struct leapraid_sas_dev *leapraid_get_next_sas_dev_from_init_list(
		struct leapraid_adapter *adapter);
void leapraid_sas_dev_remove_by_sas_address(
		struct leapraid_adapter *adapter,
		u64 sas_address, struct leapraid_card_port *port);
void leapraid_sas_dev_remove(struct leapraid_adapter *adapter,
			     struct leapraid_sas_dev *sas_dev);
void leapraid_raid_volume_remove(struct leapraid_adapter *adapter,
				 struct leapraid_raid_volume *raid_volume);
void leapraid_exp_rm(struct leapraid_adapter *adapter,
		     u64 sas_address, struct leapraid_card_port *port);
void leapraid_build_mpi_sg(struct leapraid_adapter *adapter,
			   void *sge, dma_addr_t h2c_dma_addr, size_t h2c_size,
			   dma_addr_t c2h_dma_addr, size_t c2h_size);
void leapraid_build_ieee_nodata_sg(struct leapraid_adapter *adapter,
				   void *sge);
void leapraid_build_ieee_sg(struct leapraid_adapter *adapter,
			    void *psge, dma_addr_t h2c_dma_addr,
			    size_t h2c_size, dma_addr_t c2h_dma_addr,
			    size_t c2h_size);
int leapraid_build_scmd_ieee_sg(struct leapraid_adapter *adapter,
				struct scsi_cmnd *scmd, u16 taskid);
void leapraid_fire_scsi_io(struct leapraid_adapter *adapter,
			   u16 taskid, u16 handle);
void leapraid_fire_hpr_task(struct leapraid_adapter *adapter, u16 taskid,
			    u16 msix_task);
void leapraid_fire_task(struct leapraid_adapter *adapter, u16 taskid);
int leapraid_cfg_get_volume_hdl(struct leapraid_adapter *adapter,
				u16 pd_handle, u16 *volume_handle);
int leapraid_cfg_get_volume_wwid(struct leapraid_adapter *adapter,
				 u16 volume_handle, u64 *wwid);
int leapraid_op_config_page(struct leapraid_adapter *adapter,
			    void *cfgp, union cfg_param_1 cfgp1,
			    union cfg_param_2 cfgp2,
			    enum config_page_action cfg_op);
void leapraid_log_req_context(struct leapraid_adapter *adapter, u16 smid,
			      const void *req_data);

int leapraid_change_queue_depth(struct scsi_device *sdev, int qdepth);

int leapraid_ctl_release(struct inode *inode, struct file *filep);
int leapraid_ctl_init(void);
void leapraid_ctl_exit(void);

extern struct sas_function_template leapraid_transport_functions;
extern struct scsi_transport_template *leapraid_transport_template;
struct leapraid_sas_port *leapraid_transport_port_add(
		struct leapraid_adapter *adapter, u16 handle, u64 sas_address,
		struct leapraid_card_port *card_port);
void leapraid_transport_port_remove(struct leapraid_adapter *adapter,
				    u64 sas_address, u64 sas_address_parent,
				    struct leapraid_card_port *card_port);
void leapraid_transport_add_card_phy(struct leapraid_adapter *adapter,
				     struct leapraid_card_phy *card_phy,
				     struct leapraid_sas_phy_p0 *phy_pg0,
				     struct device *parent_dev);
int leapraid_transport_add_exp_phy(struct leapraid_adapter *adapter,
				   struct leapraid_card_phy *card_phy,
				   struct leapraid_exp_p1 *exp_pg1,
				   struct device *parent_dev);
void leapraid_transport_update_links(struct leapraid_adapter *adapter,
				     u64 sas_address, u16 handle,
				     u8 phy_number, u8 link_rate,
				     struct leapraid_card_port *card_port);
void leapraid_transport_detach_phy_to_port(
		struct leapraid_adapter *adapter,
		struct leapraid_topo_node *topo_node,
		struct leapraid_card_phy *card_phy);
void leapraid_transport_attach_phy_to_port(
		struct leapraid_adapter *adapter,
		struct leapraid_topo_node *sas_node,
		struct leapraid_card_phy *card_phy,
		u64 sas_address,
		struct leapraid_card_port *card_port);
enum scsi_qc_status leapraid_queuecommand(struct Scsi_Host *shost,
					  struct scsi_cmnd *scmd);
void leapraid_overheat_cleanup(struct leapraid_adapter *adapter);
void leapraid_smart_fault_detect(struct leapraid_adapter *adapter, u16 hdl);

#endif /* LEAPRAID_FUNC_H_INCLUDED */
