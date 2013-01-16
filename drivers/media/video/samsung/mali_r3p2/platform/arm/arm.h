/* Copyright (C) 2010, 2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef ARM_H_
#define ARM_H_
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/pm.h>
#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#include <asm/io.h>

#include <mach/regs-pmu.h>

#include <linux/mali/mali_utgard.h>
#include "mali_kernel_common.h"


#define S5P_IRQ_OFFSET		(32)
#define S5P_IRQ(x)		((x) + S5P_IRQ_OFFSET)
#define IRQ_SPI(x)		S5P_IRQ(x+32)

#define IRQ_PPMMU0_3D		IRQ_SPI(118)
#define IRQ_PPMMU1_3D		IRQ_SPI(119)
#define IRQ_PPMMU2_3D		IRQ_SPI(120)
#define IRQ_PPMMU3_3D		IRQ_SPI(121)
#define IRQ_GPMMU_3D		IRQ_SPI(122)

#define IRQ_PP0_3D		IRQ_SPI(123)
#define IRQ_PP1_3D		IRQ_SPI(124)
#define IRQ_PP2_3D		IRQ_SPI(125)
#define IRQ_PP3_3D		IRQ_SPI(126)
#define IRQ_GP_3D		IRQ_SPI(127)
#define IRQ_PMU_3D		IRQ_SPI(117)

/* stuff from config.h */
#define MALI_BASE_ADDR	 0x13000000

/* for mmu and os memory */
#define MEM_BASE_ADDR    0x40000000
#define MEM_TOTAL_SIZE   0x40000000
#define MEM_MALI_OS_SIZE 0x40000000

/* for shared memory */
#define CONFIG_MALI_MEM_SIZE 256
#define MEM_MALI_SIZE 	 CONFIG_MALI_MEM_SIZE*1024*1024
#define MEM_MALI_BASE    0x80000000 - MEM_MALI_SIZE

/* clock definitions */
#define EXTXTALCLK_NAME 	"ext_xtal"
#define VPLLSRCCLK_NAME 	"vpll_src"
#define FOUTVPLLCLK_NAME	"fout_vpll"
#define SCLVPLLCLK_NAME 	"sclk_vpll"
#define GPUMOUT1CLK_NAME	"mout_g3d1"

#define MPLLCLK_NAME 		"mout_mpll"
#define GPUMOUT0CLK_NAME 	"mout_g3d0"
#define GPUCLK_NAME 		"sclk_g3d"
#define CLK_DIV_STAT_G3D 	0x1003C62C
#define CLK_DESC 		"clk-divider-status"

#define MALI_BOTTOMLOCK_VOL	900000

typedef struct mali_runtime_resumeTag {
	int clk;
	int vol;
} mali_runtime_resume_table;

mali_runtime_resume_table mali_runtime_resume = { 266, 900000 };

/* lock/unlock CPU freq by Mali */
extern int cpufreq_lock_by_mali(unsigned int freq);
extern void cpufreq_unlock_by_mali(void);

static struct clk *ext_xtal_clock = 0;
static struct clk *vpll_src_clock = 0;
static struct clk *fout_vpll_clock = 0;
static struct clk *sclk_vpll_clock = 0;

static struct clk *mpll_clock = 0;
static struct clk *mali_parent_clock = 0;
static struct clk *mali_clock = 0;

static unsigned int GPU_MHZ = 1000000;

#if defined(CONFIG_MALI_OVERCLOCK_533)
int mali_gpu_clk = 533;
int mali_gpu_vol = 1075000;
#elif defined(CONFIG_MALI_OVERCLOCK_640)
int mali_gpu_clk = 640;
int mali_gpu_vol = 1125000;
#elif defined(CONFIG_MALI_OVERCLOCK_733)
int mali_gpu_clk = 733;
int mali_gpu_vol = 1175000;
#elif defined(CONFIG_MALI_OVERCLOCK_800)
int mali_gpu_clk = 800;
int mali_gpu_vol = 1200000;
#else
int mali_gpu_clk = 440;
int mali_gpu_vol = 1050000;
#endif

#if MALI_DVFS_ENABLED
#define MALI_DVFS_DEFAULT_STEP 0
#endif
#if MALI_VOLTAGE_LOCK
int mali_lock_vol = 0;
static _mali_osk_atomic_t voltage_lock_status;
static mali_bool mali_vol_lock_flag = 0;
#endif

int gpu_power_state;
static int bPoweroff;

#ifdef CONFIG_REGULATOR
struct regulator {
	struct device *dev;
	struct list_head list;
	int uA_load;
	int min_uV;
	int max_uV;
	char *supply_name;
	struct device_attribute dev_attr;
	struct regulator_dev *rdev;
};

struct regulator *g3d_regulator=NULL;
#endif

#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,36)
extern struct platform_device s5pv310_device_pd[];
#else
extern struct platform_device exynos4_device_pd[];
#endif
#endif

mali_io_address clk_register_map = 0;

_mali_osk_lock_t *mali_dvfs_lock = 0;

/* platform setup for Exynos4412 */
static struct resource mali_gpu_resources_m400_mp4[] = {
		MALI_GPU_RESOURCES_MALI400_MP4(MALI_BASE_ADDR,
				IRQ_GP_3D,
				IRQ_GPMMU_3D,
				IRQ_PP0_3D,
				IRQ_PPMMU0_3D,
				IRQ_PP1_3D,
				IRQ_PPMMU1_3D,
				IRQ_PP2_3D,
				IRQ_PPMMU2_3D,
				IRQ_PP3_3D,
				IRQ_PPMMU3_3D) };

static void mali_platform_device_release(struct device *device);

static struct platform_device mali_gpu_device = {
		.name = MALI_GPU_NAME_UTGARD,
		.id = 0,
		.dev.release = mali_platform_device_release
};

static struct mali_gpu_device_data mali_gpu_data = {
		.shared_mem_size = MEM_MALI_SIZE,
/* FIXME: Framebuffer address is changing when using V4L2 virtual framebuffer
		.fb_start = MEM_BASE_ADDR,
		.fb_size  = MEM_TOTAL_SIZE,
		.dedicated_mem_start = CONFIG_MALI_MEM_SIZE*1024*1024,
		.dedicated_mem_size = MEM_MALI_BASE */
};

/* prototypes */
int mali_regulator_get_usecount(void);
void mali_regulator_disable(void);
void mali_regulator_enable(void);
void mali_regulator_set_voltage(int min_uV, int max_uV);
unsigned long mali_clk_get_rate(void);
mali_bool mali_clk_get(mali_bool bis_vpll);
void mali_clk_put(mali_bool binc_mali_clock);
mali_bool mali_clk_set_rate(unsigned int clk, unsigned int mhz);
mali_bool init_mali_clock(void);
mali_bool deinit_mali_clock(void);
_mali_osk_errcode_t enable_mali_clocks(void);
_mali_osk_errcode_t disable_mali_clocks(void);
void set_mali_parent_power_domain(struct platform_device* dev);
_mali_osk_errcode_t g3d_power_domain_control(int bpower_on);
_mali_osk_errcode_t mali_platform_init(void);
_mali_osk_errcode_t mali_platform_powerdown(u32 cores);
_mali_osk_errcode_t mali_platform_powerup(u32 cores);
void mali_gpu_utilization_handler(u32 utilization);
u32 pmu_get_power_up_down_info(void);
int mali_voltage_lock_push(int lock_vol);
int mali_voltage_lock_pop(void);
int mali_voltage_lock_init(void);
int mali_platform_device_register(void);
void mali_platform_device_unregister(void);
void mali_platform_device_release(struct device *device);

#endif /* ARM_H_ */
