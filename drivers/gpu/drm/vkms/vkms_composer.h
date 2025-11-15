/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _VKMS_COMPOSER_H_
#define _VKMS_COMPOSER_H_

#include <kunit/visibility.h>
#include "vkms_drv.h"

#if IS_ENABLED(CONFIG_KUNIT)
u16 lerp_u16(u16 a, u16 b, s64 t);
s64 get_lut_index(const struct vkms_color_lut *lut, u16 channel_value);
#endif

#endif /* _VKMS_COMPOSER_H_ */
