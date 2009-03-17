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
#define USER_DS		MAKE_MM_SEG(__USER_DS)
#define KERNEL_DS	MAKE_MM_SEG(__KERNEL_DS)

/*
 * Get/set the SFC/DFC registers for MOVES instructions
 */

static inline mm_segment_t get_fs(void)
{
    return USER_DS;
}

static inline mm_segment_t get_ds(void)
{
    /* return the supervisor data space code */
    return KERNEL_DS;
}

static inline void set_fs(mm_segment_t val)
{
}

#define segment_eq(a,b)	((a).seg == (b).seg)

#endif /* __ASSEMBLY__ */

#endif /* _M68K_SEGMENT_H */
