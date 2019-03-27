dnl See whether we need to use fopen-bin.h rather than fopen-same.h.
AC_DEFUN([BFD_BINARY_FOPEN],
[AC_REQUIRE([AC_CANONICAL_TARGET])
case "${host}" in
changequote(,)dnl
*-*-msdos* | *-*-go32* | *-*-mingw32* | *-*-cygwin* | *-*-windows*)
changequote([,])dnl
  AC_DEFINE(USE_BINARY_FOPEN, 1, [Use b modifier when opening binary files?]) ;;
esac])dnl

dnl Get a default for CC_FOR_BUILD to put into Makefile.
AC_DEFUN([BFD_CC_FOR_BUILD],
[# Put a plausible default for CC_FOR_BUILD in Makefile.
if test -z "$CC_FOR_BUILD"; then
  if test "x$cross_compiling" = "xno"; then
    CC_FOR_BUILD='$(CC)'
  else
    CC_FOR_BUILD=gcc
  fi
fi
AC_SUBST(CC_FOR_BUILD)
# Also set EXEEXT_FOR_BUILD.
if test "x$cross_compiling" = "xno"; then
  EXEEXT_FOR_BUILD='$(EXEEXT)'
else
  AC_CACHE_CHECK([for build system executable suffix], bfd_cv_build_exeext,
    [rm -f conftest*
     echo 'int main () { return 0; }' > conftest.c
     bfd_cv_build_exeext=
     ${CC_FOR_BUILD} -o conftest conftest.c 1>&5 2>&5
     for file in conftest.*; do
       case $file in
       *.c | *.o | *.obj | *.ilk | *.pdb) ;;
       *) bfd_cv_build_exeext=`echo $file | sed -e s/conftest//` ;;
       esac
     done
     rm -f conftest*
     test x"${bfd_cv_build_exeext}" = x && bfd_cv_build_exeext=no])
  EXEEXT_FOR_BUILD=""
  test x"${bfd_cv_build_exeext}" != xno && EXEEXT_FOR_BUILD=${bfd_cv_build_exeext}
fi
AC_SUBST(EXEEXT_FOR_BUILD)])dnl

AC_DEFUN([AM_INSTALL_LIBBFD],
[AC_MSG_CHECKING([whether to install libbfd])
  AC_ARG_ENABLE(install-libbfd,
[  --enable-install-libbfd controls installation of libbfd and related headers],
      install_libbfd_p=$enableval,
      if test "${host}" = "${target}" || test "$enable_shared" = "yes"; then
        install_libbfd_p=yes
      else
        install_libbfd_p=no
      fi)
  AC_MSG_RESULT($install_libbfd_p)
  AM_CONDITIONAL(INSTALL_LIBBFD, test $install_libbfd_p = yes)
  # Need _noncanonical variables for this.
  ACX_NONCANONICAL_HOST
  ACX_NONCANONICAL_TARGET
  # libbfd.a is a host library containing target dependent code
  bfdlibdir='$(libdir)'
  bfdincludedir='$(includedir)'
  if test "${host}" != "${target}"; then
    bfdlibdir='$(exec_prefix)/$(host_noncanonical)/$(target_noncanonical)/lib'
    bfdincludedir='$(exec_prefix)/$(host_noncanonical)/$(target_noncanonical)/include'
  fi
  AC_SUBST(bfdlibdir)
  AC_SUBST(bfdincludedir)
]
)
