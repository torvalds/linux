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

#ifndef _CXLFLASH_SUPERPIPE_H
#define _CXLFLASH_SUPERPIPE_H

extern struct cxlflash_global global;

/*
 * Terminology: use afu (and not adapter) to refer to the HW.
 * Adapter is the entire slot and includes PSL out of which
 * only the AFU is visible to user space.
 */

/* Chunk size parms: note sislite minimum chunk size is
 * 0x10000 LBAs corresponding to a NMASK or 16.
 */
#define MC_CHUNK_SIZE     (1 << MC_RHT_NMASK)	/* in LBAs */

#define CMD_TIMEOUT 30  /* 30 secs */
#define CMD_RETRIES 5   /* 5 retries for scsi_execute */

#define MAX_SECTOR_UNIT  512 /* max_sector is in 512 byte multiples */

enum lun_mode {
	MODE_NONE = 0,
	MODE_VIRTUAL,
	MODE_PHYSICAL
};

/* Global (entire driver, spans adapters) lun_info structure */
struct glun_info {
	u64 max_lba;		/* from read cap(16) */
	u32 blk_len;		/* from read cap(16) */
	enum lun_mode mode;	/* NONE, VIRTUAL, PHYSICAL */
	int users;		/* Number of users w/ references to LUN */

	u8 wwid[16];

	struct mutex mutex;

	struct blka blka;
	struct list_head list;
};

/* Local (per-adapter) lun_info structure */
struct llun_info {
	u64 lun_id[MAX_FC_PORTS]; /* from REPORT_LUNS */
	u32 lun_index;		/* Index in the LUN table */
	u32 host_no;		/* host_no from Scsi_host */
	u32 port_sel;		/* What port to use for this LUN */
	bool in_table;		/* Whether a LUN table entry was created */

	u8 wwid[16];		/* Keep a duplicate copy here? */

	struct glun_info *parent; /* Pointer to entry in global LUN structure */
	struct scsi_device *sdev;
	struct list_head list;
};

struct lun_access {
	struct llun_info *lli;
	struct scsi_device *sdev;
	struct list_head list;
};

enum ctx_ctrl {
	CTX_CTRL_CLONE		= (1 << 1),
	CTX_CTRL_ERR		= (1 << 2),
	CTX_CTRL_ERR_FALLBACK	= (1 << 3),
	CTX_CTRL_NOPID		= (1 << 4),
	CTX_CTRL_FILE		= (1 << 5)
};

#define ENCODE_CTXID(_ctx, _id)	(((((u64)_ctx) & 0xFFFFFFFF0ULL) << 28) | _id)
#define DECODE_CTXID(_val)	(_val & 0xFFFFFFFF)

struct ctx_info {
	struct sisl_ctrl_map __iomem *ctrl_map; /* initialized at startup */
	struct sisl_rht_entry *rht_start; /* 1 page (req'd for alignment),
					   * alloc/free on attach/detach
					   */
	u32 rht_out;		/* Number of checked out RHT entries */
	u32 rht_perms;		/* User-defined permissions for RHT entries */
	struct llun_info **rht_lun;       /* Mapping of RHT entries to LUNs */
	u8 *rht_needs_ws;	/* User-desired write-same function per RHTE */

	u64 ctxid;
	u64 irqs; /* Number of interrupts requested for context */
	pid_t pid;
	bool initialized;
	bool unavail;
	bool err_recovery_active;
	struct mutex mutex; /* Context protection */
	struct kref kref;
	void *ctx;
	struct cxlflash_cfg *cfg;
	struct list_head luns;	/* LUNs attached to this context */
	const struct vm_operations_struct *cxl_mmap_vmops;
	struct file *file;
	struct list_head list; /* Link contexts in error recovery */
};

struct cxlflash_global {
	struct mutex mutex;
	struct list_head gluns;/* list of glun_info structs */
	struct page *err_page; /* One page of all 0xF for error notification */
};

int cxlflash_vlun_resize(struct scsi_device *sdev,
			 struct dk_cxlflash_resize *resize);
int _cxlflash_vlun_resize(struct scsi_device *sdev, struct ctx_info *ctxi,
			  struct dk_cxlflash_resize *resize);

int cxlflash_disk_release(struct scsi_device *sdev,
			  struct dk_cxlflash_release *release);
int _cxlflash_disk_release(struct scsi_device *sdev, struct ctx_info *ctxi,
			   struct dk_cxlflash_release *release);

int cxlflash_disk_clone(struct scsi_device *sdev,
			struct dk_cxlflash_clone *clone);

int cxlflash_disk_virtual_open(struct scsi_device *sdev, void *arg);

int cxlflash_lun_attach(struct glun_info *gli, enum lun_mode mode, bool locked);
void cxlflash_lun_detach(struct glun_info *gli);

struct ctx_info *get_context(struct cxlflash_cfg *cfg, u64 rctxit, void *arg,
			     enum ctx_ctrl ctrl);
void put_context(struct ctx_info *ctxi);

struct sisl_rht_entry *get_rhte(struct ctx_info *ctxi, res_hndl_t rhndl,
				struct llun_info *lli);

struct sisl_rht_entry *rhte_checkout(struct ctx_info *ctxi,
				     struct llun_info *lli);
void rhte_checkin(struct ctx_info *ctxi, struct sisl_rht_entry *rhte);

void cxlflash_ba_terminate(struct ba_lun *ba_lun);

int cxlflash_manage_lun(struct scsi_device *sdev,
			struct dk_cxlflash_manage_lun *manage);

int check_state(struct cxlflash_cfg *cfg);

#endif /* ifndef _CXLFLASH_SUPERPIPE_H */
