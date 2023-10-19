/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__ALPHA_COMPILER_H
#define _UAPI__ALPHA_COMPILER_H

/* 
 * Herein are macros we use when describing various patterns we want to GCC.
 * In all cases we can get better schedules out of the compiler if we hide
 * as little as possible inside inline assembly.  However, we want to be
 * able to know what we'll get out before giving up inline assembly.  Thus
 * these tests and macros.
 */

#if __GNUC__ == 3 && __GNUC_MINOR__ >= 4 || __GNUC__ > 3
# define __kernel_insbl(val, shift)	__builtin_alpha_insbl(val, shift)
# define __kernel_inswl(val, shift)	__builtin_alpha_inswl(val, shift)
# define __kernel_insql(val, shift)	__builtin_alpha_insql(val, shift)
# define __kernel_inslh(val, shift)	__builtin_alpha_inslh(val, shift)
# define __kernel_extbl(val, shift)	__builtin_alpha_extbl(val, shift)
# define __kernel_extwl(val, shift)	__builtin_alpha_extwl(val, shift)
# define __kernel_cmpbge(a, b)		__builtin_alpha_cmpbge(a, b)
#else
# define __kernel_insbl(val, shift)					\
  ({ unsigned long __kir;						\
     __asm__("insbl %2,%1,%0" : "=r"(__kir) : "rI"(shift), "r"(val));	\
     __kir; })
# define __kernel_inswl(val, shift)					\
  ({ unsigned long __kir;						\
     __asm__("inswl %2,%1,%0" : "=r"(__kir) : "rI"(shift), "r"(val));	\
     __kir; })
# define __kernel_insql(val, shift)					\
  ({ unsigned long __kir;						\
     __asm__("insql %2,%1,%0" : "=r"(__kir) : "rI"(shift), "r"(val));	\
     __kir; })
# define __kernel_inslh(val, shift)					\
  ({ unsigned long __kir;						\
     __asm__("inslh %2,%1,%0" : "=r"(__kir) : "rI"(shift), "r"(val));	\
     __kir; })
# define __kernel_extbl(val, shift)					\
  ({ unsigned long __kir;						\
     __asm__("extbl %2,%1,%0" : "=r"(__kir) : "rI"(shift), "r"(val));	\
     __kir; })
# define __kernel_extwl(val, shift)					\
  ({ unsigned long __kir;						\
     __asm__("extwl %2,%1,%0" : "=r"(__kir) : "rI"(shift), "r"(val));	\
     __kir; })
# define __kernel_cmpbge(a, b)						\
  ({ unsigned long __kir;						\
     __asm__("cmpbge %r2,%1,%0" : "=r"(__kir) : "rI"(b), "rJ"(a));	\
     __kir; })
#endif

#ifdef __alpha_cix__
# if __GNUC__ == 3 && __GNUC_MINOR__ >= 4 || __GNUC__ > 3
#  define __kernel_cttz(x)		__builtin_ctzl(x)
#  define __kernel_ctlz(x)		__builtin_clzl(x)
#  define __kernel_ctpop(x)		__builtin_popcountl(x)
# else
#  define __kernel_cttz(x)						\
   ({ unsigned long __kir;						\
      __asm__("cttz %1,%0" : "=r"(__kir) : "r"(x));			\
      __kir; })
#  define __kernel_ctlz(x)						\
   ({ unsigned long __kir;						\
      __asm__("ctlz %1,%0" : "=r"(__kir) : "r"(x));			\
      __kir; })
#  define __kernel_ctpop(x)						\
   ({ unsigned long __kir;						\
      __asm__("ctpop %1,%0" : "=r"(__kir) : "r"(x));			\
      __kir; })
# endif
#else
# define __kernel_cttz(x)						\
  ({ unsigned long __kir;						\
     __asm__(".arch ev67; cttz %1,%0" : "=r"(__kir) : "r"(x));		\
     __kir; })
# define __kernel_ctlz(x)						\
  ({ unsigned long __kir;						\
     __asm__(".arch ev67; ctlz %1,%0" : "=r"(__kir) : "r"(x));		\
     __kir; })
# define __kernel_ctpop(x)						\
  ({ unsigned long __kir;						\
     __asm__(".arch ev67; ctpop %1,%0" : "=r"(__kir) : "r"(x));		\
     __kir; })
#endif


/* 
 * Beginning with EGCS 1.1, GCC defines __alpha_bwx__ when the BWX 
 * extension is enabled.  Previous versions did not define anything
 * we could test during compilation -- too bad, so sad.
 */

#if defined(__alpha_bwx__)
#define __kernel_ldbu(mem)	(mem)
#define __kernel_ldwu(mem)	(mem)
#define __kernel_stb(val,mem)	((mem) = (val))
#define __kernel_stw(val,mem)	((mem) = (val))
#else
#define __kernel_ldbu(mem)				\
  ({ unsigned char __kir;				\
     __asm__(".arch ev56;				\
	      ldbu %0,%1" : "=r"(__kir) : "m"(mem));	\
     __kir; })
#define __kernel_ldwu(mem)				\
  ({ unsigned short __kir;				\
     __asm__(".arch ev56;				\
	      ldwu %0,%1" : "=r"(__kir) : "m"(mem));	\
     __kir; })
#define __kernel_stb(val,mem)				\
  __asm__(".arch ev56;					\
	   stb %1,%0" : "=m"(mem) : "r"(val))
#define __kernel_stw(val,mem)				\
  __asm__(".arch ev56;					\
	   stw %1,%0" : "=m"(mem) : "r"(val))
#endif


#endif /* _UAPI__ALPHA_COMPILER_H */
