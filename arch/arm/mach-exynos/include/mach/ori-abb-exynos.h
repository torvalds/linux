/*
 * Copyright (c) 2012 Samsung Electronics co., ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS - ABB header file
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the Gnu General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __MACH_ABB_EXYNOS_H
#define __MACH_ABB_EXYNOS_H __FILE__

enum abb_member {
	ABB_INT,
	ABB_ARM,
	ABB_G3D,
	ABB_MIF,
};
extern void set_abb_member(enum abb_member abb_target,
			unsigned int abb_mode_value);

#endif /* __MACH_ABB_EXYNOS_H */
