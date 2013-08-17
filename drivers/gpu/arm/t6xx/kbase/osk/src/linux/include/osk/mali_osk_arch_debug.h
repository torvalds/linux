/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_ARCH_DEBUG_H_
#define _OSK_ARCH_DEBUG_H_

#include <malisw/mali_stdtypes.h>
#include "mali_osk_arch_types.h"

#if MALI_UNIT_TEST
/* Kernel testing helpers */

/** Callback to run during kassertp_test_exit() */
typedef void (*kassert_test_cb)(void *);

void      kassert_test_init(void);
void      kassert_test_term(void);
void      kassert_test_wait(void);
void      kassert_test_signal(void);
mali_bool kassert_test_has_asserted(void);
void      kassert_test_register_cb(kassert_test_cb cb, void *param);
void      kassertp_test_exit(void);
#endif

/** Maximum number of bytes (incl. end of string character) supported in the generated debug output string */
#define OSK_DEBUG_MESSAGE_SIZE 256

/**
 * All OSKP_ASSERT* and OSKP_PRINT_* macros will eventually call OSKP_PRINT to output messages
 */
void oskp_debug_print(const char *fmt, ...);
#define OSKP_PRINT(...) oskp_debug_print(__VA_ARGS__)

/**
 * Insert a breakpoint to cause entry in an attached debugger. However, since there is
 * no API available to trigger entry in a debugger, we dereference a NULL
 * pointer which should cause an exception and enter a debugger.
 */
#define OSKP_BREAKPOINT() *(int *)0 = 0

/**
 * Quit the driver and halt.
 */
#define OSKP_QUIT() BUG()

/**
 * Print a backtrace
 */
#define OSKP_TRACE() WARN_ON(1)

#define OSKP_CHANNEL_INFO      ((u32)0x00000001)      /**< @brief No output*/
#define OSKP_CHANNEL_WARN      ((u32)0x00000002)      /**< @brief Standard output*/
#define OSKP_CHANNEL_ERROR     ((u32)0x00000004)      /**< @brief Error output*/
#define OSKP_CHANNEL_RAW       ((u32)0x00000008)      /**< @brief Raw output*/
#define OSKP_CHANNEL_ALL       ((u32)0xFFFFFFFF)      /**< @brief All the channels at the same time*/

/** @brief Disable the asserts tests if set to 1. Default is to disable the asserts in release. */
#ifndef OSK_DISABLE_ASSERTS
#ifdef CONFIG_MALI_DEBUG
#define OSK_DISABLE_ASSERTS 0
#else
#define OSK_DISABLE_ASSERTS 1
#endif
#endif /* OSK_DISABLE_ASSERTS */

/* Lock order assertion check requires macro to define the order variable on the stack */
#if OSK_DISABLE_ASSERTS == 0
#define OSK_ORDER_VAR_DEFINITION(order) osk_lock_order __oskp_order__ = (order);
#else
#define OSK_ORDER_VAR_DEFINITION(order) CSTD_NOP()
#endif

/** @brief If equals to 0, a trace containing the file, line, and function will be displayed before each message. */
#define OSK_SKIP_TRACE 0

/** @brief If different from 0, the trace will only contain the file and line. */
#define OSK_SKIP_FUNCTION_NAME 0

/** @brief Variable to set the permissions per module and per channel.
 */
#define OSK_MODULES_PERMISSIONS "ALL_ALL"

/** @brief String terminating every message printed by the debug API */
#define OSK_STOP_MSG "\n"

/** @brief Enables support for runtime configuration if set to 1.
 */
#define OSK_USE_RUNTIME_CONFIG 0

/**< @brief Enables simulation of failures (for testing) if non-zero */
#ifdef CONFIG_MALI_QA_RESFAIL
#define OSK_SIMULATE_FAILURES 1
#else
#define OSK_SIMULATE_FAILURES 0
#endif

#define OSK_ACTION_IGNORE             0 /**< @brief The given message is ignored then the execution continues*/
#define OSK_ACTION_PRINT_AND_CONTINUE 1 /**< @brief The given message is printed then the execution continues*/
#define OSK_ACTION_PRINT_AND_BREAK    2 /**< @brief The given message is printed then a break point is triggered*/
#define OSK_ACTION_PRINT_AND_QUIT     3 /**< @brief The given message is printed then the execution is stopped*/
#define OSK_ACTION_PRINT_AND_TRACE    4 /**< @brief The given message and a backtrace is printed then the execution continues*/

/**
 * @def OSK_ON_INFO
 * @brief Defines the API behavior when @ref OSK_PRINT_INFO() is called
 * @note Must be set to one of the following values: @see OSK_ACTION_PRINT_AND_CONTINUE,
 * @note @ref OSK_ACTION_PRINT_AND_BREAK, @see OSK_ACTION_PRINT_AND_QUIT, @see OSK_ACTION_IGNORE
 *
 * @def OSK_ON_WARN
 * @brief Defines the API behavior when @see OSK_PRINT_WARN() is called
 * @note Must be set to one of the following values: @see OSK_ACTION_PRINT_AND_CONTINUE,
 * @note @see OSK_ACTION_PRINT_AND_BREAK, @see OSK_ACTION_PRINT_AND_QUIT, @see OSK_ACTION_IGNORE
 *
 * @def OSK_ON_ERROR
 * @brief Defines the API behavior when @see OSK_PRINT_ERROR() is called
 * @note Must be set to one of the following values: @see OSK_ACTION_PRINT_AND_CONTINUE,
 * @note @see OSK_ACTION_PRINT_AND_BREAK, @see OSK_ACTION_PRINT_AND_QUIT, @see OSK_ACTION_IGNORE
 *
 * @def OSK_ON_ASSERT
 * @brief Defines the API behavior when @see OSKP_PRINT_ASSERT() is called
 * @note Must be set to one of the following values: @see OSK_ACTION_PRINT_AND_CONTINUE,
 * @note @see OSK_ACTION_PRINT_AND_BREAK, @see OSK_ACTION_PRINT_AND_QUIT, @see OSK_ACTION_IGNORE
 *
 * @def OSK_ON_RAW
 * @brief Defines the API behavior when @see OSKP_PRINT_RAW() is called
 * @note Must be set to one of the following values: @see OSK_ACTION_PRINT_AND_CONTINUE,
 * @note @see OSK_ACTION_PRINT_AND_BREAK, @see OSK_ACTION_PRINT_AND_QUIT, @see OSK_ACTION_IGNORE
 *
 *
 */
#ifdef CONFIG_MALI_DEBUG
	#define OSK_ON_INFO                OSK_ACTION_IGNORE
	#define OSK_ON_WARN                OSK_ACTION_PRINT_AND_CONTINUE
	#define OSK_ON_ASSERT              OSK_ACTION_PRINT_AND_QUIT
	#define OSK_ON_ERROR               OSK_ACTION_PRINT_AND_CONTINUE
	#define OSK_ON_RAW                 OSK_ACTION_PRINT_AND_CONTINUE
#else
	#define OSK_ON_INFO                OSK_ACTION_IGNORE
	#define OSK_ON_WARN                OSK_ACTION_IGNORE
	#define OSK_ON_ASSERT              OSK_ACTION_IGNORE
	#define OSK_ON_ERROR               OSK_ACTION_PRINT_AND_CONTINUE
	#define OSK_ON_RAW                 OSK_ACTION_PRINT_AND_CONTINUE
#endif /* CONFIG_MALI_DEBUG */

#if MALI_UNIT_TEST
#define OSKP_KERNEL_TEST_ASSERT()		kassertp_test_exit()
#else
#define OSKP_KERNEL_TEST_ASSERT()		CSTD_NOP()
#endif

/**
 * OSK_ASSERT macros do nothing if the flag @see OSK_DISABLE_ASSERTS is set to 1
 */
#if OSK_DISABLE_ASSERTS
	#define OSKP_ASSERT(expr)           CSTD_NOP()
	#define OSKP_INTERNAL_ASSERT(expr)  CSTD_NOP()
	#define OSKP_ASSERT_MSG(expr, ...)  CSTD_NOP()
#else /* OSK_DISABLE_ASSERTS */

/**
 * @def OSKP_ASSERT_MSG(expr, ...)
 * @brief Calls @see OSKP_PRINT_ASSERT and prints the given message if @a expr is false
 *
 * @note This macro does nothing if the flag @see OSK_DISABLE_ASSERTS is set to 1
 *
 * @param expr Boolean expression
 * @param ...  Message to display when @a expr is false, as a format string followed by format arguments.
 */
#if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#define OSKP_ASSERT_MSG(expr, ...)\
	do\
	{\
		_Pragma("GCC diagnostic push")\
		_Pragma("GCC diagnostic ignored \"-Waddress\"")\
		if(MALI_FALSE == (expr))\
		{\
			OSKP_PRINT_ASSERT(__VA_ARGS__);\
		}\
		_Pragma("GCC diagnostic pop")\
	}while(MALI_FALSE)
#else
#define OSKP_ASSERT_MSG(expr, ...)\
	do\
	{\
		if(MALI_FALSE == (expr))\
		{\
			OSKP_PRINT_ASSERT(__VA_ARGS__);\
		}\
	}while(MALI_FALSE)
#endif /* (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) */

/**
 * @def OSKP_ASSERT(expr)
 * @brief Calls @see OSKP_PRINT_ASSERT and prints the expression @a expr if @a expr is false
 *
 * @note This macro does nothing if the flag @see OSK_DISABLE_ASSERTS is set to 1
 *
 * @param expr Boolean expression
 */
#define OSKP_ASSERT(expr)\
	OSKP_ASSERT_MSG(expr, #expr)

/**
 * @def OSKP_INTERNAL_ASSERT(expr)
 * @brief Calls @see OSKP_BREAKPOINT if @a expr is false
 * This assert function is for internal use of OSK functions which themselves are used to implement
 * the OSK_ASSERT functionality. These functions should use OSK_INTERNAL_ASSERT which does not use
 * any OSK functions to prevent ending up in a recursive loop.
 *
 * @note This macro does nothing if the flag @see OSK_DISABLE_ASSERTS is set to 1
 *
 * @param expr Boolean expression
 */
#define OSKP_INTERNAL_ASSERT(expr)\
	do\
	{\
		if(MALI_FALSE == (expr))\
		{\
			OSKP_BREAKPOINT();\
		}\
	}while(MALI_FALSE)


/**
 * @def OSK_CALL_ASSERT_HOOK()
 * @brief Calls oskp_debug_assert_call_hook
 * This will call oskp_debug_assert_call_hook to perform any registered action required
 * on assert.
 *
 * @note This macro does nothing if the flag @see CONFIG_MALI_DEBUG is not set.
 *
 */

#ifdef CONFIG_MALI_DEBUG
#define OSK_CALL_ASSERT_HOOK()\
		oskp_debug_assert_call_hook()
#else
#define OSK_CALL_ASSERT_HOOK()\
		CSTD_NOP()
#endif /* CONFIG_MALI_DEBUG */

/**
 * @def   OSKP_PRINT_ASSERT(...)
 * @brief Prints "MALI<ASSERT>" followed by trace, function name and the given message.
 *
 * The behavior of this function is defined by the macro @see OSK_ON_ASSERT.
 *
 * @note This macro does nothing if the flag @see OSK_DISABLE_ASSERTS is set to 1
 *
 * Example:  OSKP_PRINT_ASSERT(" %d blocks could not be allocated", mem_alocated) will print:\n
 * "MALI<ASSERT> In file <path> line: <line number> function:<function name> 10 blocks could not be allocated"
 *
 * @note Depending on the values of @see OSK_SKIP_FUNCTION_NAME and @see OSK_SKIP_TRACE the trace will be displayed
 * before the message.
 *
 * @param ...      Message to print, passed as a format string followed by format arguments.
 */
#define OSKP_PRINT_ASSERT(...)\
	do\
	{\
		OSKP_ASSERT_OUT(OSKP_PRINT_TRACE, OSKP_PRINT_FUNCTION, __VA_ARGS__);\
		OSK_CALL_ASSERT_HOOK();\
		OSKP_KERNEL_TEST_ASSERT();\
		OSKP_ASSERT_ACTION();\
	}while(MALI_FALSE)

#endif

/**
 * @def OSKP_DEBUG_CODE( X )
 * @brief Executes the code inside the macro only in debug mode
 *
 * @param X Code to compile only in debug mode.
 */
#ifdef CONFIG_MALI_DEBUG
	#define OSKP_DEBUG_CODE( X ) X
#else
	#define OSKP_DEBUG_CODE( X ) CSTD_NOP()
#endif /* CONFIG_MALI_DEBUG */

/**
 * @def OSKP_ASSERT_ACTION
 * @brief (Private) Action associated to the @see OSKP_PRINT_ASSERT event.
 */
/* Configure the post display action */
#if OSK_ON_ASSERT == OSK_ACTION_PRINT_AND_BREAK
	#define OSKP_ASSERT_ACTION OSKP_BREAKPOINT
#elif OSK_ON_ASSERT == OSK_ACTION_PRINT_AND_QUIT
	#define OSKP_ASSERT_ACTION OSKP_QUIT
#elif OSK_ON_ASSERT == OSK_ACTION_PRINT_AND_TRACE
	#define OSKP_ASSERT_ACTION OSKP_TRACE
#elif OSK_ON_ASSERT == OSK_ACTION_PRINT_AND_CONTINUE || OSK_ON_ASSERT == OSK_ACTION_IGNORE
	#define OSKP_ASSERT_ACTION() CSTD_NOP()
#else
	#error invalid value for OSK_ON_ASSERT
#endif

/**
 * @def OSKP_RAW_ACTION
 * @brief (Private) Action associated to the @see OSK_PRINT_RAW event.
 */
/* Configure the post display action */
#if OSK_ON_RAW == OSK_ACTION_PRINT_AND_BREAK
	#define OSKP_RAW_ACTION OSKP_BREAKPOINT
#elif OSK_ON_RAW == OSK_ACTION_PRINT_AND_QUIT
	#define OSKP_RAW_ACTION OSKP_QUIT
#elif OSK_ON_RAW == OSK_ACTION_PRINT_AND_TRACE
	#define OSKP_RAW_ACTION OSKP_TRACE
#elif OSK_ON_RAW == OSK_ACTION_PRINT_AND_CONTINUE || OSK_ON_RAW == OSK_ACTION_IGNORE
	#define OSKP_RAW_ACTION() CSTD_NOP()
#else
	#error invalid value for OSK_ON_RAW
#endif

/**
 * @def OSKP_INFO_ACTION
 * @brief (Private) Action associated to the @see OSK_PRINT_INFO event.
 */
/* Configure the post display action */
#if OSK_ON_INFO == OSK_ACTION_PRINT_AND_BREAK
	#define OSKP_INFO_ACTION OSKP_BREAKPOINT
#elif OSK_ON_INFO == OSK_ACTION_PRINT_AND_QUIT
	#define OSKP_INFO_ACTION OSKP_QUIT
#elif OSK_ON_INFO == OSK_ACTION_PRINT_AND_TRACE
	#define OSKP_INFO_ACTION OSKP_TRACE
#elif OSK_ON_INFO == OSK_ACTION_PRINT_AND_CONTINUE || OSK_ON_INFO == OSK_ACTION_IGNORE
	#define OSKP_INFO_ACTION() CSTD_NOP()
#else
	#error invalid value for OSK_ON_INFO
#endif

/**
 * @def OSKP_ERROR_ACTION
 * @brief (Private) Action associated to the @see OSK_PRINT_ERROR event.
 */
/* Configure the post display action */
#if OSK_ON_ERROR == OSK_ACTION_PRINT_AND_BREAK
	#define OSKP_ERROR_ACTION OSKP_BREAKPOINT
#elif OSK_ON_ERROR == OSK_ACTION_PRINT_AND_QUIT
	#define OSKP_ERROR_ACTION OSKP_QUIT
#elif OSK_ON_ERROR == OSK_ACTION_PRINT_AND_TRACE
	#define OSKP_ERROR_ACTION OSKP_TRACE
#elif OSK_ON_ERROR == OSK_ACTION_PRINT_AND_CONTINUE || OSK_ON_ERROR == OSK_ACTION_IGNORE
	#define OSKP_ERROR_ACTION() CSTD_NOP()
#else
	#error invalid value for OSK_ON_ERROR
#endif

/**
 * @def OSKP_WARN_ACTION
 * @brief (Private) Action associated to the @see OSK_PRINT_WARN event.
 */
/* Configure the post display action */
#if OSK_ON_WARN == OSK_ACTION_PRINT_AND_BREAK
	#define OSKP_WARN_ACTION OSKP_BREAKPOINT
#elif OSK_ON_WARN == OSK_ACTION_PRINT_AND_QUIT
	#define OSKP_WARN_ACTION OSKP_QUIT
#elif OSK_ON_WARN == OSK_ACTION_PRINT_AND_TRACE
	#define OSKP_WARN_ACTION OSKP_TRACE
#elif OSK_ON_WARN == OSK_ACTION_PRINT_AND_CONTINUE || OSK_ON_WARN == OSK_ACTION_IGNORE
	#define OSKP_WARN_ACTION() CSTD_NOP()
#else
	#error invalid value for OSK_ON_WARN
#endif

/**
 * @def   OSKP_PRINT_RAW(module, ...)
 * @brief Prints given message
 *
 * The behavior of this function is defined by macro @see OSK_ON_RAW
 *
 * Example:
 * @code OSKP_PRINT_RAW(OSK_BASE_MEM, " %d blocks could not be allocated", mem_allocated); @endcode will print:
 * \n
 * "10 blocks could not be allocated"
 *
 * @param module   Name of the module which prints the message.
 * @param ...      Format string followed by a varying number of parameters
 */
#define OSKP_PRINT_RAW(module, ...)\
	do\
	{\
		if(MALI_FALSE != OSKP_PRINT_IS_ALLOWED( (module), OSK_CHANNEL_RAW))\
		{\
			OSKP_RAW_OUT(oskp_module_to_str( (module) ), \
			    OSKP_PRINT_TRACE, OSKP_PRINT_FUNCTION, __VA_ARGS__ );\
		}\
		OSKP_RAW_ACTION();\
	}while(MALI_FALSE)

/**
 * @def   OSKP_PRINT_INFO(module, ...)
 * @brief Prints "MALI<INFO,module_name>: " followed by the given message.
 *
 * The behavior of this function is defined by the macro @see OSK_ON_INFO
 *
 * Example:
 * @code OSKP_PRINT_INFO(OSK_BASE_MEM, " %d blocks could not be allocated", mem_allocated); @endcode will print:
 * \n
 * "MALI<INFO,BASE_MEM>: 10 blocks could not be allocated"\n
 *
 * @param module   Name of the module which prints the message.
 * @param ...      Format string followed by a varying number of parameters
 */
#define OSKP_PRINT_INFO(module, ...)\
	do\
	{\
		if(MALI_FALSE != OSKP_PRINT_IS_ALLOWED( (module), OSK_CHANNEL_INFO))\
		{\
			OSKP_INFO_OUT(oskp_module_to_str( (module) ), \
			    OSKP_PRINT_TRACE, OSKP_PRINT_FUNCTION, __VA_ARGS__ );\
		}\
		OSKP_INFO_ACTION();\
	}while(MALI_FALSE)

/**
 * @def   OSKP_PRINT_WARN(module, ...)
 * @brief Prints "MALI<WARN,module_name>: " followed by the given message.
 *
 * The behavior of this function is defined by the macro @see OSK_ON_WARN
 *
 * Example:
 * @code OSK_PRINT_WARN(OSK_BASE_MEM, " %d blocks could not be allocated", mem_allocated);@endcode will print: \n
 * "MALI<WARN,BASE_MEM>: 10 blocks could not be allocated"\n
 *
 * @param module   Name of the module which prints the message.
 * @param ...      Format string followed by a varying number of parameters
 */
#define OSKP_PRINT_WARN(module, ...)\
	do\
	{\
		if(MALI_FALSE != OSKP_PRINT_IS_ALLOWED( (module), OSK_CHANNEL_WARN))\
		{\
			OSKP_WARN_OUT(oskp_module_to_str( (module) ), \
			    OSKP_PRINT_TRACE, OSKP_PRINT_FUNCTION, __VA_ARGS__ );\
		}\
		OSKP_WARN_ACTION();\
	}while(MALI_FALSE)

/**
 * @def OSKP_PRINT_ERROR(module, ...)
 * @brief Prints "MALI<ERROR,module_name>: " followed by the given message.
 *
 * The behavior of this function is defined by the macro @see OSK_ON_ERROR
 *
 * Example:
 * @code OSKP_PRINT_ERROR(OSK_BASE_MEM, " %d blocks could not be allocated", mem_allocated); @endcode will print:
 * \n
 * "MALI<ERROR,BASE_MEM>: 10 blocks could not be allocated"\n
 *
 * @param module   Name of the module which prints the message.
 * @param ...      Format string followed by a varying number of parameters
 */
#define OSKP_PRINT_ERROR(module, ...)\
	do\
	{\
		if(MALI_FALSE != OSKP_PRINT_IS_ALLOWED( (module), OSK_CHANNEL_ERROR))\
		{\
			OSKP_ERROR_OUT(oskp_module_to_str( (module) ), \
			    OSKP_PRINT_TRACE, OSKP_PRINT_FUNCTION, __VA_ARGS__ );\
		}\
		OSKP_ERROR_ACTION();\
	}while(MALI_FALSE)

/**
 * @def OSKP_PRINT_TRACE
 * @brief Private macro containing the format of the trace to display before every message
 * @sa OSK_SKIP_TRACE, OSK_SKIP_FUNCTION_NAME
 */
#if OSK_SKIP_TRACE == 0
	#define OSKP_PRINT_TRACE \
		"In file: " __FILE__ " line: " CSTD_STR2(__LINE__)
	#if OSK_SKIP_FUNCTION_NAME == 0
		#define OSKP_PRINT_FUNCTION CSTD_FUNC
	#else
		#define OSKP_PRINT_FUNCTION ""
	#endif
#else
	#define OSKP_PRINT_TRACE ""
#endif

/**
 * @def OSKP_PRINT_ALLOW(module, channel)
 * @brief Allow the given module to print on the given channel
 * @note If @see OSK_USE_RUNTIME_CONFIG is disabled then this macro doesn't do anything
 * @param module is a @see osk_module
 * @param channel is one of @see OSK_CHANNEL_INFO, @see OSK_CHANNEL_WARN, @see OSK_CHANNEL_ERROR,
 * @see OSK_CHANNEL_ALL
 * @return MALI_TRUE if the module is allowed to print on the channel.
 */
/**
 * @def OSKP_PRINT_BLOCK(module, channel)
 * @brief Prevent the given module from printing on the given channel
 * @note If @see OSK_USE_RUNTIME_CONFIG is disabled then this macro doesn't do anything
 * @param module is a @see osk_module
 * @param channel is one of @see OSK_CHANNEL_INFO, @see OSK_CHANNEL_WARN, @see OSK_CHANNEL_ERROR,
 * @see OSK_CHANNEL_ALL
 * @return MALI_TRUE if the module is allowed to print on the channel.
 */
#if OSK_USE_RUNTIME_CONFIG
	#define OSKP_PRINT_ALLOW(module, channel)   oskp_debug_print_allow( (module), (channel) )
	#define OSKP_PRINT_BLOCK(module, channel)   oskp_debug_print_block( (module), (channel) )
#else
	#define OSKP_PRINT_ALLOW(module, channel)   CSTD_NOP()
	#define OSKP_PRINT_BLOCK(module, channel)   CSTD_NOP()
#endif

/**
 * @def OSKP_RAW_OUT(module, trace, ...)
 * @brief (Private) system printing function associated to the @see OSK_PRINT_RAW event.
 * @param module module ID
 * @param trace location in the code from where the message is printed
 * @param function function from where the message is printed
 * @param ... Format string followed by format arguments.
 */
/* Select the correct system output function*/
#if OSK_ON_RAW != OSK_ACTION_IGNORE
	#define OSKP_RAW_OUT(module, trace, function, ...)\
		do\
		{\
			OSKP_PRINT(__VA_ARGS__);\
			OSKP_PRINT(OSK_STOP_MSG);\
		}while(MALI_FALSE)
#else
	#define OSKP_RAW_OUT(module, trace, function, ...) CSTD_NOP()
#endif


/**
 * @def OSKP_INFO_OUT(module, trace, ...)
 * @brief (Private) system printing function associated to the @see OSK_PRINT_INFO event.
 * @param module module ID
 * @param trace location in the code from where the message is printed
 * @param function function from where the message is printed
 * @param ... Format string followed by format arguments.
 */
/* Select the correct system output function*/
#if OSK_ON_INFO != OSK_ACTION_IGNORE
	#define OSKP_INFO_OUT(module, trace, function, ...)\
		do\
		{\
			/* Split up in several lines to prevent hitting max 128 chars limit of OSK print function */ \
			OSKP_PRINT("Mali<INFO,%s>: ", module);\
			OSKP_PRINT(__VA_ARGS__);\
			OSKP_PRINT(OSK_STOP_MSG);\
		}while(MALI_FALSE)
#else
	#define OSKP_INFO_OUT(module, trace, function, ...) CSTD_NOP()
#endif

/**
 * @def OSKP_ASSERT_OUT(trace, function, ...)
 * @brief (Private) system printing function associated to the @see OSKP_PRINT_ASSERT event.
 * @param trace location in the code from where the message is printed
 * @param function function from where the message is printed
 * @param ... Format string followed by format arguments.
 * @note function parameter cannot be concatenated with other strings
 */
/* Select the correct system output function*/
#if OSK_ON_ASSERT != OSK_ACTION_IGNORE
	#define OSKP_ASSERT_OUT(trace, function, ...)\
		do\
		{\
			/* Split up in several lines to prevent hitting max 128 chars limit of OSK print function */ \
			OSKP_PRINT("Mali<ASSERT>: %s function:%s ", trace, function);\
			OSKP_PRINT(__VA_ARGS__);\
			OSKP_PRINT(OSK_STOP_MSG);\
		}while(MALI_FALSE)
#else
	#define OSKP_ASSERT_OUT(trace, function, ...) CSTD_NOP()
#endif

/**
 * @def OSKP_WARN_OUT(module, trace, ...)
 * @brief (Private) system printing function associated to the @see OSK_PRINT_WARN event.
 * @param module module ID
 * @param trace location in the code from where the message is printed
 * @param function function from where the message is printed
 * @param ... Format string followed by format arguments.
 * @note function parameter cannot be concatenated with other strings
 */
/* Select the correct system output function*/
#if OSK_ON_WARN != OSK_ACTION_IGNORE
	#define OSKP_WARN_OUT(module, trace, function, ...)\
		do\
		{\
			/* Split up in several lines to prevent hitting max 128 chars limit of OSK print function */ \
			OSKP_PRINT("Mali<WARN,%s>: ", module);\
			OSKP_PRINT(__VA_ARGS__);\
			OSKP_PRINT(OSK_STOP_MSG);\
		}while(MALI_FALSE)
#else
	#define OSKP_WARN_OUT(module, trace, function, ...) CSTD_NOP()
#endif

/**
 * @def OSKP_ERROR_OUT(module, trace, ...)
 * @brief (Private) system printing function associated to the @see OSK_PRINT_ERROR event.
 * @param module module ID
 * @param trace location in the code from where the message is printed
 * @param function function from where the message is printed
 * @param ... Format string followed by format arguments.
 * @note function parameter cannot be concatenated with other strings
 */
/* Select the correct system output function*/
#if OSK_ON_ERROR != OSK_ACTION_IGNORE
	#define OSKP_ERROR_OUT(module, trace, function, ...)\
		do\
		{\
			/* Split up in several lines to prevent hitting max 128 chars limit of OSK print function */ \
			OSKP_PRINT("Mali<ERROR,%s>: ", module);\
			OSKP_PRINT(__VA_ARGS__);\
			OSKP_PRINT(OSK_STOP_MSG);\
		}while(MALI_FALSE)
#else
	#define OSKP_ERROR_OUT(module, trace, function, ...) CSTD_NOP()
#endif

/**
 * @def OSKP_PRINT_IS_ALLOWED(module, channel)
 * @brief function or constant indicating if the given module is allowed to print on the given channel
 * @note If @see OSK_USE_RUNTIME_CONFIG is disabled then this macro is set to MALI_TRUE to avoid any overhead
 * @param module is a @see osk_module
 * @param channel is one of @see OSK_CHANNEL_INFO, @see OSK_CHANNEL_WARN, @see OSK_CHANNEL_ERROR,
 * @see OSK_CHANNEL_ALL
 * @return MALI_TRUE if the module is allowed to print on the channel.
 */
#if OSK_USE_RUNTIME_CONFIG
	#define OSKP_PRINT_IS_ALLOWED(module, channel) oskp_is_allowed_to_print( (module), (channel) )
#else
	#define OSKP_PRINT_IS_ALLOWED(module, channel) MALI_TRUE
#endif

/**
 * @def OSKP_SIMULATE_FAILURE_IS_ENABLED(module, feature)
 * @brief Macro that evaluates as true if the specified feature is enabled for the given module
 * @note If @ref OSK_USE_RUNTIME_CONFIG is disabled then this macro always evaluates as true.
 * @param[in] module is a @ref cdbg_module
 * @param[in] channel is one of @see OSK_CHANNEL_INFO, @see OSK_CHANNEL_WARN, @see OSK_CHANNEL_ERROR,
 * @return MALI_TRUE if the feature is enabled
 */
#if OSK_USE_RUNTIME_CONFIG
#define OSKP_SIMULATE_FAILURE_IS_ENABLED(module, channel) oskp_is_allowed_to_simulate_failure( (module), (channel) )
#else
#define OSKP_SIMULATE_FAILURE_IS_ENABLED(module, channel) MALI_TRUE
#endif

OSK_STATIC_INLINE void osk_debug_get_thread_info( u32 *thread_id, u32 *cpu_nr )
{
	OSK_ASSERT( thread_id != NULL );
	OSK_ASSERT( cpu_nr != NULL );

	/* This implementation uses the PID as shown in ps listings.
	 * On 64-bit systems, this could narrow from signed 64-bit to unsigned 32-bit */
	*thread_id = (u32)task_pid_nr(current);

	/* On 64-bit systems, this could narrow from unsigned 64-bit to unsigned 32-bit */
	*cpu_nr = (u32)task_cpu(current);
}


#endif /* _OSK_ARCH_DEBUG_H_ */

