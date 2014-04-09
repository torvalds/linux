/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





#ifndef _ARM_CSTD_TYPES_H_
#define _ARM_CSTD_TYPES_H_

#if 1 == CSTD_TOOLCHAIN_MSVC
	#include "arm_cstd_types_msvc.h"
#elif 1 == CSTD_TOOLCHAIN_GCC
	#include "arm_cstd_types_gcc.h"
#elif 1 == CSTD_TOOLCHAIN_RVCT
	#include "arm_cstd_types_rvct.h"
#else
	#error "Toolchain not recognized"
#endif

#endif /* End (_ARM_CSTD_TYPES_H_) */
