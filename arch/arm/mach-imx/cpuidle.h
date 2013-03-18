/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 * Copyright 2012 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/cpuidle.h>

#ifdef CONFIG_CPU_IDLE
extern int imx_cpuidle_init(struct cpuidle_driver *drv);
extern int imx6q_cpuidle_init(void);
#else
static inline int imx_cpuidle_init(struct cpuidle_driver *drv)
{
	return -ENODEV;
}
static inline int imx6q_cpuidle_init(void)
{
	return -ENODEV;
}
#endif
