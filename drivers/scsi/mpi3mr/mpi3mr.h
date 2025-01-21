/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Broadcom MPI3 Storage Controllers
 *
 * Copyright (C) 2017-2023 Broadcom Inc.
 *  (mailto: mpi3mr-linuxdrv.pdl@broadcom.com)
 *
 */

#ifndef MPI3MR_H_INCLUDED
#define MPI3MR_H_INCLUDED

#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/blk-mq-pci.h>
#include <linux/delay.h>
#include <linux/dmapool.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/utsname.h>
#include <linux/workqueue.h>
#include <linux/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <uapi/scsi/scsi_bsg_mpi3mr.h>
#include <scsi/scsi_transport_sas.h>

#include "mpi/mpi30_transport.h"
#include "mpi/mpi30_cnfg.h"
#include "mpi/mpi30_image.h"
#include "mpi/mpi30_init.h"
#include "mpi/mpi30_ioc.h"
#include "mpi/mpi30_sas.h"
#include "mpi/mpi30_pci.h"
#include "mpi/mpi30_tool.h"
#include "mpi3mr_debug.h"

/* Global list and lock for storing multiple adapters managed by the driver */
extern spinlock_t mrioc_list_lock;
extern struct list_head mrioc_list;
extern int prot_mask;
extern atomic64_t event_counter;

#define MPI3MR_DRIVER_VERSION	"8.12.0.3.50"
#define MPI3MR_DRIVER_RELDATE	"11-November-2024"

#define MPI3MR_DRIVER_NAME	"mpi3mr"
#define MPI3MR_DRIVER_LICENSE	"GPL"
#define MPI3MR_DRIVER_AUTHOR	"Broadcom Inc. <mpi3mr-linuxdrv.pdl@broadcom.com>"
#define MPI3MR_DRIVER_DESC	"MPI3 Storage Controller Device Driver"

#define MPI3MR_NAME_LENGTH	64
#define IOCNAME			"%s: "

#define MPI3MR_DEFAULT_MAX_IO_SIZE	(1 * 1024 * 1024)

/* Definitions for internal SGL and Chain SGL buffers */
#define MPI3MR_PAGE_SIZE_4K		4096
#define MPI3MR_DEFAULT_SGL_ENTRIES	256
#define MPI3MR_MAX_SGL_ENTRIES		2048

/* Definitions for MAX values for shost */
#define MPI3MR_MAX_CMDS_LUN	128
#define MPI3MR_MAX_CDB_LENGTH	32

/* Admin queue management definitions */
#define MPI3MR_ADMIN_REQ_Q_SIZE		(2 * MPI3MR_PAGE_SIZE_4K)
#define MPI3MR_ADMIN_REPLY_Q_SIZE	(4 * MPI3MR_PAGE_SIZE_4K)
#define MPI3MR_ADMIN_REQ_FRAME_SZ	128
#define MPI3MR_ADMIN_REPLY_FRAME_SZ	16

/* Operational queue management definitions */
#define MPI3MR_OP_REQ_Q_QD		512
#define MPI3MR_OP_REP_Q_QD		1024
#define MPI3MR_OP_REP_Q_QD4K		4096
#define MPI3MR_OP_REQ_Q_SEG_SIZE	4096
#define MPI3MR_OP_REP_Q_SEG_SIZE	4096
#define MPI3MR_MAX_SEG_LIST_SIZE	4096

/* Reserved Host Tag definitions */
#define MPI3MR_HOSTTAG_INVALID		0xFFFF
#define MPI3MR_HOSTTAG_INITCMDS		1
#define MPI3MR_HOSTTAG_BSG_CMDS		2
#define MPI3MR_HOSTTAG_PEL_ABORT	3
#define MPI3MR_HOSTTAG_PEL_WAIT		4
#define MPI3MR_HOSTTAG_BLK_TMS		5
#define MPI3MR_HOSTTAG_CFG_CMDS		6
#define MPI3MR_HOSTTAG_TRANSPORT_CMDS	7

#define MPI3MR_NUM_DEVRMCMD		16
#define MPI3MR_HOSTTAG_DEVRMCMD_MIN	(MPI3MR_HOSTTAG_TRANSPORT_CMDS + 1)
#define MPI3MR_HOSTTAG_DEVRMCMD_MAX	(MPI3MR_HOSTTAG_DEVRMCMD_MIN + \
						MPI3MR_NUM_DEVRMCMD - 1)

#define MPI3MR_INTERNAL_CMDS_RESVD	MPI3MR_HOSTTAG_DEVRMCMD_MAX
#define MPI3MR_NUM_EVTACKCMD		4
#define MPI3MR_HOSTTAG_EVTACKCMD_MIN	(MPI3MR_HOSTTAG_DEVRMCMD_MAX + 1)
#define MPI3MR_HOSTTAG_EVTACKCMD_MAX	(MPI3MR_HOSTTAG_EVTACKCMD_MIN + \
					MPI3MR_NUM_EVTACKCMD - 1)

/* Reduced resource count definition for crash kernel */
#define MPI3MR_HOST_IOS_KDUMP		128

/* command/controller interaction timeout definitions in seconds */
#define MPI3MR_INTADMCMD_TIMEOUT		60
#define MPI3MR_PORTENABLE_TIMEOUT		300
#define MPI3MR_PORTENABLE_POLL_INTERVAL		5
#define MPI3MR_ABORTTM_TIMEOUT			60
#define MPI3MR_RESETTM_TIMEOUT			60
#define MPI3MR_RESET_HOST_IOWAIT_TIMEOUT	5
#define MPI3MR_TSUPDATE_INTERVAL		900
#define MPI3MR_DEFAULT_SHUTDOWN_TIME		120
#define	MPI3MR_RAID_ERRREC_RESET_TIMEOUT	180
#define MPI3MR_PREPARE_FOR_RESET_TIMEOUT	180
#define MPI3MR_RESET_ACK_TIMEOUT		30
#define MPI3MR_MUR_TIMEOUT			120
#define MPI3MR_RESET_TIMEOUT			510

#define MPI3MR_WATCHDOG_INTERVAL		1000 /* in milli seconds */

#define MPI3MR_RESET_TOPOLOGY_SETTLE_TIME	10

#define MPI3MR_SCMD_TIMEOUT    (60 * HZ)
#define MPI3MR_EH_SCMD_TIMEOUT (60 * HZ)

/* Internal admin command state definitions*/
#define MPI3MR_CMD_NOTUSED	0x8000
#define MPI3MR_CMD_COMPLETE	0x0001
#define MPI3MR_CMD_PENDING	0x0002
#define MPI3MR_CMD_REPLY_VALID	0x0004
#define MPI3MR_CMD_RESET	0x0008

/* Definitions for Event replies and sense buffer allocated per controller */
#define MPI3MR_NUM_EVT_REPLIES	64
#define MPI3MR_SENSE_BUF_SZ	256
#define MPI3MR_SENSEBUF_FACTOR	3
#define MPI3MR_CHAINBUF_FACTOR	3
#define MPI3MR_CHAINBUFDIX_FACTOR	2

/* Invalid target device handle */
#define MPI3MR_INVALID_DEV_HANDLE	0xFFFF

/* Controller Reset related definitions */
#define MPI3MR_HOSTDIAG_UNLOCK_RETRY_COUNT	5
#define MPI3MR_MAX_RESET_RETRY_COUNT		3

/* ResponseCode definitions */
#define MPI3MR_RI_MASK_RESPCODE		(0x000000FF)
#define MPI3MR_RSP_IO_QUEUED_ON_IOC \
			MPI3_SCSITASKMGMT_RSPCODE_IO_QUEUED_ON_IOC

#define MPI3MR_DEFAULT_MDTS	(128 * 1024)
#define MPI3MR_DEFAULT_PGSZEXP         (12)

/* Command retry count definitions */
#define MPI3MR_DEV_RMHS_RETRY_COUNT 3
#define MPI3MR_PEL_RETRY_COUNT 3

/* Default target device queue depth */
#define MPI3MR_DEFAULT_SDEV_QD	32

/* Definitions for Threaded IRQ poll*/
#define MPI3MR_IRQ_POLL_SLEEP			20
#define MPI3MR_IRQ_POLL_TRIGGER_IOCOUNT		8

/* Definitions for the controller security status*/
#define MPI3MR_CTLR_SECURITY_STATUS_MASK	0x0C
#define MPI3MR_CTLR_SECURE_DBG_STATUS_MASK	0x02

#define MPI3MR_INVALID_DEVICE			0x00
#define MPI3MR_CONFIG_SECURE_DEVICE		0x04
#define MPI3MR_HARD_SECURE_DEVICE		0x08
#define MPI3MR_TAMPERED_DEVICE			0x0C

#define MPI3MR_DEFAULT_HDB_MAX_SZ       (4 * 1024 * 1024)
#define MPI3MR_DEFAULT_HDB_DEC_SZ       (1 * 1024 * 1024)
#define MPI3MR_DEFAULT_HDB_MIN_SZ       (2 * 1024 * 1024)
#define MPI3MR_MAX_NUM_HDB      2

#define MPI3MR_HDB_TRIGGER_TYPE_UNKNOWN		0
#define MPI3MR_HDB_TRIGGER_TYPE_FAULT		1
#define MPI3MR_HDB_TRIGGER_TYPE_ELEMENT		2
#define MPI3MR_HDB_TRIGGER_TYPE_GLOBAL          3
#define MPI3MR_HDB_TRIGGER_TYPE_SOFT_RESET	4
#define MPI3MR_HDB_TRIGGER_TYPE_FW_RELEASED	5

#define MPI3MR_HDB_REFRESH_TYPE_RESERVED       0
#define MPI3MR_HDB_REFRESH_TYPE_CURRENT                1
#define MPI3MR_HDB_REFRESH_TYPE_DEFAULT                2
#define MPI3MR_HDB_HDB_REFRESH_TYPE_PERSISTENT 3

#define MPI3MR_DEFAULT_HDB_SZ  (4 * 1024 * 1024)
#define MPI3MR_MAX_NUM_HDB     2

#define MPI3MR_HDB_QUERY_ELEMENT_TRIGGER_FORMAT_INDEX   0
#define MPI3MR_HDB_QUERY_ELEMENT_TRIGGER_FORMAT_DATA    1

#define MPI3MR_THRESHOLD_REPLY_COUNT	100

/* SGE Flag definition */
#define MPI3MR_SGEFLAGS_SYSTEM_SIMPLE_END_OF_LIST \
	(MPI3_SGE_FLAGS_ELEMENT_TYPE_SIMPLE | MPI3_SGE_FLAGS_DLAS_SYSTEM | \
	MPI3_SGE_FLAGS_END_OF_LIST)

/* MSI Index from Reply Queue Index */
#define REPLY_QUEUE_IDX_TO_MSIX_IDX(qidx, offset)	(qidx + offset)

/*
 * Maximum data transfer size definitions for management
 * application commands
 */
#define MPI3MR_MAX_APP_XFER_SIZE	(1 * 1024 * 1024)
#define MPI3MR_MAX_APP_XFER_SEGMENTS	512
/*
 * 2048 sectors are for data buffers and additional 512 sectors for
 * other buffers
 */
#define MPI3MR_MAX_APP_XFER_SECTORS	(2048 + 512)

#define MPI3MR_WRITE_SAME_MAX_LEN_256_BLKS 256
#define MPI3MR_WRITE_SAME_MAX_LEN_2048_BLKS 2048

#define MPI3MR_DRIVER_EVENT_PROCESS_TRIGGER    (0xFFFD)

/**
 * struct mpi3mr_nvme_pt_sge -  Structure to store SGEs for NVMe
 * Encapsulated commands.
 *
 * @base_addr: Physical address
 * @length: SGE length
 * @rsvd: Reserved
 * @rsvd1: Reserved
 * @sub_type: sgl sub type
 * @type: sgl type
 */
struct mpi3mr_nvme_pt_sge {
	__le64 base_addr;
	__le32 length;
	u16 rsvd;
	u8 rsvd1;
	u8 sub_type:4;
	u8 type:4;
};

/**
 * struct mpi3mr_buf_map -  local structure to
 * track kernel and user buffers associated with an BSG
 * structure.
 *
 * @bsg_buf: BSG buffer virtual address
 * @bsg_buf_len:  BSG buffer length
 * @kern_buf: Kernel buffer virtual address
 * @kern_buf_len: Kernel buffer length
 * @kern_buf_dma: Kernel buffer DMA address
 * @data_dir: Data direction.
 */
struct mpi3mr_buf_map {
	void *bsg_buf;
	u32 bsg_buf_len;
	void *kern_buf;
	u32 kern_buf_len;
	dma_addr_t kern_buf_dma;
	u8 data_dir;
	u16 num_dma_desc;
	struct dma_memory_desc *dma_desc;
};

/* IOC State definitions */
enum mpi3mr_iocstate {
	MRIOC_STATE_READY = 1,
	MRIOC_STATE_RESET,
	MRIOC_STATE_FAULT,
	MRIOC_STATE_BECOMING_READY,
	MRIOC_STATE_RESET_REQUESTED,
	MRIOC_STATE_UNRECOVERABLE,
};

/* Reset reason code definitions*/
enum mpi3mr_reset_reason {
	MPI3MR_RESET_FROM_BRINGUP = 1,
	MPI3MR_RESET_FROM_FAULT_WATCH = 2,
	MPI3MR_RESET_FROM_APP = 3,
	MPI3MR_RESET_FROM_EH_HOS = 4,
	MPI3MR_RESET_FROM_TM_TIMEOUT = 5,
	MPI3MR_RESET_FROM_APP_TIMEOUT = 6,
	MPI3MR_RESET_FROM_MUR_FAILURE = 7,
	MPI3MR_RESET_FROM_CTLR_CLEANUP = 8,
	MPI3MR_RESET_FROM_CIACTIV_FAULT = 9,
	MPI3MR_RESET_FROM_PE_TIMEOUT = 10,
	MPI3MR_RESET_FROM_TSU_TIMEOUT = 11,
	MPI3MR_RESET_FROM_DELREQQ_TIMEOUT = 12,
	MPI3MR_RESET_FROM_DELREPQ_TIMEOUT = 13,
	MPI3MR_RESET_FROM_CREATEREPQ_TIMEOUT = 14,
	MPI3MR_RESET_FROM_CREATEREQQ_TIMEOUT = 15,
	MPI3MR_RESET_FROM_IOCFACTS_TIMEOUT = 16,
	MPI3MR_RESET_FROM_IOCINIT_TIMEOUT = 17,
	MPI3MR_RESET_FROM_EVTNOTIFY_TIMEOUT = 18,
	MPI3MR_RESET_FROM_EVTACK_TIMEOUT = 19,
	MPI3MR_RESET_FROM_CIACTVRST_TIMER = 20,
	MPI3MR_RESET_FROM_GETPKGVER_TIMEOUT = 21,
	MPI3MR_RESET_FROM_PELABORT_TIMEOUT = 22,
	MPI3MR_RESET_FROM_SYSFS = 23,
	MPI3MR_RESET_FROM_SYSFS_TIMEOUT = 24,
	MPI3MR_RESET_FROM_DIAG_BUFFER_POST_TIMEOUT = 25,
	MPI3MR_RESET_FROM_DIAG_BUFFER_RELEASE_TIMEOUT = 26,
	MPI3MR_RESET_FROM_FIRMWARE = 27,
	MPI3MR_RESET_FROM_CFG_REQ_TIMEOUT = 29,
	MPI3MR_RESET_FROM_SAS_TRANSPORT_TIMEOUT = 30,
	MPI3MR_RESET_FROM_TRIGGER = 31,
};

#define MPI3MR_RESET_REASON_OSTYPE_LINUX	1
#define MPI3MR_RESET_REASON_OSTYPE_SHIFT	28
#define MPI3MR_RESET_REASON_IOCNUM_SHIFT	20

/* Queue type definitions */
enum queue_type {
	MPI3MR_DEFAULT_QUEUE = 0,
	MPI3MR_POLL_QUEUE,
};

/**
 * struct mpi3mr_compimg_ver - replica of component image
 * version defined in mpi30_image.h in host endianness
 *
 */
struct mpi3mr_compimg_ver {
	u16 build_num;
	u16 cust_id;
	u8 ph_minor;
	u8 ph_major;
	u8 gen_minor;
	u8 gen_major;
};

/**
 * struct mpi3mr_ioc_facs - replica of component image version
 * defined in mpi30_ioc.h in host endianness
 *
 */
struct mpi3mr_ioc_facts {
	u32 ioc_capabilities;
	struct mpi3mr_compimg_ver fw_ver;
	u32 mpi_version;
	u32 diag_trace_sz;
	u32 diag_fw_sz;
	u32 diag_drvr_sz;
	u16 max_reqs;
	u16 product_id;
	u16 op_req_sz;
	u16 reply_sz;
	u16 exceptions;
	u16 max_perids;
	u16 max_pds;
	u16 max_sasexpanders;
	u32 max_data_length;
	u16 max_sasinitiators;
	u16 max_enclosures;
	u16 max_pcie_switches;
	u16 max_nvme;
	u16 max_vds;
	u16 max_hpds;
	u16 max_advhpds;
	u16 max_raid_pds;
	u16 min_devhandle;
	u16 max_devhandle;
	u16 max_op_req_q;
	u16 max_op_reply_q;
	u16 shutdown_timeout;
	u8 ioc_num;
	u8 who_init;
	u16 max_msix_vectors;
	u8 personality;
	u8 dma_mask;
	u8 protocol_flags;
	u8 sge_mod_mask;
	u8 sge_mod_value;
	u8 sge_mod_shift;
	u8 max_dev_per_tg;
	u16 max_io_throttle_group;
	u16 io_throttle_data_length;
	u16 io_throttle_low;
	u16 io_throttle_high;

};

/**
 * struct segments - memory descriptor structure to store
 * virtual and dma addresses for operational queue segments.
 *
 * @segment: virtual address
 * @segment_dma: dma address
 */
struct segments {
	void *segment;
	dma_addr_t segment_dma;
};

/**
 * struct op_req_qinfo -  Operational Request Queue Information
 *
 * @ci: consumer index
 * @pi: producer index
 * @num_request: Maximum number of entries in the queue
 * @qid: Queue Id starting from 1
 * @reply_qid: Associated reply queue Id
 * @num_segments: Number of discontiguous memory segments
 * @segment_qd: Depth of each segments
 * @q_lock: Concurrent queue access lock
 * @q_segments: Segment descriptor pointer
 * @q_segment_list: Segment list base virtual address
 * @q_segment_list_dma: Segment list base DMA address
 */
struct op_req_qinfo {
	u16 ci;
	u16 pi;
	u16 num_requests;
	u16 qid;
	u16 reply_qid;
	u16 num_segments;
	u16 segment_qd;
	spinlock_t q_lock;
	struct segments *q_segments;
	void *q_segment_list;
	dma_addr_t q_segment_list_dma;
};

/**
 * struct op_reply_qinfo -  Operational Reply Queue Information
 *
 * @ci: consumer index
 * @qid: Queue Id starting from 1
 * @num_replies: Maximum number of entries in the queue
 * @num_segments: Number of discontiguous memory segments
 * @segment_qd: Depth of each segments
 * @q_segments: Segment descriptor pointer
 * @q_segment_list: Segment list base virtual address
 * @q_segment_list_dma: Segment list base DMA address
 * @ephase: Expected phased identifier for the reply queue
 * @pend_ios: Number of IOs pending in HW for this queue
 * @enable_irq_poll: Flag to indicate polling is enabled
 * @in_use: Queue is handled by poll/ISR
 * @qtype: Type of queue (types defined in enum queue_type)
 */
struct op_reply_qinfo {
	u16 ci;
	u16 qid;
	u16 num_replies;
	u16 num_segments;
	u16 segment_qd;
	struct segments *q_segments;
	void *q_segment_list;
	dma_addr_t q_segment_list_dma;
	u8 ephase;
	atomic_t pend_ios;
	bool enable_irq_poll;
	atomic_t in_use;
	enum queue_type qtype;
};

/**
 * struct mpi3mr_intr_info -  Interrupt cookie information
 *
 * @mrioc: Adapter instance reference
 * @os_irq: irq number
 * @msix_index: MSIx index
 * @op_reply_q: Associated operational reply queue
 * @name: Dev name for the irq claiming device
 */
struct mpi3mr_intr_info {
	struct mpi3mr_ioc *mrioc;
	int os_irq;
	u16 msix_index;
	struct op_reply_qinfo *op_reply_q;
	char name[MPI3MR_NAME_LENGTH];
};

/**
 * struct mpi3mr_throttle_group_info - Throttle group info
 *
 * @io_divert: Flag indicates io divert is on or off for the TG
 * @need_qd_reduction: Flag to indicate QD reduction is needed
 * @qd_reduction: Queue Depth reduction in units of 10%
 * @fw_qd: QueueDepth value reported by the firmware
 * @modified_qd: Modified QueueDepth value due to throttling
 * @id: Throttle Group ID.
 * @high: High limit to turn on throttling in 512 byte blocks
 * @low: Low limit to turn off throttling in 512 byte blocks
 * @pend_large_data_sz: Counter to track pending large data
 */
struct mpi3mr_throttle_group_info {
	u8 io_divert;
	u8 need_qd_reduction;
	u8 qd_reduction;
	u16 fw_qd;
	u16 modified_qd;
	u16 id;
	u32 high;
	u32 low;
	atomic_t pend_large_data_sz;
};

/* HBA port flags */
#define MPI3MR_HBA_PORT_FLAG_DIRTY	0x01
#define MPI3MR_HBA_PORT_FLAG_NEW       0x02

/* IOCTL data transfer sge*/
#define MPI3MR_NUM_IOCTL_SGE		256
#define MPI3MR_IOCTL_SGE_SIZE		(8 * 1024)

/**
 * struct mpi3mr_hba_port - HBA's port information
 * @port_id: Port number
 * @flags: HBA port flags
 */
struct mpi3mr_hba_port {
	struct list_head list;
	u8 port_id;
	u8 flags;
};

/**
 * struct mpi3mr_sas_port - Internal SAS port information
 * @port_list: List of ports belonging to a SAS node
 * @num_phys: Number of phys associated with port
 * @marked_responding: used while refresing the sas ports
 * @lowest_phy: lowest phy ID of current sas port, valid for controller port
 * @phy_mask: phy_mask of current sas port, valid for controller port
 * @hba_port: HBA port entry
 * @remote_identify: Attached device identification
 * @rphy: SAS transport layer rphy object
 * @port: SAS transport layer port object
 * @phy_list: mpi3mr_sas_phy objects belonging to this port
 */
struct mpi3mr_sas_port {
	struct list_head port_list;
	u8 num_phys;
	u8 marked_responding;
	int lowest_phy;
	u64 phy_mask;
	struct mpi3mr_hba_port *hba_port;
	struct sas_identify remote_identify;
	struct sas_rphy *rphy;
	struct sas_port *port;
	struct list_head phy_list;
};

/**
 * struct mpi3mr_sas_phy - Internal SAS Phy information
 * @port_siblings: List of phys belonging to a port
 * @identify: Phy identification
 * @remote_identify: Attached device identification
 * @phy: SAS transport layer Phy object
 * @phy_id: Unique phy id within a port
 * @handle: Firmware device handle for this phy
 * @attached_handle: Firmware device handle for attached device
 * @phy_belongs_to_port: Flag to indicate phy belongs to port
   @hba_port: HBA port entry
 */
struct mpi3mr_sas_phy {
	struct list_head port_siblings;
	struct sas_identify identify;
	struct sas_identify remote_identify;
	struct sas_phy *phy;
	u8 phy_id;
	u16 handle;
	u16 attached_handle;
	u8 phy_belongs_to_port;
	struct mpi3mr_hba_port *hba_port;
};

/**
 * struct mpi3mr_sas_node - SAS host/expander information
 * @list: List of sas nodes in a controller
 * @parent_dev: Parent device class
 * @num_phys: Number phys belonging to sas_node
 * @sas_address: SAS address of sas_node
 * @handle: Firmware device handle for this sas_host/expander
 * @sas_address_parent: SAS address of parent expander or host
 * @enclosure_handle: Firmware handle of enclosure of this node
 * @device_info: Capabilities of this sas_host/expander
 * @non_responding: used to refresh the expander devices during reset
 * @host_node: Flag to indicate this is a host_node
 * @hba_port: HBA port entry
 * @phy: A list of phys that make up this sas_host/expander
 * @sas_port_list: List of internal ports of this node
 * @rphy: sas_rphy object of this expander node
 */
struct mpi3mr_sas_node {
	struct list_head list;
	struct device *parent_dev;
	u8 num_phys;
	u64 sas_address;
	u16 handle;
	u64 sas_address_parent;
	u16 enclosure_handle;
	u64 enclosure_logical_id;
	u8 non_responding;
	u8 host_node;
	struct mpi3mr_hba_port *hba_port;
	struct mpi3mr_sas_phy *phy;
	struct list_head sas_port_list;
	struct sas_rphy *rphy;
};

/**
 * struct mpi3mr_enclosure_node - enclosure information
 * @list: List of enclosures
 * @pg0: Enclosure page 0;
 */
struct mpi3mr_enclosure_node {
	struct list_head list;
	struct mpi3_enclosure_page0 pg0;
};

/**
 * struct tgt_dev_sas_sata - SAS/SATA device specific
 * information cached from firmware given data
 *
 * @sas_address: World wide unique SAS address
 * @sas_address_parent: Sas address of parent expander or host
 * @dev_info: Device information bits
 * @phy_id: Phy identifier provided in device page 0
 * @attached_phy_id: Attached phy identifier provided in device page 0
 * @sas_transport_attached: Is this device exposed to transport
 * @pend_sas_rphy_add: Flag to check device is in process of add
 * @hba_port: HBA port entry
 * @rphy: SAS transport layer rphy object
 */
struct tgt_dev_sas_sata {
	u64 sas_address;
	u64 sas_address_parent;
	u16 dev_info;
	u8 phy_id;
	u8 attached_phy_id;
	u8 sas_transport_attached;
	u8 pend_sas_rphy_add;
	struct mpi3mr_hba_port *hba_port;
	struct sas_rphy *rphy;
};

/**
 * struct tgt_dev_pcie - PCIe device specific information cached
 * from firmware given data
 *
 * @mdts: Maximum data transfer size
 * @capb: Device capabilities
 * @pgsz: Device page size
 * @abort_to: Timeout for abort TM
 * @reset_to: Timeout for Target/LUN reset TM
 * @dev_info: Device information bits
 */
struct tgt_dev_pcie {
	u32 mdts;
	u16 capb;
	u8 pgsz;
	u8 abort_to;
	u8 reset_to;
	u16 dev_info;
};

/**
 * struct tgt_dev_vd - virtual device specific information
 * cached from firmware given data
 *
 * @state: State of the VD
 * @tg_qd_reduction: Queue Depth reduction in units of 10%
 * @tg_id: VDs throttle group ID
 * @high: High limit to turn on throttling in 512 byte blocks
 * @low: Low limit to turn off throttling in 512 byte blocks
 * @tg: Pointer to throttle group info
 */
struct tgt_dev_vd {
	u8 state;
	u8 tg_qd_reduction;
	u16 tg_id;
	u32 tg_high;
	u32 tg_low;
	struct mpi3mr_throttle_group_info *tg;
};


/**
 * union _form_spec_inf - union of device specific information
 */
union _form_spec_inf {
	struct tgt_dev_sas_sata sas_sata_inf;
	struct tgt_dev_pcie pcie_inf;
	struct tgt_dev_vd vd_inf;
};

enum mpi3mr_dev_state {
	MPI3MR_DEV_CREATED = 1,
	MPI3MR_DEV_REMOVE_HS_STARTED = 2,
	MPI3MR_DEV_DELETED = 3,
};

/**
 * struct mpi3mr_tgt_dev - target device data structure
 *
 * @list: List pointer
 * @starget: Scsi_target pointer
 * @dev_handle: FW device handle
 * @parent_handle: FW parent device handle
 * @slot: Slot number
 * @encl_handle: FW enclosure handle
 * @perst_id: FW assigned Persistent ID
 * @devpg0_flag: Device Page0 flag
 * @dev_type: SAS/SATA/PCIE device type
 * @is_hidden: Should be exposed to upper layers or not
 * @host_exposed: Already exposed to host or not
 * @io_unit_port: IO Unit port ID
 * @non_stl: Is this device not to be attached with SAS TL
 * @io_throttle_enabled: I/O throttling needed or not
 * @wslen: Write same max length
 * @q_depth: Device specific Queue Depth
 * @wwid: World wide ID
 * @enclosure_logical_id: Enclosure logical identifier
 * @dev_spec: Device type specific information
 * @ref_count: Reference count
 * @state: device state
 */
struct mpi3mr_tgt_dev {
	struct list_head list;
	struct scsi_target *starget;
	u16 dev_handle;
	u16 parent_handle;
	u16 slot;
	u16 encl_handle;
	u16 perst_id;
	u16 devpg0_flag;
	u8 dev_type;
	u8 is_hidden;
	u8 host_exposed;
	u8 io_unit_port;
	u8 non_stl;
	u8 io_throttle_enabled;
	u16 wslen;
	u16 q_depth;
	u64 wwid;
	u64 enclosure_logical_id;
	union _form_spec_inf dev_spec;
	struct kref ref_count;
	enum mpi3mr_dev_state state;
};

/**
 * mpi3mr_tgtdev_get - k reference incrementor
 * @s: Target device reference
 *
 * Increment target device reference count.
 */
static inline void mpi3mr_tgtdev_get(struct mpi3mr_tgt_dev *s)
{
	kref_get(&s->ref_count);
}

/**
 * mpi3mr_free_tgtdev - target device memory dealloctor
 * @r: k reference pointer of the target device
 *
 * Free target device memory when no reference.
 */
static inline void mpi3mr_free_tgtdev(struct kref *r)
{
	kfree(container_of(r, struct mpi3mr_tgt_dev, ref_count));
}

/**
 * mpi3mr_tgtdev_put - k reference decrementor
 * @s: Target device reference
 *
 * Decrement target device reference count.
 */
static inline void mpi3mr_tgtdev_put(struct mpi3mr_tgt_dev *s)
{
	kref_put(&s->ref_count, mpi3mr_free_tgtdev);
}


/**
 * struct mpi3mr_stgt_priv_data - SCSI target private structure
 *
 * @starget: Scsi_target pointer
 * @dev_handle: FW device handle
 * @perst_id: FW assigned Persistent ID
 * @num_luns: Number of Logical Units
 * @block_io: I/O blocked to the device or not
 * @dev_removed: Device removed in the Firmware
 * @dev_removedelay: Device is waiting to be removed in FW
 * @dev_type: Device type
 * @dev_nvme_dif: Device is NVMe DIF enabled
 * @wslen: Write same max length
 * @io_throttle_enabled: I/O throttling needed or not
 * @io_divert: Flag indicates io divert is on or off for the dev
 * @throttle_group: Pointer to throttle group info
 * @tgt_dev: Internal target device pointer
 * @pend_count: Counter to track pending I/Os during error
 *		handling
 */
struct mpi3mr_stgt_priv_data {
	struct scsi_target *starget;
	u16 dev_handle;
	u16 perst_id;
	u32 num_luns;
	atomic_t block_io;
	u8 dev_removed;
	u8 dev_removedelay;
	u8 dev_type;
	u8 dev_nvme_dif;
	u16 wslen;
	u8 io_throttle_enabled;
	u8 io_divert;
	struct mpi3mr_throttle_group_info *throttle_group;
	struct mpi3mr_tgt_dev *tgt_dev;
	u32 pend_count;
};

/**
 * struct mpi3mr_stgt_priv_data - SCSI device private structure
 *
 * @tgt_priv_data: Scsi_target private data pointer
 * @lun_id: LUN ID of the device
 * @ncq_prio_enable: NCQ priority enable for SATA device
 * @pend_count: Counter to track pending I/Os during error
 *		handling
 * @wslen: Write same max length
 */
struct mpi3mr_sdev_priv_data {
	struct mpi3mr_stgt_priv_data *tgt_priv_data;
	u32 lun_id;
	u8 ncq_prio_enable;
	u32 pend_count;
	u16 wslen;
};

/**
 * struct mpi3mr_drv_cmd - Internal command tracker
 *
 * @mutex: Command mutex
 * @done: Completeor for wakeup
 * @reply: Firmware reply for internal commands
 * @sensebuf: Sensebuf for SCSI IO commands
 * @iou_rc: IO Unit control reason code
 * @state: Command State
 * @dev_handle: Firmware handle for device specific commands
 * @ioc_status: IOC status from the firmware
 * @ioc_loginfo:IOC log info from the firmware
 * @is_waiting: Is the command issued in block mode
 * @is_sense: Is Sense data present
 * @retry_count: Retry count for retriable commands
 * @host_tag: Host tag used by the command
 * @callback: Callback for non blocking commands
 */
struct mpi3mr_drv_cmd {
	struct mutex mutex;
	struct completion done;
	void *reply;
	u8 *sensebuf;
	u8 iou_rc;
	u16 state;
	u16 dev_handle;
	u16 ioc_status;
	u32 ioc_loginfo;
	u8 is_waiting;
	u8 is_sense;
	u8 retry_count;
	u16 host_tag;

	void (*callback)(struct mpi3mr_ioc *mrioc,
	    struct mpi3mr_drv_cmd *drv_cmd);
};

/**
 * union mpi3mr_trigger_data - Trigger data information
 * @fault: Fault code
 * @global: Global trigger data
 * @element: element trigger data
 */
union mpi3mr_trigger_data {
	u16 fault;
	u64 global;
	union mpi3_driver2_trigger_element element;
};

/**
 * struct trigger_event_data - store trigger related
 * information.
 *
 * @trace_hdb: Trace diag buffer descriptor reference
 * @fw_hdb: FW diag buffer descriptor reference
 * @trigger_type: Trigger type
 * @trigger_specific_data: Trigger specific data
 * @snapdump: Snapdump enable or disable flag
 */
struct trigger_event_data {
	struct diag_buffer_desc *trace_hdb;
	struct diag_buffer_desc *fw_hdb;
	u8 trigger_type;
	union mpi3mr_trigger_data trigger_specific_data;
	bool snapdump;
};

/**
 * struct diag_buffer_desc - memory descriptor structure to
 * store virtual, dma addresses, size, buffer status for host
 * diagnostic buffers.
 *
 * @type: Buffer type
 * @trigger_data: Trigger data
 * @trigger_type: Trigger type
 * @status: Buffer status
 * @size: Buffer size
 * @addr: Virtual address
 * @dma_addr: Buffer DMA address
 */
struct diag_buffer_desc {
	u8 type;
	union mpi3mr_trigger_data trigger_data;
	u8 trigger_type;
	u8 status;
	u32 size;
	void *addr;
	dma_addr_t dma_addr;
};

/**
 * struct dma_memory_desc - memory descriptor structure to store
 * virtual address, dma address and size for any generic dma
 * memory allocations in the driver.
 *
 * @size: buffer size
 * @addr: virtual address
 * @dma_addr: dma address
 */
struct dma_memory_desc {
	u32 size;
	void *addr;
	dma_addr_t dma_addr;
};


/**
 * struct chain_element - memory descriptor structure to store
 * virtual and dma addresses for chain elements.
 *
 * @addr: virtual address
 * @dma_addr: dma address
 */
struct chain_element {
	void *addr;
	dma_addr_t dma_addr;
};

/**
 * struct scmd_priv - SCSI command private data
 *
 * @host_tag: Host tag specific to operational queue
 * @in_lld_scope: Command in LLD scope or not
 * @meta_sg_valid: DIX command with meta data SGL or not
 * @scmd: SCSI Command pointer
 * @req_q_idx: Operational request queue index
 * @chain_idx: Chain frame index
 * @meta_chain_idx: Chain frame index of meta data SGL
 * @mpi3mr_scsiio_req: MPI SCSI IO request
 */
struct scmd_priv {
	u16 host_tag;
	u8 in_lld_scope;
	u8 meta_sg_valid;
	struct scsi_cmnd *scmd;
	u16 req_q_idx;
	int chain_idx;
	int meta_chain_idx;
	u8 mpi3mr_scsiio_req[MPI3MR_ADMIN_REQ_FRAME_SZ];
};

/**
 * struct mpi3mr_ioc - Adapter anchor structure stored in shost
 * private data
 *
 * @list: List pointer
 * @pdev: PCI device pointer
 * @shost: Scsi_Host pointer
 * @id: Controller ID
 * @cpu_count: Number of online CPUs
 * @irqpoll_sleep: usleep unit used in threaded isr irqpoll
 * @name: Controller ASCII name
 * @driver_name: Driver ASCII name
 * @sysif_regs: System interface registers virtual address
 * @sysif_regs_phys: System interface registers physical address
 * @bars: PCI BARS
 * @dma_mask: DMA mask
 * @msix_count: Number of MSIX vectors used
 * @intr_enabled: Is interrupts enabled
 * @num_admin_req: Number of admin requests
 * @admin_req_q_sz: Admin request queue size
 * @admin_req_pi: Admin request queue producer index
 * @admin_req_ci: Admin request queue consumer index
 * @admin_req_base: Admin request queue base virtual address
 * @admin_req_dma: Admin request queue base dma address
 * @admin_req_lock: Admin queue access lock
 * @num_admin_replies: Number of admin replies
 * @admin_reply_q_sz: Admin reply queue size
 * @admin_reply_ci: Admin reply queue consumer index
 * @admin_reply_ephase:Admin reply queue expected phase
 * @admin_reply_base: Admin reply queue base virtual address
 * @admin_reply_dma: Admin reply queue base dma address
 * @admin_reply_q_in_use: Queue is handled by poll/ISR
 * @ready_timeout: Controller ready timeout
 * @intr_info: Interrupt cookie pointer
 * @intr_info_count: Number of interrupt cookies
 * @is_intr_info_set: Flag to indicate intr info is setup
 * @num_queues: Number of operational queues
 * @num_op_req_q: Number of operational request queues
 * @req_qinfo: Operational request queue info pointer
 * @num_op_reply_q: Number of operational reply queues
 * @op_reply_qinfo: Operational reply queue info pointer
 * @init_cmds: Command tracker for initialization commands
 * @cfg_cmds: Command tracker for configuration requests
 * @facts: Cached IOC facts data
 * @op_reply_desc_sz: Operational reply descriptor size
 * @num_reply_bufs: Number of reply buffers allocated
 * @reply_buf_pool: Reply buffer pool
 * @reply_buf: Reply buffer base virtual address
 * @reply_buf_dma: Reply buffer DMA address
 * @reply_buf_dma_max_address: Reply DMA address max limit
 * @reply_free_qsz: Reply free queue size
 * @reply_free_q_pool: Reply free queue pool
 * @reply_free_q: Reply free queue base virtual address
 * @reply_free_q_dma: Reply free queue base DMA address
 * @reply_free_queue_lock: Reply free queue lock
 * @reply_free_queue_host_index: Reply free queue host index
 * @num_sense_bufs: Number of sense buffers
 * @sense_buf_pool: Sense buffer pool
 * @sense_buf: Sense buffer base virtual address
 * @sense_buf_dma: Sense buffer base DMA address
 * @sense_buf_q_sz: Sense buffer queue size
 * @sense_buf_q_pool: Sense buffer queue pool
 * @sense_buf_q: Sense buffer queue virtual address
 * @sense_buf_q_dma: Sense buffer queue DMA address
 * @sbq_lock: Sense buffer queue lock
 * @sbq_host_index: Sense buffer queuehost index
 * @event_masks: Event mask bitmap
 * @fwevt_worker_thread: Firmware event worker thread
 * @fwevt_lock: Firmware event lock
 * @fwevt_list: Firmware event list
 * @watchdog_work_q_name: Fault watchdog worker thread name
 * @watchdog_work_q: Fault watchdog worker thread
 * @watchdog_work: Fault watchdog work
 * @watchdog_lock: Fault watchdog lock
 * @is_driver_loading: Is driver still loading
 * @scan_started: Async scan started
 * @scan_failed: Asycn scan failed
 * @stop_drv_processing: Stop all command processing
 * @device_refresh_on: Don't process the events until devices are refreshed
 * @max_host_ios: Maximum host I/O count
 * @max_sgl_entries: Max SGL entries per I/O
 * @chain_buf_count: Chain buffer count
 * @chain_buf_pool: Chain buffer pool
 * @chain_sgl_list: Chain SGL list
 * @chain_bitmap: Chain buffer allocator bitmap
 * @chain_buf_lock: Chain buffer list lock
 * @bsg_cmds: Command tracker for BSG command
 * @host_tm_cmds: Command tracker for task management commands
 * @dev_rmhs_cmds: Command tracker for device removal commands
 * @evtack_cmds: Command tracker for event ack commands
 * @devrem_bitmap: Device removal bitmap
 * @dev_handle_bitmap_bits: Number of bits in device handle bitmap
 * @removepend_bitmap: Remove pending bitmap
 * @delayed_rmhs_list: Delayed device removal list
 * @evtack_cmds_bitmap: Event Ack bitmap
 * @delayed_evtack_cmds_list: Delayed event acknowledgment list
 * @ts_update_counter: Timestamp update counter
 * @ts_update_interval: Timestamp update interval
 * @reset_in_progress: Reset in progress flag
 * @unrecoverable: Controller unrecoverable flag
 * @prev_reset_result: Result of previous reset
 * @reset_mutex: Controller reset mutex
 * @reset_waitq: Controller reset  wait queue
 * @prepare_for_reset: Prepare for reset event received
 * @prepare_for_reset_timeout_counter: Prepare for reset timeout
 * @prp_list_virt: NVMe encapsulated PRP list virtual base
 * @prp_list_dma: NVMe encapsulated PRP list DMA
 * @prp_sz: NVME encapsulated PRP list size
 * @diagsave_timeout: Diagnostic information save timeout
 * @logging_level: Controller debug logging level
 * @flush_io_count: I/O count to flush after reset
 * @current_event: Firmware event currently in process
 * @driver_info: Driver, Kernel, OS information to firmware
 * @change_count: Topology change count
 * @pel_enabled: Persistent Event Log(PEL) enabled or not
 * @pel_abort_requested: PEL abort is requested or not
 * @pel_class: PEL Class identifier
 * @pel_locale: PEL Locale identifier
 * @pel_cmds: Command tracker for PEL wait command
 * @pel_abort_cmd: Command tracker for PEL abort command
 * @pel_newest_seqnum: Newest PEL sequenece number
 * @pel_seqnum_virt: PEL sequence number virtual address
 * @pel_seqnum_dma: PEL sequence number DMA address
 * @pel_seqnum_sz: PEL sequenece number size
 * @op_reply_q_offset: Operational reply queue offset with MSIx
 * @default_qcount: Total Default queues
 * @active_poll_qcount: Currently active poll queue count
 * @requested_poll_qcount: User requested poll queue count
 * @bsg_dev: BSG device structure
 * @bsg_queue: Request queue for BSG device
 * @stop_bsgs: Stop BSG request flag
 * @logdata_buf: Circular buffer to store log data entries
 * @logdata_buf_idx: Index of entry in buffer to store
 * @logdata_entry_sz: log data entry size
 * @pend_large_data_sz: Counter to track pending large data
 * @io_throttle_data_length: I/O size to track in 512b blocks
 * @io_throttle_high: I/O size to start throttle in 512b blocks
 * @io_throttle_low: I/O size to stop throttle in 512b blocks
 * @num_io_throttle_group: Maximum number of throttle groups
 * @throttle_groups: Pointer to throttle group info structures
 * @sas_transport_enabled: SAS transport enabled or not
 * @scsi_device_channel: Channel ID for SCSI devices
 * @transport_cmds: Command tracker for SAS transport commands
 * @sas_hba: SAS node for the controller
 * @sas_expander_list: SAS node list of expanders
 * @sas_node_lock: Lock to protect SAS node list
 * @hba_port_table_list: List of HBA Ports
 * @enclosure_list: List of Enclosure objects
 * @diag_buffers: Host diagnostic buffers
 * @driver_pg2:  Driver page 2 pointer
 * @reply_trigger_present: Reply trigger present flag
 * @event_trigger_present: Event trigger present flag
 * @scsisense_trigger_present: Scsi sense trigger present flag
 * @ioctl_dma_pool: DMA pool for IOCTL data buffers
 * @ioctl_sge: DMA buffer descriptors for IOCTL data
 * @ioctl_chain_sge: DMA buffer descriptor for IOCTL chain
 * @ioctl_resp_sge: DMA buffer descriptor for Mgmt cmd response
 * @ioctl_sges_allocated: Flag for IOCTL SGEs allocated or not
 * @trace_release_trigger_active: Trace trigger active flag
 * @fw_release_trigger_active: Fw release trigger active flag
 * @snapdump_trigger_active: Snapdump trigger active flag
 * @pci_err_recovery: PCI error recovery in progress
 * @block_on_pci_err: Block IO during PCI error recovery
 */
struct mpi3mr_ioc {
	struct list_head list;
	struct pci_dev *pdev;
	struct Scsi_Host *shost;
	u8 id;
	int cpu_count;
	bool enable_segqueue;
	u32 irqpoll_sleep;

	char name[MPI3MR_NAME_LENGTH];
	char driver_name[MPI3MR_NAME_LENGTH];

	volatile struct mpi3_sysif_registers __iomem *sysif_regs;
	resource_size_t sysif_regs_phys;
	int bars;
	u64 dma_mask;

	u16 msix_count;
	u8 intr_enabled;

	u16 num_admin_req;
	u32 admin_req_q_sz;
	u16 admin_req_pi;
	u16 admin_req_ci;
	void *admin_req_base;
	dma_addr_t admin_req_dma;
	spinlock_t admin_req_lock;

	u16 num_admin_replies;
	u32 admin_reply_q_sz;
	u16 admin_reply_ci;
	u8 admin_reply_ephase;
	void *admin_reply_base;
	dma_addr_t admin_reply_dma;
	atomic_t admin_reply_q_in_use;

	u32 ready_timeout;

	struct mpi3mr_intr_info *intr_info;
	u16 intr_info_count;
	bool is_intr_info_set;

	u16 num_queues;
	u16 num_op_req_q;
	struct op_req_qinfo *req_qinfo;

	u16 num_op_reply_q;
	struct op_reply_qinfo *op_reply_qinfo;

	struct mpi3mr_drv_cmd init_cmds;
	struct mpi3mr_drv_cmd cfg_cmds;
	struct mpi3mr_ioc_facts facts;
	u16 op_reply_desc_sz;

	u32 num_reply_bufs;
	struct dma_pool *reply_buf_pool;
	u8 *reply_buf;
	dma_addr_t reply_buf_dma;
	dma_addr_t reply_buf_dma_max_address;

	u16 reply_free_qsz;
	u16 reply_sz;
	struct dma_pool *reply_free_q_pool;
	__le64 *reply_free_q;
	dma_addr_t reply_free_q_dma;
	spinlock_t reply_free_queue_lock;
	u32 reply_free_queue_host_index;

	u32 num_sense_bufs;
	struct dma_pool *sense_buf_pool;
	u8 *sense_buf;
	dma_addr_t sense_buf_dma;

	u16 sense_buf_q_sz;
	struct dma_pool *sense_buf_q_pool;
	__le64 *sense_buf_q;
	dma_addr_t sense_buf_q_dma;
	spinlock_t sbq_lock;
	u32 sbq_host_index;
	u32 event_masks[MPI3_EVENT_NOTIFY_EVENTMASK_WORDS];

	struct workqueue_struct	*fwevt_worker_thread;
	spinlock_t fwevt_lock;
	struct list_head fwevt_list;

	char watchdog_work_q_name[50];
	struct workqueue_struct *watchdog_work_q;
	struct delayed_work watchdog_work;
	spinlock_t watchdog_lock;

	u8 is_driver_loading;
	u8 scan_started;
	u16 scan_failed;
	u8 stop_drv_processing;
	u8 device_refresh_on;

	u16 max_host_ios;
	spinlock_t tgtdev_lock;
	struct list_head tgtdev_list;
	u16 max_sgl_entries;

	u32 chain_buf_count;
	struct dma_pool *chain_buf_pool;
	struct chain_element *chain_sgl_list;
	unsigned long *chain_bitmap;
	spinlock_t chain_buf_lock;

	struct mpi3mr_drv_cmd bsg_cmds;
	struct mpi3mr_drv_cmd host_tm_cmds;
	struct mpi3mr_drv_cmd dev_rmhs_cmds[MPI3MR_NUM_DEVRMCMD];
	struct mpi3mr_drv_cmd evtack_cmds[MPI3MR_NUM_EVTACKCMD];
	unsigned long *devrem_bitmap;
	u16 dev_handle_bitmap_bits;
	unsigned long *removepend_bitmap;
	struct list_head delayed_rmhs_list;
	unsigned long *evtack_cmds_bitmap;
	struct list_head delayed_evtack_cmds_list;

	u16 ts_update_counter;
	u16 ts_update_interval;
	u8 reset_in_progress;
	u8 unrecoverable;
	int prev_reset_result;
	struct mutex reset_mutex;
	wait_queue_head_t reset_waitq;

	u8 prepare_for_reset;
	u16 prepare_for_reset_timeout_counter;

	void *prp_list_virt;
	dma_addr_t prp_list_dma;
	u32 prp_sz;

	u16 diagsave_timeout;
	int logging_level;
	u16 flush_io_count;

	struct mpi3mr_fwevt *current_event;
	struct mpi3_driver_info_layout driver_info;
	u16 change_count;

	u8 pel_enabled;
	u8 pel_abort_requested;
	u8 pel_class;
	u16 pel_locale;
	struct mpi3mr_drv_cmd pel_cmds;
	struct mpi3mr_drv_cmd pel_abort_cmd;

	u32 pel_newest_seqnum;
	void *pel_seqnum_virt;
	dma_addr_t pel_seqnum_dma;
	u32 pel_seqnum_sz;

	u16 op_reply_q_offset;
	u16 default_qcount;
	u16 active_poll_qcount;
	u16 requested_poll_qcount;

	struct device bsg_dev;
	struct request_queue *bsg_queue;
	u8 stop_bsgs;
	u8 *logdata_buf;
	u16 logdata_buf_idx;
	u16 logdata_entry_sz;

	atomic_t pend_large_data_sz;
	u32 io_throttle_data_length;
	u32 io_throttle_high;
	u32 io_throttle_low;
	u16 num_io_throttle_group;
	struct mpi3mr_throttle_group_info *throttle_groups;

	u8 sas_transport_enabled;
	u8 scsi_device_channel;
	struct mpi3mr_drv_cmd transport_cmds;
	struct mpi3mr_sas_node sas_hba;
	struct list_head sas_expander_list;
	spinlock_t sas_node_lock;
	struct list_head hba_port_table_list;
	struct list_head enclosure_list;

	struct dma_pool *ioctl_dma_pool;
	struct dma_memory_desc ioctl_sge[MPI3MR_NUM_IOCTL_SGE];
	struct dma_memory_desc ioctl_chain_sge;
	struct dma_memory_desc ioctl_resp_sge;
	bool ioctl_sges_allocated;
	bool reply_trigger_present;
	bool event_trigger_present;
	bool scsisense_trigger_present;
	struct diag_buffer_desc diag_buffers[MPI3MR_MAX_NUM_HDB];
	struct mpi3_driver_page2 *driver_pg2;
	spinlock_t trigger_lock;
	bool snapdump_trigger_active;
	bool trace_release_trigger_active;
	bool fw_release_trigger_active;
	bool pci_err_recovery;
	bool block_on_pci_err;
};

/**
 * struct mpi3mr_fwevt - Firmware event structure.
 *
 * @list: list head
 * @work: Work structure
 * @mrioc: Adapter instance reference
 * @event_id: MPI3 firmware event ID
 * @send_ack: Event acknowledgment required or not
 * @process_evt: Bottomhalf processing required or not
 * @evt_ctx: Event context to send in Ack
 * @event_data_size: size of the event data in bytes
 * @pending_at_sml: waiting for device add/remove API to complete
 * @discard: discard this event
 * @ref_count: kref count
 * @event_data: Actual MPI3 event data
 */
struct mpi3mr_fwevt {
	struct list_head list;
	struct work_struct work;
	struct mpi3mr_ioc *mrioc;
	u16 event_id;
	bool send_ack;
	bool process_evt;
	u32 evt_ctx;
	u16 event_data_size;
	bool pending_at_sml;
	bool discard;
	struct kref ref_count;
	char event_data[] __aligned(4);
};


/**
 * struct delayed_dev_rmhs_node - Delayed device removal node
 *
 * @list: list head
 * @handle: Device handle
 * @iou_rc: IO Unit Control Reason Code
 */
struct delayed_dev_rmhs_node {
	struct list_head list;
	u16 handle;
	u8 iou_rc;
};

/**
 * struct delayed_evt_ack_node - Delayed event ack node
 * @list: list head
 * @event: MPI3 event ID
 * @event_ctx: event context
 */
struct delayed_evt_ack_node {
	struct list_head list;
	u8 event;
	u32 event_ctx;
};

int mpi3mr_setup_resources(struct mpi3mr_ioc *mrioc);
void mpi3mr_cleanup_resources(struct mpi3mr_ioc *mrioc);
int mpi3mr_init_ioc(struct mpi3mr_ioc *mrioc);
int mpi3mr_reinit_ioc(struct mpi3mr_ioc *mrioc, u8 is_resume);
void mpi3mr_cleanup_ioc(struct mpi3mr_ioc *mrioc);
int mpi3mr_issue_port_enable(struct mpi3mr_ioc *mrioc, u8 async);
int mpi3mr_admin_request_post(struct mpi3mr_ioc *mrioc, void *admin_req,
u16 admin_req_sz, u8 ignore_reset);
int mpi3mr_op_request_post(struct mpi3mr_ioc *mrioc,
			   struct op_req_qinfo *opreqq, u8 *req);
void mpi3mr_add_sg_single(void *paddr, u8 flags, u32 length,
			  dma_addr_t dma_addr);
void mpi3mr_build_zero_len_sge(void *paddr);
void *mpi3mr_get_sensebuf_virt_addr(struct mpi3mr_ioc *mrioc,
				     dma_addr_t phys_addr);
void *mpi3mr_get_reply_virt_addr(struct mpi3mr_ioc *mrioc,
				     dma_addr_t phys_addr);
void mpi3mr_repost_sense_buf(struct mpi3mr_ioc *mrioc,
				     u64 sense_buf_dma);

void mpi3mr_memset_buffers(struct mpi3mr_ioc *mrioc);
void mpi3mr_free_mem(struct mpi3mr_ioc *mrioc);
void mpi3mr_os_handle_events(struct mpi3mr_ioc *mrioc,
			     struct mpi3_event_notification_reply *event_reply);
void mpi3mr_process_op_reply_desc(struct mpi3mr_ioc *mrioc,
				  struct mpi3_default_reply_descriptor *reply_desc,
				  u64 *reply_dma, u16 qidx);
void mpi3mr_start_watchdog(struct mpi3mr_ioc *mrioc);
void mpi3mr_stop_watchdog(struct mpi3mr_ioc *mrioc);

int mpi3mr_soft_reset_handler(struct mpi3mr_ioc *mrioc,
			      u16 reset_reason, u8 snapdump);
void mpi3mr_ioc_disable_intr(struct mpi3mr_ioc *mrioc);
void mpi3mr_ioc_enable_intr(struct mpi3mr_ioc *mrioc);

enum mpi3mr_iocstate mpi3mr_get_iocstate(struct mpi3mr_ioc *mrioc);
int mpi3mr_process_event_ack(struct mpi3mr_ioc *mrioc, u8 event,
			  u32 event_ctx);

void mpi3mr_wait_for_host_io(struct mpi3mr_ioc *mrioc, u32 timeout);
void mpi3mr_cleanup_fwevt_list(struct mpi3mr_ioc *mrioc);
void mpi3mr_flush_host_io(struct mpi3mr_ioc *mrioc);
void mpi3mr_invalidate_devhandles(struct mpi3mr_ioc *mrioc);
void mpi3mr_flush_delayed_cmd_lists(struct mpi3mr_ioc *mrioc);
void mpi3mr_check_rh_fault_ioc(struct mpi3mr_ioc *mrioc, u32 reason_code);
void mpi3mr_print_fault_info(struct mpi3mr_ioc *mrioc);
void mpi3mr_check_rh_fault_ioc(struct mpi3mr_ioc *mrioc, u32 reason_code);
int mpi3mr_process_op_reply_q(struct mpi3mr_ioc *mrioc,
	struct op_reply_qinfo *op_reply_q);
int mpi3mr_blk_mq_poll(struct Scsi_Host *shost, unsigned int queue_num);
void mpi3mr_bsg_init(struct mpi3mr_ioc *mrioc);
void mpi3mr_bsg_exit(struct mpi3mr_ioc *mrioc);
int mpi3mr_issue_tm(struct mpi3mr_ioc *mrioc, u8 tm_type,
	u16 handle, uint lun, u16 htag, ulong timeout,
	struct mpi3mr_drv_cmd *drv_cmd,
	u8 *resp_code, struct scsi_cmnd *scmd);
struct mpi3mr_tgt_dev *mpi3mr_get_tgtdev_by_handle(
	struct mpi3mr_ioc *mrioc, u16 handle);
void mpi3mr_pel_get_seqnum_complete(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_drv_cmd *drv_cmd);
int mpi3mr_pel_get_seqnum_post(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_drv_cmd *drv_cmd);
void mpi3mr_app_save_logdata(struct mpi3mr_ioc *mrioc, char *event_data,
	u16 event_data_size);
struct mpi3mr_enclosure_node *mpi3mr_enclosure_find_by_handle(
	struct mpi3mr_ioc *mrioc, u16 handle);
extern const struct attribute_group *mpi3mr_host_groups[];
extern const struct attribute_group *mpi3mr_dev_groups[];

extern struct sas_function_template mpi3mr_transport_functions;
extern struct scsi_transport_template *mpi3mr_transport_template;

int mpi3mr_cfg_get_dev_pg0(struct mpi3mr_ioc *mrioc, u16 *ioc_status,
	struct mpi3_device_page0 *dev_pg0, u16 pg_sz, u32 form, u32 form_spec);
int mpi3mr_cfg_get_sas_phy_pg0(struct mpi3mr_ioc *mrioc, u16 *ioc_status,
	struct mpi3_sas_phy_page0 *phy_pg0, u16 pg_sz, u32 form,
	u32 form_spec);
int mpi3mr_cfg_get_sas_phy_pg1(struct mpi3mr_ioc *mrioc, u16 *ioc_status,
	struct mpi3_sas_phy_page1 *phy_pg1, u16 pg_sz, u32 form,
	u32 form_spec);
int mpi3mr_cfg_get_sas_exp_pg0(struct mpi3mr_ioc *mrioc, u16 *ioc_status,
	struct mpi3_sas_expander_page0 *exp_pg0, u16 pg_sz, u32 form,
	u32 form_spec);
int mpi3mr_cfg_get_sas_exp_pg1(struct mpi3mr_ioc *mrioc, u16 *ioc_status,
	struct mpi3_sas_expander_page1 *exp_pg1, u16 pg_sz, u32 form,
	u32 form_spec);
int mpi3mr_cfg_get_enclosure_pg0(struct mpi3mr_ioc *mrioc, u16 *ioc_status,
	struct mpi3_enclosure_page0 *encl_pg0, u16 pg_sz, u32 form,
	u32 form_spec);
int mpi3mr_cfg_get_sas_io_unit_pg0(struct mpi3mr_ioc *mrioc,
	struct mpi3_sas_io_unit_page0 *sas_io_unit_pg0, u16 pg_sz);
int mpi3mr_cfg_get_sas_io_unit_pg1(struct mpi3mr_ioc *mrioc,
	struct mpi3_sas_io_unit_page1 *sas_io_unit_pg1, u16 pg_sz);
int mpi3mr_cfg_set_sas_io_unit_pg1(struct mpi3mr_ioc *mrioc,
	struct mpi3_sas_io_unit_page1 *sas_io_unit_pg1, u16 pg_sz);
int mpi3mr_cfg_get_driver_pg1(struct mpi3mr_ioc *mrioc,
	struct mpi3_driver_page1 *driver_pg1, u16 pg_sz);
int mpi3mr_cfg_get_driver_pg2(struct mpi3mr_ioc *mrioc,
	struct mpi3_driver_page2 *driver_pg2, u16 pg_sz, u8 page_type);

u8 mpi3mr_is_expander_device(u16 device_info);
int mpi3mr_expander_add(struct mpi3mr_ioc *mrioc, u16 handle);
void mpi3mr_expander_remove(struct mpi3mr_ioc *mrioc, u64 sas_address,
	struct mpi3mr_hba_port *hba_port);
struct mpi3mr_sas_node *__mpi3mr_expander_find_by_handle(struct mpi3mr_ioc
	*mrioc, u16 handle);
struct mpi3mr_hba_port *mpi3mr_get_hba_port_by_id(struct mpi3mr_ioc *mrioc,
	u8 port_id);
void mpi3mr_sas_host_refresh(struct mpi3mr_ioc *mrioc);
void mpi3mr_sas_host_add(struct mpi3mr_ioc *mrioc);
void mpi3mr_update_links(struct mpi3mr_ioc *mrioc,
	u64 sas_address_parent, u16 handle, u8 phy_number, u8 link_rate,
	struct mpi3mr_hba_port *hba_port);
void mpi3mr_remove_tgtdev_from_host(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_tgt_dev *tgtdev);
int mpi3mr_report_tgtdev_to_sas_transport(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_tgt_dev *tgtdev);
void mpi3mr_remove_tgtdev_from_sas_transport(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_tgt_dev *tgtdev);
struct mpi3mr_tgt_dev *__mpi3mr_get_tgtdev_by_addr_and_rphy(
	struct mpi3mr_ioc *mrioc, u64 sas_address, struct sas_rphy *rphy);
void mpi3mr_print_device_event_notice(struct mpi3mr_ioc *mrioc,
	bool device_add);
void mpi3mr_refresh_sas_ports(struct mpi3mr_ioc *mrioc);
void mpi3mr_refresh_expanders(struct mpi3mr_ioc *mrioc);
void mpi3mr_add_event_wait_for_device_refresh(struct mpi3mr_ioc *mrioc);
void mpi3mr_flush_drv_cmds(struct mpi3mr_ioc *mrioc);
void mpi3mr_flush_cmds_for_unrecovered_controller(struct mpi3mr_ioc *mrioc);
void mpi3mr_free_enclosure_list(struct mpi3mr_ioc *mrioc);
int mpi3mr_process_admin_reply_q(struct mpi3mr_ioc *mrioc);
void mpi3mr_expander_node_remove(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_sas_node *sas_expander);
void mpi3mr_alloc_diag_bufs(struct mpi3mr_ioc *mrioc);
int mpi3mr_post_diag_bufs(struct mpi3mr_ioc *mrioc);
int mpi3mr_issue_diag_buf_release(struct mpi3mr_ioc *mrioc,
	struct diag_buffer_desc *diag_buffer);
void mpi3mr_release_diag_bufs(struct mpi3mr_ioc *mrioc, u8 skip_rel_action);
void mpi3mr_set_trigger_data_in_hdb(struct diag_buffer_desc *hdb,
	u8 type, union mpi3mr_trigger_data *trigger_data, bool force);
int mpi3mr_refresh_trigger(struct mpi3mr_ioc *mrioc, u8 page_type);
struct diag_buffer_desc *mpi3mr_diag_buffer_for_type(struct mpi3mr_ioc *mrioc,
	u8 buf_type);
int mpi3mr_issue_diag_buf_post(struct mpi3mr_ioc *mrioc,
	struct diag_buffer_desc *diag_buffer);
void mpi3mr_set_trigger_data_in_all_hdb(struct mpi3mr_ioc *mrioc,
	u8 type, union mpi3mr_trigger_data *trigger_data, bool force);
void mpi3mr_reply_trigger(struct mpi3mr_ioc *mrioc, u16 iocstatus,
	u32 iocloginfo);
void mpi3mr_hdb_trigger_data_event(struct mpi3mr_ioc *mrioc,
	struct trigger_event_data *event_data);
void mpi3mr_scsisense_trigger(struct mpi3mr_ioc *mrioc, u8 senseky, u8 asc,
	u8 ascq);
void mpi3mr_event_trigger(struct mpi3mr_ioc *mrioc, u8 event);
void mpi3mr_global_trigger(struct mpi3mr_ioc *mrioc, u64 trigger_data);
void mpi3mr_hdbstatuschg_evt_th(struct mpi3mr_ioc *mrioc,
	struct mpi3_event_notification_reply *event_reply);
#endif /*MPI3MR_H_INCLUDED*/
