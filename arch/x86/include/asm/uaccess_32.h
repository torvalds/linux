#ifndef _ASM_X86_UACCESS_32_H
#define _ASM_X86_UACCESS_32_H

/*
 * User space memory access functions
 */
#include <linux/errno.h>
#include <linux/thread_info.h>
#include <linux/string.h>
#include <asm/asm.h>
#include <asm/page.h>

unsigned long __must_check __copy_to_user_ll
		(void __user *to, const void *from, unsigned long n);
unsigned long __must_check __copy_from_user_ll
		(void *to, const void __user *from, unsigned long n);
unsigned long __must_check __copy_from_user_ll_nozero
		(void *to, const void __user *from, unsigned long n);
unsigned long __must_check __copy_from_user_ll_nocache
		(void *to, const void __user *from, unsigned long n);
unsigned long __must_check __copy_from_user_ll_nocache_nozero
		(void *to, const void __user *from, unsigned long n);

/**
 * __copy_to_user_inatomic: - Copy a block of data into user space, with less checking.
 * @to:   Destination address, in user space.
 * @from: Source address, in kernel space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only.
 *
 * Copy data from kernel space to user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 * The caller should also make sure he pins the user space address
 * so that we don't result in page fault and sleep.
 */
static __always_inline unsigned long __must_check
__copy_to_user_inatomic(void __user *to, const void *from, unsigned long n)
{
	check_object_size(from, n, true);
	return __copy_to_user_ll(to, from, n);
}

/**
 * __copy_to_user: - Copy a block of data into user space, with less checking.
 * @to:   Destination address, in user space.
 * @from: Source address, in kernel space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Copy data from kernel space to user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 */
static __always_inline unsigned long __must_check
__copy_to_user(void __user *to, const void *from, unsigned long n)
{
	might_fault();
	return __copy_to_user_inatomic(to, from, n);
}

static __always_inline unsigned long
__copy_from_user_inatomic(void *to, const void __user *from, unsigned long n)
{
	return __copy_from_user_ll_nozero(to, from, n);
}

/**
 * __copy_from_user: - Copy a block of data from user space, with less checking.
 * @to:   Destination address, in kernel space.
 * @from: Source address, in user space.
 * @n:    Number of bytes to copy.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Copy data from user space to kernel space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be copied.
 * On success, this will be zero.
 *
 * If some data could not be copied, this function will pad the copied
 * data to the requested size using zero bytes.
 *
 * An alternate version - __copy_from_user_inatomic() - may be called from
 * atomic context and will fail rather than sleep.  In this case the
 * uncopied bytes will *NOT* be padded with zeros.  See fs/filemap.h
 * for explanation of why this is needed.
 */
static __always_inline unsigned long
__copy_from_user(void *to, const void __user *from, unsigned long n)
{
	might_fault();
	check_object_size(to, n, false);
	if (__builtin_constant_p(n)) {
		unsigned long ret;

		switch (n) {
		case 1:
			__uaccess_begin();
			__get_user_size(*(u8 *)to, from, 1, ret, 1);
			__uaccess_end();
			return ret;
		case 2:
			__uaccess_begin();
			__get_user_size(*(u16 *)to, from, 2, ret, 2);
			__uaccess_end();
			return ret;
		case 4:
			__uaccess_begin();
			__get_user_size(*(u32 *)to, from, 4, ret, 4);
			__uaccess_end();
			return ret;
		}
	}
	return __copy_from_user_ll(to, from, n);
}

static __always_inline unsigned long __copy_from_user_nocache(void *to,
				const void __user *from, unsigned long n)
{
	might_fault();
	if (__builtin_constant_p(n)) {
		unsigned long ret;

		switch (n) {
		case 1:
			__uaccess_begin();
			__get_user_size(*(u8 *)to, from, 1, ret, 1);
			__uaccess_end();
			return ret;
		case 2:
			__uaccess_begin();
			__get_user_size(*(u16 *)to, from, 2, ret, 2);
			__uaccess_end();
			return ret;
		case 4:
			__uaccess_begin();
			__get_user_size(*(u32 *)to, from, 4, ret, 4);
			__uaccess_end();
			return ret;
		}
	}
	return __copy_from_user_ll_nocache(to, from, n);
}

static __always_inline unsigned long
__copy_from_user_inatomic_nocache(void *to, const void __user *from,
				  unsigned long n)
{
       return __copy_from_user_ll_nocache_nozero(to, from, n);
}

#endif /* _ASM_X86_UACCESS_32_H */
