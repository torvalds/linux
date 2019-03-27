/* Public domain.  */
typedef unsigned int USItype __attribute__ ((mode (SI)));
typedef int SItype __attribute__ ((mode (SI)));
typedef float SFtype __attribute__ ((mode (SF)));
typedef float DFtype __attribute__ ((mode (DF)));

USItype __fixunsdfsi (DFtype);

#define SItype_MIN \
  (- ((SItype) (((USItype) 1 << ((sizeof (SItype) * 8) - 1)) - 1)) - 1)

USItype
__fixunsdfsi (DFtype a)
{
  if (a >= - (DFtype) SItype_MIN)
    return (SItype) (a + SItype_MIN) - SItype_MIN;
  return (SItype) a;
}
