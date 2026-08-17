/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#ifndef _AMDXDNA_PCI_DRV_H_
#define _AMDXDNA_PCI_DRV_H_

#include <drm/amdxdna_accel.h>
#include <drm/drm_mm.h>
#include <drm/drm_print.h>
#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/workqueue.h>
#include <linux/xarray.h>

#define XDNA_INFO(xdna, fmt, args...)	drm_info(&(xdna)->ddev, fmt, ##args)
#define XDNA_WARN(xdna, fmt, args...)	drm_warn(&(xdna)->ddev, "%s: "fmt, __func__, ##args)
#define XDNA_ERR(xdna, fmt, args...)	drm_err(&(xdna)->ddev, "%s: "fmt, __func__, ##args)
#define XDNA_DBG(xdna, fmt, args...)	drm_dbg(&(xdna)->ddev, fmt, ##args)
#define XDNA_INFO_ONCE(xdna, fmt, args...) drm_info_once(&(xdna)->ddev, fmt, ##args)

#define XDNA_MBZ_DBG(xdna, ptr, sz)					\
	({								\
		int __i;						\
		int __ret = 0;						\
		u8 *__ptr = (u8 *)(ptr);				\
		for (__i = 0; __i < (sz); __i++) {			\
			if (__ptr[__i]) {				\
				XDNA_DBG(xdna, "MBZ check failed");	\
				__ret = -EINVAL;			\
				break;					\
			}						\
		}							\
		__ret;							\
	})

#define to_xdna_dev(drm_dev) \
	((struct amdxdna_dev *)container_of(drm_dev, struct amdxdna_dev, ddev))

extern const struct drm_driver amdxdna_drm_drv;

struct amdxdna_client;
struct amdxdna_dev;
struct amdxdna_drm_get_info;
struct amdxdna_drm_set_state;
struct amdxdna_gem_obj;
struct amdxdna_hwctx;
struct amdxdna_sched_job;

/*
 * struct amdxdna_dev_ops - Device hardware operation callbacks
 */
struct amdxdna_dev_ops {
	int (*init)(struct amdxdna_dev *xdna);
	void (*fini)(struct amdxdna_dev *xdna);
	int (*resume)(struct amdxdna_dev *xdna);
	int (*suspend)(struct amdxdna_dev *xdna);
	int (*sriov_configure)(struct amdxdna_dev *xdna, int num_vfs);
	int (*mmap)(struct amdxdna_client *client, struct vm_area_struct *vma);
	int (*hwctx_init)(struct amdxdna_hwctx *hwctx);
	void (*hwctx_fini)(struct amdxdna_hwctx *hwctx);
	int (*hwctx_config)(struct amdxdna_hwctx *hwctx, u32 type, u64 value, void *buf, u32 size);
	int (*hwctx_sync_debug_bo)(struct amdxdna_hwctx *hwctx, u32 debug_bo_hdl);
	int (*hwctx_heap_expand)(struct amdxdna_hwctx *hwctx, struct amdxdna_gem_obj *heap);
	void (*hmm_invalidate)(struct amdxdna_gem_obj *abo, unsigned long cur_seq);
	int (*cmd_submit)(struct amdxdna_hwctx *hwctx, struct amdxdna_sched_job *job, u64 *seq);
	int (*cmd_wait)(struct amdxdna_hwctx *hwctx, u64 seq, u32 timeout);
	int (*get_aie_info)(struct amdxdna_client *client, struct amdxdna_drm_get_info *args);
	int (*set_aie_state)(struct amdxdna_client *client, struct amdxdna_drm_set_state *args);
	int (*get_array)(struct amdxdna_client *client, struct amdxdna_drm_get_array *args);
	int (*get_dev_revision)(struct amdxdna_dev *xdna, u32 *rev);
};

struct amdxdna_fw_feature_tbl {
	u64 features;
	u32 major;
	u32 max_minor;
	u32 min_minor;
};

/*
 * struct amdxdna_dev_info - Device hardware information
 * Record device static information, like reg, mbox, PSP, SMU bar index
 */
struct amdxdna_dev_info {
	int				reg_bar;
	int				mbox_bar;
	int				sram_bar;
	int				psp_bar;
	int				smu_bar;
	int				doorbell_bar;
	int				device_type;
	int				first_col;
	u32				dev_mem_buf_shift;
	u64				dev_mem_base;
	size_t				dev_mem_size;
	const char			*default_vbnv;
	const struct amdxdna_rev_vbnv	*rev_vbnv_tbl;
	size_t				dev_heap_max_size;
	const struct amdxdna_dev_priv	*dev_priv;
	const struct amdxdna_fw_feature_tbl *fw_feature_tbl;
	const struct amdxdna_dev_ops	*ops;
};

struct amdxdna_fw_ver {
	u32 major;
	u32 minor;
	u32 sub;
	u32 build;
};

struct amdxdna_carveout;

struct amdxdna_dev {
	struct drm_device		ddev;
	struct amdxdna_dev_hdl		*dev_handle;
	const struct amdxdna_dev_info	*dev_info;
	void				*xrs_hdl;

	struct mutex			dev_lock; /* per device lock */
	struct list_head		client_list;
	struct mutex			client_lock; /* client_list */
	struct amdxdna_fw_ver		fw_ver;
	struct rw_semaphore		notifier_lock; /* for mmu notifier*/
	struct workqueue_struct		*notifier_wq;

	struct iommu_group		*group;
	struct iommu_domain		*domain;
	struct iova_domain		iovad;
	/* Accurate board name queried from firmware, or default_vbnv as fallback */
	const char			*vbnv;

	struct amdxdna_carveout		*carveout;
};

/*
 * struct amdxdna_device_id - PCI device info
 */
struct amdxdna_device_id {
	unsigned short device;
	u8 revision;
	const struct amdxdna_dev_info *dev_info;
};

/*
 * struct amdxdna_client - amdxdna client
 * A per fd data structure for managing context and other user process stuffs.
 */
struct amdxdna_client {
	struct list_head		node;
	pid_t				pid;
	struct srcu_struct		hwctx_srcu;
	struct xarray			hwctx_xa;
	u32				next_hwctxid;
	struct amdxdna_dev		*xdna;
	struct drm_file			*filp;

	struct mutex			mm_lock; /* protect memory related */
	struct xarray			dev_heap_xa;
	struct drm_mm			dev_heap_mm;
	u32				dev_heap_nid;
	size_t				total_heap_size;

	struct iommu_sva		*sva;
	int				pasid;
	struct mm_struct		*mm;

	size_t				heap_usage;
	size_t				total_bo_usage;
	size_t				total_int_bo_usage;
};

#define amdxdna_for_each_hwctx(client, hwctx_id, entry)		\
	xa_for_each(&(client)->hwctx_xa, hwctx_id, entry)

/* Add device info below */
extern const struct amdxdna_dev_info dev_npu1_info;
extern const struct amdxdna_dev_info dev_npu3_pf_info;
extern const struct amdxdna_dev_info dev_npu3_vf_info;
extern const struct amdxdna_dev_info dev_npu4_info;
extern const struct amdxdna_dev_info dev_npu5_info;
extern const struct amdxdna_dev_info dev_npu6_info;

int amdxdna_sysfs_init(struct amdxdna_dev *xdna);
void amdxdna_sysfs_fini(struct amdxdna_dev *xdna);

int amdxdna_iommu_init(struct amdxdna_dev *xdna);
void amdxdna_iommu_fini(struct amdxdna_dev *xdna);
void *amdxdna_iommu_alloc(struct amdxdna_dev *xdna, size_t size, dma_addr_t *dma_addr);
void amdxdna_iommu_free(struct amdxdna_dev *xdna, size_t size,
			void *cpu_addr, dma_addr_t dma_addr);
int amdxdna_dma_map_bo(struct amdxdna_dev *xdna, struct amdxdna_gem_obj *abo);
void amdxdna_dma_unmap_bo(struct amdxdna_dev *xdna, struct amdxdna_gem_obj *abo);

static inline bool amdxdna_iova_on(struct amdxdna_dev *xdna)
{
	return !!xdna->domain;
}

static inline bool amdxdna_pasid_on(struct amdxdna_client *client)
{
	return client->pasid != IOMMU_PASID_INVALID;
}
#endif /* _AMDXDNA_PCI_DRV_H_ */
