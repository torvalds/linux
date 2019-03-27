#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(cpplib, [ ], gcc-bugs@gcc.gnu.org, cpplib)
AC_CONFIG_SRCDIR(ucnid.h)
AC_CONFIG_MACRO_DIR(../config)
AC_CANONICAL_SYSTEM

# Checks for programs.
AC_PROG_MAKE_SET
AC_PROG_INSTALL
AC_PROG_CC
AC_PROG_RANLIB

MISSING=`cd $ac_aux_dir && ${PWDCMD-pwd}`/missing
AC_CHECK_PROGS([ACLOCAL], [aclocal], [$MISSING aclocal])
AC_CHECK_PROGS([AUTOCONF], [autoconf], [$MISSING autoconf])
AC_CHECK_PROGS([AUTOHEADER], [autoheader], [$MISSING autoheader])

# Figure out what compiler warnings we can enable.
# See config/warnings.m4 for details.

ACX_PROG_CC_WARNING_OPTS([-W -Wall -Wwrite-strings -Wstrict-prototypes \
			  -Wmissing-prototypes -Wold-style-definition \
			  -Wmissing-format-attribute])
ACX_PROG_CC_WARNING_ALMOST_PEDANTIC([-Wno-long-long])

# Only enable with --enable-werror-always until existing warnings are
# corrected.
ACX_PROG_CC_WARNINGS_ARE_ERRORS([manual])

# Dependency checking.
ZW_CREATE_DEPDIR
ZW_PROG_COMPILER_DEPENDENCIES([CC])

# Checks for header files.
AC_HEADER_TIME
ACX_HEADER_STRING
AC_CHECK_HEADERS(iconv.h locale.h fcntl.h limits.h stddef.h \
	stdlib.h strings.h string.h sys/file.h unistd.h)

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_FUNC_OBSTACK
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_STRUCT_TM
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(long)
define(libcpp_UNLOCKED_FUNCS, clearerr_unlocked feof_unlocked dnl
  ferror_unlocked fflush_unlocked fgetc_unlocked fgets_unlocked dnl
  fileno_unlocked fprintf_unlocked fputc_unlocked fputs_unlocked dnl
  fread_unlocked fwrite_unlocked getchar_unlocked getc_unlocked dnl
  putchar_unlocked putc_unlocked)
AC_CHECK_FUNCS(libcpp_UNLOCKED_FUNCS)
AC_CHECK_DECLS(m4_split(m4_normalize(abort asprintf basename errno getopt \
  libcpp_UNLOCKED_FUNCS vasprintf)))

# Checks for library functions.
AC_FUNC_ALLOCA
AC_HEADER_STDC
AM_LANGINFO_CODESET
ZW_GNU_GETTEXT_SISTER_DIR

AC_CACHE_CHECK(for uchar, gcc_cv_type_uchar,
[AC_TRY_COMPILE([
#include <sys/types.h>
],
[if ((uchar *)0) return 0;
 if (sizeof(uchar)) return 0;],
ac_cv_type_uchar=yes, ac_cv_type_uchar=no)])
if test $ac_cv_type_uchar = yes; then
  AC_DEFINE(HAVE_UCHAR, 1,
  [Define if <sys/types.h> defines \`uchar'.])
fi

AM_ICONV

# More defines and substitutions.
PACKAGE="$PACKAGE_TARNAME"
AC_DEFINE_UNQUOTED(PACKAGE, "$PACKAGE", [Define to the name of this package.])
AC_SUBST(PACKAGE)

if test "x$enable_nls" != xno; then
  USED_CATALOGS='$(CATALOGS)'
else
  USED_CATALOGS=
fi
AC_SUBST(USED_CATALOGS)

AC_ARG_ENABLE(maintainer-mode,
[  --enable-maintainer-mode enable rules only needed by maintainers],,
enable_maintainer_mode=no)

if test "x$enable_maintainer_mode" = xno; then
  MAINT='#'
else
  MAINT=
fi
AC_SUBST(MAINT)

AC_ARG_ENABLE(checking,
[  --enable-checking      enable expensive run-time checks],,
enable_checking=no)

if test $enable_checking != no ; then
  AC_DEFINE(ENABLE_CHECKING, 1,
[Define if you want more run-time sanity checks.])
fi

m4_changequote(,)
case $target in
	alpha*-*-* | \
	arm*-*-*eabi* | \
	arm*-*-symbianelf* | \
	x86_64-*-* | \
	ia64-*-* | \
	hppa*64*-*-* | parisc*64*-*-* | \
	i[34567]86-*-darwin* | \
	i[34567]86-*-solaris2.1[0-9]* | \
	mips*-*-* | \
	mmix-*-* | \
	powerpc*-*-* | \
	rs6000*-*-* | \
	s390*-*-* | \
	sparc64*-*-* | ultrasparc-*-freebsd* | \
	sparcv9-*-solaris2* | \
	sparc-*-solaris2.[789] | sparc-*-solaris2.1[0-9]* | \
	sh[123456789l]*-*-*)
		need_64bit_hwint=yes ;;
	*)
		need_64bit_hwint=no ;;
esac

case $need_64bit_hwint:$ac_cv_sizeof_long in
	*:8 | no:*) host_wide_int=long ;;
	*) host_wide_int='long long' ;;
esac
m4_changequote([,])

AC_DEFINE_UNQUOTED(HOST_WIDE_INT, $host_wide_int,
[Define to the widest efficient host integer type at least
   as wide as the target's size_t type.])

# Output.

AC_CONFIG_HEADERS(config.h:config.in, [echo timestamp > stamp-h1])
AC_CONFIG_FILES(Makefile)
AC_OUTPUT
