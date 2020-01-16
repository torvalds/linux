/*
 *  linux/drivers/video/fb_yestify.c
 *
 *  Copyright (C) 2006 Antoniyes Daplas <adaplas@pol.net>
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */
#include <linux/fb.h>
#include <linux/yestifier.h>
#include <linux/export.h>

static BLOCKING_NOTIFIER_HEAD(fb_yestifier_list);

/**
 *	fb_register_client - register a client yestifier
 *	@nb: yestifier block to callback on events
 */
int fb_register_client(struct yestifier_block *nb)
{
	return blocking_yestifier_chain_register(&fb_yestifier_list, nb);
}
EXPORT_SYMBOL(fb_register_client);

/**
 *	fb_unregister_client - unregister a client yestifier
 *	@nb: yestifier block to callback on events
 */
int fb_unregister_client(struct yestifier_block *nb)
{
	return blocking_yestifier_chain_unregister(&fb_yestifier_list, nb);
}
EXPORT_SYMBOL(fb_unregister_client);

/**
 * fb_yestifier_call_chain - yestify clients of fb_events
 *
 */
int fb_yestifier_call_chain(unsigned long val, void *v)
{
	return blocking_yestifier_call_chain(&fb_yestifier_list, val, v);
}
EXPORT_SYMBOL_GPL(fb_yestifier_call_chain);
