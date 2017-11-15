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

#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/types.h>
#include <scsi/scsi.h>
#include <scsi/scsi_device.h>

extern const struct file_operations cxlflash_cxl_fops;

#define MAX_CONTEXT  CXLFLASH_MAX_CONTEXT       /* num contexts per afu */

#define CXLFLASH_BLOCK_SIZE	4096	/* 4K blocks */
#define CXLFLASH_MAX_XFER_SIZE	16777216	/* 16MB transfer */
#define CXLFLASH_MAX_SECTORS	(CXLFLASH_MAX_XFER_SIZE/512)	/* SCSI wants
								   max_sectors
								   in units of
								   512 byte
								   sectors
								*/

#define MAX_RHT_PER_CONTEXT (PAGE_SIZE / sizeof(struct sisl_rht_entry))

/* AFU command retry limit */
#define MC_RETRY_CNT         5	/* sufficient for SCSI check and
				   certain AFU errors */

/* Command management definitions */
#define CXLFLASH_NUM_CMDS	(2 * CXLFLASH_MAX_CMDS)	/* Must be a pow2 for
							   alignment and more
							   efficient array
							   index derivation
							 */

#define CXLFLASH_MAX_CMDS               256
#define CXLFLASH_MAX_CMDS_PER_LUN       CXLFLASH_MAX_CMDS

/* RRQ for master issued cmds */
#define NUM_RRQ_ENTRY                   CXLFLASH_MAX_CMDS


static inline void check_sizes(void)
{
	BUILD_BUG_ON_NOT_POWER_OF_2(CXLFLASH_NUM_CMDS);
}

/* AFU defines a fixed size of 4K for command buffers (borrow 4K page define) */
#define CMD_BUFSIZE     SIZE_4K

/* flags in IOA status area for host use */
#define B_DONE       0x01
#define B_ERROR      0x02	/* set with B_DONE */
#define B_TIMEOUT    0x04	/* set with B_DONE & B_ERROR */

enum cxlflash_lr_state {
	LINK_RESET_INVALID,
	LINK_RESET_REQUIRED,
	LINK_RESET_COMPLETE
};

enum cxlflash_init_state {
	INIT_STATE_NONE,
	INIT_STATE_PCI,
	INIT_STATE_AFU,
	INIT_STATE_SCSI
};

enum cxlflash_state {
	STATE_NORMAL,	/* Normal running state, everything good */
	STATE_RESET,	/* Reset state, trying to reset/recover */
	STATE_FAILTERM	/* Failed/terminating state, error out users/threads */
};

/*
 * Each context has its own set of resource handles that is visible
 * only from that context.
 */

struct cxlflash_cfg {
	struct afu *afu;
	struct cxl_context *mcctx;

	struct pci_dev *dev;
	struct pci_device_id *dev_id;
	struct Scsi_Host *host;

	ulong cxlflash_regs_pci;

	struct work_struct work_q;
	enum cxlflash_init_state init_state;
	enum cxlflash_lr_state lr_state;
	int lr_port;
	atomic_t scan_host_needed;

	struct cxl_afu *cxl_afu;
	struct pci_dev *parent_dev;

	atomic_t recovery_threads;
	struct mutex ctx_recovery_mutex;
	struct mutex ctx_tbl_list_mutex;
	struct rw_semaphore ioctl_rwsem;
	struct ctx_info *ctx_tbl[MAX_CONTEXT];
	struct list_head ctx_err_recovery; /* contexts w/ recovery pending */
	struct file_operations cxl_fops;

	/* Parameters that are LUN table related */
	int last_lun_index[CXLFLASH_NUM_FC_PORTS];
	int promote_lun_index;
	struct list_head lluns; /* list of llun_info structs */

	wait_queue_head_t tmf_waitq;
	spinlock_t tmf_slock;
	bool tmf_active;
	wait_queue_head_t reset_waitq;
	enum cxlflash_state state;
};

struct afu_cmd {
	struct sisl_ioarcb rcb;	/* IOARCB (cache line aligned) */
	struct sisl_ioasa sa;	/* IOASA must follow IOARCB */
	spinlock_t slock;
	struct completion cevent;
	char *buf;		/* per command buffer */
	struct afu *parent;
	int slot;
	atomic_t free;

	u8 cmd_tmf:1;

	/* As per the SISLITE spec the IOARCB EA has to be 16-byte aligned.
	 * However for performance reasons the IOARCB/IOASA should be
	 * cache line aligned.
	 */
} __aligned(cache_line_size());

struct afu {
	/* Stuff requiring alignment go first. */

	u64 rrq_entry[NUM_RRQ_ENTRY];	/* 2K RRQ */
	/*
	 * Command & data for AFU commands.
	 */
	struct afu_cmd cmd[CXLFLASH_NUM_CMDS];

	/* Beware of alignment till here. Preferably introduce new
	 * fields after this point
	 */

	/* AFU HW */
	struct cxl_ioctl_start_work work;
	struct cxlflash_afu_map __iomem *afu_map;	/* entire MMIO map */
	struct sisl_host_map __iomem *host_map;		/* MC host map */
	struct sisl_ctrl_map __iomem *ctrl_map;		/* MC control map */

	struct kref mapcount;

	ctx_hndl_t ctx_hndl;	/* master's context handle */
	u64 *hrrq_start;
	u64 *hrrq_end;
	u64 *hrrq_curr;
	bool toggle;
	bool read_room;
	atomic64_t room;
	u64 hb;
	u32 cmd_couts;		/* Number of command checkouts */
	u32 internal_lun;	/* User-desired LUN mode for this AFU */

	char version[16];
	u64 interface_version;

	struct cxlflash_cfg *parent; /* Pointer back to parent cxlflash_cfg */

};

static inline u64 lun_to_lunid(u64 lun)
{
	__be64 lun_id;

	int_to_scsilun(lun, (struct scsi_lun *)&lun_id);
	return be64_to_cpu(lun_id);
}

int cxlflash_afu_sync(struct afu *, ctx_hndl_t, res_hndl_t, u8);
void cxlflash_list_init(void);
void cxlflash_term_global_luns(void);
void cxlflash_free_errpage(void);
int cxlflash_ioctl(struct scsi_device *, int, void __user *);
void cxlflash_stop_term_user_contexts(struct cxlflash_cfg *);
int cxlflash_mark_contexts_error(struct cxlflash_cfg *);
void cxlflash_term_local_luns(struct cxlflash_cfg *);
void cxlflash_restore_luntable(struct cxlflash_cfg *);

#endif /* ifndef _CXLFLASH_COMMON_H */
