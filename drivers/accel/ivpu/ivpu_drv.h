/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#ifndef __IVPU_DRV_H__
#define __IVPU_DRV_H__

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <drm/drm_mm.h>
#include <drm/drm_print.h>

#include <linux/pci.h>
#include <linux/xarray.h>
#include <uapi/drm/ivpu_accel.h>

#include "ivpu_mmu_context.h"
#include "ivpu_ipc.h"

#define DRIVER_NAME "intel_vpu"
#define DRIVER_DESC "Driver for Intel NPU (Neural Processing Unit)"

#define PCI_DEVICE_ID_MTL	0x7d1d
#define PCI_DEVICE_ID_ARL	0xad1d
#define PCI_DEVICE_ID_LNL	0x643e
#define PCI_DEVICE_ID_PTL_P	0xb03e

#define IVPU_HW_IP_37XX 37
#define IVPU_HW_IP_40XX 40
#define IVPU_HW_IP_50XX 50
#define IVPU_HW_IP_60XX 60

#define IVPU_HW_IP_REV_LNL_B0 4

#define IVPU_HW_BTRS_MTL 1
#define IVPU_HW_BTRS_LNL 2

#define IVPU_GLOBAL_CONTEXT_MMU_SSID   0
/* SSID 1 is used by the VPU to represent reserved context */
#define IVPU_RESERVED_CONTEXT_MMU_SSID 1
#define IVPU_USER_CONTEXT_MIN_SSID     2
#define IVPU_USER_CONTEXT_MAX_SSID     (IVPU_USER_CONTEXT_MIN_SSID + 63)

#define IVPU_MIN_DB 1
#define IVPU_MAX_DB 255

#define IVPU_JOB_ID_JOB_MASK		GENMASK(7, 0)
#define IVPU_JOB_ID_CONTEXT_MASK	GENMASK(31, 8)

#define IVPU_NUM_PRIORITIES    4
#define IVPU_NUM_CMDQS_PER_CTX (IVPU_NUM_PRIORITIES)

#define IVPU_CMDQ_MIN_ID 1
#define IVPU_CMDQ_MAX_ID 255

#define IVPU_PLATFORM_SILICON 0
#define IVPU_PLATFORM_SIMICS  2
#define IVPU_PLATFORM_FPGA    3
#define IVPU_PLATFORM_INVALID 8

#define IVPU_SCHED_MODE_AUTO -1

#define IVPU_DBG_REG	 BIT(0)
#define IVPU_DBG_IRQ	 BIT(1)
#define IVPU_DBG_MMU	 BIT(2)
#define IVPU_DBG_FILE	 BIT(3)
#define IVPU_DBG_MISC	 BIT(4)
#define IVPU_DBG_FW_BOOT BIT(5)
#define IVPU_DBG_PM	 BIT(6)
#define IVPU_DBG_IPC	 BIT(7)
#define IVPU_DBG_BO	 BIT(8)
#define IVPU_DBG_JOB	 BIT(9)
#define IVPU_DBG_JSM	 BIT(10)
#define IVPU_DBG_KREF	 BIT(11)
#define IVPU_DBG_RPM	 BIT(12)
#define IVPU_DBG_MMU_MAP BIT(13)

#define ivpu_err(vdev, fmt, ...) \
	drm_err(&(vdev)->drm, "%s(): " fmt, __func__, ##__VA_ARGS__)

#define ivpu_err_ratelimited(vdev, fmt, ...) \
	drm_err_ratelimited(&(vdev)->drm, "%s(): " fmt, __func__, ##__VA_ARGS__)

#define ivpu_warn(vdev, fmt, ...) \
	drm_warn(&(vdev)->drm, "%s(): " fmt, __func__, ##__VA_ARGS__)

#define ivpu_warn_ratelimited(vdev, fmt, ...) \
	drm_err_ratelimited(&(vdev)->drm, "%s(): " fmt, __func__, ##__VA_ARGS__)

#define ivpu_info(vdev, fmt, ...) drm_info(&(vdev)->drm, fmt, ##__VA_ARGS__)

#define ivpu_dbg(vdev, type, fmt, args...) do {                                \
	if (unlikely(IVPU_DBG_##type & ivpu_dbg_mask))                         \
		dev_dbg((vdev)->drm.dev, "[%s] " fmt, #type, ##args);          \
} while (0)

#define IVPU_WA(wa_name) (vdev->wa.wa_name)

#define IVPU_PRINT_WA(wa_name) do {					\
	if (IVPU_WA(wa_name))						\
		ivpu_dbg(vdev, MISC, "Using WA: " #wa_name "\n");	\
} while (0)

struct ivpu_wa_table {
	bool punit_disabled;
	bool clear_runtime_mem;
	bool interrupt_clear_with_0;
	bool disable_clock_relinquish;
	bool disable_d0i3_msg;
	bool wp0_during_power_up;
};

struct ivpu_hw_info;
struct ivpu_mmu_info;
struct ivpu_fw_info;
struct ivpu_ipc_info;
struct ivpu_pm_info;

struct ivpu_device {
	struct drm_device drm;
	void __iomem *regb;
	void __iomem *regv;
	u32 platform;
	u32 irq;

	struct ivpu_wa_table wa;
	struct ivpu_hw_info *hw;
	struct ivpu_mmu_info *mmu;
	struct ivpu_fw_info *fw;
	struct ivpu_ipc_info *ipc;
	struct ivpu_pm_info *pm;

	struct ivpu_mmu_context gctx;
	struct ivpu_mmu_context rctx;
	struct mutex context_list_lock; /* Protects user context addition/removal */
	struct xarray context_xa;
	struct xa_limit context_xa_limit;
	struct work_struct context_abort_work;

	struct xarray db_xa;
	struct xa_limit db_limit;
	u32 db_next;

	struct mutex bo_list_lock; /* Protects bo_list */
	struct list_head bo_list;

	struct mutex submitted_jobs_lock; /* Protects submitted_jobs */
	struct xarray submitted_jobs_xa;
	struct ivpu_ipc_consumer job_done_consumer;

	atomic64_t unique_id_counter;

	ktime_t busy_start_ts;
	ktime_t busy_time;

	struct {
		int boot;
		int jsm;
		int tdr;
		int autosuspend;
		int d0i3_entry_msg;
		int state_dump_msg;
	} timeout;
};

/*
 * file_priv has its own refcount (ref) that allows user space to close the fd
 * without blocking even if VPU is still processing some jobs.
 */
struct ivpu_file_priv {
	struct kref ref;
	struct ivpu_device *vdev;
	struct mutex lock; /* Protects cmdq */
	struct xarray cmdq_xa;
	struct ivpu_mmu_context ctx;
	struct mutex ms_lock; /* Protects ms_instance_list, ms_info_bo */
	struct list_head ms_instance_list;
	struct ivpu_bo *ms_info_bo;
	struct xa_limit job_limit;
	u32 job_id_next;
	struct xa_limit cmdq_limit;
	u32 cmdq_id_next;
	bool has_mmu_faults;
	bool bound;
	bool aborted;
};

extern int ivpu_dbg_mask;
extern u8 ivpu_pll_min_ratio;
extern u8 ivpu_pll_max_ratio;
extern int ivpu_sched_mode;
extern bool ivpu_disable_mmu_cont_pages;
extern bool ivpu_force_snoop;

#define IVPU_TEST_MODE_FW_TEST            BIT(0)
#define IVPU_TEST_MODE_NULL_HW            BIT(1)
#define IVPU_TEST_MODE_NULL_SUBMISSION    BIT(2)
#define IVPU_TEST_MODE_D0I3_MSG_DISABLE   BIT(4)
#define IVPU_TEST_MODE_D0I3_MSG_ENABLE    BIT(5)
#define IVPU_TEST_MODE_MIP_DISABLE        BIT(6)
#define IVPU_TEST_MODE_DISABLE_TIMEOUTS   BIT(8)
#define IVPU_TEST_MODE_TURBO		  BIT(9)
extern int ivpu_test_mode;

struct ivpu_file_priv *ivpu_file_priv_get(struct ivpu_file_priv *file_priv);
void ivpu_file_priv_put(struct ivpu_file_priv **link);

int ivpu_boot(struct ivpu_device *vdev);
int ivpu_shutdown(struct ivpu_device *vdev);
void ivpu_prepare_for_reset(struct ivpu_device *vdev);

static inline u8 ivpu_revision(struct ivpu_device *vdev)
{
	return to_pci_dev(vdev->drm.dev)->revision;
}

static inline u16 ivpu_device_id(struct ivpu_device *vdev)
{
	return to_pci_dev(vdev->drm.dev)->device;
}

static inline int ivpu_hw_ip_gen(struct ivpu_device *vdev)
{
	switch (ivpu_device_id(vdev)) {
	case PCI_DEVICE_ID_MTL:
	case PCI_DEVICE_ID_ARL:
		return IVPU_HW_IP_37XX;
	case PCI_DEVICE_ID_LNL:
		return IVPU_HW_IP_40XX;
	case PCI_DEVICE_ID_PTL_P:
		return IVPU_HW_IP_50XX;
	default:
		dump_stack();
		ivpu_err(vdev, "Unknown NPU IP generation\n");
		return 0;
	}
}

static inline int ivpu_hw_btrs_gen(struct ivpu_device *vdev)
{
	switch (ivpu_device_id(vdev)) {
	case PCI_DEVICE_ID_MTL:
	case PCI_DEVICE_ID_ARL:
		return IVPU_HW_BTRS_MTL;
	case PCI_DEVICE_ID_LNL:
	case PCI_DEVICE_ID_PTL_P:
		return IVPU_HW_BTRS_LNL;
	default:
		dump_stack();
		ivpu_err(vdev, "Unknown buttress generation\n");
		return 0;
	}
}

static inline struct ivpu_device *to_ivpu_device(struct drm_device *dev)
{
	return container_of(dev, struct ivpu_device, drm);
}

static inline u32 ivpu_get_context_count(struct ivpu_device *vdev)
{
	struct xa_limit ctx_limit = vdev->context_xa_limit;

	return (ctx_limit.max - ctx_limit.min + 1);
}

static inline u32 ivpu_get_platform(struct ivpu_device *vdev)
{
	WARN_ON_ONCE(vdev->platform == IVPU_PLATFORM_INVALID);
	return vdev->platform;
}

static inline bool ivpu_is_silicon(struct ivpu_device *vdev)
{
	return ivpu_get_platform(vdev) == IVPU_PLATFORM_SILICON;
}

static inline bool ivpu_is_simics(struct ivpu_device *vdev)
{
	return ivpu_get_platform(vdev) == IVPU_PLATFORM_SIMICS;
}

static inline bool ivpu_is_fpga(struct ivpu_device *vdev)
{
	return ivpu_get_platform(vdev) == IVPU_PLATFORM_FPGA;
}

static inline bool ivpu_is_force_snoop_enabled(struct ivpu_device *vdev)
{
	return ivpu_force_snoop;
}

#endif /* __IVPU_DRV_H__ */
