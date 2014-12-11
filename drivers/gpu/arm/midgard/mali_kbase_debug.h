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





#ifndef _KBASE_DEBUG_H
#define _KBASE_DEBUG_H

#include <linux/bug.h>

/** @brief If equals to 0, a trace containing the file, line, and function will be displayed before each message. */
#define KBASE_DEBUG_SKIP_TRACE 0

/** @brief If different from 0, the trace will only contain the file and line. */
#define KBASE_DEBUG_SKIP_FUNCTION_NAME 0

/** @brief Disable the asserts tests if set to 1. Default is to disable the asserts in release. */
#ifndef KBASE_DEBUG_DISABLE_ASSERTS
#ifdef CONFIG_MALI_DEBUG
#define KBASE_DEBUG_DISABLE_ASSERTS 0
#else
#define KBASE_DEBUG_DISABLE_ASSERTS 1
#endif
#endif				/* KBASE_DEBUG_DISABLE_ASSERTS */

/** Function type that is called on an KBASE_DEBUG_ASSERT() or KBASE_DEBUG_ASSERT_MSG() */
typedef void (kbase_debug_assert_hook) (void *);

typedef struct kbasep_debug_assert_cb {
	kbase_debug_assert_hook *func;
	void *param;
} kbasep_debug_assert_cb;

/**
 * @def KBASEP_DEBUG_PRINT_TRACE
 * @brief Private macro containing the format of the trace to display before every message
 * @sa KBASE_DEBUG_SKIP_TRACE, KBASE_DEBUG_SKIP_FUNCTION_NAME
 */
#if !KBASE_DEBUG_SKIP_TRACE
#define KBASEP_DEBUG_PRINT_TRACE \
		"In file: " __FILE__ " line: " CSTD_STR2(__LINE__)
#if !KBASE_DEBUG_SKIP_FUNCTION_NAME
#define KBASEP_DEBUG_PRINT_FUNCTION CSTD_FUNC
#else
#define KBASEP_DEBUG_PRINT_FUNCTION ""
#endif
#else
#define KBASEP_DEBUG_PRINT_TRACE ""
#endif

/**
 * @def KBASEP_DEBUG_ASSERT_OUT(trace, function, ...)
 * @brief (Private) system printing function associated to the @see KBASE_DEBUG_ASSERT_MSG event.
 * @param trace location in the code from where the message is printed
 * @param function function from where the message is printed
 * @param ... Format string followed by format arguments.
 * @note function parameter cannot be concatenated with other strings
 */
/* Select the correct system output function*/
#ifdef CONFIG_MALI_DEBUG
#define KBASEP_DEBUG_ASSERT_OUT(trace, function, ...)\
		do { \
			pr_err("Mali<ASSERT>: %s function:%s ", trace, function);\
			pr_err(__VA_ARGS__);\
			pr_err("\n");\
		} while (MALI_FALSE)
#else
#define KBASEP_DEBUG_ASSERT_OUT(trace, function, ...) CSTD_NOP()
#endif

#ifdef CONFIG_MALI_DEBUG
#define KBASE_CALL_ASSERT_HOOK() kbasep_debug_assert_call_hook();
#else
#define KBASE_CALL_ASSERT_HOOK() CSTD_NOP();
#endif

/**
 * @def KBASE_DEBUG_ASSERT(expr)
 * @brief Calls @see KBASE_PRINT_ASSERT and prints the expression @a expr if @a expr is false
 *
 * @note This macro does nothing if the flag @see KBASE_DEBUG_DISABLE_ASSERTS is set to 1
 *
 * @param expr Boolean expression
 */
#define KBASE_DEBUG_ASSERT(expr) \
	KBASE_DEBUG_ASSERT_MSG(expr, #expr)

#if KBASE_DEBUG_DISABLE_ASSERTS
#define KBASE_DEBUG_ASSERT_MSG(expr, ...) CSTD_NOP()
#else
	/**
	 * @def KBASE_DEBUG_ASSERT_MSG(expr, ...)
	 * @brief Calls @see KBASEP_DEBUG_ASSERT_OUT and prints the given message if @a expr is false
	 *
	 * @note This macro does nothing if the flag @see KBASE_DEBUG_DISABLE_ASSERTS is set to 1
	 *
	 * @param expr Boolean expression
	 * @param ...  Message to display when @a expr is false, as a format string followed by format arguments.
	 */
#define KBASE_DEBUG_ASSERT_MSG(expr, ...) \
		do { \
			if (!(expr)) { \
				KBASEP_DEBUG_ASSERT_OUT(KBASEP_DEBUG_PRINT_TRACE, KBASEP_DEBUG_PRINT_FUNCTION, __VA_ARGS__);\
				KBASE_CALL_ASSERT_HOOK();\
				BUG();\
			} \
		} while (MALI_FALSE)
#endif				/* KBASE_DEBUG_DISABLE_ASSERTS */

/**
 * @def KBASE_DEBUG_CODE( X )
 * @brief Executes the code inside the macro only in debug mode
 *
 * @param X Code to compile only in debug mode.
 */
#ifdef CONFIG_MALI_DEBUG
#define KBASE_DEBUG_CODE(X) X
#else
#define KBASE_DEBUG_CODE(X) CSTD_NOP()
#endif				/* CONFIG_MALI_DEBUG */

/** @} */

/**
 * @brief Register a function to call on ASSERT
 *
 * Such functions will \b only be called during Debug mode, and for debugging
 * features \b only. Do not rely on them to be called in general use.
 *
 * To disable the hook, supply NULL to \a func.
 *
 * @note This function is not thread-safe, and should only be used to
 * register/deregister once in the module's lifetime.
 *
 * @param[in] func the function to call when an assert is triggered.
 * @param[in] param the parameter to pass to \a func when calling it
 */
void kbase_debug_assert_register_hook(kbase_debug_assert_hook *func, void *param);

/**
 * @brief Call a debug assert hook previously registered with kbase_debug_assert_register_hook()
 *
 * @note This function is not thread-safe with respect to multiple threads
 * registering functions and parameters with
 * kbase_debug_assert_register_hook(). Otherwise, thread safety is the
 * responsibility of the registered hook.
 */
void kbasep_debug_assert_call_hook(void);

#endif				/* _KBASE_DEBUG_H */
