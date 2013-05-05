/* 
 * User address space access functions.
 *
 * Copyright 1997 Andi Kleen <ak@muc.de>
 * Copyright 1997 Linus Torvalds
 * Copyright 2002 Andi Kleen <ak@suse.de>
 */
#include <linux/module.h>
#include <asm/uaccess.h>

/*
 * Zero Userspace
 */

unsigned long __clear_user(void __user *addr, unsigned long size)
{
	long __d0;
	might_fault();
	/* no memory constraint because it doesn't change any memory gcc knows
	   about */
	stac();
	asm volatile(
		"	testq  %[size8],%[size8]\n"
		"	jz     4f\n"
		"0:	movq %[zero],(%[dst])\n"
		"	addq   %[eight],%[dst]\n"
		"	decl %%ecx ; jnz   0b\n"
		"4:	movq  %[size1],%%rcx\n"
		"	testl %%ecx,%%ecx\n"
		"	jz     2f\n"
		"1:	movb   %b[zero],(%[dst])\n"
		"	incq   %[dst]\n"
		"	decl %%ecx ; jnz  1b\n"
		"2:\n"
		".section .fixup,\"ax\"\n"
		"3:	lea 0(%[size1],%[size8],8),%[size8]\n"
		"	jmp 2b\n"
		".previous\n"
		_ASM_EXTABLE(0b,3b)
		_ASM_EXTABLE(1b,2b)
		: [size8] "=&c"(size), [dst] "=&D" (__d0)
		: [size1] "r"(size & 7), "[size8]" (size / 8), "[dst]"(addr),
		  [zero] "r" (0UL), [eight] "r" (8UL));
	clac();
	return size;
}
EXPORT_SYMBOL(__clear_user);

unsigned long clear_user(void __user *to, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		return __clear_user(to, n);
	return n;
}
EXPORT_SYMBOL(clear_user);

unsigned long copy_in_user(void __user *to, const void __user *from, unsigned len)
{
	if (access_ok(VERIFY_WRITE, to, len) && access_ok(VERIFY_READ, from, len)) { 
		return copy_user_generic((__force void *)to, (__force void *)from, len);
	} 
	return len;		
}
EXPORT_SYMBOL(copy_in_user);

/*
 * Try to copy last bytes and clear the rest if needed.
 * Since protection fault in copy_from/to_user is not a normal situation,
 * it is not necessary to optimize tail handling.
 */
unsigned long
copy_user_handle_tail(char *to, char *from, unsigned len, unsigned zerorest)
{
	char c;
	unsigned zero_len;

	for (; len; --len, to++) {
		if (__get_user_nocheck(c, from++, sizeof(char)))
			break;
		if (__put_user_nocheck(c, to, sizeof(char)))
			break;
	}

	for (c = 0, zero_len = len; zerorest && zero_len; --zero_len)
		if (__put_user_nocheck(c, to++, sizeof(char)))
			break;
	clac();
	return len;
}
