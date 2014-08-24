/* Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __CHANSTUB_H__
#define __CHANSTUB_H__
unsigned char SignalInsert_withLock(CHANNEL_HEADER __iomem *pChannel, u32 Queue,
				     void *pSignal, spinlock_t *lock);
unsigned char SignalRemove_withLock(CHANNEL_HEADER __iomem *pChannel, u32 Queue,
				     void *pSignal, spinlock_t *lock);

#endif
