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
	start__ > max__ || size__ > max__ - start__; \
})

#define range_overflows_t(type, start, size, max) \
	range_overflows((type)(start), (type)(size), (type)(max))

/* Note we don't consider signbits :| */
#define overflows_type(x, T) \
	(sizeof(x) > sizeof(T) && (x) >> BITS_PER_TYPE(T))

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

#define page_mask_bits(ptr) ptr_mask_bits(ptr, PAGE_SHIFT)
#define page_unmask_bits(ptr) ptr_unmask_bits(ptr, PAGE_SHIFT)
#define page_pack_bits(ptr, bits) ptr_pack_bits(ptr, bits, PAGE_SHIFT)
#define page_unpack_bits(ptr, bits) ptr_unpack_bits(ptr, bits, PAGE_SHIFT)

#define ptr_offset(ptr, member) offsetof(typeof(*(ptr)), member)

#define fetch_and_zero(ptr) ({						\
	typeof(*ptr) __T = *(ptr);					\
	*(ptr) = (typeof(*ptr))0;					\
	__T;								\
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

#include <linux/list.h>

static inline int list_is_first(const struct list_head *list,
				const struct list_head *head)
{
	return head->next == list;
}

static inline void __list_del_many(struct list_head *head,
				   struct list_head *first)
{
	first->prev = head;
	WRITE_ONCE(head->next, first);
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

#endif /* !__I915_UTILS_H */
