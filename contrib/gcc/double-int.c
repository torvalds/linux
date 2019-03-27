/* Operations with long integers.
   Copyright (C) 2006 Free Software Foundation, Inc.
   
This file is part of GCC.
   
GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.
   
GCC is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.
   
You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"

/* Returns mask for PREC bits.  */

static inline double_int
double_int_mask (unsigned prec)
{
  unsigned HOST_WIDE_INT m;
  double_int mask;

  if (prec > HOST_BITS_PER_WIDE_INT)
    {
      prec -= HOST_BITS_PER_WIDE_INT;
      m = ((unsigned HOST_WIDE_INT) 2 << (prec - 1)) - 1;
      mask.high = (HOST_WIDE_INT) m;
      mask.low = ALL_ONES;
    }
  else
    {
      mask.high = 0;
      mask.low = ((unsigned HOST_WIDE_INT) 2 << (prec - 1)) - 1;
    }

  return mask;
}

/* Clears the bits of CST over the precision PREC.  If UNS is false, the bits
   outside of the precision are set to the sign bit (i.e., the PREC-th one),
   otherwise they are set to zero.
 
   This corresponds to returning the value represented by PREC lowermost bits
   of CST, with the given signedness.  */

double_int
double_int_ext (double_int cst, unsigned prec, bool uns)
{
  if (uns)
    return double_int_zext (cst, prec);
  else
    return double_int_sext (cst, prec);
}

/* The same as double_int_ext with UNS = true.  */

double_int
double_int_zext (double_int cst, unsigned prec)
{
  double_int mask = double_int_mask (prec);
  double_int r;

  r.low = cst.low & mask.low;
  r.high = cst.high & mask.high;

  return r;
}

/* The same as double_int_ext with UNS = false.  */

double_int
double_int_sext (double_int cst, unsigned prec)
{
  double_int mask = double_int_mask (prec);
  double_int r;
  unsigned HOST_WIDE_INT snum;

  if (prec <= HOST_BITS_PER_WIDE_INT)
    snum = cst.low;
  else
    {
      prec -= HOST_BITS_PER_WIDE_INT;
      snum = (unsigned HOST_WIDE_INT) cst.high;
    }
  if (((snum >> (prec - 1)) & 1) == 1)
    {
      r.low = cst.low | ~mask.low;
      r.high = cst.high | ~mask.high;
    }
  else
    {
      r.low = cst.low & mask.low;
      r.high = cst.high & mask.high;
    } 

  return r;
}

/* Constructs long integer from tree CST.  The extra bits over the precision of
   the number are filled with sign bit if CST is signed, and with zeros if it
   is unsigned.  */

double_int
tree_to_double_int (tree cst)
{
  /* We do not need to call double_int_restrict here to ensure the semantics as
     described, as this is the default one for trees.  */
  return TREE_INT_CST (cst);
}

/* Returns true if CST fits in unsigned HOST_WIDE_INT.  */

bool
double_int_fits_in_uhwi_p (double_int cst)
{
  return cst.high == 0;
}

/* Returns true if CST fits in signed HOST_WIDE_INT.  */

bool
double_int_fits_in_shwi_p (double_int cst)
{
  if (cst.high == 0)
    return (HOST_WIDE_INT) cst.low >= 0;
  else if (cst.high == -1)
    return (HOST_WIDE_INT) cst.low < 0;
  else
    return false;
}

/* Returns true if CST fits in HOST_WIDE_INT if UNS is false, or in
   unsigned HOST_WIDE_INT if UNS is true.  */

bool
double_int_fits_in_hwi_p (double_int cst, bool uns)
{
  if (uns)
    return double_int_fits_in_uhwi_p (cst);
  else
    return double_int_fits_in_shwi_p (cst);
}

/* Returns value of CST as a signed number.  CST must satisfy
   double_int_fits_in_shwi_p.  */

HOST_WIDE_INT
double_int_to_shwi (double_int cst)
{
  return (HOST_WIDE_INT) cst.low;
}

/* Returns value of CST as an unsigned number.  CST must satisfy
   double_int_fits_in_uhwi_p.  */

unsigned HOST_WIDE_INT
double_int_to_uhwi (double_int cst)
{
  return cst.low;
}

/* Returns A * B.  */

double_int
double_int_mul (double_int a, double_int b)
{
  double_int ret;
  mul_double (a.low, a.high, b.low, b.high, &ret.low, &ret.high);
  return ret;
}

/* Returns A + B.  */

double_int
double_int_add (double_int a, double_int b)
{
  double_int ret;
  add_double (a.low, a.high, b.low, b.high, &ret.low, &ret.high);
  return ret;
}

/* Returns -A.  */

double_int
double_int_neg (double_int a)
{
  double_int ret;
  neg_double (a.low, a.high, &ret.low, &ret.high);
  return ret;
}

/* Returns A / B (computed as unsigned depending on UNS, and rounded as
   specified by CODE).  CODE is enum tree_code in fact, but double_int.h
   must be included before tree.h.  The remainder after the division is
   stored to MOD.  */

double_int
double_int_divmod (double_int a, double_int b, bool uns, unsigned code,
		   double_int *mod)
{
  double_int ret;

  div_and_round_double (code, uns, a.low, a.high, b.low, b.high,
			&ret.low, &ret.high, &mod->low, &mod->high);
  return ret;
}

/* The same as double_int_divmod with UNS = false.  */

double_int
double_int_sdivmod (double_int a, double_int b, unsigned code, double_int *mod)
{
  return double_int_divmod (a, b, false, code, mod);
}

/* The same as double_int_divmod with UNS = true.  */

double_int
double_int_udivmod (double_int a, double_int b, unsigned code, double_int *mod)
{
  return double_int_divmod (a, b, true, code, mod);
}

/* Returns A / B (computed as unsigned depending on UNS, and rounded as
   specified by CODE).  CODE is enum tree_code in fact, but double_int.h
   must be included before tree.h.  */

double_int
double_int_div (double_int a, double_int b, bool uns, unsigned code)
{
  double_int mod;

  return double_int_divmod (a, b, uns, code, &mod);
}

/* The same as double_int_div with UNS = false.  */

double_int
double_int_sdiv (double_int a, double_int b, unsigned code)
{
  return double_int_div (a, b, false, code);
}

/* The same as double_int_div with UNS = true.  */

double_int
double_int_udiv (double_int a, double_int b, unsigned code)
{
  return double_int_div (a, b, true, code);
}

/* Returns A % B (computed as unsigned depending on UNS, and rounded as
   specified by CODE).  CODE is enum tree_code in fact, but double_int.h
   must be included before tree.h.  */

double_int
double_int_mod (double_int a, double_int b, bool uns, unsigned code)
{
  double_int mod;

  double_int_divmod (a, b, uns, code, &mod);
  return mod;
}

/* The same as double_int_mod with UNS = false.  */

double_int
double_int_smod (double_int a, double_int b, unsigned code)
{
  return double_int_mod (a, b, false, code);
}

/* The same as double_int_mod with UNS = true.  */

double_int
double_int_umod (double_int a, double_int b, unsigned code)
{
  return double_int_mod (a, b, true, code);
}

/* Constructs tree in type TYPE from with value given by CST.  */

tree
double_int_to_tree (tree type, double_int cst)
{
  cst = double_int_ext (cst, TYPE_PRECISION (type), TYPE_UNSIGNED (type));

  return build_int_cst_wide (type, cst.low, cst.high);
}

/* Returns true if CST is negative.  Of course, CST is considered to
   be signed.  */

bool
double_int_negative_p (double_int cst)
{
  return cst.high < 0;
}

/* Returns -1 if A < B, 0 if A == B and 1 if A > B.  Signedness of the
   comparison is given by UNS.  */

int
double_int_cmp (double_int a, double_int b, bool uns)
{
  if (uns)
    return double_int_ucmp (a, b);
  else
    return double_int_scmp (a, b);
}

/* Compares two unsigned values A and B.  Returns -1 if A < B, 0 if A == B,
   and 1 if A > B.  */

int
double_int_ucmp (double_int a, double_int b)
{
  if ((unsigned HOST_WIDE_INT) a.high < (unsigned HOST_WIDE_INT) b.high)
    return -1;
  if ((unsigned HOST_WIDE_INT) a.high > (unsigned HOST_WIDE_INT) b.high)
    return 1;
  if (a.low < b.low)
    return -1;
  if (a.low > b.low)
    return 1;

  return 0;
}

/* Compares two signed values A and B.  Returns -1 if A < B, 0 if A == B,
   and 1 if A > B.  */

int
double_int_scmp (double_int a, double_int b)
{
  if (a.high < b.high)
    return -1;
  if (a.high > b.high)
    return 1;
  if ((HOST_WIDE_INT) a.low < (HOST_WIDE_INT) b.low)
    return -1;
  if ((HOST_WIDE_INT) a.low > (HOST_WIDE_INT) b.low)
    return 1;

  return 0;
}

/* Splits last digit of *CST (taken as unsigned) in BASE and returns it.  */

static unsigned
double_int_split_digit (double_int *cst, unsigned base)
{
  unsigned HOST_WIDE_INT resl, reml;
  HOST_WIDE_INT resh, remh;

  div_and_round_double (FLOOR_DIV_EXPR, true, cst->low, cst->high, base, 0,
			&resl, &resh, &reml, &remh);
  cst->high = resh;
  cst->low = resl;

  return reml;
}

/* Dumps CST to FILE.  If UNS is true, CST is considered to be unsigned,
   otherwise it is signed.  */

void
dump_double_int (FILE *file, double_int cst, bool uns)
{
  unsigned digits[100], n;
  int i;

  if (double_int_zero_p (cst))
    {
      fprintf (file, "0");
      return;
    }

  if (!uns && double_int_negative_p (cst))
    {
      fprintf (file, "-");
      cst = double_int_neg (cst);
    }

  for (n = 0; !double_int_zero_p (cst); n++)
    digits[n] = double_int_split_digit (&cst, 10);
  for (i = n - 1; i >= 0; i--)
    fprintf (file, "%u", digits[i]);
}
