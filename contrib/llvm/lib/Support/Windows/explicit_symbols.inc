/* in libgcc.a */

#ifdef HAVE__ALLOCA
  EXPLICIT_SYMBOL(_alloca)
  EXPLICIT_SYMBOL2(alloca, _alloca)
#endif
#ifdef HAVE___ALLOCA
  EXPLICIT_SYMBOL(__alloca)
#endif
#ifdef HAVE___CHKSTK
  EXPLICIT_SYMBOL(__chkstk)
#endif
#ifdef HAVE___CHKSTK_MS
  EXPLICIT_SYMBOL(__chkstk_ms)
#endif
#ifdef HAVE____CHKSTK
  EXPLICIT_SYMBOL(___chkstk)
#endif
#ifdef HAVE____CHKSTK_MS
  EXPLICIT_SYMBOL(___chkstk_ms)
#endif
#ifdef HAVE___MAIN
  EXPLICIT_SYMBOL(__main) // FIXME: Don't call it.
#endif

#ifdef HAVE___ASHLDI3
  EXPLICIT_SYMBOL(__ashldi3)
#endif
#ifdef HAVE___ASHRDI3
  EXPLICIT_SYMBOL(__ashrdi3)
#endif
#ifdef HAVE___CMPDI2 // FIXME: unused
  EXPLICIT_SYMBOL(__cmpdi2)
#endif
#ifdef HAVE___DIVDI3
  EXPLICIT_SYMBOL(__divdi3)
#endif
#ifdef HAVE___FIXDFDI
  EXPLICIT_SYMBOL(__fixdfdi)
#endif
#ifdef HAVE___FIXSFDI
  EXPLICIT_SYMBOL(__fixsfdi)
#endif
#ifdef HAVE___FIXUNSDFDI
  EXPLICIT_SYMBOL(__fixunsdfdi)
#endif
#ifdef HAVE___FIXUNSSFDI
  EXPLICIT_SYMBOL(__fixunssfdi)
#endif
#ifdef HAVE___FLOATDIDF
  EXPLICIT_SYMBOL(__floatdidf)
#endif
#ifdef HAVE___FLOATDISF
  EXPLICIT_SYMBOL(__floatdisf)
#endif
#ifdef HAVE___LSHRDI3
  EXPLICIT_SYMBOL(__lshrdi3)
#endif
#ifdef HAVE___MODDI3
  EXPLICIT_SYMBOL(__moddi3)
#endif
#ifdef HAVE___UDIVDI3
  EXPLICIT_SYMBOL(__udivdi3)
#endif
#ifdef HAVE___UMODDI3
  EXPLICIT_SYMBOL(__umoddi3)
#endif

/* msvcrt */
#if defined(_MSC_VER)
  EXPLICIT_SYMBOL2(alloca, _alloca_probe)

#ifdef _M_IX86
#define INLINE_DEF_FLOAT_SYMBOL(SYM, ARGC) INLINE_DEF_SYMBOL##ARGC(float, SYM)
  INLINE_DEF_FLOAT_SYMBOL(acosf, 1)
  INLINE_DEF_FLOAT_SYMBOL(asinf, 1)
  INLINE_DEF_FLOAT_SYMBOL(atanf, 1)
  INLINE_DEF_FLOAT_SYMBOL(atan2f, 2)
  INLINE_DEF_FLOAT_SYMBOL(ceilf, 1)
  INLINE_DEF_FLOAT_SYMBOL(cosf, 1)
  INLINE_DEF_FLOAT_SYMBOL(coshf, 1)
  INLINE_DEF_FLOAT_SYMBOL(expf, 1)
  INLINE_DEF_FLOAT_SYMBOL(floorf, 1)
  INLINE_DEF_FLOAT_SYMBOL(fmodf, 2)
  INLINE_DEF_FLOAT_SYMBOL(logf, 1)
  INLINE_DEF_FLOAT_SYMBOL(powf, 2)
  INLINE_DEF_FLOAT_SYMBOL(sinf, 1)
  INLINE_DEF_FLOAT_SYMBOL(sinhf, 1)
  INLINE_DEF_FLOAT_SYMBOL(sqrtf, 1)
  INLINE_DEF_FLOAT_SYMBOL(tanf, 1)
  INLINE_DEF_FLOAT_SYMBOL(tanhf, 1)

  // These were added in VS 2013.
#if (1800 <= _MSC_VER && _MSC_VER < 1900)
  INLINE_DEF_FLOAT_SYMBOL(copysignf, 2)
  INLINE_DEF_FLOAT_SYMBOL(fminf, 2)
  INLINE_DEF_FLOAT_SYMBOL(fmaxf, 2)
#endif
#undef INLINE_DEF_FLOAT_SYMBOL
#endif

#endif
