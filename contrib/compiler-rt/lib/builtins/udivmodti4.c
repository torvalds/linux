/* ===-- udivmodti4.c - Implement __udivmodti4 -----------------------------===
 *
 *                    The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __udivmodti4 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */ 

#include "int_lib.h"

#ifdef CRT_HAS_128BIT

/* Effects: if rem != 0, *rem = a % b 
 * Returns: a / b 
 */

/* Translated from Figure 3-40 of The PowerPC Compiler Writer's Guide */

COMPILER_RT_ABI tu_int
__udivmodti4(tu_int a, tu_int b, tu_int* rem)
{
    const unsigned n_udword_bits = sizeof(du_int) * CHAR_BIT;
    const unsigned n_utword_bits = sizeof(tu_int) * CHAR_BIT;
    utwords n;
    n.all = a;
    utwords d;
    d.all = b;
    utwords q;
    utwords r;
    unsigned sr;
    /* special cases, X is unknown, K != 0 */
    if (n.s.high == 0)
    {
        if (d.s.high == 0)
        {
            /* 0 X
             * ---
             * 0 X
             */
            if (rem)
                *rem = n.s.low % d.s.low;
            return n.s.low / d.s.low;
        }
        /* 0 X
         * ---
         * K X
         */
        if (rem)
            *rem = n.s.low;
        return 0;
    }
    /* n.s.high != 0 */
    if (d.s.low == 0)
    {
        if (d.s.high == 0)
        {
            /* K X
             * ---
             * 0 0
             */
            if (rem)
                *rem = n.s.high % d.s.low;
            return n.s.high / d.s.low;
        }
        /* d.s.high != 0 */
        if (n.s.low == 0)
        {
            /* K 0
             * ---
             * K 0
             */
            if (rem)
            {
                r.s.high = n.s.high % d.s.high;
                r.s.low = 0;
                *rem = r.all;
            }
            return n.s.high / d.s.high;
        }
        /* K K
         * ---
         * K 0
         */
        if ((d.s.high & (d.s.high - 1)) == 0)     /* if d is a power of 2 */
        {
            if (rem)
            {
                r.s.low = n.s.low;
                r.s.high = n.s.high & (d.s.high - 1);
                *rem = r.all;
            }
            return n.s.high >> __builtin_ctzll(d.s.high);
        }
        /* K K
         * ---
         * K 0
         */
        sr = __builtin_clzll(d.s.high) - __builtin_clzll(n.s.high);
        /* 0 <= sr <= n_udword_bits - 2 or sr large */
        if (sr > n_udword_bits - 2)
        {
           if (rem)
                *rem = n.all;
            return 0;
        }
        ++sr;
        /* 1 <= sr <= n_udword_bits - 1 */
        /* q.all = n.all << (n_utword_bits - sr); */
        q.s.low = 0;
        q.s.high = n.s.low << (n_udword_bits - sr);
        /* r.all = n.all >> sr; */
        r.s.high = n.s.high >> sr;
        r.s.low = (n.s.high << (n_udword_bits - sr)) | (n.s.low >> sr);
    }
    else  /* d.s.low != 0 */
    {
        if (d.s.high == 0)
        {
            /* K X
             * ---
             * 0 K
             */
            if ((d.s.low & (d.s.low - 1)) == 0)     /* if d is a power of 2 */
            {
                if (rem)
                    *rem = n.s.low & (d.s.low - 1);
                if (d.s.low == 1)
                    return n.all;
                sr = __builtin_ctzll(d.s.low);
                q.s.high = n.s.high >> sr;
                q.s.low = (n.s.high << (n_udword_bits - sr)) | (n.s.low >> sr);
                return q.all;
            }
            /* K X
             * ---
             * 0 K
             */
            sr = 1 + n_udword_bits + __builtin_clzll(d.s.low)
                                   - __builtin_clzll(n.s.high);
            /* 2 <= sr <= n_utword_bits - 1
             * q.all = n.all << (n_utword_bits - sr);
             * r.all = n.all >> sr;
             */
            if (sr == n_udword_bits)
            {
                q.s.low = 0;
                q.s.high = n.s.low;
                r.s.high = 0;
                r.s.low = n.s.high;
            }
            else if (sr < n_udword_bits)  // 2 <= sr <= n_udword_bits - 1
            {
                q.s.low = 0;
                q.s.high = n.s.low << (n_udword_bits - sr);
                r.s.high = n.s.high >> sr;
                r.s.low = (n.s.high << (n_udword_bits - sr)) | (n.s.low >> sr);
            }
            else              // n_udword_bits + 1 <= sr <= n_utword_bits - 1
            {
                q.s.low = n.s.low << (n_utword_bits - sr);
                q.s.high = (n.s.high << (n_utword_bits - sr)) |
                           (n.s.low >> (sr - n_udword_bits));
                r.s.high = 0;
                r.s.low = n.s.high >> (sr - n_udword_bits);
            }
        }
        else
        {
            /* K X
             * ---
             * K K
             */
            sr = __builtin_clzll(d.s.high) - __builtin_clzll(n.s.high);
            /*0 <= sr <= n_udword_bits - 1 or sr large */
            if (sr > n_udword_bits - 1)
            {
               if (rem)
                    *rem = n.all;
                return 0;
            }
            ++sr;
            /* 1 <= sr <= n_udword_bits
             * q.all = n.all << (n_utword_bits - sr);
             * r.all = n.all >> sr;
             */
            q.s.low = 0;
            if (sr == n_udword_bits)
            {
                q.s.high = n.s.low;
                r.s.high = 0;
                r.s.low = n.s.high;
            }
            else
            {
                r.s.high = n.s.high >> sr;
                r.s.low = (n.s.high << (n_udword_bits - sr)) | (n.s.low >> sr);
                q.s.high = n.s.low << (n_udword_bits - sr);
            }
        }
    }
    /* Not a special case
     * q and r are initialized with:
     * q.all = n.all << (n_utword_bits - sr);
     * r.all = n.all >> sr;
     * 1 <= sr <= n_utword_bits - 1
     */
    su_int carry = 0;
    for (; sr > 0; --sr)
    {
        /* r:q = ((r:q)  << 1) | carry */
        r.s.high = (r.s.high << 1) | (r.s.low  >> (n_udword_bits - 1));
        r.s.low  = (r.s.low  << 1) | (q.s.high >> (n_udword_bits - 1));
        q.s.high = (q.s.high << 1) | (q.s.low  >> (n_udword_bits - 1));
        q.s.low  = (q.s.low  << 1) | carry;
        /* carry = 0;
         * if (r.all >= d.all)
         * {
         *     r.all -= d.all;
         *      carry = 1;
         * }
         */
        const ti_int s = (ti_int)(d.all - r.all - 1) >> (n_utword_bits - 1);
        carry = s & 1;
        r.all -= d.all & s;
    }
    q.all = (q.all << 1) | carry;
    if (rem)
        *rem = r.all;
    return q.all;
}

#endif /* CRT_HAS_128BIT */
