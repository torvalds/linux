/*
 *  Optimized user space space access functions based on mvcos.
 *
 *    Copyright IBM Corp. 2006
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *		 Gerald Schaefer (gerald.schaefer@de.ibm.com)
 */

#include <linux/jump_label.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <asm/facility.h>
#include <asm/uaccess.h>
#include <asm/futex.h>
#include "uaccess.h"

#ifndef CONFIG_64BIT
#define AHI	"ahi"
#define ALR	"alr"
#define CLR	"clr"
#define LHI	"lhi"
#define SLR	"slr"
#else
#define AHI	"aghi"
#define ALR	"algr"
#define CLR	"clgr"
#define LHI	"lghi"
#define SLR	"slgr"
#endif

static struct static_key have_mvcos = STATIC_KEY_INIT_TRUE;

static inline unsigned long copy_from_user_mvcos(void *x, const void __user *ptr,
						 unsigned long size)
{
	register unsigned long reg0 asm("0") = 0x81UL;
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	asm volatile(
		"0: .insn ss,0xc80000000000,0(%0,%2),0(%1),0\n"
		"9: jz    7f\n"
		"1:"ALR"  %0,%3\n"
		"  "SLR"  %1,%3\n"
		"  "SLR"  %2,%3\n"
		"   j     0b\n"
		"2: la    %4,4095(%1)\n"/* %4 = ptr + 4095 */
		"   nr    %4,%3\n"	/* %4 = (ptr + 4095) & -4096 */
		"  "SLR"  %4,%1\n"
		"  "CLR"  %0,%4\n"	/* copy crosses next page boundary? */
		"   jnh   4f\n"
		"3: .insn ss,0xc80000000000,0(%4,%2),0(%1),0\n"
		"10:"SLR"  %0,%4\n"
		"  "ALR"  %2,%4\n"
		"4:"LHI"  %4,-1\n"
		"  "ALR"  %4,%0\n"	/* copy remaining size, subtract 1 */
		"   bras  %3,6f\n"	/* memset loop */
		"   xc    0(1,%2),0(%2)\n"
		"5: xc    0(256,%2),0(%2)\n"
		"   la    %2,256(%2)\n"
		"6:"AHI"  %4,-256\n"
		"   jnm   5b\n"
		"   ex    %4,0(%3)\n"
		"   j     8f\n"
		"7:"SLR"  %0,%0\n"
		"8: \n"
		EX_TABLE(0b,2b) EX_TABLE(3b,4b) EX_TABLE(9b,2b) EX_TABLE(10b,4b)
		: "+a" (size), "+a" (ptr), "+a" (x), "+a" (tmp1), "=a" (tmp2)
		: "d" (reg0) : "cc", "memory");
	return size;
}

unsigned long __copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (static_key_true(&have_mvcos))
		return copy_from_user_mvcos(to, from, n);
	return copy_from_user_pt(to, from, n);
}
EXPORT_SYMBOL(__copy_from_user);

static inline unsigned long copy_to_user_mvcos(void __user *ptr, const void *x,
					       unsigned long size)
{
	register unsigned long reg0 asm("0") = 0x810000UL;
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	asm volatile(
		"0: .insn ss,0xc80000000000,0(%0,%1),0(%2),0\n"
		"6: jz    4f\n"
		"1:"ALR"  %0,%3\n"
		"  "SLR"  %1,%3\n"
		"  "SLR"  %2,%3\n"
		"   j     0b\n"
		"2: la    %4,4095(%1)\n"/* %4 = ptr + 4095 */
		"   nr    %4,%3\n"	/* %4 = (ptr + 4095) & -4096 */
		"  "SLR"  %4,%1\n"
		"  "CLR"  %0,%4\n"	/* copy crosses next page boundary? */
		"   jnh   5f\n"
		"3: .insn ss,0xc80000000000,0(%4,%1),0(%2),0\n"
		"7:"SLR"  %0,%4\n"
		"   j     5f\n"
		"4:"SLR"  %0,%0\n"
		"5: \n"
		EX_TABLE(0b,2b) EX_TABLE(3b,5b) EX_TABLE(6b,2b) EX_TABLE(7b,5b)
		: "+a" (size), "+a" (ptr), "+a" (x), "+a" (tmp1), "=a" (tmp2)
		: "d" (reg0) : "cc", "memory");
	return size;
}

unsigned long __copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (static_key_true(&have_mvcos))
		return copy_to_user_mvcos(to, from, n);
	return copy_to_user_pt(to, from, n);
}
EXPORT_SYMBOL(__copy_to_user);

static inline unsigned long copy_in_user_mvcos(void __user *to, const void __user *from,
					       unsigned long size)
{
	register unsigned long reg0 asm("0") = 0x810081UL;
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	/* FIXME: copy with reduced length. */
	asm volatile(
		"0: .insn ss,0xc80000000000,0(%0,%1),0(%2),0\n"
		"   jz    2f\n"
		"1:"ALR"  %0,%3\n"
		"  "SLR"  %1,%3\n"
		"  "SLR"  %2,%3\n"
		"   j     0b\n"
		"2:"SLR"  %0,%0\n"
		"3: \n"
		EX_TABLE(0b,3b)
		: "+a" (size), "+a" (to), "+a" (from), "+a" (tmp1), "=a" (tmp2)
		: "d" (reg0) : "cc", "memory");
	return size;
}

unsigned long __copy_in_user(void __user *to, const void __user *from, unsigned long n)
{
	if (static_key_true(&have_mvcos))
		return copy_in_user_mvcos(to, from, n);
	return copy_in_user_pt(to, from, n);
}
EXPORT_SYMBOL(__copy_in_user);

static inline unsigned long clear_user_mvcos(void __user *to, unsigned long size)
{
	register unsigned long reg0 asm("0") = 0x810000UL;
	unsigned long tmp1, tmp2;

	tmp1 = -4096UL;
	asm volatile(
		"0: .insn ss,0xc80000000000,0(%0,%1),0(%4),0\n"
		"   jz    4f\n"
		"1:"ALR"  %0,%2\n"
		"  "SLR"  %1,%2\n"
		"   j     0b\n"
		"2: la    %3,4095(%1)\n"/* %4 = to + 4095 */
		"   nr    %3,%2\n"	/* %4 = (to + 4095) & -4096 */
		"  "SLR"  %3,%1\n"
		"  "CLR"  %0,%3\n"	/* copy crosses next page boundary? */
		"   jnh   5f\n"
		"3: .insn ss,0xc80000000000,0(%3,%1),0(%4),0\n"
		"  "SLR"  %0,%3\n"
		"   j     5f\n"
		"4:"SLR"  %0,%0\n"
		"5: \n"
		EX_TABLE(0b,2b) EX_TABLE(3b,5b)
		: "+a" (size), "+a" (to), "+a" (tmp1), "=a" (tmp2)
		: "a" (empty_zero_page), "d" (reg0) : "cc", "memory");
	return size;
}

unsigned long __clear_user(void __user *to, unsigned long size)
{
	if (static_key_true(&have_mvcos))
		return clear_user_mvcos(to, size);
	return clear_user_pt(to, size);
}
EXPORT_SYMBOL(__clear_user);

static inline unsigned long strnlen_user_mvcos(const char __user *src,
					       unsigned long count)
{
	unsigned long done, len, offset, len_str;
	char buf[256];

	done = 0;
	do {
		offset = (unsigned long)src & ~PAGE_MASK;
		len = min(256UL, PAGE_SIZE - offset);
		len = min(count - done, len);
		if (copy_from_user_mvcos(buf, src, len))
			return 0;
		len_str = strnlen(buf, len);
		done += len_str;
		src += len_str;
	} while ((len_str == len) && (done < count));
	return done + 1;
}

unsigned long __strnlen_user(const char __user *src, unsigned long count)
{
	if (static_key_true(&have_mvcos))
		return strnlen_user_mvcos(src, count);
	return strnlen_user_pt(src, count);
}
EXPORT_SYMBOL(__strnlen_user);

static inline long strncpy_from_user_mvcos(char *dst, const char __user *src,
					   long count)
{
	unsigned long done, len, offset, len_str;

	if (unlikely(count <= 0))
		return 0;
	done = 0;
	do {
		offset = (unsigned long)src & ~PAGE_MASK;
		len = min(count - done, PAGE_SIZE - offset);
		if (copy_from_user_mvcos(dst, src, len))
			return -EFAULT;
		len_str = strnlen(dst, len);
		done += len_str;
		src += len_str;
		dst += len_str;
	} while ((len_str == len) && (done < count));
	return done;
}

long __strncpy_from_user(char *dst, const char __user *src, long count)
{
	if (static_key_true(&have_mvcos))
		return strncpy_from_user_mvcos(dst, src, count);
	return strncpy_from_user_pt(dst, src, count);
}
EXPORT_SYMBOL(__strncpy_from_user);

/*
 * The uaccess page tabe walk variant can be enforced with the "uaccesspt"
 * kernel parameter. This is mainly for debugging purposes.
 */
static int force_uaccess_pt __initdata;

static int __init parse_uaccess_pt(char *__unused)
{
	force_uaccess_pt = 1;
	return 0;
}
early_param("uaccesspt", parse_uaccess_pt);

static int __init uaccess_init(void)
{
	if (IS_ENABLED(CONFIG_32BIT) || force_uaccess_pt || !test_facility(27))
		static_key_slow_dec(&have_mvcos);
	return 0;
}
early_initcall(uaccess_init);
