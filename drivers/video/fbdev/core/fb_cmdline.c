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
#include <linux/init.h>
#include <linux/fb.h>

static char *video_options[FB_MAX] __read_mostly;
static const char *fb_mode_option __read_mostly;
static int ofonly __read_mostly;

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
	int retval = 0;
	size_t name_len;
	char *opt;

	if (name)
		name_len = strlen(name);

	if (name_len && ofonly && strncmp(name, "offb", 4))
		retval = 1;

	if (name_len && !retval) {
		unsigned int i;

		for (i = 0; i < FB_MAX; i++) {
			if (video_options[i] == NULL)
				continue;
			if (!video_options[i][0])
				continue;
			opt = video_options[i];
			if (!strncmp(name, opt, name_len) &&
			    opt[name_len] == ':')
				options = opt + name_len + 1;
		}
	}
	/* No match, pass global option */
	if (!options && option && fb_mode_option)
		options = fb_mode_option;
	if (options && !strncmp(options, "off", 3))
		retval = 1;

	if (option) {
		if (options)
			*option = kstrdup(options, GFP_KERNEL);
		else
			*option = NULL;
	}

	return retval;
}
EXPORT_SYMBOL(fb_get_options);

/**
 *	video_setup - process command line options
 *	@options: string of options
 *
 *	Process command line options for frame buffer subsystem.
 *
 *	NOTE: This function is a __setup and __init function.
 *            It only stores the options.  Drivers have to call
 *            fb_get_options() as necessary.
 */
static int __init video_setup(char *options)
{
	if (!options || !*options)
		goto out;

	if (!strncmp(options, "ofonly", 6)) {
		ofonly = 1;
		goto out;
	}

	if (strchr(options, ':')) {
		/* named */
		int i;

		for (i = 0; i < FB_MAX; i++) {
			if (video_options[i] == NULL) {
				video_options[i] = options;
				break;
			}
		}
	} else {
		/* global */
		fb_mode_option = options;
	}

out:
	return 1;
}
__setup("video=", video_setup);
