/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_MAP_H_
#define _XE_MAP_H_

#include <linux/iosys-map.h>

#include <xe_device.h>

/**
 * DOC: Map layer
 *
 * All access to any memory shared with a device (both sysmem and vram) in the
 * XE driver should go through this layer (xe_map). This layer is built on top
 * of :ref:`driver-api/device-io:Generalizing Access to System and I/O Memory`
 * and with extra hooks into the XE driver that allows adding asserts to memory
 * accesses (e.g. for blocking runtime_pm D3Cold on Discrete Graphics).
 */

static inline void xe_map_memcpy_to(struct xe_device *xe, struct iosys_map *dst,
				    size_t dst_offset, const void *src,
				    size_t len)
{
	xe_device_assert_mem_access(xe);
	iosys_map_memcpy_to(dst, dst_offset, src, len);
}

static inline void xe_map_memcpy_from(struct xe_device *xe, void *dst,
				      const struct iosys_map *src,
				      size_t src_offset, size_t len)
{
	xe_device_assert_mem_access(xe);
	iosys_map_memcpy_from(dst, src, src_offset, len);
}

static inline void xe_map_memset(struct xe_device *xe,
				 struct iosys_map *dst, size_t offset,
				 int value, size_t len)
{
	xe_device_assert_mem_access(xe);
	iosys_map_memset(dst, offset, value, len);
}

/* FIXME: We likely should kill these two functions sooner or later */
static inline u32 xe_map_read32(struct xe_device *xe, struct iosys_map *map)
{
	xe_device_assert_mem_access(xe);

	if (map->is_iomem)
		return readl(map->vaddr_iomem);
	else
		return READ_ONCE(*(u32 *)map->vaddr);
}

static inline void xe_map_write32(struct xe_device *xe, struct iosys_map *map,
				  u32 val)
{
	xe_device_assert_mem_access(xe);

	if (map->is_iomem)
		writel(val, map->vaddr_iomem);
	else
		*(u32 *)map->vaddr = val;
}

#define xe_map_rd(xe__, map__, offset__, type__) ({			\
	struct xe_device *__xe = xe__;					\
	xe_device_assert_mem_access(__xe);				\
	iosys_map_rd(map__, offset__, type__);				\
})

#define xe_map_wr(xe__, map__, offset__, type__, val__) ({		\
	struct xe_device *__xe = xe__;					\
	xe_device_assert_mem_access(__xe);				\
	iosys_map_wr(map__, offset__, type__, val__);			\
})

#define xe_map_rd_field(xe__, map__, struct_offset__, struct_type__, field__) ({	\
	struct xe_device *__xe = xe__;					\
	xe_device_assert_mem_access(__xe);				\
	iosys_map_rd_field(map__, struct_offset__, struct_type__, field__);		\
})

#define xe_map_wr_field(xe__, map__, struct_offset__, struct_type__, field__, val__) ({	\
	struct xe_device *__xe = xe__;					\
	xe_device_assert_mem_access(__xe);				\
	iosys_map_wr_field(map__, struct_offset__, struct_type__, field__, val__);	\
})

#endif
