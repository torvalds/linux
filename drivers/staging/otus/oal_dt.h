/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*  Module Name : oal_dt.h                                              */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains data type definition.                      */
/*                                                                      */
/*  NOTES                                                               */
/*      Platform dependent.                                             */
/*                                                                      */
/************************************************************************/

#ifndef _OAL_DT_H
#define _OAL_DT_H

/* Please include header files for buffer type in the beginning of this file */
/* Please include header files for device type here */
#include <linux/netdevice.h>

typedef     unsigned long long  u64_t;
typedef     unsigned int        u32_t;
typedef     unsigned short      u16_t;
typedef     unsigned char       u8_t;
typedef     long long           s64_t;
typedef     long                s32_t;
typedef     short               s16_t;
typedef     char                s8_t;

#ifndef     TRUE
#define     TRUE                (1==1)
#endif

#ifndef     FALSE
#define     FALSE               (1==0)
#endif

#ifndef     NULL
#define     NULL                0
#endif

/* Please include header files for buffer type in the beginning of this file */
typedef     struct sk_buff      zbuf_t;

/* Please include header files for device type in the beginning of this file */
typedef     struct net_device   zdev_t;

#endif /* #ifndef _OAL_DT_H */
