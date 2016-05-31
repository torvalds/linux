/*
 *   Driver for KeyStream 11b/g wireless LAN cards.
 *   
 *   ks_debug.h
 *   $Id: ks_debug.h 991 2009-09-14 01:38:58Z sekine $
 *
 *   Copyright (C) 2005-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it undr the terms of the GNU General Public License version 2 as
 *   published by the Free Sotware Foundation.
 */

#ifndef _KS_DEBUG_H
#define _KS_DEBUG_H

#include <linux/kernel.h>


#ifdef KS_WLAN_DEBUG
#define DPRINTK(n, fmt, args...) \
                 if (KS_WLAN_DEBUG>(n)) printk(KERN_NOTICE "%s: "fmt, __FUNCTION__, ## args)
#else
#define DPRINTK(n, fmt, args...)
#endif

extern void print_buffer(unsigned char *p, int size);

#endif /* _KS_DEBUG_H */
