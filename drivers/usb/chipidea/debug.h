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
int dbg_create_files(struct ci13xxx *ci);
void dbg_remove_files(struct ci13xxx *ci);
#else
static inline int dbg_create_files(struct ci13xxx *ci)
{
	return 0;
}

static inline void dbg_remove_files(struct ci13xxx *ci)
{
}
#endif

#endif /* __DRIVERS_USB_CHIPIDEA_DEBUG_H */
