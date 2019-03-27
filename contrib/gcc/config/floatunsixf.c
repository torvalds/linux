/* Public domain.  */
typedef int SItype __attribute__ ((mode (SI)));
typedef unsigned int USItype __attribute__ ((mode (SI)));
typedef float XFtype __attribute__ ((mode (XF)));

XFtype
__floatunsixf (USItype u)
{
  SItype s = (SItype) u;
  XFtype r = (XFtype) s;
  if (s < 0)
    r += (XFtype)2.0 * (XFtype) ((USItype) 1
				 << (sizeof (USItype) * __CHAR_BIT__ - 1));
  return r;
}
