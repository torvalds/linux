/*
 * Copyright 2008  by Andreas Eversberg <andreas@eversberg.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Quick API description:
 *
 * A clock source registers using mISDN_register_clock:
 * 	name = text string to name clock source
 *	priority = value to priorize clock sources (0 = default)
 *	ctl = callback function to enable/disable clock source
 *	priv = private pointer of clock source
 * 	return = pointer to clock source structure;
 *
 * Note: Callback 'ctl' can be called before mISDN_register_clock returns!
 *       Also it can be called during mISDN_unregister_clock.
 *
 * A clock source calls mISDN_clock_update with given samples elapsed, if
 * enabled. If function call is delayed, tv must be set with the timestamp
 * of the actual event.
 *
 * A clock source unregisters using mISDN_unregister_clock.
 *
 * To get current clock, call mISDN_clock_get. The signed short value
 * counts the number of samples since. Time since last clock event is added.
 *
 */

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/mISDNif.h>
#include "core.h"

static u_int *debug;
static LIST_HEAD(iclock_list);
static DEFINE_RWLOCK(iclock_lock);
static u16 iclock_count;		/* counter of last clock */
static struct timeval iclock_tv;	/* time stamp of last clock */
static int iclock_tv_valid;		/* already received one timestamp */
static struct mISDNclock *iclock_current;

void
mISDN_init_clock(u_int *dp)
{
	debug = dp;
	do_gettimeofday(&iclock_tv);
}

static void
select_iclock(void)
{
	struct mISDNclock *iclock, *bestclock = NULL, *lastclock = NULL;
	int pri = -128;

	list_for_each_entry(iclock, &iclock_list, list) {
		if (iclock->pri > pri) {
			pri = iclock->pri;
			bestclock = iclock;
		}
		if (iclock_current == iclock)
			lastclock = iclock;
	}
	if (lastclock && bestclock != lastclock) {
		/* last used clock source still exists but changes, disable */
		if (*debug & DEBUG_CLOCK)
			printk(KERN_DEBUG "Old clock source '%s' disable.\n",
				lastclock->name);
		lastclock->ctl(lastclock->priv, 0);
	}
	if (bestclock && bestclock != iclock_current) {
		/* new clock source selected, enable */
		if (*debug & DEBUG_CLOCK)
			printk(KERN_DEBUG "New clock source '%s' enable.\n",
				bestclock->name);
		bestclock->ctl(bestclock->priv, 1);
	}
	if (bestclock != iclock_current) {
		/* no clock received yet */
		iclock_tv_valid = 0;
	}
	iclock_current = bestclock;
}

struct mISDNclock
*mISDN_register_clock(char *name, int pri, clockctl_func_t *ctl, void *priv)
{
	u_long			flags;
	struct mISDNclock	*iclock;

	if (*debug & (DEBUG_CORE | DEBUG_CLOCK))
		printk(KERN_DEBUG "%s: %s %d\n", __func__, name, pri);
	iclock = kzalloc(sizeof(struct mISDNclock), GFP_ATOMIC);
	if (!iclock) {
		printk(KERN_ERR "%s: No memory for clock entry.\n", __func__);
		return NULL;
	}
	strncpy(iclock->name, name, sizeof(iclock->name)-1);
	iclock->pri = pri;
	iclock->priv = priv;
	iclock->ctl = ctl;
	write_lock_irqsave(&iclock_lock, flags);
	list_add_tail(&iclock->list, &iclock_list);
	select_iclock();
	write_unlock_irqrestore(&iclock_lock, flags);
	return iclock;
}
EXPORT_SYMBOL(mISDN_register_clock);

void
mISDN_unregister_clock(struct mISDNclock *iclock)
{
	u_long	flags;

	if (*debug & (DEBUG_CORE | DEBUG_CLOCK))
		printk(KERN_DEBUG "%s: %s %d\n", __func__, iclock->name,
			iclock->pri);
	write_lock_irqsave(&iclock_lock, flags);
	if (iclock_current == iclock) {
		if (*debug & DEBUG_CLOCK)
			printk(KERN_DEBUG
				"Current clock source '%s' unregisters.\n",
				iclock->name);
		iclock->ctl(iclock->priv, 0);
	}
	list_del(&iclock->list);
	select_iclock();
	write_unlock_irqrestore(&iclock_lock, flags);
}
EXPORT_SYMBOL(mISDN_unregister_clock);

void
mISDN_clock_update(struct mISDNclock *iclock, int samples, struct timeval *tv)
{
	u_long		flags;
	struct timeval	tv_now;
	time_t		elapsed_sec;
	int		elapsed_8000th;

	write_lock_irqsave(&iclock_lock, flags);
	if (iclock_current != iclock) {
		printk(KERN_ERR "%s: '%s' sends us clock updates, but we do "
			"listen to '%s'. This is a bug!\n", __func__,
			iclock->name,
			iclock_current ? iclock_current->name : "nothing");
		iclock->ctl(iclock->priv, 0);
		write_unlock_irqrestore(&iclock_lock, flags);
		return;
	}
	if (iclock_tv_valid) {
		/* increment sample counter by given samples */
		iclock_count += samples;
		if (tv) { /* tv must be set, if function call is delayed */
			iclock_tv.tv_sec = tv->tv_sec;
			iclock_tv.tv_usec = tv->tv_usec;
		} else
			do_gettimeofday(&iclock_tv);
	} else {
		/* calc elapsed time by system clock */
		if (tv) { /* tv must be set, if function call is delayed */
			tv_now.tv_sec = tv->tv_sec;
			tv_now.tv_usec = tv->tv_usec;
		} else
			do_gettimeofday(&tv_now);
		elapsed_sec = tv_now.tv_sec - iclock_tv.tv_sec;
		elapsed_8000th = (tv_now.tv_usec / 125)
			- (iclock_tv.tv_usec / 125);
		if (elapsed_8000th < 0) {
			elapsed_sec -= 1;
			elapsed_8000th += 8000;
		}
		/* add elapsed time to counter and set new timestamp */
		iclock_count += elapsed_sec * 8000 + elapsed_8000th;
		iclock_tv.tv_sec = tv_now.tv_sec;
		iclock_tv.tv_usec = tv_now.tv_usec;
		iclock_tv_valid = 1;
		if (*debug & DEBUG_CLOCK)
			printk("Received first clock from source '%s'.\n",
			    iclock_current ? iclock_current->name : "nothing");
	}
	write_unlock_irqrestore(&iclock_lock, flags);
}
EXPORT_SYMBOL(mISDN_clock_update);

unsigned short
mISDN_clock_get(void)
{
	u_long		flags;
	struct timeval	tv_now;
	time_t		elapsed_sec;
	int		elapsed_8000th;
	u16		count;

	read_lock_irqsave(&iclock_lock, flags);
	/* calc elapsed time by system clock */
	do_gettimeofday(&tv_now);
	elapsed_sec = tv_now.tv_sec - iclock_tv.tv_sec;
	elapsed_8000th = (tv_now.tv_usec / 125) - (iclock_tv.tv_usec / 125);
	if (elapsed_8000th < 0) {
		elapsed_sec -= 1;
		elapsed_8000th += 8000;
	}
	/* add elapsed time to counter */
	count =	iclock_count + elapsed_sec * 8000 + elapsed_8000th;
	read_unlock_irqrestore(&iclock_lock, flags);
	return count;
}
EXPORT_SYMBOL(mISDN_clock_get);

