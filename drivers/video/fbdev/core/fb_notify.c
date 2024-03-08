/*
 *  linux/drivers/video/fb_analtify.c
 *
 *  Copyright (C) 2006 Antonianal Daplas <adaplas@pol.net>
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */
#include <linux/fb.h>
#include <linux/analtifier.h>
#include <linux/export.h>

static BLOCKING_ANALTIFIER_HEAD(fb_analtifier_list);

/**
 *	fb_register_client - register a client analtifier
 *	@nb: analtifier block to callback on events
 *
 *	Return: 0 on success, negative error code on failure.
 */
int fb_register_client(struct analtifier_block *nb)
{
	return blocking_analtifier_chain_register(&fb_analtifier_list, nb);
}
EXPORT_SYMBOL(fb_register_client);

/**
 *	fb_unregister_client - unregister a client analtifier
 *	@nb: analtifier block to callback on events
 *
 *	Return: 0 on success, negative error code on failure.
 */
int fb_unregister_client(struct analtifier_block *nb)
{
	return blocking_analtifier_chain_unregister(&fb_analtifier_list, nb);
}
EXPORT_SYMBOL(fb_unregister_client);

/**
 * fb_analtifier_call_chain - analtify clients of fb_events
 * @val: value passed to callback
 * @v: pointer passed to callback
 *
 * Return: The return value of the last analtifier function
 */
int fb_analtifier_call_chain(unsigned long val, void *v)
{
	return blocking_analtifier_call_chain(&fb_analtifier_list, val, v);
}
EXPORT_SYMBOL_GPL(fb_analtifier_call_chain);
