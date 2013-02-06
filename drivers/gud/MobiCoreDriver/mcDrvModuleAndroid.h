/**
 * Header file of MobiCore Driver Kernel Module.
 *
 * @addtogroup MobiCore_Driver_Kernel_Module
 * @{
 * Android specific defines
 * @file
 *
 * Android specific defines
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _MC_DRV_MODULE_ANDROID_H_
#define _MC_DRV_MODULE_ANDROID_H_

/* Defines needed to identify the Daemon in Android systems
 * For the full list see:
 * platform_system_core/include/private/android_filesystem_config.h in the
 * Android source tree
 */
#define AID_ROOT	0		/* traditional unix root user */
#define AID_SYSTEM	1000	/* system server */
#define AID_MISC	9998	/* access to misc storage */
#define AID_NOBODY	9999
#define AID_APP		10000	/* first app user */

#endif /* _MC_DRV_MODULE_ANDROID_H_ */
/** @} */
