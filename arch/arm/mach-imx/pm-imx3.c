/*
 *  Copyright (C) 2012 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/io.h>

#include "common.h"
#include "crmregs-imx3.h"
#include "devices/devices-common.h"
#include "hardware.h"

/*
 * Set cpu low power mode before WFI instruction. This function is called
 * mx3 because it can be used for mx31 and mx35.
 * Currently only WAIT_MODE is supported.
 */
void mx3_cpu_lp_set(enum mx3_cpu_pwr_mode mode)
{
	int reg = __raw_readl(mx3_ccm_base + MXC_CCM_CCMR);
	reg &= ~MXC_CCM_CCMR_LPM_MASK;

	switch (mode) {
	case MX3_WAIT:
		if (cpu_is_mx35())
			reg |= MXC_CCM_CCMR_LPM_WAIT_MX35;
		__raw_writel(reg, mx3_ccm_base + MXC_CCM_CCMR);
		break;
	default:
		pr_err("Unknown cpu power mode: %d\n", mode);
		return;
	}
}
