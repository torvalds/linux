#ifndef _ASM_POWERPC_RWSEM_H
#define _ASM_POWERPC_RWSEM_H

#ifndef _LINUX_RWSEM_H
#error "Please don't include <asm/rwsem.h> directly, use <linux/rwsem.h> instead."
#endif

#ifdef __KERNEL__

/*
 * R/W semaphores for PPC using the stuff in lib/rwsem.c.
 * Adapted largely from include/asm-i386/rwsem.h
 * by Paul Mackerras <paulus@samba.org>.
 */

#include <linux/list.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <asm/system.h>

/*
 * the semaphore definition
 */
struct rw_semaphore {
	/* XXX this should be able to be an atomic_t  -- paulus */
	signed int		count;
#define RWSEM_UNLOCKED_VALUE		0x00000000
#define RWSEM_ACTIVE_BIAS		0x00000001
#define RWSEM_ACTIVE_MASK		0x0000ffff
#define RWSEM_WAITING_BIAS		(-0x00010000)
#define RWSEM_ACTIVE_READ_BIAS		RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS		(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)
	spinlock_t		wait_lock;
	struct list_head	wait_list;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
};

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define __RWSEM_DEP_MAP_INIT(lockname) , .dep_map = { .name = #lockname }
#else
# define __RWSEM_DEP_MAP_INIT(lockname)
#endif

#define __RWSEM_INITIALIZER(name) \
	{ RWSEM_UNLOCKED_VALUE, __SPIN_LOCK_UNLOCKED((name).wait_lock), \
	  LIST_HEAD_INIT((name).wait_list) __RWSEM_DEP_MAP_INIT(name) }

#define DECLARE_RWSEM(name)		\
	struct rw_semaphore name = __RWSEM_INITIALIZER(name)

extern struct rw_semaphore *rwsem_down_read_failed(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_down_write_failed(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_wake(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_downgrade_wake(struct rw_semaphore *sem);

extern void __init_rwsem(struct rw_semaphore *sem, const char *name,
			 struct lock_class_key *key);

#define init_rwsem(sem)					\
	do {						\
		static struct lock_class_key __key;	\
							\
		__init_rwsem((sem), #sem, &__key);	\
	} while (0)

/*
 * lock for reading
 */
static inline void __down_read(struct rw_semaphore *sem)
{
	if (unlikely(atomic_inc_return((atomic_t *)(&sem->count)) <= 0))
		rwsem_down_read_failed(sem);
}

static inline int __down_read_trylock(struct rw_semaphore *sem)
{
	int tmp;

	while ((tmp = sem->count) >= 0) {
		if (tmp == cmpxchg(&sem->count, tmp,
				   tmp + RWSEM_ACTIVE_READ_BIAS)) {
			return 1;
		}
	}
	return 0;
}

/*
 * lock for writing
 */
static inline void __down_write_nested(struct rw_semaphore *sem, int subclass)
{
	int tmp;

	tmp = atomic_add_return(RWSEM_ACTIVE_WRITE_BIAS,
				(atomic_t *)(&sem->count));
	if (unlikely(tmp != RWSEM_ACTIVE_WRITE_BIAS))
		rwsem_down_write_failed(sem);
}

static inline void __down_write(struct rw_semaphore *sem)
{
	__down_write_nested(sem, 0);
}

static inline int __down_write_trylock(struct rw_semaphore *sem)
{
	int tmp;

	tmp = cmpxchg(&sem->count, RWSEM_UNLOCKED_VALUE,
		      RWSEM_ACTIVE_WRITE_BIAS);
	return tmp == RWSEM_UNLOCKED_VALUE;
}

/*
 * unlock after reading
 */
static inline void __up_read(struct rw_semaphore *sem)
{
	int tmp;

	tmp = atomic_dec_return((atomic_t *)(&sem->count));
	if (unlikely(tmp < -1 && (tmp & RWSEM_ACTIVE_MASK) == 0))
		rwsem_wake(sem);
}

/*
 * unlock after writing
 */
static inline void __up_write(struct rw_semaphore *sem)
{
	if (unlikely(atomic_sub_return(RWSEM_ACTIVE_WRITE_BIAS,
			      (atomic_t *)(&sem->count)) < 0))
		rwsem_wake(sem);
}

/*
 * implement atomic add functionality
 */
static inline void rwsem_atomic_add(int delta, struct rw_semaphore *sem)
{
	atomic_add(delta, (atomic_t *)(&sem->count));
}

/*
 * downgrade write lock to read lock
 */
static inline void __downgrade_write(struct rw_semaphore *sem)
{
	int tmp;

	tmp = atomic_add_return(-RWSEM_WAITING_BIAS, (atomic_t *)(&sem->count));
	if (tmp < 0)
		rwsem_downgrade_wake(sem);
}

/*
 * implement exchange and add functionality
 */
static inline int rwsem_atomic_update(int delta, struct rw_semaphore *sem)
{
	return atomic_add_return(delta, (atomic_t *)(&sem->count));
}

static inline int rwsem_is_locked(struct rw_semaphore *sem)
{
	return (sem->count != 0);
}

#endif	/* __KERNEL__ */
#endif	/* _ASM_POWERPC_RWSEM_H */
