/*
 * soc-camera generic scaling-cropping manipulation functions
 *
 * Copyright (C) 2013 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/module.h>

#include <media/soc_camera.h>
#include <media/v4l2-common.h>

#include "soc_scale_crop.h"

#ifdef DEBUG_GEOMETRY
#define dev_geo	dev_info
#else
#define dev_geo	dev_dbg
#endif

/* Check if any dimension of r1 is smaller than respective one of r2 */
static bool is_smaller(const struct v4l2_rect *r1, const struct v4l2_rect *r2)
{
	return r1->width < r2->width || r1->height < r2->height;
}

/* Check if r1 fails to cover r2 */
static bool is_inside(const struct v4l2_rect *r1, const struct v4l2_rect *r2)
{
	return r1->left > r2->left || r1->top > r2->top ||
		r1->left + r1->width < r2->left + r2->width ||
		r1->top + r1->height < r2->top + r2->height;
}

/* Get and store current client crop */
int soc_camera_client_g_rect(struct v4l2_subdev *sd, struct v4l2_rect *rect)
{
	struct v4l2_crop crop;
	struct v4l2_cropcap cap;
	int ret;

	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = v4l2_subdev_call(sd, video, g_crop, &crop);
	if (!ret) {
		*rect = crop.c;
		return ret;
	}

	/* Camera driver doesn't support .g_crop(), assume default rectangle */
	cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = v4l2_subdev_call(sd, video, cropcap, &cap);
	if (!ret)
		*rect = cap.defrect;

	return ret;
}
EXPORT_SYMBOL(soc_camera_client_g_rect);

/* Client crop has changed, update our sub-rectangle to remain within the area */
static void update_subrect(struct v4l2_rect *rect, struct v4l2_rect *subrect)
{
	if (rect->width < subrect->width)
		subrect->width = rect->width;

	if (rect->height < subrect->height)
		subrect->height = rect->height;

	if (rect->left > subrect->left)
		subrect->left = rect->left;
	else if (rect->left + rect->width >
		 subrect->left + subrect->width)
		subrect->left = rect->left + rect->width -
			subrect->width;

	if (rect->top > subrect->top)
		subrect->top = rect->top;
	else if (rect->top + rect->height >
		 subrect->top + subrect->height)
		subrect->top = rect->top + rect->height -
			subrect->height;
}

/*
 * The common for both scaling and cropping iterative approach is:
 * 1. try if the client can produce exactly what requested by the user
 * 2. if (1) failed, try to double the client image until we get one big enough
 * 3. if (2) failed, try to request the maximum image
 */
int soc_camera_client_s_crop(struct v4l2_subdev *sd,
			struct v4l2_crop *crop, struct v4l2_crop *cam_crop,
			struct v4l2_rect *target_rect, struct v4l2_rect *subrect)
{
	struct v4l2_rect *rect = &crop->c, *cam_rect = &cam_crop->c;
	struct device *dev = sd->v4l2_dev->dev;
	struct v4l2_cropcap cap;
	int ret;
	unsigned int width, height;

	v4l2_subdev_call(sd, video, s_crop, crop);
	ret = soc_camera_client_g_rect(sd, cam_rect);
	if (ret < 0)
		return ret;

	/*
	 * Now cam_crop contains the current camera input rectangle, and it must
	 * be within camera cropcap bounds
	 */
	if (!memcmp(rect, cam_rect, sizeof(*rect))) {
		/* Even if camera S_CROP failed, but camera rectangle matches */
		dev_dbg(dev, "Camera S_CROP successful for %dx%d@%d:%d\n",
			rect->width, rect->height, rect->left, rect->top);
		*target_rect = *cam_rect;
		return 0;
	}

	/* Try to fix cropping, that camera hasn't managed to set */
	dev_geo(dev, "Fix camera S_CROP for %dx%d@%d:%d to %dx%d@%d:%d\n",
		cam_rect->width, cam_rect->height,
		cam_rect->left, cam_rect->top,
		rect->width, rect->height, rect->left, rect->top);

	/* We need sensor maximum rectangle */
	ret = v4l2_subdev_call(sd, video, cropcap, &cap);
	if (ret < 0)
		return ret;

	/* Put user requested rectangle within sensor bounds */
	soc_camera_limit_side(&rect->left, &rect->width, cap.bounds.left, 2,
			      cap.bounds.width);
	soc_camera_limit_side(&rect->top, &rect->height, cap.bounds.top, 4,
			      cap.bounds.height);

	/*
	 * Popular special case - some cameras can only handle fixed sizes like
	 * QVGA, VGA,... Take care to avoid infinite loop.
	 */
	width = max(cam_rect->width, 2);
	height = max(cam_rect->height, 2);

	/*
	 * Loop as long as sensor is not covering the requested rectangle and
	 * is still within its bounds
	 */
	while (!ret && (is_smaller(cam_rect, rect) ||
			is_inside(cam_rect, rect)) &&
	       (cap.bounds.width > width || cap.bounds.height > height)) {

		width *= 2;
		height *= 2;

		cam_rect->width = width;
		cam_rect->height = height;

		/*
		 * We do not know what capabilities the camera has to set up
		 * left and top borders. We could try to be smarter in iterating
		 * them, e.g., if camera current left is to the right of the
		 * target left, set it to the middle point between the current
		 * left and minimum left. But that would add too much
		 * complexity: we would have to iterate each border separately.
		 * Instead we just drop to the left and top bounds.
		 */
		if (cam_rect->left > rect->left)
			cam_rect->left = cap.bounds.left;

		if (cam_rect->left + cam_rect->width < rect->left + rect->width)
			cam_rect->width = rect->left + rect->width -
				cam_rect->left;

		if (cam_rect->top > rect->top)
			cam_rect->top = cap.bounds.top;

		if (cam_rect->top + cam_rect->height < rect->top + rect->height)
			cam_rect->height = rect->top + rect->height -
				cam_rect->top;

		v4l2_subdev_call(sd, video, s_crop, cam_crop);
		ret = soc_camera_client_g_rect(sd, cam_rect);
		dev_geo(dev, "Camera S_CROP %d for %dx%d@%d:%d\n", ret,
			cam_rect->width, cam_rect->height,
			cam_rect->left, cam_rect->top);
	}

	/* S_CROP must not modify the rectangle */
	if (is_smaller(cam_rect, rect) || is_inside(cam_rect, rect)) {
		/*
		 * The camera failed to configure a suitable cropping,
		 * we cannot use the current rectangle, set to max
		 */
		*cam_rect = cap.bounds;
		v4l2_subdev_call(sd, video, s_crop, cam_crop);
		ret = soc_camera_client_g_rect(sd, cam_rect);
		dev_geo(dev, "Camera S_CROP %d for max %dx%d@%d:%d\n", ret,
			cam_rect->width, cam_rect->height,
			cam_rect->left, cam_rect->top);
	}

	if (!ret) {
		*target_rect = *cam_rect;
		update_subrect(target_rect, subrect);
	}

	return ret;
}
EXPORT_SYMBOL(soc_camera_client_s_crop);

/* Iterative s_mbus_fmt, also updates cached client crop on success */
static int client_s_fmt(struct soc_camera_device *icd,
			struct v4l2_rect *rect, struct v4l2_rect *subrect,
			unsigned int max_width, unsigned int max_height,
			struct v4l2_mbus_framefmt *mf, bool host_can_scale)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;
	unsigned int width = mf->width, height = mf->height, tmp_w, tmp_h;
	struct v4l2_cropcap cap;
	bool host_1to1;
	int ret;

	ret = v4l2_device_call_until_err(sd->v4l2_dev,
					 soc_camera_grp_id(icd), video,
					 s_mbus_fmt, mf);
	if (ret < 0)
		return ret;

	dev_geo(dev, "camera scaled to %ux%u\n", mf->width, mf->height);

	if (width == mf->width && height == mf->height) {
		/* Perfect! The client has done it all. */
		host_1to1 = true;
		goto update_cache;
	}

	host_1to1 = false;
	if (!host_can_scale)
		goto update_cache;

	cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	ret = v4l2_subdev_call(sd, video, cropcap, &cap);
	if (ret < 0)
		return ret;

	if (max_width > cap.bounds.width)
		max_width = cap.bounds.width;
	if (max_height > cap.bounds.height)
		max_height = cap.bounds.height;

	/* Camera set a format, but geometry is not precise, try to improve */
	tmp_w = mf->width;
	tmp_h = mf->height;

	/* width <= max_width && height <= max_height - guaranteed by try_fmt */
	while ((width > tmp_w || height > tmp_h) &&
	       tmp_w < max_width && tmp_h < max_height) {
		tmp_w = min(2 * tmp_w, max_width);
		tmp_h = min(2 * tmp_h, max_height);
		mf->width = tmp_w;
		mf->height = tmp_h;
		ret = v4l2_device_call_until_err(sd->v4l2_dev,
					soc_camera_grp_id(icd), video,
					s_mbus_fmt, mf);
		dev_geo(dev, "Camera scaled to %ux%u\n",
			mf->width, mf->height);
		if (ret < 0) {
			/* This shouldn't happen */
			dev_err(dev, "Client failed to set format: %d\n", ret);
			return ret;
		}
	}

update_cache:
	/* Update cache */
	ret = soc_camera_client_g_rect(sd, rect);
	if (ret < 0)
		return ret;

	if (host_1to1)
		*subrect = *rect;
	else
		update_subrect(rect, subrect);

	return 0;
}

/**
 * @icd		- soc-camera device
 * @rect	- camera cropping window
 * @subrect	- part of rect, sent to the user
 * @mf		- in- / output camera output window
 * @width	- on input: max host input width
 *		  on output: user width, mapped back to input
 * @height	- on input: max host input height
 *		  on output: user height, mapped back to input
 * @host_can_scale - host can scale this pixel format
 * @shift	- shift, used for scaling
 */
int soc_camera_client_scale(struct soc_camera_device *icd,
			struct v4l2_rect *rect, struct v4l2_rect *subrect,
			struct v4l2_mbus_framefmt *mf,
			unsigned int *width, unsigned int *height,
			bool host_can_scale, unsigned int shift)
{
	struct device *dev = icd->parent;
	struct v4l2_mbus_framefmt mf_tmp = *mf;
	unsigned int scale_h, scale_v;
	int ret;

	/*
	 * 5. Apply iterative camera S_FMT for camera user window (also updates
	 *    client crop cache and the imaginary sub-rectangle).
	 */
	ret = client_s_fmt(icd, rect, subrect, *width, *height,
			   &mf_tmp, host_can_scale);
	if (ret < 0)
		return ret;

	dev_geo(dev, "5: camera scaled to %ux%u\n",
		mf_tmp.width, mf_tmp.height);

	/* 6. Retrieve camera output window (g_fmt) */

	/* unneeded - it is already in "mf_tmp" */

	/* 7. Calculate new client scales. */
	scale_h = soc_camera_calc_scale(rect->width, shift, mf_tmp.width);
	scale_v = soc_camera_calc_scale(rect->height, shift, mf_tmp.height);

	mf->width	= mf_tmp.width;
	mf->height	= mf_tmp.height;
	mf->colorspace	= mf_tmp.colorspace;

	/*
	 * 8. Calculate new host crop - apply camera scales to previously
	 *    updated "effective" crop.
	 */
	*width = soc_camera_shift_scale(subrect->width, shift, scale_h);
	*height = soc_camera_shift_scale(subrect->height, shift, scale_v);

	dev_geo(dev, "8: new client sub-window %ux%u\n", *width, *height);

	return 0;
}
EXPORT_SYMBOL(soc_camera_client_scale);

/*
 * Calculate real client output window by applying new scales to the current
 * client crop. New scales are calculated from the requested output format and
 * host crop, mapped backed onto the client input (subrect).
 */
void soc_camera_calc_client_output(struct soc_camera_device *icd,
		struct v4l2_rect *rect, struct v4l2_rect *subrect,
		const struct v4l2_pix_format *pix, struct v4l2_mbus_framefmt *mf,
		unsigned int shift)
{
	struct device *dev = icd->parent;
	unsigned int scale_v, scale_h;

	if (subrect->width == rect->width &&
	    subrect->height == rect->height) {
		/* No sub-cropping */
		mf->width	= pix->width;
		mf->height	= pix->height;
		return;
	}

	/* 1.-2. Current camera scales and subwin - cached. */

	dev_geo(dev, "2: subwin %ux%u@%u:%u\n",
		subrect->width, subrect->height,
		subrect->left, subrect->top);

	/*
	 * 3. Calculate new combined scales from input sub-window to requested
	 *    user window.
	 */

	/*
	 * TODO: CEU cannot scale images larger than VGA to smaller than SubQCIF
	 * (128x96) or larger than VGA. This and similar limitations have to be
	 * taken into account here.
	 */
	scale_h = soc_camera_calc_scale(subrect->width, shift, pix->width);
	scale_v = soc_camera_calc_scale(subrect->height, shift, pix->height);

	dev_geo(dev, "3: scales %u:%u\n", scale_h, scale_v);

	/*
	 * 4. Calculate desired client output window by applying combined scales
	 *    to client (real) input window.
	 */
	mf->width = soc_camera_shift_scale(rect->width, shift, scale_h);
	mf->height = soc_camera_shift_scale(rect->height, shift, scale_v);
}
EXPORT_SYMBOL(soc_camera_calc_client_output);
