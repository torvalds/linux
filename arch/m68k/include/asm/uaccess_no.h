#ifndef __M68KNOMMU_UACCESS_H
#define __M68KNOMMU_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/mm.h>
#include <linux/string.h>

#include <asm/segment.h>

#define access_ok(type,addr,size)	_access_ok((unsigned long)(addr),(size))

/*
 * It is not enough to just have access_ok check for a real RAM address.
 * This would disallow the case of code/ro-data running XIP in flash/rom.
 * Ideally we would check the possible flash ranges too, but that is
 * currently not so easy.
 */
static inline int _access_ok(unsigned long addr, unsigned long size)
{
	return 1;
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

struct exception_table_entry
{
	unsigned long insn, fixup;
};


/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 */

#define put_user(x, ptr)				\
({							\
    int __pu_err = 0;					\
    typeof(*(ptr)) __pu_val = (x);			\
    switch (sizeof (*(ptr))) {				\
    case 1:						\
	__put_user_asm(__pu_err, __pu_val, ptr, b);	\
	break;						\
    case 2:						\
	__put_user_asm(__pu_err, __pu_val, ptr, w);	\
	break;						\
    case 4:						\
	__put_user_asm(__pu_err, __pu_val, ptr, l);	\
	break;						\
    case 8:						\
	memcpy(ptr, &__pu_val, sizeof (*(ptr))); \
	break;						\
    default:						\
	__pu_err = __put_user_bad();			\
	break;						\
    }							\
    __pu_err;						\
})
#define __put_user(x, ptr) put_user(x, ptr)

extern int __put_user_bad(void);

/*
 * Tell gcc we read from memory instead of writing: this is because
 * we do not write to any memory gcc knows about, so there are no
 * aliasing issues.
 */

#define __ptr(x) ((unsigned long *)(x))

#define __put_user_asm(err,x,ptr,bwl)				\
	__asm__ ("move" #bwl " %0,%1"				\
		: /* no outputs */						\
		:"d" (x),"m" (*__ptr(ptr)) : "memory")

#define get_user(x, ptr)					\
({								\
    int __gu_err = 0;						\
    typeof(x) __gu_val = 0;					\
    switch (sizeof(*(ptr))) {					\
    case 1:							\
	__get_user_asm(__gu_err, __gu_val, ptr, b, "=d");	\
	break;							\
    case 2:							\
	__get_user_asm(__gu_err, __gu_val, ptr, w, "=r");	\
	break;							\
    case 4:							\
	__get_user_asm(__gu_err, __gu_val, ptr, l, "=r");	\
	break;							\
    case 8:							\
	memcpy((void *) &__gu_val, ptr, sizeof (*(ptr)));	\
	break;							\
    default:							\
	__gu_val = 0;						\
	__gu_err = __get_user_bad();				\
	break;							\
    }								\
    (x) = (typeof(*(ptr))) __gu_val;				\
    __gu_err;							\
})
#define __get_user(x, ptr) get_user(x, ptr)

extern int __get_user_bad(void);

#define __get_user_asm(err,x,ptr,bwl,reg)			\
	__asm__ ("move" #bwl " %1,%0"				\
		 : "=d" (x)					\
		 : "m" (*__ptr(ptr)))

#define copy_from_user(to, from, n)		(memcpy(to, from, n), 0)
#define copy_to_user(to, from, n)		(memcpy(to, from, n), 0)

#define __copy_from_user(to, from, n) copy_from_user(to, from, n)
#define __copy_to_user(to, from, n) copy_to_user(to, from, n)
#define __copy_to_user_inatomic __copy_to_user
#define __copy_from_user_inatomic __copy_from_user

/*
 * Copy a null terminated string from userspace.
 */

static inline long
strncpy_from_user(char *dst, const char *src, long count)
{
	char *tmp;
	strncpy(dst, src, count);
	for (tmp = dst; *tmp && count > 0; tmp++, count--)
		;
	return(tmp - dst); /* DAVIDM should we count a NUL ?  check getname */
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 on exception, a value greater than N if too long
 */
static inline long strnlen_user(const char *src, long n)
{
	return(strlen(src) + 1); /* DAVIDM make safer */
}

#define strlen_user(str) strnlen_user(str, 32767)

/*
 * Zero Userspace
 */

static inline unsigned long
__clear_user(void *to, unsigned long n)
{
	memset(to, 0, n);
	return 0;
}

#define	clear_user(to,n)	__clear_user(to,n)

#endif /* _M68KNOMMU_UACCESS_H */
