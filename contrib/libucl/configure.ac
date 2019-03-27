m4_define([maj_ver], [0])
m4_define([med_ver], [8])
m4_define([min_ver], [0])
m4_define([so_version], [6:0:0])
m4_define([ucl_version], [maj_ver.med_ver.min_ver])

AC_INIT([libucl],[ucl_version],[https://github.com/vstakhov/libucl],[libucl])
AC_CONFIG_SRCDIR([configure.ac])
AM_INIT_AUTOMAKE([1.11 foreign -Wall -Wportability no-dist-gzip dist-xz])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

UCL_VERSION=ucl_version
SO_VERSION=so_version

AC_SUBST(UCL_VERSION)
AC_SUBST(SO_VERSION)

AC_PROG_CC_C99
AM_PROG_CC_C_O
AM_PROG_AR
LT_INIT
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

AC_C_CONST
AC_TYPE_SIZE_T

AC_CHECK_HEADERS_ONCE([fcntl.h unistd.h])
AC_TYPE_OFF_T
AC_FUNC_MMAP
AC_CHECK_HEADERS_ONCE([fcntl.h])
AC_CHECK_HEADERS_ONCE([sys/types.h])
AC_CHECK_HEADERS_ONCE([sys/stat.h])
AC_CHECK_HEADERS_ONCE([sys/param.h])
AC_CHECK_HEADERS_ONCE([sys/mman.h])
AC_CHECK_HEADERS_ONCE([stdlib.h])
AC_CHECK_HEADERS_ONCE([stddef.h])
AC_CHECK_HEADERS_ONCE([stdarg.h])
AC_CHECK_HEADERS_ONCE([stdbool.h])
AC_CHECK_HEADERS_ONCE([stdint.h])
AC_CHECK_HEADERS_ONCE([string.h])
AC_CHECK_HEADERS_ONCE([strings.h])
AC_CHECK_HEADERS_ONCE([unistd.h])
AC_CHECK_HEADERS_ONCE([ctype.h])
AC_CHECK_HEADERS_ONCE([errno.h])
AC_CHECK_HEADERS_ONCE([limits.h])
AC_CHECK_HEADERS_ONCE([libgen.h])
AC_CHECK_HEADERS_ONCE([stdio.h])
AC_CHECK_HEADERS_ONCE([float.h])
AC_CHECK_HEADERS_ONCE([math.h])
AC_CHECK_HEADERS_ONCE([endian.h sys/endian.h machine/endian.h])

dnl Example of default-disabled feature
AC_ARG_ENABLE([urls], AS_HELP_STRING([--enable-urls], 
	[Enable URLs fetch (requires libfetch or libcurl) @<:@default=no@:>@]), [],
	[enable_urls=no])
AC_ARG_ENABLE([regex], AS_HELP_STRING([--enable-regex], 
	[Enable regex checking for schema @<:@default=yes@:>@]), [],
	[enable_regex=yes])
AC_ARG_ENABLE([signatures], AS_HELP_STRING([--enable-signatures],
	[Enable signatures check (requires openssl) @<:@default=no@:>@]), [],
	[enable_signatures=no])
AC_ARG_ENABLE([lua], AS_HELP_STRING([--enable-lua],
	[Enable lua API build (requires lua libraries and headers) @<:@default=no@:>@]), [],
	[enable_lua=no])
AC_ARG_ENABLE([utils],
	AS_HELP_STRING([--enable-utils], [Build and install utils @<:@default=no@:>@]),
	[case "${enableval}" in
  		yes) utils=true ;;
  		no)  utils=false ;;
  		*) AC_MSG_ERROR([bad value ${enableval} for --enable-utils]) ;;
	esac],[utils=false])
AM_CONDITIONAL([UTILS], [test x$utils = xtrue])

AS_IF([test "x$enable_signatures" = "xyes"], [
	AC_SEARCH_LIBS([EVP_MD_CTX_create], [crypto], [
		AC_DEFINE(HAVE_OPENSSL, 1, [Define to 1 if you have the 'crypto' library (-lcrypto).])
		LIBCRYPTO_LIB="-lcrypto"
		LIBS_EXTRA="${LIBS_EXTRA} -lcrypto"
		], [AC_MSG_ERROR([unable to find the EVP_MD_CTX_create() function])])
])
AC_SUBST(LIBCRYPTO_LIB)
AC_PATH_PROG(PANDOC, pandoc, [/non/existent])

AC_SEARCH_LIBS([clock_gettime], [rt], [], [
	AC_CHECK_HEADER([mach/mach_time.h], [
		AC_DEFINE(HAVE_MACH_MACH_TIME_H, 1, [Define to 1 on Darwin])
	], [AC_MSG_ERROR([unable to find clock_gettime or mach_absolute_time])])
])
AC_SEARCH_LIBS([remainder], [m], [], [AC_MSG_ERROR([unable to find remainder() function])])

AS_IF([test "x$enable_regex" = "xyes"], [
	AC_CHECK_HEADER([regex.h], [
		AC_DEFINE(HAVE_REGEX_H, 1, [Define to 1 if you have the <regex.h> header file.])
		AC_SEARCH_LIBS([regexec], [regex], [
			AS_IF([test "x$ac_cv_search_regexec" = "x-lregex"], [
				LIBREGEX_LIB="-lregex"
				LIBS_EXTRA="${LIBS_EXTRA} -lregex"
				]
			)], 
			[AC_MSG_ERROR([unable to find the regexec() function])])],
			[AC_MSG_ERROR([unable to find the regex.h header])
		],
		[#include <sys/types.h>])
])
AC_SUBST(LIBREGEX_LIB)

AS_IF([test "x$enable_lua" = "xyes"], [
	AX_PROG_LUA([5.1], [], [
		AX_LUA_HEADERS([
			AX_LUA_LIBS([
				AC_DEFINE(HAVE_LUA, 1, [Define to 1 for lua support.])
				with_lua="yes"
			], [AC_MSG_ERROR([unable to find the lua libraries])
			])
		], [AC_MSG_ERROR([unable to find the lua header files])
		])
	], [AC_MSG_ERROR([unable to find the lua interpreter])])
], [with_lua="no"])

AM_CONDITIONAL([LUA_SUB], [test "$with_lua" = "yes"])

AS_IF([test "x$enable_urls" = "xyes"], [
	AC_CHECK_HEADER([fetch.h], [
		AC_DEFINE(HAVE_FETCH_H, 1, [Define to 1 if you have the <fetch.h> header file.])
		AC_CHECK_LIB(fetch, fetchXGet, [
			AC_DEFINE(HAVE_LIBFETCH, 1, [Define to 1 if you have the 'fetch' library (-lfetch).])
			LIBFETCH_LIBS="-lfetch"
			have_libfetch="yes"
			LIBS_EXTRA="${LIBS_EXTRA} -lfetch"
		])
	], [],[
	#include <stdio.h>
	#ifdef HAVE_SYS_PARAM_H
	#include <sys/param.h>
	#endif
	])
	AC_SUBST(LIBFETCH_LIBS)

	AS_IF([ test "x$have_libfetch" != "xyes"], [
		dnl Fallback to libcurl
		PKG_CHECK_MODULES([CURL], [libcurl], [
			AC_DEFINE(CURL_FOUND, 1, [Use libcurl])
			LIBS_EXTRA="${LIBS_EXTRA} -lcurl"],
		[AC_MSG_ERROR([unable to find neither libfetch nor libcurl])])
	])
	AC_SUBST(CURL_FOUND)
	AC_SUBST(CURL_LIBS)
	AC_SUBST(CURL_CFLAGS)
])

AC_SUBST(LIBS_EXTRA)

AC_MSG_CHECKING(for GCC atomic builtins)
AC_LINK_IFELSE([
	AC_LANG_SOURCE([[
		int main() {
			volatile unsigned long val = 1;
			__sync_synchronize();
			__sync_val_compare_and_swap(&val, 1, 0);
			__sync_add_and_fetch(&val, 1);
			__sync_sub_and_fetch(&val, 1);
			return 0;
		}
	]])
],
[
	AC_MSG_RESULT([yes])
	AC_DEFINE([HAVE_ATOMIC_BUILTINS], [1], [Has gcc/MSVC atomic intrinsics])
],
[
	AC_MSG_RESULT([no])
	AC_DEFINE([HAVE_ATOMIC_BUILTINS], [0], [Has gcc/MSVC atomic intrinsics])
	AC_MSG_WARN([Libucl references could be thread-unsafe because atomic builtins are missing])
])

AX_CODE_COVERAGE

AC_CONFIG_FILES(Makefile \
	src/Makefile \
	lua/Makefile
	tests/Makefile \
	utils/Makefile \
	doc/Makefile \
	lua/libucl.rockspec \
	libucl.pc)
AC_CONFIG_FILES([stamp-h], [echo timestamp > stamp-h])
AC_OUTPUT
