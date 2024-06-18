/*
 *  linux/drivers/video/fb_cmdline.c
 *
 *  Copyright (C) 2014 Intel Corp
 *  Copyright (C) 1994 Martin Schaller
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Authors:
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 */

#include <linux/export.h>
#include <linux/fb.h>
#include <linux/string.h>

#include <video/cmdline.h>

/**
 * fb_get_options - get kernel boot parameters
 * @name:   framebuffer name as it would appear in
 *          the boot parameter line
 *          (video=<name>:<options>)
 * @option: the option will be stored here
 *
 * The caller owns the string returned in @option and is
 * responsible for releasing the memory.
 *
 * NOTE: Needed to maintain backwards compatibility
 */
int fb_get_options(const char *name, char **option)
{
	const char *options = NULL;
	bool is_of = false;
	bool enabled;

	if (name)
		is_of = strncmp(name, "offb", 4);

	enabled = __video_get_options(name, &options, is_of);

	if (options) {
		if (!strncmp(options, "off", 3))
			enabled = false;
	}

	if (option) {
		if (options)
			*option = kstrdup(options, GFP_KERNEL);
		else
			*option = NULL;
	}

	return enabled ? 0 : 1; // 0 on success, 1 otherwise
}
EXPORT_SYMBOL(fb_get_options);
