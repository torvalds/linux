/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _BCACHE_UTIL_H
#define _BCACHE_UTIL_H

#include <linux/blkdev.h>
#include <linux/closure.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched/clock.h>
#include <linux/llist.h>
#include <linux/ratelimit.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/crc64.h>

struct closure;

#ifdef CONFIG_BCACHE_DEBUG

#define EBUG_ON(cond)			BUG_ON(cond)
#define atomic_dec_bug(v)	BUG_ON(atomic_dec_return(v) < 0)
#define atomic_inc_bug(v, i)	BUG_ON(atomic_inc_return(v) <= i)

#else /* DEBUG */

#define EBUG_ON(cond)		do { if (cond) do {} while (0); } while (0)
#define atomic_dec_bug(v)	atomic_dec(v)
#define atomic_inc_bug(v, i)	atomic_inc(v)

#endif

#define DECLARE_HEAP(type, name)					\
	struct {							\
		size_t size, used;					\
		type *data;						\
	} name

#define init_heap(heap, _size, gfp)					\
({									\
	size_t _bytes;							\
	(heap)->used = 0;						\
	(heap)->size = (_size);						\
	_bytes = (heap)->size * sizeof(*(heap)->data);			\
	(heap)->data = kvmalloc(_bytes, (gfp) & GFP_KERNEL);		\
	(heap)->data;							\
})

#define free_heap(heap)							\
do {									\
	kvfree((heap)->data);						\
	(heap)->data = NULL;						\
} while (0)

#define heap_swap(h, i, j)	swap((h)->data[i], (h)->data[j])

#define heap_sift(h, i, cmp)						\
do {									\
	size_t _r, _j = i;						\
									\
	for (; _j * 2 + 1 < (h)->used; _j = _r) {			\
		_r = _j * 2 + 1;					\
		if (_r + 1 < (h)->used &&				\
		    cmp((h)->data[_r], (h)->data[_r + 1]))		\
			_r++;						\
									\
		if (cmp((h)->data[_r], (h)->data[_j]))			\
			break;						\
		heap_swap(h, _r, _j);					\
	}								\
} while (0)

#define heap_sift_down(h, i, cmp)					\
do {									\
	while (i) {							\
		size_t p = (i - 1) / 2;					\
		if (cmp((h)->data[i], (h)->data[p]))			\
			break;						\
		heap_swap(h, i, p);					\
		i = p;							\
	}								\
} while (0)

#define heap_add(h, d, cmp)						\
({									\
	bool _r = !heap_full(h);					\
	if (_r) {							\
		size_t _i = (h)->used++;				\
		(h)->data[_i] = d;					\
									\
		heap_sift_down(h, _i, cmp);				\
		heap_sift(h, _i, cmp);					\
	}								\
	_r;								\
})

#define heap_pop(h, d, cmp)						\
({									\
	bool _r = (h)->used;						\
	if (_r) {							\
		(d) = (h)->data[0];					\
		(h)->used--;						\
		heap_swap(h, 0, (h)->used);				\
		heap_sift(h, 0, cmp);					\
	}								\
	_r;								\
})

#define heap_peek(h)	((h)->used ? (h)->data[0] : NULL)

#define heap_full(h)	((h)->used == (h)->size)

#define DECLARE_FIFO(type, name)					\
	struct {							\
		size_t front, back, size, mask;				\
		type *data;						\
	} name

#define fifo_for_each(c, fifo, iter)					\
	for (iter = (fifo)->front;					\
	     c = (fifo)->data[iter], iter != (fifo)->back;		\
	     iter = (iter + 1) & (fifo)->mask)

#define __init_fifo(fifo, gfp)						\
({									\
	size_t _allocated_size, _bytes;					\
	BUG_ON(!(fifo)->size);						\
									\
	_allocated_size = roundup_pow_of_two((fifo)->size + 1);		\
	_bytes = _allocated_size * sizeof(*(fifo)->data);		\
									\
	(fifo)->mask = _allocated_size - 1;				\
	(fifo)->front = (fifo)->back = 0;				\
									\
	(fifo)->data = kvmalloc(_bytes, (gfp) & GFP_KERNEL);		\
	(fifo)->data;							\
})

#define init_fifo_exact(fifo, _size, gfp)				\
({									\
	(fifo)->size = (_size);						\
	__init_fifo(fifo, gfp);						\
})

#define init_fifo(fifo, _size, gfp)					\
({									\
	(fifo)->size = (_size);						\
	if ((fifo)->size > 4)						\
		(fifo)->size = roundup_pow_of_two((fifo)->size) - 1;	\
	__init_fifo(fifo, gfp);						\
})

#define free_fifo(fifo)							\
do {									\
	kvfree((fifo)->data);						\
	(fifo)->data = NULL;						\
} while (0)

#define fifo_used(fifo)		(((fifo)->back - (fifo)->front) & (fifo)->mask)
#define fifo_free(fifo)		((fifo)->size - fifo_used(fifo))

#define fifo_empty(fifo)	(!fifo_used(fifo))
#define fifo_full(fifo)		(!fifo_free(fifo))

#define fifo_front(fifo)	((fifo)->data[(fifo)->front])
#define fifo_back(fifo)							\
	((fifo)->data[((fifo)->back - 1) & (fifo)->mask])

#define fifo_idx(fifo, p)	(((p) - &fifo_front(fifo)) & (fifo)->mask)

#define fifo_push_back(fifo, i)						\
({									\
	bool _r = !fifo_full((fifo));					\
	if (_r) {							\
		(fifo)->data[(fifo)->back++] = (i);			\
		(fifo)->back &= (fifo)->mask;				\
	}								\
	_r;								\
})

#define fifo_pop_front(fifo, i)						\
({									\
	bool _r = !fifo_empty((fifo));					\
	if (_r) {							\
		(i) = (fifo)->data[(fifo)->front++];			\
		(fifo)->front &= (fifo)->mask;				\
	}								\
	_r;								\
})

#define fifo_push_front(fifo, i)					\
({									\
	bool _r = !fifo_full((fifo));					\
	if (_r) {							\
		--(fifo)->front;					\
		(fifo)->front &= (fifo)->mask;				\
		(fifo)->data[(fifo)->front] = (i);			\
	}								\
	_r;								\
})

#define fifo_pop_back(fifo, i)						\
({									\
	bool _r = !fifo_empty((fifo));					\
	if (_r) {							\
		--(fifo)->back;						\
		(fifo)->back &= (fifo)->mask;				\
		(i) = (fifo)->data[(fifo)->back]			\
	}								\
	_r;								\
})

#define fifo_push(fifo, i)	fifo_push_back(fifo, (i))
#define fifo_pop(fifo, i)	fifo_pop_front(fifo, (i))

#define fifo_swap(l, r)							\
do {									\
	swap((l)->front, (r)->front);					\
	swap((l)->back, (r)->back);					\
	swap((l)->size, (r)->size);					\
	swap((l)->mask, (r)->mask);					\
	swap((l)->data, (r)->data);					\
} while (0)

#define fifo_move(dest, src)						\
do {									\
	typeof(*((dest)->data)) _t;					\
	while (!fifo_full(dest) &&					\
	       fifo_pop(src, _t))					\
		fifo_push(dest, _t);					\
} while (0)

/*
 * Simple array based allocator - preallocates a number of elements and you can
 * never allocate more than that, also has no locking.
 *
 * Handy because if you know you only need a fixed number of elements you don't
 * have to worry about memory allocation failure, and sometimes a mempool isn't
 * what you want.
 *
 * We treat the free elements as entries in a singly linked list, and the
 * freelist as a stack - allocating and freeing push and pop off the freelist.
 */

#define DECLARE_ARRAY_ALLOCATOR(type, name, size)			\
	struct {							\
		type	*freelist;					\
		type	data[size];					\
	} name

#define array_alloc(array)						\
({									\
	typeof((array)->freelist) _ret = (array)->freelist;		\
									\
	if (_ret)							\
		(array)->freelist = *((typeof((array)->freelist) *) _ret);\
									\
	_ret;								\
})

#define array_free(array, ptr)						\
do {									\
	typeof((array)->freelist) _ptr = ptr;				\
									\
	*((typeof((array)->freelist) *) _ptr) = (array)->freelist;	\
	(array)->freelist = _ptr;					\
} while (0)

#define array_allocator_init(array)					\
do {									\
	typeof((array)->freelist) _i;					\
									\
	BUILD_BUG_ON(sizeof((array)->data[0]) < sizeof(void *));	\
	(array)->freelist = NULL;					\
									\
	for (_i = (array)->data;					\
	     _i < (array)->data + ARRAY_SIZE((array)->data);		\
	     _i++)							\
		array_free(array, _i);					\
} while (0)

#define array_freelist_empty(array)	((array)->freelist == NULL)

#define ANYSINT_MAX(t)							\
	((((t) 1 << (sizeof(t) * 8 - 2)) - (t) 1) * (t) 2 + (t) 1)

int bch_strtoint_h(const char *cp, int *res);
int bch_strtouint_h(const char *cp, unsigned int *res);
int bch_strtoll_h(const char *cp, long long *res);
int bch_strtoull_h(const char *cp, unsigned long long *res);

static inline int bch_strtol_h(const char *cp, long *res)
{
#if BITS_PER_LONG == 32
	return bch_strtoint_h(cp, (int *) res);
#else
	return bch_strtoll_h(cp, (long long *) res);
#endif
}

static inline int bch_strtoul_h(const char *cp, long *res)
{
#if BITS_PER_LONG == 32
	return bch_strtouint_h(cp, (unsigned int *) res);
#else
	return bch_strtoull_h(cp, (unsigned long long *) res);
#endif
}

#define strtoi_h(cp, res)						\
	(__builtin_types_compatible_p(typeof(*res), int)		\
	? bch_strtoint_h(cp, (void *) res)				\
	: __builtin_types_compatible_p(typeof(*res), long)		\
	? bch_strtol_h(cp, (void *) res)				\
	: __builtin_types_compatible_p(typeof(*res), long long)		\
	? bch_strtoll_h(cp, (void *) res)				\
	: __builtin_types_compatible_p(typeof(*res), unsigned int)	\
	? bch_strtouint_h(cp, (void *) res)				\
	: __builtin_types_compatible_p(typeof(*res), unsigned long)	\
	? bch_strtoul_h(cp, (void *) res)				\
	: __builtin_types_compatible_p(typeof(*res), unsigned long long)\
	? bch_strtoull_h(cp, (void *) res) : -EINVAL)

#define strtoul_safe(cp, var)						\
({									\
	unsigned long _v;						\
	int _r = kstrtoul(cp, 10, &_v);					\
	if (!_r)							\
		var = _v;						\
	_r;								\
})

#define strtoul_safe_clamp(cp, var, min, max)				\
({									\
	unsigned long _v;						\
	int _r = kstrtoul(cp, 10, &_v);					\
	if (!_r)							\
		var = clamp_t(typeof(var), _v, min, max);		\
	_r;								\
})

ssize_t bch_hprint(char *buf, int64_t v);

bool bch_is_zero(const char *p, size_t n);
int bch_parse_uuid(const char *s, char *uuid);

struct time_stats {
	spinlock_t	lock;
	/*
	 * all fields are in nanoseconds, averages are ewmas stored left shifted
	 * by 8
	 */
	uint64_t	max_duration;
	uint64_t	average_duration;
	uint64_t	average_frequency;
	uint64_t	last;
};

void bch_time_stats_update(struct time_stats *stats, uint64_t time);

static inline unsigned int local_clock_us(void)
{
	return local_clock() >> 10;
}

#define NSEC_PER_ns			1L
#define NSEC_PER_us			NSEC_PER_USEC
#define NSEC_PER_ms			NSEC_PER_MSEC
#define NSEC_PER_sec			NSEC_PER_SEC

#define __print_time_stat(stats, name, stat, units)			\
	sysfs_print(name ## _ ## stat ## _ ## units,			\
		    div_u64((stats)->stat >> 8, NSEC_PER_ ## units))

#define sysfs_print_time_stats(stats, name,				\
			       frequency_units,				\
			       duration_units)				\
do {									\
	__print_time_stat(stats, name,					\
			  average_frequency,	frequency_units);	\
	__print_time_stat(stats, name,					\
			  average_duration,	duration_units);	\
	sysfs_print(name ## _ ##max_duration ## _ ## duration_units,	\
			div_u64((stats)->max_duration,			\
				NSEC_PER_ ## duration_units));		\
									\
	sysfs_print(name ## _last_ ## frequency_units, (stats)->last	\
		    ? div_s64(local_clock() - (stats)->last,		\
			      NSEC_PER_ ## frequency_units)		\
		    : -1LL);						\
} while (0)

#define sysfs_time_stats_attribute(name,				\
				   frequency_units,			\
				   duration_units)			\
read_attribute(name ## _average_frequency_ ## frequency_units);		\
read_attribute(name ## _average_duration_ ## duration_units);		\
read_attribute(name ## _max_duration_ ## duration_units);		\
read_attribute(name ## _last_ ## frequency_units)

#define sysfs_time_stats_attribute_list(name,				\
					frequency_units,		\
					duration_units)			\
&sysfs_ ## name ## _average_frequency_ ## frequency_units,		\
&sysfs_ ## name ## _average_duration_ ## duration_units,		\
&sysfs_ ## name ## _max_duration_ ## duration_units,			\
&sysfs_ ## name ## _last_ ## frequency_units,

#define ewma_add(ewma, val, weight, factor)				\
({									\
	(ewma) *= (weight) - 1;						\
	(ewma) += (val) << factor;					\
	(ewma) /= (weight);						\
	(ewma) >> factor;						\
})

struct bch_ratelimit {
	/* Next time we want to do some work, in nanoseconds */
	uint64_t		next;

	/*
	 * Rate at which we want to do work, in units per second
	 * The units here correspond to the units passed to bch_next_delay()
	 */
	atomic_long_t		rate;
};

static inline void bch_ratelimit_reset(struct bch_ratelimit *d)
{
	d->next = local_clock();
}

uint64_t bch_next_delay(struct bch_ratelimit *d, uint64_t done);

#define __DIV_SAFE(n, d, zero)						\
({									\
	typeof(n) _n = (n);						\
	typeof(d) _d = (d);						\
	_d ? _n / _d : zero;						\
})

#define DIV_SAFE(n, d)	__DIV_SAFE(n, d, 0)

#define container_of_or_null(ptr, type, member)				\
({									\
	typeof(ptr) _ptr = ptr;						\
	_ptr ? container_of(_ptr, type, member) : NULL;			\
})

#define RB_INSERT(root, new, member, cmp)				\
({									\
	__label__ dup;							\
	struct rb_node **n = &(root)->rb_node, *parent = NULL;		\
	typeof(new) this;						\
	int res, ret = -1;						\
									\
	while (*n) {							\
		parent = *n;						\
		this = container_of(*n, typeof(*(new)), member);	\
		res = cmp(new, this);					\
		if (!res)						\
			goto dup;					\
		n = res < 0						\
			? &(*n)->rb_left				\
			: &(*n)->rb_right;				\
	}								\
									\
	rb_link_node(&(new)->member, parent, n);			\
	rb_insert_color(&(new)->member, root);				\
	ret = 0;							\
dup:									\
	ret;								\
})

#define RB_SEARCH(root, search, member, cmp)				\
({									\
	struct rb_node *n = (root)->rb_node;				\
	typeof(&(search)) this, ret = NULL;				\
	int res;							\
									\
	while (n) {							\
		this = container_of(n, typeof(search), member);		\
		res = cmp(&(search), this);				\
		if (!res) {						\
			ret = this;					\
			break;						\
		}							\
		n = res < 0						\
			? n->rb_left					\
			: n->rb_right;					\
	}								\
	ret;								\
})

#define RB_GREATER(root, search, member, cmp)				\
({									\
	struct rb_node *n = (root)->rb_node;				\
	typeof(&(search)) this, ret = NULL;				\
	int res;							\
									\
	while (n) {							\
		this = container_of(n, typeof(search), member);		\
		res = cmp(&(search), this);				\
		if (res < 0) {						\
			ret = this;					\
			n = n->rb_left;					\
		} else							\
			n = n->rb_right;				\
	}								\
	ret;								\
})

#define RB_FIRST(root, type, member)					\
	container_of_or_null(rb_first(root), type, member)

#define RB_LAST(root, type, member)					\
	container_of_or_null(rb_last(root), type, member)

#define RB_NEXT(ptr, member)						\
	container_of_or_null(rb_next(&(ptr)->member), typeof(*ptr), member)

#define RB_PREV(ptr, member)						\
	container_of_or_null(rb_prev(&(ptr)->member), typeof(*ptr), member)

static inline uint64_t bch_crc64(const void *p, size_t len)
{
	uint64_t crc = 0xffffffffffffffffULL;

	crc = crc64_be(crc, p, len);
	return crc ^ 0xffffffffffffffffULL;
}

/*
 * A stepwise-linear pseudo-exponential.  This returns 1 << (x >>
 * frac_bits), with the less-significant bits filled in by linear
 * interpolation.
 *
 * This can also be interpreted as a floating-point number format,
 * where the low frac_bits are the mantissa (with implicit leading
 * 1 bit), and the more significant bits are the exponent.
 * The return value is 1.mantissa * 2^exponent.
 *
 * The way this is used, fract_bits is 6 and the largest possible
 * input is CONGESTED_MAX-1 = 1023 (exponent 16, mantissa 0x1.fc),
 * so the maximum output is 0x1fc00.
 */
static inline unsigned int fract_exp_two(unsigned int x,
					 unsigned int fract_bits)
{
	unsigned int mantissa = 1 << fract_bits;	/* Implicit bit */

	mantissa += x & (mantissa - 1);
	x >>= fract_bits;	/* The exponent */
	/* Largest intermediate value 0x7f0000 */
	return mantissa << x >> fract_bits;
}

void bch_bio_map(struct bio *bio, void *base);
int bch_bio_alloc_pages(struct bio *bio, gfp_t gfp_mask);

#endif /* _BCACHE_UTIL_H */
