/*
 * Copyright (C) 2011-2014 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __MXC_DISPDRV_H__
#define __MXC_DISPDRV_H__
#include <linux/fb.h>
#include "crtc.h"

struct mxc_dispdrv_handle {
	struct mxc_dispdrv_driver *drv;
};

struct mxc_dispdrv_setting {
	/*input-feedback parameter*/
	struct fb_info *fbi;
	int if_fmt;
	int default_bpp;
	char *dft_mode_str;

	/* feedback parameter */
	enum crtc crtc;
};

struct mxc_dispdrv_driver {
	const char *name;
	int (*init) (struct mxc_dispdrv_handle *, struct mxc_dispdrv_setting *);
	void (*deinit) (struct mxc_dispdrv_handle *);
	/* display driver enable function for extension */
	int (*enable) (struct mxc_dispdrv_handle *, struct fb_info *);
	/* display driver disable function, called at early part of fb_blank */
	void (*disable) (struct mxc_dispdrv_handle *, struct fb_info *);
	/* display driver setup function, called at early part of fb_set_par */
	int (*setup) (struct mxc_dispdrv_handle *, struct fb_info *fbi);
};

struct mxc_dispdrv_handle *mxc_dispdrv_register(struct mxc_dispdrv_driver *drv);
int mxc_dispdrv_unregister(struct mxc_dispdrv_handle *handle);
struct mxc_dispdrv_handle *mxc_dispdrv_gethandle(char *name,
	struct mxc_dispdrv_setting *setting);
void mxc_dispdrv_puthandle(struct mxc_dispdrv_handle *handle);
int mxc_dispdrv_setdata(struct mxc_dispdrv_handle *handle, void *data);
void *mxc_dispdrv_getdata(struct mxc_dispdrv_handle *handle);
#endif
