/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2007-2010, 2012-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
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

/**
*  Fundamental error macro. Reports an error code. This is abstracted to allow us to
*  easily switch to a different error reporting method if we want, and also to allow
*  us to search for error returns easily.
*
*  Note no closing semicolon - this is supplied in typical usage:
*
*  MALI_ERROR(MALI_ERROR_OUT_OF_MEMORY);
*/
#define MALI_ERROR(error_code) return (error_code)

/**
 *  Basic error macro, to indicate success.
 *  Note no closing semicolon - this is supplied in typical usage:
 *
 *  MALI_SUCCESS;
 */
#define MALI_SUCCESS MALI_ERROR(_MALI_OSK_ERR_OK)

/**
 *  Basic error macro. This checks whether the given condition is true, and if not returns
 *  from this function with the supplied error code. This is a macro so that we can override it
 *  for stress testing.
 *
 *  Note that this uses the do-while-0 wrapping to ensure that we don't get problems with dangling
 *  else clauses. Note also no closing semicolon - this is supplied in typical usage:
 *
 *  MALI_CHECK((p!=NULL), ERROR_NO_OBJECT);
 */
#define MALI_CHECK(condition, error_code) do { if(!(condition)) MALI_ERROR(error_code); } while(0)

/**
 *  Error propagation macro. If the expression given is anything other than
 *  _MALI_OSK_NO_ERROR, then the value is returned from the enclosing function
 *  as an error code. This effectively acts as a guard clause, and propagates
 *  error values up the call stack. This uses a temporary value to ensure that
 *  the error expression is not evaluated twice.
 *  If the counter for forcing a failure has been set using _mali_force_error,
 *  this error will be returned without evaluating the expression in
 *  MALI_CHECK_NO_ERROR
 */
#define MALI_CHECK_NO_ERROR(expression) \
	do { _mali_osk_errcode_t _check_no_error_result=(expression); \
		if(_check_no_error_result != _MALI_OSK_ERR_OK) \
			MALI_ERROR(_check_no_error_result); \
	} while(0)

/**
 *  Pointer check macro. Checks non-null pointer.
 */
#define MALI_CHECK_NON_NULL(pointer, error_code) MALI_CHECK( ((pointer)!=NULL), (error_code) )

/**
 *  Error macro with goto. This checks whether the given condition is true, and if not jumps
 *  to the specified label using a goto. The label must therefore be local to the function in
 *  which this macro appears. This is most usually used to execute some clean-up code before
 *  exiting with a call to ERROR.
 *
 *  Like the other macros, this is a macro to allow us to override the condition if we wish,
 *  e.g. to force an error during stress testing.
 */
#define MALI_CHECK_GOTO(condition, label) do { if(!(condition)) goto label; } while(0)

/**
 *  Explicitly ignore a parameter passed into a function, to suppress compiler warnings.
 *  Should only be used with parameter names.
 */
#define MALI_IGNORE(x) x=x

#if defined(CONFIG_MALI_QUIET)
#define MALI_PRINTF(args)
#else
#define MALI_PRINTF(args) _mali_osk_dbgmsg args;
#endif

#define MALI_PRINT_ERROR(args) do{ \
		MALI_PRINTF(("Mali: ERR: %s\n" ,__FILE__)); \
		MALI_PRINTF(("           %s()%4d\n           ", __FUNCTION__, __LINE__)) ; \
		MALI_PRINTF(args); \
		MALI_PRINTF(("\n")); \
	} while(0)

#define MALI_PRINT(args) do{ \
		MALI_PRINTF(("Mali: ")); \
		MALI_PRINTF(args); \
	} while (0)

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
