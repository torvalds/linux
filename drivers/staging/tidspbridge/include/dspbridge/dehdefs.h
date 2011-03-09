/*
 * dehdefs.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Definition for Bridge driver module DEH.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef DEHDEFS_
#define DEHDEFS_

#include <dspbridge/mbx_sh.h>	/* shared mailbox codes */

/* DEH object manager */
struct deh_mgr;

/* Magic code used to determine if DSP signaled exception. */
#define DEH_BASE        MBX_DEH_BASE
#define DEH_USERS_BASE  MBX_DEH_USERS_BASE
#define DEH_LIMIT       MBX_DEH_LIMIT

#endif /* _DEHDEFS_H */
