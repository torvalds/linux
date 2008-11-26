#ifndef ____ASM_ARCH_SDRC_H
#define ____ASM_ARCH_SDRC_H

/*
 * OMAP2/3 SDRC/SMS register definitions
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Copyright (C) 2007 Nokia Corporation
 *
 * Written by Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <mach/io.h>

/* SDRC register offsets - read/write with sdrc_{read,write}_reg() */

#define SDRC_SYSCONFIG		0x010
#define SDRC_DLLA_CTRL		0x060
#define SDRC_DLLA_STATUS	0x064
#define SDRC_DLLB_CTRL		0x068
#define SDRC_DLLB_STATUS	0x06C
#define SDRC_POWER		0x070
#define SDRC_MR_0		0x084
#define SDRC_ACTIM_CTRL_A_0	0x09c
#define SDRC_ACTIM_CTRL_B_0	0x0a0
#define SDRC_RFR_CTRL_0		0x0a4

/*
 * These values represent the number of memory clock cycles between
 * autorefresh initiation.  They assume 1 refresh per 64 ms (JEDEC), 8192
 * rows per device, and include a subtraction of a 50 cycle window in the
 * event that the autorefresh command is delayed due to other SDRC activity.
 * The '| 1' sets the ARE field to send one autorefresh when the autorefresh
 * counter reaches 0.
 *
 * These represent optimal values for common parts, it won't work for all.
 * As long as you scale down, most parameters are still work, they just
 * become sub-optimal. The RFR value goes in the opposite direction. If you
 * don't adjust it down as your clock period increases the refresh interval
 * will not be met. Setting all parameters for complete worst case may work,
 * but may cut memory performance by 2x. Due to errata the DLLs need to be
 * unlocked and their value needs run time calibration.	A dynamic call is
 * need for that as no single right value exists acorss production samples.
 *
 * Only the FULL speed values are given. Current code is such that rate
 * changes must be made at DPLLoutx2. The actual value adjustment for low
 * frequency operation will be handled by omap_set_performance()
 *
 * By having the boot loader boot up in the fastest L4 speed available likely
 * will result in something which you can switch between.
 */
#define SDRC_RFR_CTRL_165MHz	(0x00044c00 | 1)
#define SDRC_RFR_CTRL_133MHz	(0x0003de00 | 1)
#define SDRC_RFR_CTRL_100MHz	(0x0002da01 | 1)
#define SDRC_RFR_CTRL_110MHz	(0x0002da01 | 1) /* Need to calc */
#define SDRC_RFR_CTRL_BYPASS	(0x00005000 | 1) /* Need to calc */


/*
 * SMS register access
 */


#define OMAP242X_SMS_REGADDR(reg)	IO_ADDRESS(OMAP2420_SMS_BASE + reg)
#define OMAP243X_SMS_REGADDR(reg)	IO_ADDRESS(OMAP243X_SMS_BASE + reg)
#define OMAP343X_SMS_REGADDR(reg)	IO_ADDRESS(OMAP343X_SMS_BASE + reg)

/* SMS register offsets - read/write with sms_{read,write}_reg() */

#define SMS_SYSCONFIG		0x010
/* REVISIT: fill in other SMS registers here */

#endif
