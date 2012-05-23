#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/bug.h>

void copy_from_user_overflow(void)
{
	WARN(1, "Buffer overflow detected!\n");
}
EXPORT_SYMBOL(copy_from_user_overflow);

#define REPEAT_BYTE(x)	((~0ul / 0xff) * (x))

/* Return the high bit set in the first byte that is a zero */
static inline unsigned long has_zero(unsigned long a)
{
	return ((a - REPEAT_BYTE(0x01)) & ~a) & REPEAT_BYTE(0x80);
}

static inline long find_zero(unsigned long c)
{
#ifdef CONFIG_64BIT
	if (!(c & 0xff00000000000000UL))
		return 0;
	if (!(c & 0x00ff000000000000UL))
		return 1;
	if (!(c & 0x0000ff0000000000UL))
		return 2;
	if (!(c & 0x000000ff00000000UL))
		return 3;
#define __OFF 4
#else
#define __OFF 0
#endif
	if (!(c & 0xff000000))
		return __OFF + 0;
	if (!(c & 0x00ff0000))
		return __OFF + 1;
	if (!(c & 0x0000ff00))
		return __OFF + 2;
	return __OFF + 3;
#undef __OFF
}

/*
 * Do a strncpy, return length of string without final '\0'.
 * 'count' is the user-supplied count (return 'count' if we
 * hit it), 'max' is the address space maximum (and we return
 * -EFAULT if we hit it).
 */
static inline long do_strncpy_from_user(char *dst, const char __user *src, long count, unsigned long max)
{
	long res = 0;

	/*
	 * Truncate 'max' to the user-specified limit, so that
	 * we only have one limit we need to check in the loop
	 */
	if (max > count)
		max = count;

	if (((long) dst | (long) src) & (sizeof(long) - 1))
		goto byte_at_a_time;

	while (max >= sizeof(unsigned long)) {
		unsigned long c;

		/* Fall back to byte-at-a-time if we get a page fault */
		if (unlikely(__get_user(c,(unsigned long __user *)(src+res))))
			break;
		*(unsigned long *)(dst+res) = c;
		if (has_zero(c))
			return res + find_zero(c);
		res += sizeof(unsigned long);
		max -= sizeof(unsigned long);
	}

byte_at_a_time:
	while (max) {
		char c;

		if (unlikely(__get_user(c,src+res)))
			return -EFAULT;
		dst[res] = c;
		if (!c)
			return res;
		res++;
		max--;
	}

	/*
	 * Uhhuh. We hit 'max'. But was that the user-specified maximum
	 * too? If so, that's ok - we got as much as the user asked for.
	 */
	if (res >= count)
		return res;

	/*
	 * Nope: we hit the address space limit, and we still had more
	 * characters the caller would have wanted. That's an EFAULT.
	 */
	return -EFAULT;
}

/**
 * strncpy_from_user: - Copy a NUL terminated string from userspace.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from userspace to kernel space.
 *
 * On success, returns the length of the string (not including the trailing
 * NUL).
 *
 * If access to userspace fails, returns -EFAULT (some data may have been
 * copied).
 *
 * If @count is smaller than the length of the string, copies @count bytes
 * and returns @count.
 */
long strncpy_from_user(char *dst, const char __user *src, long count)
{
	unsigned long max_addr, src_addr;

	if (unlikely(count <= 0))
		return 0;

	max_addr = ~0UL;
	if (likely(segment_eq(get_fs(), USER_DS)))
		max_addr = STACK_TOP;
	src_addr = (unsigned long)src;
	if (likely(src_addr < max_addr)) {
		unsigned long max = max_addr - src_addr;
		return do_strncpy_from_user(dst, src, count, max);
	}
	return -EFAULT;
}
EXPORT_SYMBOL(strncpy_from_user);
