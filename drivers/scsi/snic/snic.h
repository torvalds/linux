/*
 * Copyright 2014 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _SNIC_H_
#define _SNIC_H_

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/mempool.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>

#include "snic_disc.h"
#include "snic_io.h"
#include "snic_res.h"
#include "snic_trc.h"
#include "snic_stats.h"
#include "vnic_dev.h"
#include "vnic_wq.h"
#include "vnic_cq.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "vnic_snic.h"

#define SNIC_DRV_NAME		"snic"
#define SNIC_DRV_DESCRIPTION	"Cisco SCSI NIC Driver"
#define SNIC_DRV_VERSION	"0.0.1.18"
#define PFX			SNIC_DRV_NAME ":"
#define DFX			SNIC_DRV_NAME "%d: "

#define DESC_CLEAN_LOW_WATERMARK	8
#define SNIC_UCSM_DFLT_THROTTLE_CNT_BLD 16 /* UCSM default throttle count */
#define SNIC_MAX_IO_REQ			50 /* scsi_cmnd tag map entries */
#define SNIC_MIN_IO_REQ			8  /* Min IO throttle count */
#define SNIC_IO_LOCKS			64 /* IO locks: power of 2 */
#define SNIC_DFLT_QUEUE_DEPTH		32 /* Default Queue Depth */
#define SNIC_MAX_QUEUE_DEPTH		64 /* Max Queue Depth */
#define SNIC_DFLT_CMD_TIMEOUT		90 /* Extended tmo for FW */

/*
 * Tag bits used for special requests.
 */
#define SNIC_TAG_ABORT		BIT(30)		/* Tag indicating abort */
#define SNIC_TAG_DEV_RST	BIT(29)		/* Tag for device reset */
#define SNIC_TAG_IOCTL_DEV_RST	BIT(28)		/* Tag for User Device Reset */
#define SNIC_TAG_MASK		(BIT(24) - 1)	/* Mask for lookup */
#define SNIC_NO_TAG		-1

/*
 * Command flags to identify the type of command and for other future use
 */
#define SNIC_NO_FLAGS			0
#define SNIC_IO_INITIALIZED		BIT(0)
#define SNIC_IO_ISSUED			BIT(1)
#define SNIC_IO_DONE			BIT(2)
#define SNIC_IO_REQ_NULL		BIT(3)
#define SNIC_IO_ABTS_PENDING		BIT(4)
#define SNIC_IO_ABORTED			BIT(5)
#define SNIC_IO_ABTS_ISSUED		BIT(6)
#define SNIC_IO_TERM_ISSUED		BIT(7)
#define SNIC_IO_ABTS_TIMEDOUT		BIT(8)
#define SNIC_IO_ABTS_TERM_DONE		BIT(9)
#define SNIC_IO_ABTS_TERM_REQ_NULL	BIT(10)
#define SNIC_IO_ABTS_TERM_TIMEDOUT	BIT(11)
#define SNIC_IO_INTERNAL_TERM_PENDING	BIT(12)
#define SNIC_IO_INTERNAL_TERM_ISSUED	BIT(13)
#define SNIC_DEVICE_RESET		BIT(14)
#define SNIC_DEV_RST_ISSUED		BIT(15)
#define SNIC_DEV_RST_TIMEDOUT		BIT(16)
#define SNIC_DEV_RST_ABTS_ISSUED	BIT(17)
#define SNIC_DEV_RST_TERM_ISSUED	BIT(18)
#define SNIC_DEV_RST_DONE		BIT(19)
#define SNIC_DEV_RST_REQ_NULL		BIT(20)
#define SNIC_DEV_RST_ABTS_DONE		BIT(21)
#define SNIC_DEV_RST_TERM_DONE		BIT(22)
#define SNIC_DEV_RST_ABTS_PENDING	BIT(23)
#define SNIC_DEV_RST_PENDING		BIT(24)
#define SNIC_DEV_RST_NOTSUP		BIT(25)
#define SNIC_SCSI_CLEANUP		BIT(26)
#define SNIC_HOST_RESET_ISSUED		BIT(27)
#define SNIC_HOST_RESET_CMD_TERM	\
	(SNIC_DEV_RST_NOTSUP | SNIC_SCSI_CLEANUP | SNIC_HOST_RESET_ISSUED)

#define SNIC_ABTS_TIMEOUT		30000		/* msec */
#define SNIC_LUN_RESET_TIMEOUT		30000		/* msec */
#define SNIC_HOST_RESET_TIMEOUT		30000		/* msec */


/*
 * These are protected by the hashed req_lock.
 */
#define CMD_SP(Cmnd)		\
	(((struct snic_internal_io_state *)scsi_cmd_priv(Cmnd))->rqi)
#define CMD_STATE(Cmnd)		\
	(((struct snic_internal_io_state *)scsi_cmd_priv(Cmnd))->state)
#define CMD_ABTS_STATUS(Cmnd)	\
	(((struct snic_internal_io_state *)scsi_cmd_priv(Cmnd))->abts_status)
#define CMD_LR_STATUS(Cmnd)	\
	(((struct snic_internal_io_state *)scsi_cmd_priv(Cmnd))->lr_status)
#define CMD_FLAGS(Cmnd)	\
	(((struct snic_internal_io_state *)scsi_cmd_priv(Cmnd))->flags)

#define SNIC_INVALID_CODE 0x100	/* Hdr Status val unused by firmware */

#define SNIC_MAX_TARGET			256
#define SNIC_FLAGS_NONE			(0)

/* snic module params */
extern unsigned int snic_max_qdepth;

/* snic debugging */
extern unsigned int snic_log_level;

#define SNIC_MAIN_LOGGING	0x1
#define SNIC_SCSI_LOGGING	0x2
#define SNIC_ISR_LOGGING	0x8
#define SNIC_DESC_LOGGING	0x10

#define SNIC_CHECK_LOGGING(LEVEL, CMD)		\
do {						\
	if (unlikely(snic_log_level & LEVEL))	\
		do {				\
			CMD;			\
		} while (0);			\
} while (0)

#define SNIC_MAIN_DBG(host, fmt, args...)	\
	SNIC_CHECK_LOGGING(SNIC_MAIN_LOGGING,		\
		shost_printk(KERN_INFO, host, fmt, ## args);)

#define SNIC_SCSI_DBG(host, fmt, args...)	\
	SNIC_CHECK_LOGGING(SNIC_SCSI_LOGGING,		\
		shost_printk(KERN_INFO, host, fmt, ##args);)

#define SNIC_DISC_DBG(host, fmt, args...)	\
	SNIC_CHECK_LOGGING(SNIC_SCSI_LOGGING,		\
		shost_printk(KERN_INFO, host, fmt, ##args);)

#define SNIC_ISR_DBG(host, fmt, args...)	\
	SNIC_CHECK_LOGGING(SNIC_ISR_LOGGING,		\
		shost_printk(KERN_INFO, host, fmt, ##args);)

#define SNIC_HOST_ERR(host, fmt, args...)		\
	shost_printk(KERN_ERR, host, fmt, ##args)

#define SNIC_HOST_INFO(host, fmt, args...)		\
	shost_printk(KERN_INFO, host, fmt, ##args)

#define SNIC_INFO(fmt, args...)				\
	pr_info(PFX fmt, ## args)

#define SNIC_DBG(fmt, args...)				\
	pr_info(PFX fmt, ## args)

#define SNIC_ERR(fmt, args...)				\
	pr_err(PFX fmt, ## args)

#ifdef DEBUG
#define SNIC_BUG_ON(EXPR) \
	({ \
		if (EXPR) { \
			SNIC_ERR("SNIC BUG(%s)\n", #EXPR); \
			BUG_ON(EXPR); \
		} \
	})
#else
#define SNIC_BUG_ON(EXPR) \
	({ \
		if (EXPR) { \
			SNIC_ERR("SNIC BUG(%s) at %s : %d\n", \
				 #EXPR, __func__, __LINE__); \
			WARN_ON_ONCE(EXPR); \
		} \
	})
#endif

/* Soft assert */
#define SNIC_ASSERT_NOT_IMPL(EXPR) \
	({ \
		if (EXPR) {\
			SNIC_INFO("Functionality not impl'ed at %s:%d\n", \
				  __func__, __LINE__); \
			WARN_ON_ONCE(EXPR); \
		} \
	 })


extern const char *snic_state_str[];

enum snic_intx_intr_index {
	SNIC_INTX_WQ_RQ_COPYWQ,
	SNIC_INTX_ERR,
	SNIC_INTX_NOTIFY,
	SNIC_INTX_INTR_MAX,
};

enum snic_msix_intr_index {
	SNIC_MSIX_WQ,
	SNIC_MSIX_IO_CMPL,
	SNIC_MSIX_ERR_NOTIFY,
	SNIC_MSIX_INTR_MAX,
};

#define SNIC_INTRHDLR_NAMSZ	(2 * IFNAMSIZ)
struct snic_msix_entry {
	int requested;
	char devname[SNIC_INTRHDLR_NAMSZ];
	irqreturn_t (*isr)(int, void *);
	void *devid;
};

enum snic_state {
	SNIC_INIT = 0,
	SNIC_ERROR,
	SNIC_ONLINE,
	SNIC_OFFLINE,
	SNIC_FWRESET,
};

#define SNIC_WQ_MAX		1
#define SNIC_CQ_IO_CMPL_MAX	1
#define SNIC_CQ_MAX		(SNIC_WQ_MAX + SNIC_CQ_IO_CMPL_MAX)

/* firmware version information */
struct snic_fw_info {
	u32	fw_ver;
	u32	hid;			/* u16 hid | u16 vnic id */
	u32	max_concur_ios;		/* max concurrent ios */
	u32	max_sgs_per_cmd;	/* max sgls per IO */
	u32	max_io_sz;		/* max io size supported */
	u32	hba_cap;		/* hba capabilities */
	u32	max_tgts;		/* max tgts supported */
	u16	io_tmo;			/* FW Extended timeout */
	struct completion *wait;	/* protected by snic lock*/
};

/*
 * snic_work item : defined to process asynchronous events
 */
struct snic_work {
	struct work_struct work;
	u16	ev_id;
	u64	*ev_data;
};

/*
 * snic structure to represent SCSI vNIC
 */
struct snic {
	/* snic specific members */
	struct list_head list;
	char name[IFNAMSIZ];
	atomic_t state;
	spinlock_t snic_lock;
	struct completion *remove_wait;
	bool in_remove;
	bool stop_link_events;		/* stop processing link events */

	/* discovery related */
	struct snic_disc disc;

	/* Scsi Host info */
	struct Scsi_Host *shost;

	/* vnic related structures */
	struct vnic_dev_bar bar0;

	struct vnic_stats *stats;
	unsigned long stats_time;
	unsigned long stats_reset_time;

	struct vnic_dev *vdev;

	/* hw resource info */
	unsigned int wq_count;
	unsigned int cq_count;
	unsigned int intr_count;
	unsigned int err_intr_offset;

	int link_status; /* retrieved from svnic_dev_link_status() */
	u32 link_down_cnt;

	/* pci related */
	struct pci_dev *pdev;
	struct snic_msix_entry msix[SNIC_MSIX_INTR_MAX];

	/* io related info */
	mempool_t *req_pool[SNIC_REQ_MAX_CACHES]; /* (??) */
	____cacheline_aligned spinlock_t io_req_lock[SNIC_IO_LOCKS];

	/* Maintain snic specific commands, cmds with no tag in spl_cmd_list */
	____cacheline_aligned spinlock_t spl_cmd_lock;
	struct list_head spl_cmd_list;

	unsigned int max_tag_id;
	atomic_t ios_inflight;		/* io in flight counter */

	struct vnic_snic_config config;

	struct work_struct link_work;

	/* firmware information */
	struct snic_fw_info fwinfo;

	/* Work for processing Target related work */
	struct work_struct tgt_work;

	/* Work for processing Discovery */
	struct work_struct disc_work;

	/* stats related */
	unsigned int reset_stats;
	atomic64_t io_cmpl_skip;
	struct snic_stats s_stats;	/* Per SNIC driver stats */

	/* platform specific */
#ifdef CONFIG_SCSI_SNIC_DEBUG_FS
	struct dentry *stats_host;	/* Per snic debugfs root */
	struct dentry *stats_file;	/* Per snic debugfs file */
	struct dentry *reset_stats_file;/* Per snic reset stats file */
#endif

	/* completion queue cache line section */
	____cacheline_aligned struct vnic_cq cq[SNIC_CQ_MAX];

	/* work queue cache line section */
	____cacheline_aligned struct vnic_wq wq[SNIC_WQ_MAX];
	spinlock_t wq_lock[SNIC_WQ_MAX];

	/* interrupt resource cache line section */
	____cacheline_aligned struct vnic_intr intr[SNIC_MSIX_INTR_MAX];
}; /* end of snic structure */

/*
 * SNIC Driver's Global Data
 */
struct snic_global {
	struct list_head snic_list;
	spinlock_t snic_list_lock;

	struct kmem_cache *req_cache[SNIC_REQ_MAX_CACHES];

	struct workqueue_struct *event_q;

#ifdef CONFIG_SCSI_SNIC_DEBUG_FS
	/* debugfs related global data */
	struct dentry *trc_root;
	struct dentry *stats_root;

	struct snic_trc trc ____cacheline_aligned;
#endif
};

extern struct snic_global *snic_glob;

int snic_glob_init(void);
void snic_glob_cleanup(void);

extern struct workqueue_struct *snic_event_queue;
extern const struct attribute_group *snic_host_groups[];

int snic_queuecommand(struct Scsi_Host *, struct scsi_cmnd *);
int snic_abort_cmd(struct scsi_cmnd *);
int snic_device_reset(struct scsi_cmnd *);
int snic_host_reset(struct scsi_cmnd *);
int snic_reset(struct Scsi_Host *, struct scsi_cmnd *);
void snic_shutdown_scsi_cleanup(struct snic *);


int snic_request_intr(struct snic *);
void snic_free_intr(struct snic *);
int snic_set_intr_mode(struct snic *);
void snic_clear_intr_mode(struct snic *);

int snic_fwcq_cmpl_handler(struct snic *, int);
int snic_wq_cmpl_handler(struct snic *, int);
void snic_free_wq_buf(struct vnic_wq *, struct vnic_wq_buf *);


void snic_log_q_error(struct snic *);
void snic_handle_link_event(struct snic *);
void snic_handle_link(struct work_struct *);

int snic_queue_exch_ver_req(struct snic *);
void snic_io_exch_ver_cmpl_handler(struct snic *, struct snic_fw_req *);

int snic_queue_wq_desc(struct snic *, void *os_buf, u16 len);

void snic_handle_untagged_req(struct snic *, struct snic_req_info *);
void snic_release_untagged_req(struct snic *, struct snic_req_info *);
void snic_free_all_untagged_reqs(struct snic *);
int snic_get_conf(struct snic *);
void snic_set_state(struct snic *, enum snic_state);
int snic_get_state(struct snic *);
const char *snic_state_to_str(unsigned int);
void snic_hex_dump(char *, char *, int);
void snic_print_desc(const char *fn, char *os_buf, int len);
const char *show_opcode_name(int val);
#endif /* _SNIC_H */
