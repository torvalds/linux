/*
 * Copyright 2008  by Karsten Keil <kkeil@novell.com>
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
 */

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/mISDNif.h>
#include "core.h"

static u_int debug;

MODULE_AUTHOR("Karsten Keil");
MODULE_LICENSE("GPL");
module_param(debug, uint, S_IRUGO | S_IWUSR);

static LIST_HEAD(devices);
DEFINE_RWLOCK(device_lock);
static u64		device_ids;
#define MAX_DEVICE_ID	63

static LIST_HEAD(Bprotocols);
DEFINE_RWLOCK(bp_lock);

struct mISDNdevice
*get_mdevice(u_int id)
{
	struct mISDNdevice	*dev;

	read_lock(&device_lock);
	list_for_each_entry(dev, &devices, D.list)
		if (dev->id == id) {
			read_unlock(&device_lock);
			return dev;
		}
	read_unlock(&device_lock);
	return NULL;
}

int
get_mdevice_count(void)
{
	struct mISDNdevice	*dev;
	int			cnt = 0;

	read_lock(&device_lock);
	list_for_each_entry(dev, &devices, D.list)
		cnt++;
	read_unlock(&device_lock);
	return cnt;
}

static int
get_free_devid(void)
{
	u_int	i;

	for (i = 0; i <= MAX_DEVICE_ID; i++)
		if (!test_and_set_bit(i, (u_long *)&device_ids))
			return i;
	return -1;
}

int
mISDN_register_device(struct mISDNdevice *dev, char *name)
{
	u_long	flags;
	int	err;

	dev->id = get_free_devid();
	if (dev->id < 0)
		return -EBUSY;
	if (name && name[0])
		strcpy(dev->name, name);
	else
		sprintf(dev->name, "mISDN%d", dev->id);
	if (debug & DEBUG_CORE)
		printk(KERN_DEBUG "mISDN_register %s %d\n",
			dev->name, dev->id);
	err = create_stack(dev);
	if (err)
		return err;
	write_lock_irqsave(&device_lock, flags);
	list_add_tail(&dev->D.list, &devices);
	write_unlock_irqrestore(&device_lock, flags);
	return 0;
}
EXPORT_SYMBOL(mISDN_register_device);

void
mISDN_unregister_device(struct mISDNdevice *dev) {
	u_long	flags;

	if (debug & DEBUG_CORE)
		printk(KERN_DEBUG "mISDN_unregister %s %d\n",
			dev->name, dev->id);
	write_lock_irqsave(&device_lock, flags);
	list_del(&dev->D.list);
	write_unlock_irqrestore(&device_lock, flags);
	test_and_clear_bit(dev->id, (u_long *)&device_ids);
	delete_stack(dev);
}
EXPORT_SYMBOL(mISDN_unregister_device);

u_int
get_all_Bprotocols(void)
{
	struct Bprotocol	*bp;
	u_int	m = 0;

	read_lock(&bp_lock);
	list_for_each_entry(bp, &Bprotocols, list)
		m |= bp->Bprotocols;
	read_unlock(&bp_lock);
	return m;
}

struct Bprotocol *
get_Bprotocol4mask(u_int m)
{
	struct Bprotocol	*bp;

	read_lock(&bp_lock);
	list_for_each_entry(bp, &Bprotocols, list)
		if (bp->Bprotocols & m) {
			read_unlock(&bp_lock);
			return bp;
		}
	read_unlock(&bp_lock);
	return NULL;
}

struct Bprotocol *
get_Bprotocol4id(u_int id)
{
	u_int	m;

	if (id < ISDN_P_B_START || id > 63) {
		printk(KERN_WARNING "%s id not in range  %d\n",
		    __func__, id);
		return NULL;
	}
	m = 1 << (id & ISDN_P_B_MASK);
	return get_Bprotocol4mask(m);
}

int
mISDN_register_Bprotocol(struct Bprotocol *bp)
{
	u_long			flags;
	struct Bprotocol	*old;

	if (debug & DEBUG_CORE)
		printk(KERN_DEBUG "%s: %s/%x\n", __func__,
		    bp->name, bp->Bprotocols);
	old = get_Bprotocol4mask(bp->Bprotocols);
	if (old) {
		printk(KERN_WARNING
		    "register duplicate protocol old %s/%x new %s/%x\n",
		    old->name, old->Bprotocols, bp->name, bp->Bprotocols);
		return -EBUSY;
	}
	write_lock_irqsave(&bp_lock, flags);
	list_add_tail(&bp->list, &Bprotocols);
	write_unlock_irqrestore(&bp_lock, flags);
	return 0;
}
EXPORT_SYMBOL(mISDN_register_Bprotocol);

void
mISDN_unregister_Bprotocol(struct Bprotocol *bp)
{
	u_long	flags;

	if (debug & DEBUG_CORE)
		printk(KERN_DEBUG "%s: %s/%x\n", __func__, bp->name,
			bp->Bprotocols);
	write_lock_irqsave(&bp_lock, flags);
	list_del(&bp->list);
	write_unlock_irqrestore(&bp_lock, flags);
}
EXPORT_SYMBOL(mISDN_unregister_Bprotocol);

int
mISDNInit(void)
{
	int	err;

	printk(KERN_INFO "Modular ISDN core version %d.%d.%d\n",
		MISDN_MAJOR_VERSION, MISDN_MINOR_VERSION, MISDN_RELEASE);
	mISDN_initstack(&debug);
	err = mISDN_inittimer(&debug);
	if (err)
		goto error;
	err = l1_init(&debug);
	if (err) {
		mISDN_timer_cleanup();
		goto error;
	}
	err = Isdnl2_Init(&debug);
	if (err) {
		mISDN_timer_cleanup();
		l1_cleanup();
		goto error;
	}
	err = misdn_sock_init(&debug);
	if (err) {
		mISDN_timer_cleanup();
		l1_cleanup();
		Isdnl2_cleanup();
	}
error:
	return err;
}

void mISDN_cleanup(void)
{
	misdn_sock_cleanup();
	mISDN_timer_cleanup();
	l1_cleanup();
	Isdnl2_cleanup();

	if (!list_empty(&devices))
		printk(KERN_ERR "%s devices still registered\n", __func__);

	if (!list_empty(&Bprotocols))
		printk(KERN_ERR "%s Bprotocols still registered\n", __func__);
	printk(KERN_DEBUG "mISDNcore unloaded\n");
}

module_init(mISDNInit);
module_exit(mISDN_cleanup);

