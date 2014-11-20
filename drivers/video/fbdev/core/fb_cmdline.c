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
 *    Vetter <danie.vetter@ffwll.ch>
 */
#include <linux/init.h>
#include <linux/fb.h>

static char *video_options[FB_MAX] __read_mostly;
static int ofonly __read_mostly;

const char *fb_mode_option;
EXPORT_SYMBOL_GPL(fb_mode_option);

/**
 * fb_get_options - get kernel boot parameters
 * @name:   framebuffer name as it would appear in
 *          the boot parameter line
 *          (video=<name>:<options>)
 * @option: the option will be stored here
 *
 * NOTE: Needed to maintain backwards compatibility
 */
int fb_get_options(const char *name, char **option)
{
	char *opt, *options = NULL;
	int retval = 0;
	int name_len = strlen(name), i;

	if (name_len && ofonly && strncmp(name, "offb", 4))
		retval = 1;

	if (name_len && !retval) {
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
		options = kstrdup(fb_mode_option, GFP_KERNEL);
	if (options && !strncmp(options, "off", 3))
		retval = 1;

	if (option)
		*option = options;

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
 *
 *	Returns zero.
 *
 */
static int __init video_setup(char *options)
{
	int i, global = 0;

	if (!options || !*options)
		global = 1;

	if (!global && !strncmp(options, "ofonly", 6)) {
		ofonly = 1;
		global = 1;
	}

	if (!global && !strchr(options, ':')) {
		fb_mode_option = options;
		global = 1;
	}

	if (!global) {
		for (i = 0; i < FB_MAX; i++) {
			if (video_options[i] == NULL) {
				video_options[i] = options;
				break;
			}
		}
	}

	return 1;
}
__setup("video=", video_setup);
