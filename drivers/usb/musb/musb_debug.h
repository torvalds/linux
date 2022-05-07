/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MUSB OTG driver debug defines
 *
 * Copyright 2005 Mentor Graphics Corporation
 * Copyright (C) 2005-2006 by Texas Instruments
 * Copyright (C) 2006-2007 Nokia Corporation
 */

#ifndef __MUSB_LINUX_DEBUG_H__
#define __MUSB_LINUX_DEBUG_H__

#define yprintk(facility, format, args...) \
	do { printk(facility "%s %d: " format , \
	__func__, __LINE__ , ## args); } while (0)
#define WARNING(fmt, args...) yprintk(KERN_WARNING, fmt, ## args)
#define INFO(fmt, args...) yprintk(KERN_INFO, fmt, ## args)
#define ERR(fmt, args...) yprintk(KERN_ERR, fmt, ## args)

void musb_dbg(struct musb *musb, const char *fmt, ...);

#ifdef CONFIG_DEBUG_FS
void musb_init_debugfs(struct musb *musb);
void musb_exit_debugfs(struct musb *musb);
#else
static inline void musb_init_debugfs(struct musb *musb)
{
}
static inline void musb_exit_debugfs(struct musb *musb)
{
}
#endif

#endif				/*  __MUSB_LINUX_DEBUG_H__ */
