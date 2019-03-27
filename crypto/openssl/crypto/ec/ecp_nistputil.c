/*
 * Copyright 2011-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* Copyright 2011 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 *
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <openssl/opensslconf.h>
#ifdef OPENSSL_NO_EC_NISTP_64_GCC_128
NON_EMPTY_TRANSLATION_UNIT
#else

/*
 * Common utility functions for ecp_nistp224.c, ecp_nistp256.c, ecp_nistp521.c.
 */

# include <stddef.h>
# include "ec_lcl.h"

/*
 * Convert an array of points into affine coordinates. (If the point at
 * infinity is found (Z = 0), it remains unchanged.) This function is
 * essentially an equivalent to EC_POINTs_make_affine(), but works with the
 * internal representation of points as used by ecp_nistp###.c rather than
 * with (BIGNUM-based) EC_POINT data structures. point_array is the
 * input/output buffer ('num' points in projective form, i.e. three
 * coordinates each), based on an internal representation of field elements
 * of size 'felem_size'. tmp_felems needs to point to a temporary array of
 * 'num'+1 field elements for storage of intermediate values.
 */
void ec_GFp_nistp_points_make_affine_internal(size_t num, void *point_array,
                                              size_t felem_size,
                                              void *tmp_felems,
                                              void (*felem_one) (void *out),
                                              int (*felem_is_zero) (const void
                                                                    *in),
                                              void (*felem_assign) (void *out,
                                                                    const void
                                                                    *in),
                                              void (*felem_square) (void *out,
                                                                    const void
                                                                    *in),
                                              void (*felem_mul) (void *out,
                                                                 const void
                                                                 *in1,
                                                                 const void
                                                                 *in2),
                                              void (*felem_inv) (void *out,
                                                                 const void
                                                                 *in),
                                              void (*felem_contract) (void
                                                                      *out,
                                                                      const
                                                                      void
                                                                      *in))
{
    int i = 0;

# define tmp_felem(I) (&((char *)tmp_felems)[(I) * felem_size])
# define X(I) (&((char *)point_array)[3*(I) * felem_size])
# define Y(I) (&((char *)point_array)[(3*(I) + 1) * felem_size])
# define Z(I) (&((char *)point_array)[(3*(I) + 2) * felem_size])

    if (!felem_is_zero(Z(0)))
        felem_assign(tmp_felem(0), Z(0));
    else
        felem_one(tmp_felem(0));
    for (i = 1; i < (int)num; i++) {
        if (!felem_is_zero(Z(i)))
            felem_mul(tmp_felem(i), tmp_felem(i - 1), Z(i));
        else
            felem_assign(tmp_felem(i), tmp_felem(i - 1));
    }
    /*
     * Now each tmp_felem(i) is the product of Z(0) .. Z(i), skipping any
     * zero-valued factors: if Z(i) = 0, we essentially pretend that Z(i) = 1
     */

    felem_inv(tmp_felem(num - 1), tmp_felem(num - 1));
    for (i = num - 1; i >= 0; i--) {
        if (i > 0)
            /*
             * tmp_felem(i-1) is the product of Z(0) .. Z(i-1), tmp_felem(i)
             * is the inverse of the product of Z(0) .. Z(i)
             */
            /* 1/Z(i) */
            felem_mul(tmp_felem(num), tmp_felem(i - 1), tmp_felem(i));
        else
            felem_assign(tmp_felem(num), tmp_felem(0)); /* 1/Z(0) */

        if (!felem_is_zero(Z(i))) {
            if (i > 0)
                /*
                 * For next iteration, replace tmp_felem(i-1) by its inverse
                 */
                felem_mul(tmp_felem(i - 1), tmp_felem(i), Z(i));

            /*
             * Convert point (X, Y, Z) into affine form (X/(Z^2), Y/(Z^3), 1)
             */
            felem_square(Z(i), tmp_felem(num)); /* 1/(Z^2) */
            felem_mul(X(i), X(i), Z(i)); /* X/(Z^2) */
            felem_mul(Z(i), Z(i), tmp_felem(num)); /* 1/(Z^3) */
            felem_mul(Y(i), Y(i), Z(i)); /* Y/(Z^3) */
            felem_contract(X(i), X(i));
            felem_contract(Y(i), Y(i));
            felem_one(Z(i));
        } else {
            if (i > 0)
                /*
                 * For next iteration, replace tmp_felem(i-1) by its inverse
                 */
                felem_assign(tmp_felem(i - 1), tmp_felem(i));
        }
    }
}

/*-
 * This function looks at 5+1 scalar bits (5 current, 1 adjacent less
 * significant bit), and recodes them into a signed digit for use in fast point
 * multiplication: the use of signed rather than unsigned digits means that
 * fewer points need to be precomputed, given that point inversion is easy
 * (a precomputed point dP makes -dP available as well).
 *
 * BACKGROUND:
 *
 * Signed digits for multiplication were introduced by Booth ("A signed binary
 * multiplication technique", Quart. Journ. Mech. and Applied Math., vol. IV,
 * pt. 2 (1951), pp. 236-240), in that case for multiplication of integers.
 * Booth's original encoding did not generally improve the density of nonzero
 * digits over the binary representation, and was merely meant to simplify the
 * handling of signed factors given in two's complement; but it has since been
 * shown to be the basis of various signed-digit representations that do have
 * further advantages, including the wNAF, using the following general approach:
 *
 * (1) Given a binary representation
 *
 *       b_k  ...  b_2  b_1  b_0,
 *
 *     of a nonnegative integer (b_k in {0, 1}), rewrite it in digits 0, 1, -1
 *     by using bit-wise subtraction as follows:
 *
 *        b_k b_(k-1)  ...  b_2  b_1  b_0
 *      -     b_k      ...  b_3  b_2  b_1  b_0
 *       -------------------------------------
 *        s_k b_(k-1)  ...  s_3  s_2  s_1  s_0
 *
 *     A left-shift followed by subtraction of the original value yields a new
 *     representation of the same value, using signed bits s_i = b_(i+1) - b_i.
 *     This representation from Booth's paper has since appeared in the
 *     literature under a variety of different names including "reversed binary
 *     form", "alternating greedy expansion", "mutual opposite form", and
 *     "sign-alternating {+-1}-representation".
 *
 *     An interesting property is that among the nonzero bits, values 1 and -1
 *     strictly alternate.
 *
 * (2) Various window schemes can be applied to the Booth representation of
 *     integers: for example, right-to-left sliding windows yield the wNAF
 *     (a signed-digit encoding independently discovered by various researchers
 *     in the 1990s), and left-to-right sliding windows yield a left-to-right
 *     equivalent of the wNAF (independently discovered by various researchers
 *     around 2004).
 *
 * To prevent leaking information through side channels in point multiplication,
 * we need to recode the given integer into a regular pattern: sliding windows
 * as in wNAFs won't do, we need their fixed-window equivalent -- which is a few
 * decades older: we'll be using the so-called "modified Booth encoding" due to
 * MacSorley ("High-speed arithmetic in binary computers", Proc. IRE, vol. 49
 * (1961), pp. 67-91), in a radix-2^5 setting.  That is, we always combine five
 * signed bits into a signed digit:
 *
 *       s_(4j + 4) s_(4j + 3) s_(4j + 2) s_(4j + 1) s_(4j)
 *
 * The sign-alternating property implies that the resulting digit values are
 * integers from -16 to 16.
 *
 * Of course, we don't actually need to compute the signed digits s_i as an
 * intermediate step (that's just a nice way to see how this scheme relates
 * to the wNAF): a direct computation obtains the recoded digit from the
 * six bits b_(4j + 4) ... b_(4j - 1).
 *
 * This function takes those five bits as an integer (0 .. 63), writing the
 * recoded digit to *sign (0 for positive, 1 for negative) and *digit (absolute
 * value, in the range 0 .. 8).  Note that this integer essentially provides the
 * input bits "shifted to the left" by one position: for example, the input to
 * compute the least significant recoded digit, given that there's no bit b_-1,
 * has to be b_4 b_3 b_2 b_1 b_0 0.
 *
 */
void ec_GFp_nistp_recode_scalar_bits(unsigned char *sign,
                                     unsigned char *digit, unsigned char in)
{
    unsigned char s, d;

    s = ~((in >> 5) - 1);       /* sets all bits to MSB(in), 'in' seen as
                                 * 6-bit value */
    d = (1 << 6) - in - 1;
    d = (d & s) | (in & ~s);
    d = (d >> 1) + (d & 1);

    *sign = s & 1;
    *digit = d;
}
#endif
