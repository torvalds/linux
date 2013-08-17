/*
 * Copyright (c) 2012 Samsung Electronics co., ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS - ASV header file
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the Gnu General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_ASV_EXYNOS_H
#define __ASM_ARCH_ASV_EXYNOS_H __FILE__

enum asv_type_id {
	ID_ARM,
	ID_INT,
	ID_MIF,
	ID_G3D,
	ID_END,
};

struct asv_common {
	bool		init_done;
	unsigned int	(*get_voltage)(enum asv_type_id, unsigned int freq);
};

extern unsigned int asv_get_volt(enum asv_type_id target_type, unsigned int target_freq);
extern int exynos5250_init_asv(struct asv_common *asv_info);

#endif /* __ASM_ARCH_ASV_EXYNOS_H */
