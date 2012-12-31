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
 * @file
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_ARCH_LOCKS_H_
#define _OSK_ARCH_LOCKS_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

/**
 * Private macro to safely allow asserting on a mutex/rwlock/spinlock/irq
 * spinlock pointer whilst still allowing its name to appear during
 * CONFIG_PROVE_LOCKING.
 *
 * It's safe because \a lock can safely have side-effects.
 *
 * Makes use of a GNU C extension, but this macro is only needed under Linux
 * anyway.
 *
 * NOTE: the local variable must not conflict with an identifier in a wider
 * scope
 *
 * NOTE: Due to the way this is used in this file, this definition must persist
 * outside of this file
 */
#define OSKP_LOCK_PTR_ASSERT( lock ) \
	({ \
	__typeof__( lock ) __oskp_lock__ = ( lock ); \
	OSK_ASSERT( NULL != __oskp_lock__ ); \
	__oskp_lock__; })

/*
 * A definition must be provided of each lock init function to eliminate
 * warnings. They'lll be hidden by subsequent macro definitions
 */

OSK_STATIC_INLINE osk_error osk_rwlock_init(osk_rwlock * const lock, osk_lock_order order)
{
	OSK_ASSERT_MSG( MALI_FALSE,
		"FATAL: this definition of osk_rwlock_init() should've been uncallable - a macro redefines it\n" );
	CSTD_UNUSED( lock );
	CSTD_UNUSED( order );
	return OSK_ERR_FAIL;
}

OSK_STATIC_INLINE osk_error osk_mutex_init(osk_mutex * const lock, osk_lock_order order)
{
	OSK_ASSERT_MSG( MALI_FALSE,
		"FATAL: this definition of osk_mutex_init() should've been uncallable - a macro redefines it\n" );
	CSTD_UNUSED( lock );
	CSTD_UNUSED( order );
	return OSK_ERR_FAIL;
}

OSK_STATIC_INLINE osk_error osk_spinlock_init(osk_spinlock * const lock, osk_lock_order order)
{
	OSK_ASSERT_MSG( MALI_FALSE,
		"FATAL: this definition of osk_spinlock_init() should've been uncallable - a macro redefines it\n" );
	CSTD_UNUSED( lock );
	CSTD_UNUSED( order );
	return OSK_ERR_FAIL;
}

OSK_STATIC_INLINE osk_error osk_spinlock_irq_init(osk_spinlock_irq * const lock, osk_lock_order order)
{
	OSK_ASSERT_MSG( MALI_FALSE,
		"FATAL: this definition of osk_spinlock_irq_init() should've been uncallable - a macro redefines it\n" );
	CSTD_UNUSED( lock );
	CSTD_UNUSED( order );
	return OSK_ERR_FAIL;
}

/*
 * End of 'dummy' definitions
 */


/* Note: This uses a GNU C Extension to allow Linux's CONFIG_PROVE_LOCKING to work correctly
 *
 * This is not required outside of Linux
 *
 * NOTE: the local variable must not conflict with an identifier in a wider scope */
#define osk_rwlock_init( ARG_LOCK, ARG_ORDER ) \
	({ \
	osk_lock_order __oskp_order__ = (ARG_ORDER); \
	OSK_ASSERT( OSK_LOCK_ORDER_LAST <= __oskp_order__ && __oskp_order__ <= OSK_LOCK_ORDER_FIRST ); \
	init_rwsem( OSKP_LOCK_PTR_ASSERT((ARG_LOCK)) ); \
	OSK_ERR_NONE;})

OSK_STATIC_INLINE void osk_rwlock_term(osk_rwlock * lock)
{
	OSK_ASSERT(NULL != lock);
	/* nop */
}

OSK_STATIC_INLINE void osk_rwlock_read_lock(osk_rwlock * lock)
{
	OSK_ASSERT(NULL != lock);
	down_read(lock);
}

OSK_STATIC_INLINE void osk_rwlock_read_unlock(osk_rwlock * lock)
{
	OSK_ASSERT(NULL != lock);
	up_read(lock);
}

OSK_STATIC_INLINE void osk_rwlock_write_lock(osk_rwlock * lock)
{
	OSK_ASSERT(NULL != lock);
	down_write(lock);
}

OSK_STATIC_INLINE void osk_rwlock_write_unlock(osk_rwlock * lock)
{
	OSK_ASSERT(NULL != lock);
	up_write(lock);
}

/* Note: This uses a GNU C Extension to allow Linux's CONFIG_PROVE_LOCKING to work correctly
 *
 * This is not required outside of Linux
 *
 * NOTE: the local variable must not conflict with an identifier in a wider scope */
#define osk_mutex_init( ARG_LOCK, ARG_ORDER ) \
	({ \
	osk_lock_order __oskp_order__ = (ARG_ORDER); \
	OSK_ASSERT( OSK_LOCK_ORDER_LAST <= __oskp_order__ && __oskp_order__ <= OSK_LOCK_ORDER_FIRST ); \
	mutex_init( OSKP_LOCK_PTR_ASSERT((ARG_LOCK)) ); \
	OSK_ERR_NONE;})


OSK_STATIC_INLINE void osk_mutex_term(osk_mutex * lock)
{
	OSK_ASSERT(NULL != lock);
	return; /* nop */
}

OSK_STATIC_INLINE void osk_mutex_lock(osk_mutex * lock)
{
	OSK_ASSERT(NULL != lock);
	mutex_lock(lock);
}

OSK_STATIC_INLINE void osk_mutex_unlock(osk_mutex * lock)
{
	OSK_ASSERT(NULL != lock);
	mutex_unlock(lock);
}

/* Note: This uses a GNU C Extension to allow Linux's CONFIG_PROVE_LOCKING to work correctly
 *
 * This is not required outside of Linux
 *
 * NOTE: the local variable must not conflict with an identifier in a wider scope */
#define osk_spinlock_init( ARG_LOCK, ARG_ORDER ) \
	({ \
	osk_lock_order __oskp_order__ = (ARG_ORDER); \
	OSK_ASSERT( OSK_LOCK_ORDER_LAST <= __oskp_order__ && __oskp_order__ <= OSK_LOCK_ORDER_FIRST ); \
	spin_lock_init( OSKP_LOCK_PTR_ASSERT((ARG_LOCK)) ); \
	OSK_ERR_NONE;})

OSK_STATIC_INLINE void osk_spinlock_term(osk_spinlock * lock)
{
	OSK_ASSERT(NULL != lock);
	/* nop */
}

OSK_STATIC_INLINE void osk_spinlock_lock(osk_spinlock * lock)
{
	OSK_ASSERT(NULL != lock);
	spin_lock(lock);
}

OSK_STATIC_INLINE void osk_spinlock_unlock(osk_spinlock * lock)
{
	OSK_ASSERT(NULL != lock);
	spin_unlock(lock);
}

/* Note: This uses a GNU C Extension to allow Linux's CONFIG_PROVE_LOCKING to work correctly
 *
 * This is not required outside of Linux
 *
 * NOTE: the local variable must not conflict with an identifier in a wider scope */
#define osk_spinlock_irq_init( ARG_LOCK, ARG_ORDER ) \
	({ \
	osk_lock_order __oskp_order__ = (ARG_ORDER); \
	OSK_ASSERT( OSK_LOCK_ORDER_LAST <= __oskp_order__ && __oskp_order__ <= OSK_LOCK_ORDER_FIRST ); \
	spin_lock_init( &(OSKP_LOCK_PTR_ASSERT((ARG_LOCK))->lock) ); \
	OSK_ERR_NONE;})

OSK_STATIC_INLINE void osk_spinlock_irq_term(osk_spinlock_irq * lock)
{
	OSK_ASSERT(NULL != lock);
}

OSK_STATIC_INLINE void osk_spinlock_irq_lock(osk_spinlock_irq * lock)
{
	OSK_ASSERT(NULL != lock);
	spin_lock_irqsave(&lock->lock, lock->flags);
}

OSK_STATIC_INLINE void osk_spinlock_irq_unlock(osk_spinlock_irq * lock)
{
	OSK_ASSERT(NULL != lock);
	spin_unlock_irqrestore(&lock->lock, lock->flags);
}

#endif /* _OSK_ARCH_LOCKS_H_ */
