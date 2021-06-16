/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Universal Flash Storage Host Performance Booster
 *
 * Copyright (C) 2017-2021 Samsung Electronics Co., Ltd.
 *
 * Authors:
 *	Yongmyung Lee <ymhungry.lee@samsung.com>
 *	Jinyoung Choi <j-young.choi@samsung.com>
 */

#ifndef _UFSHPB_H_
#define _UFSHPB_H_

/* hpb response UPIU macro */
#define HPB_RSP_NONE				0x0
#define HPB_RSP_REQ_REGION_UPDATE		0x1
#define HPB_RSP_DEV_RESET			0x2
#define MAX_ACTIVE_NUM				2
#define MAX_INACTIVE_NUM			2
#define DEV_DATA_SEG_LEN			0x14
#define DEV_SENSE_SEG_LEN			0x12
#define DEV_DES_TYPE				0x80
#define DEV_ADDITIONAL_LEN			0x10

/* hpb map & entries macro */
#define HPB_RGN_SIZE_UNIT			512
#define HPB_ENTRY_BLOCK_SIZE			4096
#define HPB_ENTRY_SIZE				0x8
#define PINNED_NOT_SET				U32_MAX

/* hpb support chunk size */
#define HPB_MULTI_CHUNK_HIGH			1

/* hpb vender defined opcode */
#define UFSHPB_READ				0xF8
#define UFSHPB_READ_BUFFER			0xF9
#define UFSHPB_READ_BUFFER_ID			0x01
#define HPB_READ_BUFFER_CMD_LENGTH		10
#define LU_ENABLED_HPB_FUNC			0x02

#define HPB_RESET_REQ_RETRIES			10
#define HPB_MAP_REQ_RETRIES			5

#define HPB_SUPPORT_VERSION			0x100

enum UFSHPB_MODE {
	HPB_HOST_CONTROL,
	HPB_DEVICE_CONTROL,
};

enum UFSHPB_STATE {
	HPB_INIT = 0,
	HPB_PRESENT = 1,
	HPB_SUSPEND,
	HPB_FAILED,
	HPB_RESET,
};

enum HPB_RGN_STATE {
	HPB_RGN_INACTIVE,
	HPB_RGN_ACTIVE,
	/* pinned regions are always active */
	HPB_RGN_PINNED,
};

enum HPB_SRGN_STATE {
	HPB_SRGN_UNUSED,
	HPB_SRGN_INVALID,
	HPB_SRGN_VALID,
	HPB_SRGN_ISSUED,
};

/**
 * struct ufshpb_lu_info - UFSHPB logical unit related info
 * @num_blocks: the number of logical block
 * @pinned_start: the start region number of pinned region
 * @num_pinned: the number of pinned regions
 * @max_active_rgns: maximum number of active regions
 */
struct ufshpb_lu_info {
	int num_blocks;
	int pinned_start;
	int num_pinned;
	int max_active_rgns;
};

struct ufshpb_map_ctx {
	struct page **m_page;
	unsigned long *ppn_dirty;
};

struct ufshpb_subregion {
	struct ufshpb_map_ctx *mctx;
	enum HPB_SRGN_STATE srgn_state;
	int rgn_idx;
	int srgn_idx;
	bool is_last;
	/* below information is used by rsp_list */
	struct list_head list_act_srgn;
};

struct ufshpb_region {
	struct ufshpb_subregion *srgn_tbl;
	enum HPB_RGN_STATE rgn_state;
	int rgn_idx;
	int srgn_cnt;

	/* below information is used by rsp_list */
	struct list_head list_inact_rgn;

	/* below information is used by lru */
	struct list_head list_lru_rgn;
};

#define for_each_sub_region(rgn, i, srgn)				\
	for ((i) = 0;							\
	     ((i) < (rgn)->srgn_cnt) && ((srgn) = &(rgn)->srgn_tbl[i]); \
	     (i)++)

/**
 * struct ufshpb_req - UFSHPB READ BUFFER (for caching map) request structure
 * @req: block layer request for READ BUFFER
 * @bio: bio for holding map page
 * @hpb: ufshpb_lu structure that related to the L2P map
 * @mctx: L2P map information
 * @rgn_idx: target region index
 * @srgn_idx: target sub-region index
 * @lun: target logical unit number
 */
struct ufshpb_req {
	struct request *req;
	struct bio *bio;
	struct ufshpb_lu *hpb;
	struct ufshpb_map_ctx *mctx;

	unsigned int rgn_idx;
	unsigned int srgn_idx;
};

struct victim_select_info {
	struct list_head lh_lru_rgn; /* LRU list of regions */
	int max_lru_active_cnt; /* supported hpb #region - pinned #region */
	atomic_t active_cnt;
};

struct ufshpb_stats {
	u64 hit_cnt;
	u64 miss_cnt;
	u64 rb_noti_cnt;
	u64 rb_active_cnt;
	u64 rb_inactive_cnt;
	u64 map_req_cnt;
};

struct ufshpb_lu {
	int lun;
	struct scsi_device *sdev_ufs_lu;

	spinlock_t rgn_state_lock; /* for protect rgn/srgn state */
	struct ufshpb_region *rgn_tbl;

	atomic_t hpb_state;

	spinlock_t rsp_list_lock;
	struct list_head lh_act_srgn; /* hold rsp_list_lock */
	struct list_head lh_inact_rgn; /* hold rsp_list_lock */

	/* cached L2P map management worker */
	struct work_struct map_work;

	/* for selecting victim */
	struct victim_select_info lru_info;

	/* pinned region information */
	u32 lu_pinned_start;
	u32 lu_pinned_end;

	/* HPB related configuration */
	u32 rgns_per_lu;
	u32 srgns_per_lu;
	u32 last_srgn_entries;
	int srgns_per_rgn;
	u32 srgn_mem_size;
	u32 entries_per_rgn_mask;
	u32 entries_per_rgn_shift;
	u32 entries_per_srgn;
	u32 entries_per_srgn_mask;
	u32 entries_per_srgn_shift;
	u32 pages_per_srgn;

	struct ufshpb_stats stats;

	struct kmem_cache *map_req_cache;
	struct kmem_cache *m_page_cache;

	struct list_head list_hpb_lu;
};

struct ufs_hba;
struct ufshcd_lrb;

#ifndef CONFIG_SCSI_UFS_HPB
static void ufshpb_prep(struct ufs_hba *hba, struct ufshcd_lrb *lrbp) {}
static void ufshpb_rsp_upiu(struct ufs_hba *hba, struct ufshcd_lrb *lrbp) {}
static void ufshpb_resume(struct ufs_hba *hba) {}
static void ufshpb_suspend(struct ufs_hba *hba) {}
static void ufshpb_reset(struct ufs_hba *hba) {}
static void ufshpb_reset_host(struct ufs_hba *hba) {}
static void ufshpb_init(struct ufs_hba *hba) {}
static void ufshpb_init_hpb_lu(struct ufs_hba *hba, struct scsi_device *sdev) {}
static void ufshpb_destroy_lu(struct ufs_hba *hba, struct scsi_device *sdev) {}
static void ufshpb_remove(struct ufs_hba *hba) {}
static bool ufshpb_is_allowed(struct ufs_hba *hba) { return false; }
static void ufshpb_get_geo_info(struct ufs_hba *hba, u8 *geo_buf) {}
static void ufshpb_get_dev_info(struct ufs_hba *hba, u8 *desc_buf) {}
#else
void ufshpb_prep(struct ufs_hba *hba, struct ufshcd_lrb *lrbp);
void ufshpb_rsp_upiu(struct ufs_hba *hba, struct ufshcd_lrb *lrbp);
void ufshpb_resume(struct ufs_hba *hba);
void ufshpb_suspend(struct ufs_hba *hba);
void ufshpb_reset(struct ufs_hba *hba);
void ufshpb_reset_host(struct ufs_hba *hba);
void ufshpb_init(struct ufs_hba *hba);
void ufshpb_init_hpb_lu(struct ufs_hba *hba, struct scsi_device *sdev);
void ufshpb_destroy_lu(struct ufs_hba *hba, struct scsi_device *sdev);
void ufshpb_remove(struct ufs_hba *hba);
bool ufshpb_is_allowed(struct ufs_hba *hba);
void ufshpb_get_geo_info(struct ufs_hba *hba, u8 *geo_buf);
void ufshpb_get_dev_info(struct ufs_hba *hba, u8 *desc_buf);
extern struct attribute_group ufs_sysfs_hpb_stat_group;
#endif

#endif /* End of Header */
