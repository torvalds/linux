/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef __I915_UTILS_H
#define __I915_UTILS_H

#include <linux/list.h>
#include <linux/overflow.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct drm_i915_private;
struct timer_list;

#define FDO_BUG_URL "https://gitlab.freedesktop.org/drm/intel/-/wikis/How-to-file-i915-bugs"

#undef WARN_ON
/* Many gcc seem to no see through this and fall over :( */
#if 0
#define WARN_ON(x) ({ \
	bool __i915_warn_cond = (x); \
	if (__builtin_constant_p(__i915_warn_cond)) \
		BUILD_BUG_ON(__i915_warn_cond); \
	WARN(__i915_warn_cond, "WARN_ON(" #x ")"); })
#else
#define WARN_ON(x) WARN((x), "%s", "WARN_ON(" __stringify(x) ")")
#endif

#undef WARN_ON_ONCE
#define WARN_ON_ONCE(x) WARN_ONCE((x), "%s", "WARN_ON_ONCE(" __stringify(x) ")")

#define MISSING_CASE(x) WARN(1, "Missing case (%s == %ld)\n", \
			     __stringify(x), (long)(x))

void __printf(3, 4)
__i915_printk(struct drm_i915_private *dev_priv, const char *level,
	      const char *fmt, ...);

#define i915_report_error(dev_priv, fmt, ...)				   \
	__i915_printk(dev_priv, KERN_ERR, fmt, ##__VA_ARGS__)

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG)

int __i915_inject_probe_error(struct drm_i915_private *i915, int err,
			      const char *func, int line);
#define i915_inject_probe_error(_i915, _err) \
	__i915_inject_probe_error((_i915), (_err), __func__, __LINE__)
bool i915_error_injected(void);

#else

#define i915_inject_probe_error(i915, e) ({ BUILD_BUG_ON_INVALID(i915); 0; })
#define i915_error_injected() false

#endif

#define i915_inject_probe_failure(i915) i915_inject_probe_error((i915), -ENODEV)

#define i915_probe_error(i915, fmt, ...)				   \
	__i915_printk(i915, i915_error_injected() ? KERN_DEBUG : KERN_ERR, \
		      fmt, ##__VA_ARGS__)

#if defined(GCC_VERSION) && GCC_VERSION >= 70000
#define add_overflows_t(T, A, B) \
	__builtin_add_overflow_p((A), (B), (T)0)
#else
#define add_overflows_t(T, A, B) ({ \
	typeof(A) a = (A); \
	typeof(B) b = (B); \
	(T)(a + b) < a; \
})
#endif

#define add_overflows(A, B) \
	add_overflows_t(typeof((A) + (B)), (A), (B))

#define range_overflows(start, size, max) ({ \
	typeof(start) start__ = (start); \
	typeof(size) size__ = (size); \
	typeof(max) max__ = (max); \
	(void)(&start__ == &size__); \
	(void)(&start__ == &max__); \
	start__ >= max__ || size__ > max__ - start__; \
})

#define range_overflows_t(type, start, size, max) \
	range_overflows((type)(start), (type)(size), (type)(max))

#define range_overflows_end(start, size, max) ({ \
	typeof(start) start__ = (start); \
	typeof(size) size__ = (size); \
	typeof(max) max__ = (max); \
	(void)(&start__ == &size__); \
	(void)(&start__ == &max__); \
	start__ > max__ || size__ > max__ - start__; \
})

#define range_overflows_end_t(type, start, size, max) \
	range_overflows_end((type)(start), (type)(size), (type)(max))

/* Note we don't consider signbits :| */
#define overflows_type(x, T) \
	(sizeof(x) > sizeof(T) && (x) >> BITS_PER_TYPE(T))

static inline bool
__check_struct_size(size_t base, size_t arr, size_t count, size_t *size)
{
	size_t sz;

	if (check_mul_overflow(count, arr, &sz))
		return false;

	if (check_add_overflow(sz, base, &sz))
		return false;

	*size = sz;
	return true;
}

/**
 * check_struct_size() - Calculate size of structure with trailing array.
 * @p: Pointer to the structure.
 * @member: Name of the array member.
 * @n: Number of elements in the array.
 * @sz: Total size of structure and array
 *
 * Calculates size of memory needed for structure @p followed by an
 * array of @n @member elements, like struct_size() but reports
 * whether it overflowed, and the resultant size in @sz
 *
 * Return: false if the calculation overflowed.
 */
#define check_struct_size(p, member, n, sz) \
	likely(__check_struct_size(sizeof(*(p)), \
				   sizeof(*(p)->member) + __must_be_array((p)->member), \
				   n, sz))

#define ptr_mask_bits(ptr, n) ({					\
	unsigned long __v = (unsigned long)(ptr);			\
	(typeof(ptr))(__v & -BIT(n));					\
})

#define ptr_unmask_bits(ptr, n) ((unsigned long)(ptr) & (BIT(n) - 1))

#define ptr_unpack_bits(ptr, bits, n) ({				\
	unsigned long __v = (unsigned long)(ptr);			\
	*(bits) = __v & (BIT(n) - 1);					\
	(typeof(ptr))(__v & -BIT(n));					\
})

#define ptr_pack_bits(ptr, bits, n) ({					\
	unsigned long __bits = (bits);					\
	GEM_BUG_ON(__bits & -BIT(n));					\
	((typeof(ptr))((unsigned long)(ptr) | __bits));			\
})

#define ptr_dec(ptr) ({							\
	unsigned long __v = (unsigned long)(ptr);			\
	(typeof(ptr))(__v - 1);						\
})

#define ptr_inc(ptr) ({							\
	unsigned long __v = (unsigned long)(ptr);			\
	(typeof(ptr))(__v + 1);						\
})

#define page_mask_bits(ptr) ptr_mask_bits(ptr, PAGE_SHIFT)
#define page_unmask_bits(ptr) ptr_unmask_bits(ptr, PAGE_SHIFT)
#define page_pack_bits(ptr, bits) ptr_pack_bits(ptr, bits, PAGE_SHIFT)
#define page_unpack_bits(ptr, bits) ptr_unpack_bits(ptr, bits, PAGE_SHIFT)

#define struct_member(T, member) (((T *)0)->member)

#define ptr_offset(ptr, member) offsetof(typeof(*(ptr)), member)

#define fetch_and_zero(ptr) ({						\
	typeof(*ptr) __T = *(ptr);					\
	*(ptr) = (typeof(*ptr))0;					\
	__T;								\
})

/*
 * container_of_user: Extract the superclass from a pointer to a member.
 *
 * Exactly like container_of() with the exception that it plays nicely
 * with sparse for __user @ptr.
 */
#define container_of_user(ptr, type, member) ({				\
	void __user *__mptr = (void __user *)(ptr);			\
	BUILD_BUG_ON_MSG(!__same_type(*(ptr), struct_member(type, member)) && \
			 !__same_type(*(ptr), void),			\
			 "pointer type mismatch in container_of()");	\
	((type __user *)(__mptr - offsetof(type, member))); })

/*
 * check_user_mbz: Check that a user value exists and is zero
 *
 * Frequently in our uABI we reserve space for future extensions, and
 * two ensure that userspace is prepared we enforce that space must
 * be zero. (Then any future extension can safely assume a default value
 * of 0.)
 *
 * check_user_mbz() combines checking that the user pointer is accessible
 * and that the contained value is zero.
 *
 * Returns: -EFAULT if not accessible, -EINVAL if !zero, or 0 on success.
 */
#define check_user_mbz(U) ({						\
	typeof(*(U)) mbz__;						\
	get_user(mbz__, (U)) ? -EFAULT : mbz__ ? -EINVAL : 0;		\
})

static inline u64 ptr_to_u64(const void *ptr)
{
	return (uintptr_t)ptr;
}

#define u64_to_ptr(T, x) ({						\
	typecheck(u64, x);						\
	(T *)(uintptr_t)(x);						\
})

#define __mask_next_bit(mask) ({					\
	int __idx = ffs(mask) - 1;					\
	mask &= ~BIT(__idx);						\
	__idx;								\
})

static inline bool is_power_of_2_u64(u64 n)
{
	return (n != 0 && ((n & (n - 1)) == 0));
}

static inline void __list_del_many(struct list_head *head,
				   struct list_head *first)
{
	first->prev = head;
	WRITE_ONCE(head->next, first);
}

static inline int list_is_last_rcu(const struct list_head *list,
				   const struct list_head *head)
{
	return READ_ONCE(list->next) == head;
}

/*
 * Wait until the work is finally complete, even if it tries to postpone
 * by requeueing itself. Note, that if the worker never cancels itself,
 * we will spin forever.
 */
static inline void drain_delayed_work(struct delayed_work *dw)
{
	do {
		while (flush_delayed_work(dw))
			;
	} while (delayed_work_pending(dw));
}

static inline unsigned long msecs_to_jiffies_timeout(const unsigned int m)
{
	unsigned long j = msecs_to_jiffies(m);

	return min_t(unsigned long, MAX_JIFFY_OFFSET, j + 1);
}

/*
 * If you need to wait X milliseconds between events A and B, but event B
 * doesn't happen exactly after event A, you record the timestamp (jiffies) of
 * when event A happened, then just before event B you call this function and
 * pass the timestamp as the first argument, and X as the second argument.
 */
static inline void
wait_remaining_ms_from_jiffies(unsigned long timestamp_jiffies, int to_wait_ms)
{
	unsigned long target_jiffies, tmp_jiffies, remaining_jiffies;

	/*
	 * Don't re-read the value of "jiffies" every time since it may change
	 * behind our back and break the math.
	 */
	tmp_jiffies = jiffies;
	target_jiffies = timestamp_jiffies +
			 msecs_to_jiffies_timeout(to_wait_ms);

	if (time_after(target_jiffies, tmp_jiffies)) {
		remaining_jiffies = target_jiffies - tmp_jiffies;
		while (remaining_jiffies)
			remaining_jiffies =
			    schedule_timeout_uninterruptible(remaining_jiffies);
	}
}

/**
 * __wait_for - magic wait macro
 *
 * Macro to help avoid open coding check/wait/timeout patterns. Note that it's
 * important that we check the condition again after having timed out, since the
 * timeout could be due to preemption or similar and we've never had a chance to
 * check the condition before the timeout.
 */
#define __wait_for(OP, COND, US, Wmin, Wmax) ({ \
	const ktime_t end__ = ktime_add_ns(ktime_get_raw(), 1000ll * (US)); \
	long wait__ = (Wmin); /* recommended min for usleep is 10 us */	\
	int ret__;							\
	might_sleep();							\
	for (;;) {							\
		const bool expired__ = ktime_after(ktime_get_raw(), end__); \
		OP;							\
		/* Guarantee COND check prior to timeout */		\
		barrier();						\
		if (COND) {						\
			ret__ = 0;					\
			break;						\
		}							\
		if (expired__) {					\
			ret__ = -ETIMEDOUT;				\
			break;						\
		}							\
		usleep_range(wait__, wait__ * 2);			\
		if (wait__ < (Wmax))					\
			wait__ <<= 1;					\
	}								\
	ret__;								\
})

#define _wait_for(COND, US, Wmin, Wmax)	__wait_for(, (COND), (US), (Wmin), \
						   (Wmax))
#define wait_for(COND, MS)		_wait_for((COND), (MS) * 1000, 10, 1000)

/* If CONFIG_PREEMPT_COUNT is disabled, in_atomic() always reports false. */
#if defined(CONFIG_DRM_I915_DEBUG) && defined(CONFIG_PREEMPT_COUNT)
# define _WAIT_FOR_ATOMIC_CHECK(ATOMIC) WARN_ON_ONCE((ATOMIC) && !in_atomic())
#else
# define _WAIT_FOR_ATOMIC_CHECK(ATOMIC) do { } while (0)
#endif

#define _wait_for_atomic(COND, US, ATOMIC) \
({ \
	int cpu, ret, timeout = (US) * 1000; \
	u64 base; \
	_WAIT_FOR_ATOMIC_CHECK(ATOMIC); \
	if (!(ATOMIC)) { \
		preempt_disable(); \
		cpu = smp_processor_id(); \
	} \
	base = local_clock(); \
	for (;;) { \
		u64 now = local_clock(); \
		if (!(ATOMIC)) \
			preempt_enable(); \
		/* Guarantee COND check prior to timeout */ \
		barrier(); \
		if (COND) { \
			ret = 0; \
			break; \
		} \
		if (now - base >= timeout) { \
			ret = -ETIMEDOUT; \
			break; \
		} \
		cpu_relax(); \
		if (!(ATOMIC)) { \
			preempt_disable(); \
			if (unlikely(cpu != smp_processor_id())) { \
				timeout -= now - base; \
				cpu = smp_processor_id(); \
				base = local_clock(); \
			} \
		} \
	} \
	ret; \
})

#define wait_for_us(COND, US) \
({ \
	int ret__; \
	BUILD_BUG_ON(!__builtin_constant_p(US)); \
	if ((US) > 10) \
		ret__ = _wait_for((COND), (US), 10, 10); \
	else \
		ret__ = _wait_for_atomic((COND), (US), 0); \
	ret__; \
})

#define wait_for_atomic_us(COND, US) \
({ \
	BUILD_BUG_ON(!__builtin_constant_p(US)); \
	BUILD_BUG_ON((US) > 50000); \
	_wait_for_atomic((COND), (US), 1); \
})

#define wait_for_atomic(COND, MS) wait_for_atomic_us((COND), (MS) * 1000)

#define KHz(x) (1000 * (x))
#define MHz(x) KHz(1000 * (x))

#define KBps(x) (1000 * (x))
#define MBps(x) KBps(1000 * (x))
#define GBps(x) ((u64)1000 * MBps((x)))

static inline const char *yesno(bool v)
{
	return v ? "yes" : "no";
}

static inline const char *onoff(bool v)
{
	return v ? "on" : "off";
}

static inline const char *enableddisabled(bool v)
{
	return v ? "enabled" : "disabled";
}

static inline void add_taint_for_CI(unsigned int taint)
{
	/*
	 * The system is "ok", just about surviving for the user, but
	 * CI results are now unreliable as the HW is very suspect.
	 * CI checks the taint state after every test and will reboot
	 * the machine if the kernel is tainted.
	 */
	add_taint(taint, LOCKDEP_STILL_OK);
}

void cancel_timer(struct timer_list *t);
void set_timer_ms(struct timer_list *t, unsigned long timeout);

static inline bool timer_expired(const struct timer_list *t)
{
	return READ_ONCE(t->expires) && !timer_pending(t);
}

/*
 * This is a lookalike for IS_ENABLED() that takes a kconfig value,
 * e.g. CONFIG_DRM_I915_SPIN_REQUEST, and evaluates whether it is non-zero
 * i.e. whether the configuration is active. Wrapping up the config inside
 * a boolean context prevents clang and smatch from complaining about potential
 * issues in confusing logical-&& with bitwise-& for constants.
 *
 * Sadly IS_ENABLED() itself does not work with kconfig values.
 *
 * Returns 0 if @config is 0, 1 if set to any value.
 */
#define IS_ACTIVE(config) ((config) != 0)

#endif /* !__I915_UTILS_H */
