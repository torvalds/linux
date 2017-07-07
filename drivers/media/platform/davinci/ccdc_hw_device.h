/*
 * Copyright (C) 2008-2009 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * ccdc device API
 */
#ifndef _CCDC_HW_DEVICE_H
#define _CCDC_HW_DEVICE_H

#ifdef __KERNEL__
#include <linux/videodev2.h>
#include <linux/device.h>
#include <media/davinci/vpfe_types.h>
#include <media/davinci/ccdc_types.h>

/*
 * ccdc hw operations
 */
struct ccdc_hw_ops {
	/* Pointer to initialize function to initialize ccdc device */
	int (*open) (struct device *dev);
	/* Pointer to deinitialize function */
	int (*close) (struct device *dev);
	/* set ccdc base address */
	void (*set_ccdc_base)(void *base, int size);
	/* Pointer to function to enable or disable ccdc */
	void (*enable) (int en);
	/* reset sbl. only for 6446 */
	void (*reset) (void);
	/* enable output to sdram */
	void (*enable_out_to_sdram) (int en);
	/* Pointer to function to set hw parameters */
	int (*set_hw_if_params) (struct vpfe_hw_if_param *param);
	/* get interface parameters */
	int (*get_hw_if_params) (struct vpfe_hw_if_param *param);
	/*
	 * Pointer to function to set parameters. Used
	 * for implementing VPFE_S_CCDC_PARAMS
	 */
	int (*set_params) (void *params);
	/*
	 * Pointer to function to get parameter. Used
	 * for implementing VPFE_G_CCDC_PARAMS
	 */
	int (*get_params) (void *params);
	/* Pointer to function to configure ccdc */
	int (*configure) (void);

	/* Pointer to function to set buffer type */
	int (*set_buftype) (enum ccdc_buftype buf_type);
	/* Pointer to function to get buffer type */
	enum ccdc_buftype (*get_buftype) (void);
	/* Pointer to function to set frame format */
	int (*set_frame_format) (enum ccdc_frmfmt frm_fmt);
	/* Pointer to function to get frame format */
	enum ccdc_frmfmt (*get_frame_format) (void);
	/* enumerate hw pix formats */
	int (*enum_pix)(u32 *hw_pix, int i);
	/* Pointer to function to set buffer type */
	u32 (*get_pixel_format) (void);
	/* Pointer to function to get pixel format. */
	int (*set_pixel_format) (u32 pixfmt);
	/* Pointer to function to set image window */
	int (*set_image_window) (struct v4l2_rect *win);
	/* Pointer to function to set image window */
	void (*get_image_window) (struct v4l2_rect *win);
	/* Pointer to function to get line length */
	unsigned int (*get_line_length) (void);

	/* Pointer to function to set frame buffer address */
	void (*setfbaddr) (unsigned long addr);
	/* Pointer to function to get field id */
	int (*getfid) (void);
};

struct ccdc_hw_device {
	/* ccdc device name */
	char name[32];
	/* module owner */
	struct module *owner;
	/* hw ops */
	struct ccdc_hw_ops hw_ops;
};

/* Used by CCDC module to register & unregister with vpfe capture driver */
int vpfe_register_ccdc_device(struct ccdc_hw_device *dev);
void vpfe_unregister_ccdc_device(struct ccdc_hw_device *dev);

#endif
#endif
