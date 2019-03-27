#ifndef JEMALLOC_INTERNAL_BACKGROUND_THREAD_EXTERNS_H
#define JEMALLOC_INTERNAL_BACKGROUND_THREAD_EXTERNS_H

extern bool opt_background_thread;
extern size_t opt_max_background_threads;
extern malloc_mutex_t background_thread_lock;
extern atomic_b_t background_thread_enabled_state;
extern size_t n_background_threads;
extern size_t max_background_threads;
extern background_thread_info_t *background_thread_info;
extern bool can_enable_background_thread;

bool background_thread_create(tsd_t *tsd, unsigned arena_ind);
bool background_threads_enable(tsd_t *tsd);
bool background_threads_disable(tsd_t *tsd);
void background_thread_interval_check(tsdn_t *tsdn, arena_t *arena,
    arena_decay_t *decay, size_t npages_new);
void background_thread_prefork0(tsdn_t *tsdn);
void background_thread_prefork1(tsdn_t *tsdn);
void background_thread_postfork_parent(tsdn_t *tsdn);
void background_thread_postfork_child(tsdn_t *tsdn);
bool background_thread_stats_read(tsdn_t *tsdn,
    background_thread_stats_t *stats);
void background_thread_ctl_init(tsdn_t *tsdn);

#ifdef JEMALLOC_PTHREAD_CREATE_WRAPPER
extern int pthread_create_wrapper(pthread_t *__restrict, const pthread_attr_t *,
    void *(*)(void *), void *__restrict);
#endif
bool background_thread_boot0(void);
bool background_thread_boot1(tsdn_t *tsdn);

#endif /* JEMALLOC_INTERNAL_BACKGROUND_THREAD_EXTERNS_H */
