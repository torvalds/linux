/*
 * Header file of MobiCore Driver Kernel Module.
 *
 * <-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 * <-- Copyright Trustonic Limited 2013 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MC_PM_H_
#define _MC_PM_H_

#include "main.h"
#ifdef MC_BL_NOTIFIER
#include <asm/bL_switcher.h>
#endif


#define NO_SLEEP_REQ	0
#define REQ_TO_SLEEP	1

#define NORMAL_EXECUTION	0
#define READY_TO_SLEEP		1

/* How much time after resume the daemon should backoff */
#define DAEMON_BACKOFF_TIME	500

/* Initialize Power Management */
int mc_pm_initialize(struct mc_context *context);
/* Free all Power Management resources*/
int mc_pm_free(void);

#endif /* _MC_PM_H_ */
