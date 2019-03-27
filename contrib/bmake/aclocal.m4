dnl RCSid:
dnl	$Id: aclocal.m4,v 1.6 2017/11/26 22:39:20 sjg Exp $
dnl

dnl 
dnl AC_CHECK_HEADER_HAS(HEADER, PATTERN, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]))

AC_DEFUN(AC_CHECK_HEADER_HAS,
[dnl first check if header exists and if so, see if it contains PATTERN
ac_has_hdr=`echo "ac_cv_header_$1" | sed 'y%./+-%__p_%'`
ac_has_it=`echo "ac_cv_header_$1"_$2 | sed 'y%./+-%__p_%'`
if eval "test \"`echo x'$'$ac_has_hdr`\" = x"; then
  AC_CHECK_HEADER($1)
fi
if eval "test \"`echo '$'$ac_has_hdr`\" = yes"; then
  ac_x=HAVE_`echo "$1" | sed 'y%abcdefghijklmnopqrstuvwxyz./-%ABCDEFGHIJKLMNOPQRSTUVWXYZ___%'`
  AC_DEFINE_UNQUOTED($ac_x)
  AC_MSG_CHECKING([if $1 has $2])
  AC_CACHE_VAL($ac_has_it,
               [eval $ac_has_it=no
                AC_EGREP_HEADER($2, $1, eval "$ac_has_it=yes")])

  if eval "test \"`echo '$'$ac_has_it`\" = yes"; then
    AC_MSG_RESULT(yes)
    ac_x=HAVE_`echo "$1"_$2 | sed 'y%abcdefghijklmnopqrstuvwxyz./-%ABCDEFGHIJKLMNOPQRSTUVWXYZ___%'`
    AC_DEFINE_UNQUOTED($ac_x)
    ifelse([$3], , :, [$3])
  else
    AC_MSG_RESULT(no)
    ifelse([$4], , , [$4
])dnl
  fi
fi
])

dnl AC_EGREP(PATTERN, FILE, ACTION-IF-FOUND [,
dnl                 ACTION-IF-NOT-FOUND])
AC_DEFUN(AC_EGREP,
[
dnl Prevent m4 from eating character classes:
changequote(, )dnl
if egrep "$1" $2 >/dev/null 2>&1; then
changequote([, ])dnl
  ifelse([$3], , :, [$3])
ifelse([$4], , , [else
  $4
])dnl
fi
])

dnl
dnl Test for __attribute__
dnl

AC_DEFUN(AC_C___ATTRIBUTE__, [
AC_MSG_CHECKING(for __attribute__)
AC_CACHE_VAL(ac_cv___attribute__, [
AC_LINK_IFELSE([
#include <stdlib.h>

static void foo(void) __attribute__ ((noreturn));

static void
foo(void)
{
  exit(1);
}

int
main(int argc, char **argv)
{
	foo();
}
],
ac_cv___attribute__=yes,
ac_cv___attribute__=no)])
if test "$ac_cv___attribute__" = "yes"; then
  AC_DEFINE(HAVE___ATTRIBUTE__, 1, [define if your compiler has __attribute__])
fi
AC_MSG_RESULT($ac_cv___attribute__)
])

