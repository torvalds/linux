/*
 * Copyright (C) 2010, 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_UTGARD_H__
#define	__MALI_UTGARD_H__ 

/** @brief MALI GPU power down using MALI in-built PMU
 * 
 * called to power down all cores 
 */
int mali_pmu_powerdown(void);


/** @brief MALI GPU power up using MALI in-built PMU
 * 
 * called to power up all cores 
 */
int mali_pmu_powerup(void);

#endif /* __MALI_UTGARD_H__ */

