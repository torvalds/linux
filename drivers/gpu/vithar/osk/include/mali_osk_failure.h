/*
 *
 * (C) COPYRIGHT 2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#ifndef _OSK_FAILURE_H_
#define _OSK_FAILURE_H_
/** @file mali_osk_failure.h
 *
 * Base kernel side failure simulation mechanism interface.
 *
 * Provides a mechanism to simulate failure of
 * functions which use the OSK_SIMULATE_FAILURE macro. This is intended to
 * exercise error-handling paths during testing.
 */
#include <malisw/mali_malisw.h>
#include "osk/include/mali_osk_debug.h"

/**
 * @addtogroup osk
 * @{
 */

/**
 * @addtogroup osk_failure Simulated failure
 * @{
 */

/**
 * @addtogroup osk_failure_public Public
 * @{
 */
/**
 * @brief Decide whether or not to simulate a failure in a given module
 *
 * Functions that can return a failure indication should use this macro to
 * decide whether to do so in cases where no genuine failure occurred. This
 * allows testing of error-handling paths in callers of those functions. A
 * module ID must be specified to ensure that failures are only simulated in
 * those modules for which they have been enabled.
 *
 * If it evaluates as MALI_TRUE, a message may be printed giving the location
 * of the macro usage: the actual behavior is defined by @ref OSK_ON_FAIL.
 *
 * A break point set on the oskp_failure function will halt execution
 * before this macro evaluates as MALI_TRUE.
 *
 * @param[in] module Numeric ID of the module using the macro
 *
 * @return MALI_FALSE if execution should continue as normal; otherwise
 *         a failure should be simulated by the code using this macro.
 *
 * @note Unless simulation of failures was enabled at compilation time, this
 *       macro always evaluates as MALI_FALSE.
 */
#if OSK_SIMULATE_FAILURES
#define OSK_SIMULATE_FAILURE( module ) \
            ( OSKP_SIMULATE_FAILURE_IS_ENABLED( (module), OSK_CHANNEL_INFO ) && \
             oskp_is_failure_on() &&\
              oskp_simulate_failure( module, OSKP_PRINT_TRACE, OSKP_PRINT_FUNCTION ) )
#else
#define OSK_SIMULATE_FAILURE( module ) \
            ( CSTD_NOP( module ), MALI_FALSE )
#endif


/**
 * @brief Get the number of potential failures
 *
 * This function can be used to find out the total number of potential
 * failures during a test, before using @ref osk_set_failure_range to
 * set the number of successes to allow. This allows testing of error-
 * handling paths to be parallelized (in different processes) by sub-
 * dividing the range of successes to allow before provoking a failure.
 *
 * @return The number of times the @ref OSK_SIMULATE_FAILURE macro has been
 *         evaluated since the counter was last reset.
 */
u64 osk_get_potential_failures( void );

/**
 * @brief Set the range of failures to simulate
 *
 * This function configures a range of potential failures to be tested by
 * simulating actual failure. The @ref OSK_SIMULATE_FAILURE macro will
 * evaluate as MALI_FALSE for the first @p start evaluations after the range
 * is set; then as MALI_TRUE for the next @p end - @p start evaluations;
 * finally, as MALI_FALSE after being evaluated @p end times (until the
 * mechanism is reset). @p end must be greater than or equal to @p start.
 *
 * This function also resets the count of successes allowed so far.
 *
 * @param[in] start Number of potential failures to count before simulating
 *                  the first failure, or U64_MAX to never fail.
 * @param[in] end   Number of potential failures to count before allowing
 *                  resumption of success, or U64_MAX to fail all after
 *                  @p first.
 */
void osk_set_failure_range( u64 start, u64 end );

/**
 * @brief Find out whether a failure was simulated
 *
 * This function can be used to find out whether an apparent failure was
 * genuine or simulated by @ref OSK_SIMULATE_FAILURE macro.
 *
 * @return MALI_FALSE unless a failure was simulated since the last call to
 *         the @ref osk_set_failure_range function.
 * @since 2.3
 */
mali_bool osk_failure_simulated( void );

/** @} */
/* end public*/

/**
 * @addtogroup osk_failure_private Private
 * @{
 */

/**
 * @brief Decide whether or not to simulate a failure
 *
 * @param[in] module   Numeric ID of the module that can fail
 * @param[in] trace    Pointer to string giving the location in the source code
 * @param[in] function Pointer to name of the calling function
 *
 * @return MALI_FALSE if execution should continue as normal; otherwise
 *         a failure should be simulated by the calling code.
 */

mali_bool oskp_simulate_failure( osk_module  module,
                                  const char  *trace,
                                  const char  *function );
mali_bool oskp_is_failure_on(void);
void oskp_failure_init( void );
void oskp_failure_term( void );
/** @} */
/* end osk_failure_private group*/

/** @} */
/* end osk_failure group*/

/** @} */
/* end osk group*/




#endif /* _OSK_FAILURE_H_ */

