/*
 * debug.h - ChipIdea USB driver debug interfaces
 *
 * Copyright (C) 2008 Chipidea - MIPS Technologies, Inc. All rights reserved.
 *
 * Author: David Lopo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DRIVERS_USB_CHIPIDEA_DEBUG_H
#define __DRIVERS_USB_CHIPIDEA_DEBUG_H

#ifdef CONFIG_USB_CHIPIDEA_DEBUG
void dbg_interrupt(u32 intmask);
void dbg_done(u8 addr, const u32 token, int status);
void dbg_event(u8 addr, const char *name, int status);
void dbg_queue(u8 addr, const struct usb_request *req, int status);
void dbg_setup(u8 addr, const struct usb_ctrlrequest *req);
int dbg_create_files(struct device *dev);
int dbg_remove_files(struct device *dev);
#else
static inline void dbg_interrupt(u32 intmask)
{
}

static inline void dbg_done(u8 addr, const u32 token, int status)
{
}

static inline void dbg_event(u8 addr, const char *name, int status)
{
}

static inline void dbg_queue(u8 addr, const struct usb_request *req, int status)
{
}

static inline void dbg_setup(u8 addr, const struct usb_ctrlrequest *req)
{
}

static inline int dbg_create_files(struct device *dev)
{
	return 0;
}

static inline int dbg_remove_files(struct device *dev)
{
	return 0;
}
#endif

#endif /* __DRIVERS_USB_CHIPIDEA_DEBUG_H */
