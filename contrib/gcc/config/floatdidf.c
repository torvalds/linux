/* Public domain.  */
typedef int SItype __attribute__ ((mode (SI)));
typedef unsigned int USItype __attribute__ ((mode (SI)));
typedef int DItype __attribute__ ((mode (DI)));
typedef float DFtype __attribute__ ((mode (DF)));

DFtype __floatdidf (DItype);

DFtype
__floatdidf (DItype u)
{
  /* When the word size is small, we never get any rounding error.  */
  DFtype f = (SItype) (u >> (sizeof (SItype) * 8));
  f *= 0x1p32f;
  f += (USItype) u;
  return f;
}
