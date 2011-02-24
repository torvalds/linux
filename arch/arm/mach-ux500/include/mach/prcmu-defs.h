/*
 * Copyright (C) STMicroelectronics 2009
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Sundar Iyer <sundar.iyer@stericsson.com>
 * Author: Martin Persson <martin.persson@stericsson.com>
 *
 * License Terms: GNU General Public License v2
 *
 * PRCM Unit definitions
 */

#ifndef __MACH_PRCMU_DEFS_H
#define __MACH_PRCMU_DEFS_H

enum prcmu_cpu_opp {
	CPU_OPP_INIT	  = 0x00,
	CPU_OPP_NO_CHANGE = 0x01,
	CPU_OPP_100	  = 0x02,
	CPU_OPP_50	  = 0x03,
	CPU_OPP_MAX	  = 0x04,
	CPU_OPP_EXT_CLK	  = 0x07
};
enum prcmu_ape_opp {
	APE_OPP_NO_CHANGE = 0x00,
	APE_OPP_100	  = 0x02,
	APE_OPP_50	  = 0x03,
};

#endif /* __MACH_PRCMU_DEFS_H */
