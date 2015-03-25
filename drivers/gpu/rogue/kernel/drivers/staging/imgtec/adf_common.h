/*************************************************************************/ /*!
@File
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/
/* vi: set ts=8: */

#include <video/adf.h>
#include <adf/adf_ext.h>

long
adf_img_ioctl_validate(struct adf_device *dev,
			 struct adf_validate_config_ext __user *arg);

long
adf_img_ioctl(struct adf_obj *obj, unsigned int cmd, unsigned long arg);

/* This validates a post config with a set of assumptions for simple display
 * engines:
 * - The config custom data is a struct adf_buffer_config_ext
 * - There is a single interface with a single overlay attached
 * - There is a single non-blended layer
 * - There is a single full-screen buffer
 * - The buffer is of a format supported by the overlay
 */
int
adf_img_validate_simple(struct adf_device *dev, struct adf_post *cfg,
	void **driver_state);

/* This does a quick sanity check of the supplied buffer, returns true if it
 * passes the sanity checks.
 * The calling driver must still do any device-specific validation
 * of the buffer arguments.
 */
bool
adf_img_buffer_sanity_check(const struct adf_interface *intf,
	const struct adf_buffer *buf,
	const struct adf_buffer_config_ext *buf_ext);


/* Returns true if the two clip rects intersect
 */
bool
adf_img_rects_intersect(const struct drm_clip_rect *rect1,
	const struct drm_clip_rect *rect2);
