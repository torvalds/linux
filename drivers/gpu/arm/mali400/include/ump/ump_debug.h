/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2005-2011, 2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file ump_debug.h
 *
 * The file include several useful macros for debugging and printing.
 * - UMP_PRINTF(...)           Do not use this function: Will be included in Release builds.
 * - UMP_DEBUG_TRACE()         Prints current location in code.
 * - UMP_DEBUG_PRINT(nr, (X) ) Prints the second argument if nr<=UMP_DEBUG_LEVEL.
 * - UMP_DEBUG_TPRINT(nr, X )  Prints the source trace and second argument if nr<=UMP_DEBUG_LEVEL.
 * - UMP_DEBUG_ERROR( (X) )    Prints an errortext, a source trace, and the given error message.
 * - UMP_DEBUG_ASSERT(exp,(X)) If the asserted expr is false, the program will exit.
 * - UMP_DEBUG_ASSERT_RANGE(x, min, max) Triggers if variable x is not between or equal to max and min.
 * - UMP_DEBUG_ASSERT_LEQ(x, max) Triggers if variable x is not less than equal to max.
 * - UMP_DEBUG_ASSERT_POINTER(pointer)  Triggers if the pointer is a zero pointer.
 * - UMP_DEBUG_CODE( X )       The code inside the macro is only copiled in Debug builds.
 *
 * The (X) means that you must add an extra parantese around the argumentlist.
 *
 * The  printf function: UMP_PRINTF(...) is routed to _ump_sys_printf
 *
 * Suggested range for the DEBUG-LEVEL is [1:6] where
 * [1:2] Is messages with highest priority, indicate possible errors.
 * [3:4] Is messages with medium priority, output important variables.
 * [5:6] Is messages with low priority, used during extensive debugging.
 *
 */
#ifndef _UMP_DEBUG_H_
#define _UMP_DEBUG_H_

#include <stdio.h>
#include <stdlib.h>

/* START: Configuration */
#ifndef UMP_PRINTF
#define UMP_PRINTF printf
#endif /* UMP_PRINTF */

#ifndef UMP_PRINT_FLUSH
#define UMP_PRINT_FLUSH do {} while (0)
#endif /* UMP_PRINT_FLUSH */

#ifndef UMP_DEBUG_LEVEL
#define UMP_DEBUG_LEVEL 1
#endif /* UMP_DEBUG_LEVEL */

#ifndef UMP_DEBUG_ERROR_START_MSG
#define UMP_DEBUG_ERROR_START_MSG do {\
		UMP_PRINTF("*********************************************************************\n");\
		UMP_PRINT_FLUSH; } while (0)
#endif /* UMP_DEBUG_ERROR_START_MSG */

#ifndef UMP_DEBUG_ERROR_STOP_MSG
#define UMP_DEBUG_ERROR_STOP_MSG  do { UMP_PRINTF("\n"); UMP_PRINT_FLUSH; } while (0)
#endif /* UMP_DEBUG_ERROR_STOP_MSG */

#ifndef UMP_ASSERT_QUIT_CMD
#define UMP_ASSERT_QUIT_CMD    abort()
#endif /* UMP_ASSERT_QUIT_CMD */
/* STOP: Configuration */

/**
 *  The macro UMP_FUNCTION evaluates to the name of the function enclosing
 *  this macro's usage, or "<unknown>" if not supported.
 */
#if (defined(__SYMBIAN32__) && defined(__ARMCC__)) || defined(_MSC_VER)
#   define UMP_FUNCTION __FUNCTION__
#elif __STDC__  && __STDC_VERSION__ >= 199901L
#   define UMP_FUNCTION __FUNCTION__
#elif defined(__GNUC__) && __GNUC__ >= 2
#   define UMP_FUNCTION __FUNCTION__
#elif defined(__func__)
#   define UMP_FUNCTION __func__
#else
#   define UMP_FUNCTION "<unknown>"
#endif

/**
 *  Explicitly ignore a parameter passed into a function, to suppress compiler warnings.
 *  Should only be used with parameter names.
 */
#define UMP_IGNORE(x) (void)x

/**
 * @def     UMP_DEBUG_TRACE()
 * @brief   Prints current location in code.
 *          Can be turned off by defining UMP_DEBUG_SKIP_TRACE
 */

#ifndef UMP_DEBUG_SKIP_TRACE
#ifndef UMP_DEBUG_SKIP_PRINT_FUNCTION_NAME
#define UMP_DEBUG_TRACE()  do { UMP_PRINTF( "In file: "__FILE__ \
	        "  function: %s()   line:%4d\n" , UMP_FUNCTION, __LINE__);  UMP_PRINT_FLUSH; } while (0)
#else
#define UMP_DEBUG_TRACE()  do { UMP_PRINTF( "In file: "__FILE__ "  line:%4d\n" , __LINE__);  UMP_PRINT_FLUSH; } while (0)
#endif /* UMP_DEBUG_SKIP_PRINT_FUNCTION_NAME */
#else
#define UMP_DEBUG_TRACE()
#endif /* UMP_DEBUG_SKIP_TRACE */

/**
 * @def     UMP_DEBUG_PRINT(nr, (X) )
 * @brief   Prints the second argument if nr<=UMP_DEBUG_LEVEL.
 *          Can be turned off by defining UMP_DEBUG_SKIP_PRINT
 * @param   nr   If nr <= UMP_DEBUG_LEVEL, we print the text.
 * @param   X  A parantese with the contents to be sent to UMP_PRINTF
 */
#ifndef UMP_DEBUG_SKIP_PRINT
#define UMP_DEBUG_PRINT(nr, X )  do { if ( nr<=UMP_DEBUG_LEVEL ) { UMP_PRINTF X ; UMP_PRINT_FLUSH; } } while (0)
#else
#define UMP_DEBUG_PRINT(nr, X )
#endif /* UMP_DEBUG_SKIP_PRINT */

/**
 * @def     UMP_DEBUG_TPRINT(nr, (X) )
 * @brief   Prints the second argument if nr<=UMP_DEBUG_LEVEL.
 *          Can be turned off by defining UMP_DEBUG_SKIP_TPRINT.
 *          Can be shortened by defining UMP_DEBUG_TPRINT_SKIP_FUNCTION.
 * @param   nr   If nr <= UMP_DEBUG_LEVEL, we print the text.
 * @param   X  A parantese with the contents to be sent to UMP_PRINTF
 */

/* helper to handle if the function name should be included or not */
#ifndef UMP_DEBUG_TPRINT_SKIP_FUNCTION
#define UMP_DEBUG_TPRINT_INTERN do {UMP_PRINTF( ""__FILE__" %s()%4d " , UMP_FUNCTION, __LINE__); UMP_PRINT_FLUSH; }  while (0)
#else
#define UMP_DEBUG_TPRINT_INTERN do {UMP_PRINTF( ""__FILE__ "%4d " , __LINE__); UMP_PRINT_FLUSH; }  while (0)
#endif /* UMP_DEBUG_TPRINT_SKIP_FUNCTION */

#ifndef UMP_DEBUG_SKIP_TPRINT
#define UMP_DEBUG_TPRINT(nr, X ) \
	do{\
		if ( nr<=UMP_DEBUG_LEVEL )\
		{\
			UMP_DEBUG_TPRINT_INTERN;\
			UMP_PRINTF X ;\
			UMP_PRINT_FLUSH;\
		}\
	} while (0)
#else
#define UMP_DEBUG_TPRINT(nr, X )
#endif /* UMP_DEBUG_SKIP_TPRINT */

/**
 * @def     UMP_DEBUG_ERROR( (X) )
 * @brief   Prints an errortext, a source Trace, and the given error message.
 *          Prints filename, function, linenr, and the given error message.
 *          The error message must be inside a second parantese.
 *          The error message is written on a separate line, and a NL char is added.
 *          Can be turned of by defining UMP_DEBUG_SKIP_ERROR;
 *          You do not need to type the words ERROR in the message, since it will
 *          be added anyway.
 *
 * @note    You should not end the text with a newline, since it is added by the macro.
 * @note    You should not write "ERROR" in the text, since it is added by the macro.
 * @param    X  A parantese with the contents to be sent to UMP_PRINTF
 */

#ifndef UMP_DEBUG_SKIP_ERROR
#define UMP_DEBUG_ERROR( X )  \
	do{ \
		UMP_DEBUG_ERROR_START_MSG;\
		UMP_PRINTF("ERROR: ");\
		UMP_PRINT_FLUSH;\
		UMP_DEBUG_TRACE(); \
		UMP_PRINTF X ; \
		UMP_PRINT_FLUSH;\
		UMP_DEBUG_ERROR_STOP_MSG;\
	} while (0)
#else
#define UMP_DEBUG_ERROR( X ) do{ ; } while ( 0 )
#endif /* UMP_DEBUG_SKIP_ERROR */

/**
 * @def     UMP_DEBUG_ASSERT(expr, (X) )
 * @brief   If the asserted expr is false, the program will exit.
 *          Prints filename, function, linenr, and the given error message.
 *          The error message must be inside a second parantese.
 *          The error message is written on a separate line, and a NL char is added.
 *          Can be turned of by defining UMP_DEBUG_SKIP_ERROR;
 *          You do not need to type the words ASSERT in the message, since it will
 *          be added anyway.
 *
 * @param    X  A parantese with the contents to be sent to UMP_PRINTF
 *          Prints filename, function, linenr, and the error message
 *          on a separte line. A newline char is added at the end.
 *          Can be turned of by defining UMP_DEBUG_SKIP_ASSERT
 * @param   expr  Will exit program if \a expr is false;
 * @param   (X)  Text that will be written if the assertion toggles.
 */

#ifndef UMP_DEBUG_SKIP_ASSERT
#define UMP_DEBUG_ASSERT(expr, X ) \
	do{\
		if ( !(expr) ) \
		{ \
			UMP_DEBUG_ERROR_START_MSG;\
			UMP_PRINTF("ASSERT EXIT: ");\
			UMP_PRINT_FLUSH;\
			UMP_DEBUG_TRACE(); \
			UMP_PRINTF X ; \
			UMP_PRINT_FLUSH;\
			UMP_DEBUG_ERROR_STOP_MSG;\
			UMP_ASSERT_QUIT_CMD;\
		}\
	} while (0)
#else
#define UMP_DEBUG_ASSERT(expr, X)
#endif /* UMP_DEBUG_SKIP_ASSERT */


/**
 * @def     UMP_DEBUG_ASSERT_POINTER(pointer)
 * @brief   If the asserted pointer is NULL, the program terminates and TRACE info is printed
 *          The checking is disabled if "UMP_DEBUG_SKIP_ASSERT" is defined.
 */
#define UMP_DEBUG_ASSERT_POINTER(pointer) UMP_DEBUG_ASSERT(pointer, ("Null pointer " #pointer) )

/**
 * @def     UMP_DEBUG_ASSERT_HANDLE(handle)
 * @brief   If the asserted handle is not a valid handle, the program terminates and TRACE info is printed
 *          The checking is disabled if "UMP_DEBUG_SKIP_ASSERT" is defined.
 */
#define UMP_DEBUG_ASSERT_HANDLE(handle) UMP_DEBUG_ASSERT(UMP_NO_HANDLE != (handle), ("Invalid handle" #handle) )

/**
 * @def     UMP_DEBUG_ASSERT_ALIGNMENT(ptr, align)
 * @brief   If the asserted pointer is  not aligned to align, the program terminates with trace info printed.
 *          The checking is disabled if "UMP_DEBUG_SKIP_ASSERT" is defined.
 */
#ifndef UMP_DEBUG_SKIP_ASSERT
#define UMP_DEBUG_ASSERT_ALIGNMENT(ptr, align) do {                                                    \
		UMP_DEBUG_ASSERT(0 == (align & (align - 1)), ("align %d is not a power-of-two", align));           \
		UMP_DEBUG_ASSERT(0 == (((u32)(ptr)) & (align - 1)), ("ptr %p not aligned to %d bytes", (void*)ptr, align)); \
	} while (0)
#else
#define UMP_DEBUG_ASSERT_ALIGNMENT(ptr, align)
#endif /* UMP_DEBUG_SKIP_ASSERT */

/**
 * @def     UMP_DEBUG_ASSERT_RANGE(x,min,max)
 * @brief   If variable x is not between or equal to max and min, the assertion triggers.
 *          The checking is disabled if "UMP_DEBUG_SKIP_ASSERT" is defined.
 */
#define UMP_DEBUG_ASSERT_RANGE(x, min, max) \
	UMP_DEBUG_ASSERT( (x) >= (min) && (x) <= (max), \
	                  (#x " out of range (%2.2f)", (double)x ) \
	                )

/**
 * @def     UMP_DEBUG_ASSERT_LEQ(x,max)
 * @brief   If variable x is less than or equal to max, the assertion triggers.
 *          The checking is disabled if "UMP_DEBUG_SKIP_ASSERT" is defined.
 */
#define UMP_DEBUG_ASSERT_LEQ(x, max) \
	UMP_DEBUG_ASSERT( (x) <= (max), \
	                  (#x " out of range (%2.2f)", (double)x ) \
	                )

/**
 * @def     UMP_DEBUG_CODE( X )
 * @brief   Run the code X on debug builds.
 *          The code will not be used if UMP_DEBUG_SKIP_CODE is defined .
 *
 */
#ifdef UMP_DEBUG_SKIP_CODE
#define UMP_DEBUG_CODE( X )
#else
#define UMP_DEBUG_CODE( X ) X
#endif /* UMP_DEBUG_SKIP_CODE */

#endif /* _UMP_DEBUG_H_ */

