/*
 *
 * (C) COPYRIGHT 2008-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_osk_atomics.h
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_ATOMICS_H_
#define _OSK_ATOMICS_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_osk_api
 * @{
 */

/**
 * @defgroup oskatomic Atomic Access
 *
 * @anchor oskatomic_important
 * @par Important Information on Atomic variables
 *
 * Atomic variables are objects that can be modified by only one thread at a time.
 * For use in SMP systems, strongly ordered access is enforced using memory
 * barriers.
 *
 * An atomic variable implements an unsigned integer counter which is exactly
 * 32 bits long. Arithmetic on it is the same as on u32 values, which is the
 * arithmetic of integers modulo 2^32. For example, incrementing past
 * 0xFFFFFFFF rolls over to 0, decrementing past 0 rolls over to
 * 0xFFFFFFFF. That is, overflow is a well defined condition (unlike signed
 * integer arithmetic in C).
 */
/** @{ */

/** @brief Subtract a value from an atomic variable and return the new value.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a atom parameter.
 *
 * @note Please refer to @see oskatomic_important Important Information on Atomic
 * variables.
 *
 * @param atom pointer to an atomic variable
 * @param value value to subtract from \a atom
 * @return value of atomic variable after \a value has been subtracted from it.
 */
OSK_STATIC_INLINE u32 osk_atomic_sub(osk_atomic *atom, u32 value);

/** @brief Add a value to an atomic variable and return the new value.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a atom parameter.
 *
 * @note Please refer to @see oskatomic_important Important Information on Atomic
 * variables.
 *
 * @param atom pointer to an atomic variable
 * @param value value to add to \a atom
 * @return value of atomic variable after \a value has been added to it.
 */
OSK_STATIC_INLINE u32 osk_atomic_add(osk_atomic *atom, u32 value);

/** @brief Decrement an atomic variable and return its decremented value.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a atom parameter.
 *
 * @note Please refer to @see oskatomic_important Important Information on Atomic
 * variables.
 *
 * @param atom pointer to an atomic variable
 * @return decremented value of atomic variable
 */
OSK_STATIC_INLINE u32 osk_atomic_dec(osk_atomic *atom);

/** @brief Increment an atomic variable and return its incremented value.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a atom parameter.
 *
 * @note Please refer to @see oskatomic_important Important Information on Atomic
 * variables.
 *
 * @param atom pointer to an atomic variable
 * @return incremented value of atomic variable
 */
OSK_STATIC_INLINE u32 osk_atomic_inc(osk_atomic *atom);

/** @brief Sets the value of an atomic variable.
 *
 * Note: if the value of the atomic variable is set as part of a read-modify-write
 * operation and multiple threads have access to the atomic variable at that time,
 * please use osk_atomic_compare_and_swap() instead, which can ensure no other 
 * process changed the atomic variable during the read-write-modify operation.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a atom parameter.
 *
 * @note Please refer to @see oskatomic_important Important Information on Atomic
 * variables.
 *
 * @param atom pointer to an atomic variable
 * @param value the value to set
 */
OSK_STATIC_INLINE void osk_atomic_set(osk_atomic *atom, u32 value);

/** @brief Return the value of an atomic variable.
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a atom parameter.
 *
 * @note Please refer to @see oskatomic_important Important Information on Atomic
 * variables.
 *
 * @param atom pointer to an atomic variable
 * @return value of the atomic variable
 */
OSK_STATIC_INLINE u32 osk_atomic_get(osk_atomic *atom);

/** @brief Compare the value of an atomic variable, and atomically exchange it
 * if the comparison succeeds.
 *
 * This function implements the Atomic Compare-And-Swap operation (CAS) which
 * allows atomically performing a read-modify-write operation on atomic variables.
 * The CAS operation is suited for implementing synchronization primitives such
 * as semaphores and mutexes, as well as lock-free and wait-free algorithms.
 *
 * It atomically does the following: compare \a atom with \a old_value and sets \a
 * atom to \a new_value if the comparison was true.
 *
 * Regardless of the outcome of the comparison, the initial value of \a atom is
 * returned - hence the reason for this being a 'swap' operation. If the value
 * returned is equal to \a old_value, then the atomic operation succeeded. Any
 * other value shows that the atomic operation failed, and should be repeated
 * based upon the returned value.
 *
 * For example:
@code
typedef struct my_data
{
	osk_atomic index;
	object objects[10];
} data;
u32 index, old_index, new_index;

// Updates the index into an array of objects based on the current indexed object.
// If another process updated the index in the mean time, the index will not be
// updated and we try again based on the updated index. 

index = osk_atomic_get(&data.index);
do {
	old_index = index;
	new_index = calc_new_index(&data.objects[old_index]);
	index = osk_atomic_compare_and_swap(&data.index, old_index, new_index)
} while (index != old_index);

@endcode
 *
 * It is a programming error to pass an invalid pointer (including NULL)
 * through the \a atom parameter.
 *
 * @note Please refer to @see oskatomic_important Important Information on Atomic
 * variables.
 *
 * @param atom pointer to an atomic variable
 * @param old_value The value to make the comparison with \a atom
 * @param new_value The value to atomically write to atom, depending on whether
 * the comparison succeeded.
 * @return The \em initial value of \a atom, before the operation commenced.
 */
OSK_STATIC_INLINE u32 osk_atomic_compare_and_swap(osk_atomic * atom, u32 old_value, u32 new_value) CHECK_RESULT;

/** @} */ /* end group oskatomic */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

/* pull in the arch header with the implementation  */
#include <osk/mali_osk_arch_atomics.h>

#ifdef __cplusplus
}
#endif

#endif /* _OSK_ATOMICS_H_ */
