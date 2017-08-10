/*
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 *
 * Based on: include/asm-m68knommu/uaccess.h
 */

#ifndef __BLACKFIN_UACCESS_H
#define __BLACKFIN_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/mm.h>
#include <linux/string.h>

#include <asm/segment.h>
#include <asm/sections.h>

#define get_ds()        (KERNEL_DS)
#define get_fs()        (current_thread_info()->addr_limit)

static inline void set_fs(mm_segment_t fs)
{
	current_thread_info()->addr_limit = fs;
}

#define segment_eq(a, b) ((a) == (b))

#define access_ok(type, addr, size) _access_ok((unsigned long)(addr), (size))

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 */

#ifndef CONFIG_ACCESS_CHECK
static inline int _access_ok(unsigned long addr, unsigned long size) { return 1; }
#else
extern int _access_ok(unsigned long addr, unsigned long size);
#endif

#include <asm/extable.h>

/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */

#define put_user(x, p)						\
	({							\
		int _err = 0;					\
		typeof(*(p)) _x = (x);				\
		typeof(*(p)) __user *_p = (p);			\
		if (!access_ok(VERIFY_WRITE, _p, sizeof(*(_p)))) {\
			_err = -EFAULT;				\
		}						\
		else {						\
		switch (sizeof (*(_p))) {			\
		case 1:						\
			__put_user_asm(_x, _p, B);		\
			break;					\
		case 2:						\
			__put_user_asm(_x, _p, W);		\
			break;					\
		case 4:						\
			__put_user_asm(_x, _p,  );		\
			break;					\
		case 8: {					\
			long _xl, _xh;				\
			_xl = ((__force long *)&_x)[0];		\
			_xh = ((__force long *)&_x)[1];		\
			__put_user_asm(_xl, ((__force long __user *)_p)+0, );\
			__put_user_asm(_xh, ((__force long __user *)_p)+1, );\
		} break;					\
		default:					\
			_err = __put_user_bad();		\
			break;					\
		}						\
		}						\
		_err;						\
	})

#define __put_user(x, p) put_user(x, p)
static inline int bad_user_access_length(void)
{
	panic("bad_user_access_length");
	return -1;
}

#define __put_user_bad() (printk(KERN_INFO "put_user_bad %s:%d %s\n",\
                           __FILE__, __LINE__, __func__),\
                           bad_user_access_length(), (-EFAULT))

/*
 * Tell gcc we read from memory instead of writing: this is because
 * we do not write to any memory gcc knows about, so there are no
 * aliasing issues.
 */

#define __ptr(x) ((unsigned long __force *)(x))

#define __put_user_asm(x, p, bhw)			\
	__asm__ (#bhw"[%1] = %0;\n\t"			\
		 : /* no outputs */			\
		 :"d" (x), "a" (__ptr(p)) : "memory")

#define get_user(x, ptr)					\
({								\
	int _err = 0;						\
	unsigned long _val = 0;					\
	const typeof(*(ptr)) __user *_p = (ptr);		\
	const size_t ptr_size = sizeof(*(_p));			\
	if (likely(access_ok(VERIFY_READ, _p, ptr_size))) {	\
		BUILD_BUG_ON(ptr_size >= 8);			\
		switch (ptr_size) {				\
		case 1:						\
			__get_user_asm(_val, _p, B, (Z));	\
			break;					\
		case 2:						\
			__get_user_asm(_val, _p, W, (Z));	\
			break;					\
		case 4:						\
			__get_user_asm(_val, _p,  , );		\
			break;					\
		}						\
	} else							\
		_err = -EFAULT;					\
	x = (__force typeof(*(ptr)))_val;			\
	_err;							\
})

#define __get_user(x, p) get_user(x, p)

#define __get_user_bad() (bad_user_access_length(), (-EFAULT))

#define __get_user_asm(x, ptr, bhw, option)	\
({						\
	__asm__ __volatile__ (			\
		"%0 =" #bhw "[%1]" #option ";"	\
		: "=d" (x)			\
		: "a" (__ptr(ptr)));		\
})

static inline unsigned long __must_check
raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	memcpy(to, (const void __force *)from, n);
	return 0;
}

static inline unsigned long __must_check
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	memcpy((void __force *)to, from, n);
	SSYNC();
	return 0;
}

#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER
/*
 * Copy a null terminated string from userspace.
 */

static inline long __must_check
strncpy_from_user(char *dst, const char __user *src, long count)
{
	char *tmp;
	if (!access_ok(VERIFY_READ, src, 1))
		return -EFAULT;
	strncpy(dst, (const char __force *)src, count);
	for (tmp = dst; *tmp && count > 0; tmp++, count--) ;
	return (tmp - dst);
}

/*
 * Get the size of a string in user space.
 *   src: The string to measure
 *     n: The maximum valid length
 *
 * Get the size of a NUL-terminated string in user space.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 * On exception, returns 0.
 * If the string is too long, returns a value greater than n.
 */
static inline long __must_check strnlen_user(const char __user *src, long n)
{
	if (!access_ok(VERIFY_READ, src, 1))
		return 0;
	return strnlen((const char __force *)src, n) + 1;
}

/*
 * Zero Userspace
 */

static inline unsigned long __must_check
__clear_user(void __user *to, unsigned long n)
{
	if (!access_ok(VERIFY_WRITE, to, n))
		return n;
	memset((void __force *)to, 0, n);
	return 0;
}

#define clear_user(to, n) __clear_user(to, n)

/* How to interpret these return values:
 *	CORE:      can be accessed by core load or dma memcpy
 *	CORE_ONLY: can only be accessed by core load
 *	DMA:       can only be accessed by dma memcpy
 *	IDMA:      can only be accessed by interprocessor dma memcpy (BF561)
 *	ITEST:     can be accessed by isram memcpy or dma memcpy
 */
enum {
	BFIN_MEM_ACCESS_CORE = 0,
	BFIN_MEM_ACCESS_CORE_ONLY,
	BFIN_MEM_ACCESS_DMA,
	BFIN_MEM_ACCESS_IDMA,
	BFIN_MEM_ACCESS_ITEST,
};
/**
 *	bfin_mem_access_type() - what kind of memory access is required
 *	@addr:   the address to check
 *	@size:   number of bytes needed
 *	@return: <0 is error, >=0 is BFIN_MEM_ACCESS_xxx enum (see above)
 */
int bfin_mem_access_type(unsigned long addr, unsigned long size);

#endif				/* _BLACKFIN_UACCESS_H */
