/* Public domain.  */
typedef unsigned int USItype __attribute__ ((mode (SI)));
typedef int DItype __attribute__ ((mode (DI)));
typedef unsigned int UDItype __attribute__ ((mode (DI)));
typedef float SFtype __attribute__ ((mode (SF)));
typedef float DFtype __attribute__ ((mode (DF)));

DItype __fixsfdi (SFtype);

/* This version is needed to prevent recursion; fixunssfdi in libgcc
   calls fixsfdi, which in turn calls calls fixunssfdi.  */

static DItype
local_fixunssfdi (SFtype original_a)
{
  DFtype a = original_a;
  USItype hi, lo;

  hi = a / (((UDItype) 1) << (sizeof (USItype) * 8));
  lo = a - ((DFtype) hi) * (((UDItype) 1) << (sizeof (USItype) * 8));
  return ((UDItype) hi << (sizeof (USItype) * 8)) | lo;
}

DItype
__fixsfdi (SFtype a)
{
  if (a < 0)
    return - local_fixunssfdi (-a);
  return local_fixunssfdi (a);
}
