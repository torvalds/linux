/*
 * Copyright (C) 2012 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ARMADA_IOCTLP_H
#define ARMADA_IOCTLP_H

#define ARMADA_IOCTL_PROTO(name)\
extern int armada_##name##_ioctl(struct drm_device *, void *, struct drm_file *)

ARMADA_IOCTL_PROTO(gem_create);
ARMADA_IOCTL_PROTO(gem_mmap);
ARMADA_IOCTL_PROTO(gem_pwrite);

#endif
