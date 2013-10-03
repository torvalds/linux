/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and the associated README documentation file (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

struct go7007_md_params {
	__u16 region;
	__u16 trigger;
	__u16 pixel_threshold;
	__u16 motion_threshold;
	__u32 reserved[8];
};

struct go7007_md_region {
	__u16 region;
	__u16 flags;
	struct v4l2_clip *clips;
	__u32 reserved[8];
};

#define	GO7007IOC_S_MD_PARAMS	_IOWR('V', BASE_VIDIOC_PRIVATE + 6, \
					struct go7007_md_params)
#define	GO7007IOC_G_MD_PARAMS	_IOR('V', BASE_VIDIOC_PRIVATE + 7, \
					struct go7007_md_params)
#define	GO7007IOC_S_MD_REGION	_IOW('V', BASE_VIDIOC_PRIVATE + 8, \
					struct go7007_md_region)
