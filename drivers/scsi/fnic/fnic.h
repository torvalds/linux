/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */
#ifndef _FNIC_H_
#define _FNIC_H_

#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/fc_frame.h>
#include "fnic_io.h"
#include "fnic_res.h"
#include "fnic_trace.h"
#include "fnic_stats.h"
#include "vnic_dev.h"
#include "vnic_wq.h"
#include "vnic_rq.h"
#include "vnic_cq.h"
#include "vnic_wq_copy.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "vnic_scsi.h"
#include "fnic_fdls.h"

#define DRV_NAME		"fnic"
#define DRV_DESCRIPTION		"Cisco FCoE HBA Driver"
#define DRV_VERSION		"1.8.0.0"
#define PFX			DRV_NAME ": "
#define DFX                     DRV_NAME "%d: "

#define FABRIC_LOGO_MAX_RETRY 3
#define DESC_CLEAN_LOW_WATERMARK 8
#define FNIC_UCSM_DFLT_THROTTLE_CNT_BLD	16 /* UCSM default throttle count */
#define FNIC_MIN_IO_REQ			256 /* Min IO throttle count */
#define FNIC_MAX_IO_REQ		1024 /* scsi_cmnd tag map entries */
#define FNIC_DFLT_IO_REQ        256 /* Default scsi_cmnd tag map entries */
#define FNIC_DFLT_QUEUE_DEPTH	256
#define	FNIC_STATS_RATE_LIMIT	4 /* limit rate at which stats are pulled up */
#define LUN0_DELAY_TIME			9

/*
 * Tag bits used for special requests.
 */
#define FNIC_TAG_ABORT		BIT(30)		/* tag bit indicating abort */
#define FNIC_TAG_DEV_RST	BIT(29)		/* indicates device reset */
#define FNIC_TAG_MASK		(BIT(24) - 1)	/* mask for lookup */
#define FNIC_NO_TAG             -1

/*
 * Command flags to identify the type of command and for other future
 * use.
 */
#define FNIC_NO_FLAGS                   0
#define FNIC_IO_INITIALIZED             BIT(0)
#define FNIC_IO_ISSUED                  BIT(1)
#define FNIC_IO_DONE                    BIT(2)
#define FNIC_IO_REQ_NULL                BIT(3)
#define FNIC_IO_ABTS_PENDING            BIT(4)
#define FNIC_IO_ABORTED                 BIT(5)
#define FNIC_IO_ABTS_ISSUED             BIT(6)
#define FNIC_IO_TERM_ISSUED             BIT(7)
#define FNIC_IO_INTERNAL_TERM_ISSUED    BIT(8)
#define FNIC_IO_ABT_TERM_DONE           BIT(9)
#define FNIC_IO_ABT_TERM_REQ_NULL       BIT(10)
#define FNIC_IO_ABT_TERM_TIMED_OUT      BIT(11)
#define FNIC_DEVICE_RESET               BIT(12)  /* Device reset request */
#define FNIC_DEV_RST_ISSUED             BIT(13)
#define FNIC_DEV_RST_TIMED_OUT          BIT(14)
#define FNIC_DEV_RST_ABTS_ISSUED        BIT(15)
#define FNIC_DEV_RST_TERM_ISSUED        BIT(16)
#define FNIC_DEV_RST_DONE               BIT(17)
#define FNIC_DEV_RST_REQ_NULL           BIT(18)
#define FNIC_DEV_RST_ABTS_DONE          BIT(19)
#define FNIC_DEV_RST_TERM_DONE          BIT(20)
#define FNIC_DEV_RST_ABTS_PENDING       BIT(21)

#define FNIC_FW_RESET_TIMEOUT        60000	/* mSec   */
#define FNIC_FCOE_MAX_CMD_LEN        16
/* Retry supported by rport (returned by PRLI service parameters) */
#define FNIC_FC_RP_FLAGS_RETRY            0x1

/* Cisco vendor id */
#define PCI_VENDOR_ID_CISCO						0x1137
#define PCI_DEVICE_ID_CISCO_VIC_FC				0x0045	/* fc vnic */

/* sereno pcie switch */
#define PCI_DEVICE_ID_CISCO_SERENO             0x004e
#define PCI_DEVICE_ID_CISCO_CRUZ               0x007a	/* Cruz */
#define PCI_DEVICE_ID_CISCO_BODEGA             0x0131	/* Bodega */
#define PCI_DEVICE_ID_CISCO_BEVERLY            0x025f	/* Beverly */

/* Sereno */
#define PCI_SUBDEVICE_ID_CISCO_VASONA			0x004f	/* vasona mezz */
#define PCI_SUBDEVICE_ID_CISCO_COTATI			0x0084	/* cotati mlom */
#define PCI_SUBDEVICE_ID_CISCO_LEXINGTON		0x0085	/* lexington pcie */
#define PCI_SUBDEVICE_ID_CISCO_ICEHOUSE			0x00cd	/* Icehouse */
#define PCI_SUBDEVICE_ID_CISCO_KIRKWOODLAKE		0x00ce	/* KirkwoodLake pcie */
#define PCI_SUBDEVICE_ID_CISCO_SUSANVILLE		0x012e	/* Susanville MLOM */
#define PCI_SUBDEVICE_ID_CISCO_TORRANCE			0x0139	/* Torrance MLOM */

/* Cruz */
#define PCI_SUBDEVICE_ID_CISCO_CALISTOGA		0x012c	/* Calistoga MLOM */
#define PCI_SUBDEVICE_ID_CISCO_MOUNTAINVIEW		0x0137	/* Cruz Mezz */
/* Cruz MountTian SIOC */
#define PCI_SUBDEVICE_ID_CISCO_MOUNTTIAN		0x014b
#define PCI_SUBDEVICE_ID_CISCO_CLEARLAKE		0x014d	/* ClearLake pcie */
/* Cruz MountTian2 SIOC */
#define PCI_SUBDEVICE_ID_CISCO_MOUNTTIAN2		0x0157
#define PCI_SUBDEVICE_ID_CISCO_CLAREMONT		0x015d	/* Claremont MLOM */

/* Bodega */
/* VIC 1457 PCIe mLOM */
#define PCI_SUBDEVICE_ID_CISCO_BRADBURY         0x0218
#define PCI_SUBDEVICE_ID_CISCO_BRENTWOOD        0x0217	/* VIC 1455 PCIe */
/* VIC 1487 PCIe mLOM */
#define PCI_SUBDEVICE_ID_CISCO_BURLINGAME       0x021a
#define PCI_SUBDEVICE_ID_CISCO_BAYSIDE          0x0219	/* VIC 1485 PCIe */
/* VIC 1440 Mezz mLOM */
#define PCI_SUBDEVICE_ID_CISCO_BAKERSFIELD      0x0215
#define PCI_SUBDEVICE_ID_CISCO_BOONVILLE        0x0216	/* VIC 1480 Mezz */
#define PCI_SUBDEVICE_ID_CISCO_BENICIA          0x024a	/* VIC 1495 */
#define PCI_SUBDEVICE_ID_CISCO_BEAUMONT         0x024b	/* VIC 1497 */
#define PCI_SUBDEVICE_ID_CISCO_BRISBANE         0x02af	/* VIC 1467 */
#define PCI_SUBDEVICE_ID_CISCO_BENTON           0x02b0	/* VIC 1477 */
#define PCI_SUBDEVICE_ID_CISCO_TWIN_RIVER       0x02cf	/* VIC 14425 */
#define PCI_SUBDEVICE_ID_CISCO_TWIN_PEAK        0x02d0	/* VIC 14825 */

/* Beverly */
#define PCI_SUBDEVICE_ID_CISCO_BERN             0x02de	/* VIC 15420 */
#define PCI_SUBDEVICE_ID_CISCO_STOCKHOLM        0x02dd	/* VIC 15428 */
#define PCI_SUBDEVICE_ID_CISCO_KRAKOW           0x02dc	/* VIC 15411 */
#define PCI_SUBDEVICE_ID_CISCO_LUCERNE          0x02db	/* VIC 15231 */
#define PCI_SUBDEVICE_ID_CISCO_TURKU            0x02e8	/* VIC 15238 */
#define PCI_SUBDEVICE_ID_CISCO_TURKU_PLUS       0x02f3	/* VIC 15237 */
#define PCI_SUBDEVICE_ID_CISCO_ZURICH           0x02df	/* VIC 15230 */
#define PCI_SUBDEVICE_ID_CISCO_RIGA             0x02e0	/* VIC 15427 */
#define PCI_SUBDEVICE_ID_CISCO_GENEVA           0x02e1	/* VIC 15422 */
#define PCI_SUBDEVICE_ID_CISCO_HELSINKI         0x02e4	/* VIC 15235 */
#define PCI_SUBDEVICE_ID_CISCO_GOTHENBURG       0x02f2	/* VIC 15425 */

struct fnic_pcie_device {
	u32 device;
	u8 *desc;
	u32 subsystem_device;
	u8 *subsys_desc;
};

/*
 * fnic private data per SCSI command.
 * These fields are locked by the hashed io_req_lock.
 */
struct fnic_cmd_priv {
	struct fnic_io_req *io_req;
	enum fnic_ioreq_state state;
	u32 flags;
	u16 abts_status;
	u16 lr_status;
};

static inline struct fnic_cmd_priv *fnic_priv(struct scsi_cmnd *cmd)
{
	return scsi_cmd_priv(cmd);
}

static inline u64 fnic_flags_and_state(struct scsi_cmnd *cmd)
{
	struct fnic_cmd_priv *fcmd = fnic_priv(cmd);

	return ((u64)fcmd->flags << 32) | fcmd->state;
}

#define FCPIO_INVALID_CODE 0x100 /* hdr_status value unused by firmware */

#define FNIC_LUN_RESET_TIMEOUT	     10000	/* mSec */
#define FNIC_HOST_RESET_TIMEOUT	     10000	/* mSec */
#define FNIC_RMDEVICE_TIMEOUT        1000       /* mSec */
#define FNIC_HOST_RESET_SETTLE_TIME  30         /* Sec */
#define FNIC_ABT_TERM_DELAY_TIMEOUT  500        /* mSec */

#define FNIC_MAX_FCP_TARGET     256
#define FNIC_PCI_OFFSET		2
/**
 * state_flags to identify host state along along with fnic's state
 **/
#define __FNIC_FLAGS_FWRESET		BIT(0) /* fwreset in progress */
#define __FNIC_FLAGS_BLOCK_IO		BIT(1) /* IOs are blocked */

#define FNIC_FLAGS_NONE			(0)
#define FNIC_FLAGS_FWRESET		(__FNIC_FLAGS_FWRESET | \
					__FNIC_FLAGS_BLOCK_IO)

#define FNIC_FLAGS_IO_BLOCKED		(__FNIC_FLAGS_BLOCK_IO)

#define fnic_set_state_flags(fnicp, st_flags)	\
	__fnic_set_state_flags(fnicp, st_flags, 0)

#define fnic_clear_state_flags(fnicp, st_flags)  \
	__fnic_set_state_flags(fnicp, st_flags, 1)

enum reset_states {
	NOT_IN_PROGRESS = 0,
	IN_PROGRESS,
	RESET_ERROR
};

enum rscn_type {
	NOT_PC_RSCN = 0,
	PC_RSCN
};

enum pc_rscn_handling_status {
	PC_RSCN_HANDLING_NOT_IN_PROGRESS = 0,
	PC_RSCN_HANDLING_IN_PROGRESS
};

enum pc_rscn_handling_feature {
	PC_RSCN_HANDLING_FEATURE_OFF = 0,
	PC_RSCN_HANDLING_FEATURE_ON
};

extern unsigned int fnic_fdmi_support;
extern unsigned int fnic_log_level;
extern unsigned int io_completions;
extern struct workqueue_struct *fnic_event_queue;

extern unsigned int pc_rscn_handling_feature_flag;
extern spinlock_t reset_fnic_list_lock;
extern struct list_head reset_fnic_list;
extern struct workqueue_struct *reset_fnic_work_queue;
extern struct work_struct reset_fnic_work;


#define FNIC_MAIN_LOGGING 0x01
#define FNIC_FCS_LOGGING 0x02
#define FNIC_SCSI_LOGGING 0x04
#define FNIC_ISR_LOGGING 0x08

#define FNIC_CHECK_LOGGING(LEVEL, CMD)				\
do {								\
	if (unlikely(fnic_log_level & LEVEL))			\
		do {						\
			CMD;					\
		} while (0);					\
} while (0)

#define FNIC_MAIN_DBG(kern_level, host, fnic_num, fmt, args...)		\
	FNIC_CHECK_LOGGING(FNIC_MAIN_LOGGING,			\
			 shost_printk(kern_level, host,			\
				"fnic<%d>: %s: %d: " fmt, fnic_num,\
				__func__, __LINE__, ##args);)

#define FNIC_FCS_DBG(kern_level, host, fnic_num, fmt, args...)		\
	FNIC_CHECK_LOGGING(FNIC_FCS_LOGGING,			\
			 shost_printk(kern_level, host,			\
				"fnic<%d>: %s: %d: " fmt, fnic_num,\
				__func__, __LINE__, ##args);)

#define FNIC_FIP_DBG(kern_level, host, fnic_num, fmt, args...)		\
	FNIC_CHECK_LOGGING(FNIC_FCS_LOGGING,			\
			 shost_printk(kern_level, host,			\
				"fnic<%d>: %s: %d: " fmt, fnic_num,\
				__func__, __LINE__, ##args);)

#define FNIC_SCSI_DBG(kern_level, host, fnic_num, fmt, args...)		\
	FNIC_CHECK_LOGGING(FNIC_SCSI_LOGGING,			\
			 shost_printk(kern_level, host,			\
				"fnic<%d>: %s: %d: " fmt, fnic_num,\
				__func__, __LINE__, ##args);)

#define FNIC_ISR_DBG(kern_level, host, fnic_num, fmt, args...)		\
	FNIC_CHECK_LOGGING(FNIC_ISR_LOGGING,			\
			 shost_printk(kern_level, host,			\
				"fnic<%d>: %s: %d: " fmt, fnic_num,\
				__func__, __LINE__, ##args);)

#define FNIC_MAIN_NOTE(kern_level, host, fmt, args...)          \
	shost_printk(kern_level, host, fmt, ##args)

#define FNIC_WQ_COPY_MAX 64
#define FNIC_WQ_MAX 1
#define FNIC_RQ_MAX 1
#define FNIC_CQ_MAX (FNIC_WQ_COPY_MAX + FNIC_WQ_MAX + FNIC_RQ_MAX)
#define FNIC_DFLT_IO_COMPLETIONS 256

#define FNIC_MQ_CQ_INDEX        2

extern const char *fnic_state_str[];

enum fnic_intx_intr_index {
	FNIC_INTX_WQ_RQ_COPYWQ,
	FNIC_INTX_DUMMY,
	FNIC_INTX_NOTIFY,
	FNIC_INTX_ERR,
	FNIC_INTX_INTR_MAX,
};

enum fnic_msix_intr_index {
	FNIC_MSIX_RQ,
	FNIC_MSIX_WQ,
	FNIC_MSIX_WQ_COPY,
	FNIC_MSIX_ERR_NOTIFY = FNIC_MSIX_WQ_COPY + FNIC_WQ_COPY_MAX,
	FNIC_MSIX_INTR_MAX,
};

struct fnic_msix_entry {
	int requested;
	char devname[IFNAMSIZ + 11];
	irqreturn_t (*isr)(int, void *);
	void *devid;
	int irq_num;
};

enum fnic_state {
	FNIC_IN_FC_MODE = 0,
	FNIC_IN_FC_TRANS_ETH_MODE,
	FNIC_IN_ETH_MODE,
	FNIC_IN_ETH_TRANS_FC_MODE,
};

struct mempool;

enum fnic_role_e {
	FNIC_ROLE_FCP_INITIATOR = 0,
};

enum fnic_evt {
	FNIC_EVT_START_VLAN_DISC = 1,
	FNIC_EVT_START_FCF_DISC = 2,
	FNIC_EVT_MAX,
};

struct fnic_frame_list {
	/*
	 * Link to frame lists
	 */
	struct list_head links;
	void *fp;
	int frame_len;
	int rx_ethhdr_stripped;
};

struct fnic_event {
	struct list_head list;
	struct fnic *fnic;
	enum fnic_evt event;
};

struct fnic_cpy_wq {
	unsigned long hw_lock_flags;
	u16 active_ioreq_count;
	u16 ioreq_table_size;
	____cacheline_aligned struct fnic_io_req **io_req_table;
};

/* Per-instance private data structure */
struct fnic {
	int fnic_num;
	enum fnic_role_e role;
	struct fnic_iport_s iport;
	struct Scsi_Host *host;
	struct vnic_dev_bar bar0;

	struct fnic_msix_entry msix[FNIC_MSIX_INTR_MAX];

	struct vnic_stats *stats;
	unsigned long stats_time;	/* time of stats update */
	unsigned long stats_reset_time; /* time of stats reset */
	struct vnic_nic_cfg *nic_cfg;
	char name[IFNAMSIZ];
	struct timer_list notify_timer; /* used for MSI interrupts */

	unsigned int fnic_max_tag_id;
	unsigned int err_intr_offset;
	unsigned int link_intr_offset;

	unsigned int wq_count;
	unsigned int cq_count;

	struct completion reset_completion_wait;
	struct mutex sgreset_mutex;
	spinlock_t sgreset_lock; /* lock for sgreset */
	struct scsi_cmnd *sgreset_sc;
	struct dentry *fnic_stats_debugfs_host;
	struct dentry *fnic_stats_debugfs_file;
	struct dentry *fnic_reset_debugfs_file;
	unsigned int reset_stats;
	atomic64_t io_cmpl_skip;
	struct fnic_stats fnic_stats;

	u32 vlan_hw_insert:1;	        /* let hw insert the tag */
	u32 in_remove:1;                /* fnic device in removal */
	u32 stop_rx_link_events:1;      /* stop proc. rx frames, link events */

	struct completion *fw_reset_done;
	u32 reset_in_progress;
	atomic_t in_flight;		/* io counter */
	bool internal_reset_inprogress;
	u32 _reserved;			/* fill hole */
	unsigned long state_flags;	/* protected by host lock */
	enum fnic_state state;
	spinlock_t fnic_lock;
	unsigned long lock_flags;

	u16 vlan_id;	                /* VLAN tag including priority */
	u8 data_src_addr[ETH_ALEN];
	u64 fcp_input_bytes;		/* internal statistic */
	u64 fcp_output_bytes;		/* internal statistic */
	u32 link_down_cnt;
	u32 soft_reset_count;
	int link_status;

	struct list_head list;
	struct list_head links;
	struct pci_dev *pdev;
	struct vnic_fc_config config;
	struct vnic_dev *vdev;
	unsigned int raw_wq_count;
	unsigned int wq_copy_count;
	unsigned int rq_count;
	int fw_ack_index[FNIC_WQ_COPY_MAX];
	unsigned short fw_ack_recd[FNIC_WQ_COPY_MAX];
	unsigned short wq_copy_desc_low[FNIC_WQ_COPY_MAX];
	unsigned int intr_count;
	u32 __iomem *legacy_pba;
	struct fnic_host_tag *tags;
	mempool_t *io_req_pool;
	mempool_t *io_sgl_pool[FNIC_SGL_NUM_CACHES];

	unsigned int copy_wq_base;
	struct work_struct link_work;
	struct work_struct frame_work;
	struct work_struct flush_work;
	struct list_head frame_queue;
	struct list_head tx_queue;
	mempool_t *frame_pool;
	mempool_t *frame_elem_pool;
	struct work_struct tport_work;
	struct list_head tport_event_list;

	char subsys_desc[14];
	int subsys_desc_len;
	int pc_rscn_handling_status;

	/*** FIP related data members  -- start ***/
	void (*set_vlan)(struct fnic *, u16 vlan);
	struct work_struct      fip_frame_work;
	struct work_struct		fip_timer_work;
	struct list_head		fip_frame_queue;
	struct timer_list       fip_timer;
	spinlock_t              vlans_lock;
	struct timer_list retry_fip_timer;
	struct timer_list fcs_ka_timer;
	struct timer_list enode_ka_timer;
	struct timer_list vn_ka_timer;
	struct list_head vlan_list;
	/*** FIP related data members  -- end ***/

	/* copy work queue cache line section */
	____cacheline_aligned struct vnic_wq_copy hw_copy_wq[FNIC_WQ_COPY_MAX];
	____cacheline_aligned struct fnic_cpy_wq sw_copy_wq[FNIC_WQ_COPY_MAX];

	/* completion queue cache line section */
	____cacheline_aligned struct vnic_cq cq[FNIC_CQ_MAX];

	spinlock_t wq_copy_lock[FNIC_WQ_COPY_MAX];

	/* work queue cache line section */
	____cacheline_aligned struct vnic_wq wq[FNIC_WQ_MAX];
	spinlock_t wq_lock[FNIC_WQ_MAX];

	/* receive queue cache line section */
	____cacheline_aligned struct vnic_rq rq[FNIC_RQ_MAX];

	/* interrupt resource cache line section */
	____cacheline_aligned struct vnic_intr intr[FNIC_MSIX_INTR_MAX];
};

extern struct workqueue_struct *fnic_event_queue;
extern struct workqueue_struct *fnic_fip_queue;
extern const struct attribute_group *fnic_host_groups[];

void fnic_clear_intr_mode(struct fnic *fnic);
int fnic_set_intr_mode(struct fnic *fnic);
int fnic_set_intr_mode_msix(struct fnic *fnic);
void fnic_free_intr(struct fnic *fnic);
int fnic_request_intr(struct fnic *fnic);

void fnic_free_wq_buf(struct vnic_wq *wq, struct vnic_wq_buf *buf);
void fnic_handle_frame(struct work_struct *work);
void fnic_tport_event_handler(struct work_struct *work);
void fnic_handle_link(struct work_struct *work);
void fnic_handle_event(struct work_struct *work);
void fdls_reclaim_oxid_handler(struct work_struct *work);
void fdls_schedule_oxid_free(struct fnic_iport_s *iport, uint16_t *active_oxid);
void fdls_schedule_oxid_free_retry_work(struct work_struct *work);
int fnic_rq_cmpl_handler(struct fnic *fnic, int);
int fnic_alloc_rq_frame(struct vnic_rq *rq);
void fnic_free_rq_buf(struct vnic_rq *rq, struct vnic_rq_buf *buf);
void fnic_flush_tx(struct work_struct *work);
void fnic_update_mac_locked(struct fnic *, u8 *new);

int fnic_queuecommand(struct Scsi_Host *, struct scsi_cmnd *);
int fnic_abort_cmd(struct scsi_cmnd *);
int fnic_device_reset(struct scsi_cmnd *);
int fnic_eh_host_reset_handler(struct scsi_cmnd *sc);
int fnic_host_reset(struct Scsi_Host *shost);
void fnic_reset(struct Scsi_Host *shost);
int fnic_issue_fc_host_lip(struct Scsi_Host *shost);
void fnic_get_host_port_state(struct Scsi_Host *shost);
void fnic_scsi_fcpio_reset(struct fnic *fnic);
int fnic_wq_copy_cmpl_handler(struct fnic *fnic, int copy_work_to_do, unsigned int cq_index);
int fnic_wq_cmpl_handler(struct fnic *fnic, int);
int fnic_flogi_reg_handler(struct fnic *fnic, u32);
void fnic_wq_copy_cleanup_handler(struct vnic_wq_copy *wq,
				  struct fcpio_host_req *desc);
int fnic_fw_reset_handler(struct fnic *fnic);
void fnic_terminate_rport_io(struct fc_rport *);
const char *fnic_state_to_str(unsigned int state);
void fnic_mq_map_queues_cpus(struct Scsi_Host *host);
void fnic_log_q_error(struct fnic *fnic);
void fnic_handle_link_event(struct fnic *fnic);
int fnic_stats_debugfs_init(struct fnic *fnic);
void fnic_stats_debugfs_remove(struct fnic *fnic);
int fnic_is_abts_pending(struct fnic *, struct scsi_cmnd *);

void fnic_handle_fip_frame(struct work_struct *work);
void fnic_reset_work_handler(struct work_struct *work);
void fnic_handle_fip_event(struct fnic *fnic);
void fnic_fcoe_reset_vlans(struct fnic *fnic);
extern void fnic_handle_fip_timer(struct timer_list *t);

static inline int
fnic_chk_state_flags_locked(struct fnic *fnic, unsigned long st_flags)
{
	return ((fnic->state_flags & st_flags) == st_flags);
}
void __fnic_set_state_flags(struct fnic *, unsigned long, unsigned long);
void fnic_dump_fchost_stats(struct Scsi_Host *, struct fc_host_statistics *);
void fnic_free_txq(struct list_head *head);
int fnic_get_desc_by_devid(struct pci_dev *pdev, char **desc,
						   char **subsys_desc);
void fnic_fdls_link_status_change(struct fnic *fnic, int linkup);
void fnic_delete_fcp_tports(struct fnic *fnic);
void fnic_flush_tport_event_list(struct fnic *fnic);
int fnic_count_ioreqs_wq(struct fnic *fnic, u32 hwq, u32 portid);
unsigned int fnic_count_ioreqs(struct fnic *fnic, u32 portid);
unsigned int fnic_count_all_ioreqs(struct fnic *fnic);
unsigned int fnic_count_lun_ioreqs_wq(struct fnic *fnic, u32 hwq,
						  struct scsi_device *device);
unsigned int fnic_count_lun_ioreqs(struct fnic *fnic,
					   struct scsi_device *device);
void fnic_scsi_unload(struct fnic *fnic);
void fnic_scsi_unload_cleanup(struct fnic *fnic);
int fnic_get_debug_info(struct stats_debug_info *info,
			struct fnic *fnic);

struct fnic_scsi_iter_data {
	struct fnic *fnic;
	void *data1;
	void *data2;
	bool (*fn)(struct fnic *fnic, struct scsi_cmnd *sc,
			void *data1, void *data2);
};

static inline bool
fnic_io_iter_handler(struct scsi_cmnd *sc, void *iter_data)
{
	struct fnic_scsi_iter_data *iter = iter_data;

	return iter->fn(iter->fnic, sc, iter->data1, iter->data2);
}

static inline void
fnic_scsi_io_iter(struct fnic *fnic,
		bool (*fn)(struct fnic *fnic, struct scsi_cmnd *sc,
				void *data1, void *data2),
		void *data1, void *data2)
{
	struct fnic_scsi_iter_data iter_data = {
		.fn = fn,
		.fnic = fnic,
		.data1 = data1,
		.data2 = data2,
	};
	scsi_host_busy_iter(fnic->host, fnic_io_iter_handler, &iter_data);
}

#ifdef FNIC_DEBUG
static inline void
fnic_debug_dump(struct fnic *fnic, uint8_t *u8arr, int len)
{
	int i;

	for (i = 0; i < len; i = i+8) {
		FNIC_FCS_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		    "%d: %02x %02x %02x %02x %02x %02x %02x %02x", i / 8,
		    u8arr[i + 0], u8arr[i + 1], u8arr[i + 2], u8arr[i + 3],
		    u8arr[i + 4], u8arr[i + 5], u8arr[i + 6], u8arr[i + 7]);
	}
}

static inline void
fnic_debug_dump_fc_frame(struct fnic *fnic, struct fc_frame_header *fchdr,
				int len, char *pfx)
{
	uint32_t s_id, d_id;

	s_id = ntoh24(fchdr->fh_s_id);
	d_id = ntoh24(fchdr->fh_d_id);
	FNIC_FCS_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		"%s packet contents: sid/did/type/oxid = 0x%x/0x%x/0x%x/0x%x (len = %d)\n",
		pfx, s_id, d_id, fchdr->fh_type,
		FNIC_STD_GET_OX_ID(fchdr), len);

	fnic_debug_dump(fnic, (uint8_t *)fchdr, len);

}
#else /* FNIC_DEBUG */
static inline void
fnic_debug_dump(struct fnic *fnic, uint8_t *u8arr, int len) {}
static inline void
fnic_debug_dump_fc_frame(struct fnic *fnic, struct fc_frame_header *fchdr,
				uint32_t len, char *pfx) {}
#endif /* FNIC_DEBUG */
#endif /* _FNIC_H_ */
