/* linux/arch/arm/plat-s5p/include/plat/media.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Samsung Media device descriptions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _S5P_MEDIA_H
#define _S5P_MEDIA_H

#include <linux/types.h>
#include <asm/setup.h>

#ifdef CONFIG_CMA
#include <linux/cma.h>
void s5p_cma_region_reserve(struct cma_region *regions_normal,
			      struct cma_region *regions_secure,
			      size_t align_secure, const char *map);
#else

struct s5p_media_device {
	u32		id;
	const char	*name;
	u32		bank;
	size_t		memsize;
	dma_addr_t	paddr;
};

extern struct meminfo meminfo;
extern dma_addr_t s5p_get_media_memory_bank(int dev_id, int bank);
extern size_t s5p_get_media_memsize_bank(int dev_id, int bank);
extern dma_addr_t s5p_get_media_membase_bank(int bank);
extern void s5p_reserve_mem(size_t boundary);
#endif /* CONFIG_CMA */
#endif
