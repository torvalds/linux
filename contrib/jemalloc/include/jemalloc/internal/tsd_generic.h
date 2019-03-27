#ifdef JEMALLOC_INTERNAL_TSD_GENERIC_H
#error This file should be included only once, by tsd.h.
#endif
#define JEMALLOC_INTERNAL_TSD_GENERIC_H

typedef struct tsd_init_block_s tsd_init_block_t;
struct tsd_init_block_s {
	ql_elm(tsd_init_block_t) link;
	pthread_t thread;
	void *data;
};

/* Defined in tsd.c, to allow the mutex headers to have tsd dependencies. */
typedef struct tsd_init_head_s tsd_init_head_t;

typedef struct {
	bool initialized;
	tsd_t val;
} tsd_wrapper_t;

void *tsd_init_check_recursion(tsd_init_head_t *head,
    tsd_init_block_t *block);
void tsd_init_finish(tsd_init_head_t *head, tsd_init_block_t *block);

extern pthread_key_t tsd_tsd;
extern tsd_init_head_t tsd_init_head;
extern tsd_wrapper_t tsd_boot_wrapper;
extern bool tsd_booted;

/* Initialization/cleanup. */
JEMALLOC_ALWAYS_INLINE void
tsd_cleanup_wrapper(void *arg) {
	tsd_wrapper_t *wrapper = (tsd_wrapper_t *)arg;

	if (wrapper->initialized) {
		wrapper->initialized = false;
		tsd_cleanup(&wrapper->val);
		if (wrapper->initialized) {
			/* Trigger another cleanup round. */
			if (pthread_setspecific(tsd_tsd, (void *)wrapper) != 0)
			{
				malloc_write("<jemalloc>: Error setting TSD\n");
				if (opt_abort) {
					abort();
				}
			}
			return;
		}
	}
	malloc_tsd_dalloc(wrapper);
}

JEMALLOC_ALWAYS_INLINE void
tsd_wrapper_set(tsd_wrapper_t *wrapper) {
	if (pthread_setspecific(tsd_tsd, (void *)wrapper) != 0) {
		malloc_write("<jemalloc>: Error setting TSD\n");
		abort();
	}
}

JEMALLOC_ALWAYS_INLINE tsd_wrapper_t *
tsd_wrapper_get(bool init) {
	tsd_wrapper_t *wrapper = (tsd_wrapper_t *)pthread_getspecific(tsd_tsd);

	if (init && unlikely(wrapper == NULL)) {
		tsd_init_block_t block;
		wrapper = (tsd_wrapper_t *)
		    tsd_init_check_recursion(&tsd_init_head, &block);
		if (wrapper) {
			return wrapper;
		}
		wrapper = (tsd_wrapper_t *)
		    malloc_tsd_malloc(sizeof(tsd_wrapper_t));
		block.data = (void *)wrapper;
		if (wrapper == NULL) {
			malloc_write("<jemalloc>: Error allocating TSD\n");
			abort();
		} else {
			wrapper->initialized = false;
			tsd_t initializer = TSD_INITIALIZER;
			wrapper->val = initializer;
		}
		tsd_wrapper_set(wrapper);
		tsd_init_finish(&tsd_init_head, &block);
	}
	return wrapper;
}

JEMALLOC_ALWAYS_INLINE bool
tsd_boot0(void) {
	if (pthread_key_create(&tsd_tsd, tsd_cleanup_wrapper) != 0) {
		return true;
	}
	tsd_wrapper_set(&tsd_boot_wrapper);
	tsd_booted = true;
	return false;
}

JEMALLOC_ALWAYS_INLINE void
tsd_boot1(void) {
	tsd_wrapper_t *wrapper;
	wrapper = (tsd_wrapper_t *)malloc_tsd_malloc(sizeof(tsd_wrapper_t));
	if (wrapper == NULL) {
		malloc_write("<jemalloc>: Error allocating TSD\n");
		abort();
	}
	tsd_boot_wrapper.initialized = false;
	tsd_cleanup(&tsd_boot_wrapper.val);
	wrapper->initialized = false;
	tsd_t initializer = TSD_INITIALIZER;
	wrapper->val = initializer;
	tsd_wrapper_set(wrapper);
}

JEMALLOC_ALWAYS_INLINE bool
tsd_boot(void) {
	if (tsd_boot0()) {
		return true;
	}
	tsd_boot1();
	return false;
}

JEMALLOC_ALWAYS_INLINE bool
tsd_booted_get(void) {
	return tsd_booted;
}

JEMALLOC_ALWAYS_INLINE bool
tsd_get_allocates(void) {
	return true;
}

/* Get/set. */
JEMALLOC_ALWAYS_INLINE tsd_t *
tsd_get(bool init) {
	tsd_wrapper_t *wrapper;

	assert(tsd_booted);
	wrapper = tsd_wrapper_get(init);
	if (tsd_get_allocates() && !init && wrapper == NULL) {
		return NULL;
	}
	return &wrapper->val;
}

JEMALLOC_ALWAYS_INLINE void
tsd_set(tsd_t *val) {
	tsd_wrapper_t *wrapper;

	assert(tsd_booted);
	wrapper = tsd_wrapper_get(true);
	if (likely(&wrapper->val != val)) {
		wrapper->val = *(val);
	}
	wrapper->initialized = true;
}
