#ifndef __METAG_UACCESS_H
#define __METAG_UACCESS_H

/*
 * User space memory access functions
 */

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s)  ((mm_segment_t) { (s) })

#define KERNEL_DS       MAKE_MM_SEG(0xFFFFFFFF)
#define USER_DS		MAKE_MM_SEG(PAGE_OFFSET)

#define get_ds()	(KERNEL_DS)
#define get_fs()        (current_thread_info()->addr_limit)
#define set_fs(x)       (current_thread_info()->addr_limit = (x))

#define segment_eq(a, b)	((a).seg == (b).seg)

#define __kernel_ok (segment_eq(get_fs(), KERNEL_DS))
/*
 * Explicitly allow NULL pointers here. Parts of the kernel such
 * as readv/writev use access_ok to validate pointers, but want
 * to allow NULL pointers for various reasons. NULL pointers are
 * safe to allow through because the first page is not mappable on
 * Meta.
 *
 * We also wish to avoid letting user code access the system area
 * and the kernel half of the address space.
 */
#define __user_bad(addr, size) (((addr) > 0 && (addr) < META_MEMORY_BASE) || \
				((addr) > PAGE_OFFSET &&		\
				 (addr) < LINCORE_BASE))

static inline int __access_ok(unsigned long addr, unsigned long size)
{
	return __kernel_ok || !__user_bad(addr, size);
}

#define access_ok(type, addr, size) __access_ok((unsigned long)(addr),	\
						(unsigned long)(size))

static inline int verify_area(int type, const void *addr, unsigned long size)
{
	return access_ok(type, addr, size) ? 0 : -EFAULT;
}

/*
 * The exception table consists of pairs of addresses: the first is the
 * address of an instruction that is allowed to fault, and the second is
 * the address at which the program should continue.  No registers are
 * modified, so it is entirely up to the continuation code to figure out
 * what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */
struct exception_table_entry {
	unsigned long insn, fixup;
};

extern int fixup_exception(struct pt_regs *regs);

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */

#define put_user(x, ptr) \
	__put_user_check((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))
#define __put_user(x, ptr) \
	__put_user_nocheck((__typeof__(*(ptr)))(x), (ptr), sizeof(*(ptr)))

extern void __put_user_bad(void);

#define __put_user_nocheck(x, ptr, size)		\
({                                                      \
	long __pu_err;                                  \
	__put_user_size((x), (ptr), (size), __pu_err);	\
	__pu_err;                                       \
})

#define __put_user_check(x, ptr, size)				\
({                                                              \
	long __pu_err = -EFAULT;                                \
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);           \
	if (access_ok(VERIFY_WRITE, __pu_addr, size))		\
		__put_user_size((x), __pu_addr, (size), __pu_err);	\
	__pu_err;                                               \
})

extern long __put_user_asm_b(unsigned int x, void __user *addr);
extern long __put_user_asm_w(unsigned int x, void __user *addr);
extern long __put_user_asm_d(unsigned int x, void __user *addr);
extern long __put_user_asm_l(unsigned long long x, void __user *addr);

#define __put_user_size(x, ptr, size, retval)				\
do {                                                                    \
	retval = 0;                                                     \
	switch (size) {                                                 \
	case 1:								\
		retval = __put_user_asm_b((__force unsigned int)x, ptr);\
		break;							\
	case 2:								\
		retval = __put_user_asm_w((__force unsigned int)x, ptr);\
		break;							\
	case 4:								\
		retval = __put_user_asm_d((__force unsigned int)x, ptr);\
		break;							\
	case 8:								\
		retval = __put_user_asm_l((__force unsigned long long)x,\
					  ptr);				\
		break;							\
	default:							\
		__put_user_bad();					\
	}								\
} while (0)

#define get_user(x, ptr) \
	__get_user_check((x), (ptr), sizeof(*(ptr)))
#define __get_user(x, ptr) \
	__get_user_nocheck((x), (ptr), sizeof(*(ptr)))

extern long __get_user_bad(void);

#define __get_user_nocheck(x, ptr, size)			\
({                                                              \
	long __gu_err, __gu_val;                                \
	__get_user_size(__gu_val, (ptr), (size), __gu_err);	\
	(x) = (__force __typeof__(*(ptr)))__gu_val;             \
	__gu_err;                                               \
})

#define __get_user_check(x, ptr, size)					\
({                                                                      \
	long __gu_err = -EFAULT, __gu_val = 0;                          \
	const __typeof__(*(ptr)) __user *__gu_addr = (ptr);		\
	if (access_ok(VERIFY_READ, __gu_addr, size))			\
		__get_user_size(__gu_val, __gu_addr, (size), __gu_err);	\
	(x) = (__force __typeof__(*(ptr)))__gu_val;                     \
	__gu_err;                                                       \
})

extern unsigned char __get_user_asm_b(const void __user *addr, long *err);
extern unsigned short __get_user_asm_w(const void __user *addr, long *err);
extern unsigned int __get_user_asm_d(const void __user *addr, long *err);

#define __get_user_size(x, ptr, size, retval)			\
do {                                                            \
	retval = 0;                                             \
	switch (size) {                                         \
	case 1:							\
		x = __get_user_asm_b(ptr, &retval); break;	\
	case 2:							\
		x = __get_user_asm_w(ptr, &retval); break;	\
	case 4:							\
		x = __get_user_asm_d(ptr, &retval); break;	\
	default:						\
		(x) = __get_user_bad();				\
	}                                                       \
} while (0)

/*
 * Copy a null terminated string from userspace.
 *
 * Must return:
 * -EFAULT		for an exception
 * count		if we hit the buffer limit
 * bytes copied		if we hit a null byte
 * (without the null byte)
 */

extern long __must_check __strncpy_from_user(char *dst, const char __user *src,
					     long count);

#define strncpy_from_user(dst, src, count) __strncpy_from_user(dst, src, count)

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */
extern long __must_check strnlen_user(const char __user *src, long count);

#define strlen_user(str) strnlen_user(str, 32767)

extern unsigned long __must_check __copy_user_zeroing(void *to,
						      const void __user *from,
						      unsigned long n);

static inline unsigned long
copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (likely(access_ok(VERIFY_READ, from, n)))
		return __copy_user_zeroing(to, from, n);
	memset(to, 0, n);
	return n;
}

#define __copy_from_user(to, from, n) __copy_user_zeroing(to, from, n)
#define __copy_from_user_inatomic __copy_from_user

extern unsigned long __must_check __copy_user(void __user *to,
					      const void *from,
					      unsigned long n);

static inline unsigned long copy_to_user(void __user *to, const void *from,
					 unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		return __copy_user(to, from, n);
	return n;
}

#define __copy_to_user(to, from, n) __copy_user(to, from, n)
#define __copy_to_user_inatomic __copy_to_user

/*
 * Zero Userspace
 */

extern unsigned long __must_check __do_clear_user(void __user *to,
						  unsigned long n);

static inline unsigned long clear_user(void __user *to, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		return __do_clear_user(to, n);
	return n;
}

#define __clear_user(to, n)            __do_clear_user(to, n)

#endif /* _METAG_UACCESS_H */
