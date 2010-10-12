/*
 * pwr_sh.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Power Manager shared definitions (used on both GPP and DSP sides).
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef PWR_SH_
#define PWR_SH_

#include <dspbridge/mbx_sh.h>

/* valid sleep command codes that can be sent by GPP via mailbox: */
#define PWR_DEEPSLEEP           MBX_PM_DSPIDLE
#define PWR_EMERGENCYDEEPSLEEP  MBX_PM_EMERGENCYSLEEP
#define PWR_SLEEPUNTILRESTART   MBX_PM_SLEEPUNTILRESTART
#define PWR_WAKEUP              MBX_PM_DSPWAKEUP
#define PWR_AUTOENABLE          MBX_PM_PWRENABLE
#define PWR_AUTODISABLE         MBX_PM_PWRDISABLE
#define PWR_RETENTION             MBX_PM_DSPRETN

#endif /* PWR_SH_ */
