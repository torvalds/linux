/*
 *
 * (C) COPYRIGHT 2010 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#ifndef _MALISW_VERSION_MACROS_H_
#define _MALISW_VERSION_MACROS_H_

/**
 * @file mali_version_macros.h
 * Mali version control macros.
 */

/**
 * @addtogroup malisw 
 * @{
 */

/**
 * @defgroup malisw_version Mali module version control
 *
 * This file provides a set of macros used to check a module's version. This
 * version information can be used to perform compile time checks of a module's
 * suitability for use with another.
 *
 * Module versions have both a Major and Minor value which specify the version
 * of the interface only. These are defined in the following way:
 *
 * @li Major: This version is incremented whenever a compatibility-breaking
 * change is made. For example, removing an interface function.
 * @li Minor: This version is incremented whenever an interface change is made
 * that does not break compatibility. For example, adding a new function to the
 * interface. This value is reset to zero whenever the major number is
 * incremented.
 *
 * When providing a driver module that will be used with this system, the public
 * header must include a major and minor define of the following form:
 *
 * @code
 * #define MALI_MODULE_<module>_MAJOR X
 * #define MALI_MODULE_<module>_MINOR Y
 * @endcode
 * e.g. for a module CZAM with include czam/mali_czam.h
 * @code
 *
 * #define MALI_MODULE_CZAM_MAJOR 1
 * #define MALI_MODULE_CZAM_MINOR 0
 * @endcode
 *
 * The version assertion macros outlined below are wrapped with a static function.
 * This provides more useful error messages when the assertions fail, and allows
 * the assertions to be specified adjacent to the inclusion of the module header.
 *
 * These macros should be used in the global scope of the file. Normal use would be:
 *
 * @code
 * #include <modulex/mali_modulex.h>
 * #include <moduley/mali_moduley.h>
 * #include <modulez/mali_modulez.h>
 * #include <modulez/mali_modulew.h>
 *
 * // This module added an enum we needed on minor 4 of major release 2
 * MALI_MODULE_ASSERT_MAJOR_EQUALS_MINOR_AT_LEAST( MODULEW, 2, 4 )
 *
 * // this module really needs to be a specific version
 * MALI_MODULE_ASSERT_EQUALS( MODULEX, 2, 0 )
 *
 * // 1.4 has performance problems
 * MALI_MODULE_ASSERT_MAXIMUM( MODULEY, 1, 3 )
 *
 * // Major defines a backward compatible series of versions
 * MALI_MODULE_ASSERT_MAJOR_EQUALS( MODULEZ, 1 )
 * @endcode
 *
 * @par Version Assertions
 *
 * This module provides the following compile time version assertion macros.
 *
 * @li #MALI_MODULE_ASSERT_MAJOR_EQUALS_MINOR_AT_LEAST
 * @li #MALI_MODULE_ASSERT_MAJOR_EQUALS
 * @li #MALI_MODULE_ASSERT_EQUALS
 * @li #MALI_MODULE_ASSERT_MINIMUM
 * @li #MALI_MODULE_ASSERT_MAXIMUM
 *
 * @par Limitations
 *
 * To allow the macros to be placed in the global scope and report more readable
 * errors, they produce a static function. This makes them unsuitable for use
 * within headers as the names are only unique on the name of the module under test,
 * the line number of the current file, and assert type (min, max, equals, ...).
 */

/**
 * @addtogroup malisw_version 
 * @{
 */

#include "arm_cstd/arm_cstd.h"

/**
 * Private helper macro, indirection so that __LINE__ resolves correctly.
 */
#define MALIP_MODULE_ASSERT_FUNCTION_SIGNATURE2( module, type, line ) \
	static INLINE void _mali_module_##module##_version_check_##type##_##line(void)

#define MALIP_MODULE_ASSERT_FUNCTION_SIGNATURE( module, type, line ) \
	MALIP_MODULE_ASSERT_FUNCTION_SIGNATURE2( module, type, line )

/**
 * @hideinitializer
 * This macro provides a compile time assert that a module interface that has been
 * @#included in the source base has is greater than or equal to the version specified.
 *
 * Expected use is for cases where a module version before the requested minimum
 * does not support a specific function or is missing an enum affecting the code that is
 * importing the module.
 *
 * It should be invoked at the global scope and ideally following straight after
 * the module header has been included. For example:
 *
 * @code
 * #include <modulex/mali_modulex.h>
 *
 * MALI_MODULE_ASSERT_MINIMUM( MODULEX, 1, 3 ) 
 * @endcode
 */
#define MALI_MODULE_ASSERT_MINIMUM( module, major, minor ) \
	MALIP_MODULE_ASSERT_FUNCTION_SIGNATURE( module, minimum, __LINE__ ) \
	{ \
		CSTD_COMPILE_TIME_ASSERT( ( ( MALI_MODULE_##module##_MAJOR << 16 ) | MALI_MODULE_##module##_MINOR ) \
					  >= ( ( (major) << 16 ) | (minor) ) ); \
	}

/**
 * @hideinitializer
 * This macro provides a compile time assert that a module interface that has been
 * @#included in the source base is less than or equal to the version specified.
 *
 * Expected use is for cases where a later published minor version is found to be
 * incompatible in some way after the new minor has been issued.
 *
 * It should be invoked at the global scope and ideally following straight after
 * the module header has been included. For example:
 *
 * @code
 * #include <modulex/mali_modulex.h>
 *
 * MALI_MODULE_ASSERT_MAXIMUM( MODULEX, 1, 3 ) 
 * @endcode
 */
#define MALI_MODULE_ASSERT_MAXIMUM( module, major, minor ) \
	MALIP_MODULE_ASSERT_FUNCTION_SIGNATURE( module, maximum, __LINE__ ) \
	{ \
		CSTD_COMPILE_TIME_ASSERT( ( ( MALI_MODULE_##module##_MAJOR << 16 ) | MALI_MODULE_##module##_MINOR ) \
					  <= ( ( (major) << 16 ) | (minor) ) ); \
	}

/**
 * @hideinitializer
 * This macro provides a compile time assert that a module interface that has been
 * @#included in the source base is equal to the version specified.
 *
 * Expected use is for cases where a specific version is known to work and other
 * versions are considered to be risky.
 *
 * It should be invoked at the global scope and ideally following straight after
 * the module header has been included. For example:
 *
 * @code
 * #include <modulex/mali_modulex.h>
 *
 * MALI_MODULE_ASSERT_EQUALS( MODULEX, 1, 3 ) 
 * @endcode
 */
#define MALI_MODULE_ASSERT_EQUALS( module, major, minor ) \
	MALIP_MODULE_ASSERT_FUNCTION_SIGNATURE( module, equals, __LINE__ ) \
	{ \
		CSTD_COMPILE_TIME_ASSERT( MALI_MODULE_##module##_MAJOR == major ); \
		CSTD_COMPILE_TIME_ASSERT( MALI_MODULE_##module##_MINOR == minor ); \
	}

/**
 * @hideinitializer
 * This macro provides a compile time assert that a module interface that has been
 * @#included in the source base has a major version equal to the major version specified.
 *
 * Expected use is for cases where a module is considered low risk and any minor changes
 * are not considered to be important.
 *
 * It should be invoked at the global scope and ideally following straight after
 * the module header has been included. For example:
 *
 * @code
 * #include <modulex/mali_modulex.h>
 *
 * MALI_MODULE_ASSERT_MAJOR_EQUALS( MODULEX, 1, 3 ) 
 * @endcode
 */
#define MALI_MODULE_ASSERT_MAJOR_EQUALS( module, major ) \
	MALIP_MODULE_ASSERT_FUNCTION_SIGNATURE( module, major_equals, __LINE__ ) \
	{ \
		CSTD_COMPILE_TIME_ASSERT( MALI_MODULE_##module##_MAJOR == major ); \
	}

/**
 * @hideinitializer
 * This macro provides a compile time assert that a module interface that has been
 * @#included in the source base has a major version equal to the major version specified
 * and that the minor version is at least that which is specified.
 *
 * Expected use is for cases where a major revision is suitable from a specific minor
 * revision but future major versions are a risk.
 *
 * It should be invoked at the global scope and ideally following straight after
 * the module header has been included. For example:
 *
 * @code
 * #include <modulex/mali_modulex.h>
 *
 * MALI_MODULE_ASSERT_MAJOR_EQUALS_MINOR_AT_LEAST( MODULEX, 1, 3 ) 
 * @endcode
 */
#define MALI_MODULE_ASSERT_MAJOR_EQUALS_MINOR_AT_LEAST( module, major, minor ) \
	MALIP_MODULE_ASSERT_FUNCTION_SIGNATURE( module, major_equals_minor_at_least, __LINE__ ) \
	{ \
		CSTD_COMPILE_TIME_ASSERT( MALI_MODULE_##module##_MAJOR == major ); \
		CSTD_COMPILE_TIME_ASSERT( MALI_MODULE_##module##_MINOR >= minor ); \
	}

/* @} */

/* @} */

#endif /* _MALISW_VERSION_MACROS_H_ */
