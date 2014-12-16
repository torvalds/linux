/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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


	if (CPU_CLOCK_SPEED_UNDEFINED != cpu_clock_speed)
	{
		*cpu_clock = cpu_clock_speed;
		return 0;
	}
	else
	{
		int result = 0;
		u32 reg_val = 0;
		u32 osc2_value = 0;
		u32 pa_divide = 0;
		u32 pb_divide = 0;
		u32 pc_divide = 0;
		void __iomem *pSysCfgReg = NULL;
		void __iomem *pSCCReg = NULL;

		/* Init the value case something goes wrong */
		*cpu_clock = 0;

		/* Map CPU register into virtual memory */
		pSysCfgReg = ioremap(MOTHERBOARD_SYS_CFG_START, 0x1000);
		if (pSysCfgReg == NULL) {
			result = 1;

			goto pSysCfgReg_map_failed;
		}

		pSCCReg = ioremap(CORETILE_EXPRESS_A9X4_SCC_START, 0x1000);
		if (pSCCReg == NULL) {
			result = 1;

			goto pSCCReg_map_failed;
		}

		raw_spin_lock(&syscfg_lock);

		/*Read SYS regs - OSC2 */
		reg_val = readl(pSysCfgReg + SYS_CFGCTRL_OFFSET);

		/*Verify if there is no other undergoing request */
		if (!(reg_val & SYS_CFGCTRL_START_BIT_VALUE)) {
			/*Reset the CGFGSTAT reg */
			writel(0, (pSysCfgReg + SYS_CFGSTAT_OFFSET));

			writel(SYS_CFGCTRL_START_BIT_VALUE | READ_REG_BIT_VALUE | DCC_DEFAULT_BIT_VALUE | SYS_CFG_OSC_FUNC_BIT_VALUE | SITE_DEFAULT_BIT_VALUE | BOARD_STACK_POS_DEFAULT_BIT_VALUE | DEVICE_DEFAULT_BIT_VALUE, (pSysCfgReg + SYS_CFGCTRL_OFFSET));
			/* Wait for the transaction to complete */
			while (!(readl(pSysCfgReg + SYS_CFGSTAT_OFFSET) & SYS_CFG_COMPLETE_BIT_VALUE))
				;
			/* Read SYS_CFGSTAT Register to get the status of submitted transaction */
			reg_val = readl(pSysCfgReg + SYS_CFGSTAT_OFFSET);

			/*------------------------------------------------------------------------------------------*/
			/* Check for possible errors */
			if (reg_val & SYS_CFG_ERROR_BIT_VALUE) {
				/* Error while setting register */
				result = 1;
			} else {
				osc2_value = readl(pSysCfgReg + SYS_CFGDATA_OFFSET);
				/* Read the SCC CFGRW0 register */
				reg_val = readl(pSCCReg);

				/*
				   Select the appropriate feed:
				   CFGRW0[0] - CLKOB
				   CFGRW0[1] - CLKOC
				   CFGRW0[2] - FACLK (CLK)B FROM AXICLK PLL)
				 */
				/* Calculate the  FCLK */
				if (IS_SINGLE_BIT_SET(reg_val, 0)) {	/*CFGRW0[0] - CLKOB */
					/* CFGRW0[6:3] */
					pa_divide = ((reg_val & (FEED_REG_BIT_MASK << FCLK_PA_DIVIDE_BIT_SHIFT)) >> FCLK_PA_DIVIDE_BIT_SHIFT);
					/* CFGRW0[10:7] */
					pb_divide = ((reg_val & (FEED_REG_BIT_MASK << FCLK_PB_DIVIDE_BIT_SHIFT)) >> FCLK_PB_DIVIDE_BIT_SHIFT);
					*cpu_clock = osc2_value * (pa_divide + 1) / (pb_divide + 1);
				} else {
					if (IS_SINGLE_BIT_SET(reg_val, 1)) {	/*CFGRW0[1] - CLKOC */
						/* CFGRW0[6:3] */
						pa_divide = ((reg_val & (FEED_REG_BIT_MASK << FCLK_PA_DIVIDE_BIT_SHIFT)) >> FCLK_PA_DIVIDE_BIT_SHIFT);
						/* CFGRW0[14:11] */
						pc_divide = ((reg_val & (FEED_REG_BIT_MASK << FCLK_PC_DIVIDE_BIT_SHIFT)) >> FCLK_PC_DIVIDE_BIT_SHIFT);
						*cpu_clock = osc2_value * (pa_divide + 1) / (pc_divide + 1);
					} else if (IS_SINGLE_BIT_SET(reg_val, 2)) {	/*CFGRW0[2] - FACLK */
						/* CFGRW0[18:15] */
						pa_divide = ((reg_val & (FEED_REG_BIT_MASK << AXICLK_PA_DIVIDE_BIT_SHIFT)) >> AXICLK_PA_DIVIDE_BIT_SHIFT);
						/* CFGRW0[22:19] */
						pb_divide = ((reg_val & (FEED_REG_BIT_MASK << AXICLK_PB_DIVIDE_BIT_SHIFT)) >> AXICLK_PB_DIVIDE_BIT_SHIFT);
						*cpu_clock = osc2_value * (pa_divide + 1) / (pb_divide + 1);
					} else {
						result = 1;
					}
				}
			}
		} else {
			result = 1;
		}
		raw_spin_unlock(&syscfg_lock);
		/* Convert result expressed in Hz to Mhz units. */
		*cpu_clock /= HZ_IN_MHZ;
		if (!result)
		{
			cpu_clock_speed = *cpu_clock;
		}

		/* Unmap memory */
		iounmap(pSCCReg);

	 pSCCReg_map_failed:
		iounmap(pSysCfgReg);

	 pSysCfgReg_map_failed:

		return result;
	}
}
