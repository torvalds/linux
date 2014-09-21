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

#define EXPORT_SYMTAB
#include <linux/kernel.h>
#ifdef CONFIG_MODVERSIONS
#include <config/modversions.h>
#endif
#include <linux/module.h>
#include <linux/init.h>		/* for module_init and module_exit */
#include <linux/slab.h>		/* for memcpy */
#include <linux/types.h>

#include "channel.h"
#include "chanstub.h"
#include "timskmodutils.h"
#include "version.h"

static __init int
channel_mod_init(void)
{
	if (!unisys_spar_platform)
		return -ENODEV;
	return 0;
}

static __exit void
channel_mod_exit(void)
{
}

unsigned char
SignalInsert_withLock(CHANNEL_HEADER __iomem *pChannel, u32 Queue,
		      void *pSignal, spinlock_t *lock)
{
	unsigned char result;
	unsigned long flags;

	spin_lock_irqsave(lock, flags);
	result = visor_signal_insert(pChannel, Queue, pSignal);
	spin_unlock_irqrestore(lock, flags);
	return result;
}

unsigned char
SignalRemove_withLock(CHANNEL_HEADER __iomem *pChannel, u32 Queue,
		      void *pSignal, spinlock_t *lock)
{
	unsigned char result;

	spin_lock(lock);
	result = visor_signal_remove(pChannel, Queue, pSignal);
	spin_unlock(lock);
	return result;
}

module_init(channel_mod_init);
module_exit(channel_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bryan Glaudel");
MODULE_ALIAS("uischan");
	/* this is extracted during depmod and kept in modules.dep */
