/* Public domain.  */
typedef int DItype __attribute__ ((mode (DI)));
typedef unsigned int UDItype __attribute__ ((mode (DI)));
typedef unsigned int USItype __attribute__ ((mode (SI)));
typedef float SFtype __attribute__ ((mode (SF)));
typedef float DFtype __attribute__ ((mode (DF)));

SFtype __floatundisf (UDItype);

SFtype
__floatundisf (UDItype u)
{
  /* Protect against double-rounding error.
     Represent any low-order bits, that might be truncated by a bit that
     won't be lost.  The bit can go in anywhere below the rounding position
     of SFTYPE.  A fixed mask and bit position handles all usual
     configurations.  */
  if (53 < (sizeof (DItype) * 8)
      && 53 > ((sizeof (DItype) * 8) - 53 + 24))
    {
      if (u >= ((UDItype) 1 << 53))
	{
	  if ((UDItype) u & (((UDItype) 1 << (sizeof (DItype) * 8 - 53)) - 1))
	    {
	      u &= ~ (((UDItype) 1 << (sizeof (DItype) * 8 - 53)) - 1);
	      u |= (UDItype) 1 << (sizeof (DItype) * 8 - 53);
	    }
	}
    }
  /* Do the calculation in a wider type so that we don't lose any of
     the precision of the high word while multiplying it.  */
  DFtype f = (USItype) (u >> (sizeof (USItype) * 8));
  f *= 0x1p32f;
  f += (USItype) u;
  return (SFtype) f;
}
