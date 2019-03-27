sinclude(../config/acx.m4)
sinclude(../config/no-executables.m4)
sinclude(../config/warnings.m4)

dnl See whether strncmp reads past the end of its string parameters.
dnl On some versions of SunOS4 at least, strncmp reads a word at a time
dnl but erroneously reads past the end of strings.  This can cause
dnl a SEGV in some cases.
AC_DEFUN(libiberty_AC_FUNC_STRNCMP,
[AC_REQUIRE([AC_FUNC_MMAP])
AC_CACHE_CHECK([for working strncmp], ac_cv_func_strncmp_works,
[AC_TRY_RUN([
/* Test by Jim Wilson and Kaveh Ghazi.
   Check whether strncmp reads past the end of its string parameters. */
#include <sys/types.h>

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#ifndef MAP_ANON
#ifdef MAP_ANONYMOUS
#define MAP_ANON MAP_ANONYMOUS
#else
#define MAP_ANON MAP_FILE
#endif
#endif

#ifndef MAP_FILE
#define MAP_FILE 0
#endif
#ifndef O_RDONLY
#define O_RDONLY 0
#endif

#define MAP_LEN 0x10000

main ()
{
#if defined(HAVE_MMAP) || defined(HAVE_MMAP_ANYWHERE)
  char *p;
  int dev_zero;

  dev_zero = open ("/dev/zero", O_RDONLY);
  if (dev_zero < 0)
    exit (1);
  
  p = (char *) mmap (0, MAP_LEN, PROT_READ|PROT_WRITE,
		     MAP_ANON|MAP_PRIVATE, dev_zero, 0);
  if (p == (char *)-1)
    p = (char *) mmap (0, MAP_LEN, PROT_READ|PROT_WRITE,
		       MAP_ANON|MAP_PRIVATE, -1, 0);
  if (p == (char *)-1)
    exit (2);
  else
    {
      char *string = "__si_type_info";
      char *q = (char *) p + MAP_LEN - strlen (string) - 2;
      char *r = (char *) p + 0xe;

      strcpy (q, string);
      strcpy (r, string);
      strncmp (r, q, 14);
    }
#endif /* HAVE_MMAP || HAVE_MMAP_ANYWHERE */
  exit (0);
}
], ac_cv_func_strncmp_works=yes, ac_cv_func_strncmp_works=no,
  ac_cv_func_strncmp_works=no)
rm -f core core.* *.core])
if test $ac_cv_func_strncmp_works = no ; then
  AC_LIBOBJ([strncmp])
fi
])

dnl See if errno must be declared even when <errno.h> is included.
AC_DEFUN(libiberty_AC_DECLARE_ERRNO,
[AC_CACHE_CHECK(whether errno must be declared, libiberty_cv_declare_errno,
[AC_TRY_COMPILE(
[#include <errno.h>],
[int x = errno;],
libiberty_cv_declare_errno=no,
libiberty_cv_declare_errno=yes)])
if test $libiberty_cv_declare_errno = yes
then AC_DEFINE(NEED_DECLARATION_ERRNO, 1,
  [Define if errno must be declared even when <errno.h> is included.])
fi
])

dnl See whether we need a declaration for a function.
AC_DEFUN(libiberty_NEED_DECLARATION,
[AC_MSG_CHECKING([whether $1 must be declared])
AC_CACHE_VAL(libiberty_cv_decl_needed_$1,
[AC_TRY_COMPILE([
#include "confdefs.h"
#include <stdio.h>
#ifdef HAVE_STRING_H
#include <string.h>
#else
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif],
[char *(*pfn) = (char *(*)) $1],
libiberty_cv_decl_needed_$1=no, libiberty_cv_decl_needed_$1=yes)])
AC_MSG_RESULT($libiberty_cv_decl_needed_$1)
if test $libiberty_cv_decl_needed_$1 = yes; then
  AC_DEFINE([NEED_DECLARATION_]translit($1, [a-z], [A-Z]), 1,
            [Define if $1 is not declared in system header files.])
fi
])dnl

# We always want a C version of alloca() compiled into libiberty,
# because native-compiler support for the real alloca is so !@#$%
# unreliable that GCC has decided to use it only when being compiled
# by GCC.  This is the part of AC_FUNC_ALLOCA that calculates the
# information alloca.c needs.
AC_DEFUN(libiberty_AC_FUNC_C_ALLOCA,
[AC_CACHE_CHECK(whether alloca needs Cray hooks, ac_cv_os_cray,
[AC_EGREP_CPP(webecray,
[#if defined(CRAY) && ! defined(CRAY2)
webecray
#else
wenotbecray
#endif
], ac_cv_os_cray=yes, ac_cv_os_cray=no)])
if test $ac_cv_os_cray = yes; then
  for ac_func in _getb67 GETB67 getb67; do
    AC_CHECK_FUNC($ac_func, 
      [AC_DEFINE_UNQUOTED(CRAY_STACKSEG_END, $ac_func, 
  [Define to one of _getb67, GETB67, getb67 for Cray-2 and Cray-YMP
   systems. This function is required for alloca.c support on those
   systems.])  break])
  done
fi

AC_CACHE_CHECK(stack direction for C alloca, ac_cv_c_stack_direction,
[AC_TRY_RUN([find_stack_direction ()
{
  static char *addr = 0;
  auto char dummy;
  if (addr == 0)
    {
      addr = &dummy;
      return find_stack_direction ();
    }
  else
    return (&dummy > addr) ? 1 : -1;
}
main ()
{
  exit (find_stack_direction() < 0);
}], 
  ac_cv_c_stack_direction=1,
  ac_cv_c_stack_direction=-1,
  ac_cv_c_stack_direction=0)])
AC_DEFINE_UNQUOTED(STACK_DIRECTION, $ac_cv_c_stack_direction,
  [Define if you know the direction of stack growth for your system;
   otherwise it will be automatically deduced at run-time.
        STACK_DIRECTION > 0 => grows toward higher addresses
        STACK_DIRECTION < 0 => grows toward lower addresses
        STACK_DIRECTION = 0 => direction of growth unknown])
])

# AC_LANG_FUNC_LINK_TRY(C)(FUNCTION)
# ----------------------------------
# Don't include <ctype.h> because on OSF/1 3.0 it includes
# <sys/types.h> which includes <sys/select.h> which contains a
# prototype for select.  Similarly for bzero.
#
# This test used to merely assign f=$1 in main(), but that was
# optimized away by HP unbundled cc A.05.36 for ia64 under +O3,
# presumably on the basis that there's no need to do that store if the
# program is about to exit.  Conversely, the AIX linker optimizes an
# unused external declaration that initializes f=$1.  So this test
# program has both an external initialization of f, and a use of f in
# main that affects the exit status.
#
m4_define([AC_LANG_FUNC_LINK_TRY(C)],
[AC_LANG_PROGRAM(
[/* System header to define __stub macros and hopefully few prototypes,
    which can conflict with char $1 (); below.
    Prefer <limits.h> to <assert.h> if __STDC__ is defined, since
    <limits.h> exists even on freestanding compilers.  Under hpux,
    including <limits.h> includes <sys/time.h> and causes problems
    checking for functions defined therein.  */
#if defined (__STDC__) && !defined (_HPUX_SOURCE)
# include <limits.h>
#else
# include <assert.h>
#endif
/* Override any gcc2 internal prototype to avoid an error.  */
#ifdef __cplusplus
extern "C"
{
#endif
/* We use char because int might match the return type of a gcc2
   builtin and then its argument prototype would still apply.  */
char $1 ();
/* The GNU C library defines this for functions which it implements
    to always fail with ENOSYS.  Some functions are actually named
    something starting with __ and the normal name is an alias.  */
#if defined (__stub_$1) || defined (__stub___$1)
choke me
#else
char (*f) () = $1;
#endif
#ifdef __cplusplus
}
#endif
], [return f != $1;])])

