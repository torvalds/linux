/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2020-2021 Intel Corporation. */
#ifndef __CXL_MEM_H__
#define __CXL_MEM_H__
#include <uapi/linux/cxl_mem.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/uuid.h>
#include <linux/node.h>
#include <cxl/event.h>
#include <cxl/mailbox.h>
#include "cxl.h"

/* CXL 2.0 8.2.8.5.1.1 Memory Device Status Register */
#define CXLMDEV_STATUS_OFFSET 0x0
#define   CXLMDEV_DEV_FATAL BIT(0)
#define   CXLMDEV_FW_HALT BIT(1)
#define   CXLMDEV_STATUS_MEDIA_STATUS_MASK GENMASK(3, 2)
#define     CXLMDEV_MS_NOT_READY 0
#define     CXLMDEV_MS_READY 1
#define     CXLMDEV_MS_ERROR 2
#define     CXLMDEV_MS_DISABLED 3
#define CXLMDEV_READY(status)                                                  \
	(FIELD_GET(CXLMDEV_STATUS_MEDIA_STATUS_MASK, status) ==                \
	 CXLMDEV_MS_READY)
#define   CXLMDEV_MBOX_IF_READY BIT(4)
#define   CXLMDEV_RESET_NEEDED_MASK GENMASK(7, 5)
#define     CXLMDEV_RESET_NEEDED_NOT 0
#define     CXLMDEV_RESET_NEEDED_COLD 1
#define     CXLMDEV_RESET_NEEDED_WARM 2
#define     CXLMDEV_RESET_NEEDED_HOT 3
#define     CXLMDEV_RESET_NEEDED_CXL 4
#define CXLMDEV_RESET_NEEDED(status)                                           \
	(FIELD_GET(CXLMDEV_RESET_NEEDED_MASK, status) !=                       \
	 CXLMDEV_RESET_NEEDED_NOT)

/**
 * struct cxl_memdev - CXL bus object representing a Type-3 Memory Device
 * @dev: driver core device object
 * @cdev: char dev core object for ioctl operations
 * @cxlds: The device state backing this device
 * @detach_work: active memdev lost a port in its ancestry
 * @cxl_nvb: coordinate removal of @cxl_nvd if present
 * @cxl_nvd: optional bridge to an nvdimm if the device supports pmem
 * @endpoint: connection to the CXL port topology for this memory device
 * @id: id number of this memdev instance.
 * @depth: endpoint port depth
 * @scrub_cycle: current scrub cycle set for this device
 * @scrub_region_id: id number of a backed region (if any) for which current scrub cycle set
 * @err_rec_array: List of xarrarys to store the memdev error records to
 *		   check attributes for a memory repair operation are from
 *		   current boot.
 */
struct cxl_memdev {
	struct device dev;
	struct cdev cdev;
	struct cxl_dev_state *cxlds;
	struct work_struct detach_work;
	struct cxl_nvdimm_bridge *cxl_nvb;
	struct cxl_nvdimm *cxl_nvd;
	struct cxl_port *endpoint;
	int id;
	int depth;
	u8 scrub_cycle;
	int scrub_region_id;
	void *err_rec_array;
};

static inline struct cxl_memdev *to_cxl_memdev(struct device *dev)
{
	return container_of(dev, struct cxl_memdev, dev);
}

static inline struct cxl_port *cxled_to_port(struct cxl_endpoint_decoder *cxled)
{
	return to_cxl_port(cxled->cxld.dev.parent);
}

static inline struct cxl_port *cxlrd_to_port(struct cxl_root_decoder *cxlrd)
{
	return to_cxl_port(cxlrd->cxlsd.cxld.dev.parent);
}

static inline struct cxl_memdev *
cxled_to_memdev(struct cxl_endpoint_decoder *cxled)
{
	struct cxl_port *port = to_cxl_port(cxled->cxld.dev.parent);

	return to_cxl_memdev(port->uport_dev);
}

bool is_cxl_memdev(const struct device *dev);
static inline bool is_cxl_endpoint(struct cxl_port *port)
{
	return is_cxl_memdev(port->uport_dev);
}

struct cxl_memdev *devm_cxl_add_memdev(struct device *host,
				       struct cxl_dev_state *cxlds);
int devm_cxl_sanitize_setup_notifier(struct device *host,
				     struct cxl_memdev *cxlmd);
struct cxl_memdev_state;
int devm_cxl_setup_fw_upload(struct device *host, struct cxl_memdev_state *mds);
int devm_cxl_dpa_reserve(struct cxl_endpoint_decoder *cxled,
			 resource_size_t base, resource_size_t len,
			 resource_size_t skipped);

#define CXL_NR_PARTITIONS_MAX 2

struct cxl_dpa_info {
	u64 size;
	struct cxl_dpa_part_info {
		struct range range;
		enum cxl_partition_mode mode;
	} part[CXL_NR_PARTITIONS_MAX];
	int nr_partitions;
};

int cxl_dpa_setup(struct cxl_dev_state *cxlds, const struct cxl_dpa_info *info);

static inline struct cxl_ep *cxl_ep_load(struct cxl_port *port,
					 struct cxl_memdev *cxlmd)
{
	if (!port)
		return NULL;

	return xa_load(&port->endpoints, (unsigned long)&cxlmd->dev);
}

/*
 * Per CXL 3.0 Section 8.2.8.4.5.1
 */
#define CMD_CMD_RC_TABLE							\
	C(SUCCESS, 0, NULL),							\
	C(BACKGROUND, -ENXIO, "background cmd started successfully"),           \
	C(INPUT, -ENXIO, "cmd input was invalid"),				\
	C(UNSUPPORTED, -ENXIO, "cmd is not supported"),				\
	C(INTERNAL, -ENXIO, "internal device error"),				\
	C(RETRY, -ENXIO, "temporary error, retry once"),			\
	C(BUSY, -ENXIO, "ongoing background operation"),			\
	C(MEDIADISABLED, -ENXIO, "media access is disabled"),			\
	C(FWINPROGRESS, -ENXIO,	"one FW package can be transferred at a time"), \
	C(FWOOO, -ENXIO, "FW package content was transferred out of order"),    \
	C(FWAUTH, -ENXIO, "FW package authentication failed"),			\
	C(FWSLOT, -ENXIO, "FW slot is not supported for requested operation"),  \
	C(FWROLLBACK, -ENXIO, "rolled back to the previous active FW"),         \
	C(FWRESET, -ENXIO, "FW failed to activate, needs cold reset"),		\
	C(HANDLE, -ENXIO, "one or more Event Record Handles were invalid"),     \
	C(PADDR, -EFAULT, "physical address specified is invalid"),		\
	C(POISONLMT, -EBUSY, "poison injection limit has been reached"),        \
	C(MEDIAFAILURE, -ENXIO, "permanent issue with the media"),		\
	C(ABORT, -ENXIO, "background cmd was aborted by device"),               \
	C(SECURITY, -ENXIO, "not valid in the current security state"),         \
	C(PASSPHRASE, -ENXIO, "phrase doesn't match current set passphrase"),   \
	C(MBUNSUPPORTED, -ENXIO, "unsupported on the mailbox it was issued on"),\
	C(PAYLOADLEN, -ENXIO, "invalid payload length"),			\
	C(LOG, -ENXIO, "invalid or unsupported log page"),			\
	C(INTERRUPTED, -ENXIO, "asynchronous event occured"),			\
	C(FEATUREVERSION, -ENXIO, "unsupported feature version"),		\
	C(FEATURESELVALUE, -ENXIO, "unsupported feature selection value"),	\
	C(FEATURETRANSFERIP, -ENXIO, "feature transfer in progress"),		\
	C(FEATURETRANSFEROOO, -ENXIO, "feature transfer out of order"),		\
	C(RESOURCEEXHAUSTED, -ENXIO, "resources are exhausted"),		\
	C(EXTLIST, -ENXIO, "invalid Extent List"),				\

#undef C
#define C(a, b, c) CXL_MBOX_CMD_RC_##a
enum  { CMD_CMD_RC_TABLE };
#undef C
#define C(a, b, c) { b, c }
struct cxl_mbox_cmd_rc {
	int err;
	const char *desc;
};

static const
struct cxl_mbox_cmd_rc cxl_mbox_cmd_rctable[] ={ CMD_CMD_RC_TABLE };
#undef C

static inline const char *cxl_mbox_cmd_rc2str(struct cxl_mbox_cmd *mbox_cmd)
{
	return cxl_mbox_cmd_rctable[mbox_cmd->return_code].desc;
}

static inline int cxl_mbox_cmd_rc2errno(struct cxl_mbox_cmd *mbox_cmd)
{
	return cxl_mbox_cmd_rctable[mbox_cmd->return_code].err;
}

/*
 * CXL 2.0 - Memory capacity multiplier
 * See Section 8.2.9.5
 *
 * Volatile, Persistent, and Partition capacities are specified to be in
 * multiples of 256MB - define a multiplier to convert to/from bytes.
 */
#define CXL_CAPACITY_MULTIPLIER SZ_256M

/*
 * Event Interrupt Policy
 *
 * CXL rev 3.0 section 8.2.9.2.4; Table 8-52
 */
enum cxl_event_int_mode {
	CXL_INT_NONE		= 0x00,
	CXL_INT_MSI_MSIX	= 0x01,
	CXL_INT_FW		= 0x02
};
struct cxl_event_interrupt_policy {
	u8 info_settings;
	u8 warn_settings;
	u8 failure_settings;
	u8 fatal_settings;
} __packed;

/**
 * struct cxl_event_state - Event log driver state
 *
 * @buf: Buffer to receive event data
 * @log_lock: Serialize event_buf and log use
 */
struct cxl_event_state {
	struct cxl_get_event_payload *buf;
	struct mutex log_lock;
};

/* Device enabled poison commands */
enum poison_cmd_enabled_bits {
	CXL_POISON_ENABLED_LIST,
	CXL_POISON_ENABLED_INJECT,
	CXL_POISON_ENABLED_CLEAR,
	CXL_POISON_ENABLED_SCAN_CAPS,
	CXL_POISON_ENABLED_SCAN_MEDIA,
	CXL_POISON_ENABLED_SCAN_RESULTS,
	CXL_POISON_ENABLED_MAX
};

/* Device enabled security commands */
enum security_cmd_enabled_bits {
	CXL_SEC_ENABLED_SANITIZE,
	CXL_SEC_ENABLED_SECURE_ERASE,
	CXL_SEC_ENABLED_GET_SECURITY_STATE,
	CXL_SEC_ENABLED_SET_PASSPHRASE,
	CXL_SEC_ENABLED_DISABLE_PASSPHRASE,
	CXL_SEC_ENABLED_UNLOCK,
	CXL_SEC_ENABLED_FREEZE_SECURITY,
	CXL_SEC_ENABLED_PASSPHRASE_SECURE_ERASE,
	CXL_SEC_ENABLED_MAX
};

/**
 * struct cxl_poison_state - Driver poison state info
 *
 * @max_errors: Maximum media error records held in device cache
 * @enabled_cmds: All poison commands enabled in the CEL
 * @list_out: The poison list payload returned by device
 * @mutex: Protect reads of the poison list
 *
 * Reads of the poison list are synchronized to ensure that a reader
 * does not get an incomplete list because their request overlapped
 * (was interrupted or preceded by) another read request of the same
 * DPA range. CXL Spec 3.0 Section 8.2.9.8.4.1
 */
struct cxl_poison_state {
	u32 max_errors;
	DECLARE_BITMAP(enabled_cmds, CXL_POISON_ENABLED_MAX);
	struct cxl_mbox_poison_out *list_out;
	struct mutex mutex;  /* Protect reads of poison list */
};

/*
 * Get FW Info
 * CXL rev 3.0 section 8.2.9.3.1; Table 8-56
 */
struct cxl_mbox_get_fw_info {
	u8 num_slots;
	u8 slot_info;
	u8 activation_cap;
	u8 reserved[13];
	char slot_1_revision[16];
	char slot_2_revision[16];
	char slot_3_revision[16];
	char slot_4_revision[16];
} __packed;

#define CXL_FW_INFO_SLOT_INFO_CUR_MASK			GENMASK(2, 0)
#define CXL_FW_INFO_SLOT_INFO_NEXT_MASK			GENMASK(5, 3)
#define CXL_FW_INFO_SLOT_INFO_NEXT_SHIFT		3
#define CXL_FW_INFO_ACTIVATION_CAP_HAS_LIVE_ACTIVATE	BIT(0)

/*
 * Transfer FW Input Payload
 * CXL rev 3.0 section 8.2.9.3.2; Table 8-57
 */
struct cxl_mbox_transfer_fw {
	u8 action;
	u8 slot;
	u8 reserved[2];
	__le32 offset;
	u8 reserved2[0x78];
	u8 data[];
} __packed;

#define CXL_FW_TRANSFER_ACTION_FULL	0x0
#define CXL_FW_TRANSFER_ACTION_INITIATE	0x1
#define CXL_FW_TRANSFER_ACTION_CONTINUE	0x2
#define CXL_FW_TRANSFER_ACTION_END	0x3
#define CXL_FW_TRANSFER_ACTION_ABORT	0x4

/*
 * CXL rev 3.0 section 8.2.9.3.2 mandates 128-byte alignment for FW packages
 * and for each part transferred in a Transfer FW command.
 */
#define CXL_FW_TRANSFER_ALIGNMENT	128

/*
 * Activate FW Input Payload
 * CXL rev 3.0 section 8.2.9.3.3; Table 8-58
 */
struct cxl_mbox_activate_fw {
	u8 action;
	u8 slot;
} __packed;

#define CXL_FW_ACTIVATE_ONLINE		0x0
#define CXL_FW_ACTIVATE_OFFLINE		0x1

/* FW state bits */
#define CXL_FW_STATE_BITS		32
#define CXL_FW_CANCEL			0

/**
 * struct cxl_fw_state - Firmware upload / activation state
 *
 * @state: fw_uploader state bitmask
 * @oneshot: whether the fw upload fits in a single transfer
 * @num_slots: Number of FW slots available
 * @cur_slot: Slot number currently active
 * @next_slot: Slot number for the new firmware
 */
struct cxl_fw_state {
	DECLARE_BITMAP(state, CXL_FW_STATE_BITS);
	bool oneshot;
	int num_slots;
	int cur_slot;
	int next_slot;
};

/**
 * struct cxl_security_state - Device security state
 *
 * @state: state of last security operation
 * @enabled_cmds: All security commands enabled in the CEL
 * @poll_tmo_secs: polling timeout
 * @sanitize_active: sanitize completion pending
 * @poll_dwork: polling work item
 * @sanitize_node: sanitation sysfs file to notify
 */
struct cxl_security_state {
	unsigned long state;
	DECLARE_BITMAP(enabled_cmds, CXL_SEC_ENABLED_MAX);
	int poll_tmo_secs;
	bool sanitize_active;
	struct delayed_work poll_dwork;
	struct kernfs_node *sanitize_node;
};

/*
 * enum cxl_devtype - delineate type-2 from a generic type-3 device
 * @CXL_DEVTYPE_DEVMEM - Vendor specific CXL Type-2 device implementing HDM-D or
 *			 HDM-DB, no requirement that this device implements a
 *			 mailbox, or other memory-device-standard manageability
 *			 flows.
 * @CXL_DEVTYPE_CLASSMEM - Common class definition of a CXL Type-3 device with
 *			   HDM-H and class-mandatory memory device registers
 */
enum cxl_devtype {
	CXL_DEVTYPE_DEVMEM,
	CXL_DEVTYPE_CLASSMEM,
};

/**
 * struct cxl_dpa_perf - DPA performance property entry
 * @dpa_range: range for DPA address
 * @coord: QoS performance data (i.e. latency, bandwidth)
 * @cdat_coord: raw QoS performance data from CDAT
 * @qos_class: QoS Class cookies
 */
struct cxl_dpa_perf {
	struct range dpa_range;
	struct access_coordinate coord[ACCESS_COORDINATE_MAX];
	struct access_coordinate cdat_coord[ACCESS_COORDINATE_MAX];
	int qos_class;
};

/**
 * struct cxl_dpa_partition - DPA partition descriptor
 * @res: shortcut to the partition in the DPA resource tree (cxlds->dpa_res)
 * @perf: performance attributes of the partition from CDAT
 * @mode: operation mode for the DPA capacity, e.g. ram, pmem, dynamic...
 */
struct cxl_dpa_partition {
	struct resource res;
	struct cxl_dpa_perf perf;
	enum cxl_partition_mode mode;
};

/**
 * struct cxl_dev_state - The driver device state
 *
 * cxl_dev_state represents the CXL driver/device state.  It provides an
 * interface to mailbox commands as well as some cached data about the device.
 * Currently only memory devices are represented.
 *
 * @dev: The device associated with this CXL state
 * @cxlmd: The device representing the CXL.mem capabilities of @dev
 * @reg_map: component and ras register mapping parameters
 * @regs: Parsed register blocks
 * @cxl_dvsec: Offset to the PCIe device DVSEC
 * @rcd: operating in RCD mode (CXL 3.0 9.11.8 CXL Devices Attached to an RCH)
 * @media_ready: Indicate whether the device media is usable
 * @dpa_res: Overall DPA resource tree for the device
 * @part: DPA partition array
 * @nr_partitions: Number of DPA partitions
 * @serial: PCIe Device Serial Number
 * @type: Generic Memory Class device or Vendor Specific Memory device
 * @cxl_mbox: CXL mailbox context
 * @cxlfs: CXL features context
 */
struct cxl_dev_state {
	struct device *dev;
	struct cxl_memdev *cxlmd;
	struct cxl_register_map reg_map;
	struct cxl_regs regs;
	int cxl_dvsec;
	bool rcd;
	bool media_ready;
	struct resource dpa_res;
	struct cxl_dpa_partition part[CXL_NR_PARTITIONS_MAX];
	unsigned int nr_partitions;
	u64 serial;
	enum cxl_devtype type;
	struct cxl_mailbox cxl_mbox;
#ifdef CONFIG_CXL_FEATURES
	struct cxl_features_state *cxlfs;
#endif
};

static inline resource_size_t cxl_pmem_size(struct cxl_dev_state *cxlds)
{
	/*
	 * Static PMEM may be at partition index 0 when there is no static RAM
	 * capacity.
	 */
	for (int i = 0; i < cxlds->nr_partitions; i++)
		if (cxlds->part[i].mode == CXL_PARTMODE_PMEM)
			return resource_size(&cxlds->part[i].res);
	return 0;
}

static inline struct cxl_dev_state *mbox_to_cxlds(struct cxl_mailbox *cxl_mbox)
{
	return dev_get_drvdata(cxl_mbox->host);
}

/**
 * struct cxl_memdev_state - Generic Type-3 Memory Device Class driver data
 *
 * CXL 8.1.12.1 PCI Header - Class Code Register Memory Device defines
 * common memory device functionality like the presence of a mailbox and
 * the functionality related to that like Identify Memory Device and Get
 * Partition Info
 * @cxlds: Core driver state common across Type-2 and Type-3 devices
 * @lsa_size: Size of Label Storage Area
 *                (CXL 2.0 8.2.9.5.1.1 Identify Memory Device)
 * @firmware_version: Firmware version for the memory device.
 * @total_bytes: sum of all possible capacities
 * @volatile_only_bytes: hard volatile capacity
 * @persistent_only_bytes: hard persistent capacity
 * @partition_align_bytes: alignment size for partition-able capacity
 * @active_volatile_bytes: sum of hard + soft volatile
 * @active_persistent_bytes: sum of hard + soft persistent
 * @event: event log driver state
 * @poison: poison driver state info
 * @security: security driver state info
 * @fw: firmware upload / activation state
 * @mce_notifier: MCE notifier
 *
 * See CXL 3.0 8.2.9.8.2 Capacity Configuration and Label Storage for
 * details on capacity parameters.
 */
struct cxl_memdev_state {
	struct cxl_dev_state cxlds;
	size_t lsa_size;
	char firmware_version[0x10];
	u64 total_bytes;
	u64 volatile_only_bytes;
	u64 persistent_only_bytes;
	u64 partition_align_bytes;
	u64 active_volatile_bytes;
	u64 active_persistent_bytes;

	struct cxl_event_state event;
	struct cxl_poison_state poison;
	struct cxl_security_state security;
	struct cxl_fw_state fw;
	struct notifier_block mce_notifier;
};

static inline struct cxl_memdev_state *
to_cxl_memdev_state(struct cxl_dev_state *cxlds)
{
	if (cxlds->type != CXL_DEVTYPE_CLASSMEM)
		return NULL;
	return container_of(cxlds, struct cxl_memdev_state, cxlds);
}

enum cxl_opcode {
	CXL_MBOX_OP_INVALID		= 0x0000,
	CXL_MBOX_OP_RAW			= CXL_MBOX_OP_INVALID,
	CXL_MBOX_OP_GET_EVENT_RECORD	= 0x0100,
	CXL_MBOX_OP_CLEAR_EVENT_RECORD	= 0x0101,
	CXL_MBOX_OP_GET_EVT_INT_POLICY	= 0x0102,
	CXL_MBOX_OP_SET_EVT_INT_POLICY	= 0x0103,
	CXL_MBOX_OP_GET_FW_INFO		= 0x0200,
	CXL_MBOX_OP_TRANSFER_FW		= 0x0201,
	CXL_MBOX_OP_ACTIVATE_FW		= 0x0202,
	CXL_MBOX_OP_GET_TIMESTAMP	= 0x0300,
	CXL_MBOX_OP_SET_TIMESTAMP	= 0x0301,
	CXL_MBOX_OP_GET_SUPPORTED_LOGS	= 0x0400,
	CXL_MBOX_OP_GET_LOG		= 0x0401,
	CXL_MBOX_OP_GET_LOG_CAPS	= 0x0402,
	CXL_MBOX_OP_CLEAR_LOG           = 0x0403,
	CXL_MBOX_OP_GET_SUP_LOG_SUBLIST = 0x0405,
	CXL_MBOX_OP_GET_SUPPORTED_FEATURES	= 0x0500,
	CXL_MBOX_OP_GET_FEATURE		= 0x0501,
	CXL_MBOX_OP_SET_FEATURE		= 0x0502,
	CXL_MBOX_OP_DO_MAINTENANCE	= 0x0600,
	CXL_MBOX_OP_IDENTIFY		= 0x4000,
	CXL_MBOX_OP_GET_PARTITION_INFO	= 0x4100,
	CXL_MBOX_OP_SET_PARTITION_INFO	= 0x4101,
	CXL_MBOX_OP_GET_LSA		= 0x4102,
	CXL_MBOX_OP_SET_LSA		= 0x4103,
	CXL_MBOX_OP_GET_HEALTH_INFO	= 0x4200,
	CXL_MBOX_OP_GET_ALERT_CONFIG	= 0x4201,
	CXL_MBOX_OP_SET_ALERT_CONFIG	= 0x4202,
	CXL_MBOX_OP_GET_SHUTDOWN_STATE	= 0x4203,
	CXL_MBOX_OP_SET_SHUTDOWN_STATE	= 0x4204,
	CXL_MBOX_OP_GET_POISON		= 0x4300,
	CXL_MBOX_OP_INJECT_POISON	= 0x4301,
	CXL_MBOX_OP_CLEAR_POISON	= 0x4302,
	CXL_MBOX_OP_GET_SCAN_MEDIA_CAPS	= 0x4303,
	CXL_MBOX_OP_SCAN_MEDIA		= 0x4304,
	CXL_MBOX_OP_GET_SCAN_MEDIA	= 0x4305,
	CXL_MBOX_OP_SANITIZE		= 0x4400,
	CXL_MBOX_OP_SECURE_ERASE	= 0x4401,
	CXL_MBOX_OP_GET_SECURITY_STATE	= 0x4500,
	CXL_MBOX_OP_SET_PASSPHRASE	= 0x4501,
	CXL_MBOX_OP_DISABLE_PASSPHRASE	= 0x4502,
	CXL_MBOX_OP_UNLOCK		= 0x4503,
	CXL_MBOX_OP_FREEZE_SECURITY	= 0x4504,
	CXL_MBOX_OP_PASSPHRASE_SECURE_ERASE	= 0x4505,
	CXL_MBOX_OP_MAX			= 0x10000
};

#define DEFINE_CXL_CEL_UUID                                                    \
	UUID_INIT(0xda9c0b5, 0xbf41, 0x4b78, 0x8f, 0x79, 0x96, 0xb1, 0x62,     \
		  0x3b, 0x3f, 0x17)

#define DEFINE_CXL_VENDOR_DEBUG_UUID                                           \
	UUID_INIT(0x5e1819d9, 0x11a9, 0x400c, 0x81, 0x1f, 0xd6, 0x07, 0x19,     \
		  0x40, 0x3d, 0x86)

struct cxl_mbox_get_supported_logs {
	__le16 entries;
	u8 rsvd[6];
	struct cxl_gsl_entry {
		uuid_t uuid;
		__le32 size;
	} __packed entry[];
}  __packed;

struct cxl_cel_entry {
	__le16 opcode;
	__le16 effect;
} __packed;

struct cxl_mbox_get_log {
	uuid_t uuid;
	__le32 offset;
	__le32 length;
} __packed;

/* See CXL 2.0 Table 175 Identify Memory Device Output Payload */
struct cxl_mbox_identify {
	char fw_revision[0x10];
	__le64 total_capacity;
	__le64 volatile_capacity;
	__le64 persistent_capacity;
	__le64 partition_align;
	__le16 info_event_log_size;
	__le16 warning_event_log_size;
	__le16 failure_event_log_size;
	__le16 fatal_event_log_size;
	__le32 lsa_size;
	u8 poison_list_max_mer[3];
	__le16 inject_poison_limit;
	u8 poison_caps;
	u8 qos_telemetry_caps;
} __packed;

/*
 * General Media Event Record UUID
 * CXL rev 3.0 Section 8.2.9.2.1.1; Table 8-43
 */
#define CXL_EVENT_GEN_MEDIA_UUID                                            \
	UUID_INIT(0xfbcd0a77, 0xc260, 0x417f, 0x85, 0xa9, 0x08, 0x8b, 0x16, \
		  0x21, 0xeb, 0xa6)

/*
 * DRAM Event Record UUID
 * CXL rev 3.0 section 8.2.9.2.1.2; Table 8-44
 */
#define CXL_EVENT_DRAM_UUID                                                 \
	UUID_INIT(0x601dcbb3, 0x9c06, 0x4eab, 0xb8, 0xaf, 0x4e, 0x9b, 0xfb, \
		  0x5c, 0x96, 0x24)

/*
 * Memory Module Event Record UUID
 * CXL rev 3.0 section 8.2.9.2.1.3; Table 8-45
 */
#define CXL_EVENT_MEM_MODULE_UUID                                           \
	UUID_INIT(0xfe927475, 0xdd59, 0x4339, 0xa5, 0x86, 0x79, 0xba, 0xb1, \
		  0x13, 0xb7, 0x74)

/*
 * Memory Sparing Event Record UUID
 * CXL rev 3.2 section 8.2.10.2.1.4: Table 8-60
 */
#define CXL_EVENT_MEM_SPARING_UUID                                          \
	UUID_INIT(0xe71f3a40, 0x2d29, 0x4092, 0x8a, 0x39, 0x4d, 0x1c, 0x96, \
		  0x6c, 0x7c, 0x65)

/*
 * Get Event Records output payload
 * CXL rev 3.0 section 8.2.9.2.2; Table 8-50
 */
#define CXL_GET_EVENT_FLAG_OVERFLOW		BIT(0)
#define CXL_GET_EVENT_FLAG_MORE_RECORDS		BIT(1)
struct cxl_get_event_payload {
	u8 flags;
	u8 reserved1;
	__le16 overflow_err_count;
	__le64 first_overflow_timestamp;
	__le64 last_overflow_timestamp;
	__le16 record_count;
	u8 reserved2[10];
	struct cxl_event_record_raw records[];
} __packed;

/*
 * CXL rev 3.0 section 8.2.9.2.2; Table 8-49
 */
enum cxl_event_log_type {
	CXL_EVENT_TYPE_INFO = 0x00,
	CXL_EVENT_TYPE_WARN,
	CXL_EVENT_TYPE_FAIL,
	CXL_EVENT_TYPE_FATAL,
	CXL_EVENT_TYPE_MAX
};

/*
 * Clear Event Records input payload
 * CXL rev 3.0 section 8.2.9.2.3; Table 8-51
 */
struct cxl_mbox_clear_event_payload {
	u8 event_log;		/* enum cxl_event_log_type */
	u8 clear_flags;
	u8 nr_recs;
	u8 reserved[3];
	__le16 handles[];
} __packed;
#define CXL_CLEAR_EVENT_MAX_HANDLES U8_MAX

struct cxl_mbox_get_partition_info {
	__le64 active_volatile_cap;
	__le64 active_persistent_cap;
	__le64 next_volatile_cap;
	__le64 next_persistent_cap;
} __packed;

struct cxl_mbox_get_lsa {
	__le32 offset;
	__le32 length;
} __packed;

struct cxl_mbox_set_lsa {
	__le32 offset;
	__le32 reserved;
	u8 data[];
} __packed;

struct cxl_mbox_set_partition_info {
	__le64 volatile_capacity;
	u8 flags;
} __packed;

#define  CXL_SET_PARTITION_IMMEDIATE_FLAG	BIT(0)

/* Get Health Info Output Payload CXL 3.2 Spec 8.2.10.9.3.1 Table 8-148 */
struct cxl_mbox_get_health_info_out {
	u8 health_status;
	u8 media_status;
	u8 additional_status;
	u8 life_used;
	__le16 device_temperature;
	__le32 dirty_shutdown_cnt;
	__le32 corrected_volatile_error_cnt;
	__le32 corrected_persistent_error_cnt;
} __packed;

/* Set Shutdown State Input Payload CXL 3.2 Spec 8.2.10.9.3.5 Table 8-152 */
struct cxl_mbox_set_shutdown_state_in {
	u8 state;
} __packed;

/* Set Timestamp CXL 3.0 Spec 8.2.9.4.2 */
struct cxl_mbox_set_timestamp_in {
	__le64 timestamp;

} __packed;

/* Get Poison List  CXL 3.0 Spec 8.2.9.8.4.1 */
struct cxl_mbox_poison_in {
	__le64 offset;
	__le64 length;
} __packed;

struct cxl_mbox_poison_out {
	u8 flags;
	u8 rsvd1;
	__le64 overflow_ts;
	__le16 count;
	u8 rsvd2[20];
	struct cxl_poison_record {
		__le64 address;
		__le32 length;
		__le32 rsvd;
	} __packed record[];
} __packed;

/*
 * Get Poison List address field encodes the starting
 * address of poison, and the source of the poison.
 */
#define CXL_POISON_START_MASK		GENMASK_ULL(63, 6)
#define CXL_POISON_SOURCE_MASK		GENMASK(2, 0)

/* Get Poison List record length is in units of 64 bytes */
#define CXL_POISON_LEN_MULT	64

/* Kernel defined maximum for a list of poison errors */
#define CXL_POISON_LIST_MAX	1024

/* Get Poison List: Payload out flags */
#define CXL_POISON_FLAG_MORE            BIT(0)
#define CXL_POISON_FLAG_OVERFLOW        BIT(1)
#define CXL_POISON_FLAG_SCANNING        BIT(2)

/* Get Poison List: Poison Source */
#define CXL_POISON_SOURCE_UNKNOWN	0
#define CXL_POISON_SOURCE_EXTERNAL	1
#define CXL_POISON_SOURCE_INTERNAL	2
#define CXL_POISON_SOURCE_INJECTED	3
#define CXL_POISON_SOURCE_VENDOR	7

/* Inject & Clear Poison  CXL 3.0 Spec 8.2.9.8.4.2/3 */
struct cxl_mbox_inject_poison {
	__le64 address;
};

/* Clear Poison  CXL 3.0 Spec 8.2.9.8.4.3 */
struct cxl_mbox_clear_poison {
	__le64 address;
	u8 write_data[CXL_POISON_LEN_MULT];
} __packed;

/**
 * struct cxl_mem_command - Driver representation of a memory device command
 * @info: Command information as it exists for the UAPI
 * @opcode: The actual bits used for the mailbox protocol
 * @flags: Set of flags effecting driver behavior.
 *
 *  * %CXL_CMD_FLAG_FORCE_ENABLE: In cases of error, commands with this flag
 *    will be enabled by the driver regardless of what hardware may have
 *    advertised.
 *
 * The cxl_mem_command is the driver's internal representation of commands that
 * are supported by the driver. Some of these commands may not be supported by
 * the hardware. The driver will use @info to validate the fields passed in by
 * the user then submit the @opcode to the hardware.
 *
 * See struct cxl_command_info.
 */
struct cxl_mem_command {
	struct cxl_command_info info;
	enum cxl_opcode opcode;
	u32 flags;
#define CXL_CMD_FLAG_FORCE_ENABLE BIT(0)
};

#define CXL_PMEM_SEC_STATE_USER_PASS_SET	0x01
#define CXL_PMEM_SEC_STATE_MASTER_PASS_SET	0x02
#define CXL_PMEM_SEC_STATE_LOCKED		0x04
#define CXL_PMEM_SEC_STATE_FROZEN		0x08
#define CXL_PMEM_SEC_STATE_USER_PLIMIT		0x10
#define CXL_PMEM_SEC_STATE_MASTER_PLIMIT	0x20

/* set passphrase input payload */
struct cxl_set_pass {
	u8 type;
	u8 reserved[31];
	/* CXL field using NVDIMM define, same length */
	u8 old_pass[NVDIMM_PASSPHRASE_LEN];
	u8 new_pass[NVDIMM_PASSPHRASE_LEN];
} __packed;

/* disable passphrase input payload */
struct cxl_disable_pass {
	u8 type;
	u8 reserved[31];
	u8 pass[NVDIMM_PASSPHRASE_LEN];
} __packed;

/* passphrase secure erase payload */
struct cxl_pass_erase {
	u8 type;
	u8 reserved[31];
	u8 pass[NVDIMM_PASSPHRASE_LEN];
} __packed;

enum {
	CXL_PMEM_SEC_PASS_MASTER = 0,
	CXL_PMEM_SEC_PASS_USER,
};

int cxl_internal_send_cmd(struct cxl_mailbox *cxl_mbox,
			  struct cxl_mbox_cmd *cmd);
int cxl_dev_state_identify(struct cxl_memdev_state *mds);
int cxl_await_media_ready(struct cxl_dev_state *cxlds);
int cxl_enumerate_cmds(struct cxl_memdev_state *mds);
int cxl_mem_dpa_fetch(struct cxl_memdev_state *mds, struct cxl_dpa_info *info);
struct cxl_memdev_state *cxl_memdev_state_create(struct device *dev);
void set_exclusive_cxl_commands(struct cxl_memdev_state *mds,
				unsigned long *cmds);
void clear_exclusive_cxl_commands(struct cxl_memdev_state *mds,
				  unsigned long *cmds);
void cxl_mem_get_event_records(struct cxl_memdev_state *mds, u32 status);
void cxl_event_trace_record(const struct cxl_memdev *cxlmd,
			    enum cxl_event_log_type type,
			    enum cxl_event_type event_type,
			    const uuid_t *uuid, union cxl_event *evt);
int cxl_get_dirty_count(struct cxl_memdev_state *mds, u32 *count);
int cxl_arm_dirty_shutdown(struct cxl_memdev_state *mds);
int cxl_set_timestamp(struct cxl_memdev_state *mds);
int cxl_poison_state_init(struct cxl_memdev_state *mds);
int cxl_mem_get_poison(struct cxl_memdev *cxlmd, u64 offset, u64 len,
		       struct cxl_region *cxlr);
int cxl_trigger_poison_list(struct cxl_memdev *cxlmd);
int cxl_inject_poison(struct cxl_memdev *cxlmd, u64 dpa);
int cxl_clear_poison(struct cxl_memdev *cxlmd, u64 dpa);
int cxl_inject_poison_locked(struct cxl_memdev *cxlmd, u64 dpa);
int cxl_clear_poison_locked(struct cxl_memdev *cxlmd, u64 dpa);

#ifdef CONFIG_CXL_EDAC_MEM_FEATURES
int devm_cxl_memdev_edac_register(struct cxl_memdev *cxlmd);
int devm_cxl_region_edac_register(struct cxl_region *cxlr);
int cxl_store_rec_gen_media(struct cxl_memdev *cxlmd, union cxl_event *evt);
int cxl_store_rec_dram(struct cxl_memdev *cxlmd, union cxl_event *evt);
void devm_cxl_memdev_edac_release(struct cxl_memdev *cxlmd);
#else
static inline int devm_cxl_memdev_edac_register(struct cxl_memdev *cxlmd)
{ return 0; }
static inline int devm_cxl_region_edac_register(struct cxl_region *cxlr)
{ return 0; }
static inline int cxl_store_rec_gen_media(struct cxl_memdev *cxlmd,
					  union cxl_event *evt)
{ return 0; }
static inline int cxl_store_rec_dram(struct cxl_memdev *cxlmd,
				     union cxl_event *evt)
{ return 0; }
static inline void devm_cxl_memdev_edac_release(struct cxl_memdev *cxlmd)
{ return; }
#endif

#ifdef CONFIG_CXL_SUSPEND
void cxl_mem_active_inc(void);
void cxl_mem_active_dec(void);
#else
static inline void cxl_mem_active_inc(void)
{
}
static inline void cxl_mem_active_dec(void)
{
}
#endif

int cxl_mem_sanitize(struct cxl_memdev *cxlmd, u16 cmd);

/**
 * struct cxl_hdm - HDM Decoder registers and cached / decoded capabilities
 * @regs: mapped registers, see devm_cxl_setup_hdm()
 * @decoder_count: number of decoders for this port
 * @target_count: for switch decoders, max downstream port targets
 * @interleave_mask: interleave granularity capability, see check_interleave_cap()
 * @iw_cap_mask: bitmask of supported interleave ways, see check_interleave_cap()
 * @port: mapped cxl_port, see devm_cxl_setup_hdm()
 */
struct cxl_hdm {
	struct cxl_component_regs regs;
	unsigned int decoder_count;
	unsigned int target_count;
	unsigned int interleave_mask;
	unsigned long iw_cap_mask;
	struct cxl_port *port;
};

struct seq_file;
struct dentry *cxl_debugfs_create_dir(const char *dir);
void cxl_dpa_debug(struct seq_file *file, struct cxl_dev_state *cxlds);
#endif /* __CXL_MEM_H__ */
