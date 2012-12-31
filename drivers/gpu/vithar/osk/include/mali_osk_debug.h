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



#ifndef _OSK_DEBUG_H_
#define _OSK_DEBUG_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdarg.h>
#include <malisw/mali_malisw.h>

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_osk_api
 * @{
 */

/**
 * @addtogroup oskdebug Debug
 *  
 * OSK debug macros for asserts and debug messages. Mimics CDBG functionality.
 *
 * @{
 */

/**
 * @brief OSK module IDs
 */
typedef enum
{
	OSK_UNKNOWN = 0, /**< @brief Unknown module */
	OSK_OSK,         /**< @brief ID of OSK module */
	OSK_UKK,         /**< @brief ID of UKK module */
	OSK_BASE_MMU,    /**< @brief ID of Base MMU */
	OSK_BASE_JD,     /**< @brief ID of Base Job Dispatch */
	OSK_BASE_JM,     /**< @brief ID of Base Job Manager */
	OSK_BASE_CORE,   /**< @brief ID of Base Core */
	OSK_BASE_MEM,    /**< @brief ID of Base Memory */
	OSK_BASE_EVENT,  /**< @brief ID of Base Event */
	OSK_BASE_CTX,    /**< @brief ID of Base Context */
	OSK_BASE_PM,     /**< @brief ID of Base Power Management */
	OSK_UMP,         /**< @brief ID of UMP module */
	OSK_MODULES_ALL  /**< @brief Select all the modules at once / Also gives the number of modules in the enum */
} osk_module;

/**
 * Debug messages are sent to a particular channel (info, warn or error) or to all channels
 */
#define OSK_CHANNEL_INFO      OSKP_CHANNEL_INFO      /**< @brief No output*/
#define OSK_CHANNEL_WARN      OSKP_CHANNEL_WARN      /**< @brief Standard output*/
#define OSK_CHANNEL_ERROR     OSKP_CHANNEL_ERROR     /**< @brief Error output*/
#define OSK_CHANNEL_RAW       OSKP_CHANNEL_RAW       /**< @brief Raw output*/
#define OSK_CHANNEL_ALL       OSKP_CHANNEL_ALL       /**< @brief All the channels at the same time*/

/** Function type that is called on an OSK_ASSERT() or OSK_ASSERT_MSG() */
typedef void (osk_debug_assert_hook)( void * );

typedef struct oskp_debug_assert_cb
{
	osk_debug_assert_hook *func;
	void *param;
} oskp_debug_assert_cb;

/**
 * @def OSK_DISABLE_ASSERTS
 *
 * @brief Indicates whether asserts are in use and evaluate their
 * expressions. 0 indicates they are, any other value indicates that they are
 * not.
 */

/**
 * @def OSK_ASSERT_MSG(expr, ...)
 * @brief Prints the given message if @a expr is false
 *
 * @note This macro does nothing if the flag @see OSK_DISABLE_ASSERTS is set to 1
 *
 * @param expr Boolean expression
 * @param ...  Message to display when @a expr is false, as a format string followed by format arguments.
 * The format string and format arguments needs to be enclosed by parentheses.
 * See oskp_validate_format_string for a list of supported format specifiers.
 */
#define OSK_ASSERT_MSG(expr, ...) OSKP_ASSERT_MSG(expr, __VA_ARGS__)

/**
 * @def OSK_ASSERT(expr)
 * @brief Prints the expression @a expr if @a expr is false
 *
 * @note This macro does nothing if the flag @see OSK_DISABLE_ASSERTS is set to 1
 *
 * @param expr Boolean expression
 */
#define OSK_ASSERT(expr) OSKP_ASSERT(expr)

/**
 * @def OSK_INTERNAL_ASSERT(expr)
 * @brief Asserts if @a expr is false. 
 * This assert function is for internal use of OSK functions which themselves are used to implement
 * the OSK_ASSERT functionality. These functions should use OSK_INTERNAL_ASSERT which does not use
 * any OSK functions to prevent ending up in a recursive loop. 
 *
 * @note This macro does nothing if the flag @see OSK_DISABLE_ASSERTS is set to 1
 *
 * @param expr Boolean expression
 */
#define OSK_INTERNAL_ASSERT(expr) OSKP_INTERNAL_ASSERT(expr)

/**
 * @def OSK_DEBUG_CODE( X )
 * @brief Executes the code inside the macro only in debug mode
 *
 * @param X Code to compile only in debug mode.
 */
#define OSK_DEBUG_CODE( X ) OSKP_DEBUG_CODE( X )

/**
 * @def   OSK_PRINT(module, ...)
 * @brief Prints given message
 *
 * Example:
 * @code OSK_PRINT(OSK_BASE_MEM, " %d blocks could not be allocated", mem_allocated); @endcode will print:
 * \n
 * "10 blocks could not be allocated\n"
 *
 * @param module   Name of the module which prints the message.
 * @param ...      Format string followed by a varying number of parameters
 *                 See oskp_validate_format_string for a list of supported format specifiers.
 */
#define OSK_PRINT(module, ...) OSKP_PRINT_RAW(module, __VA_ARGS__)

/**
 * @def   OSKP_PRINT_INFO(module, ...)
 * @brief Prints "MALI<INFO,module_name>: " followed by the given message.
 *
 * Example:
 * @code OSK_PRINT_INFO(OSK_BASE_MEM, " %d blocks could not be allocated", mem_allocated); @endcode will print:
 * \n
 * "MALI<INFO,BASE_MEM>: 10 blocks could not be allocated"\n
 *
 * @note Only gets compiled in for debug builds
 *
 * @param module   Name of the module which prints the message.
 * @param ...      Format string followed by a varying number of parameters
 *                 See oskp_validate_format_string for a list of supported format specifiers.
 */
#define OSK_PRINT_INFO(module, ...) OSKP_PRINT_INFO(module, __VA_ARGS__)

/**
 * @def   OSK_PRINT_WARN(module, ...)
 * @brief Prints "MALI<WARN,module_name>: " followed by the given message.
 *
 * Example:
 * @code OSK_PRINT_WARN(OSK_BASE_MEM, " %d blocks could not be allocated", mem_allocated); @endcode will print:
 * \n
 * "MALI<WARN,BASE_MEM>: 10 blocks could not be allocated"\n
 *
 * @note Only gets compiled in for debug builds
 *
 * @param module   Name of the module which prints the message.
 * @param ...      Format string followed by a varying number of parameters
 *                 See oskp_validate_format_string for a list of supported format specifiers.
 */
#define OSK_PRINT_WARN(module, ...) OSKP_PRINT_WARN(module, __VA_ARGS__)

/**
 * @def   OSK_PRINT_ERROR(module, ...)
 * @brief Prints "MALI<ERROR,module_name>: " followed by the given message.
 *
 * Example:
 * @code OSK_PRINT_ERROR(OSK_BASE_MEM, " %d blocks could not be allocated", mem_allocated); @endcode will print:
 * \n
 * "MALI<ERROR,BASE_MEM>: 10 blocks could not be allocated"\n
 *
 * @param module   Name of the module which prints the message.
 * @param ...      Format string followed by a varying number of parameters
 *                 See oskp_validate_format_string for a list of supported format specifiers.
 */
#define OSK_PRINT_ERROR(module, ...) OSKP_PRINT_ERROR(module, __VA_ARGS__)

/**
 * @def OSK_PRINT_ALLOW(module, channel)
 * @brief Allow the given module to print on the given channel
 * @note If @see OSK_USE_RUNTIME_CONFIG is disabled then this macro doesn't do anything
 * @note Only gets compiled in for debug builds
 * @param module is a @see osk_module
 * @param channel is one of @see OSK_CHANNEL_INFO, @see OSK_CHANNEL_WARN, @see OSK_CHANNEL_ERROR,
 * @see OSK_CHANNEL_ALL
 * @return MALI_TRUE if the module is allowed to print on the channel.
 */
#define OSK_PRINT_ALLOW(module, channel) OSKP_PRINT_ALLOW(module, channel)

/**
 * @def OSK_PRINT_BLOCK(module, channel)
 * @brief Prevent the given module from printing on the given channel
 * @note If @see OSK_USE_RUNTIME_CONFIG is disabled then this macro doesn't do anything
 * @note Only gets compiled in for debug builds
 * @param module is a @see osk_module
 * @param channel is one of @see OSK_CHANNEL_INFO, @see OSK_CHANNEL_WARN, @see OSK_CHANNEL_ERROR,
 * @see OSK_CHANNEL_ALL
 * @return MALI_TRUE if the module is allowed to print on the channel.
 */
#define OSK_PRINT_BLOCK(module, channel) OSKP_PRINT_BLOCK(module, channel)

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
void osk_debug_assert_register_hook( osk_debug_assert_hook *func, void *param );

/**
 * @brief Call a debug assert hook previously registered with osk_debug_assert_register_hook()
 *
 * @note This function is not thread-safe with respect to multiple threads
 * registering functions and parameters with
 * osk_debug_assert_register_hook(). Otherwise, thread safety is the
 * responsibility of the registered hook.
 */
void oskp_debug_assert_call_hook( void );

/**
 * @brief Convert a module id into a module name.
 *
 * @param module ID of the module to convert
 * @note module names are stored in : @see oskp_str_modules.
 * @return the name of the given module ID as a string of characters.
 */
const char* oskp_module_to_str(const osk_module module);

/**
 * @brief Validate the format string
 *
 * Validates the printf style format string against the formats
 * that are supported by the OSK_PRINT macros. If an invalid
 * format is used, a warning message is printed identifying
 * the unsupported format specifier.
 *
 * Supported length and specifiers in the format string are:
 *
 * "d", "ld", "lld",
 * "x", "lx", "llx",
 * "X", "lX", "llX",
 * "p",
 * "c",
 * "s"
 *
 * Notes:
 * - in release builds this function does nothing.
 * - this function takes a variable number of arguments to
 *   ease using it with variadic macros. Only the format
 *   argument is used though.
 *
 * @param format format string
 *
 */
void oskp_validate_format_string(const char *format, ...);

/**
 * @brief printf-style string formatting.
 *
 * Refer to the cutils specification for restrictions on the format string.
 *
 * @param str    output buffer
 * @param size   size of the output buffer in bytes (incl. eos)
 * @param format the format string
 *               See oskp_validate_format_string for a list of supported
 *               format specifiers.
 * @param [in] ...    The variadic arguments
 *
 * @return The number of characters written on success, or a negative value
 * on failure.
 */
s32 osk_snprintf(char *str,  size_t size, const char *format, ...);

/**
 * @brief Get thread information for the current thread
 *
 * The information is for debug purposes only. For example, the current CPU for
 * the thread could've changed by the time you access the returned information.
 *
 * On systems that support 64-bit thread IDs, the thread ID will be
 * truncated. Therefore, this only gives an appoximate guide as to which thread
 * is making the call.
 *
 * @param[out] thread_id first 32-bits of the current thread's ID
 * @param[out] cpu_nr the CPU that the thread was probably executing on at the
 *             time of the call.
 */
OSK_STATIC_INLINE void osk_debug_get_thread_info( u32 *thread_id, u32 *cpu_nr );

/* @} */ /* end group oskdebug */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

#include <osk/mali_osk_arch_debug.h>

#ifdef __cplusplus
}
#endif

#endif /* _OSK_DEBUG_H_ */

