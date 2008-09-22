/* Copyright (c) 2007 Coraid, Inc.  See COPYING for GPL terms. */
/*
 * aoemain.c
 * Module initialization routines, discover timer
 */

#include <linux/hdreg.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include "aoe.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sam Hopkins <sah@coraid.com>");
MODULE_DESCRIPTION("AoE block/char driver for 2.6.2 and newer 2.6 kernels");
MODULE_VERSION(VERSION);

enum { TINIT, TRUN, TKILL };

static void
discover_timer(ulong vp)
{
	static struct timer_list t;
	static volatile ulong die;
	static spinlock_t lock;
	ulong flags;
	enum { DTIMERTICK = HZ * 60 }; /* one minute */

	switch (vp) {
	case TINIT:
		init_timer(&t);
		spin_lock_init(&lock);
		t.data = TRUN;
		t.function = discover_timer;
		die = 0;
	case TRUN:
		spin_lock_irqsave(&lock, flags);
		if (!die) {
			t.expires = jiffies + DTIMERTICK;
			add_timer(&t);
		}
		spin_unlock_irqrestore(&lock, flags);

		aoecmd_cfg(0xffff, 0xff);
		return;
	case TKILL:
		spin_lock_irqsave(&lock, flags);
		die = 1;
		spin_unlock_irqrestore(&lock, flags);

		del_timer_sync(&t);
	default:
		return;
	}
}

static void
aoe_exit(void)
{
	discover_timer(TKILL);

	aoenet_exit();
	unregister_blkdev(AOE_MAJOR, DEVICE_NAME);
	aoechr_exit();
	aoedev_exit();
	aoeblk_exit();		/* free cache after de-allocating bufs */
}

static int __init
aoe_init(void)
{
	int ret;

	ret = aoedev_init();
	if (ret)
		return ret;
	ret = aoechr_init();
	if (ret)
		goto chr_fail;
	ret = aoeblk_init();
	if (ret)
		goto blk_fail;
	ret = aoenet_init();
	if (ret)
		goto net_fail;
	ret = register_blkdev(AOE_MAJOR, DEVICE_NAME);
	if (ret < 0) {
		printk(KERN_ERR "aoe: can't register major\n");
		goto blkreg_fail;
	}

	printk(KERN_INFO "aoe: AoE v%s initialised.\n", VERSION);
	discover_timer(TINIT);
	return 0;

 blkreg_fail:
	aoenet_exit();
 net_fail:
	aoeblk_exit();
 blk_fail:
	aoechr_exit();
 chr_fail:
	aoedev_exit();
	
	printk(KERN_INFO "aoe: initialisation failure.\n");
	return ret;
}

module_init(aoe_init);
module_exit(aoe_exit);

