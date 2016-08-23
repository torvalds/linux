/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * This header file contains mode setting related functions and definitions
 * which are only used within the drm kms helper module as internal
 * implementation details and are not exported to drivers.
 */

#include <drm/drm_dp_helper.h>

/* drm_fb_helper.c */
int drm_fb_helper_modinit(void);

/* drm_dp_aux_dev.c */
#ifdef CONFIG_DRM_DP_AUX_CHARDEV
int drm_dp_aux_dev_init(void);
void drm_dp_aux_dev_exit(void);
int drm_dp_aux_register_devnode(struct drm_dp_aux *aux);
void drm_dp_aux_unregister_devnode(struct drm_dp_aux *aux);
#else
static inline int drm_dp_aux_dev_init(void)
{
	return 0;
}

static inline void drm_dp_aux_dev_exit(void)
{
}

static inline int drm_dp_aux_register_devnode(struct drm_dp_aux *aux)
{
	return 0;
}

static inline void drm_dp_aux_unregister_devnode(struct drm_dp_aux *aux)
{
}
#endif
