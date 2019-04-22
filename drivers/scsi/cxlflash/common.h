/*
 * CXL Flash Device Driver
 *
 * Written by: Manoj N. Kumar <manoj@linux.vnet.ibm.com>, IBM Corporation
 *             Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2015 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _CXLFLASH_COMMON_H
#define _CXLFLASH_COMMON_H

#include <linux/async.h>
#include <linux/cdev.h>
#include <linux/irq_poll.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/types.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>

#include "backend.h"

extern const struct file_operations cxlflash_cxl_fops;

#define MAX_CONTEXT	CXLFLASH_MAX_CONTEXT	/* num contexts per afu */
#define MAX_FC_PORTS	CXLFLASH_MAX_FC_PORTS	/* max ports per AFU */
#define LEGACY_FC_PORTS	2			/* legacy ports per AFU */

#define CHAN2PORTBANK(_x)	((_x) >> ilog2(CXLFLASH_NUM_FC_PORTS_PER_BANK))
#define CHAN2BANKPORT(_x)	((_x) & (CXLFLASH_NUM_FC_PORTS_PER_BANK - 1))

#define CHAN2PORTMASK(_x)	(1 << (_x))	/* channel to port mask */
#define PORTMASK2CHAN(_x)	(ilog2((_x)))	/* port mask to channel */
#define PORTNUM2CHAN(_x)	((_x) - 1)	/* port number to channel */

#define CXLFLASH_BLOCK_SIZE	4096		/* 4K blocks */
#define CXLFLASH_MAX_XFER_SIZE	16777216	/* 16MB transfer */
#define CXLFLASH_MAX_SECTORS	(CXLFLASH_MAX_XFER_SIZE/512)	/* SCSI wants
								 * max_sectors
								 * in units of
								 * 512 byte
								 * sectors
								 */

#define MAX_RHT_PER_CONTEXT (PAGE_SIZE / sizeof(struct sisl_rht_entry))

/* AFU command retry limit */
#define MC_RETRY_CNT	5	/* Sufficient for SCSI and certain AFU errors */

/* Command management definitions */
#define CXLFLASH_MAX_CMDS               256
#define CXLFLASH_MAX_CMDS_PER_LUN       CXLFLASH_MAX_CMDS

/* RRQ for master issued cmds */
#define NUM_RRQ_ENTRY                   CXLFLASH_MAX_CMDS

/* SQ for master issued cmds */
#define NUM_SQ_ENTRY			CXLFLASH_MAX_CMDS

/* Hardware queue definitions */
#define CXLFLASH_DEF_HWQS		1
#define CXLFLASH_MAX_HWQS		8
#define PRIMARY_HWQ			0


static inline void check_sizes(void)
{
	BUILD_BUG_ON_NOT_POWER_OF_2(CXLFLASH_NUM_FC_PORTS_PER_BANK);
	BUILD_BUG_ON_NOT_POWER_OF_2(CXLFLASH_MAX_CMDS);
}

/* AFU defines a fixed size of 4K for command buffers (borrow 4K page define) */
#define CMD_BUFSIZE     SIZE_4K

enum cxlflash_lr_state {
	LINK_RESET_INVALID,
	LINK_RESET_REQUIRED,
	LINK_RESET_COMPLETE
};

enum cxlflash_init_state {
	INIT_STATE_NONE,
	INIT_STATE_PCI,
	INIT_STATE_AFU,
	INIT_STATE_SCSI,
	INIT_STATE_CDEV
};

enum cxlflash_state {
	STATE_PROBING,	/* Initial state during probe */
	STATE_PROBED,	/* Temporary state, probe completed but EEH occurred */
	STATE_NORMAL,	/* Normal running state, everything good */
	STATE_RESET,	/* Reset state, trying to reset/recover */
	STATE_FAILTERM	/* Failed/terminating state, error out users/threads */
};

enum cxlflash_hwq_mode {
	HWQ_MODE_RR,	/* Roundrobin (default) */
	HWQ_MODE_TAG,	/* Distribute based on block MQ tag */
	HWQ_MODE_CPU,	/* CPU affinity */
	MAX_HWQ_MODE
};

/*
 * Each context has its own set of resource handles that is visible
 * only from that context.
 */

struct cxlflash_cfg {
	struct afu *afu;

	const struct cxlflash_backend_ops *ops;
	struct pci_dev *dev;
	struct pci_device_id *dev_id;
	struct Scsi_Host *host;
	int num_fc_ports;
	struct cdev cdev;
	struct device *chardev;

	ulong cxlflash_regs_pci;

	struct work_struct work_q;
	enum cxlflash_init_state init_state;
	enum cxlflash_lr_state lr_state;
	int lr_port;
	atomic_t scan_host_needed;

	void *afu_cookie;

	atomic_t recovery_threads;
	struct mutex ctx_recovery_mutex;
	struct mutex ctx_tbl_list_mutex;
	struct rw_semaphore ioctl_rwsem;
	struct ctx_info *ctx_tbl[MAX_CONTEXT];
	struct list_head ctx_err_recovery; /* contexts w/ recovery pending */
	struct file_operations cxl_fops;

	/* Parameters that are LUN table related */
	int last_lun_index[MAX_FC_PORTS];
	int promote_lun_index;
	struct list_head lluns; /* list of llun_info structs */

	wait_queue_head_t tmf_waitq;
	spinlock_t tmf_slock;
	bool tmf_active;
	bool ws_unmap;		/* Write-same unmap supported */
	wait_queue_head_t reset_waitq;
	enum cxlflash_state state;
	async_cookie_t async_reset_cookie;
};

struct afu_cmd {
	struct sisl_ioarcb rcb;	/* IOARCB (cache line aligned) */
	struct sisl_ioasa sa;	/* IOASA must follow IOARCB */
	struct afu *parent;
	struct scsi_cmnd *scp;
	struct completion cevent;
	struct list_head queue;
	u32 hwq_index;

	u8 cmd_tmf:1,
	   cmd_aborted:1;

	struct list_head list;	/* Pending commands link */

	/* As per the SISLITE spec the IOARCB EA has to be 16-byte aligned.
	 * However for performance reasons the IOARCB/IOASA should be
	 * cache line aligned.
	 */
} __aligned(cache_line_size());

static inline struct afu_cmd *sc_to_afuc(struct scsi_cmnd *sc)
{
	return PTR_ALIGN(scsi_cmd_priv(sc), __alignof__(struct afu_cmd));
}

static inline struct afu_cmd *sc_to_afuci(struct scsi_cmnd *sc)
{
	struct afu_cmd *afuc = sc_to_afuc(sc);

	INIT_LIST_HEAD(&afuc->queue);
	return afuc;
}

static inline struct afu_cmd *sc_to_afucz(struct scsi_cmnd *sc)
{
	struct afu_cmd *afuc = sc_to_afuc(sc);

	memset(afuc, 0, sizeof(*afuc));
	return sc_to_afuci(sc);
}

struct hwq {
	/* Stuff requiring alignment go first. */
	struct sisl_ioarcb sq[NUM_SQ_ENTRY];		/* 16K SQ */
	u64 rrq_entry[NUM_RRQ_ENTRY];			/* 2K RRQ */

	/* Beware of alignment till here. Preferably introduce new
	 * fields after this point
	 */
	struct afu *afu;
	void *ctx_cookie;
	struct sisl_host_map __iomem *host_map;		/* MC host map */
	struct sisl_ctrl_map __iomem *ctrl_map;		/* MC control map */
	ctx_hndl_t ctx_hndl;	/* master's context handle */
	u32 index;		/* Index of this hwq */
	int num_irqs;		/* Number of interrupts requested for context */
	struct list_head pending_cmds;	/* Commands pending completion */

	atomic_t hsq_credits;
	spinlock_t hsq_slock;	/* Hardware send queue lock */
	struct sisl_ioarcb *hsq_start;
	struct sisl_ioarcb *hsq_end;
	struct sisl_ioarcb *hsq_curr;
	spinlock_t hrrq_slock;
	u64 *hrrq_start;
	u64 *hrrq_end;
	u64 *hrrq_curr;
	bool toggle;
	bool hrrq_online;

	s64 room;

	struct irq_poll irqpoll;
} __aligned(cache_line_size());

struct afu {
	struct hwq hwqs[CXLFLASH_MAX_HWQS];
	int (*send_cmd)(struct afu *afu, struct afu_cmd *cmd);
	int (*context_reset)(struct hwq *hwq);

	/* AFU HW */
	struct cxlflash_afu_map __iomem *afu_map;	/* entire MMIO map */

	atomic_t cmds_active;	/* Number of currently active AFU commands */
	struct mutex sync_active;	/* Mutex to serialize AFU commands */
	u64 hb;
	u32 internal_lun;	/* User-desired LUN mode for this AFU */

	u32 num_hwqs;		/* Number of hardware queues */
	u32 desired_hwqs;	/* Desired h/w queues, effective on AFU reset */
	enum cxlflash_hwq_mode hwq_mode; /* Steering mode for h/w queues */
	u32 hwq_rr_count;	/* Count to distribute traffic for roundrobin */

	char version[16];
	u64 interface_version;

	u32 irqpoll_weight;
	struct cxlflash_cfg *parent; /* Pointer back to parent cxlflash_cfg */
};

static inline struct hwq *get_hwq(struct afu *afu, u32 index)
{
	WARN_ON(index >= CXLFLASH_MAX_HWQS);

	return &afu->hwqs[index];
}

static inline bool afu_is_irqpoll_enabled(struct afu *afu)
{
	return !!afu->irqpoll_weight;
}

static inline bool afu_has_cap(struct afu *afu, u64 cap)
{
	u64 afu_cap = afu->interface_version >> SISL_INTVER_CAP_SHIFT;

	return afu_cap & cap;
}

static inline bool afu_is_ocxl_lisn(struct afu *afu)
{
	return afu_has_cap(afu, SISL_INTVER_CAP_OCXL_LISN);
}

static inline bool afu_is_afu_debug(struct afu *afu)
{
	return afu_has_cap(afu, SISL_INTVER_CAP_AFU_DEBUG);
}

static inline bool afu_is_lun_provision(struct afu *afu)
{
	return afu_has_cap(afu, SISL_INTVER_CAP_LUN_PROVISION);
}

static inline bool afu_is_sq_cmd_mode(struct afu *afu)
{
	return afu_has_cap(afu, SISL_INTVER_CAP_SQ_CMD_MODE);
}

static inline bool afu_is_ioarrin_cmd_mode(struct afu *afu)
{
	return afu_has_cap(afu, SISL_INTVER_CAP_IOARRIN_CMD_MODE);
}

static inline u64 lun_to_lunid(u64 lun)
{
	__be64 lun_id;

	int_to_scsilun(lun, (struct scsi_lun *)&lun_id);
	return be64_to_cpu(lun_id);
}

static inline struct fc_port_bank __iomem *get_fc_port_bank(
					    struct cxlflash_cfg *cfg, int i)
{
	struct afu *afu = cfg->afu;

	return &afu->afu_map->global.bank[CHAN2PORTBANK(i)];
}

static inline __be64 __iomem *get_fc_port_regs(struct cxlflash_cfg *cfg, int i)
{
	struct fc_port_bank __iomem *fcpb = get_fc_port_bank(cfg, i);

	return &fcpb->fc_port_regs[CHAN2BANKPORT(i)][0];
}

static inline __be64 __iomem *get_fc_port_luns(struct cxlflash_cfg *cfg, int i)
{
	struct fc_port_bank __iomem *fcpb = get_fc_port_bank(cfg, i);

	return &fcpb->fc_port_luns[CHAN2BANKPORT(i)][0];
}

int cxlflash_afu_sync(struct afu *afu, ctx_hndl_t c, res_hndl_t r, u8 mode);
void cxlflash_list_init(void);
void cxlflash_term_global_luns(void);
void cxlflash_free_errpage(void);
int cxlflash_ioctl(struct scsi_device *sdev, unsigned int cmd,
		   void __user *arg);
void cxlflash_stop_term_user_contexts(struct cxlflash_cfg *cfg);
int cxlflash_mark_contexts_error(struct cxlflash_cfg *cfg);
void cxlflash_term_local_luns(struct cxlflash_cfg *cfg);
void cxlflash_restore_luntable(struct cxlflash_cfg *cfg);

#endif /* ifndef _CXLFLASH_COMMON_H */
