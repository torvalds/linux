/****************************************************************************
*
*    Copyright (c) 2005 - 2010 by Vivante Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Vivante Corporation.
*
*****************************************************************************
*
*    Auto-generated file on 12/8/2010. Do not edit!!!
*
*****************************************************************************/




#ifndef __gc_hal_kernel_qnx_h_
#define __gc_hal_kernel_qnx_h_

#define _QNX_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <sys/procmgr.h>
#include <sys/memmsg.h>
#include <fcntl.h>
#include <sys/syspage.h>
#include <hw/inout.h>
#include <atomic.h>

#define NTSTRSAFE_NO_CCH_FUNCTIONS
#include "gc_hal.h"
#include "gc_hal_driver.h"
#include "gc_hal_kernel.h"
#include "gc_hal_kernel_device.h"
#include "gc_hal_kernel_os.h"
#include "../inc/gc_hal_common_qnx.h"

#define _WIDE(string)				L##string
#define WIDE(string)				_WIDE(string)

#define countof(a)					(sizeof(a) / sizeof(a[0]))

#ifndef GAL_DEV
#define GAL_DEV	"/dev/galcore"
#endif

#endif /* __gc_hal_kernel_qnx_h_ */

