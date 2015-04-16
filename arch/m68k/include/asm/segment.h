#ifndef _M68K_SEGMENT_H
#define _M68K_SEGMENT_H

/* define constants */
/* Address spaces (FC0-FC2) */
#define USER_DATA     (1)
#ifndef __USER_DS
#define __USER_DS     (USER_DATA)
#endif
#define USER_PROGRAM  (2)
#define SUPER_DATA    (5)
#ifndef __KERNEL_DS
#define __KERNEL_DS   (SUPER_DATA)
#endif
#define SUPER_PROGRAM (6)
#define CPU_SPACE     (7)

#ifndef __ASSEMBLY__

typedef struct {
	unsigned long seg;
} mm_segment_t;

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })

#ifdef CONFIG_CPU_HAS_ADDRESS_SPACES
/*
 * Get/set the SFC/DFC registers for MOVES instructions
 */
#define USER_DS		MAKE_MM_SEG(__USER_DS)
#define KERNEL_DS	MAKE_MM_SEG(__KERNEL_DS)

static inline mm_segment_t get_fs(void)
{
	mm_segment_t _v;
	__asm__ ("movec %/dfc,%0":"=r" (_v.seg):);
	return _v;
}

static inline void set_fs(mm_segment_t val)
{
	__asm__ __volatile__ ("movec %0,%/sfc\n\t"
			      "movec %0,%/dfc\n\t"
			      : /* no outputs */ : "r" (val.seg) : "memory");
}

static inline mm_segment_t get_ds(void)
{
    /* return the supervisor data space code */
    return KERNEL_DS;
}

#else
#define USER_DS		MAKE_MM_SEG(TASK_SIZE)
#define KERNEL_DS	MAKE_MM_SEG(0xFFFFFFFF)
#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))
#endif

#define segment_eq(a, b) ((a).seg == (b).seg)

#endif /* __ASSEMBLY__ */

#endif /* _M68K_SEGMENT_H */
