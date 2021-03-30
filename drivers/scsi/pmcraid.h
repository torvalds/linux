/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * pmcraid.h -- PMC Sierra MaxRAID controller driver header file
 *
 * Written By: Anil Ravindranath<anil_ravindranath@pmc-sierra.com>
 *             PMC-Sierra Inc
 *
 * Copyright (C) 2008, 2009 PMC Sierra Inc.
 */

#ifndef _PMCRAID_H
#define _PMCRAID_H

#include <linux/types.h>
#include <linux/completion.h>
#include <linux/list.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <linux/cdev.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <linux/connector.h>
/*
 * Driver name   : string representing the driver name
 * Device file   : /dev file to be used for management interfaces
 * Driver version: version string in major_version.minor_version.patch format
 * Driver date   : date information in "Mon dd yyyy" format
 */
#define PMCRAID_DRIVER_NAME		"PMC MaxRAID"
#define PMCRAID_DEVFILE			"pmcsas"
#define PMCRAID_DRIVER_VERSION		"1.0.3"

#define PMCRAID_FW_VERSION_1		0x002

/* Maximum number of adapters supported by current version of the driver */
#define PMCRAID_MAX_ADAPTERS		1024

/* Bit definitions as per firmware, bit position [0][1][2].....[31] */
#define PMC_BIT8(n)          (1 << (7-n))
#define PMC_BIT16(n)         (1 << (15-n))
#define PMC_BIT32(n)         (1 << (31-n))

/* PMC PCI vendor ID and device ID values */
#define PCI_VENDOR_ID_PMC			0x11F8
#define PCI_DEVICE_ID_PMC_MAXRAID		0x5220

/*
 * MAX_CMD          : maximum commands that can be outstanding with IOA
 * MAX_IO_CMD       : command blocks available for IO commands
 * MAX_HCAM_CMD     : command blocks avaibale for HCAMS
 * MAX_INTERNAL_CMD : command blocks avaible for internal commands like reset
 */
#define PMCRAID_MAX_CMD				1024
#define PMCRAID_MAX_IO_CMD			1020
#define PMCRAID_MAX_HCAM_CMD			2
#define PMCRAID_MAX_INTERNAL_CMD		2

/* MAX_IOADLS       : max number of scatter-gather lists supported by IOA
 * IOADLS_INTERNAL  : number of ioadls included as part of IOARCB.
 * IOADLS_EXTERNAL  : number of ioadls allocated external to IOARCB
 */
#define PMCRAID_IOADLS_INTERNAL			 27
#define PMCRAID_IOADLS_EXTERNAL			 37
#define PMCRAID_MAX_IOADLS			 PMCRAID_IOADLS_INTERNAL

/* HRRQ_ENTRY_SIZE  : size of hrrq buffer
 * IOARCB_ALIGNMENT : alignment required for IOARCB
 * IOADL_ALIGNMENT  : alignment requirement for IOADLs
 * MSIX_VECTORS     : number of MSIX vectors supported
 */
#define HRRQ_ENTRY_SIZE                          sizeof(__le32)
#define PMCRAID_IOARCB_ALIGNMENT                 32
#define PMCRAID_IOADL_ALIGNMENT                  16
#define PMCRAID_IOASA_ALIGNMENT                  4
#define PMCRAID_NUM_MSIX_VECTORS                 16

/* various other limits */
#define PMCRAID_VENDOR_ID_LEN			8
#define PMCRAID_PRODUCT_ID_LEN			16
#define PMCRAID_SERIAL_NUM_LEN			8
#define PMCRAID_LUN_LEN				8
#define PMCRAID_MAX_CDB_LEN			16
#define PMCRAID_DEVICE_ID_LEN			8
#define PMCRAID_SENSE_DATA_LEN			256
#define PMCRAID_ADD_CMD_PARAM_LEN		48

#define PMCRAID_MAX_BUS_TO_SCAN                  1
#define PMCRAID_MAX_NUM_TARGETS_PER_BUS          256
#define PMCRAID_MAX_NUM_LUNS_PER_TARGET          8

/* IOA bus/target/lun number of IOA resources */
#define PMCRAID_IOA_BUS_ID                       0xfe
#define PMCRAID_IOA_TARGET_ID                    0xff
#define PMCRAID_IOA_LUN_ID                       0xff
#define PMCRAID_VSET_BUS_ID                      0x1
#define PMCRAID_VSET_LUN_ID                      0x0
#define PMCRAID_PHYS_BUS_ID                      0x0
#define PMCRAID_VIRTUAL_ENCL_BUS_ID              0x8
#define PMCRAID_MAX_VSET_TARGETS                 0x7F
#define PMCRAID_MAX_VSET_LUNS_PER_TARGET         8

#define PMCRAID_IOA_MAX_SECTORS                  32767
#define PMCRAID_VSET_MAX_SECTORS                 512
#define PMCRAID_MAX_CMD_PER_LUN                  254

/* Number of configuration table entries (resources), includes 1 FP,
 * 1 Enclosure device
 */
#define PMCRAID_MAX_RESOURCES                    256

/* Adapter Commands used by driver */
#define PMCRAID_QUERY_RESOURCE_STATE             0xC2
#define PMCRAID_RESET_DEVICE                     0xC3
/* options to select reset target */
#define ENABLE_RESET_MODIFIER                    0x80
#define RESET_DEVICE_LUN                         0x40
#define RESET_DEVICE_TARGET                      0x20
#define RESET_DEVICE_BUS                         0x10

#define PMCRAID_IDENTIFY_HRRQ                    0xC4
#define PMCRAID_QUERY_IOA_CONFIG                 0xC5
#define PMCRAID_QUERY_CMD_STATUS		 0xCB
#define PMCRAID_ABORT_CMD                        0xC7

/* CANCEL ALL command, provides option for setting SYNC_COMPLETE
 * on the target resources for which commands got cancelled
 */
#define PMCRAID_CANCEL_ALL_REQUESTS		 0xCE
#define PMCRAID_SYNC_COMPLETE_AFTER_CANCEL       PMC_BIT8(0)

/* HCAM command and types of HCAM supported by IOA */
#define PMCRAID_HOST_CONTROLLED_ASYNC            0xCF
#define PMCRAID_HCAM_CODE_CONFIG_CHANGE          0x01
#define PMCRAID_HCAM_CODE_LOG_DATA               0x02

/* IOA shutdown command and various shutdown types */
#define PMCRAID_IOA_SHUTDOWN                     0xF7
#define PMCRAID_SHUTDOWN_NORMAL                  0x00
#define PMCRAID_SHUTDOWN_PREPARE_FOR_NORMAL      0x40
#define PMCRAID_SHUTDOWN_NONE                    0x100
#define PMCRAID_SHUTDOWN_ABBREV                  0x80

/* SET SUPPORTED DEVICES command and the option to select all the
 * devices to be supported
 */
#define PMCRAID_SET_SUPPORTED_DEVICES            0xFB
#define ALL_DEVICES_SUPPORTED                    PMC_BIT8(0)

/* This option is used with SCSI WRITE_BUFFER command */
#define PMCRAID_WR_BUF_DOWNLOAD_AND_SAVE         0x05

/* IOASC Codes used by driver */
#define PMCRAID_IOASC_SENSE_MASK                 0xFFFFFF00
#define PMCRAID_IOASC_SENSE_KEY(ioasc)           ((ioasc) >> 24)
#define PMCRAID_IOASC_SENSE_CODE(ioasc)          (((ioasc) & 0x00ff0000) >> 16)
#define PMCRAID_IOASC_SENSE_QUAL(ioasc)          (((ioasc) & 0x0000ff00) >> 8)
#define PMCRAID_IOASC_SENSE_STATUS(ioasc)        ((ioasc) & 0x000000ff)

#define PMCRAID_IOASC_GOOD_COMPLETION			0x00000000
#define PMCRAID_IOASC_GC_IOARCB_NOTFOUND		0x005A0000
#define PMCRAID_IOASC_NR_INIT_CMD_REQUIRED		0x02040200
#define PMCRAID_IOASC_NR_IOA_RESET_REQUIRED		0x02048000
#define PMCRAID_IOASC_NR_SYNC_REQUIRED			0x023F0000
#define PMCRAID_IOASC_ME_READ_ERROR_NO_REALLOC		0x03110C00
#define PMCRAID_IOASC_HW_CANNOT_COMMUNICATE		0x04050000
#define PMCRAID_IOASC_HW_DEVICE_TIMEOUT			0x04080100
#define PMCRAID_IOASC_HW_DEVICE_BUS_STATUS_ERROR	0x04448500
#define PMCRAID_IOASC_HW_IOA_RESET_REQUIRED		0x04448600
#define PMCRAID_IOASC_IR_INVALID_RESOURCE_HANDLE        0x05250000
#define PMCRAID_IOASC_AC_TERMINATED_BY_HOST		0x0B5A0000
#define PMCRAID_IOASC_UA_BUS_WAS_RESET			0x06290000
#define PMCRAID_IOASC_TIME_STAMP_OUT_OF_SYNC		0x06908B00
#define PMCRAID_IOASC_UA_BUS_WAS_RESET_BY_OTHER		0x06298000

/* Driver defined IOASCs */
#define PMCRAID_IOASC_IOA_WAS_RESET			0x10000001
#define PMCRAID_IOASC_PCI_ACCESS_ERROR			0x10000002

/* Various timeout values (in milliseconds) used. If any of these are chip
 * specific, move them to pmcraid_chip_details structure.
 */
#define PMCRAID_PCI_DEASSERT_TIMEOUT		2000
#define PMCRAID_BIST_TIMEOUT			2000
#define PMCRAID_AENWAIT_TIMEOUT			5000
#define PMCRAID_TRANSOP_TIMEOUT			60000

#define PMCRAID_RESET_TIMEOUT			(2 * HZ)
#define PMCRAID_CHECK_FOR_RESET_TIMEOUT		((HZ / 10))
#define PMCRAID_VSET_IO_TIMEOUT			(60 * HZ)
#define PMCRAID_INTERNAL_TIMEOUT		(60 * HZ)
#define PMCRAID_SHUTDOWN_TIMEOUT		(150 * HZ)
#define PMCRAID_RESET_BUS_TIMEOUT		(60 * HZ)
#define PMCRAID_RESET_HOST_TIMEOUT		(150 * HZ)
#define PMCRAID_REQUEST_SENSE_TIMEOUT		(30 * HZ)
#define PMCRAID_SET_SUP_DEV_TIMEOUT		(2 * 60 * HZ)

/* structure to represent a scatter-gather element (IOADL descriptor) */
struct pmcraid_ioadl_desc {
	__le64 address;
	__le32 data_len;
	__u8  reserved[3];
	__u8  flags;
} __attribute__((packed, aligned(PMCRAID_IOADL_ALIGNMENT)));

/* pmcraid_ioadl_desc.flags values */
#define IOADL_FLAGS_CHAINED      PMC_BIT8(0)
#define IOADL_FLAGS_LAST_DESC    PMC_BIT8(1)
#define IOADL_FLAGS_READ_LAST    PMC_BIT8(1)
#define IOADL_FLAGS_WRITE_LAST   PMC_BIT8(1)


/* additional IOARCB data which can be CDB or additional request parameters
 * or list of IOADLs. Firmware supports max of 512 bytes for IOARCB, hence then
 * number of IOADLs are limted to 27. In case they are more than 27, they will
 * be used in chained form
 */
struct pmcraid_ioarcb_add_data {
	union {
		struct pmcraid_ioadl_desc ioadl[PMCRAID_IOADLS_INTERNAL];
		__u8 add_cmd_params[PMCRAID_ADD_CMD_PARAM_LEN];
	} u;
};

/*
 * IOA Request Control Block
 */
struct pmcraid_ioarcb {
	__le64 ioarcb_bus_addr;
	__le32 resource_handle;
	__le32 response_handle;
	__le64 ioadl_bus_addr;
	__le32 ioadl_length;
	__le32 data_transfer_length;
	__le64 ioasa_bus_addr;
	__le16 ioasa_len;
	__le16 cmd_timeout;
	__le16 add_cmd_param_offset;
	__le16 add_cmd_param_length;
	__le32 reserved1[2];
	__le32 reserved2;
	__u8  request_type;
	__u8  request_flags0;
	__u8  request_flags1;
	__u8  hrrq_id;
	__u8  cdb[PMCRAID_MAX_CDB_LEN];
	struct pmcraid_ioarcb_add_data add_data;
};

/* well known resource handle values */
#define PMCRAID_IOA_RES_HANDLE        0xffffffff
#define PMCRAID_INVALID_RES_HANDLE    0

/* pmcraid_ioarcb.request_type values */
#define REQ_TYPE_SCSI                 0x00
#define REQ_TYPE_IOACMD               0x01
#define REQ_TYPE_HCAM                 0x02

/* pmcraid_ioarcb.flags0 values */
#define TRANSFER_DIR_WRITE            PMC_BIT8(0)
#define INHIBIT_UL_CHECK              PMC_BIT8(2)
#define SYNC_OVERRIDE                 PMC_BIT8(3)
#define SYNC_COMPLETE                 PMC_BIT8(4)
#define NO_LINK_DESCS                 PMC_BIT8(5)

/* pmcraid_ioarcb.flags1 values */
#define DELAY_AFTER_RESET             PMC_BIT8(0)
#define TASK_TAG_SIMPLE               0x10
#define TASK_TAG_ORDERED              0x20
#define TASK_TAG_QUEUE_HEAD           0x30

/* toggle bit offset in response handle */
#define HRRQ_TOGGLE_BIT               0x01
#define HRRQ_RESPONSE_BIT             0x02

/* IOA Status Area */
struct pmcraid_ioasa_vset {
	__le32 failing_lba_hi;
	__le32 failing_lba_lo;
	__le32 reserved;
} __attribute__((packed, aligned(4)));

struct pmcraid_ioasa {
	__le32 ioasc;
	__le16 returned_status_length;
	__le16 available_status_length;
	__le32 residual_data_length;
	__le32 ilid;
	__le32 fd_ioasc;
	__le32 fd_res_address;
	__le32 fd_res_handle;
	__le32 reserved;

	/* resource specific sense information */
	union {
		struct pmcraid_ioasa_vset vset;
	} u;

	/* IOA autosense data */
	__le16 auto_sense_length;
	__le16 error_data_length;
	__u8  sense_data[PMCRAID_SENSE_DATA_LEN];
} __attribute__((packed, aligned(4)));

#define PMCRAID_DRIVER_ILID           0xffffffff

/* Config Table Entry per Resource */
struct pmcraid_config_table_entry {
	__u8  resource_type;
	__u8  bus_protocol;
	__le16 array_id;
	__u8  common_flags0;
	__u8  common_flags1;
	__u8  unique_flags0;
	__u8  unique_flags1;	/*also used as vset target_id */
	__le32 resource_handle;
	__le32 resource_address;
	__u8  device_id[PMCRAID_DEVICE_ID_LEN];
	__u8  lun[PMCRAID_LUN_LEN];
} __attribute__((packed, aligned(4)));

/* extended configuration table sizes are also of 32 bytes in size */
struct pmcraid_config_table_entry_ext {
	struct pmcraid_config_table_entry cfgte;
};

/* resource types (config_table_entry.resource_type values) */
#define RES_TYPE_AF_DASD     0x00
#define RES_TYPE_GSCSI       0x01
#define RES_TYPE_VSET        0x02
#define RES_TYPE_IOA_FP      0xFF

#define RES_IS_IOA(res)      ((res).resource_type == RES_TYPE_IOA_FP)
#define RES_IS_GSCSI(res)    ((res).resource_type == RES_TYPE_GSCSI)
#define RES_IS_VSET(res)     ((res).resource_type == RES_TYPE_VSET)
#define RES_IS_AFDASD(res)   ((res).resource_type == RES_TYPE_AF_DASD)

/* bus_protocol values used by driver */
#define RES_TYPE_VENCLOSURE  0x8

/* config_table_entry.common_flags0 */
#define MULTIPATH_RESOURCE   PMC_BIT32(0)

/* unique_flags1 */
#define IMPORT_MODE_MANUAL   PMC_BIT8(0)

/* well known resource handle values */
#define RES_HANDLE_IOA       0xFFFFFFFF
#define RES_HANDLE_NONE      0x00000000

/* well known resource address values */
#define RES_ADDRESS_IOAFP    0xFEFFFFFF
#define RES_ADDRESS_INVALID  0xFFFFFFFF

/* BUS/TARGET/LUN values from resource_addrr */
#define RES_BUS(res_addr)    (le32_to_cpu(res_addr) & 0xFF)
#define RES_TARGET(res_addr) ((le32_to_cpu(res_addr) >> 16) & 0xFF)
#define RES_LUN(res_addr)    0x0

/* configuration table structure */
struct pmcraid_config_table {
	__le16 num_entries;
	__u8  table_format;
	__u8  reserved1;
	__u8  flags;
	__u8  reserved2[11];
	union {
		struct pmcraid_config_table_entry
				entries[PMCRAID_MAX_RESOURCES];
		struct pmcraid_config_table_entry_ext
				entries_ext[PMCRAID_MAX_RESOURCES];
	};
} __attribute__((packed, aligned(4)));

/* config_table.flags value */
#define MICROCODE_UPDATE_REQUIRED		PMC_BIT32(0)

/*
 * HCAM format
 */
#define PMCRAID_HOSTRCB_LDNSIZE			4056

/* Error log notification format */
struct pmcraid_hostrcb_error {
	__le32 fd_ioasc;
	__le32 fd_ra;
	__le32 fd_rh;
	__le32 prc;
	union {
		__u8 data[PMCRAID_HOSTRCB_LDNSIZE];
	} u;
} __attribute__ ((packed, aligned(4)));

struct pmcraid_hcam_hdr {
	__u8  op_code;
	__u8  notification_type;
	__u8  notification_lost;
	__u8  flags;
	__u8  overlay_id;
	__u8  reserved1[3];
	__le32 ilid;
	__le32 timestamp1;
	__le32 timestamp2;
	__le32 data_len;
} __attribute__((packed, aligned(4)));

#define PMCRAID_AEN_GROUP	0x3

struct pmcraid_hcam_ccn {
	struct pmcraid_hcam_hdr header;
	struct pmcraid_config_table_entry cfg_entry;
	struct pmcraid_config_table_entry cfg_entry_old;
} __attribute__((packed, aligned(4)));

#define PMCRAID_CCN_EXT_SIZE	3944
struct pmcraid_hcam_ccn_ext {
	struct pmcraid_hcam_hdr header;
	struct pmcraid_config_table_entry_ext cfg_entry;
	struct pmcraid_config_table_entry_ext cfg_entry_old;
	__u8   reserved[PMCRAID_CCN_EXT_SIZE];
} __attribute__((packed, aligned(4)));

struct pmcraid_hcam_ldn {
	struct pmcraid_hcam_hdr header;
	struct pmcraid_hostrcb_error error_log;
} __attribute__((packed, aligned(4)));

/* pmcraid_hcam.op_code values */
#define HOSTRCB_TYPE_CCN			0xE1
#define HOSTRCB_TYPE_LDN			0xE2

/* pmcraid_hcam.notification_type values */
#define NOTIFICATION_TYPE_ENTRY_CHANGED		0x0
#define NOTIFICATION_TYPE_ENTRY_NEW		0x1
#define NOTIFICATION_TYPE_ENTRY_DELETED		0x2
#define NOTIFICATION_TYPE_STATE_CHANGE		0x3
#define NOTIFICATION_TYPE_ENTRY_STATECHANGED	0x4
#define NOTIFICATION_TYPE_ERROR_LOG		0x10
#define NOTIFICATION_TYPE_INFORMATION_LOG	0x11

#define HOSTRCB_NOTIFICATIONS_LOST		PMC_BIT8(0)

/* pmcraid_hcam.flags values */
#define HOSTRCB_INTERNAL_OP_ERROR		PMC_BIT8(0)
#define HOSTRCB_ERROR_RESPONSE_SENT		PMC_BIT8(1)

/* pmcraid_hcam.overlay_id values */
#define HOSTRCB_OVERLAY_ID_08			0x08
#define HOSTRCB_OVERLAY_ID_09			0x09
#define HOSTRCB_OVERLAY_ID_11			0x11
#define HOSTRCB_OVERLAY_ID_12			0x12
#define HOSTRCB_OVERLAY_ID_13			0x13
#define HOSTRCB_OVERLAY_ID_14			0x14
#define HOSTRCB_OVERLAY_ID_16			0x16
#define HOSTRCB_OVERLAY_ID_17			0x17
#define HOSTRCB_OVERLAY_ID_20			0x20
#define HOSTRCB_OVERLAY_ID_FF			0xFF

/* Implementation specific card details */
struct pmcraid_chip_details {
	/* hardware register offsets */
	unsigned long  ioastatus;
	unsigned long  ioarrin;
	unsigned long  mailbox;
	unsigned long  global_intr_mask;
	unsigned long  ioa_host_intr;
	unsigned long  ioa_host_msix_intr;
	unsigned long  ioa_host_intr_clr;
	unsigned long  ioa_host_mask;
	unsigned long  ioa_host_mask_clr;
	unsigned long  host_ioa_intr;
	unsigned long  host_ioa_intr_clr;

	/* timeout used during transitional to operational state */
	unsigned long transop_timeout;
};

/* IOA to HOST doorbells (interrupts) */
#define INTRS_TRANSITION_TO_OPERATIONAL		PMC_BIT32(0)
#define INTRS_IOARCB_TRANSFER_FAILED		PMC_BIT32(3)
#define INTRS_IOA_UNIT_CHECK			PMC_BIT32(4)
#define INTRS_NO_HRRQ_FOR_CMD_RESPONSE		PMC_BIT32(5)
#define INTRS_CRITICAL_OP_IN_PROGRESS		PMC_BIT32(6)
#define INTRS_IO_DEBUG_ACK			PMC_BIT32(7)
#define INTRS_IOARRIN_LOST			PMC_BIT32(27)
#define INTRS_SYSTEM_BUS_MMIO_ERROR		PMC_BIT32(28)
#define INTRS_IOA_PROCESSOR_ERROR		PMC_BIT32(29)
#define INTRS_HRRQ_VALID			PMC_BIT32(30)
#define INTRS_OPERATIONAL_STATUS		PMC_BIT32(0)
#define INTRS_ALLOW_MSIX_VECTOR0		PMC_BIT32(31)

/* Host to IOA Doorbells */
#define DOORBELL_RUNTIME_RESET			PMC_BIT32(1)
#define DOORBELL_IOA_RESET_ALERT		PMC_BIT32(7)
#define DOORBELL_IOA_DEBUG_ALERT		PMC_BIT32(9)
#define DOORBELL_ENABLE_DESTRUCTIVE_DIAGS	PMC_BIT32(8)
#define DOORBELL_IOA_START_BIST			PMC_BIT32(23)
#define DOORBELL_INTR_MODE_MSIX			PMC_BIT32(25)
#define DOORBELL_INTR_MSIX_CLR			PMC_BIT32(26)
#define DOORBELL_RESET_IOA			PMC_BIT32(31)

/* Global interrupt mask register value */
#define GLOBAL_INTERRUPT_MASK			0x5ULL

#define PMCRAID_ERROR_INTERRUPTS	(INTRS_IOARCB_TRANSFER_FAILED | \
					 INTRS_IOA_UNIT_CHECK | \
					 INTRS_NO_HRRQ_FOR_CMD_RESPONSE | \
					 INTRS_IOARRIN_LOST | \
					 INTRS_SYSTEM_BUS_MMIO_ERROR | \
					 INTRS_IOA_PROCESSOR_ERROR)

#define PMCRAID_PCI_INTERRUPTS		(PMCRAID_ERROR_INTERRUPTS | \
					 INTRS_HRRQ_VALID | \
					 INTRS_TRANSITION_TO_OPERATIONAL |\
					 INTRS_ALLOW_MSIX_VECTOR0)

/* control_block, associated with each of the commands contains IOARCB, IOADLs
 * memory for IOASA. Additional 3 * 16 bytes are allocated in order to support
 * additional request parameters (of max size 48) any command.
 */
struct pmcraid_control_block {
	struct pmcraid_ioarcb ioarcb;
	struct pmcraid_ioadl_desc ioadl[PMCRAID_IOADLS_EXTERNAL + 3];
	struct pmcraid_ioasa ioasa;
} __attribute__ ((packed, aligned(PMCRAID_IOARCB_ALIGNMENT)));

/* pmcraid_sglist - Scatter-gather list allocated for passthrough ioctls
 */
struct pmcraid_sglist {
	u32 order;
	u32 num_sg;
	u32 num_dma_sg;
	struct scatterlist *scatterlist;
};

/* page D0 inquiry data of focal point resource */
struct pmcraid_inquiry_data {
	__u8	ph_dev_type;
	__u8	page_code;
	__u8	reserved1;
	__u8	add_page_len;
	__u8	length;
	__u8	reserved2;
	__be16	fw_version;
	__u8	reserved3[16];
};

#define PMCRAID_TIMESTAMP_LEN		12
#define PMCRAID_REQ_TM_STR_LEN		6
#define PMCRAID_SCSI_SET_TIMESTAMP	0xA4
#define PMCRAID_SCSI_SERVICE_ACTION	0x0F

struct pmcraid_timestamp_data {
	__u8 reserved1[4];
	__u8 timestamp[PMCRAID_REQ_TM_STR_LEN];		/* current time value */
	__u8 reserved2[2];
};

/* pmcraid_cmd - LLD representation of SCSI command */
struct pmcraid_cmd {

	/* Ptr and bus address of DMA.able control block for this command */
	struct pmcraid_control_block *ioa_cb;
	dma_addr_t ioa_cb_bus_addr;
	dma_addr_t dma_handle;

	/* pointer to mid layer structure of SCSI commands */
	struct scsi_cmnd *scsi_cmd;

	struct list_head free_list;
	struct completion wait_for_completion;
	struct timer_list timer;	/* needed for internal commands */
	u32 timeout;			/* current timeout value */
	u32 index;			/* index into the command list */
	u8 completion_req;		/* for handling internal commands */
	u8 release;			/* for handling completions */

	void (*cmd_done) (struct pmcraid_cmd *);
	struct pmcraid_instance *drv_inst;

	struct pmcraid_sglist *sglist; /* used for passthrough IOCTLs */

	/* scratch used */
	union {
		/* during reset sequence */
		unsigned long time_left;
		struct pmcraid_resource_entry *res;
		int hrrq_index;

		/* used during IO command error handling. Sense buffer
		 * for REQUEST SENSE command if firmware is not sending
		 * auto sense data
		 */
		struct  {
			u8 *sense_buffer;
			dma_addr_t sense_buffer_dma;
		};
	};
};

/*
 * Interrupt registers of IOA
 */
struct pmcraid_interrupts {
	void __iomem *ioa_host_interrupt_reg;
	void __iomem *ioa_host_msix_interrupt_reg;
	void __iomem *ioa_host_interrupt_clr_reg;
	void __iomem *ioa_host_interrupt_mask_reg;
	void __iomem *ioa_host_interrupt_mask_clr_reg;
	void __iomem *global_interrupt_mask_reg;
	void __iomem *host_ioa_interrupt_reg;
	void __iomem *host_ioa_interrupt_clr_reg;
};

/* ISR parameters LLD allocates (one for each MSI-X if enabled) vectors */
struct pmcraid_isr_param {
	struct pmcraid_instance *drv_inst;
	u8 hrrq_id;			/* hrrq entry index */
};


/* AEN message header sent as part of event data to applications */
struct pmcraid_aen_msg {
	u32 hostno;
	u32 length;
	u8  reserved[8];
	u8  data[];
};

/* Controller state event message type */
struct pmcraid_state_msg {
	struct pmcraid_aen_msg msg;
	u32 ioa_state;
};

#define PMC_DEVICE_EVENT_RESET_START		0x11000000
#define PMC_DEVICE_EVENT_RESET_SUCCESS		0x11000001
#define PMC_DEVICE_EVENT_RESET_FAILED		0x11000002
#define PMC_DEVICE_EVENT_SHUTDOWN_START		0x11000003
#define PMC_DEVICE_EVENT_SHUTDOWN_SUCCESS	0x11000004
#define PMC_DEVICE_EVENT_SHUTDOWN_FAILED	0x11000005

struct pmcraid_hostrcb {
	struct pmcraid_instance *drv_inst;
	struct pmcraid_aen_msg *msg;
	struct pmcraid_hcam_hdr *hcam;	/* pointer to hcam buffer */
	struct pmcraid_cmd  *cmd;       /* pointer to command block used */
	dma_addr_t baddr;		/* system address of hcam buffer */
	atomic_t ignore;		/* process HCAM response ? */
};

#define PMCRAID_AEN_HDR_SIZE	sizeof(struct pmcraid_aen_msg)



/*
 * Per adapter structure maintained by LLD
 */
struct pmcraid_instance {
	/* Array of allowed-to-be-exposed resources, initialized from
	 * Configutation Table, later updated with CCNs
	 */
	struct pmcraid_resource_entry *res_entries;

	struct list_head free_res_q;	/* res_entries lists for easy lookup */
	struct list_head used_res_q;	/* List of to be exposed resources */
	spinlock_t resource_lock;	/* spinlock to protect resource list */

	void __iomem *mapped_dma_addr;
	void __iomem *ioa_status;	/* Iomapped IOA status register */
	void __iomem *mailbox;		/* Iomapped mailbox register */
	void __iomem *ioarrin;		/* IOmapped IOARR IN register */

	struct pmcraid_interrupts int_regs;
	struct pmcraid_chip_details *chip_cfg;

	/* HostRCBs needed for HCAM */
	struct pmcraid_hostrcb ldn;
	struct pmcraid_hostrcb ccn;
	struct pmcraid_state_msg scn;	/* controller state change msg */


	/* Bus address of start of HRRQ */
	dma_addr_t hrrq_start_bus_addr[PMCRAID_NUM_MSIX_VECTORS];

	/* Pointer to 1st entry of HRRQ */
	__le32 *hrrq_start[PMCRAID_NUM_MSIX_VECTORS];

	/* Pointer to last entry of HRRQ */
	__le32 *hrrq_end[PMCRAID_NUM_MSIX_VECTORS];

	/* Pointer to current pointer of hrrq */
	__le32 *hrrq_curr[PMCRAID_NUM_MSIX_VECTORS];

	/* Lock for HRRQ access */
	spinlock_t hrrq_lock[PMCRAID_NUM_MSIX_VECTORS];

	struct pmcraid_inquiry_data *inq_data;
	dma_addr_t  inq_data_baddr;

	struct pmcraid_timestamp_data *timestamp_data;
	dma_addr_t  timestamp_data_baddr;

	/* size of configuration table entry, varies based on the firmware */
	u32	config_table_entry_size;

	/* Expected toggle bit at host */
	u8 host_toggle_bit[PMCRAID_NUM_MSIX_VECTORS];


	/* Wait Q for  threads to wait for Reset IOA completion */
	wait_queue_head_t reset_wait_q;
	struct pmcraid_cmd *reset_cmd;

	/* structures for supporting SIGIO based AEN. */
	struct fasync_struct *aen_queue;
	struct mutex aen_queue_lock;	/* lock for aen subscribers list */
	struct cdev cdev;

	struct Scsi_Host *host;	/* mid layer interface structure handle */
	struct pci_dev *pdev;	/* PCI device structure handle */

	/* No of Reset IOA retries . IOA marked dead if threshold exceeds */
	u8 ioa_reset_attempts;
#define PMCRAID_RESET_ATTEMPTS 3

	u8  current_log_level;	/* default level for logging IOASC errors */

	u8  num_hrrq;		/* Number of interrupt vectors allocated */
	u8  interrupt_mode;	/* current interrupt mode legacy or msix */
	dev_t dev;		/* Major-Minor numbers for Char device */

	/* Used as ISR handler argument */
	struct pmcraid_isr_param hrrq_vector[PMCRAID_NUM_MSIX_VECTORS];

	/* Message id as filled in last fired IOARCB, used to identify HRRQ */
	atomic_t last_message_id;

	/* configuration table */
	struct pmcraid_config_table *cfg_table;
	dma_addr_t cfg_table_bus_addr;

	/* structures related to command blocks */
	struct kmem_cache *cmd_cachep;		/* cache for cmd blocks */
	struct dma_pool *control_pool;		/* pool for control blocks */
	char   cmd_pool_name[64];		/* name of cmd cache */
	char   ctl_pool_name[64];		/* name of control cache */

	struct pmcraid_cmd *cmd_list[PMCRAID_MAX_CMD];

	struct list_head free_cmd_pool;
	struct list_head pending_cmd_pool;
	spinlock_t free_pool_lock;		/* free pool lock */
	spinlock_t pending_pool_lock;		/* pending pool lock */

	/* Tasklet to handle deferred processing */
	struct tasklet_struct isr_tasklet[PMCRAID_NUM_MSIX_VECTORS];

	/* Work-queue (Shared) for deferred reset processing */
	struct work_struct worker_q;

	/* No of IO commands pending with FW */
	atomic_t outstanding_cmds;

	/* should add/delete resources to mid-layer now ?*/
	atomic_t expose_resources;



	u32 ioa_state:4;	/* For IOA Reset sequence FSM */
#define IOA_STATE_OPERATIONAL       0x0
#define IOA_STATE_UNKNOWN           0x1
#define IOA_STATE_DEAD              0x2
#define IOA_STATE_IN_SOFT_RESET     0x3
#define IOA_STATE_IN_HARD_RESET     0x4
#define IOA_STATE_IN_RESET_ALERT    0x5
#define IOA_STATE_IN_BRINGDOWN      0x6
#define IOA_STATE_IN_BRINGUP        0x7

	u32 ioa_reset_in_progress:1; /* true if IOA reset is in progress */
	u32 ioa_hard_reset:1;	/* TRUE if Hard Reset is needed */
	u32 ioa_unit_check:1;	/* Indicates Unit Check condition */
	u32 ioa_bringdown:1;	/* whether IOA needs to be brought down */
	u32 force_ioa_reset:1;  /* force adapter reset ? */
	u32 reinit_cfg_table:1; /* reinit config table due to lost CCN */
	u32 ioa_shutdown_type:2;/* shutdown type used during reset */
#define SHUTDOWN_NONE               0x0
#define SHUTDOWN_NORMAL             0x1
#define SHUTDOWN_ABBREV             0x2
	u32 timestamp_error:1; /* indicate set timestamp for out of sync */

};

/* LLD maintained resource entry structure */
struct pmcraid_resource_entry {
	struct list_head queue;	/* link to "to be exposed" resources */
	union {
		struct pmcraid_config_table_entry cfg_entry;
		struct pmcraid_config_table_entry_ext cfg_entry_ext;
	};
	struct scsi_device *scsi_dev;	/* Link scsi_device structure */
	atomic_t read_failures;		/* count of failed READ commands */
	atomic_t write_failures;	/* count of failed WRITE commands */

	/* To indicate add/delete/modify during CCN */
	u8 change_detected;
#define RES_CHANGE_ADD          0x1	/* add this to mid-layer */
#define RES_CHANGE_DEL          0x2	/* remove this from mid-layer */

	u8 reset_progress;      /* Device is resetting */

	/*
	 * When IOA asks for sync (i.e. IOASC = Not Ready, Sync Required), this
	 * flag will be set, mid layer will be asked to retry. In the next
	 * attempt, this flag will be checked in queuecommand() to set
	 * SYNC_COMPLETE flag in IOARCB (flag_0).
	 */
	u8 sync_reqd;

	/* target indicates the mapped target_id assigned to this resource if
	 * this is VSET resource. For non-VSET resources this will be un-used
	 * or zero
	 */
	u8 target;
};

/* Data structures used in IOASC error code logging */
struct pmcraid_ioasc_error {
	u32 ioasc_code;		/* IOASC code */
	u8 log_level;		/* default log level assignment. */
	char *error_string;
};

/* Initial log_level assignments for various IOASCs */
#define IOASC_LOG_LEVEL_NONE	    0x0 /* no logging */
#define IOASC_LOG_LEVEL_MUST        0x1	/* must log: all high-severity errors */
#define IOASC_LOG_LEVEL_HARD        0x2	/* optional – low severity errors */

/* Error information maintained by LLD. LLD initializes the pmcraid_error_table
 * statically.
 */
static struct pmcraid_ioasc_error pmcraid_ioasc_error_table[] = {
	{0x01180600, IOASC_LOG_LEVEL_HARD,
	 "Recovered Error, soft media error, sector reassignment suggested"},
	{0x015D0000, IOASC_LOG_LEVEL_HARD,
	 "Recovered Error, failure prediction threshold exceeded"},
	{0x015D9200, IOASC_LOG_LEVEL_HARD,
	 "Recovered Error, soft Cache Card Battery error threshold"},
	{0x015D9200, IOASC_LOG_LEVEL_HARD,
	 "Recovered Error, soft Cache Card Battery error threshold"},
	{0x02048000, IOASC_LOG_LEVEL_HARD,
	 "Not Ready, IOA Reset Required"},
	{0x02408500, IOASC_LOG_LEVEL_HARD,
	 "Not Ready, IOA microcode download required"},
	{0x03110B00, IOASC_LOG_LEVEL_HARD,
	 "Medium Error, data unreadable, reassignment suggested"},
	{0x03110C00, IOASC_LOG_LEVEL_MUST,
	 "Medium Error, data unreadable do not reassign"},
	{0x03310000, IOASC_LOG_LEVEL_HARD,
	 "Medium Error, media corrupted"},
	{0x04050000, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, IOA can't communicate with device"},
	{0x04080000, IOASC_LOG_LEVEL_MUST,
	 "Hardware Error, device bus error"},
	{0x04088000, IOASC_LOG_LEVEL_MUST,
	 "Hardware Error, device bus is not functioning"},
	{0x04118000, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, IOA reserved area data check"},
	{0x04118100, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, IOA reserved area invalid data pattern"},
	{0x04118200, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, IOA reserved area LRC error"},
	{0x04320000, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, reassignment space exhausted"},
	{0x04330000, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, data transfer underlength error"},
	{0x04330000, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, data transfer overlength error"},
	{0x04418000, IOASC_LOG_LEVEL_MUST,
	 "Hardware Error, PCI bus error"},
	{0x04440000, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, device error"},
	{0x04448200, IOASC_LOG_LEVEL_MUST,
	 "Hardware Error, IOA error"},
	{0x04448300, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, undefined device response"},
	{0x04448400, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, IOA microcode error"},
	{0x04448600, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, IOA reset required"},
	{0x04449200, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, hard Cache Fearuee Card Battery error"},
	{0x0444A000, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, failed device altered"},
	{0x0444A200, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, data check after reassignment"},
	{0x0444A300, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, LRC error after reassignment"},
	{0x044A0000, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, device bus error (msg/cmd phase)"},
	{0x04670400, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, new device can't be used"},
	{0x04678000, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, invalid multiadapter configuration"},
	{0x04678100, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, incorrect connection between enclosures"},
	{0x04678200, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, connections exceed IOA design limits"},
	{0x04678300, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, incorrect multipath connection"},
	{0x04679000, IOASC_LOG_LEVEL_HARD,
	 "Hardware Error, command to LUN failed"},
	{0x064C8000, IOASC_LOG_LEVEL_HARD,
	 "Unit Attention, cache exists for missing/failed device"},
	{0x06670100, IOASC_LOG_LEVEL_HARD,
	 "Unit Attention, incompatible exposed mode device"},
	{0x06670600, IOASC_LOG_LEVEL_HARD,
	 "Unit Attention, attachment of logical unit failed"},
	{0x06678000, IOASC_LOG_LEVEL_HARD,
	 "Unit Attention, cables exceed connective design limit"},
	{0x06678300, IOASC_LOG_LEVEL_HARD,
	 "Unit Attention, incomplete multipath connection between" \
	 "IOA and enclosure"},
	{0x06678400, IOASC_LOG_LEVEL_HARD,
	 "Unit Attention, incomplete multipath connection between" \
	 "device and enclosure"},
	{0x06678500, IOASC_LOG_LEVEL_HARD,
	 "Unit Attention, incomplete multipath connection between" \
	 "IOA and remote IOA"},
	{0x06678600, IOASC_LOG_LEVEL_HARD,
	 "Unit Attention, missing remote IOA"},
	{0x06679100, IOASC_LOG_LEVEL_HARD,
	 "Unit Attention, enclosure doesn't support required multipath" \
	 "function"},
	{0x06698200, IOASC_LOG_LEVEL_HARD,
	 "Unit Attention, corrupt array parity detected on device"},
	{0x066B0200, IOASC_LOG_LEVEL_HARD,
	 "Unit Attention, array exposed"},
	{0x066B8200, IOASC_LOG_LEVEL_HARD,
	 "Unit Attention, exposed array is still protected"},
	{0x066B9200, IOASC_LOG_LEVEL_HARD,
	 "Unit Attention, Multipath redundancy level got worse"},
	{0x07270000, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, device is read/write protected by IOA"},
	{0x07278000, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, IOA doesn't support device attribute"},
	{0x07278100, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, NVRAM mirroring prohibited"},
	{0x07278400, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, array is short 2 or more devices"},
	{0x07278600, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, exposed array is short a required device"},
	{0x07278700, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, array members not at required addresses"},
	{0x07278800, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, exposed mode device resource address conflict"},
	{0x07278900, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, incorrect resource address of exposed mode device"},
	{0x07278A00, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, Array is missing a device and parity is out of sync"},
	{0x07278B00, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, maximum number of arrays already exist"},
	{0x07278C00, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, cannot locate cache data for device"},
	{0x07278D00, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, cache data exits for a changed device"},
	{0x07279100, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, detection of a device requiring format"},
	{0x07279200, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, IOA exceeds maximum number of devices"},
	{0x07279600, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, missing array, volume set is not functional"},
	{0x07279700, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, single device for a volume set"},
	{0x07279800, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, missing multiple devices for a volume set"},
	{0x07279900, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, maximum number of volument sets already exists"},
	{0x07279A00, IOASC_LOG_LEVEL_HARD,
	 "Data Protect, other volume set problem"},
};

/* macros to help in debugging */
#define pmcraid_err(...)  \
	printk(KERN_ERR "MaxRAID: "__VA_ARGS__)

#define pmcraid_info(...) \
	if (pmcraid_debug_log) \
		printk(KERN_INFO "MaxRAID: "__VA_ARGS__)

/* check if given command is a SCSI READ or SCSI WRITE command */
#define SCSI_READ_CMD           0x1	/* any of SCSI READ commands */
#define SCSI_WRITE_CMD          0x2	/* any of SCSI WRITE commands */
#define SCSI_CMD_TYPE(opcode) \
({  u8 op = opcode; u8 __type = 0;\
	if (op == READ_6 || op == READ_10 || op == READ_12 || op == READ_16)\
		__type = SCSI_READ_CMD;\
	else if (op == WRITE_6 || op == WRITE_10 || op == WRITE_12 || \
		 op == WRITE_16)\
		__type = SCSI_WRITE_CMD;\
	__type;\
})

#define IS_SCSI_READ_WRITE(opcode) \
({	u8 __type = SCSI_CMD_TYPE(opcode); \
	(__type == SCSI_READ_CMD || __type == SCSI_WRITE_CMD) ? 1 : 0;\
})


/*
 * pmcraid_ioctl_header - definition of header structure that precedes all the
 * buffers given as ioctl arguments.
 *
 * .signature           : always ASCII string, "PMCRAID"
 * .reserved            : not used
 * .buffer_length       : length of the buffer following the header
 */
struct pmcraid_ioctl_header {
	u8  signature[8];
	u32 reserved;
	u32 buffer_length;
};

#define PMCRAID_IOCTL_SIGNATURE      "PMCRAID"

/*
 * pmcraid_passthrough_ioctl_buffer - structure given as argument to
 * passthrough(or firmware handled) IOCTL commands. Note that ioarcb requires
 * 32-byte alignment so, it is necessary to pack this structure to avoid any
 * holes between ioctl_header and passthrough buffer
 *
 * .ioactl_header : ioctl header
 * .ioarcb        : filled-up ioarcb buffer, driver always reads this buffer
 * .ioasa         : buffer for ioasa, driver fills this with IOASA from firmware
 * .request_buffer: The I/O buffer (flat), driver reads/writes to this based on
 *                  the transfer directions passed in ioarcb.flags0. Contents
 *                  of this buffer are valid only when ioarcb.data_transfer_len
 *                  is not zero.
 */
struct pmcraid_passthrough_ioctl_buffer {
	struct pmcraid_ioctl_header ioctl_header;
	struct pmcraid_ioarcb ioarcb;
	struct pmcraid_ioasa  ioasa;
	u8  request_buffer[];
} __attribute__ ((packed, aligned(PMCRAID_IOARCB_ALIGNMENT)));

/*
 * keys to differentiate between driver handled IOCTLs and passthrough
 * IOCTLs passed to IOA. driver determines the ioctl type using macro
 * _IOC_TYPE
 */
#define PMCRAID_DRIVER_IOCTL         'D'
#define PMCRAID_PASSTHROUGH_IOCTL    'F'

#define DRV_IOCTL(n, size) \
	_IOC(_IOC_READ|_IOC_WRITE, PMCRAID_DRIVER_IOCTL, (n), (size))

#define FMW_IOCTL(n, size) \
	_IOC(_IOC_READ|_IOC_WRITE, PMCRAID_PASSTHROUGH_IOCTL,  (n), (size))

/*
 * _ARGSIZE: macro that gives size of the argument type passed to an IOCTL cmd.
 * This is to facilitate applications avoiding un-necessary memory allocations.
 * For example, most of driver handled ioctls do not require ioarcb, ioasa.
 */
#define _ARGSIZE(arg) (sizeof(struct pmcraid_ioctl_header) + sizeof(arg))

/* Driver handled IOCTL command definitions */

#define PMCRAID_IOCTL_RESET_ADAPTER          \
	DRV_IOCTL(5, sizeof(struct pmcraid_ioctl_header))

/* passthrough/firmware handled commands */
#define PMCRAID_IOCTL_PASSTHROUGH_COMMAND         \
	FMW_IOCTL(1, sizeof(struct pmcraid_passthrough_ioctl_buffer))

#define PMCRAID_IOCTL_DOWNLOAD_MICROCODE     \
	FMW_IOCTL(2, sizeof(struct pmcraid_passthrough_ioctl_buffer))


#endif /* _PMCRAID_H */
