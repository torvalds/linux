/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(__RK_INIT__)
#define __RK_INIT__

#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include "rgxdevice.h"
#include "device.h"

/****************************************************************************
       Add Rockchip modificatons here!
*****************************************************************************/
#define RK33_DVFS_SUPPORT                   1	// 1:DVFS on   0:DVFS off
#define RK33_SYSFS_FILE_SUPPORT             1	// 1:add information nodes in /sys/devices/ffa30000.gpu/

//RK33_USE_RGX_GET_GPU_UTIL and RK33_USE_CUSTOMER_GET_GPU_UTIL are mutually exclusive
#define RK33_USE_RGX_GET_GPU_UTIL           1
#define RK33_USE_CUSTOMER_GET_GPU_UTIL      0

#define RK33_USE_CL_COUNT_UTILS             0
#define OPEN_GPU_PD                         1

//USE_KTHREAD and USE_HRTIMER are mutually exclusive
#define USE_KTHREAD                         0
#define USE_HRTIMER                         1

#define RK_TF_VERSION                       1

#define RK33_MAX_UTILIS                 4
#define RK33_DVFS_FREQ                  50
#define RK33_DEFAULT_CLOCK              400
#define RK33_DVFS_FREQ_LIMIT            1
#define RGX_DVFS_CURRENT_FREQ           0

#define FPS_DEFAULT_GAP                 300
#define FPS_MAX_GAP                     5000
#define LIMIT_FPS                       60
#define LIMIT_FPS_POWER_SAVE            50
#define ONE_KHZ                         1000
#define ONE_MHZ                         1000000
#define HZ_TO_MHZ(m)                  ((m) / ONE_MHZ)
/* Conversion helpers for setting up high resolution timers */
#define HR_TIMER_DELAY_MSEC(x)          (ns_to_ktime((x)*1000000U))
#define HR_TIMER_DELAY_NSEC(x)          (ns_to_ktime(x))
#define RGX_DVFS_LEVEL_INTERVAL          2

/* Shift used for kbasep_pm_metrics_data.time_busy/idle - units of (1 << 8) ns
   This gives a maximum period between samples of 2^(32+8)/100 ns = slightly under 11s.
   Exceeding this will cause overflow */
#define RK_PM_TIME_SHIFT       8

#define RK_EXPORT_API(func)  EXPORT_SYMBOL(func);

typedef struct _rgx_dvfs_info {
	IMG_UINT voltage;
	IMG_UINT clock;
	IMG_INT min_threshold;
	IMG_INT max_threshold;
	IMG_UINT64 time;
	IMG_UINT coef;
} rgx_dvfs_info;

typedef struct _rgx_dvfs_status_type {
	IMG_INT step;
	IMG_INT utilisation;
	IMG_UINT32 temperature;
	IMG_UINT32 temperature_time;
#if 0
	IMG_INT upper_lock;
	IMG_INT under_lock;
#endif

} rgx_dvfs_status;

enum {
	DBG_OFF = 0,
	DBG_LOW,
	DBG_HIGH,
};

struct rk_utilis {
	IMG_INT utilis[RK33_MAX_UTILIS];
	IMG_INT time_busys[RK33_MAX_UTILIS];
	IMG_INT time_idles[RK33_MAX_UTILIS];
};

struct rk_context {
	/** Indicator if system clock to mail-t604 is active */
	IMG_INT cmu_pmu_status;
	/** cmd & pmu lock */
	spinlock_t cmu_pmu_lock;
	/*Timer */
	spinlock_t timer_lock;

#if OPEN_GPU_PD
	IMG_BOOL bEnablePd;
	struct clk *pd_gpu_0;
	struct clk *pd_gpu_1;
#endif
	//struct clk              *aclk_gpu;
	struct clk *aclk_gpu_mem;
	struct clk *aclk_gpu_cfg;
	struct clk *sclk_gpu_core;
	struct regulator *gpu_reg;

	PVRSRV_DEVICE_NODE *psDeviceNode;
	RGXFWIF_GPU_UTIL_STATS sUtilStats;
	IMG_BOOL gpu_active;
	IMG_BOOL dvfs_enabled;

#if RK33_DVFS_SUPPORT
#if RK33_USE_CUSTOMER_GET_GPU_UTIL
	ktime_t time_period_start;
#endif

	/*Temperature */
	IMG_UINT32 temperature;
	IMG_UINT32 temperature_time;

#if USE_HRTIMER
	struct hrtimer timer;
#endif
	IMG_BOOL timer_active;

#if USE_KTHREAD
	/*dvfs kthread */
	struct task_struct *dvfs_task;
	wait_queue_head_t dvfs_wait;
#endif

	/*To calculate utilization for x sec */
	IMG_INT freq_level;
	IMG_INT freq;
	IMG_INT time_tick;
	struct rk_utilis stUtilis;
	IMG_INT utilisation;
	IMG_UINT32 time_busy;
	IMG_UINT32 time_idle;

#if RK33_USE_CL_COUNT_UTILS
	IMG_UINT32 abs_load[4];
#endif

#if RK33_SYSFS_FILE_SUPPORT
#if RK33_DVFS_FREQ_LIMIT
	IMG_INT up_level;
	IMG_INT down_level;
#endif				//end of RK33_DVFS_FREQ_LIMIT
	IMG_INT debug_level;
	IMG_UINT fps_gap;
	IMG_INT fix_freq;
#endif				//end of RK33_SYSFS_FILE_SUPPORT

#endif				//end of RK33_DVFS_SUPPORT
};

IMG_VOID RgxRkInit(IMG_VOID);
IMG_VOID RgxRkUnInit(IMG_VOID);
IMG_VOID RgxResume(IMG_VOID);
IMG_VOID RgxSuspend(IMG_VOID);

IMG_BOOL rk33_dvfs_init(IMG_VOID);
IMG_VOID rk33_dvfs_term(IMG_VOID);

#if RK33_DVFS_SUPPORT && RK33_USE_RGX_GET_GPU_UTIL
IMG_BOOL rk33_set_device_node(IMG_HANDLE hDevCookie);
IMG_BOOL rk33_clear_device_node(IMG_VOID);
#endif

PVRSRV_ERROR IonInit(void *pvPrivateData);
IMG_VOID IonDeinit(IMG_VOID);
PVRSRV_ERROR RkPrePowerState(PVRSRV_DEV_POWER_STATE eNewPowerState,
			     PVRSRV_DEV_POWER_STATE eCurrentPowerState,
			     IMG_BOOL bForced);
PVRSRV_ERROR RkPostPowerState(PVRSRV_DEV_POWER_STATE eNewPowerState,
			      PVRSRV_DEV_POWER_STATE eCurrentPowerState,
			      IMG_BOOL bForced);
#endif /* __SUNXI_INIT__ */
