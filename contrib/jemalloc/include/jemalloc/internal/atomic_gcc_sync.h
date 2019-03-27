#ifndef JEMALLOC_INTERNAL_ATOMIC_GCC_SYNC_H
#define JEMALLOC_INTERNAL_ATOMIC_GCC_SYNC_H

#define ATOMIC_INIT(...) {__VA_ARGS__}

typedef enum {
	atomic_memory_order_relaxed,
	atomic_memory_order_acquire,
	atomic_memory_order_release,
	atomic_memory_order_acq_rel,
	atomic_memory_order_seq_cst
} atomic_memory_order_t;

ATOMIC_INLINE void
atomic_fence(atomic_memory_order_t mo) {
	/* Easy cases first: no barrier, and full barrier. */
	if (mo == atomic_memory_order_relaxed) {
		asm volatile("" ::: "memory");
		return;
	}
	if (mo == atomic_memory_order_seq_cst) {
		asm volatile("" ::: "memory");
		__sync_synchronize();
		asm volatile("" ::: "memory");
		return;
	}
	asm volatile("" ::: "memory");
#  if defined(__i386__) || defined(__x86_64__)
	/* This is implicit on x86. */
#  elif defined(__ppc__)
	asm volatile("lwsync");
#  elif defined(__sparc__) && defined(__arch64__)
	if (mo == atomic_memory_order_acquire) {
		asm volatile("membar #LoadLoad | #LoadStore");
	} else if (mo == atomic_memory_order_release) {
		asm volatile("membar #LoadStore | #StoreStore");
	} else {
		asm volatile("membar #LoadLoad | #LoadStore | #StoreStore");
	}
#  else
	__sync_synchronize();
#  endif
	asm volatile("" ::: "memory");
}

/*
 * A correct implementation of seq_cst loads and stores on weakly ordered
 * architectures could do either of the following:
 *   1. store() is weak-fence -> store -> strong fence, load() is load ->
 *      strong-fence.
 *   2. store() is strong-fence -> store, load() is strong-fence -> load ->
 *      weak-fence.
 * The tricky thing is, load() and store() above can be the load or store
 * portions of a gcc __sync builtin, so we have to follow GCC's lead, which
 * means going with strategy 2.
 * On strongly ordered architectures, the natural strategy is to stick a strong
 * fence after seq_cst stores, and have naked loads.  So we want the strong
 * fences in different places on different architectures.
 * atomic_pre_sc_load_fence and atomic_post_sc_store_fence allow us to
 * accomplish this.
 */

ATOMIC_INLINE void
atomic_pre_sc_load_fence() {
#  if defined(__i386__) || defined(__x86_64__) ||			\
    (defined(__sparc__) && defined(__arch64__))
	atomic_fence(atomic_memory_order_relaxed);
#  else
	atomic_fence(atomic_memory_order_seq_cst);
#  endif
}

ATOMIC_INLINE void
atomic_post_sc_store_fence() {
#  if defined(__i386__) || defined(__x86_64__) ||			\
    (defined(__sparc__) && defined(__arch64__))
	atomic_fence(atomic_memory_order_seq_cst);
#  else
	atomic_fence(atomic_memory_order_relaxed);
#  endif

}

#define JEMALLOC_GENERATE_ATOMICS(type, short_type,			\
    /* unused */ lg_size)						\
typedef struct {							\
	type volatile repr;						\
} atomic_##short_type##_t;						\
									\
ATOMIC_INLINE type							\
atomic_load_##short_type(const atomic_##short_type##_t *a,		\
    atomic_memory_order_t mo) {						\
	if (mo == atomic_memory_order_seq_cst) {			\
		atomic_pre_sc_load_fence();				\
	}								\
	type result = a->repr;						\
	if (mo != atomic_memory_order_relaxed) {			\
		atomic_fence(atomic_memory_order_acquire);		\
	}								\
	return result;							\
}									\
									\
ATOMIC_INLINE void							\
atomic_store_##short_type(atomic_##short_type##_t *a,			\
    type val, atomic_memory_order_t mo) {				\
	if (mo != atomic_memory_order_relaxed) {			\
		atomic_fence(atomic_memory_order_release);		\
	}								\
	a->repr = val;							\
	if (mo == atomic_memory_order_seq_cst) {			\
		atomic_post_sc_store_fence();				\
	}								\
}									\
									\
ATOMIC_INLINE type							\
atomic_exchange_##short_type(atomic_##short_type##_t *a, type val,	\
    atomic_memory_order_t mo) {						\
	/*								\
	 * Because of FreeBSD, we care about gcc 4.2, which doesn't have\
	 * an atomic exchange builtin.  We fake it with a CAS loop.	\
	 */								\
	while (true) {							\
		type old = a->repr;					\
		if (__sync_bool_compare_and_swap(&a->repr, old, val)) {	\
			return old;					\
		}							\
	}								\
}									\
									\
ATOMIC_INLINE bool							\
atomic_compare_exchange_weak_##short_type(atomic_##short_type##_t *a,	\
    type *expected, type desired, atomic_memory_order_t success_mo,	\
    atomic_memory_order_t failure_mo) {					\
	type prev = __sync_val_compare_and_swap(&a->repr, *expected,	\
	    desired);							\
	if (prev == *expected) {					\
		return true;						\
	} else {							\
		*expected = prev;					\
		return false;						\
	}								\
}									\
ATOMIC_INLINE bool							\
atomic_compare_exchange_strong_##short_type(atomic_##short_type##_t *a,	\
    type *expected, type desired, atomic_memory_order_t success_mo,	\
    atomic_memory_order_t failure_mo) {					\
	type prev = __sync_val_compare_and_swap(&a->repr, *expected,	\
	    desired);							\
	if (prev == *expected) {					\
		return true;						\
	} else {							\
		*expected = prev;					\
		return false;						\
	}								\
}

#define JEMALLOC_GENERATE_INT_ATOMICS(type, short_type,			\
    /* unused */ lg_size)						\
JEMALLOC_GENERATE_ATOMICS(type, short_type, /* unused */ lg_size)	\
									\
ATOMIC_INLINE type							\
atomic_fetch_add_##short_type(atomic_##short_type##_t *a, type val,	\
    atomic_memory_order_t mo) {						\
	return __sync_fetch_and_add(&a->repr, val);			\
}									\
									\
ATOMIC_INLINE type							\
atomic_fetch_sub_##short_type(atomic_##short_type##_t *a, type val,	\
    atomic_memory_order_t mo) {						\
	return __sync_fetch_and_sub(&a->repr, val);			\
}									\
									\
ATOMIC_INLINE type							\
atomic_fetch_and_##short_type(atomic_##short_type##_t *a, type val,	\
    atomic_memory_order_t mo) {						\
	return __sync_fetch_and_and(&a->repr, val);			\
}									\
									\
ATOMIC_INLINE type							\
atomic_fetch_or_##short_type(atomic_##short_type##_t *a, type val,	\
    atomic_memory_order_t mo) {						\
	return __sync_fetch_and_or(&a->repr, val);			\
}									\
									\
ATOMIC_INLINE type							\
atomic_fetch_xor_##short_type(atomic_##short_type##_t *a, type val,	\
    atomic_memory_order_t mo) {						\
	return __sync_fetch_and_xor(&a->repr, val);			\
}

#endif /* JEMALLOC_INTERNAL_ATOMIC_GCC_SYNC_H */
