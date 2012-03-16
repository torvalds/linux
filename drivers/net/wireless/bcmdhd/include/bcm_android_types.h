/*
 * Android related remote wl declarations
 *
 * Copyright (C) 2012, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 * $Id: bcm_android_types.h 241182 2011-02-17 21:50:03Z $
 *
 */
#ifndef _wlu_android_h
#define _wlu_android_h
#define  __fd_mask unsigned long
typedef struct
	{
	
#ifdef __USE_XOPEN
    __fd_mask fds_bits[__FD_SETSIZE / __NFDBITS];
# define __FDS_BITS(set) ((set)->fds_bits)
#else
    __fd_mask __fds_bits[__FD_SETSIZE / __NFDBITS];
# define __FDS_BITS(set) ((set)->__fds_bits)
#endif
	} fd_set1;
#define fd_set fd_set1

#define htons(x) BCMSWAP16(x)
#define htonl(x) BCMSWAP32(x)
#define __FD_ZERO(s) \
	do {                                                                        \
    unsigned int __i;                                                         \
    fd_set *__arr = (s);                                                      \
    for (__i = 0; __i < sizeof (fd_set) / sizeof (__fd_mask); ++__i)          \
	__FDS_BITS(__arr)[__i] = 0;                                            \
	} while (0)
#define __FD_SET(d, s)     (__FDS_BITS (s)[__FDELT(d)] |= __FDMASK(d))
#define __FD_CLR(d, s)     (__FDS_BITS (s)[__FDELT(d)] &= ~__FDMASK(d))
#define __FD_ISSET(d, s)   ((__FDS_BITS (s)[__FDELT(d)] & __FDMASK(d)) != 0)
#define MCL_CURRENT 1
#define MCL_FUTURE 2
#endif 
