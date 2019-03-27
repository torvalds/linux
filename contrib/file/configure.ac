dnl Process this file with autoconf to produce a configure script.
AC_INIT([file],[5.34],[christos@astron.com])
AM_INIT_AUTOMAKE([subdir-objects foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_MSG_CHECKING(for builtin ELF support)
AC_ARG_ENABLE(elf,
[  --disable-elf            disable builtin ELF support],
[if test "${enableval}" = yes; then
  AC_MSG_RESULT(yes)
  AC_DEFINE([BUILTIN_ELF], 1, [Define if built-in ELF support is used])
else
  AC_MSG_RESULT(no)
fi], [
  # enable by default
  AC_MSG_RESULT(yes)
  AC_DEFINE([BUILTIN_ELF], 1, [Define in built-in ELF support is used])
])

AC_MSG_CHECKING(for ELF core file support)
AC_ARG_ENABLE(elf-core,
[  --disable-elf-core       disable ELF core file support],
[if test "${enableval}" = yes; then
  AC_MSG_RESULT(yes)
  AC_DEFINE([ELFCORE], 1, [Define for ELF core file support])
else
  AC_MSG_RESULT(no)
fi], [
  # enable by default
  AC_MSG_RESULT(yes)
  AC_DEFINE([ELFCORE], 1, [Define for ELF core file support])
])

AC_MSG_CHECKING(for zlib support)
AC_ARG_ENABLE(zlib,
[AS_HELP_STRING([--disable-zlib], [disable zlib compression support @<:@default=auto@:>@])])
AC_MSG_RESULT($enable_zlib)

AC_MSG_CHECKING(for libseccomp support)
AC_ARG_ENABLE(libseccomp,
[AS_HELP_STRING([--disable-libseccomp], [disable libseccomp sandboxing @<:@default=auto@:>@])])
AC_MSG_RESULT($enable_libseccomp)

AC_MSG_CHECKING(for file formats in man section 5)
AC_ARG_ENABLE(fsect-man5,
[  --enable-fsect-man5      enable file formats in man section 5],
[if test "${enableval}" = yes; then
  AC_MSG_RESULT(yes)
  fsect=5
else
  AC_MSG_RESULT(no)
  fsect=4
fi], [
  # disable by default
  AC_MSG_RESULT(no)
  fsect=4
])

AC_CANONICAL_HOST
case "$host_os" in
   mingw32*)
      MINGW=1
      ;;
   *)
      MINGW=0
      ;;
esac
AC_SUBST(MINGW)
AM_CONDITIONAL(MINGW, test "$MINGW" = 1)

AC_SUBST([pkgdatadir], ['$(datadir)/misc'])
AC_SUBST(fsect)
AM_CONDITIONAL(FSECT5, test x$fsect = x5)

AC_SUBST(WARNINGS)

dnl Checks for programs.
AC_PROG_CC_STDC
AC_USE_SYSTEM_EXTENSIONS
AM_PROG_CC_C_O
AC_C_BIGENDIAN
AC_PROG_INSTALL
AC_PROG_LN_S
LT_INIT([disable-static pic-only])
gl_VISIBILITY
dnl Checks for headers
AC_HEADER_STDC
AC_HEADER_MAJOR
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS(stdint.h fcntl.h locale.h stdint.h inttypes.h unistd.h)
AC_CHECK_HEADERS(stddef.h utime.h wchar.h wctype.h limits.h)
AC_CHECK_HEADERS(getopt.h err.h xlocale.h signal.h)
AC_CHECK_HEADERS(sys/mman.h sys/stat.h sys/types.h sys/utime.h sys/time.h)
if test "$enable_zlib" != "no"; then
  AC_CHECK_HEADERS(zlib.h)
fi
AC_CHECK_TYPE([sig_t],[AC_DEFINE([HAVE_SIG_T],1,[Have sig_t type])],,[
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif])

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_rdev])

AC_CHECK_MEMBERS([struct tm.tm_gmtoff])
AC_STRUCT_TIMEZONE
AC_STRUCT_TIMEZONE_DAYLIGHT
AC_SYS_LARGEFILE
AC_FUNC_FSEEKO
AC_TYPE_MBSTATE_T

AC_STRUCT_OPTION_GETOPT_H
AC_TYPE_PID_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_INT32_T
AC_TYPE_UINT64_T
AC_TYPE_INT64_T
AC_TYPE_INTPTR_T
AC_TYPE_UINTPTR_T
AC_FUNC_MMAP
AC_FUNC_FORK
AC_FUNC_MBRTOWC

AC_MSG_CHECKING(for gcc compiler warnings)
AC_ARG_ENABLE(warnings,
[  --disable-warnings	disable compiler warnings],
[if test "${enableval}" = no -o "$GCC" = no; then
   AC_MSG_RESULT(no)
   WARNINGS=
else
   AC_MSG_RESULT(yes)
   WARNINGS="-Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith \
       -Wmissing-declarations -Wredundant-decls -Wnested-externs \
       -Wsign-compare -Wreturn-type -Wswitch -Wshadow \
       -Wcast-qual -Wwrite-strings -Wextra -Wunused-parameter -Wformat=2"
fi], [
if test "$GCC" = yes; then
   AC_MSG_RESULT(yes)
   WARNINGS="-Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith \
       -Wmissing-declarations -Wredundant-decls -Wnested-externs \
       -Wsign-compare -Wreturn-type -Wswitch -Wshadow \
       -Wcast-qual -Wwrite-strings -Wextra -Wunused-parameter -Wformat=2"
else
   WARNINGS=
   AC_MSG_RESULT(no)
fi])

dnl Checks for functions
AC_CHECK_FUNCS(strerror strndup strtoul mkstemp mkostemp utimes utime wcwidth strtof newlocale uselocale freelocale setlocale)

dnl Provide implementation of some required functions if necessary
AC_REPLACE_FUNCS(getopt_long asprintf vasprintf strlcpy strlcat getline ctime_r asctime_r localtime_r gmtime_r pread strcasestr fmtcheck dprintf)

dnl Checks for libraries
if test "$enable_zlib" != "no"; then
  AC_CHECK_LIB(z, gzopen)
fi
if test "$enable_libseccomp" != "no"; then
    AC_CHECK_LIB(seccomp, seccomp_init)
fi
if test "$MINGW" = 1; then
  AC_CHECK_LIB(gnurx,regexec,,AC_MSG_ERROR([libgnurx is required to build file(1) with MinGW]))
fi

dnl See if we are cross-compiling
AM_CONDITIONAL(IS_CROSS_COMPILE, test "$cross_compiling" = yes)

dnl Final sanity checks
if test "$enable_zlib" = "yes"; then
  if test "$ac_cv_header_zlib_h$ac_cv_lib_z_gzopen" != "yesyes"; then
    AC_MSG_ERROR([zlib support requested but not found])
  fi
elif  test "$ac_cv_header_zlib_h$ac_cv_lib_z_gzopen" = "yesyes"; then
  AC_DEFINE([ZLIBSUPPORT], 1, [Enable zlib compression support])
fi

AC_CONFIG_FILES([Makefile src/Makefile magic/Makefile tests/Makefile doc/Makefile python/Makefile])
AC_OUTPUT
