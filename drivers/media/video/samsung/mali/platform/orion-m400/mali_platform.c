/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions for a default platform
 */
#include <linux/version.h>
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"
#include "mali_linux_pm.h"

#if USING_MALI_PMM
#include "mali_pmm.h"
#endif

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>

#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
#include <plat/pd.h>
#endif

#if MALI_TIMELINE_PROFILING_ENABLED
#include "mali_kernel_profiling.h"
#endif

#include <asm/io.h>
#include <mach/regs-pmu.h>

#define EXTXTALCLK_NAME 		"ext_xtal"
#define VPLLSRCCLK_NAME 		"vpll_src"
#define FOUTVPLLCLK_NAME 		"fout_vpll"
#define SCLVPLLCLK_NAME 		"sclk_vpll"
#define GPUMOUT1CLK_NAME 		"mout_g3d1"

#define MPLLCLK_NAME 		"mout_mpll"
#define GPUMOUT0CLK_NAME 	"mout_g3d0"
#define GPUCLK_NAME 		"sclk_g3d"
#define CLK_DIV_STAT_G3D 	0x1003C62C
#define CLK_DESC 		"clk-divider-status"

static struct clk               *ext_xtal_clock = 0;
static struct clk               *vpll_src_clock = 0;
static struct clk               *fout_vpll_clock = 0;
static struct clk               *sclk_vpll_clock = 0;

static struct clk               *mpll_clock = 0;
static struct clk               *mali_parent_clock = 0;
static struct clk               *mali_clock = 0;

int mali_gpu_clk 	=		160;
static unsigned int GPU_MHZ	=		1000000;
#ifdef CONFIG_S5PV310_ASV
int mali_gpu_vol     =               1100000;        /* 1.10V for ASV */
#else
int mali_gpu_vol     =               1100000;        /* 1.10V */
#endif

#if MALI_DVFS_ENABLED
#define MALI_DVFS_DEFAULT_STEP 0 // 134Mhz default
#endif

int  gpu_power_state;
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

mali_io_address clk_register_map=0;

#if MALI_GPU_BOTTOM_LOCK
_mali_osk_lock_t *mali_dvfs_lock;
#else
static _mali_osk_lock_t *mali_dvfs_lock;
#endif

#ifdef CONFIG_REGULATOR
int mali_regulator_get_usecount(void)
{
	struct regulator_dev *rdev;

	if( IS_ERR_OR_NULL(g3d_regulator) )
	{
		MALI_DEBUG_PRINT(1, ("error on mali_regulator_get_usecount : g3d_regulator is null\n"));
		return 0;
	}
	rdev = g3d_regulator->rdev;
	return rdev->use_count;
}

void mali_regulator_disable(void)
{
	bPoweroff = 1;
	if( IS_ERR_OR_NULL(g3d_regulator) )
	{
		MALI_DEBUG_PRINT(1, ("error on mali_regulator_disable : g3d_regulator is null\n"));
		return;
	}
	regulator_disable(g3d_regulator);
	MALI_DEBUG_PRINT(1, ("regulator_disable -> use cnt: %d \n",mali_regulator_get_usecount()));
}

void mali_regulator_enable(void)
{
	bPoweroff = 0;
	if( IS_ERR_OR_NULL(g3d_regulator) )
	{
		MALI_DEBUG_PRINT(1, ("error on mali_regulator_enable : g3d_regulator is null\n"));
		return;
	}
	regulator_enable(g3d_regulator);
	MALI_DEBUG_PRINT(1, ("regulator_enable -> use cnt: %d \n",mali_regulator_get_usecount()));
}

void mali_regulator_set_voltage(int min_uV, int max_uV)
{
	int voltage;

	_mali_osk_lock_wait(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);

	if( IS_ERR_OR_NULL(g3d_regulator) )
	{
		MALI_DEBUG_PRINT(1, ("error on mali_regulator_set_voltage : g3d_regulator is null\n"));
		return;
	}
	MALI_DEBUG_PRINT(2, ("= regulator_set_voltage: %d, %d \n",min_uV, max_uV));

#if MALI_TIMELINE_PROFILING_ENABLED
    _mali_profiling_add_event( MALI_PROFILING_EVENT_TYPE_SINGLE |
                               MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
                               MALI_PROFILING_EVENT_REASON_SINGLE_SW_GPU_VOLTS,
                               min_uV, max_uV, 0, 0, 0);
#endif

	regulator_set_voltage(g3d_regulator,min_uV,max_uV);
	voltage = regulator_get_voltage(g3d_regulator);

#if MALI_TIMELINE_PROFILING_ENABLED
    _mali_profiling_add_event( MALI_PROFILING_EVENT_TYPE_SINGLE |
                               MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
                               MALI_PROFILING_EVENT_REASON_SINGLE_SW_GPU_VOLTS,
                               voltage, 0, 1, 0, 0);
#endif
	mali_gpu_vol = voltage;
	MALI_DEBUG_PRINT(1, ("= regulator_get_voltage: %d \n",mali_gpu_vol));

	_mali_osk_lock_signal(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
}
#endif  

unsigned long mali_clk_get_rate(void)
{
	return clk_get_rate(mali_clock);
}

mali_bool mali_clk_get(mali_bool bis_vpll)
{
	if (bis_vpll == MALI_TRUE)
	{
		if (ext_xtal_clock == NULL)
		{
			ext_xtal_clock = clk_get(NULL,EXTXTALCLK_NAME);
			if (IS_ERR(ext_xtal_clock)) {
				MALI_PRINT( ("MALI Error : failed to get source ext_xtal_clock\n"));
				return MALI_FALSE;
			}
		}

		if (vpll_src_clock == NULL)
		{
			vpll_src_clock = clk_get(NULL,VPLLSRCCLK_NAME);
			if (IS_ERR(vpll_src_clock)) {
				MALI_PRINT( ("MALI Error : failed to get source vpll_src_clock\n"));
				return MALI_FALSE;
			}
		}

		if (fout_vpll_clock == NULL)
		{
			fout_vpll_clock = clk_get(NULL,FOUTVPLLCLK_NAME);
			if (IS_ERR(fout_vpll_clock)) {
				MALI_PRINT( ("MALI Error : failed to get source fout_vpll_clock\n"));
				return MALI_FALSE;
			}
		}

		if (sclk_vpll_clock == NULL)
		{
			sclk_vpll_clock = clk_get(NULL,SCLVPLLCLK_NAME);
			if (IS_ERR(sclk_vpll_clock)) {
				MALI_PRINT( ("MALI Error : failed to get source sclk_vpll_clock\n"));
				return MALI_FALSE;
			}
		}

		if (mali_parent_clock == NULL)
		{
			mali_parent_clock = clk_get(NULL, GPUMOUT1CLK_NAME);
		
			if (IS_ERR(mali_parent_clock)) {
				MALI_PRINT( ( "MALI Error : failed to get source mali parent clock\n"));
				return MALI_FALSE;
			}
		}
	}
	else // mpll
	{
		if (mpll_clock == NULL)
		{
			mpll_clock = clk_get(NULL,MPLLCLK_NAME);

			if (IS_ERR(mpll_clock)) {
				MALI_PRINT( ("MALI Error : failed to get source mpll clock\n"));
				return MALI_FALSE;
			}
		}

		if (mali_parent_clock == NULL)
		{
			mali_parent_clock = clk_get(NULL, GPUMOUT0CLK_NAME);
		
			if (IS_ERR(mali_parent_clock)) {
				MALI_PRINT( ( "MALI Error : failed to get source mali parent clock\n"));
				return MALI_FALSE;
			}
		}
	}

	// mali clock get always.
	if (mali_clock == NULL)
	{
		mali_clock = clk_get(NULL, GPUCLK_NAME);

		if (IS_ERR(mali_clock)) {
			MALI_PRINT( ("MALI Error : failed to get source mali clock\n"));
			return MALI_FALSE;
		}
	}

	return MALI_TRUE;
}

void mali_clk_put(mali_bool binc_mali_clock)
{
	if (mali_parent_clock)
	{
		clk_put(mali_parent_clock);
		mali_parent_clock = 0;
	}
	
	if (mpll_clock)
	{
		clk_put(mpll_clock);
		mpll_clock = 0;
	}

	if (sclk_vpll_clock)
	{
		clk_put(sclk_vpll_clock);
		sclk_vpll_clock = 0;
	}

	if (fout_vpll_clock)
	{
		clk_put(fout_vpll_clock);
		fout_vpll_clock = 0;
	}

	if (vpll_src_clock)
	{
		clk_put(vpll_src_clock);
		vpll_src_clock = 0;
	}
	
	if (ext_xtal_clock)
	{
		clk_put(ext_xtal_clock);
		ext_xtal_clock = 0;
	}

	if (binc_mali_clock == MALI_TRUE && mali_clock)
	{
		clk_put(mali_clock);
		mali_clock = 0;
	}

}


mali_bool mali_clk_set_rate(unsigned int clk, unsigned int mhz)
{ 
	unsigned long rate = 0;
	mali_bool bis_vpll = MALI_FALSE;

#ifdef CONFIG_VPLL_USE_FOR_TVENC
	bis_vpll = MALI_TRUE;
#endif

	_mali_osk_lock_wait(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);

	if (mali_clk_get(bis_vpll) == MALI_FALSE)
		return MALI_FALSE;
	
	rate = (unsigned long)clk * (unsigned long)mhz;
	MALI_DEBUG_PRINT(3,("= clk_set_rate : %d , %d \n",clk, mhz ));

	if (bis_vpll)
	{
		clk_set_rate(fout_vpll_clock, (unsigned int)clk * GPU_MHZ);
		clk_set_parent(vpll_src_clock, ext_xtal_clock);
		clk_set_parent(sclk_vpll_clock, fout_vpll_clock);

		clk_set_parent(mali_parent_clock, sclk_vpll_clock);
		clk_set_parent(mali_clock, mali_parent_clock);
	}
	else
	{
		clk_set_parent(mali_parent_clock, mpll_clock);
		clk_set_parent(mali_clock, mali_parent_clock);
	}

	if (clk_enable(mali_clock) < 0)
		return MALI_FALSE;

#if MALI_TIMELINE_PROFILING_ENABLED
    _mali_profiling_add_event( MALI_PROFILING_EVENT_TYPE_SINGLE |
                               MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
                               MALI_PROFILING_EVENT_REASON_SINGLE_SW_GPU_FREQ,
                               rate, 0, 0, 0, 0);
#endif

	clk_set_rate(mali_clock, rate);
	rate = clk_get_rate(mali_clock);

#if MALI_TIMELINE_PROFILING_ENABLED
    _mali_profiling_add_event( MALI_PROFILING_EVENT_TYPE_SINGLE |
                               MALI_PROFILING_EVENT_CHANNEL_SOFTWARE |
                               MALI_PROFILING_EVENT_REASON_SINGLE_SW_GPU_FREQ,
                               rate, 0, 0, 0, 0);
#endif

	if (bis_vpll)
		mali_gpu_clk = (int)(rate / mhz);
	else
		mali_gpu_clk = (int)((rate + 500000) / mhz);

	GPU_MHZ = mhz;
	MALI_DEBUG_PRINT(3,("= clk_get_rate: %d \n",mali_gpu_clk));

	mali_clk_put(MALI_FALSE);

	_mali_osk_lock_signal(mali_dvfs_lock, _MALI_OSK_LOCKMODE_RW);
	
	return MALI_TRUE;
}

static mali_bool init_mali_clock(void)
{
	mali_bool ret = MALI_TRUE;
	
	gpu_power_state = 0;

	if (mali_clock != 0)
		return ret; // already initialized

	mali_dvfs_lock = _mali_osk_lock_init(_MALI_OSK_LOCKFLAG_NONINTERRUPTABLE
			| _MALI_OSK_LOCKFLAG_ONELOCK, 0, 0);
	if (mali_dvfs_lock == NULL)
		return _MALI_OSK_ERR_FAULT;

	if (mali_clk_set_rate(mali_gpu_clk, GPU_MHZ) == MALI_FALSE)
	{
		ret = MALI_FALSE;
		goto err_clock_get;
	}

	MALI_PRINT(("init_mali_clock mali_clock %p \n", mali_clock));


#ifdef CONFIG_REGULATOR
#if USING_MALI_PMM
	g3d_regulator = regulator_get(&mali_gpu_device.dev, "vdd_g3d");
#else
	g3d_regulator = regulator_get(NULL, "vdd_g3d");
#endif

	if (IS_ERR(g3d_regulator)) 
	{
		MALI_PRINT( ("MALI Error : failed to get vdd_g3d\n"));
		ret = MALI_FALSE;
		goto err_regulator;
	}

	regulator_enable(g3d_regulator);
	MALI_DEBUG_PRINT(1, ("= regulator_enable -> use cnt: %d \n",mali_regulator_get_usecount()));
	mali_regulator_set_voltage(mali_gpu_vol, mali_gpu_vol);
#endif

	MALI_DEBUG_PRINT(2, ("MALI Clock is set at mali driver\n"));
	

	MALI_DEBUG_PRINT(3,("::clk_put:: %s mali_parent_clock - normal\n", __FUNCTION__));
	MALI_DEBUG_PRINT(3,("::clk_put:: %s mpll_clock  - normal\n", __FUNCTION__));

	mali_clk_put(MALI_FALSE);

	return MALI_TRUE;


#ifdef CONFIG_REGULATOR
err_regulator:
	regulator_put(g3d_regulator);
#endif
	
err_clock_get:
	mali_clk_put(MALI_TRUE);

	return ret;
}

static mali_bool deinit_mali_clock(void)
{
	if (mali_clock == 0)
		return MALI_TRUE;

#ifdef CONFIG_REGULATOR
	if (g3d_regulator) 
	{
		regulator_put(g3d_regulator);
		g3d_regulator=NULL;
	}
#endif

	mali_clk_put(MALI_TRUE);

	return MALI_TRUE;
}


static _mali_osk_errcode_t enable_mali_clocks(void)
{
	int err;
	err = clk_enable(mali_clock);
	MALI_DEBUG_PRINT(3,("enable_mali_clocks mali_clock %p error %d \n", mali_clock, err));

	// set clock rate
	mali_clk_set_rate(mali_gpu_clk, GPU_MHZ);
	
	MALI_SUCCESS;
}

static _mali_osk_errcode_t disable_mali_clocks(void)
{
	clk_disable(mali_clock);
	MALI_DEBUG_PRINT(3,("disable_mali_clocks mali_clock %p \n", mali_clock));
	
	MALI_SUCCESS;
}

void set_mali_parent_power_domain(struct platform_device* dev)
{
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,36)
	dev->dev.parent = &s5pv310_device_pd[PD_G3D].dev;
#else
	dev->dev.parent = &exynos4_device_pd[PD_G3D].dev;
#endif

#endif
}

_mali_osk_errcode_t g3d_power_domain_control(int bpower_on)
{
	if (bpower_on)
	{
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
		MALI_DEBUG_PRINT(3,("_mali_osk_pmm_dev_activate \n"));
		_mali_osk_pmm_dev_activate();
#else //MALI_PMM_RUNTIME_JOB_CONTROL_ON
		void __iomem *status;
		u32 timeout;
		__raw_writel(S5P_INT_LOCAL_PWR_EN, S5P_PMU_G3D_CONF);
                status = S5P_PMU_G3D_CONF + 0x4;

		timeout = 10;
		while ((__raw_readl(status) & S5P_INT_LOCAL_PWR_EN)
			!= S5P_INT_LOCAL_PWR_EN) {
			if (timeout == 0) {
				MALI_PRINTF(("Power domain  enable failed.\n"));
				return -ETIMEDOUT;
			}
			timeout--;
			_mali_osk_time_ubusydelay(100);
        }
#endif //MALI_PMM_RUNTIME_JOB_CONTROL_ON
	}
	else
	{
#if MALI_PMM_RUNTIME_JOB_CONTROL_ON
		MALI_DEBUG_PRINT( 4,("_mali_osk_pmm_dev_idle\n"));
		_mali_osk_pmm_dev_idle();

#else //MALI_PMM_RUNTIME_JOB_CONTROL_ON
		void __iomem *status;
		u32 timeout;
		__raw_writel(0, S5P_PMU_G3D_CONF);
		
		status = S5P_PMU_G3D_CONF + 0x4;
		/* Wait max 1ms */
		timeout = 10;
		while (__raw_readl(status) & S5P_INT_LOCAL_PWR_EN) 
		{
			if (timeout == 0) {
				MALI_PRINTF(("Power domain  disable failed.\n" ));
        	                	return -ETIMEDOUT;
			}
			timeout--;
			_mali_osk_time_ubusydelay( 100);
		}
#endif //MALI_PMM_RUNTIME_JOB_CONTROL_ON
	}

    MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_init()
{
	MALI_CHECK(init_mali_clock(), _MALI_OSK_ERR_FAULT);
#if MALI_DVFS_ENABLED
	if (!clk_register_map) clk_register_map = _mali_osk_mem_mapioregion( CLK_DIV_STAT_G3D, 0x20, CLK_DESC );
	if(!init_mali_dvfs_status(MALI_DVFS_DEFAULT_STEP))
		MALI_DEBUG_PRINT(1, ("mali_platform_init failed\n"));        
#endif

    MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_deinit()
{
	deinit_mali_clock();

#if MALI_DVFS_ENABLED
	deinit_mali_dvfs_status();
	if (clk_register_map )
	{
		_mali_osk_mem_unmapioregion(CLK_DIV_STAT_G3D, 0x20, clk_register_map);
		clk_register_map=0;
	}
#endif

    MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_powerdown(u32 cores)
{
	MALI_DEBUG_PRINT(3,("power down is called in mali_platform_powerdown state %x core %x \n", gpu_power_state, cores));

	if (gpu_power_state != 0) // power down after state is 0
	{
		gpu_power_state = gpu_power_state & (~cores);
		if (gpu_power_state == 0)
		{
			MALI_DEBUG_PRINT( 3,("disable clock\n"));
			disable_mali_clocks();			
		}
	}
	else
	{
		MALI_PRINT(("mali_platform_powerdown gpu_power_state == 0 and cores %x \n", cores));
	}

	bPoweroff=1;



    MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_powerup(u32 cores)
{
	MALI_DEBUG_PRINT(3,("power up is called in mali_platform_powerup state %x core %x \n", gpu_power_state, cores));

	if (gpu_power_state == 0) // power up only before state is 0
	{
		gpu_power_state = gpu_power_state | cores;

		if (gpu_power_state != 0)
		{
			MALI_DEBUG_PRINT(4,("enable clock \n"));
			enable_mali_clocks();
		}
	}
	else
	{
		gpu_power_state = gpu_power_state | cores;
	}
	
  	bPoweroff=0;


	MALI_SUCCESS;
}

void mali_gpu_utilization_handler(u32 utilization)
{
	if (bPoweroff==0) 
	{
#if MALI_DVFS_ENABLED
		if(!mali_dvfs_handler(utilization))
			MALI_DEBUG_PRINT(1,( "error on mali dvfs status in utilization\n"));
#endif
	}
}

#if MALI_POWER_MGMT_TEST_SUITE
u32 pmu_get_power_up_down_info(void)
{
	return 4095;
}

#endif
_mali_osk_errcode_t mali_platform_power_mode_change(mali_power_mode power_mode)
{
    MALI_SUCCESS;
}

