/*
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2008 Red Hat Inc.
 * Copyright (c) 2016 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_dumb_buffers.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>
#include <drm/drm_mode.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

/**
 * DOC: overview
 *
 * The KMS API doesn't standardize backing storage object creation and leaves it
 * to driver-specific ioctls. Furthermore actually creating a buffer object even
 * for GEM-based drivers is done through a driver-specific ioctl - GEM only has
 * a common userspace interface for sharing and destroying objects. While not an
 * issue for full-fledged graphics stacks that include device-specific userspace
 * components (in libdrm for instance), this limit makes DRM-based early boot
 * graphics unnecessarily complex.
 *
 * Dumb objects partly alleviate the problem by providing a standard API to
 * create dumb buffers suitable for scanout, which can then be used to create
 * KMS frame buffers.
 *
 * To support dumb objects drivers must implement the &drm_driver.dumb_create
 * and &drm_driver.dumb_map_offset operations (the latter defaults to
 * drm_gem_dumb_map_offset() if not set). Drivers that don't use GEM handles
 * additionally need to implement the &drm_driver.dumb_destroy operation. See
 * the callbacks for further details.
 *
 * Note that dumb objects may not be used for gpu acceleration, as has been
 * attempted on some ARM embedded platforms. Such drivers really must have
 * a hardware-specific ioctl to allocate suitable buffer objects.
 */

static int drm_mode_align_dumb(struct drm_mode_create_dumb *args,
			       unsigned long hw_pitch_align,
			       unsigned long hw_size_align)
{
	u32 pitch = args->pitch;
	u32 size;

	if (!pitch)
		return -EINVAL;

	if (hw_pitch_align)
		pitch = roundup(pitch, hw_pitch_align);

	if (!hw_size_align)
		hw_size_align = PAGE_SIZE;
	else if (!IS_ALIGNED(hw_size_align, PAGE_SIZE))
		return -EINVAL; /* TODO: handle this if necessary */

	if (check_mul_overflow(args->height, pitch, &size))
		return -EINVAL;
	size = ALIGN(size, hw_size_align);
	if (!size)
		return -EINVAL;

	args->pitch = pitch;
	args->size = size;

	return 0;
}

/**
 * drm_mode_size_dumb - Calculates the scanline and buffer sizes for dumb buffers
 * @dev: DRM device
 * @args: Parameters for the dumb buffer
 * @hw_pitch_align: Hardware scanline alignment in bytes
 * @hw_size_align: Hardware buffer-size alignment in bytes
 *
 * The helper drm_mode_size_dumb() calculates the size of the buffer
 * allocation and the scanline size for a dumb buffer. Callers have to
 * set the buffers width, height and color mode in the argument @arg.
 * The helper validates the correctness of the input and tests for
 * possible overflows. If successful, it returns the dumb buffer's
 * required scanline pitch and size in &args.
 *
 * The parameter @hw_pitch_align allows the driver to specifies an
 * alignment for the scanline pitch, if the hardware requires any. The
 * calculated pitch will be a multiple of the alignment. The parameter
 * @hw_size_align allows to specify an alignment for buffer sizes. The
 * provided alignment should represent requirements of the graphics
 * hardware. drm_mode_size_dumb() handles GEM-related constraints
 * automatically across all drivers and hardware. For example, the
 * returned buffer size is always a multiple of PAGE_SIZE, which is
 * required by mmap().
 *
 * Returns:
 * Zero on success, or a negative error code otherwise.
 */
int drm_mode_size_dumb(struct drm_device *dev,
		       struct drm_mode_create_dumb *args,
		       unsigned long hw_pitch_align,
		       unsigned long hw_size_align)
{
	u64 pitch = 0;
	u32 fourcc;

	/*
	 * The scanline pitch depends on the buffer width and the color
	 * format. The latter is specified as a color-mode constant for
	 * which we first have to find the corresponding color format.
	 *
	 * Different color formats can have the same color-mode constant.
	 * For example XRGB8888 and BGRX8888 both have a color mode of 32.
	 * It is possible to use different formats for dumb-buffer allocation
	 * and rendering as long as all involved formats share the same
	 * color-mode constant.
	 */
	fourcc = drm_driver_color_mode_format(dev, args->bpp);
	if (fourcc != DRM_FORMAT_INVALID) {
		const struct drm_format_info *info = drm_format_info(fourcc);

		if (!info)
			return -EINVAL;
		pitch = drm_format_info_min_pitch(info, 0, args->width);
	} else if (args->bpp) {
		/*
		 * Some userspace throws in arbitrary values for bpp and
		 * relies on the kernel to figure it out. In this case we
		 * fall back to the old method of using bpp directly. The
		 * over-commitment of memory from the rounding is acceptable
		 * for compatibility with legacy userspace. We have a number
		 * of deprecated legacy values that are explicitly supported.
		 */
		switch (args->bpp) {
		default:
			drm_warn_once(dev,
				      "Unknown color mode %u; guessing buffer size.\n",
				      args->bpp);
			fallthrough;
		/*
		 * These constants represent various YUV formats supported by
		 * drm_gem_afbc_get_bpp().
		 */
		case 12: // DRM_FORMAT_YUV420_8BIT
		case 15: // DRM_FORMAT_YUV420_10BIT
		case 30: // DRM_FORMAT_VUY101010
			fallthrough;
		/*
		 * Used by Mesa and Gstreamer to allocate NV formats and others
		 * as RGB buffers. Technically, XRGB16161616F formats are RGB,
		 * but the dumb buffers are not supposed to be used for anything
		 * beyond 32 bits per pixels.
		 */
		case 10: // DRM_FORMAT_NV{15,20,30}, DRM_FORMAT_P010
		case 64: // DRM_FORMAT_{XRGB,XBGR,ARGB,ABGR}16161616F
			pitch = args->width * DIV_ROUND_UP(args->bpp, SZ_8);
			break;
		}
	}

	if (!pitch || pitch > U32_MAX)
		return -EINVAL;

	args->pitch = pitch;

	return drm_mode_align_dumb(args, hw_pitch_align, hw_size_align);
}
EXPORT_SYMBOL(drm_mode_size_dumb);

int drm_mode_create_dumb(struct drm_device *dev,
			 struct drm_mode_create_dumb *args,
			 struct drm_file *file_priv)
{
	u32 cpp, stride, size;

	if (!dev->driver->dumb_create)
		return -ENOSYS;
	if (!args->width || !args->height || !args->bpp)
		return -EINVAL;

	/* overflow checks for 32bit size calculations */
	if (args->bpp > U32_MAX - 8)
		return -EINVAL;
	cpp = DIV_ROUND_UP(args->bpp, 8);
	if (cpp > U32_MAX / args->width)
		return -EINVAL;
	stride = cpp * args->width;
	if (args->height > U32_MAX / stride)
		return -EINVAL;

	/* test for wrap-around */
	size = args->height * stride;
	if (PAGE_ALIGN(size) == 0)
		return -EINVAL;

	/*
	 * handle, pitch and size are output parameters. Zero them out to
	 * prevent drivers from accidentally using uninitialized data. Since
	 * not all existing userspace is clearing these fields properly we
	 * cannot reject IOCTL with garbage in them.
	 */
	args->handle = 0;
	args->pitch = 0;
	args->size = 0;

	return dev->driver->dumb_create(file_priv, dev, args);
}

int drm_mode_create_dumb_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv)
{
	struct drm_mode_create_dumb *args = data;
	int err;

	err = drm_mode_create_dumb(dev, args, file_priv);
	if (err) {
		args->handle = 0;
		args->pitch = 0;
		args->size = 0;
	}
	return err;
}

static int drm_mode_mmap_dumb(struct drm_device *dev, struct drm_mode_map_dumb *args,
			      struct drm_file *file_priv)
{
	if (!dev->driver->dumb_create)
		return -ENOSYS;

	if (dev->driver->dumb_map_offset)
		return dev->driver->dumb_map_offset(file_priv, dev, args->handle,
						    &args->offset);
	else
		return drm_gem_dumb_map_offset(file_priv, dev, args->handle,
					       &args->offset);
}

/**
 * drm_mode_mmap_dumb_ioctl - create an mmap offset for a dumb backing storage buffer
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * Allocate an offset in the drm device node's address space to be able to
 * memory map a dumb buffer.
 *
 * Called by the user via ioctl.
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
int drm_mode_mmap_dumb_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv)
{
	struct drm_mode_map_dumb *args = data;
	int err;

	err = drm_mode_mmap_dumb(dev, args, file_priv);
	if (err)
		args->offset = 0;
	return err;
}

int drm_mode_destroy_dumb(struct drm_device *dev, u32 handle,
			  struct drm_file *file_priv)
{
	if (!dev->driver->dumb_create)
		return -ENOSYS;

	return drm_gem_handle_delete(file_priv, handle);
}

int drm_mode_destroy_dumb_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv)
{
	struct drm_mode_destroy_dumb *args = data;

	return drm_mode_destroy_dumb(dev, args->handle, file_priv);
}
