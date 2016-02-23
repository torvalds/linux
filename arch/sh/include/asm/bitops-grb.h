#ifndef __ASM_SH_BITOPS_GRB_H
#define __ASM_SH_BITOPS_GRB_H

static inline void set_bit(int nr, volatile void * addr)
{
	int	mask;
	volatile unsigned int *a = addr;
	unsigned long tmp;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);

        __asm__ __volatile__ (
                "   .align 2              \n\t"
                "   mova    1f,   r0      \n\t" /* r0 = end point */
                "   mov    r15,   r1      \n\t" /* r1 = saved sp */
                "   mov    #-6,   r15     \n\t" /* LOGIN: r15 = size */
                "   mov.l  @%1,   %0      \n\t" /* load  old value */
                "   or      %2,   %0      \n\t" /* or */
                "   mov.l   %0,   @%1     \n\t" /* store new value */
                "1: mov     r1,   r15     \n\t" /* LOGOUT */
                : "=&r" (tmp),
                  "+r"  (a)
                : "r"   (mask)
                : "memory" , "r0", "r1");
}

static inline void clear_bit(int nr, volatile void * addr)
{
	int	mask;
	volatile unsigned int *a = addr;
        unsigned long tmp;

	a += nr >> 5;
        mask = ~(1 << (nr & 0x1f));
        __asm__ __volatile__ (
                "   .align 2              \n\t"
                "   mova    1f,   r0      \n\t" /* r0 = end point */
                "   mov    r15,   r1      \n\t" /* r1 = saved sp */
                "   mov    #-6,   r15     \n\t" /* LOGIN: r15 = size */
                "   mov.l  @%1,   %0      \n\t" /* load  old value */
                "   and     %2,   %0      \n\t" /* and */
                "   mov.l   %0,   @%1     \n\t" /* store new value */
                "1: mov     r1,   r15     \n\t" /* LOGOUT */
                : "=&r" (tmp),
                  "+r"  (a)
                : "r"   (mask)
                : "memory" , "r0", "r1");
}

static inline void change_bit(int nr, volatile void * addr)
{
        int     mask;
        volatile unsigned int *a = addr;
        unsigned long tmp;

        a += nr >> 5;
        mask = 1 << (nr & 0x1f);
        __asm__ __volatile__ (
                "   .align 2              \n\t"
                "   mova    1f,   r0      \n\t" /* r0 = end point */
                "   mov    r15,   r1      \n\t" /* r1 = saved sp */
                "   mov    #-6,   r15     \n\t" /* LOGIN: r15 = size */
                "   mov.l  @%1,   %0      \n\t" /* load  old value */
                "   xor     %2,   %0      \n\t" /* xor */
                "   mov.l   %0,   @%1     \n\t" /* store new value */
                "1: mov     r1,   r15     \n\t" /* LOGOUT */
                : "=&r" (tmp),
                  "+r"  (a)
                : "r"   (mask)
                : "memory" , "r0", "r1");
}

static inline int test_and_set_bit(int nr, volatile void * addr)
{
        int     mask, retval;
	volatile unsigned int *a = addr;
        unsigned long tmp;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);

        __asm__ __volatile__ (
                "   .align 2              \n\t"
                "   mova    1f,   r0      \n\t" /* r0 = end point */
                "   mov    r15,   r1      \n\t" /* r1 = saved sp */
                "   mov   #-14,   r15     \n\t" /* LOGIN: r15 = size */
                "   mov.l  @%2,   %0      \n\t" /* load old value */
                "   mov     %0,   %1      \n\t"
                "   tst     %1,   %3      \n\t" /* T = ((*a & mask) == 0) */
                "   mov    #-1,   %1      \n\t" /* retvat = -1 */
                "   negc    %1,   %1      \n\t" /* retval = (mask & *a) != 0 */
                "   or      %3,   %0      \n\t"
                "   mov.l   %0,  @%2      \n\t" /* store new value */
                "1: mov     r1,  r15      \n\t" /* LOGOUT */
                : "=&r" (tmp),
                  "=&r" (retval),
                  "+r"  (a)
                : "r"   (mask)
                : "memory" , "r0", "r1" ,"t");

        return retval;
}

static inline int test_and_clear_bit(int nr, volatile void * addr)
{
        int     mask, retval,not_mask;
        volatile unsigned int *a = addr;
        unsigned long tmp;

        a += nr >> 5;
        mask = 1 << (nr & 0x1f);

	not_mask = ~mask;

        __asm__ __volatile__ (
                "   .align 2              \n\t"
		"   mova    1f,   r0      \n\t" /* r0 = end point */
                "   mov    r15,   r1      \n\t" /* r1 = saved sp */
		"   mov   #-14,   r15     \n\t" /* LOGIN */
		"   mov.l  @%2,   %0      \n\t" /* load old value */
                "   mov     %0,   %1      \n\t" /* %1 = *a */
                "   tst     %1,   %3      \n\t" /* T = ((*a & mask) == 0) */
		"   mov    #-1,   %1      \n\t" /* retvat = -1 */
                "   negc    %1,   %1      \n\t" /* retval = (mask & *a) != 0 */
                "   and     %4,   %0      \n\t"
                "   mov.l   %0,  @%2      \n\t" /* store new value */
		"1: mov     r1,   r15     \n\t" /* LOGOUT */
		: "=&r" (tmp),
		  "=&r" (retval),
		  "+r"  (a)
		: "r"   (mask),
		  "r"   (not_mask)
		: "memory" , "r0", "r1", "t");

        return retval;
}

static inline int test_and_change_bit(int nr, volatile void * addr)
{
        int     mask, retval;
        volatile unsigned int *a = addr;
        unsigned long tmp;

        a += nr >> 5;
        mask = 1 << (nr & 0x1f);

        __asm__ __volatile__ (
                "   .align 2              \n\t"
                "   mova    1f,   r0      \n\t" /* r0 = end point */
                "   mov    r15,   r1      \n\t" /* r1 = saved sp */
                "   mov   #-14,   r15     \n\t" /* LOGIN */
                "   mov.l  @%2,   %0      \n\t" /* load old value */
                "   mov     %0,   %1      \n\t" /* %1 = *a */
                "   tst     %1,   %3      \n\t" /* T = ((*a & mask) == 0) */
                "   mov    #-1,   %1      \n\t" /* retvat = -1 */
                "   negc    %1,   %1      \n\t" /* retval = (mask & *a) != 0 */
                "   xor     %3,   %0      \n\t"
                "   mov.l   %0,  @%2      \n\t" /* store new value */
                "1: mov     r1,   r15     \n\t" /* LOGOUT */
                : "=&r" (tmp),
                  "=&r" (retval),
                  "+r"  (a)
                : "r"   (mask)
                : "memory" , "r0", "r1", "t");

        return retval;
}

#include <asm-generic/bitops/non-atomic.h>

#endif /* __ASM_SH_BITOPS_GRB_H */
