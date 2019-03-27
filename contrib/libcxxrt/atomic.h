
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif
#ifndef __has_feature
#define __has_feature(x) 0
#endif
/**
 * Swap macro that enforces a happens-before relationship with a corresponding
 * ATOMIC_LOAD.
 */
#if __has_builtin(__c11_atomic_exchange)
#define ATOMIC_SWAP(addr, val)\
	__c11_atomic_exchange(reinterpret_cast<_Atomic(__typeof__(val))*>(addr), val, __ATOMIC_ACQ_REL)
#elif __has_builtin(__sync_swap)
#define ATOMIC_SWAP(addr, val)\
	__sync_swap(addr, val)
#else
#define ATOMIC_SWAP(addr, val)\
	__sync_lock_test_and_set(addr, val)
#endif

#if __has_builtin(__c11_atomic_load)
#define ATOMIC_LOAD(addr)\
	__c11_atomic_load(reinterpret_cast<_Atomic(__typeof__(*addr))*>(addr), __ATOMIC_ACQUIRE)
#else
#define ATOMIC_LOAD(addr)\
	(__sync_synchronize(), *addr)
#endif

