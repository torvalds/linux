/*
 *    Disk Array driver for HP Smart Array SAS controllers
 *    Copyright 2016 Microsemi Corporation
 *    Copyright 2014-2015 PMC-Sierra, Inc.
 *    Copyright 2000,2009-2015 Hewlett-Packard Development Company, L.P.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; version 2 of the License.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    Questions/Comments/Bugfixes to esc.storagedev@microsemi.com
 *
 */
#ifndef HPSA_H
#define HPSA_H

#include <scsi/scsicam.h>

#define IO_OK		0
#define IO_ERROR	1

struct ctlr_info;

struct access_method {
	void (*submit_command)(struct ctlr_info *h,
		struct CommandList *c);
	void (*set_intr_mask)(struct ctlr_info *h, unsigned long val);
	bool (*intr_pending)(struct ctlr_info *h);
	unsigned long (*command_completed)(struct ctlr_info *h, u8 q);
};

/* for SAS hosts and SAS expanders */
struct hpsa_sas_node {
	struct device *parent_dev;
	struct list_head port_list_head;
};

struct hpsa_sas_port {
	struct list_head port_list_entry;
	u64 sas_address;
	struct sas_port *port;
	int next_phy_index;
	struct list_head phy_list_head;
	struct hpsa_sas_node *parent_node;
	struct sas_rphy *rphy;
};

struct hpsa_sas_phy {
	struct list_head phy_list_entry;
	struct sas_phy *phy;
	struct hpsa_sas_port *parent_port;
	bool added_to_port;
};

struct hpsa_scsi_dev_t {
	unsigned int devtype;
	int bus, target, lun;		/* as presented to the OS */
	unsigned char scsi3addr[8];	/* as presented to the HW */
	u8 physical_device : 1;
	u8 expose_device;
	u8 removed : 1;			/* device is marked for death */
#define RAID_CTLR_LUNID "\0\0\0\0\0\0\0\0"
	unsigned char device_id[16];    /* from inquiry pg. 0x83 */
	u64 sas_address;
	unsigned char vendor[8];        /* bytes 8-15 of inquiry data */
	unsigned char model[16];        /* bytes 16-31 of inquiry data */
	unsigned char rev;		/* byte 2 of inquiry data */
	unsigned char raid_level;	/* from inquiry page 0xC1 */
	unsigned char volume_offline;	/* discovered via TUR or VPD */
	u16 queue_depth;		/* max queue_depth for this device */
	atomic_t reset_cmds_out;	/* Count of commands to-be affected */
	atomic_t ioaccel_cmds_out;	/* Only used for physical devices
					 * counts commands sent to physical
					 * device via "ioaccel" path.
					 */
	u32 ioaccel_handle;
	u8 active_path_index;
	u8 path_map;
	u8 bay;
	u8 box[8];
	u16 phys_connector[8];
	int offload_config;		/* I/O accel RAID offload configured */
	int offload_enabled;		/* I/O accel RAID offload enabled */
	int offload_to_be_enabled;
	int hba_ioaccel_enabled;
	int offload_to_mirror;		/* Send next I/O accelerator RAID
					 * offload request to mirror drive
					 */
	struct raid_map_data raid_map;	/* I/O accelerator RAID map */

	/*
	 * Pointers from logical drive map indices to the phys drives that
	 * make those logical drives.  Note, multiple logical drives may
	 * share physical drives.  You can have for instance 5 physical
	 * drives with 3 logical drives each using those same 5 physical
	 * disks. We need these pointers for counting i/o's out to physical
	 * devices in order to honor physical device queue depth limits.
	 */
	struct hpsa_scsi_dev_t *phys_disk[RAID_MAP_MAX_ENTRIES];
	int nphysical_disks;
	int supports_aborts;
	struct hpsa_sas_port *sas_port;
	int external;   /* 1-from external array 0-not <0-unknown */
};

struct reply_queue_buffer {
	u64 *head;
	size_t size;
	u8 wraparound;
	u32 current_entry;
	dma_addr_t busaddr;
};

#pragma pack(1)
struct bmic_controller_parameters {
	u8   led_flags;
	u8   enable_command_list_verification;
	u8   backed_out_write_drives;
	u16  stripes_for_parity;
	u8   parity_distribution_mode_flags;
	u16  max_driver_requests;
	u16  elevator_trend_count;
	u8   disable_elevator;
	u8   force_scan_complete;
	u8   scsi_transfer_mode;
	u8   force_narrow;
	u8   rebuild_priority;
	u8   expand_priority;
	u8   host_sdb_asic_fix;
	u8   pdpi_burst_from_host_disabled;
	char software_name[64];
	char hardware_name[32];
	u8   bridge_revision;
	u8   snapshot_priority;
	u32  os_specific;
	u8   post_prompt_timeout;
	u8   automatic_drive_slamming;
	u8   reserved1;
	u8   nvram_flags;
	u8   cache_nvram_flags;
	u8   drive_config_flags;
	u16  reserved2;
	u8   temp_warning_level;
	u8   temp_shutdown_level;
	u8   temp_condition_reset;
	u8   max_coalesce_commands;
	u32  max_coalesce_delay;
	u8   orca_password[4];
	u8   access_id[16];
	u8   reserved[356];
};
#pragma pack()

struct ctlr_info {
	int	ctlr;
	char	devname[8];
	char    *product_name;
	struct pci_dev *pdev;
	u32	board_id;
	u64	sas_address;
	void __iomem *vaddr;
	unsigned long paddr;
	int 	nr_cmds; /* Number of commands allowed on this controller */
#define HPSA_CMDS_RESERVED_FOR_ABORTS 2
#define HPSA_CMDS_RESERVED_FOR_DRIVER 1
	struct CfgTable __iomem *cfgtable;
	int	interrupts_enabled;
	int 	max_commands;
	atomic_t commands_outstanding;
#	define PERF_MODE_INT	0
#	define DOORBELL_INT	1
#	define SIMPLE_MODE_INT	2
#	define MEMQ_MODE_INT	3
	unsigned int intr[MAX_REPLY_QUEUES];
	unsigned int msix_vector;
	unsigned int msi_vector;
	int intr_mode; /* either PERF_MODE_INT or SIMPLE_MODE_INT */
	struct access_method access;

	/* queue and queue Info */
	unsigned int Qdepth;
	unsigned int maxSG;
	spinlock_t lock;
	int maxsgentries;
	u8 max_cmd_sg_entries;
	int chainsize;
	struct SGDescriptor **cmd_sg_list;
	struct ioaccel2_sg_element **ioaccel2_cmd_sg_list;

	/* pointers to command and error info pool */
	struct CommandList 	*cmd_pool;
	dma_addr_t		cmd_pool_dhandle;
	struct io_accel1_cmd	*ioaccel_cmd_pool;
	dma_addr_t		ioaccel_cmd_pool_dhandle;
	struct io_accel2_cmd	*ioaccel2_cmd_pool;
	dma_addr_t		ioaccel2_cmd_pool_dhandle;
	struct ErrorInfo 	*errinfo_pool;
	dma_addr_t		errinfo_pool_dhandle;
	unsigned long  		*cmd_pool_bits;
	int			scan_finished;
	spinlock_t		scan_lock;
	wait_queue_head_t	scan_wait_queue;

	struct Scsi_Host *scsi_host;
	spinlock_t devlock; /* to protect hba[ctlr]->dev[];  */
	int ndevices; /* number of used elements in .dev[] array. */
	struct hpsa_scsi_dev_t *dev[HPSA_MAX_DEVICES];
	/*
	 * Performant mode tables.
	 */
	u32 trans_support;
	u32 trans_offset;
	struct TransTable_struct __iomem *transtable;
	unsigned long transMethod;

	/* cap concurrent passthrus at some reasonable maximum */
#define HPSA_MAX_CONCURRENT_PASSTHRUS (10)
	atomic_t passthru_cmds_avail;

	/*
	 * Performant mode completion buffers
	 */
	size_t reply_queue_size;
	struct reply_queue_buffer reply_queue[MAX_REPLY_QUEUES];
	u8 nreply_queues;
	u32 *blockFetchTable;
	u32 *ioaccel1_blockFetchTable;
	u32 *ioaccel2_blockFetchTable;
	u32 __iomem *ioaccel2_bft2_regs;
	unsigned char *hba_inquiry_data;
	u32 driver_support;
	u32 fw_support;
	int ioaccel_support;
	int ioaccel_maxsg;
	u64 last_intr_timestamp;
	u32 last_heartbeat;
	u64 last_heartbeat_timestamp;
	u32 heartbeat_sample_interval;
	atomic_t firmware_flash_in_progress;
	u32 __percpu *lockup_detected;
	struct delayed_work monitor_ctlr_work;
	struct delayed_work rescan_ctlr_work;
	int remove_in_progress;
	/* Address of h->q[x] is passed to intr handler to know which queue */
	u8 q[MAX_REPLY_QUEUES];
	char intrname[MAX_REPLY_QUEUES][16];	/* "hpsa0-msix00" names */
	u32 TMFSupportFlags; /* cache what task mgmt funcs are supported. */
#define HPSATMF_BITS_SUPPORTED  (1 << 0)
#define HPSATMF_PHYS_LUN_RESET  (1 << 1)
#define HPSATMF_PHYS_NEX_RESET  (1 << 2)
#define HPSATMF_PHYS_TASK_ABORT (1 << 3)
#define HPSATMF_PHYS_TSET_ABORT (1 << 4)
#define HPSATMF_PHYS_CLEAR_ACA  (1 << 5)
#define HPSATMF_PHYS_CLEAR_TSET (1 << 6)
#define HPSATMF_PHYS_QRY_TASK   (1 << 7)
#define HPSATMF_PHYS_QRY_TSET   (1 << 8)
#define HPSATMF_PHYS_QRY_ASYNC  (1 << 9)
#define HPSATMF_IOACCEL_ENABLED (1 << 15)
#define HPSATMF_MASK_SUPPORTED  (1 << 16)
#define HPSATMF_LOG_LUN_RESET   (1 << 17)
#define HPSATMF_LOG_NEX_RESET   (1 << 18)
#define HPSATMF_LOG_TASK_ABORT  (1 << 19)
#define HPSATMF_LOG_TSET_ABORT  (1 << 20)
#define HPSATMF_LOG_CLEAR_ACA   (1 << 21)
#define HPSATMF_LOG_CLEAR_TSET  (1 << 22)
#define HPSATMF_LOG_QRY_TASK    (1 << 23)
#define HPSATMF_LOG_QRY_TSET    (1 << 24)
#define HPSATMF_LOG_QRY_ASYNC   (1 << 25)
	u32 events;
#define CTLR_STATE_CHANGE_EVENT				(1 << 0)
#define CTLR_ENCLOSURE_HOT_PLUG_EVENT			(1 << 1)
#define CTLR_STATE_CHANGE_EVENT_PHYSICAL_DRV		(1 << 4)
#define CTLR_STATE_CHANGE_EVENT_LOGICAL_DRV		(1 << 5)
#define CTLR_STATE_CHANGE_EVENT_REDUNDANT_CNTRL		(1 << 6)
#define CTLR_STATE_CHANGE_EVENT_AIO_ENABLED_DISABLED	(1 << 30)
#define CTLR_STATE_CHANGE_EVENT_AIO_CONFIG_CHANGE	(1 << 31)

#define RESCAN_REQUIRED_EVENT_BITS \
		(CTLR_ENCLOSURE_HOT_PLUG_EVENT | \
		CTLR_STATE_CHANGE_EVENT_PHYSICAL_DRV | \
		CTLR_STATE_CHANGE_EVENT_LOGICAL_DRV | \
		CTLR_STATE_CHANGE_EVENT_AIO_ENABLED_DISABLED | \
		CTLR_STATE_CHANGE_EVENT_AIO_CONFIG_CHANGE)
	spinlock_t offline_device_lock;
	struct list_head offline_device_list;
	int	acciopath_status;
	int	drv_req_rescan;
	int	raid_offload_debug;
	int     discovery_polling;
	struct  ReportLUNdata *lastlogicals;
	int	needs_abort_tags_swizzled;
	struct workqueue_struct *resubmit_wq;
	struct workqueue_struct *rescan_ctlr_wq;
	atomic_t abort_cmds_available;
	wait_queue_head_t abort_cmd_wait_queue;
	wait_queue_head_t event_sync_wait_queue;
	struct mutex reset_mutex;
	u8 reset_in_progress;
	struct hpsa_sas_node *sas_host;
};

struct offline_device_entry {
	unsigned char scsi3addr[8];
	struct list_head offline_list;
};

#define HPSA_ABORT_MSG 0
#define HPSA_DEVICE_RESET_MSG 1
#define HPSA_RESET_TYPE_CONTROLLER 0x00
#define HPSA_RESET_TYPE_BUS 0x01
#define HPSA_RESET_TYPE_LUN 0x04
#define HPSA_PHYS_TARGET_RESET 0x99 /* not defined by cciss spec */
#define HPSA_MSG_SEND_RETRY_LIMIT 10
#define HPSA_MSG_SEND_RETRY_INTERVAL_MSECS (10000)

/* Maximum time in seconds driver will wait for command completions
 * when polling before giving up.
 */
#define HPSA_MAX_POLL_TIME_SECS (20)

/* During SCSI error recovery, HPSA_TUR_RETRY_LIMIT defines
 * how many times to retry TEST UNIT READY on a device
 * while waiting for it to become ready before giving up.
 * HPSA_MAX_WAIT_INTERVAL_SECS is the max wait interval
 * between sending TURs while waiting for a device
 * to become ready.
 */
#define HPSA_TUR_RETRY_LIMIT (20)
#define HPSA_MAX_WAIT_INTERVAL_SECS (30)

/* HPSA_BOARD_READY_WAIT_SECS is how long to wait for a board
 * to become ready, in seconds, before giving up on it.
 * HPSA_BOARD_READY_POLL_INTERVAL_MSECS * is how long to wait
 * between polling the board to see if it is ready, in
 * milliseconds.  HPSA_BOARD_READY_POLL_INTERVAL and
 * HPSA_BOARD_READY_ITERATIONS are derived from those.
 */
#define HPSA_BOARD_READY_WAIT_SECS (120)
#define HPSA_BOARD_NOT_READY_WAIT_SECS (100)
#define HPSA_BOARD_READY_POLL_INTERVAL_MSECS (100)
#define HPSA_BOARD_READY_POLL_INTERVAL \
	((HPSA_BOARD_READY_POLL_INTERVAL_MSECS * HZ) / 1000)
#define HPSA_BOARD_READY_ITERATIONS \
	((HPSA_BOARD_READY_WAIT_SECS * 1000) / \
		HPSA_BOARD_READY_POLL_INTERVAL_MSECS)
#define HPSA_BOARD_NOT_READY_ITERATIONS \
	((HPSA_BOARD_NOT_READY_WAIT_SECS * 1000) / \
		HPSA_BOARD_READY_POLL_INTERVAL_MSECS)
#define HPSA_POST_RESET_PAUSE_MSECS (3000)
#define HPSA_POST_RESET_NOOP_RETRIES (12)

/*  Defining the diffent access_menthods */
/*
 * Memory mapped FIFO interface (SMART 53xx cards)
 */
#define SA5_DOORBELL	0x20
#define SA5_REQUEST_PORT_OFFSET	0x40
#define SA5_REQUEST_PORT64_LO_OFFSET 0xC0
#define SA5_REQUEST_PORT64_HI_OFFSET 0xC4
#define SA5_REPLY_INTR_MASK_OFFSET	0x34
#define SA5_REPLY_PORT_OFFSET		0x44
#define SA5_INTR_STATUS		0x30
#define SA5_SCRATCHPAD_OFFSET	0xB0

#define SA5_CTCFG_OFFSET	0xB4
#define SA5_CTMEM_OFFSET	0xB8

#define SA5_INTR_OFF		0x08
#define SA5B_INTR_OFF		0x04
#define SA5_INTR_PENDING	0x08
#define SA5B_INTR_PENDING	0x04
#define FIFO_EMPTY		0xffffffff
#define HPSA_FIRMWARE_READY	0xffff0000 /* value in scratchpad register */

#define HPSA_ERROR_BIT		0x02

/* Performant mode flags */
#define SA5_PERF_INTR_PENDING   0x04
#define SA5_PERF_INTR_OFF       0x05
#define SA5_OUTDB_STATUS_PERF_BIT       0x01
#define SA5_OUTDB_CLEAR_PERF_BIT        0x01
#define SA5_OUTDB_CLEAR         0xA0
#define SA5_OUTDB_CLEAR_PERF_BIT        0x01
#define SA5_OUTDB_STATUS        0x9C


#define HPSA_INTR_ON 	1
#define HPSA_INTR_OFF	0

/*
 * Inbound Post Queue offsets for IO Accelerator Mode 2
 */
#define IOACCEL2_INBOUND_POSTQ_32	0x48
#define IOACCEL2_INBOUND_POSTQ_64_LOW	0xd0
#define IOACCEL2_INBOUND_POSTQ_64_HI	0xd4

#define HPSA_PHYSICAL_DEVICE_BUS	0
#define HPSA_RAID_VOLUME_BUS		1
#define HPSA_EXTERNAL_RAID_VOLUME_BUS	2
#define HPSA_HBA_BUS			0
#define HPSA_LEGACY_HBA_BUS		3

/*
	Send the command to the hardware
*/
static void SA5_submit_command(struct ctlr_info *h,
	struct CommandList *c)
{
	writel(c->busaddr, h->vaddr + SA5_REQUEST_PORT_OFFSET);
	(void) readl(h->vaddr + SA5_SCRATCHPAD_OFFSET);
}

static void SA5_submit_command_no_read(struct ctlr_info *h,
	struct CommandList *c)
{
	writel(c->busaddr, h->vaddr + SA5_REQUEST_PORT_OFFSET);
}

static void SA5_submit_command_ioaccel2(struct ctlr_info *h,
	struct CommandList *c)
{
	writel(c->busaddr, h->vaddr + SA5_REQUEST_PORT_OFFSET);
}

/*
 *  This card is the opposite of the other cards.
 *   0 turns interrupts on...
 *   0x08 turns them off...
 */
static void SA5_intr_mask(struct ctlr_info *h, unsigned long val)
{
	if (val) { /* Turn interrupts on */
		h->interrupts_enabled = 1;
		writel(0, h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
		(void) readl(h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
	} else { /* Turn them off */
		h->interrupts_enabled = 0;
		writel(SA5_INTR_OFF,
			h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
		(void) readl(h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
	}
}

static void SA5_performant_intr_mask(struct ctlr_info *h, unsigned long val)
{
	if (val) { /* turn on interrupts */
		h->interrupts_enabled = 1;
		writel(0, h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
		(void) readl(h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
	} else {
		h->interrupts_enabled = 0;
		writel(SA5_PERF_INTR_OFF,
			h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
		(void) readl(h->vaddr + SA5_REPLY_INTR_MASK_OFFSET);
	}
}

static unsigned long SA5_performant_completed(struct ctlr_info *h, u8 q)
{
	struct reply_queue_buffer *rq = &h->reply_queue[q];
	unsigned long register_value = FIFO_EMPTY;

	/* msi auto clears the interrupt pending bit. */
	if (unlikely(!(h->msi_vector || h->msix_vector))) {
		/* flush the controller write of the reply queue by reading
		 * outbound doorbell status register.
		 */
		(void) readl(h->vaddr + SA5_OUTDB_STATUS);
		writel(SA5_OUTDB_CLEAR_PERF_BIT, h->vaddr + SA5_OUTDB_CLEAR);
		/* Do a read in order to flush the write to the controller
		 * (as per spec.)
		 */
		(void) readl(h->vaddr + SA5_OUTDB_STATUS);
	}

	if ((((u32) rq->head[rq->current_entry]) & 1) == rq->wraparound) {
		register_value = rq->head[rq->current_entry];
		rq->current_entry++;
		atomic_dec(&h->commands_outstanding);
	} else {
		register_value = FIFO_EMPTY;
	}
	/* Check for wraparound */
	if (rq->current_entry == h->max_commands) {
		rq->current_entry = 0;
		rq->wraparound ^= 1;
	}
	return register_value;
}

/*
 *   returns value read from hardware.
 *     returns FIFO_EMPTY if there is nothing to read
 */
static unsigned long SA5_completed(struct ctlr_info *h,
	__attribute__((unused)) u8 q)
{
	unsigned long register_value
		= readl(h->vaddr + SA5_REPLY_PORT_OFFSET);

	if (register_value != FIFO_EMPTY)
		atomic_dec(&h->commands_outstanding);

#ifdef HPSA_DEBUG
	if (register_value != FIFO_EMPTY)
		dev_dbg(&h->pdev->dev, "Read %lx back from board\n",
			register_value);
	else
		dev_dbg(&h->pdev->dev, "FIFO Empty read\n");
#endif

	return register_value;
}
/*
 *	Returns true if an interrupt is pending..
 */
static bool SA5_intr_pending(struct ctlr_info *h)
{
	unsigned long register_value  =
		readl(h->vaddr + SA5_INTR_STATUS);
	return register_value & SA5_INTR_PENDING;
}

static bool SA5_performant_intr_pending(struct ctlr_info *h)
{
	unsigned long register_value = readl(h->vaddr + SA5_INTR_STATUS);

	if (!register_value)
		return false;

	/* Read outbound doorbell to flush */
	register_value = readl(h->vaddr + SA5_OUTDB_STATUS);
	return register_value & SA5_OUTDB_STATUS_PERF_BIT;
}

#define SA5_IOACCEL_MODE1_INTR_STATUS_CMP_BIT    0x100

static bool SA5_ioaccel_mode1_intr_pending(struct ctlr_info *h)
{
	unsigned long register_value = readl(h->vaddr + SA5_INTR_STATUS);

	return (register_value & SA5_IOACCEL_MODE1_INTR_STATUS_CMP_BIT) ?
		true : false;
}

#define IOACCEL_MODE1_REPLY_QUEUE_INDEX  0x1A0
#define IOACCEL_MODE1_PRODUCER_INDEX     0x1B8
#define IOACCEL_MODE1_CONSUMER_INDEX     0x1BC
#define IOACCEL_MODE1_REPLY_UNUSED       0xFFFFFFFFFFFFFFFFULL

static unsigned long SA5_ioaccel_mode1_completed(struct ctlr_info *h, u8 q)
{
	u64 register_value;
	struct reply_queue_buffer *rq = &h->reply_queue[q];

	BUG_ON(q >= h->nreply_queues);

	register_value = rq->head[rq->current_entry];
	if (register_value != IOACCEL_MODE1_REPLY_UNUSED) {
		rq->head[rq->current_entry] = IOACCEL_MODE1_REPLY_UNUSED;
		if (++rq->current_entry == rq->size)
			rq->current_entry = 0;
		/*
		 * @todo
		 *
		 * Don't really need to write the new index after each command,
		 * but with current driver design this is easiest.
		 */
		wmb();
		writel((q << 24) | rq->current_entry, h->vaddr +
				IOACCEL_MODE1_CONSUMER_INDEX);
		atomic_dec(&h->commands_outstanding);
	}
	return (unsigned long) register_value;
}

static struct access_method SA5_access = {
	SA5_submit_command,
	SA5_intr_mask,
	SA5_intr_pending,
	SA5_completed,
};

static struct access_method SA5_ioaccel_mode1_access = {
	SA5_submit_command,
	SA5_performant_intr_mask,
	SA5_ioaccel_mode1_intr_pending,
	SA5_ioaccel_mode1_completed,
};

static struct access_method SA5_ioaccel_mode2_access = {
	SA5_submit_command_ioaccel2,
	SA5_performant_intr_mask,
	SA5_performant_intr_pending,
	SA5_performant_completed,
};

static struct access_method SA5_performant_access = {
	SA5_submit_command,
	SA5_performant_intr_mask,
	SA5_performant_intr_pending,
	SA5_performant_completed,
};

static struct access_method SA5_performant_access_no_read = {
	SA5_submit_command_no_read,
	SA5_performant_intr_mask,
	SA5_performant_intr_pending,
	SA5_performant_completed,
};

struct board_type {
	u32	board_id;
	char	*product_name;
	struct access_method *access;
};

#endif /* HPSA_H */

