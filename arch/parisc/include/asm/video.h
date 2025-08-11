/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_VIDEO_H_
#define _ASM_VIDEO_H_

#include <linux/types.h>

struct device;

#if defined(CONFIG_STI_CORE) && defined(CONFIG_VIDEO)
bool video_is_primary_device(struct device *dev);
#define video_is_primary_device video_is_primary_device
#endif

#include <asm-generic/video.h>

#endif /* _ASM_VIDEO_H_ */
