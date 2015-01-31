/*
 * Copyright (C) 2010, 2012-2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_COMMON_H__
#define __MALI_KERNEL_COMMON_H__

#include "mali_osk.h"

/* Make sure debug is defined when it should be */
#ifndef DEBUG
#if defined(_DEBUG)
#define DEBUG
#endif
#endif

/* The file include several useful macros for error checking, debugging and printing.
 * - MALI_PRINTF(...)           Do not use this function: Will be included in Release builds.
 * - MALI_DEBUG_PRINT(nr, (X) ) Prints the second argument if nr<=MALI_DEBUG_LEVEL.
 * - MALI_DEBUG_ERROR( (X) )    Prints an errortext, a source trace, and the given error message.
 * - MALI_DEBUG_ASSERT(exp,(X)) If the asserted expr is false, the program will exit.
 * - MALI_DEBUG_ASSERT_POINTER(pointer)  Triggers if the pointer is a zero pointer.
 * - MALI_DEBUG_CODE( X )       The code inside the macro is only compiled in Debug builds.
 *
 * The (X) means that you must add an extra parenthesis around the argumentlist.
 *
 * The  printf function: MALI_PRINTF(...) is routed to _mali_osk_debugmsg
 *
 * Suggested range for the DEBUG-LEVEL is [1:6] where
 * [1:2] Is messages with highest priority, indicate possible errors.
 * [3:4] Is messages with medium priority, output important variables.
 * [5:6] Is messages with low priority, used during extensive debugging.
 */

#ifdef DEBUG
#ifndef mali_debug_level
extern int mali_debug_level;
#endif

#define MALI_DEBUG_CODE(code) code
#define MALI_DEBUG_PRINT(level, args)  do { \
	if((level) <=  mali_debug_level)\
        {MALI_PRINTF(("Mali<" #level ">: ")); MALI_PRINTF(args); } \
	} while (0)

#define MALI_DEBUG_PRINT_ERROR(args) MALI_PRINT_ERROR(args)

#define MALI_DEBUG_PRINT_IF(level,condition,args)  \
	if((condition)&&((level) <=  mali_debug_level))\
        {MALI_PRINTF(("Mali<" #level ">: ")); MALI_PRINTF(args); }

#define MALI_DEBUG_PRINT_ELSE(level, args)\
	else if((level) <=  mali_debug_level)\
    { MALI_PRINTF(("Mali<" #level ">: ")); MALI_PRINTF(args); }

/**
 * @note these variants of DEBUG ASSERTS will cause a debugger breakpoint
 * to be entered (see _mali_osk_break() ). An alternative would be to call
 * _mali_osk_abort(), on OSs that support it.
 */
#define MALI_DEBUG_PRINT_ASSERT(condition, args) do  {if( !(condition)) { MALI_PRINT_ERROR(args); _mali_osk_break(); } } while(0)
#define MALI_DEBUG_ASSERT_POINTER(pointer) do  {if( (pointer)== NULL) {MALI_PRINT_ERROR(("NULL pointer " #pointer)); _mali_osk_break();} } while(0)
#define MALI_DEBUG_ASSERT(condition) do  {if( !(condition)) {MALI_PRINT_ERROR(("ASSERT failed: " #condition )); _mali_osk_break();} } while(0)

#else /* DEBUG */

#define MALI_DEBUG_CODE(code)
#define MALI_DEBUG_PRINT(string,args) do {} while(0)
#define MALI_DEBUG_PRINT_ERROR(args) do {} while(0)
#define MALI_DEBUG_PRINT_IF(level,condition,args) do {} while(0)
#define MALI_DEBUG_PRINT_ELSE(level,condition,args) do {} while(0)
#define MALI_DEBUG_PRINT_ASSERT(condition,args) do {} while(0)
#define MALI_DEBUG_ASSERT_POINTER(pointer) do {} while(0)
#define MALI_DEBUG_ASSERT(condition) do {} while(0)

#endif /* DEBUG */

/**
 * variables from user space cannot be dereferenced from kernel space; tagging them
 * with __user allows the GCC compiler to generate a warning. Other compilers may
 * not support this so we define it here as an empty macro if the compiler doesn't
 * define it.
 */
#ifndef __user
#define __user
#endif

#endif /* __MALI_KERNEL_COMMON_H__ */
