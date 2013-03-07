#include <linux/bug.h>

#ifdef CONFIG_ZCACHE_DEBUG

/* we try to keep these statistics SMP-consistent */
static ssize_t zcache_obj_count;
static atomic_t zcache_obj_atomic = ATOMIC_INIT(0);
static ssize_t zcache_obj_count_max;
static inline void inc_zcache_obj_count(void)
{
	zcache_obj_count = atomic_inc_return(&zcache_obj_atomic);
	if (zcache_obj_count > zcache_obj_count_max)
		zcache_obj_count_max = zcache_obj_count;
}
static inline void dec_zcache_obj_count(void)
{
	zcache_obj_count = atomic_dec_return(&zcache_obj_atomic);
	BUG_ON(zcache_obj_count < 0);
};
static ssize_t zcache_objnode_count;
static atomic_t zcache_objnode_atomic = ATOMIC_INIT(0);
static ssize_t zcache_objnode_count_max;
static inline void inc_zcache_objnode_count(void)
{
	zcache_objnode_count = atomic_inc_return(&zcache_objnode_atomic);
	if (zcache_objnode_count > zcache_objnode_count_max)
		zcache_objnode_count_max = zcache_objnode_count;
};
static inline void dec_zcache_objnode_count(void)
{
	zcache_objnode_count = atomic_dec_return(&zcache_objnode_atomic);
	BUG_ON(zcache_objnode_count < 0);
};
static u64 zcache_eph_zbytes;
static atomic_long_t zcache_eph_zbytes_atomic = ATOMIC_INIT(0);
static u64 zcache_eph_zbytes_max;
static inline void inc_zcache_eph_zbytes(unsigned clen)
{
	zcache_eph_zbytes = atomic_long_add_return(clen, &zcache_eph_zbytes_atomic);
	if (zcache_eph_zbytes > zcache_eph_zbytes_max)
		zcache_eph_zbytes_max = zcache_eph_zbytes;
};
static inline void dec_zcache_eph_zbytes(unsigned zsize)
{
	zcache_eph_zbytes = atomic_long_sub_return(zsize, &zcache_eph_zbytes_atomic);
};
extern  u64 zcache_pers_zbytes;
static atomic_long_t zcache_pers_zbytes_atomic = ATOMIC_INIT(0);
static u64 zcache_pers_zbytes_max;
static inline void inc_zcache_pers_zbytes(unsigned clen)
{
	zcache_pers_zbytes = atomic_long_add_return(clen, &zcache_pers_zbytes_atomic);
	if (zcache_pers_zbytes > zcache_pers_zbytes_max)
		zcache_pers_zbytes_max = zcache_pers_zbytes;
}
static inline void dec_zcache_pers_zbytes(unsigned zsize)
{
	zcache_pers_zbytes = atomic_long_sub_return(zsize, &zcache_pers_zbytes_atomic);
}
extern ssize_t zcache_eph_pageframes;
static atomic_t zcache_eph_pageframes_atomic = ATOMIC_INIT(0);
static ssize_t zcache_eph_pageframes_max;
static inline void inc_zcache_eph_pageframes(void)
{
	zcache_eph_pageframes = atomic_inc_return(&zcache_eph_pageframes_atomic);
	if (zcache_eph_pageframes > zcache_eph_pageframes_max)
		zcache_eph_pageframes_max = zcache_eph_pageframes;
};
static inline void dec_zcache_eph_pageframes(void)
{
	zcache_eph_pageframes = atomic_dec_return(&zcache_eph_pageframes_atomic);
};
extern ssize_t zcache_pers_pageframes;
static atomic_t zcache_pers_pageframes_atomic = ATOMIC_INIT(0);
static ssize_t zcache_pers_pageframes_max;
static inline void inc_zcache_pers_pageframes(void)
{
	zcache_pers_pageframes = atomic_inc_return(&zcache_pers_pageframes_atomic);
	if (zcache_pers_pageframes > zcache_pers_pageframes_max)
		zcache_pers_pageframes_max = zcache_pers_pageframes;
}
static inline void dec_zcache_pers_pageframes(void)
{
	zcache_pers_pageframes = atomic_dec_return(&zcache_pers_pageframes_atomic);
}
static ssize_t zcache_pageframes_alloced;
static atomic_t zcache_pageframes_alloced_atomic = ATOMIC_INIT(0);
static inline void inc_zcache_pageframes_alloced(void)
{
	zcache_pageframes_alloced = atomic_inc_return(&zcache_pageframes_alloced_atomic);
};
static ssize_t zcache_pageframes_freed;
static atomic_t zcache_pageframes_freed_atomic = ATOMIC_INIT(0);
static inline void inc_zcache_pageframes_freed(void)
{
	zcache_pageframes_freed = atomic_inc_return(&zcache_pageframes_freed_atomic);
}
static ssize_t zcache_eph_zpages;
static atomic_t zcache_eph_zpages_atomic = ATOMIC_INIT(0);
static ssize_t zcache_eph_zpages_max;
static inline void inc_zcache_eph_zpages(void)
{
	zcache_eph_zpages = atomic_inc_return(&zcache_eph_zpages_atomic);
	if (zcache_eph_zpages > zcache_eph_zpages_max)
		zcache_eph_zpages_max = zcache_eph_zpages;
}
static inline void dec_zcache_eph_zpages(unsigned zpages)
{
	zcache_eph_zpages = atomic_sub_return(zpages, &zcache_eph_zpages_atomic);
}
extern ssize_t zcache_pers_zpages;
static atomic_t zcache_pers_zpages_atomic = ATOMIC_INIT(0);
static ssize_t zcache_pers_zpages_max;
static inline void inc_zcache_pers_zpages(void)
{
	zcache_pers_zpages = atomic_inc_return(&zcache_pers_zpages_atomic);
	if (zcache_pers_zpages > zcache_pers_zpages_max)
		zcache_pers_zpages_max = zcache_pers_zpages;
}
static inline void dec_zcache_pers_zpages(unsigned zpages)
{
	zcache_pers_zpages = atomic_sub_return(zpages, &zcache_pers_zpages_atomic);
}

static inline unsigned long curr_pageframes_count(void)
{
	return zcache_pageframes_alloced -
		atomic_read(&zcache_pageframes_freed_atomic) -
		atomic_read(&zcache_eph_pageframes_atomic) -
		atomic_read(&zcache_pers_pageframes_atomic);
};
/* but for the rest of these, counting races are ok */
static ssize_t zcache_flush_total;
static ssize_t zcache_flush_found;
static ssize_t zcache_flobj_total;
static ssize_t zcache_flobj_found;
static ssize_t zcache_failed_eph_puts;
static ssize_t zcache_failed_pers_puts;
static ssize_t zcache_failed_getfreepages;
static ssize_t zcache_failed_alloc;
static ssize_t zcache_put_to_flush;
static ssize_t zcache_compress_poor;
static ssize_t zcache_mean_compress_poor;
static ssize_t zcache_eph_ate_tail;
static ssize_t zcache_eph_ate_tail_failed;
static ssize_t zcache_pers_ate_eph;
static ssize_t zcache_pers_ate_eph_failed;
static ssize_t zcache_evicted_eph_zpages;
static ssize_t zcache_evicted_eph_pageframes;

extern ssize_t zcache_last_active_file_pageframes;
extern ssize_t zcache_last_inactive_file_pageframes;
extern ssize_t zcache_last_active_anon_pageframes;
extern ssize_t zcache_last_inactive_anon_pageframes;
static ssize_t zcache_eph_nonactive_puts_ignored;
static ssize_t zcache_pers_nonactive_puts_ignored;
#ifdef CONFIG_ZCACHE_WRITEBACK
extern ssize_t zcache_writtenback_pages;
extern ssize_t zcache_outstanding_writeback_pages;
#endif

static inline void inc_zcache_flush_total(void) { zcache_flush_total ++; };
static inline void inc_zcache_flush_found(void) { zcache_flush_found ++; };
static inline void inc_zcache_flobj_total(void) { zcache_flobj_total ++; };
static inline void inc_zcache_flobj_found(void) { zcache_flobj_found ++; };
static inline void inc_zcache_failed_eph_puts(void) { zcache_failed_eph_puts ++; };
static inline void inc_zcache_failed_pers_puts(void) { zcache_failed_pers_puts ++; };
static inline void inc_zcache_failed_getfreepages(void) { zcache_failed_getfreepages ++; };
static inline void inc_zcache_failed_alloc(void) { zcache_failed_alloc ++; };
static inline void inc_zcache_put_to_flush(void) { zcache_put_to_flush ++; };
static inline void inc_zcache_compress_poor(void) { zcache_compress_poor ++; };
static inline void inc_zcache_mean_compress_poor(void) { zcache_mean_compress_poor ++; };
static inline void inc_zcache_eph_ate_tail(void) { zcache_eph_ate_tail ++; };
static inline void inc_zcache_eph_ate_tail_failed(void) { zcache_eph_ate_tail_failed ++; };
static inline void inc_zcache_pers_ate_eph(void) { zcache_pers_ate_eph ++; };
static inline void inc_zcache_pers_ate_eph_failed(void) { zcache_pers_ate_eph_failed ++; };
static inline void inc_zcache_evicted_eph_zpages(unsigned zpages) { zcache_evicted_eph_zpages += zpages; };
static inline void inc_zcache_evicted_eph_pageframes(void) { zcache_evicted_eph_pageframes ++; };

static inline void inc_zcache_eph_nonactive_puts_ignored(void) { zcache_eph_nonactive_puts_ignored ++; };
static inline void inc_zcache_pers_nonactive_puts_ignored(void) { zcache_pers_nonactive_puts_ignored ++; };

int zcache_debugfs_init(void);
#else
static inline void inc_zcache_obj_count(void) { };
static inline void dec_zcache_obj_count(void) { };
static inline void inc_zcache_objnode_count(void) { };
static inline void dec_zcache_objnode_count(void) { };
static inline void inc_zcache_eph_zbytes(unsigned clen) { };
static inline void dec_zcache_eph_zbytes(unsigned zsize) { };
static inline void inc_zcache_pers_zbytes(unsigned clen) { };
static inline void dec_zcache_pers_zbytes(unsigned zsize) { };
static inline void inc_zcache_eph_pageframes(void) { };
static inline void dec_zcache_eph_pageframes(void) { };
static inline void inc_zcache_pers_pageframes(void) { };
static inline void dec_zcache_pers_pageframes(void) { };
static inline void inc_zcache_pageframes_alloced(void) { };
static inline void inc_zcache_pageframes_freed(void) { };
static inline void inc_zcache_eph_zpages(void) { };
static inline void dec_zcache_eph_zpages(unsigned zpages) { };
static inline void inc_zcache_pers_zpages(void) { };
static inline void dec_zcache_pers_zpages(unsigned zpages) { };
static inline unsigned long curr_pageframes_count(void)
{
	return 0;
};
static inline int zcache_debugfs_init(void)
{
	return 0;
};
static inline void inc_zcache_flush_total(void) { };
static inline void inc_zcache_flush_found(void) { };
static inline void inc_zcache_flobj_total(void) { };
static inline void inc_zcache_flobj_found(void) { };
static inline void inc_zcache_failed_eph_puts(void) { };
static inline void inc_zcache_failed_pers_puts(void) { };
static inline void inc_zcache_failed_getfreepages(void) { };
static inline void inc_zcache_failed_alloc(void) { };
static inline void inc_zcache_put_to_flush(void) { };
static inline void inc_zcache_compress_poor(void) { };
static inline void inc_zcache_mean_compress_poor(void) { };
static inline void inc_zcache_eph_ate_tail(void) { };
static inline void inc_zcache_eph_ate_tail_failed(void) { };
static inline void inc_zcache_pers_ate_eph(void) { };
static inline void inc_zcache_pers_ate_eph_failed(void) { };
static inline void inc_zcache_evicted_eph_zpages(unsigned zpages) { };
static inline void inc_zcache_evicted_eph_pageframes(void) { };

static inline void inc_zcache_eph_nonactive_puts_ignored(void) { };
static inline void inc_zcache_pers_nonactive_puts_ignored(void) { };
#endif
