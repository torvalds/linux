/* Copyright (c) 2012 Coraid, Inc.  See COPYING for GPL terms. */
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

static struct timer_list timer;
struct workqueue_struct *aoe_wq;

static void discover_timer(struct timer_list *t)
{
	mod_timer(t, jiffies + HZ * 60); /* one minute */

	aoecmd_cfg(0xffff, 0xff);
}

static void __exit
aoe_exit(void)
{
	timer_delete_sync(&timer);

	aoenet_exit();
	unregister_blkdev(AOE_MAJOR, DEVICE_NAME);
	aoecmd_exit();
	aoechr_exit();
	aoedev_exit();
	aoeblk_exit();		/* free cache after de-allocating bufs */
	destroy_workqueue(aoe_wq);
}

static int __init
aoe_init(void)
{
	int ret;

	aoe_wq = alloc_workqueue("aoe_wq", WQ_PERCPU, 0);
	if (!aoe_wq)
		return -ENOMEM;

	ret = aoedev_init();
	if (ret)
		goto dev_fail;
	ret = aoechr_init();
	if (ret)
		goto chr_fail;
	ret = aoeblk_init();
	if (ret)
		goto blk_fail;
	ret = aoenet_init();
	if (ret)
		goto net_fail;
	ret = aoecmd_init();
	if (ret)
		goto cmd_fail;
	ret = register_blkdev(AOE_MAJOR, DEVICE_NAME);
	if (ret < 0) {
		printk(KERN_ERR "aoe: can't register major\n");
		goto blkreg_fail;
	}
	printk(KERN_INFO "aoe: AoE v%s initialised.\n", VERSION);

	timer_setup(&timer, discover_timer, 0);
	discover_timer(&timer);
	return 0;
 blkreg_fail:
	aoecmd_exit();
 cmd_fail:
	aoenet_exit();
 net_fail:
	aoeblk_exit();
 blk_fail:
	aoechr_exit();
 chr_fail:
	aoedev_exit();
 dev_fail:
	destroy_workqueue(aoe_wq);

	printk(KERN_INFO "aoe: initialisation failure.\n");
	return ret;
}

module_init(aoe_init);
module_exit(aoe_exit);

