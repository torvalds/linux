/* Public domain.  */
typedef unsigned int USItype __attribute__ ((mode (SI)));
typedef unsigned int UDItype __attribute__ ((mode (DI)));
typedef float DFtype __attribute__ ((mode (DF)));

DFtype __floatundidf (UDItype);

DFtype
__floatundidf (UDItype u)
{
  /* When the word size is small, we never get any rounding error.  */
  DFtype f = (USItype) (u >> (sizeof (USItype) * 8));
  f *= 0x1p32f;
  f += (USItype) u;
  return f;
}
