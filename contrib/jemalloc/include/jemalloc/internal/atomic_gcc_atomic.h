#ifndef JEMALLOC_INTERNAL_ATOMIC_GCC_ATOMIC_H
#define JEMALLOC_INTERNAL_ATOMIC_GCC_ATOMIC_H

#include "jemalloc/internal/assert.h"

#define ATOMIC_INIT(...) {__VA_ARGS__}

typedef enum {
	atomic_memory_order_relaxed,
	atomic_memory_order_acquire,
	atomic_memory_order_release,
	atomic_memory_order_acq_rel,
	atomic_memory_order_seq_cst
} atomic_memory_order_t;

ATOMIC_INLINE int
atomic_enum_to_builtin(atomic_memory_order_t mo) {
	switch (mo) {
	case atomic_memory_order_relaxed:
		return __ATOMIC_RELAXED;
	case atomic_memory_order_acquire:
		return __ATOMIC_ACQUIRE;
	case atomic_memory_order_release:
		return __ATOMIC_RELEASE;
	case atomic_memory_order_acq_rel:
		return __ATOMIC_ACQ_REL;
	case atomic_memory_order_seq_cst:
		return __ATOMIC_SEQ_CST;
	}
	/* Can't happen; the switch is exhaustive. */
	not_reached();
}

ATOMIC_INLINE void
atomic_fence(atomic_memory_order_t mo) {
	__atomic_thread_fence(atomic_enum_to_builtin(mo));
}

#define JEMALLOC_GENERATE_ATOMICS(type, short_type,			\
    /* unused */ lg_size)						\
typedef struct {							\
	type repr;							\
} atomic_##short_type##_t;						\
									\
ATOMIC_INLINE type							\
atomic_load_##short_type(const atomic_##short_type##_t *a,		\
    atomic_memory_order_t mo) {						\
	type result;							\
	__atomic_load(&a->repr, &result, atomic_enum_to_builtin(mo));	\
	return result;							\
}									\
									\
ATOMIC_INLINE void							\
atomic_store_##short_type(atomic_##short_type##_t *a, type val,		\
    atomic_memory_order_t mo) {						\
	__atomic_store(&a->repr, &val, atomic_enum_to_builtin(mo));	\
}									\
									\
ATOMIC_INLINE type							\
atomic_exchange_##short_type(atomic_##short_type##_t *a, type val,	\
    atomic_memory_order_t mo) {						\
	type result;							\
	__atomic_exchange(&a->repr, &val, &result,			\
	    atomic_enum_to_builtin(mo));				\
	return result;							\
}									\
									\
ATOMIC_INLINE bool							\
atomic_compare_exchange_weak_##short_type(atomic_##short_type##_t *a,	\
    type *expected, type desired, atomic_memory_order_t success_mo,	\
    atomic_memory_order_t failure_mo) {					\
	return __atomic_compare_exchange(&a->repr, expected, &desired,	\
	    true, atomic_enum_to_builtin(success_mo),			\
	    atomic_enum_to_builtin(failure_mo));			\
}									\
									\
ATOMIC_INLINE bool							\
atomic_compare_exchange_strong_##short_type(atomic_##short_type##_t *a,	\
    type *expected, type desired, atomic_memory_order_t success_mo,	\
    atomic_memory_order_t failure_mo) {					\
	return __atomic_compare_exchange(&a->repr, expected, &desired,	\
	    false,							\
	    atomic_enum_to_builtin(success_mo),				\
	    atomic_enum_to_builtin(failure_mo));			\
}


#define JEMALLOC_GENERATE_INT_ATOMICS(type, short_type,			\
    /* unused */ lg_size)						\
JEMALLOC_GENERATE_ATOMICS(type, short_type, /* unused */ lg_size)	\
									\
ATOMIC_INLINE type							\
atomic_fetch_add_##short_type(atomic_##short_type##_t *a, type val,	\
    atomic_memory_order_t mo) {						\
	return __atomic_fetch_add(&a->repr, val,			\
	    atomic_enum_to_builtin(mo));				\
}									\
									\
ATOMIC_INLINE type							\
atomic_fetch_sub_##short_type(atomic_##short_type##_t *a, type val,	\
    atomic_memory_order_t mo) {						\
	return __atomic_fetch_sub(&a->repr, val,			\
	    atomic_enum_to_builtin(mo));				\
}									\
									\
ATOMIC_INLINE type							\
atomic_fetch_and_##short_type(atomic_##short_type##_t *a, type val,	\
    atomic_memory_order_t mo) {						\
	return __atomic_fetch_and(&a->repr, val,			\
	    atomic_enum_to_builtin(mo));				\
}									\
									\
ATOMIC_INLINE type							\
atomic_fetch_or_##short_type(atomic_##short_type##_t *a, type val,	\
    atomic_memory_order_t mo) {						\
	return __atomic_fetch_or(&a->repr, val,				\
	    atomic_enum_to_builtin(mo));				\
}									\
									\
ATOMIC_INLINE type							\
atomic_fetch_xor_##short_type(atomic_##short_type##_t *a, type val,	\
    atomic_memory_order_t mo) {						\
	return __atomic_fetch_xor(&a->repr, val,			\
	    atomic_enum_to_builtin(mo));				\
}

#endif /* JEMALLOC_INTERNAL_ATOMIC_GCC_ATOMIC_H */
