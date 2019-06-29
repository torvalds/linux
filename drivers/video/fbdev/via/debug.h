/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 */
#ifndef __DEBUG_H__
#define __DEBUG_H__

#ifndef VIAFB_DEBUG
#define VIAFB_DEBUG 0
#endif

#if VIAFB_DEBUG
#define DEBUG_MSG(f, a...)   printk(f, ## a)
#else
#define DEBUG_MSG(f, a...)
#endif

#define VIAFB_WARN 0
#if VIAFB_WARN
#define WARN_MSG(f, a...)   printk(f, ## a)
#else
#define WARN_MSG(f, a...)
#endif

#endif /* __DEBUG_H__ */
