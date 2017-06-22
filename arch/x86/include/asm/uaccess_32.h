#ifndef _ASM_X86_UACCESS_32_H
#define _ASM_X86_UACCESS_32_H

/*
 * User space memory access functions
 */
#include <linux/string.h>
#include <asm/asm.h>
#include <asm/page.h>

unsigned long __must_check __copy_user_ll
		(void *to, const void *from, unsigned long n);
unsigned long __must_check __copy_from_user_ll_nocache_nozero
		(void *to, const void __user *from, unsigned long n);

static __always_inline unsigned long __must_check
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return __copy_user_ll((__force void *)to, from, n);
}

static __always_inline unsigned long
raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (__builtin_constant_p(n)) {
		unsigned long ret;

		switch (n) {
		case 1:
			ret = 0;
			__uaccess_begin();
			__get_user_asm_nozero(*(u8 *)to, from, ret,
					      "b", "b", "=q", 1);
			__uaccess_end();
			return ret;
		case 2:
			ret = 0;
			__uaccess_begin();
			__get_user_asm_nozero(*(u16 *)to, from, ret,
					      "w", "w", "=r", 2);
			__uaccess_end();
			return ret;
		case 4:
			ret = 0;
			__uaccess_begin();
			__get_user_asm_nozero(*(u32 *)to, from, ret,
					      "l", "k", "=r", 4);
			__uaccess_end();
			return ret;
		}
	}
	return __copy_user_ll(to, (__force const void *)from, n);
}

static __always_inline unsigned long
__copy_from_user_inatomic_nocache(void *to, const void __user *from,
				  unsigned long n)
{
       return __copy_from_user_ll_nocache_nozero(to, from, n);
}

#endif /* _ASM_X86_UACCESS_32_H */
