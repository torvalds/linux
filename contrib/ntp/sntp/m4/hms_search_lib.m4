dnl Helper function to manage granular libraries
dnl
dnl Usage:
dnl
dnl LIB_MATH=''
dnl AC_SUBST([LIB_MATH])
dnl ...
dnl HMS_SEARCH_LIBS([LIB_MATH], [sqrt], [m], [AIF], [AINF], [OL])
dnl 
dnl which expands to something like:
dnl 
dnl  AC_SEARCH_LIBS([sqrt], [m], [case "$ac_cv_search_sqrt" in
dnl    'none required') ;;
dnl    no) ;;
dnl    *) LIB_MATH="$ac_cv_search_sqrt $LIB_MATH" ;;
dnl   esac
dnl   [AIF]],
dnl   [AINF],
dnl   [OL])
dnl
dnl arguments are: lib-var, function, search-libs, [AIF], [AINF], [other-libs]
AC_DEFUN([HMS_SEARCH_LIBS],
[AC_SEARCH_LIBS([$2], [$3], [case "$ac_cv_search_$2[]" in
 'none required') ;;
 no) ;;
 *) $1[]="$ac_cv_search_$2[] $[]$1" ;;
 esac
 $4],
 $5,
 [$6])])
