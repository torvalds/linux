/* More subroutines needed by GCC output code on some machines.  */
/* Compile this one with gcc.  */
/* Copyright (C) 1989, 92-98, 1999 Free Software Foundation, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.
 */
/* support functions required by the kernel. based on code from gcc-2.95.3 */
/* I Molton     29/07/01 */

#include "gcclib.h"
#include "longlong.h"

static const UQItype __clz_tab[] =
{
  0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
  6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
  8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
};

UDItype
__udivmoddi4 (UDItype n, UDItype d, UDItype *rp)
{
  DIunion ww;
  DIunion nn, dd;
  DIunion rr;
  USItype d0, d1, n0, n1, n2;
  USItype q0, q1;
  USItype b, bm;

  nn.ll = n;
  dd.ll = d;

  d0 = dd.s.low;
  d1 = dd.s.high;
  n0 = nn.s.low;
  n1 = nn.s.high;

  if (d1 == 0)
    {
      if (d0 > n1)
        {
          /* 0q = nn / 0D */

          count_leading_zeros (bm, d0);

          if (bm != 0)
            {
              /* Normalize, i.e. make the most significant bit of the
                 denominator set.  */

              d0 = d0 << bm;
              n1 = (n1 << bm) | (n0 >> (SI_TYPE_SIZE - bm));
              n0 = n0 << bm;
            }

          udiv_qrnnd (q0, n0, n1, n0, d0);
          q1 = 0;

          /* Remainder in n0 >> bm.  */
        }
      else
        {
          /* qq = NN / 0d */

          if (d0 == 0)
            d0 = 1 / d0;        /* Divide intentionally by zero.  */

          count_leading_zeros (bm, d0);

          if (bm == 0)
            {
              /* From (n1 >= d0) /\ (the most significant bit of d0 is set),
                 conclude (the most significant bit of n1 is set) /\ (the
                 leading quotient digit q1 = 1).

                 This special case is necessary, not an optimization.
                 (Shifts counts of SI_TYPE_SIZE are undefined.)  */

              n1 -= d0;
              q1 = 1;
            }
          else
            {
              /* Normalize.  */

              b = SI_TYPE_SIZE - bm;

              d0 = d0 << bm;
              n2 = n1 >> b;
              n1 = (n1 << bm) | (n0 >> b);
              n0 = n0 << bm;

              udiv_qrnnd (q1, n1, n2, n1, d0);
            }

          /* n1 != d0...  */

          udiv_qrnnd (q0, n0, n1, n0, d0);

          /* Remainder in n0 >> bm.  */
        }

      if (rp != 0)
        {
          rr.s.low = n0 >> bm;
          rr.s.high = 0;
          *rp = rr.ll;
        }
    }
  else
    {
      if (d1 > n1)
        {
          /* 00 = nn / DD */

          q0 = 0;
          q1 = 0;

          /* Remainder in n1n0.  */
          if (rp != 0)
            {
              rr.s.low = n0;
              rr.s.high = n1;
              *rp = rr.ll;
            }
        }
      else
        {
          /* 0q = NN / dd */

          count_leading_zeros (bm, d1);
          if (bm == 0)
            {
              /* From (n1 >= d1) /\ (the most significant bit of d1 is set),
                 conclude (the most significant bit of n1 is set) /\ (the
                 quotient digit q0 = 0 or 1).

                 This special case is necessary, not an optimization.  */

              /* The condition on the next line takes advantage of that
                 n1 >= d1 (true due to program flow).  */
              if (n1 > d1 || n0 >= d0)
                {
                  q0 = 1;
                  sub_ddmmss (n1, n0, n1, n0, d1, d0);
                }
              else
                q0 = 0;

              q1 = 0;

              if (rp != 0)
                {
                  rr.s.low = n0;
                  rr.s.high = n1;
                  *rp = rr.ll;
                }
            }
          else
            {
              USItype m1, m0;
              /* Normalize.  */

              b = SI_TYPE_SIZE - bm;

              d1 = (d1 << bm) | (d0 >> b);
              d0 = d0 << bm;
              n2 = n1 >> b;
              n1 = (n1 << bm) | (n0 >> b);
              n0 = n0 << bm;

              udiv_qrnnd (q0, n1, n2, n1, d1);
              umul_ppmm (m1, m0, q0, d0);

              if (m1 > n1 || (m1 == n1 && m0 > n0))
                {
                  q0--;
                  sub_ddmmss (m1, m0, m1, m0, d1, d0);
                }

              q1 = 0;

              /* Remainder in (n1n0 - m1m0) >> bm.  */
              if (rp != 0)
                {
                  sub_ddmmss (n1, n0, n1, n0, m1, m0);
                  rr.s.low = (n1 << b) | (n0 >> bm);
                  rr.s.high = n1 >> bm;
                  *rp = rr.ll;
                }
            }
        }
    }

  ww.s.low = q0;
  ww.s.high = q1;
  return ww.ll;
}

UDItype
__udivdi3 (UDItype n, UDItype d)
{
  return __udivmoddi4 (n, d, (UDItype *) 0);
}

UDItype
__umoddi3 (UDItype u, UDItype v)
{
  UDItype w;

  (void) __udivmoddi4 (u ,v, &w);

  return w;
}

