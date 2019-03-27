/*
 * Copyright 2002-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "../bn_lcl.h"
#if !(defined(__GNUC__) && __GNUC__>=2)
# include "../bn_asm.c"         /* kind of dirty hack for Sun Studio */
#else
/*-
 * x86_64 BIGNUM accelerator version 0.1, December 2002.
 *
 * Implemented by Andy Polyakov <appro@openssl.org> for the OpenSSL
 * project.
 *
 * Rights for redistribution and usage in source and binary forms are
 * granted according to the OpenSSL license. Warranty of any kind is
 * disclaimed.
 *
 * Q. Version 0.1? It doesn't sound like Andy, he used to assign real
 *    versions, like 1.0...
 * A. Well, that's because this code is basically a quick-n-dirty
 *    proof-of-concept hack. As you can see it's implemented with
 *    inline assembler, which means that you're bound to GCC and that
 *    there might be enough room for further improvement.
 *
 * Q. Why inline assembler?
 * A. x86_64 features own ABI which I'm not familiar with. This is
 *    why I decided to let the compiler take care of subroutine
 *    prologue/epilogue as well as register allocation. For reference.
 *    Win64 implements different ABI for AMD64, different from Linux.
 *
 * Q. How much faster does it get?
 * A. 'apps/openssl speed rsa dsa' output with no-asm:
 *
 *                        sign    verify    sign/s verify/s
 *      rsa  512 bits   0.0006s   0.0001s   1683.8  18456.2
 *      rsa 1024 bits   0.0028s   0.0002s    356.0   6407.0
 *      rsa 2048 bits   0.0172s   0.0005s     58.0   1957.8
 *      rsa 4096 bits   0.1155s   0.0018s      8.7    555.6
 *                        sign    verify    sign/s verify/s
 *      dsa  512 bits   0.0005s   0.0006s   2100.8   1768.3
 *      dsa 1024 bits   0.0014s   0.0018s    692.3    559.2
 *      dsa 2048 bits   0.0049s   0.0061s    204.7    165.0
 *
 *    'apps/openssl speed rsa dsa' output with this module:
 *
 *                        sign    verify    sign/s verify/s
 *      rsa  512 bits   0.0004s   0.0000s   2767.1  33297.9
 *      rsa 1024 bits   0.0012s   0.0001s    867.4  14674.7
 *      rsa 2048 bits   0.0061s   0.0002s    164.0   5270.0
 *      rsa 4096 bits   0.0384s   0.0006s     26.1   1650.8
 *                        sign    verify    sign/s verify/s
 *      dsa  512 bits   0.0002s   0.0003s   4442.2   3786.3
 *      dsa 1024 bits   0.0005s   0.0007s   1835.1   1497.4
 *      dsa 2048 bits   0.0016s   0.0020s    620.4    504.6
 *
 *    For the reference. IA-32 assembler implementation performs
 *    very much like 64-bit code compiled with no-asm on the same
 *    machine.
 */

# undef mul
# undef mul_add

/*-
 * "m"(a), "+m"(r)      is the way to favor DirectPath µ-code;
 * "g"(0)               let the compiler to decide where does it
 *                      want to keep the value of zero;
 */
# define mul_add(r,a,word,carry) do {   \
        register BN_ULONG high,low;     \
        asm ("mulq %3"                  \
                : "=a"(low),"=d"(high)  \
                : "a"(word),"m"(a)      \
                : "cc");                \
        asm ("addq %2,%0; adcq %3,%1"   \
                : "+r"(carry),"+d"(high)\
                : "a"(low),"g"(0)       \
                : "cc");                \
        asm ("addq %2,%0; adcq %3,%1"   \
                : "+m"(r),"+d"(high)    \
                : "r"(carry),"g"(0)     \
                : "cc");                \
        carry=high;                     \
        } while (0)

# define mul(r,a,word,carry) do {       \
        register BN_ULONG high,low;     \
        asm ("mulq %3"                  \
                : "=a"(low),"=d"(high)  \
                : "a"(word),"g"(a)      \
                : "cc");                \
        asm ("addq %2,%0; adcq %3,%1"   \
                : "+r"(carry),"+d"(high)\
                : "a"(low),"g"(0)       \
                : "cc");                \
        (r)=carry, carry=high;          \
        } while (0)
# undef sqr
# define sqr(r0,r1,a)                   \
        asm ("mulq %2"                  \
                : "=a"(r0),"=d"(r1)     \
                : "a"(a)                \
                : "cc");

BN_ULONG bn_mul_add_words(BN_ULONG *rp, const BN_ULONG *ap, int num,
                          BN_ULONG w)
{
    BN_ULONG c1 = 0;

    if (num <= 0)
        return c1;

    while (num & ~3) {
        mul_add(rp[0], ap[0], w, c1);
        mul_add(rp[1], ap[1], w, c1);
        mul_add(rp[2], ap[2], w, c1);
        mul_add(rp[3], ap[3], w, c1);
        ap += 4;
        rp += 4;
        num -= 4;
    }
    if (num) {
        mul_add(rp[0], ap[0], w, c1);
        if (--num == 0)
            return c1;
        mul_add(rp[1], ap[1], w, c1);
        if (--num == 0)
            return c1;
        mul_add(rp[2], ap[2], w, c1);
        return c1;
    }

    return c1;
}

BN_ULONG bn_mul_words(BN_ULONG *rp, const BN_ULONG *ap, int num, BN_ULONG w)
{
    BN_ULONG c1 = 0;

    if (num <= 0)
        return c1;

    while (num & ~3) {
        mul(rp[0], ap[0], w, c1);
        mul(rp[1], ap[1], w, c1);
        mul(rp[2], ap[2], w, c1);
        mul(rp[3], ap[3], w, c1);
        ap += 4;
        rp += 4;
        num -= 4;
    }
    if (num) {
        mul(rp[0], ap[0], w, c1);
        if (--num == 0)
            return c1;
        mul(rp[1], ap[1], w, c1);
        if (--num == 0)
            return c1;
        mul(rp[2], ap[2], w, c1);
    }
    return c1;
}

void bn_sqr_words(BN_ULONG *r, const BN_ULONG *a, int n)
{
    if (n <= 0)
        return;

    while (n & ~3) {
        sqr(r[0], r[1], a[0]);
        sqr(r[2], r[3], a[1]);
        sqr(r[4], r[5], a[2]);
        sqr(r[6], r[7], a[3]);
        a += 4;
        r += 8;
        n -= 4;
    }
    if (n) {
        sqr(r[0], r[1], a[0]);
        if (--n == 0)
            return;
        sqr(r[2], r[3], a[1]);
        if (--n == 0)
            return;
        sqr(r[4], r[5], a[2]);
    }
}

BN_ULONG bn_div_words(BN_ULONG h, BN_ULONG l, BN_ULONG d)
{
    BN_ULONG ret, waste;

 asm("divq      %4":"=a"(ret), "=d"(waste)
 :     "a"(l), "d"(h), "r"(d)
 :     "cc");

    return ret;
}

BN_ULONG bn_add_words(BN_ULONG *rp, const BN_ULONG *ap, const BN_ULONG *bp,
                      int n)
{
    BN_ULONG ret;
    size_t i = 0;

    if (n <= 0)
        return 0;

    asm volatile ("       subq    %0,%0           \n" /* clear carry */
                  "       jmp     1f              \n"
                  ".p2align 4                     \n"
                  "1:     movq    (%4,%2,8),%0    \n"
                  "       adcq    (%5,%2,8),%0    \n"
                  "       movq    %0,(%3,%2,8)    \n"
                  "       lea     1(%2),%2        \n"
                  "       dec     %1              \n"
                  "       jnz     1b              \n"
                  "       sbbq    %0,%0           \n"
                  :"=&r" (ret), "+c"(n), "+r"(i)
                  :"r"(rp), "r"(ap), "r"(bp)
                  :"cc", "memory");

    return ret & 1;
}

# ifndef SIMICS
BN_ULONG bn_sub_words(BN_ULONG *rp, const BN_ULONG *ap, const BN_ULONG *bp,
                      int n)
{
    BN_ULONG ret;
    size_t i = 0;

    if (n <= 0)
        return 0;

    asm volatile ("       subq    %0,%0           \n" /* clear borrow */
                  "       jmp     1f              \n"
                  ".p2align 4                     \n"
                  "1:     movq    (%4,%2,8),%0    \n"
                  "       sbbq    (%5,%2,8),%0    \n"
                  "       movq    %0,(%3,%2,8)    \n"
                  "       lea     1(%2),%2        \n"
                  "       dec     %1              \n"
                  "       jnz     1b              \n"
                  "       sbbq    %0,%0           \n"
                  :"=&r" (ret), "+c"(n), "+r"(i)
                  :"r"(rp), "r"(ap), "r"(bp)
                  :"cc", "memory");

    return ret & 1;
}
# else
/* Simics 1.4<7 has buggy sbbq:-( */
#  define BN_MASK2 0xffffffffffffffffL
BN_ULONG bn_sub_words(BN_ULONG *r, BN_ULONG *a, BN_ULONG *b, int n)
{
    BN_ULONG t1, t2;
    int c = 0;

    if (n <= 0)
        return (BN_ULONG)0;

    for (;;) {
        t1 = a[0];
        t2 = b[0];
        r[0] = (t1 - t2 - c) & BN_MASK2;
        if (t1 != t2)
            c = (t1 < t2);
        if (--n <= 0)
            break;

        t1 = a[1];
        t2 = b[1];
        r[1] = (t1 - t2 - c) & BN_MASK2;
        if (t1 != t2)
            c = (t1 < t2);
        if (--n <= 0)
            break;

        t1 = a[2];
        t2 = b[2];
        r[2] = (t1 - t2 - c) & BN_MASK2;
        if (t1 != t2)
            c = (t1 < t2);
        if (--n <= 0)
            break;

        t1 = a[3];
        t2 = b[3];
        r[3] = (t1 - t2 - c) & BN_MASK2;
        if (t1 != t2)
            c = (t1 < t2);
        if (--n <= 0)
            break;

        a += 4;
        b += 4;
        r += 4;
    }
    return c;
}
# endif

/* mul_add_c(a,b,c0,c1,c2)  -- c+=a*b for three word number c=(c2,c1,c0) */
/* mul_add_c2(a,b,c0,c1,c2) -- c+=2*a*b for three word number c=(c2,c1,c0) */
/* sqr_add_c(a,i,c0,c1,c2)  -- c+=a[i]^2 for three word number c=(c2,c1,c0) */
/*
 * sqr_add_c2(a,i,c0,c1,c2) -- c+=2*a[i]*a[j] for three word number
 * c=(c2,c1,c0)
 */

/*
 * Keep in mind that carrying into high part of multiplication result
 * can not overflow, because it cannot be all-ones.
 */
# if 0
/* original macros are kept for reference purposes */
#  define mul_add_c(a,b,c0,c1,c2)       do {    \
        BN_ULONG ta = (a), tb = (b);            \
        BN_ULONG lo, hi;                        \
        BN_UMULT_LOHI(lo,hi,ta,tb);             \
        c0 += lo; hi += (c0<lo)?1:0;            \
        c1 += hi; c2 += (c1<hi)?1:0;            \
        } while(0)

#  define mul_add_c2(a,b,c0,c1,c2)      do {    \
        BN_ULONG ta = (a), tb = (b);            \
        BN_ULONG lo, hi, tt;                    \
        BN_UMULT_LOHI(lo,hi,ta,tb);             \
        c0 += lo; tt = hi+((c0<lo)?1:0);        \
        c1 += tt; c2 += (c1<tt)?1:0;            \
        c0 += lo; hi += (c0<lo)?1:0;            \
        c1 += hi; c2 += (c1<hi)?1:0;            \
        } while(0)

#  define sqr_add_c(a,i,c0,c1,c2)       do {    \
        BN_ULONG ta = (a)[i];                   \
        BN_ULONG lo, hi;                        \
        BN_UMULT_LOHI(lo,hi,ta,ta);             \
        c0 += lo; hi += (c0<lo)?1:0;            \
        c1 += hi; c2 += (c1<hi)?1:0;            \
        } while(0)
# else
#  define mul_add_c(a,b,c0,c1,c2) do {  \
        BN_ULONG t1,t2;                 \
        asm ("mulq %3"                  \
                : "=a"(t1),"=d"(t2)     \
                : "a"(a),"m"(b)         \
                : "cc");                \
        asm ("addq %3,%0; adcq %4,%1; adcq %5,%2"       \
                : "+r"(c0),"+r"(c1),"+r"(c2)            \
                : "r"(t1),"r"(t2),"g"(0)                \
                : "cc");                                \
        } while (0)

#  define sqr_add_c(a,i,c0,c1,c2) do {  \
        BN_ULONG t1,t2;                 \
        asm ("mulq %2"                  \
                : "=a"(t1),"=d"(t2)     \
                : "a"(a[i])             \
                : "cc");                \
        asm ("addq %3,%0; adcq %4,%1; adcq %5,%2"       \
                : "+r"(c0),"+r"(c1),"+r"(c2)            \
                : "r"(t1),"r"(t2),"g"(0)                \
                : "cc");                                \
        } while (0)

#  define mul_add_c2(a,b,c0,c1,c2) do { \
        BN_ULONG t1,t2;                 \
        asm ("mulq %3"                  \
                : "=a"(t1),"=d"(t2)     \
                : "a"(a),"m"(b)         \
                : "cc");                \
        asm ("addq %3,%0; adcq %4,%1; adcq %5,%2"       \
                : "+r"(c0),"+r"(c1),"+r"(c2)            \
                : "r"(t1),"r"(t2),"g"(0)                \
                : "cc");                                \
        asm ("addq %3,%0; adcq %4,%1; adcq %5,%2"       \
                : "+r"(c0),"+r"(c1),"+r"(c2)            \
                : "r"(t1),"r"(t2),"g"(0)                \
                : "cc");                                \
        } while (0)
# endif

# define sqr_add_c2(a,i,j,c0,c1,c2)      \
        mul_add_c2((a)[i],(a)[j],c0,c1,c2)

void bn_mul_comba8(BN_ULONG *r, BN_ULONG *a, BN_ULONG *b)
{
    BN_ULONG c1, c2, c3;

    c1 = 0;
    c2 = 0;
    c3 = 0;
    mul_add_c(a[0], b[0], c1, c2, c3);
    r[0] = c1;
    c1 = 0;
    mul_add_c(a[0], b[1], c2, c3, c1);
    mul_add_c(a[1], b[0], c2, c3, c1);
    r[1] = c2;
    c2 = 0;
    mul_add_c(a[2], b[0], c3, c1, c2);
    mul_add_c(a[1], b[1], c3, c1, c2);
    mul_add_c(a[0], b[2], c3, c1, c2);
    r[2] = c3;
    c3 = 0;
    mul_add_c(a[0], b[3], c1, c2, c3);
    mul_add_c(a[1], b[2], c1, c2, c3);
    mul_add_c(a[2], b[1], c1, c2, c3);
    mul_add_c(a[3], b[0], c1, c2, c3);
    r[3] = c1;
    c1 = 0;
    mul_add_c(a[4], b[0], c2, c3, c1);
    mul_add_c(a[3], b[1], c2, c3, c1);
    mul_add_c(a[2], b[2], c2, c3, c1);
    mul_add_c(a[1], b[3], c2, c3, c1);
    mul_add_c(a[0], b[4], c2, c3, c1);
    r[4] = c2;
    c2 = 0;
    mul_add_c(a[0], b[5], c3, c1, c2);
    mul_add_c(a[1], b[4], c3, c1, c2);
    mul_add_c(a[2], b[3], c3, c1, c2);
    mul_add_c(a[3], b[2], c3, c1, c2);
    mul_add_c(a[4], b[1], c3, c1, c2);
    mul_add_c(a[5], b[0], c3, c1, c2);
    r[5] = c3;
    c3 = 0;
    mul_add_c(a[6], b[0], c1, c2, c3);
    mul_add_c(a[5], b[1], c1, c2, c3);
    mul_add_c(a[4], b[2], c1, c2, c3);
    mul_add_c(a[3], b[3], c1, c2, c3);
    mul_add_c(a[2], b[4], c1, c2, c3);
    mul_add_c(a[1], b[5], c1, c2, c3);
    mul_add_c(a[0], b[6], c1, c2, c3);
    r[6] = c1;
    c1 = 0;
    mul_add_c(a[0], b[7], c2, c3, c1);
    mul_add_c(a[1], b[6], c2, c3, c1);
    mul_add_c(a[2], b[5], c2, c3, c1);
    mul_add_c(a[3], b[4], c2, c3, c1);
    mul_add_c(a[4], b[3], c2, c3, c1);
    mul_add_c(a[5], b[2], c2, c3, c1);
    mul_add_c(a[6], b[1], c2, c3, c1);
    mul_add_c(a[7], b[0], c2, c3, c1);
    r[7] = c2;
    c2 = 0;
    mul_add_c(a[7], b[1], c3, c1, c2);
    mul_add_c(a[6], b[2], c3, c1, c2);
    mul_add_c(a[5], b[3], c3, c1, c2);
    mul_add_c(a[4], b[4], c3, c1, c2);
    mul_add_c(a[3], b[5], c3, c1, c2);
    mul_add_c(a[2], b[6], c3, c1, c2);
    mul_add_c(a[1], b[7], c3, c1, c2);
    r[8] = c3;
    c3 = 0;
    mul_add_c(a[2], b[7], c1, c2, c3);
    mul_add_c(a[3], b[6], c1, c2, c3);
    mul_add_c(a[4], b[5], c1, c2, c3);
    mul_add_c(a[5], b[4], c1, c2, c3);
    mul_add_c(a[6], b[3], c1, c2, c3);
    mul_add_c(a[7], b[2], c1, c2, c3);
    r[9] = c1;
    c1 = 0;
    mul_add_c(a[7], b[3], c2, c3, c1);
    mul_add_c(a[6], b[4], c2, c3, c1);
    mul_add_c(a[5], b[5], c2, c3, c1);
    mul_add_c(a[4], b[6], c2, c3, c1);
    mul_add_c(a[3], b[7], c2, c3, c1);
    r[10] = c2;
    c2 = 0;
    mul_add_c(a[4], b[7], c3, c1, c2);
    mul_add_c(a[5], b[6], c3, c1, c2);
    mul_add_c(a[6], b[5], c3, c1, c2);
    mul_add_c(a[7], b[4], c3, c1, c2);
    r[11] = c3;
    c3 = 0;
    mul_add_c(a[7], b[5], c1, c2, c3);
    mul_add_c(a[6], b[6], c1, c2, c3);
    mul_add_c(a[5], b[7], c1, c2, c3);
    r[12] = c1;
    c1 = 0;
    mul_add_c(a[6], b[7], c2, c3, c1);
    mul_add_c(a[7], b[6], c2, c3, c1);
    r[13] = c2;
    c2 = 0;
    mul_add_c(a[7], b[7], c3, c1, c2);
    r[14] = c3;
    r[15] = c1;
}

void bn_mul_comba4(BN_ULONG *r, BN_ULONG *a, BN_ULONG *b)
{
    BN_ULONG c1, c2, c3;

    c1 = 0;
    c2 = 0;
    c3 = 0;
    mul_add_c(a[0], b[0], c1, c2, c3);
    r[0] = c1;
    c1 = 0;
    mul_add_c(a[0], b[1], c2, c3, c1);
    mul_add_c(a[1], b[0], c2, c3, c1);
    r[1] = c2;
    c2 = 0;
    mul_add_c(a[2], b[0], c3, c1, c2);
    mul_add_c(a[1], b[1], c3, c1, c2);
    mul_add_c(a[0], b[2], c3, c1, c2);
    r[2] = c3;
    c3 = 0;
    mul_add_c(a[0], b[3], c1, c2, c3);
    mul_add_c(a[1], b[2], c1, c2, c3);
    mul_add_c(a[2], b[1], c1, c2, c3);
    mul_add_c(a[3], b[0], c1, c2, c3);
    r[3] = c1;
    c1 = 0;
    mul_add_c(a[3], b[1], c2, c3, c1);
    mul_add_c(a[2], b[2], c2, c3, c1);
    mul_add_c(a[1], b[3], c2, c3, c1);
    r[4] = c2;
    c2 = 0;
    mul_add_c(a[2], b[3], c3, c1, c2);
    mul_add_c(a[3], b[2], c3, c1, c2);
    r[5] = c3;
    c3 = 0;
    mul_add_c(a[3], b[3], c1, c2, c3);
    r[6] = c1;
    r[7] = c2;
}

void bn_sqr_comba8(BN_ULONG *r, const BN_ULONG *a)
{
    BN_ULONG c1, c2, c3;

    c1 = 0;
    c2 = 0;
    c3 = 0;
    sqr_add_c(a, 0, c1, c2, c3);
    r[0] = c1;
    c1 = 0;
    sqr_add_c2(a, 1, 0, c2, c3, c1);
    r[1] = c2;
    c2 = 0;
    sqr_add_c(a, 1, c3, c1, c2);
    sqr_add_c2(a, 2, 0, c3, c1, c2);
    r[2] = c3;
    c3 = 0;
    sqr_add_c2(a, 3, 0, c1, c2, c3);
    sqr_add_c2(a, 2, 1, c1, c2, c3);
    r[3] = c1;
    c1 = 0;
    sqr_add_c(a, 2, c2, c3, c1);
    sqr_add_c2(a, 3, 1, c2, c3, c1);
    sqr_add_c2(a, 4, 0, c2, c3, c1);
    r[4] = c2;
    c2 = 0;
    sqr_add_c2(a, 5, 0, c3, c1, c2);
    sqr_add_c2(a, 4, 1, c3, c1, c2);
    sqr_add_c2(a, 3, 2, c3, c1, c2);
    r[5] = c3;
    c3 = 0;
    sqr_add_c(a, 3, c1, c2, c3);
    sqr_add_c2(a, 4, 2, c1, c2, c3);
    sqr_add_c2(a, 5, 1, c1, c2, c3);
    sqr_add_c2(a, 6, 0, c1, c2, c3);
    r[6] = c1;
    c1 = 0;
    sqr_add_c2(a, 7, 0, c2, c3, c1);
    sqr_add_c2(a, 6, 1, c2, c3, c1);
    sqr_add_c2(a, 5, 2, c2, c3, c1);
    sqr_add_c2(a, 4, 3, c2, c3, c1);
    r[7] = c2;
    c2 = 0;
    sqr_add_c(a, 4, c3, c1, c2);
    sqr_add_c2(a, 5, 3, c3, c1, c2);
    sqr_add_c2(a, 6, 2, c3, c1, c2);
    sqr_add_c2(a, 7, 1, c3, c1, c2);
    r[8] = c3;
    c3 = 0;
    sqr_add_c2(a, 7, 2, c1, c2, c3);
    sqr_add_c2(a, 6, 3, c1, c2, c3);
    sqr_add_c2(a, 5, 4, c1, c2, c3);
    r[9] = c1;
    c1 = 0;
    sqr_add_c(a, 5, c2, c3, c1);
    sqr_add_c2(a, 6, 4, c2, c3, c1);
    sqr_add_c2(a, 7, 3, c2, c3, c1);
    r[10] = c2;
    c2 = 0;
    sqr_add_c2(a, 7, 4, c3, c1, c2);
    sqr_add_c2(a, 6, 5, c3, c1, c2);
    r[11] = c3;
    c3 = 0;
    sqr_add_c(a, 6, c1, c2, c3);
    sqr_add_c2(a, 7, 5, c1, c2, c3);
    r[12] = c1;
    c1 = 0;
    sqr_add_c2(a, 7, 6, c2, c3, c1);
    r[13] = c2;
    c2 = 0;
    sqr_add_c(a, 7, c3, c1, c2);
    r[14] = c3;
    r[15] = c1;
}

void bn_sqr_comba4(BN_ULONG *r, const BN_ULONG *a)
{
    BN_ULONG c1, c2, c3;

    c1 = 0;
    c2 = 0;
    c3 = 0;
    sqr_add_c(a, 0, c1, c2, c3);
    r[0] = c1;
    c1 = 0;
    sqr_add_c2(a, 1, 0, c2, c3, c1);
    r[1] = c2;
    c2 = 0;
    sqr_add_c(a, 1, c3, c1, c2);
    sqr_add_c2(a, 2, 0, c3, c1, c2);
    r[2] = c3;
    c3 = 0;
    sqr_add_c2(a, 3, 0, c1, c2, c3);
    sqr_add_c2(a, 2, 1, c1, c2, c3);
    r[3] = c1;
    c1 = 0;
    sqr_add_c(a, 2, c2, c3, c1);
    sqr_add_c2(a, 3, 1, c2, c3, c1);
    r[4] = c2;
    c2 = 0;
    sqr_add_c2(a, 3, 2, c3, c1, c2);
    r[5] = c3;
    c3 = 0;
    sqr_add_c(a, 3, c1, c2, c3);
    r[6] = c1;
    r[7] = c2;
}
#endif
