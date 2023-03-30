/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_MACROS_H_
#define _XE_MACROS_H_

#include <linux/bug.h>

#define XE_EXTRA_DEBUG 1
#define XE_WARN_ON WARN_ON
#define XE_BUG_ON BUG_ON

#define XE_IOCTL_ERR(xe, cond) \
	((cond) && (drm_info(&(xe)->drm, \
			    "Ioctl argument check failed at %s:%d: %s", \
			    __FILE__, __LINE__, #cond), 1))

#endif
