/*
 *
 * (C) COPYRIGHT 2011-2013 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





#include <linux/io.h>
#include <mali_kbase.h>
#include "mali_kbase_cpu_vexpress.h"

#define HZ_IN_MHZ					    (1000000)

#define CORETILE_EXPRESS_A9X4_SCC_START	(0x100E2000)
#define MOTHERBOARD_SYS_CFG_START		(0x10000000)
#define SYS_CFGDATA_OFFSET				(0x000000A0)
#define SYS_CFGCTRL_OFFSET				(0x000000A4)
#define SYS_CFGSTAT_OFFSET				(0x000000A8)

#define SYS_CFGCTRL_START_BIT_VALUE		  (1 << 31)
#define READ_REG_BIT_VALUE				  (0 << 30)
#define DCC_DEFAULT_BIT_VALUE			  (0 << 26)
#define SYS_CFG_OSC_FUNC_BIT_VALUE		  (1 << 20)
#define SITE_DEFAULT_BIT_VALUE			  (1 << 16)
#define BOARD_STACK_POS_DEFAULT_BIT_VALUE (0 << 12)
#define DEVICE_DEFAULT_BIT_VALUE	      (2 <<  0)
#define SYS_CFG_COMPLETE_BIT_VALUE		  (1 <<  0)
#define SYS_CFG_ERROR_BIT_VALUE			  (1 <<  1)

#define FEED_REG_BIT_MASK				(0x0F)
#define FCLK_PA_DIVIDE_BIT_SHIFT		(0x03)
#define FCLK_PB_DIVIDE_BIT_SHIFT		(0x07)
#define FCLK_PC_DIVIDE_BIT_SHIFT		(0x0B)
#define AXICLK_PA_DIVIDE_BIT_SHIFT		(0x0F)
#define AXICLK_PB_DIVIDE_BIT_SHIFT		(0x13)

#define IS_SINGLE_BIT_SET(val, pos)		(val&(1<<pos))

#define CPU_CLOCK_SPEED_UNDEFINED 0

#define CPU_CLOCK_SPEED_6XV7 50

static u32 cpu_clock_speed = CPU_CLOCK_SPEED_UNDEFINED;

static DEFINE_RAW_SPINLOCK(syscfg_lock);
/**
 * kbase_get_vendor_specific_cpu_clock_speed
 * @brief  Retrieves the CPU clock speed.
 *         The implementation is platform specific.
 * @param[out]    cpu_clock - the value of CPU clock speed in MHz
 * @return        0 on success, 1 otherwise
*/
int kbase_get_vexpress_cpu_clock_speed(u32 *cpu_clock)
{
	/* TODO: MIDBASE-2873 - Provide runtime detection of CPU clock freq for 6XV7 board */
	*cpu_clock = CPU_CLOCK_SPEED_6XV7;

	return 0;
}
