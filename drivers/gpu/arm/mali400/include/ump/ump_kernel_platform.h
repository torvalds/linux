/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2010 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file ump_kernel_platform.h
 *
 * This file should define UMP_KERNEL_API_EXPORT,
 * which dictates how the UMP kernel API should be exported/imported.
 * Modify this file, if needed, to match your platform setup.
 */

#ifndef __UMP_KERNEL_PLATFORM_H__
#define __UMP_KERNEL_PLATFORM_H__

/** @addtogroup ump_kernel_space_api
 * @{ */

/**
 * A define which controls how UMP kernel space API functions are imported and exported.
 * This define should be set by the implementor of the UMP API.
 */

#if defined(_WIN32)

#if defined(UMP_BUILDING_UMP_LIBRARY)
#define UMP_KERNEL_API_EXPORT __declspec(dllexport)
#else
#define UMP_KERNEL_API_EXPORT __declspec(dllimport)
#endif

#else

#define UMP_KERNEL_API_EXPORT

#endif


/** @} */ /* end group ump_kernel_space_api */


#endif /* __UMP_KERNEL_PLATFORM_H__ */
