
dnl
dnl GLIBCXX_CONDITIONAL (NAME, SHELL-TEST)
dnl
dnl Exactly like AM_CONDITIONAL, but delays evaluation of the test until the
dnl end of configure.  This lets tested variables be reassigned, and the
dnl conditional will depend on the final state of the variable.  For a simple
dnl example of why this is needed, see GLIBCXX_ENABLE_HOSTED.
dnl
m4_define([_m4_divert(glibcxx_diversion)], 8000)dnl
AC_DEFUN([GLIBCXX_CONDITIONAL], [dnl
  m4_divert_text([glibcxx_diversion],dnl
   AM_CONDITIONAL([$1],[$2])
  )dnl
])dnl
AC_DEFUN([GLIBCXX_EVALUATE_CONDITIONALS], [m4_undivert([glibcxx_diversion])])dnl


dnl
dnl Check to see what architecture and operating system we are compiling
dnl for.  Also, if architecture- or OS-specific flags are required for
dnl compilation, pick them up here.
dnl
AC_DEFUN([GLIBCXX_CHECK_HOST], [
  . $glibcxx_srcdir/configure.host
  AC_MSG_NOTICE([CPU config directory is $cpu_include_dir])
  AC_MSG_NOTICE([OS config directory is $os_include_dir])
])

dnl
dnl Initialize the rest of the library configury.  At this point we have
dnl variables like $host.
dnl
dnl Sets:
dnl  SUBDIRS
dnl Substs:
dnl  glibcxx_builddir     (absolute path)
dnl  glibcxx_srcdir       (absolute path)
dnl  toplevel_srcdir      (absolute path)
dnl  with_cross_host
dnl  with_newlib
dnl  with_target_subdir
dnl plus
dnl  - the variables in GLIBCXX_CHECK_HOST / configure.host
dnl  - default settings for all AM_CONFITIONAL test variables
dnl  - lots of tools, like CC and CXX
dnl
AC_DEFUN([GLIBCXX_CONFIGURE], [
  # Keep these sync'd with the list in Makefile.am.  The first provides an
  # expandable list at autoconf time; the second provides an expandable list
  # (i.e., shell variable) at configure time.
  m4_define([glibcxx_SUBDIRS],[include libmath libsupc++ src po testsuite])
  SUBDIRS='glibcxx_SUBDIRS'

  # These need to be absolute paths, yet at the same time need to
  # canonicalize only relative paths, because then amd will not unmount
  # drives. Thus the use of PWDCMD: set it to 'pawd' or 'amq -w' if using amd.
  glibcxx_builddir=`${PWDCMD-pwd}`
  case $srcdir in
    [\\/$]* | ?:[\\/]*) glibcxx_srcdir=${srcdir} ;;
    *) glibcxx_srcdir=`cd "$srcdir" && ${PWDCMD-pwd} || echo "$srcdir"` ;;
  esac
  toplevel_srcdir=${glibcxx_srcdir}/..
  AC_SUBST(glibcxx_builddir)
  AC_SUBST(glibcxx_srcdir)
  AC_SUBST(toplevel_srcdir)

  # We use these options to decide which functions to include.  They are
  # set from the top level.
  AC_ARG_WITH([target-subdir],
    AC_HELP_STRING([--with-target-subdir=SUBDIR],
                   [configuring in a subdirectory]))

  AC_ARG_WITH([cross-host],
    AC_HELP_STRING([--with-cross-host=HOST],
                   [configuring with a cross compiler]))

  AC_ARG_WITH([newlib],
    AC_HELP_STRING([--with-newlib],
                   [assume newlib as a system C library]))

  # We're almost certainly being configured before anything else which uses
  # C++, so all of our AC_PROG_* discoveries will be cached.  It's vital that
  # we not cache the value of CXX that we "discover" here, because it's set
  # to something unique for us and libjava.  Other target libraries need to
  # find CXX for themselves.  We yank the rug out from under the normal AC_*
  # process by sneakily renaming the cache variable.  This also lets us debug
  # the value of "our" CXX in postmortems.
  #
  # We must also force CXX to /not/ be a precious variable, otherwise the
  # wrong (non-multilib-adjusted) value will be used in multilibs.  This
  # little trick also affects CPPFLAGS, CXXFLAGS, and LDFLAGS.  And as a side
  # effect, CXXFLAGS is no longer automagically subst'd, so we have to do
  # that ourselves.  Un-preciousing AC_PROG_CC also affects CC and CFLAGS.
  #
  # -fno-builtin must be present here so that a non-conflicting form of
  # std::exit can be guessed by AC_PROG_CXX, and used in later tests.

  m4_define([ac_cv_prog_CXX],[glibcxx_cv_prog_CXX])
  m4_rename([_AC_ARG_VAR_PRECIOUS],[glibcxx_PRECIOUS])
  m4_define([_AC_ARG_VAR_PRECIOUS],[])
  save_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS="$CXXFLAGS -fno-builtin"
  AC_PROG_CC
  AC_PROG_CXX
  CXXFLAGS="$save_CXXFLAGS"
  m4_rename([glibcxx_PRECIOUS],[_AC_ARG_VAR_PRECIOUS])
  AC_SUBST(CFLAGS)
  AC_SUBST(CXXFLAGS)

  # Will set LN_S to either 'ln -s', 'ln', or 'cp -p' (if linking isn't
  # available).  Uncomment the next line to force a particular method.
  AC_PROG_LN_S
  #LN_S='cp -p'

  AC_CHECK_TOOL(AS, as)
  AC_CHECK_TOOL(AR, ar)
  AC_CHECK_TOOL(RANLIB, ranlib, ranlib-not-found-in-path-error)

  AM_MAINTAINER_MODE

  # Set up safe default values for all subsequent AM_CONDITIONAL tests
  # which are themselves conditionally expanded.
  ## (Right now, this only matters for enable_wchar_t, but nothing prevents
  ## other macros from doing the same.  This should be automated.)  -pme
  need_libmath=no

  # Check for uClibc since Linux platforms use different configuration
  # directories depending on the C library in use.
  AC_EGREP_CPP([_using_uclibc], [
  #include <stdio.h>
  #if __UCLIBC__
    _using_uclibc
  #endif
  ], uclibc=yes, uclibc=no)

  # Find platform-specific directories containing configuration info.
  # Also possibly modify flags used elsewhere, as needed by the platform.
  GLIBCXX_CHECK_HOST
])


dnl
dnl Tests for newer compiler features, or features that are present in newer
dnl compiler versions but not older compiler versions still in use, should
dnl be placed here.
dnl
dnl Defines:
dnl  WERROR='-Werror' if requested and possible; g++'s that lack the
dnl   new inlining code or the new system_header pragma will die on -Werror.
dnl   Leave it out by default and use maint-mode to use it.
dnl  SECTION_FLAGS='-ffunction-sections -fdata-sections' if
dnl   compiler supports it and the user has not requested debug mode.
dnl
AC_DEFUN([GLIBCXX_CHECK_COMPILER_FEATURES], [
  # All these tests are for C++; save the language and the compiler flags.
  # The CXXFLAGS thing is suspicious, but based on similar bits previously
  # found in GLIBCXX_CONFIGURE.
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS
  ac_test_CXXFLAGS="${CXXFLAGS+set}"
  ac_save_CXXFLAGS="$CXXFLAGS"

  # Check for maintainer-mode bits.
  if test x"$USE_MAINTAINER_MODE" = xno; then
    WERROR=''
  else
    WERROR='-Werror'
  fi

  # Check for -ffunction-sections -fdata-sections
  AC_MSG_CHECKING([for g++ that supports -ffunction-sections -fdata-sections])
  CXXFLAGS='-Werror -ffunction-sections -fdata-sections'
  AC_TRY_COMPILE(, [int foo;], [ac_fdsections=yes], [ac_fdsections=no])
  if test "$ac_test_CXXFLAGS" = set; then
    CXXFLAGS="$ac_save_CXXFLAGS"
  else
    # this is the suspicious part
    CXXFLAGS=''
  fi
  if test x"$ac_fdsections" = x"yes"; then
    SECTION_FLAGS='-ffunction-sections -fdata-sections'
  fi
  AC_MSG_RESULT($ac_fdsections)

  AC_LANG_RESTORE
  AC_SUBST(WERROR)
  AC_SUBST(SECTION_FLAGS)
])


dnl
dnl If GNU ld is in use, check to see if tricky linker opts can be used.  If
dnl the native linker is in use, all variables will be defined to something
dnl safe (like an empty string).
dnl
dnl Defines:
dnl  SECTION_LDFLAGS='-Wl,--gc-sections' if possible
dnl  OPT_LDFLAGS='-Wl,-O1' and '-z,relro' if possible
dnl  LD (as a side effect of testing)
dnl Sets:
dnl  with_gnu_ld
dnl  glibcxx_gnu_ld_version (possibly)
dnl
dnl The last will be a single integer, e.g., version 1.23.45.0.67.89 will
dnl set glibcxx_gnu_ld_version to 12345.  Zeros cause problems.
dnl
AC_DEFUN([GLIBCXX_CHECK_LINKER_FEATURES], [
  # If we're not using GNU ld, then there's no point in even trying these
  # tests.  Check for that first.  We should have already tested for gld
  # by now (in libtool), but require it now just to be safe...
  test -z "$SECTION_LDFLAGS" && SECTION_LDFLAGS=''
  test -z "$OPT_LDFLAGS" && OPT_LDFLAGS=''
  AC_REQUIRE([AC_PROG_LD])
  AC_REQUIRE([AC_PROG_AWK])

  # The name set by libtool depends on the version of libtool.  Shame on us
  # for depending on an impl detail, but c'est la vie.  Older versions used
  # ac_cv_prog_gnu_ld, but now it's lt_cv_prog_gnu_ld, and is copied back on
  # top of with_gnu_ld (which is also set by --with-gnu-ld, so that actually
  # makes sense).  We'll test with_gnu_ld everywhere else, so if that isn't
  # set (hence we're using an older libtool), then set it.
  if test x${with_gnu_ld+set} != xset; then
    if test x${ac_cv_prog_gnu_ld+set} != xset; then
      # We got through "ac_require(ac_prog_ld)" and still not set?  Huh?
      with_gnu_ld=no
    else
      with_gnu_ld=$ac_cv_prog_gnu_ld
    fi
  fi

  # Start by getting the version number.  I think the libtool test already
  # does some of this, but throws away the result.
  if test x"$with_gnu_ld" = x"yes"; then
    AC_MSG_CHECKING([for ld version])
    changequote(,)
    ldver=`$LD --version 2>/dev/null | head -1 | \
           sed -e 's/GNU ld \(version \)\{0,1\}\(([^)]*) \)\{0,1\}\([0-9.][0-9.]*\).*/\3/'`
    changequote([,])
    glibcxx_gnu_ld_version=`echo $ldver | \
           $AWK -F. '{ if (NF<3) [$]3=0; print ([$]1*100+[$]2)*100+[$]3 }'`
    AC_MSG_RESULT($glibcxx_gnu_ld_version)
  fi

  # Set --gc-sections.
  glibcxx_gcsections_min_ld=21602
  if test x"$with_gnu_ld" = x"yes" && 
	test $glibcxx_gnu_ld_version -gt $glibcxx_gcsections_min_ld ; then

    # Sufficiently young GNU ld it is!  Joy and bunny rabbits!
    # NB: This flag only works reliably after 2.16.1. Configure tests
    # for this are difficult, so hard wire a value that should work.

    ac_test_CFLAGS="${CFLAGS+set}"
    ac_save_CFLAGS="$CFLAGS"
    CFLAGS='-Wl,--gc-sections'

    # Check for -Wl,--gc-sections
    AC_MSG_CHECKING([for ld that supports -Wl,--gc-sections])
    AC_TRY_LINK([ int one(void) { return 1; }
     int two(void) { return 2; }
	], [ two(); ] , [ac_gcsections=yes], [ac_gcsections=no])
    if test "$ac_gcsections" = "yes"; then
      rm -f conftest.c
      touch conftest.c
      if $CC -c conftest.c; then
	if $LD --gc-sections -o conftest conftest.o 2>&1 | \
	   grep "Warning: gc-sections option ignored" > /dev/null; then
	  ac_gcsections=no
	fi
      fi
      rm -f conftest.c conftest.o conftest
    fi
    if test "$ac_gcsections" = "yes"; then
      SECTION_LDFLAGS="-Wl,--gc-sections $SECTION_LDFLAGS"
    fi
    AC_MSG_RESULT($ac_gcsections)

    if test "$ac_test_CFLAGS" = set; then
      CFLAGS="$ac_save_CFLAGS"
    else
      # this is the suspicious part
      CFLAGS=''
    fi
  fi

  # Set -z,relro.
  # Note this is only for shared objects.
  ac_ld_relro=no
  if test x"$with_gnu_ld" = x"yes"; then
    AC_MSG_CHECKING([for ld that supports -Wl,-z,relro])
    cxx_z_relo=`$LD -v --help 2>/dev/null | grep "z relro"`
    if test -n "$cxx_z_relo"; then
      OPT_LDFLAGS="-Wl,-z,relro"
      ac_ld_relro=yes
    fi
    AC_MSG_RESULT($ac_ld_relro)
  fi

  # Set linker optimization flags.
  if test x"$with_gnu_ld" = x"yes"; then
    OPT_LDFLAGS="-Wl,-O1 $OPT_LDFLAGS"
  fi

  AC_SUBST(SECTION_LDFLAGS)
  AC_SUBST(OPT_LDFLAGS)
])


dnl
dnl Check to see if this target can enable the iconv specializations.
dnl If --disable-c-mbchar was given, no wchar_t specialization is enabled.  
dnl (This must have been previously checked, along with the rest of C99 
dnl support.) By default, iconv support is disabled.
dnl
dnl Defines:
dnl  _GLIBCXX_USE_ICONV if all the bits are found.
dnl Substs:
dnl  LIBICONV to a -l string containing the iconv library, if needed.
dnl
AC_DEFUN([GLIBCXX_CHECK_ICONV_SUPPORT], [

  enable_iconv=no
  # Only continue checking if the ISO C99 headers exist and support is on.
  if test x"$enable_wchar_t" = xyes; then

    # Use iconv for wchar_t to char conversions. As such, check for
    # X/Open Portability Guide, version 2 features (XPG2).
    AC_CHECK_HEADER(iconv.h, ac_has_iconv_h=yes, ac_has_iconv_h=no)
    AC_CHECK_HEADER(langinfo.h, ac_has_langinfo_h=yes, ac_has_langinfo_h=no)

    # Check for existence of libiconv.a providing XPG2 wchar_t support.
    AC_CHECK_LIB(iconv, iconv, LIBICONV="-liconv")
    ac_save_LIBS="$LIBS"
    LIBS="$LIBS $LIBICONV"
    AC_SUBST(LIBICONV)

    AC_CHECK_FUNCS([iconv_open iconv_close iconv nl_langinfo],
    [ac_XPG2funcs=yes], [ac_XPG2funcs=no])

    LIBS="$ac_save_LIBS"

    if test x"$ac_has_iconv_h" = xyes &&
       test x"$ac_has_langinfo_h" = xyes &&
       test x"$ac_XPG2funcs" = xyes;
    then
      AC_DEFINE([_GLIBCXX_USE_ICONV],1,
	        [Define if iconv and related functions exist and are usable.])
      enable_iconv=yes
    fi
  fi
  AC_MSG_CHECKING([for enabled iconv specializations])
  AC_MSG_RESULT($enable_iconv)
])


dnl
dnl Check for headers for, and arguments to, the setrlimit() function.
dnl Used only in testsuite_hooks.h.  Called from GLIBCXX_CONFIGURE_TESTSUITE.
dnl
dnl Defines:
dnl  _GLIBCXX_RES_LIMITS if we can set artificial resource limits 
dnl  various HAVE_LIMIT_* for individual limit names
dnl
AC_DEFUN([GLIBCXX_CHECK_SETRLIMIT_ancilliary], [
  AC_MSG_CHECKING([for RLIMIT_$1])
  AC_TRY_COMPILE(
    [#include <unistd.h>
     #include <sys/time.h>
     #include <sys/resource.h>
    ],
    [ int f = RLIMIT_$1 ; ],
    [glibcxx_mresult=1], [glibcxx_mresult=0])
  AC_DEFINE_UNQUOTED(HAVE_LIMIT_$1, $glibcxx_mresult,
                     [Only used in build directory testsuite_hooks.h.])
  if test $glibcxx_mresult = 1 ; then res=yes ; else res=no ; fi
  AC_MSG_RESULT($res)
])

AC_DEFUN([GLIBCXX_CHECK_SETRLIMIT], [
  setrlimit_have_headers=yes
  AC_CHECK_HEADERS(unistd.h sys/time.h sys/resource.h,
                   [],
                   [setrlimit_have_headers=no])
  # If don't have the headers, then we can't run the tests now, and we
  # won't be seeing any of these during testsuite compilation.
  if test $setrlimit_have_headers = yes; then
    # Can't do these in a loop, else the resulting syntax is wrong.
    GLIBCXX_CHECK_SETRLIMIT_ancilliary(DATA)
    GLIBCXX_CHECK_SETRLIMIT_ancilliary(RSS)
    GLIBCXX_CHECK_SETRLIMIT_ancilliary(VMEM)
    GLIBCXX_CHECK_SETRLIMIT_ancilliary(AS)
    GLIBCXX_CHECK_SETRLIMIT_ancilliary(FSIZE)

    # Check for rlimit, setrlimit.
    AC_CACHE_VAL(ac_setrlimit, [
      AC_TRY_COMPILE(
        [#include <unistd.h>
         #include <sys/time.h>
         #include <sys/resource.h>
        ],
        [struct rlimit r;
         setrlimit(0, &r);],
        [ac_setrlimit=yes], [ac_setrlimit=no])
    ])
  fi

  AC_MSG_CHECKING([for testsuite resource limits support])
  if test $setrlimit_have_headers = yes && test $ac_setrlimit = yes; then
    ac_res_limits=yes
    AC_DEFINE(_GLIBCXX_RES_LIMITS, 1,
              [Define if using setrlimit to set resource limits during
              "make check"])
  else
    ac_res_limits=no
  fi
  AC_MSG_RESULT($ac_res_limits)
])


dnl
dnl Check whether S_ISREG (Posix) or S_IFREG is available in <sys/stat.h>.
dnl Define HAVE_S_ISREG / HAVE_S_IFREG appropriately.
dnl
AC_DEFUN([GLIBCXX_CHECK_S_ISREG_OR_S_IFREG], [
  AC_MSG_CHECKING([for S_ISREG or S_IFREG])
  AC_CACHE_VAL(glibcxx_cv_S_ISREG, [
    AC_TRY_LINK(
      [#include <sys/stat.h>],
      [struct stat buffer;
       fstat(0, &buffer);
       S_ISREG(buffer.st_mode);],
      [glibcxx_cv_S_ISREG=yes],
      [glibcxx_cv_S_ISREG=no])
  ])
  AC_CACHE_VAL(glibcxx_cv_S_IFREG, [
    AC_TRY_LINK(
      [#include <sys/stat.h>],
      [struct stat buffer;
       fstat(0, &buffer);
       S_IFREG & buffer.st_mode;],
      [glibcxx_cv_S_IFREG=yes],
      [glibcxx_cv_S_IFREG=no])
  ])
  res=no
  if test $glibcxx_cv_S_ISREG = yes; then
    AC_DEFINE(HAVE_S_ISREG, 1, 
              [Define if S_IFREG is available in <sys/stat.h>.])
    res=S_ISREG
  elif test $glibcxx_cv_S_IFREG = yes; then
    AC_DEFINE(HAVE_S_IFREG, 1,
              [Define if S_IFREG is available in <sys/stat.h>.])
    res=S_IFREG
  fi
  AC_MSG_RESULT($res)
])


dnl
dnl Check whether poll is available in <poll.h>, and define HAVE_POLL.
dnl
AC_DEFUN([GLIBCXX_CHECK_POLL], [
  AC_MSG_CHECKING([for poll])
  AC_CACHE_VAL(glibcxx_cv_POLL, [
    AC_TRY_LINK(
      [#include <poll.h>],
      [struct pollfd pfd[1];
       pfd[0].events = POLLIN;
       poll(pfd, 1, 0);],
      [glibcxx_cv_POLL=yes],
      [glibcxx_cv_POLL=no])
  ])
  if test $glibcxx_cv_POLL = yes; then
    AC_DEFINE(HAVE_POLL, 1, [Define if poll is available in <poll.h>.])
  fi
  AC_MSG_RESULT($glibcxx_cv_POLL)
])


dnl
dnl Check whether writev is available in <sys/uio.h>, and define HAVE_WRITEV.
dnl
AC_DEFUN([GLIBCXX_CHECK_WRITEV], [
  AC_MSG_CHECKING([for writev])
  AC_CACHE_VAL(glibcxx_cv_WRITEV, [
    AC_TRY_LINK(
      [#include <sys/uio.h>],
      [struct iovec iov[2];
       writev(0, iov, 0);],
      [glibcxx_cv_WRITEV=yes],
      [glibcxx_cv_WRITEV=no])
  ])
  if test $glibcxx_cv_WRITEV = yes; then
    AC_DEFINE(HAVE_WRITEV, 1, [Define if writev is available in <sys/uio.h>.])
  fi
  AC_MSG_RESULT($glibcxx_cv_WRITEV)
])


dnl
dnl Check whether int64_t is available in <stdint.h>, and define HAVE_INT64_T.
dnl
AC_DEFUN([GLIBCXX_CHECK_INT64_T], [
  AC_MSG_CHECKING([for int64_t])
  AC_CACHE_VAL(glibcxx_cv_INT64_T, [
    AC_TRY_COMPILE(
      [#include <stdint.h>],
      [int64_t var;],
      [glibcxx_cv_INT64_T=yes],
      [glibcxx_cv_INT64_T=no])
  ])
  if test $glibcxx_cv_INT64_T = yes; then
    AC_DEFINE(HAVE_INT64_T, 1, [Define if int64_t is available in <stdint.h>.])
  fi
  AC_MSG_RESULT($glibcxx_cv_INT64_T)
])


dnl
dnl Check whether LFS support is available.
dnl
AC_DEFUN([GLIBCXX_CHECK_LFS], [
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS
  ac_save_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS="$CXXFLAGS -fno-exceptions"	
  AC_MSG_CHECKING([for LFS support])
  AC_CACHE_VAL(glibcxx_cv_LFS, [
    AC_TRY_LINK(
      [#include <unistd.h>
       #include <stdio.h>
       #include <sys/stat.h>
      ],
      [FILE* fp;
       fopen64("t", "w");
       fseeko64(fp, 0, SEEK_CUR);
       ftello64(fp);
       lseek64(1, 0, SEEK_CUR);
       struct stat64 buf;
       fstat64(1, &buf);],
      [glibcxx_cv_LFS=yes],
      [glibcxx_cv_LFS=no])
  ])
  if test $glibcxx_cv_LFS = yes; then
    AC_DEFINE(_GLIBCXX_USE_LFS, 1, [Define if LFS support is available.])
  fi
  AC_MSG_RESULT($glibcxx_cv_LFS)
  CXXFLAGS="$ac_save_CXXFLAGS"
  AC_LANG_RESTORE
])


dnl
dnl Check for whether a fully dynamic basic_string implementation should
dnl be turned on, that does not put empty objects in per-process static
dnl memory (mostly useful together with shared memory allocators, see PR
dnl libstdc++/16612 for details).
dnl
dnl --enable-fully-dynamic-string defines _GLIBCXX_FULLY_DYNAMIC_STRING
dnl --disable-fully-dynamic-string leaves _GLIBCXX_FULLY_DYNAMIC_STRING undefined
dnl  +  Usage:  GLIBCXX_ENABLE_FULLY_DYNAMIC_STRING[(DEFAULT)]
dnl       Where DEFAULT is either `yes' or `no'.
dnl
AC_DEFUN([GLIBCXX_ENABLE_FULLY_DYNAMIC_STRING], [
  GLIBCXX_ENABLE(fully-dynamic-string,$1,,[do not put empty strings in per-process static memory])
  if test $enable_fully_dynamic_string = yes; then
    AC_DEFINE(_GLIBCXX_FULLY_DYNAMIC_STRING, 1,
              [Define if a fully dynamic basic_string is wanted.])
  fi
])


dnl
dnl Does any necessary configuration of the testsuite directory.  Generates
dnl the testsuite_hooks.h header.
dnl
dnl GLIBCXX_ENABLE_SYMVERS and GLIBCXX_IS_NATIVE must be done before this.
dnl
dnl Sets:
dnl  enable_abi_check 
dnl  GLIBCXX_TEST_WCHAR_T
dnl  GLIBCXX_TEST_THREAD
dnl Substs:
dnl  baseline_dir
dnl
AC_DEFUN([GLIBCXX_CONFIGURE_TESTSUITE], [
  if $GLIBCXX_IS_NATIVE ; then
    # Do checks for resource limit functions.
    GLIBCXX_CHECK_SETRLIMIT

    # Look for setenv, so that extended locale tests can be performed.
    GLIBCXX_CHECK_STDLIB_DECL_AND_LINKAGE_3(setenv)
  fi

  if $GLIBCXX_IS_NATIVE && test $is_hosted = yes &&
     test $enable_symvers != no; then
    case "$host" in
      *-*-cygwin*)
        enable_abi_check=no ;;
      *)
        enable_abi_check=yes ;;
    esac
  else
    # Only build this as native, since automake does not understand
    # CXX_FOR_BUILD.
    enable_abi_check=no
  fi
  
  # Export file names for ABI checking.
  baseline_dir="$glibcxx_srcdir/config/abi/post/${abi_baseline_pair}\$(MULTISUBDIR)"
  AC_SUBST(baseline_dir)
])


dnl
dnl Set up *_INCLUDES variables for all sundry Makefile.am's.
dnl
dnl Substs:
dnl  GLIBCXX_INCLUDES
dnl  TOPLEVEL_INCLUDES
dnl
AC_DEFUN([GLIBCXX_EXPORT_INCLUDES], [
  # Used for every C++ compile we perform.
  GLIBCXX_INCLUDES="\
-I$glibcxx_builddir/include/$host_alias \
-I$glibcxx_builddir/include \
-I$glibcxx_srcdir/libsupc++"

  # For Canadian crosses, pick this up too.
  if test $CANADIAN = yes; then
    GLIBCXX_INCLUDES="$GLIBCXX_INCLUDES -I\${includedir}"
  fi

  # Stuff in the actual top level.  Currently only used by libsupc++ to
  # get unwind* headers from the gcc dir.
  #TOPLEVEL_INCLUDES='-I$(toplevel_srcdir)/gcc -I$(toplevel_srcdir)/include'
  TOPLEVEL_INCLUDES='-I$(toplevel_srcdir)/gcc'

  # Now, export this to all the little Makefiles....
  AC_SUBST(GLIBCXX_INCLUDES)
  AC_SUBST(TOPLEVEL_INCLUDES)
])


dnl
dnl Set up *_FLAGS and *FLAGS variables for all sundry Makefile.am's.
dnl (SECTION_FLAGS is done under CHECK_COMPILER_FEATURES.)
dnl
dnl Substs:
dnl  OPTIMIZE_CXXFLAGS
dnl  WARN_FLAGS
dnl
AC_DEFUN([GLIBCXX_EXPORT_FLAGS], [
  # Optimization flags that are probably a good idea for thrill-seekers. Just
  # uncomment the lines below and make, everything else is ready to go...
  # OPTIMIZE_CXXFLAGS = -O3 -fstrict-aliasing -fvtable-gc
  OPTIMIZE_CXXFLAGS=
  AC_SUBST(OPTIMIZE_CXXFLAGS)

  WARN_FLAGS='-Wall -Wextra -Wwrite-strings -Wcast-qual'
  AC_SUBST(WARN_FLAGS)
])


dnl
dnl All installation directory information is determined here.
dnl
dnl Substs:
dnl  gxx_install_dir
dnl  glibcxx_prefixdir
dnl  glibcxx_toolexecdir
dnl  glibcxx_toolexeclibdir
dnl
dnl Assumes cross_compiling bits already done, and with_cross_host in
dnl particular.
dnl
dnl This logic must match gcc/configure.ac's setting of gcc_gxx_include_dir.
dnl config/gxx-include-dir.m4 must be kept consistant with this as well.
AC_DEFUN([GLIBCXX_EXPORT_INSTALL_INFO], [
  glibcxx_toolexecdir=no
  glibcxx_toolexeclibdir=no
  glibcxx_prefixdir=$prefix

  AC_MSG_CHECKING([for gxx-include-dir])
  AC_ARG_WITH([gxx-include-dir],
    AC_HELP_STRING([--with-gxx-include-dir=DIR],
                   [installation directory for include files]),
    [case "$withval" in
      yes) AC_MSG_ERROR([Missing directory for --with-gxx-include-dir]) ;;
      no)  gxx_include_dir=no ;;
      *)   gxx_include_dir=$withval ;;
     esac],
    [gxx_include_dir=no])
  AC_MSG_RESULT($gxx_include_dir)

  AC_MSG_CHECKING([for --enable-version-specific-runtime-libs])
  AC_ARG_ENABLE([version-specific-runtime-libs],
    AC_HELP_STRING([--enable-version-specific-runtime-libs],
                   [Specify that runtime libraries should be installed in a compiler-specific directory]),
    [case "$enableval" in
      yes) version_specific_libs=yes ;;
      no)  version_specific_libs=no ;;
      *)   AC_MSG_ERROR([Unknown argument to enable/disable version-specific libs]);;
     esac],
    [version_specific_libs=no])
  AC_MSG_RESULT($version_specific_libs)

  # Default case for install directory for include files.
  if test $version_specific_libs = no && test $gxx_include_dir = no; then
    gxx_include_dir='include/c++/${gcc_version}'
    if test -n "$with_cross_host" && 
       test x"$with_cross_host" != x"no"; then	
      gxx_include_dir='${prefix}/${target_alias}/'"$gxx_include_dir"
    else
      gxx_include_dir='${prefix}/'"$gxx_include_dir"
    fi
  fi

  # Version-specific runtime libs processing.
  if test $version_specific_libs = yes; then
    # Need the gcc compiler version to know where to install libraries
    # and header files if --enable-version-specific-runtime-libs option
    # is selected.  FIXME: these variables are misnamed, there are
    # no executables installed in _toolexecdir or _toolexeclibdir.
    if test x"$gxx_include_dir" = x"no"; then
      gxx_include_dir='${libdir}/gcc/${host_alias}/${gcc_version}/include/c++'
    fi
    glibcxx_toolexecdir='${libdir}/gcc/${host_alias}'
    glibcxx_toolexeclibdir='${toolexecdir}/${gcc_version}$(MULTISUBDIR)'
  fi

  # Calculate glibcxx_toolexecdir, glibcxx_toolexeclibdir
  # Install a library built with a cross compiler in tooldir, not libdir.
  if test x"$glibcxx_toolexecdir" = x"no"; then
    if test -n "$with_cross_host" &&
       test x"$with_cross_host" != x"no"; then
      glibcxx_toolexecdir='${exec_prefix}/${host_alias}'
      glibcxx_toolexeclibdir='${toolexecdir}/lib'
    else
      glibcxx_toolexecdir='${libdir}/gcc/${host_alias}'
      glibcxx_toolexeclibdir='${libdir}'
    fi
    multi_os_directory=`$CXX -print-multi-os-directory`
    case $multi_os_directory in
      .) ;; # Avoid trailing /.
      *) glibcxx_toolexeclibdir=$glibcxx_toolexeclibdir/$multi_os_directory ;;
    esac
  fi

  AC_MSG_CHECKING([for install location])
  AC_MSG_RESULT($gxx_include_dir)

  AC_SUBST(glibcxx_prefixdir)
  AC_SUBST(gxx_include_dir)
  AC_SUBST(glibcxx_toolexecdir)
  AC_SUBST(glibcxx_toolexeclibdir)
])


dnl
dnl GLIBCXX_ENABLE
dnl    (FEATURE, DEFAULT, HELP-ARG, HELP-STRING)
dnl    (FEATURE, DEFAULT, HELP-ARG, HELP-STRING, permit a|b|c)
dnl    (FEATURE, DEFAULT, HELP-ARG, HELP-STRING, SHELL-CODE-HANDLER)
dnl
dnl See docs/html/17_intro/configury.html#enable for documentation.
dnl
m4_define([GLIBCXX_ENABLE],[dnl
m4_define([_g_switch],[--enable-$1])dnl
m4_define([_g_help],[AC_HELP_STRING(_g_switch$3,[$4 @<:@default=$2@:>@])])dnl
 AC_ARG_ENABLE($1,_g_help,
  m4_bmatch([$5],
   [^permit ],
     [[
      case "$enableval" in
       m4_bpatsubst([$5],[permit ])) ;;
       *) AC_MSG_ERROR(Unknown argument to enable/disable $1) ;;
          dnl Idea for future:  generate a URL pointing to
          dnl "onlinedocs/configopts.html#whatever"
      esac
     ]],
   [^$],
     [[
      case "$enableval" in
       yes|no) ;;
       *) AC_MSG_ERROR(Argument to enable/disable $1 must be yes or no) ;;
      esac
     ]],
   [[$5]]),
  [enable_]m4_bpatsubst([$1],-,_)[=][$2])
m4_undefine([_g_switch])dnl
m4_undefine([_g_help])dnl
])


dnl
dnl Check for ISO/IEC 9899:1999 "C99" support.
dnl
dnl --enable-c99 defines _GLIBCXX_USE_C99
dnl --disable-c99 leaves _GLIBCXX_USE_C99 undefined
dnl  +  Usage:  GLIBCXX_ENABLE_C99[(DEFAULT)]
dnl       Where DEFAULT is either `yes' or `no'.
dnl  +  If 'C99' stuff is not available, ignores DEFAULT and sets `no'.
dnl
AC_DEFUN([GLIBCXX_ENABLE_C99], [
  GLIBCXX_ENABLE(c99,$1,,[turns on ISO/IEC 9899:1999 support])

  if test x"$enable_c99" = x"yes"; then

  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS

  # Check for the existence of <math.h> functions used if C99 is enabled.
  AC_MSG_CHECKING([for ISO C99 support in <math.h>])
  AC_CACHE_VAL(ac_c99_math, [
  AC_TRY_COMPILE([#include <math.h>],
	         [fpclassify(0.0);
	          isfinite(0.0); 
		  isinf(0.0);
	          isnan(0.0);
		  isnormal(0.0);
	  	  signbit(0.0);
	 	  isgreater(0.0,0.0);
		  isgreaterequal(0.0,0.0);
		  isless(0.0,0.0);
		  islessequal(0.0,0.0);
		  islessgreater(0.0,0.0);
		  islessgreater(0.0,0.0);
		  isunordered(0.0,0.0);
		 ],[ac_c99_math=yes], [ac_c99_math=no])
  ])
  AC_MSG_RESULT($ac_c99_math)
  if test x"$ac_c99_math" = x"yes"; then
    AC_DEFINE(_GLIBCXX_USE_C99_MATH, 1,
              [Define if C99 functions or macros in <math.h> should be imported
              in <cmath> in namespace std.])
  fi

  # Check for the existence of <complex.h> complex math functions.
  # This is necessary even though libstdc++ uses the builtin versions
  # of these functions, because if the builtin cannot be used, a reference
  # to the library function is emitted.
  AC_CHECK_HEADERS(complex.h, ac_has_complex_h=yes, ac_has_complex_h=no)
  ac_c99_complex=no;
  if test x"$ac_has_complex_h" = x"yes"; then
    AC_MSG_CHECKING([for ISO C99 support in <complex.h>])
    AC_TRY_COMPILE([#include <complex.h>],
	           [typedef __complex__ float float_type; float_type tmpf;
	            cabsf(tmpf);
		    cargf(tmpf);
		    ccosf(tmpf);
  		    ccoshf(tmpf);
		    cexpf(tmpf);
	            clogf(tmpf);
		    csinf(tmpf);
		    csinhf(tmpf);
		    csqrtf(tmpf);
		    ctanf(tmpf);
		    ctanhf(tmpf);
		    cpowf(tmpf, tmpf);
		    typedef __complex__ double double_type; double_type tmpd;
	            cabs(tmpd);
		    carg(tmpd);
		    ccos(tmpd);
  		    ccosh(tmpd);
		    cexp(tmpd);
	            clog(tmpd);
		    csin(tmpd);
		    csinh(tmpd);
		    csqrt(tmpd);
		    ctan(tmpd);
		    ctanh(tmpd);
		    cpow(tmpd, tmpd);
		    typedef __complex__ long double ld_type; ld_type tmpld;
	            cabsl(tmpld);
		    cargl(tmpld);
		    ccosl(tmpld);
  		    ccoshl(tmpld);
		    cexpl(tmpld);
	            clogl(tmpld);
		    csinl(tmpld);
		    csinhl(tmpld);
		    csqrtl(tmpld);
		    ctanl(tmpld);
		    ctanhl(tmpld);
		    cpowl(tmpld, tmpld);
		   ],[ac_c99_complex=yes], [ac_c99_complex=no])
  fi
  AC_MSG_RESULT($ac_c99_complex)
  if test x"$ac_c99_complex" = x"yes"; then
    AC_DEFINE(_GLIBCXX_USE_C99_COMPLEX, 1,
              [Define if C99 functions in <complex.h> should be used in
              <complex>. Using compiler builtins for these functions requires
              corresponding C99 library functions to be present.])
  fi

  # Check for the existence in <stdio.h> of vscanf, et. al.
  AC_MSG_CHECKING([for ISO C99 support in <stdio.h>])
  AC_CACHE_VAL(ac_c99_stdio, [
  AC_TRY_COMPILE([#include <stdio.h>
		  #include <stdarg.h>
                  void foo(char* fmt, ...)
                  {
	            va_list args; va_start(args, fmt);
                    vfscanf(stderr, "%i", args); 
		    vscanf("%i", args);
                    vsnprintf(fmt, 0, "%i", args);
                    vsscanf(fmt, "%i", args);
		  }],
                 [snprintf("12", 0, "%i");],
		 [ac_c99_stdio=yes], [ac_c99_stdio=no])
  ])
  AC_MSG_RESULT($ac_c99_stdio)

  # Check for the existence in <stdlib.h> of lldiv_t, et. al.
  AC_MSG_CHECKING([for ISO C99 support in <stdlib.h>])
  AC_CACHE_VAL(ac_c99_stdlib, [
  AC_TRY_COMPILE([#include <stdlib.h>],
                 [char* tmp;
	    	  strtof("gnu", &tmp);
		  strtold("gnu", &tmp);
	          strtoll("gnu", &tmp, 10);
	          strtoull("gnu", &tmp, 10);
	          llabs(10);
		  lldiv(10,1);
		  atoll("10");
		  _Exit(0);
		  lldiv_t mydivt;],[ac_c99_stdlib=yes], [ac_c99_stdlib=no])
  ])
  AC_MSG_RESULT($ac_c99_stdlib)

  # Check for the existence in <wchar.h> of wcstold, etc.
  ac_c99_wchar=no;
  if test x"$ac_has_wchar_h" = xyes &&
     test x"$ac_has_wctype_h" = xyes; then
    AC_MSG_CHECKING([for ISO C99 support in <wchar.h>])	
    AC_TRY_COMPILE([#include <wchar.h>
                    namespace test
                    {
		      using ::wcstold;
		      using ::wcstoll;
		      using ::wcstoull;
		    }
		   ],[],[ac_c99_wchar=yes], [ac_c99_wchar=no])

    # Checks for wide character functions that may not be present.
    # Injection of these is wrapped with guard macros.
    # NB: only put functions here, instead of immediately above, if
    # absolutely necessary.
    AC_TRY_COMPILE([#include <wchar.h>
                    namespace test { using ::vfwscanf; } ], [],
 	    	   [AC_DEFINE(HAVE_VFWSCANF,1,
			[Defined if vfwscanf exists.])],[])

    AC_TRY_COMPILE([#include <wchar.h>
                    namespace test { using ::vswscanf; } ], [],
 	    	   [AC_DEFINE(HAVE_VSWSCANF,1,
			[Defined if vswscanf exists.])],[])

    AC_TRY_COMPILE([#include <wchar.h>
                    namespace test { using ::vwscanf; } ], [],
 	    	   [AC_DEFINE(HAVE_VWSCANF,1,[Defined if vwscanf exists.])],[])

    AC_TRY_COMPILE([#include <wchar.h>
                    namespace test { using ::wcstof; } ], [],
 	    	   [AC_DEFINE(HAVE_WCSTOF,1,[Defined if wcstof exists.])],[])

    AC_TRY_COMPILE([#include <wctype.h>],
                   [ wint_t t; int i = iswblank(t);], 
 	    	   [AC_DEFINE(HAVE_ISWBLANK,1,
			[Defined if iswblank exists.])],[])

    AC_MSG_RESULT($ac_c99_wchar)
  fi

  # Option parsed, now set things appropriately.
  if test x"$ac_c99_math" = x"no" ||
     test x"$ac_c99_complex" = x"no" ||
     test x"$ac_c99_stdio" = x"no" ||
     test x"$ac_c99_stdlib" = x"no" ||
     test x"$ac_c99_wchar" = x"no"; then
    enable_c99=no;
  else
    AC_DEFINE(_GLIBCXX_USE_C99, 1,
    [Define if C99 functions or macros from <wchar.h>, <math.h>,
    <complex.h>, <stdio.h>, and <stdlib.h> can be used or exposed.])
  fi

  AC_LANG_RESTORE
  fi	

  AC_MSG_CHECKING([for fully enabled ISO C99 support])
  AC_MSG_RESULT($enable_c99)
])


dnl
dnl Check for ISO/IEC 9899:1999 "C99" support to ISO/IEC DTR 19768 "TR1"
dnl facilities in Chapter 8, "C compatibility".
dnl
AC_DEFUN([GLIBCXX_CHECK_C99_TR1], [

  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS

  # Check for the existence of <complex.h> complex math functions used
  # by tr1/complex.
  AC_CHECK_HEADERS(complex.h, ac_has_complex_h=yes, ac_has_complex_h=no)
  ac_c99_complex_tr1=no;
  if test x"$ac_has_complex_h" = x"yes"; then
    AC_MSG_CHECKING([for ISO C99 support to TR1 in <complex.h>])
    AC_TRY_COMPILE([#include <complex.h>],
	           [typedef __complex__ float float_type; float_type tmpf;
	            cacosf(tmpf);
	            casinf(tmpf);
	            catanf(tmpf);
	            cacoshf(tmpf);
	            casinhf(tmpf);
	            catanhf(tmpf);
		    typedef __complex__ double double_type; double_type tmpd;
	            cacos(tmpd);
	            casin(tmpd);
	            catan(tmpd);
	            cacosh(tmpd);
	            casinh(tmpd);
	            catanh(tmpd);
		    typedef __complex__ long double ld_type; ld_type tmpld;
	            cacosl(tmpld);
	            casinl(tmpld);
	            catanl(tmpld);
	            cacoshl(tmpld);
	            casinhl(tmpld);
	            catanhl(tmpld);
		   ],[ac_c99_complex_tr1=yes], [ac_c99_complex_tr1=no])
  fi
  AC_MSG_RESULT($ac_c99_complex_tr1)
  if test x"$ac_c99_complex_tr1" = x"yes"; then
    AC_DEFINE(_GLIBCXX_USE_C99_COMPLEX_TR1, 1,
              [Define if C99 functions in <complex.h> should be used in
              <tr1/complex>. Using compiler builtins for these functions
	      requires corresponding C99 library functions to be present.])
  fi

  # Check for the existence of <ctype.h> functions.
  AC_MSG_CHECKING([for ISO C99 support to TR1 in <ctype.h>])
  AC_CACHE_VAL(ac_c99_ctype_tr1, [
  AC_TRY_COMPILE([#include <ctype.h>],
	         [int ch;
	          int ret;
	          ret = isblank(ch);
		 ],[ac_c99_ctype_tr1=yes], [ac_c99_ctype_tr1=no])
  ])
  AC_MSG_RESULT($ac_c99_ctype_tr1)
  if test x"$ac_c99_ctype_tr1" = x"yes"; then
    AC_DEFINE(_GLIBCXX_USE_C99_CTYPE_TR1, 1,
              [Define if C99 functions in <ctype.h> should be imported in
	      <tr1/cctype> in namespace std::tr1.])
  fi

  # Check for the existence of <fenv.h> functions.
  AC_CHECK_HEADERS(fenv.h, ac_has_fenv_h=yes, ac_has_fenv_h=no)
  ac_c99_fenv_tr1=no;
  if test x"$ac_has_fenv_h" = x"yes"; then
    AC_MSG_CHECKING([for ISO C99 support to TR1 in <fenv.h>])
    AC_TRY_COMPILE([#include <fenv.h>],
	           [int except, mode;
	            fexcept_t* pflag;
                    fenv_t* penv;
	            int ret;
	            ret = feclearexcept(except);
	            ret = fegetexceptflag(pflag, except);
	            ret = feraiseexcept(except);
	            ret = fesetexceptflag(pflag, except);
	            ret = fetestexcept(except);
	            ret = fegetround();
	            ret = fesetround(mode);
	            ret = fegetenv(penv);
	            ret = feholdexcept(penv);
	            ret = fesetenv(penv);
	            ret = feupdateenv(penv);
		   ],[ac_c99_fenv_tr1=yes], [ac_c99_fenv_tr1=no])
  fi
  AC_MSG_RESULT($ac_c99_fenv_tr1)
  if test x"$ac_c99_fenv_tr1" = x"yes"; then
    AC_DEFINE(_GLIBCXX_USE_C99_FENV_TR1, 1,
              [Define if C99 functions in <fenv.h> should be imported in
	      <tr1/cfenv> in namespace std::tr1.])
  fi

  # Check for the existence of <stdint.h> types.
  AC_MSG_CHECKING([for ISO C99 support to TR1 in <stdint.h>])
  AC_CACHE_VAL(ac_c99_stdint_tr1, [
  AC_TRY_COMPILE([#include <stdint.h>],
	         [typedef int8_t          my_int8_t;
	          typedef int16_t         my_int16_t;
	          typedef int32_t         my_int32_t;
	          typedef int64_t         my_int64_t;
	          typedef int_fast8_t     my_int_fast8_t;
	          typedef int_fast16_t    my_int_fast16_t;
	          typedef int_fast32_t    my_int_fast32_t;
	          typedef int_fast64_t    my_int_fast64_t;	
	          typedef int_least8_t    my_int_least8_t;
	          typedef int_least16_t   my_int_least16_t;
	          typedef int_least32_t   my_int_least32_t;
	          typedef int_least64_t   my_int_least64_t;
		  typedef intmax_t        my_intmax_t;
		  typedef intptr_t        my_intptr_t;
	          typedef uint8_t         my_uint8_t;
	          typedef uint16_t        my_uint16_t;
	          typedef uint32_t        my_uint32_t;
	          typedef uint64_t        my_uint64_t;
	          typedef uint_fast8_t    my_uint_fast8_t;
	          typedef uint_fast16_t   my_uint_fast16_t;
	          typedef uint_fast32_t   my_uint_fast32_t;
	          typedef uint_fast64_t   my_uint_fast64_t;	
	          typedef uint_least8_t   my_uint_least8_t;
	          typedef uint_least16_t  my_uint_least16_t;
	          typedef uint_least32_t  my_uint_least32_t;
	          typedef uint_least64_t  my_uint_least64_t;
		  typedef uintmax_t       my_uintmax_t;
		  typedef uintptr_t       my_uintptr_t;
		 ],[ac_c99_stdint_tr1=yes], [ac_c99_stdint_tr1=no])
  ])
  AC_MSG_RESULT($ac_c99_stdint_tr1)
  if test x"$ac_c99_stdint_tr1" = x"yes"; then
    AC_DEFINE(_GLIBCXX_USE_C99_STDINT_TR1, 1,
              [Define if C99 types in <stdint.h> should be imported in
	      <tr1/cstdint> in namespace std::tr1.])
  fi

  # Check for the existence of <math.h> functions.
  AC_MSG_CHECKING([for ISO C99 support to TR1 in <math.h>])
  AC_CACHE_VAL(ac_c99_math_tr1, [
  AC_TRY_COMPILE([#include <math.h>],
	         [typedef double_t  my_double_t;
	          typedef float_t   my_float_t;
	          acosh(0.0);
	          acoshf(0.0f);
	          acoshl(0.0l);
	          asinh(0.0);
	          asinhf(0.0f);
	          asinhl(0.0l);
	          atanh(0.0);
	          atanhf(0.0f);
	          atanhl(0.0l);
	          cbrt(0.0);
	          cbrtf(0.0f);
	          cbrtl(0.0l);
	          copysign(0.0, 0.0);
	          copysignf(0.0f, 0.0f);
	          copysignl(0.0l, 0.0l);
	          erf(0.0);
	          erff(0.0f);
	          erfl(0.0l);
	          erfc(0.0);
	          erfcf(0.0f);
	          erfcl(0.0l);
	          exp2(0.0);
	          exp2f(0.0f);
	          exp2l(0.0l);
	          expm1(0.0);
	          expm1f(0.0f);
	          expm1l(0.0l);
	          fdim(0.0, 0.0);
	          fdimf(0.0f, 0.0f);
	          fdiml(0.0l, 0.0l);
	          fma(0.0, 0.0, 0.0);
	          fmaf(0.0f, 0.0f, 0.0f);
	          fmal(0.0l, 0.0l, 0.0l);
	          fmax(0.0, 0.0);
	          fmaxf(0.0f, 0.0f);
	          fmaxl(0.0l, 0.0l);
	          fmin(0.0, 0.0);
	          fminf(0.0f, 0.0f);
	          fminl(0.0l, 0.0l);
	          hypot(0.0, 0.0);
	          hypotf(0.0f, 0.0f);
	          hypotl(0.0l, 0.0l);
	          ilogb(0.0);
	          ilogbf(0.0f);
	          ilogbl(0.0l);
	          lgamma(0.0);
	          lgammaf(0.0f);
	          lgammal(0.0l);
	          llrint(0.0);
	          llrintf(0.0f);
	          llrintl(0.0l);
	          llround(0.0);
	          llroundf(0.0f);
	          llroundl(0.0l);
	          log1p(0.0);
	          log1pf(0.0f);
	          log1pl(0.0l);
	          log2(0.0);
	          log2f(0.0f);
	          log2l(0.0l);
	          logb(0.0);
	          logbf(0.0f);
	          logbl(0.0l);
	          lrint(0.0);
	          lrintf(0.0f);
	          lrintl(0.0l);
	          lround(0.0);
	          lroundf(0.0f);
	          lroundl(0.0l);
	          nan(0);
	          nanf(0);
	          nanl(0);
	          nearbyint(0.0);
	          nearbyintf(0.0f);
	          nearbyintl(0.0l);
	          nextafter(0.0, 0.0);
	          nextafterf(0.0f, 0.0f);
	          nextafterl(0.0l, 0.0l);
	          nexttoward(0.0, 0.0);
	          nexttowardf(0.0f, 0.0f);
	          nexttowardl(0.0l, 0.0l);
	          remainder(0.0, 0.0);
	          remainderf(0.0f, 0.0f);
	          remainderl(0.0l, 0.0l);
	          remquo(0.0, 0.0, 0);
	          remquo(0.0f, 0.0f, 0);
	          remquo(0.0l, 0.0l, 0);
	          rint(0.0);
	          rintf(0.0f);
	          rintl(0.0l);
	          round(0.0);
	          roundf(0.0f);
	          roundl(0.0l);
	          scalbln(0.0, 0l);
	          scalblnf(0.0f, 0l);
	          scalblnl(0.0l, 0l);
	          scalbn(0.0, 0);
	          scalbnf(0.0f, 0);
	          scalbnl(0.0l, 0);
	          tgamma(0.0);
	          tgammaf(0.0f);
	          tgammal(0.0l);
	          trunc(0.0);
	          truncf(0.0f);
	          truncl(0.0l);
		 ],[ac_c99_math_tr1=yes], [ac_c99_math_tr1=no])
  ])
  AC_MSG_RESULT($ac_c99_math_tr1)
  if test x"$ac_c99_math_tr1" = x"yes"; then
    AC_DEFINE(_GLIBCXX_USE_C99_MATH_TR1, 1,
              [Define if C99 functions or macros in <math.h> should be imported
              in <tr1/cmath> in namespace std::tr1.])
  fi

  # Check for the existence of <inttypes.h> functions (NB: doesn't make
  # sense if the previous check fails, per C99, 7.8/1).
  ac_c99_inttypes_tr1=no;
  if test x"$ac_c99_stdint_tr1" = x"yes"; then
    AC_MSG_CHECKING([for ISO C99 support to TR1 in <inttypes.h>])
    AC_TRY_COMPILE([#include <inttypes.h>],
	           [intmax_t i, numer, denom, base;
	            const char* s;
	            char** endptr;
	            intmax_t ret = imaxabs(i);
	            imaxdiv_t dret = imaxdiv(numer, denom);
	            ret = strtoimax(s, endptr, base);
	            uintmax_t uret = strtoumax(s, endptr, base);
        	   ],[ac_c99_inttypes_tr1=yes], [ac_c99_inttypes_tr1=no])
  fi
  AC_MSG_RESULT($ac_c99_inttypes_tr1)
  if test x"$ac_c99_inttypes_tr1" = x"yes"; then
    AC_DEFINE(_GLIBCXX_USE_C99_INTTYPES_TR1, 1,
              [Define if C99 functions in <inttypes.h> should be imported in
              <tr1/cinttypes> in namespace std::tr1.])
  fi

  # Check for the existence of the <stdbool.h> header.	
  AC_CHECK_HEADERS(stdbool.h)

  AC_LANG_RESTORE
])

dnl
dnl Check whether "dev/random" and "dev/urandom" are available for the
dnl random_device of "TR1" (Chapter 5.1, "Random number generation").
dnl
AC_DEFUN([GLIBCXX_CHECK_RANDOM_TR1], [

  AC_MSG_CHECKING([for "dev/random" and "dev/urandom" for TR1 random_device])
  AC_CACHE_VAL(ac_random_tr1, [
  AC_TRY_RUN([#include <stdio.h>
	      int main()
	      {
                return !(fopen("/dev/random", "r")
                         && fopen("/dev/urandom", "r"));
	      }	      
	     ],
             [ac_random_tr1=yes], [ac_random_tr1=no],
	     [ac_random_tr1=no])
  ])
  AC_MSG_RESULT($ac_random_tr1)
  if test x"$ac_random_tr1" = x"yes"; then
    AC_DEFINE(_GLIBCXX_USE_RANDOM_TR1, 1,
              [Define if dev/random and dev/urandom are available for
	       the random_device of TR1 (Chapter 5.1).])
  fi

])

dnl
dnl Check for what type of C headers to use.
dnl
dnl --enable-cheaders= [does stuff].
dnl --disable-cheaders [does not do anything, really].
dnl  +  Usage:  GLIBCXX_ENABLE_CHEADERS[(DEFAULT)]
dnl       Where DEFAULT is either `c' or `c_std'.
dnl
AC_DEFUN([GLIBCXX_ENABLE_CHEADERS], [
  GLIBCXX_ENABLE(cheaders,$1,[=KIND],
    [construct "C" headers for g++], [permit c|c_std])
  AC_MSG_NOTICE("C" header strategy set to $enable_cheaders)

  C_INCLUDE_DIR='${glibcxx_srcdir}/include/'$enable_cheaders

  AC_SUBST(C_INCLUDE_DIR)
  GLIBCXX_CONDITIONAL(GLIBCXX_C_HEADERS_C, test $enable_cheaders = c)
  GLIBCXX_CONDITIONAL(GLIBCXX_C_HEADERS_C_STD, test $enable_cheaders = c_std)
  GLIBCXX_CONDITIONAL(GLIBCXX_C_HEADERS_COMPATIBILITY, test $c_compatibility = yes)
])


dnl
dnl Check for which locale library to use.  The choice is mapped to
dnl a subdirectory of config/locale.
dnl
dnl Default is generic.
dnl
AC_DEFUN([GLIBCXX_ENABLE_CLOCALE], [
  GLIBCXX_ENABLE(clocale,auto,[@<:@=MODEL@:>@],
    [use MODEL for target locale package],
    [permit generic|gnu|ieee_1003.1-2001|yes|no|auto])

  # Deal with gettext issues.  Default to not using it (=no) until we detect
  # support for it later.  Let the user turn it off via --e/d, but let that
  # default to on for easier handling.
  USE_NLS=no
  AC_ARG_ENABLE(nls,
    AC_HELP_STRING([--enable-nls],[use Native Language Support (default)]),
    [],
    [enable_nls=yes])
  
  # Either a known packaage, or "auto"
  if test $enable_clocale = no || test $enable_clocale = yes; then
     enable_clocale=auto
  fi
  enable_clocale_flag=$enable_clocale

  # Probe for locale model to use if none specified.
  # Default to "generic".
  if test $enable_clocale_flag = auto; then
    case ${target_os} in
      linux* | gnu* | kfreebsd*-gnu | knetbsd*-gnu)
        enable_clocale_flag=gnu	
        ;;
      darwin* | freebsd*)
        enable_clocale_flag=darwin
	;;
      *)
        enable_clocale_flag=generic
        ;;
    esac
  fi

  # Sanity check model, and test for special functionality.
  if test $enable_clocale_flag = gnu; then
    AC_EGREP_CPP([_GLIBCXX_ok], [
    #include <features.h>
    #if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 2)
      _GLIBCXX_ok
    #endif
    ], enable_clocale_flag=gnu, enable_clocale_flag=generic)

    if test $enable_clocale = auto; then
      # Test for bugs early in glibc-2.2.x series
      AC_TRY_RUN([
      #define _GNU_SOURCE 1
      #include <locale.h>
      #include <string.h>
      #if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
      extern __typeof(newlocale) __newlocale;
      extern __typeof(duplocale) __duplocale;
      extern __typeof(strcoll_l) __strcoll_l;
      #endif
      int main()
      {
	const char __one[] = "Äuglein Augmen";
        const char __two[] = "Äuglein";
       	int i;
        int j;
        __locale_t        loc;
        __locale_t        loc_dup;
        loc = __newlocale(1 << LC_ALL, "de_DE", 0);
        loc_dup = __duplocale(loc);
        i = __strcoll_l(__one, __two, loc);
        j = __strcoll_l(__one, __two, loc_dup);
        return 0;
      }
      ],
      [enable_clocale_flag=gnu],[enable_clocale_flag=generic],
      [enable_clocale_flag=generic])
    fi

    # Set it to scream when it hurts.
    ac_save_CFLAGS="$CFLAGS"	
    CFLAGS="-Wimplicit-function-declaration -Werror"

    # Use strxfrm_l if available.
    AC_TRY_COMPILE([#define _GNU_SOURCE 1
     		    #include <string.h>
		    #include <locale.h>],
	            [char s[128]; __locale_t loc; strxfrm_l(s, "C", 5, loc);], 
                    AC_DEFINE(HAVE_STRXFRM_L, 1, 
                    [Define if strxfrm_l is available in <string.h>.]),)
    
    # Use strerror_l if available.
    AC_TRY_COMPILE([#define _GNU_SOURCE 1
		    #include <string.h>
		    #include <locale.h>],
	            [__locale_t loc; strerror_l(5, loc);], 
                    AC_DEFINE(HAVE_STRERROR_L, 1, 
                    [Define if strerror_l is available in <string.h>.]),)

    CFLAGS="$ac_save_CFLAGS"
  fi

  # Perhaps use strerror_r if available, and strerror_l isn't.
  ac_save_CFLAGS="$CFLAGS"	
  CFLAGS="-Wimplicit-function-declaration -Werror"
  AC_TRY_COMPILE([#define _GNU_SOURCE 1
	     	  #include <string.h>
		  #include <locale.h>],
	          [char s[128]; strerror_r(5, s, 128);], 
                  AC_DEFINE(HAVE_STRERROR_R, 1, 
                  [Define if strerror_r is available in <string.h>.]),)
  CFLAGS="$ac_save_CFLAGS"

  # Set configure bits for specified locale package
  AC_MSG_CHECKING([for C locale to use])
  case ${enable_clocale_flag} in
    generic)
      AC_MSG_RESULT(generic)

      CLOCALE_H=config/locale/generic/c_locale.h
      CLOCALE_CC=config/locale/generic/c_locale.cc
      CCODECVT_CC=config/locale/generic/codecvt_members.cc
      CCOLLATE_CC=config/locale/generic/collate_members.cc
      CCTYPE_CC=config/locale/generic/ctype_members.cc
      CMESSAGES_H=config/locale/generic/messages_members.h
      CMESSAGES_CC=config/locale/generic/messages_members.cc
      CMONEY_CC=config/locale/generic/monetary_members.cc
      CNUMERIC_CC=config/locale/generic/numeric_members.cc
      CTIME_H=config/locale/generic/time_members.h
      CTIME_CC=config/locale/generic/time_members.cc
      CLOCALE_INTERNAL_H=config/locale/generic/c++locale_internal.h
      ;;
    darwin)
      AC_MSG_RESULT(darwin or freebsd)

      CLOCALE_H=config/locale/generic/c_locale.h
      CLOCALE_CC=config/locale/generic/c_locale.cc
      CCODECVT_CC=config/locale/generic/codecvt_members.cc
      CCOLLATE_CC=config/locale/generic/collate_members.cc
      CCTYPE_CC=config/locale/darwin/ctype_members.cc
      CMESSAGES_H=config/locale/generic/messages_members.h
      CMESSAGES_CC=config/locale/generic/messages_members.cc
      CMONEY_CC=config/locale/generic/monetary_members.cc
      CNUMERIC_CC=config/locale/generic/numeric_members.cc
      CTIME_H=config/locale/generic/time_members.h
      CTIME_CC=config/locale/generic/time_members.cc
      CLOCALE_INTERNAL_H=config/locale/generic/c++locale_internal.h
      ;;
	
    gnu)
      AC_MSG_RESULT(gnu)

      # Declare intention to use gettext, and add support for specific
      # languages.
      # For some reason, ALL_LINGUAS has to be before AM-GNU-GETTEXT
      ALL_LINGUAS="de fr"

      # Don't call AM-GNU-GETTEXT here. Instead, assume glibc.
      AC_CHECK_PROG(check_msgfmt, msgfmt, yes, no)
      if test x"$check_msgfmt" = x"yes" && test x"$enable_nls" = x"yes"; then
        USE_NLS=yes
      fi
      # Export the build objects.
      for ling in $ALL_LINGUAS; do \
        glibcxx_MOFILES="$glibcxx_MOFILES $ling.mo"; \
        glibcxx_POFILES="$glibcxx_POFILES $ling.po"; \
      done
      AC_SUBST(glibcxx_MOFILES)
      AC_SUBST(glibcxx_POFILES)

      CLOCALE_H=config/locale/gnu/c_locale.h
      CLOCALE_CC=config/locale/gnu/c_locale.cc
      CCODECVT_CC=config/locale/gnu/codecvt_members.cc
      CCOLLATE_CC=config/locale/gnu/collate_members.cc
      CCTYPE_CC=config/locale/gnu/ctype_members.cc
      CMESSAGES_H=config/locale/gnu/messages_members.h
      CMESSAGES_CC=config/locale/gnu/messages_members.cc
      CMONEY_CC=config/locale/gnu/monetary_members.cc
      CNUMERIC_CC=config/locale/gnu/numeric_members.cc
      CTIME_H=config/locale/gnu/time_members.h
      CTIME_CC=config/locale/gnu/time_members.cc
      CLOCALE_INTERNAL_H=config/locale/gnu/c++locale_internal.h
      ;;
    ieee_1003.1-2001)
      AC_MSG_RESULT(IEEE 1003.1)

      CLOCALE_H=config/locale/ieee_1003.1-2001/c_locale.h
      CLOCALE_CC=config/locale/ieee_1003.1-2001/c_locale.cc
      CCODECVT_CC=config/locale/generic/codecvt_members.cc
      CCOLLATE_CC=config/locale/generic/collate_members.cc
      CCTYPE_CC=config/locale/generic/ctype_members.cc
      CMESSAGES_H=config/locale/ieee_1003.1-2001/messages_members.h
      CMESSAGES_CC=config/locale/ieee_1003.1-2001/messages_members.cc
      CMONEY_CC=config/locale/generic/monetary_members.cc
      CNUMERIC_CC=config/locale/generic/numeric_members.cc
      CTIME_H=config/locale/generic/time_members.h
      CTIME_CC=config/locale/generic/time_members.cc
      CLOCALE_INTERNAL_H=config/locale/generic/c++locale_internal.h
      ;;
  esac

  # This is where the testsuite looks for locale catalogs, using the
  # -DLOCALEDIR define during testsuite compilation.
  glibcxx_localedir=${glibcxx_builddir}/po/share/locale
  AC_SUBST(glibcxx_localedir)

  # A standalone libintl (e.g., GNU libintl) may be in use.
  if test $USE_NLS = yes; then
    AC_CHECK_HEADERS([libintl.h], [], USE_NLS=no)
    AC_SEARCH_LIBS(gettext, intl, [], USE_NLS=no)
  fi
  if test $USE_NLS = yes; then
    AC_DEFINE(_GLIBCXX_USE_NLS, 1, 
              [Define if NLS translations are to be used.])
  fi

  AC_SUBST(USE_NLS)
  AC_SUBST(CLOCALE_H)
  AC_SUBST(CMESSAGES_H)
  AC_SUBST(CCODECVT_CC)
  AC_SUBST(CCOLLATE_CC)
  AC_SUBST(CCTYPE_CC)
  AC_SUBST(CMESSAGES_CC)
  AC_SUBST(CMONEY_CC)
  AC_SUBST(CNUMERIC_CC)
  AC_SUBST(CTIME_H)
  AC_SUBST(CTIME_CC)
  AC_SUBST(CLOCALE_CC)
  AC_SUBST(CLOCALE_INTERNAL_H)
])


dnl
dnl Check for which std::allocator base class to use.  The choice is
dnl mapped from a subdirectory of include/ext.
dnl
dnl Default is new.
dnl
AC_DEFUN([GLIBCXX_ENABLE_ALLOCATOR], [
  AC_MSG_CHECKING([for std::allocator base class])
  GLIBCXX_ENABLE(libstdcxx-allocator,auto,[=KIND],
    [use KIND for target std::allocator base],
    [permit new|malloc|mt|bitmap|pool|yes|no|auto])

  # If they didn't use this option switch, or if they specified --enable
  # with no specific model, we'll have to look for one.  If they
  # specified --disable (???), do likewise.
  if test $enable_libstdcxx_allocator = no ||
     test $enable_libstdcxx_allocator = yes;
  then
     enable_libstdcxx_allocator=auto
  fi

  # Either a known package, or "auto". Auto implies the default choice
  # for a particular platform.
  enable_libstdcxx_allocator_flag=$enable_libstdcxx_allocator

  # Probe for host-specific support if no specific model is specified.
  # Default to "new".
  if test $enable_libstdcxx_allocator_flag = auto; then
    case ${target_os} in
      linux* | gnu* | kfreebsd*-gnu | knetbsd*-gnu)
        enable_libstdcxx_allocator_flag=new
        ;;
      *)
        enable_libstdcxx_allocator_flag=new
        ;;
    esac
  fi
  AC_MSG_RESULT($enable_libstdcxx_allocator_flag)
  

  # Set configure bits for specified locale package
  case ${enable_libstdcxx_allocator_flag} in
    bitmap)
      ALLOCATOR_H=config/allocator/bitmap_allocator_base.h
      ALLOCATOR_NAME=__gnu_cxx::bitmap_allocator
      ;;
    malloc)
      ALLOCATOR_H=config/allocator/malloc_allocator_base.h
      ALLOCATOR_NAME=__gnu_cxx::malloc_allocator
      ;;
    mt)
      ALLOCATOR_H=config/allocator/mt_allocator_base.h
      ALLOCATOR_NAME=__gnu_cxx::__mt_alloc
      ;;
    new)
      ALLOCATOR_H=config/allocator/new_allocator_base.h
      ALLOCATOR_NAME=__gnu_cxx::new_allocator
      ;;
    pool)
      ALLOCATOR_H=config/allocator/pool_allocator_base.h
      ALLOCATOR_NAME=__gnu_cxx::__pool_alloc
      ;;	
  esac

  AC_SUBST(ALLOCATOR_H)
  AC_SUBST(ALLOCATOR_NAME)
])


dnl
dnl Check for whether the Boost-derived checks should be turned on.
dnl
dnl --enable-concept-checks turns them on.
dnl --disable-concept-checks leaves them off.
dnl  +  Usage:  GLIBCXX_ENABLE_CONCEPT_CHECKS[(DEFAULT)]
dnl       Where DEFAULT is either `yes' or `no'.
dnl
AC_DEFUN([GLIBCXX_ENABLE_CONCEPT_CHECKS], [
  GLIBCXX_ENABLE(concept-checks,$1,,[use Boost-derived template checks])
  if test $enable_concept_checks = yes; then
    AC_DEFINE(_GLIBCXX_CONCEPT_CHECKS, 1,
              [Define to use concept checking code from the boost libraries.])
  fi
])


dnl
dnl Check for which I/O library to use:  stdio, or something specific.
dnl
dnl Default is stdio.
dnl
AC_DEFUN([GLIBCXX_ENABLE_CSTDIO], [
  AC_MSG_CHECKING([for underlying I/O to use])
  GLIBCXX_ENABLE(cstdio,stdio,[=PACKAGE],
    [use target-specific I/O package], [permit stdio])

  # Now that libio has been removed, you can have any color you want as long
  # as it's black.  This is one big no-op until other packages are added, but
  # showing the framework never hurts.
  case ${enable_cstdio} in
    stdio)
      CSTDIO_H=config/io/c_io_stdio.h
      BASIC_FILE_H=config/io/basic_file_stdio.h
      BASIC_FILE_CC=config/io/basic_file_stdio.cc
      AC_MSG_RESULT(stdio)
      ;;
  esac

  AC_SUBST(CSTDIO_H)
  AC_SUBST(BASIC_FILE_H)
  AC_SUBST(BASIC_FILE_CC)
])


dnl
dnl Check for "unusual" flags to pass to the compiler while building.
dnl
dnl --enable-cxx-flags='-foo -bar -baz' is a general method for passing
dnl     experimental flags such as -fpch, -fIMI, -Dfloat=char, etc.
dnl --disable-cxx-flags passes nothing.
dnl  +  See http://gcc.gnu.org/ml/libstdc++/2000-q2/msg00131.html
dnl         http://gcc.gnu.org/ml/libstdc++/2000-q2/msg00284.html
dnl         http://gcc.gnu.org/ml/libstdc++/2000-q1/msg00035.html
dnl  +  Usage:  GLIBCXX_ENABLE_CXX_FLAGS(default flags)
dnl       If "default flags" is an empty string, the effect is the same
dnl       as --disable or --enable=no.
dnl
AC_DEFUN([GLIBCXX_ENABLE_CXX_FLAGS], [dnl
  AC_MSG_CHECKING([for extra compiler flags for building])
  GLIBCXX_ENABLE(cxx-flags,$1,[=FLAGS],
    [pass compiler FLAGS when building library],
    [case "x$enable_cxx_flags" in
      xno | x)   enable_cxx_flags= ;;
      x-*)       ;;
      *)         AC_MSG_ERROR(_g_switch needs compiler flags as arguments) ;;
     esac])

  # Run through flags (either default or command-line) and set anything
  # extra (e.g., #defines) that must accompany particular g++ options.
  if test -n "$enable_cxx_flags"; then
    for f in $enable_cxx_flags; do
      case "$f" in
        -fhonor-std)  ;;
        -*)  ;;
        *)   # and we're trying to pass /what/ exactly?
             AC_MSG_ERROR([compiler flags start with a -]) ;;
      esac
    done
  fi

  EXTRA_CXX_FLAGS="$enable_cxx_flags"
  AC_MSG_RESULT($EXTRA_CXX_FLAGS)
  AC_SUBST(EXTRA_CXX_FLAGS)
])


dnl
dnl Check to see if debugging libraries are to be built.
dnl
dnl --enable-libstdcxx-debug
dnl builds a separate set of debugging libraries in addition to the
dnl normal (shared, static) libstdc++ binaries.
dnl
dnl --disable-libstdcxx-debug
dnl builds only one (non-debug) version of libstdc++.
dnl
dnl --enable-libstdcxx-debug-flags=FLAGS
dnl iff --enable-debug == yes, then use FLAGS to build the debug library.
dnl
dnl  +  Usage:  GLIBCXX_ENABLE_DEBUG[(DEFAULT)]
dnl       Where DEFAULT is either `yes' or `no'.
dnl
AC_DEFUN([GLIBCXX_ENABLE_DEBUG], [
  AC_MSG_CHECKING([for additional debug build])
  GLIBCXX_ENABLE(libstdcxx-debug,$1,,[build extra debug library])
  AC_MSG_RESULT($enable_libstdcxx_debug)
  GLIBCXX_CONDITIONAL(GLIBCXX_BUILD_DEBUG, test $enable_libstdcxx_debug = yes)
])


dnl
dnl Check for explicit debug flags.
dnl
dnl --enable-libstdcxx-debug-flags='-O1'
dnl is a general method for passing flags to be used when
dnl building debug libraries with --enable-debug.
dnl
dnl --disable-libstdcxx-debug-flags does nothing.
dnl  +  Usage:  GLIBCXX_ENABLE_DEBUG_FLAGS(default flags)
dnl       If "default flags" is an empty string, the effect is the same
dnl       as --disable or --enable=no.
dnl
AC_DEFUN([GLIBCXX_ENABLE_DEBUG_FLAGS], [
  GLIBCXX_ENABLE(libstdcxx-debug-flags,[$1],[=FLAGS],
    [pass compiler FLAGS when building debug library],
    [case "x$enable_libstdcxx_debug_flags" in
      xno | x)    enable_libstdcxx_debug_flags= ;;
      x-*)        ;;
      *)          AC_MSG_ERROR(_g_switch needs compiler flags as arguments) ;;
     esac])

  # Option parsed, now set things appropriately
  DEBUG_FLAGS="$enable_libstdcxx_debug_flags"
  AC_SUBST(DEBUG_FLAGS)

  AC_MSG_NOTICE([Debug build flags set to $DEBUG_FLAGS])
])


dnl
dnl Check if the user only wants a freestanding library implementation.
dnl
dnl --disable-hosted-libstdcxx will turn off most of the library build,
dnl installing only the headers required by [17.4.1.3] and the language
dnl support library.  More than that will be built (to keep the Makefiles
dnl conveniently clean), but not installed.
dnl
dnl Sets:
dnl  is_hosted  (yes/no)
dnl
dnl Defines:
dnl  _GLIBCXX_HOSTED   (always defined, either to 1 or 0)
dnl
AC_DEFUN([GLIBCXX_ENABLE_HOSTED], [
  AC_ARG_ENABLE([hosted-libstdcxx],
    AC_HELP_STRING([--disable-hosted-libstdcxx],
                   [only build freestanding C++ runtime support]),,
    [case "$host" in
	arm*-*-symbianelf*) 
	    enable_hosted_libstdcxx=no
	    ;;
        *) 
	    enable_hosted_libstdcxx=yes
	    ;;
     esac])
  if test "$enable_hosted_libstdcxx" = no; then
    AC_MSG_NOTICE([Only freestanding libraries will be built])
    is_hosted=no
    hosted_define=0
    enable_abi_check=no
    enable_libstdcxx_pch=no
  else
    is_hosted=yes
    hosted_define=1
  fi
  GLIBCXX_CONDITIONAL(GLIBCXX_HOSTED, test $is_hosted = yes)
  AC_DEFINE_UNQUOTED(_GLIBCXX_HOSTED, $hosted_define,
    [Define to 1 if a full hosted library is built, or 0 if freestanding.])
])


dnl
dnl Check for template specializations for the 'long long' type.
dnl The result determines only whether 'long long' I/O is enabled; things
dnl like numeric_limits<> specializations are always available.
dnl
dnl --enable-long-long defines _GLIBCXX_USE_LONG_LONG
dnl --disable-long-long leaves _GLIBCXX_USE_LONG_LONG undefined
dnl  +  Usage:  GLIBCXX_ENABLE_LONG_LONG[(DEFAULT)]
dnl       Where DEFAULT is either `yes' or `no'.
dnl
AC_DEFUN([GLIBCXX_ENABLE_LONG_LONG], [
  GLIBCXX_ENABLE(long-long,$1,,[enable template specializations for 'long long'])
  if test $enable_long_long = yes; then
    AC_DEFINE(_GLIBCXX_USE_LONG_LONG, 1, 
              [Define if code specialized for long long should be used.])
  fi
  AC_MSG_CHECKING([for enabled long long specializations])
  AC_MSG_RESULT([$enable_long_long])
])


dnl
dnl Check for template specializations for the 'wchar_t' type.
dnl
dnl --enable-wchar_t defines _GLIBCXX_USE_WCHAR_T
dnl --disable-wchar_t leaves _GLIBCXX_USE_WCHAR_T undefined
dnl  +  Usage:  GLIBCXX_ENABLE_WCHAR_T[(DEFAULT)]
dnl       Where DEFAULT is either `yes' or `no'.
dnl
dnl Necessary support must also be present.
dnl
AC_DEFUN([GLIBCXX_ENABLE_WCHAR_T], [
  GLIBCXX_ENABLE(wchar_t,$1,,[enable template specializations for 'wchar_t'])

  # Test wchar.h for mbstate_t, which is needed for char_traits and fpos.
  AC_CHECK_HEADERS(wchar.h, ac_has_wchar_h=yes, ac_has_wchar_h=no)
  AC_MSG_CHECKING([for mbstate_t])
  AC_TRY_COMPILE([#include <wchar.h>],
  [mbstate_t teststate;],
  have_mbstate_t=yes, have_mbstate_t=no)
  AC_MSG_RESULT($have_mbstate_t)
  if test x"$have_mbstate_t" = xyes; then
    AC_DEFINE(HAVE_MBSTATE_T,1,[Define if mbstate_t exists in wchar.h.])
  fi

  # Test it always, for use in GLIBCXX_ENABLE_C99, together with
  # ac_has_wchar_h.
  AC_CHECK_HEADERS(wctype.h, ac_has_wctype_h=yes, ac_has_wctype_h=no)
  
  if test x"$enable_wchar_t" = x"yes"; then

    AC_LANG_SAVE
    AC_LANG_CPLUSPLUS
    
    if test x"$ac_has_wchar_h" = xyes &&
       test x"$ac_has_wctype_h" = xyes; then
      AC_TRY_COMPILE([#include <wchar.h>
                      #include <stddef.h>
                      wint_t i;
		      long l = WEOF;
		      long j = WCHAR_MIN;
		      long k = WCHAR_MAX;
                      namespace test
                      {
			using ::btowc;
			using ::fgetwc;
			using ::fgetws;
			using ::fputwc;
			using ::fputws;
			using ::fwide;
			using ::fwprintf; 
			using ::fwscanf;
			using ::getwc;
			using ::getwchar;
 			using ::mbrlen; 
			using ::mbrtowc; 
			using ::mbsinit; 
			using ::mbsrtowcs; 
			using ::putwc;
			using ::putwchar;
			using ::swprintf; 
			using ::swscanf; 
			using ::ungetwc;
			using ::vfwprintf; 
			using ::vswprintf; 
			using ::vwprintf; 
			using ::wcrtomb; 
			using ::wcscat; 
			using ::wcschr; 
			using ::wcscmp; 
			using ::wcscoll; 
			using ::wcscpy; 
			using ::wcscspn; 
			using ::wcsftime; 
			using ::wcslen;
			using ::wcsncat; 
			using ::wcsncmp; 
			using ::wcsncpy; 
			using ::wcspbrk;
			using ::wcsrchr; 
			using ::wcsrtombs; 
			using ::wcsspn; 
			using ::wcsstr;
			using ::wcstod; 
			using ::wcstok; 
			using ::wcstol;
			using ::wcstoul; 
			using ::wcsxfrm; 
			using ::wctob; 
			using ::wmemchr;
			using ::wmemcmp;
			using ::wmemcpy;
			using ::wmemmove;
			using ::wmemset;
			using ::wprintf; 
			using ::wscanf; 
		      }
		     ],[],[], [enable_wchar_t=no])
    else
      enable_wchar_t=no
    fi

    AC_LANG_RESTORE
  fi

  if test x"$enable_wchar_t" = x"yes"; then
    AC_DEFINE(_GLIBCXX_USE_WCHAR_T, 1,
              [Define if code specialized for wchar_t should be used.])
  fi

  AC_MSG_CHECKING([for enabled wchar_t specializations])
  AC_MSG_RESULT([$enable_wchar_t])
])


dnl
dnl Check to see if building and using a C++ precompiled header can be done.
dnl
dnl --enable-libstdcxx-pch=yes
dnl default, this shows intent to use stdc++.h.gch If it looks like it
dnl may work, after some light-hearted attempts to puzzle out compiler
dnl support, flip bits on in include/Makefile.am
dnl
dnl --disable-libstdcxx-pch
dnl turns off attempts to use or build stdc++.h.gch.
dnl
dnl Substs:
dnl  glibcxx_PCHFLAGS
dnl
AC_DEFUN([GLIBCXX_ENABLE_PCH], [
  GLIBCXX_ENABLE(libstdcxx-pch,$1,,[build pre-compiled libstdc++ headers])
  if test $enable_libstdcxx_pch = yes; then
    AC_CACHE_CHECK([for compiler with PCH support],
      [glibcxx_cv_prog_CXX_pch],
      [ac_save_CXXFLAGS="$CXXFLAGS"
       CXXFLAGS="$CXXFLAGS -Werror -Winvalid-pch -Wno-deprecated"
       AC_LANG_SAVE
       AC_LANG_CPLUSPLUS
       echo '#include <math.h>' > conftest.h
       if $CXX $CXXFLAGS $CPPFLAGS -x c++-header conftest.h \
		          -o conftest.h.gch 1>&5 2>&1 &&
	        echo '#error "pch failed"' > conftest.h &&
          echo '#include "conftest.h"' > conftest.cc &&
	       $CXX -c $CXXFLAGS $CPPFLAGS conftest.cc 1>&5 2>&1 ;
       then
         glibcxx_cv_prog_CXX_pch=yes
       else
         glibcxx_cv_prog_CXX_pch=no
       fi
       rm -f conftest*
       CXXFLAGS=$ac_save_CXXFLAGS
       AC_LANG_RESTORE
      ])
    enable_libstdcxx_pch=$glibcxx_cv_prog_CXX_pch
  fi

  AC_MSG_CHECKING([for enabled PCH])
  AC_MSG_RESULT([$enable_libstdcxx_pch])

  GLIBCXX_CONDITIONAL(GLIBCXX_BUILD_PCH, test $enable_libstdcxx_pch = yes)
  if test $enable_libstdcxx_pch = yes; then
    glibcxx_PCHFLAGS="-include bits/stdtr1c++.h"
  else
    glibcxx_PCHFLAGS=""
  fi
  AC_SUBST(glibcxx_PCHFLAGS)
])


dnl
dnl Check for atomic builtins.
dnl See:
dnl http://gcc.gnu.org/onlinedocs/gcc/Atomic-Builtins.html#Atomic-Builtins
dnl
dnl This checks to see if the host supports the compiler-generated
dnl builtins for atomic operations. Note, this is intended to be an
dnl all-or-nothing switch, so all the atomic operations that are used
dnl should be checked.
dnl
dnl Note:
dnl libgomp and libgfortran do this with a link test, instead of an asm test.
dnl see: CHECK_SYNC_FETCH_AND_ADD
dnl
dnl Defines:
dnl  _GLIBCXX_ATOMIC_BUILTINS if the compiler on this target supports atomics.
dnl
AC_DEFUN([GLIBCXX_ENABLE_ATOMIC_BUILTINS], [
  AC_MSG_CHECKING([for atomic builtins])
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS

  # Fake what AC_TRY_COMPILE does.  XXX Look at redoing this new-style.
    cat > conftest.$ac_ext << EOF
[#]line __oline__ "configure"
int main()
{
  // NB: _Atomic_word not necessarily int. 
  typedef int atomic_type;
  atomic_type c1;
  atomic_type c2;
  const atomic_type c3(0);
  if (__sync_fetch_and_add(&c1, c2) == c3)
    {
      // Do something.
    }
   return 0;
}
EOF
    old_CXXFLAGS="$CXXFLAGS"
    CXXFLAGS=-S
    if AC_TRY_EVAL(ac_compile); then
      if grep __sync_fetch_and_add conftest.s >/dev/null 2>&1 ; then
        enable_atomic_builtins=no
      else
      AC_DEFINE(_GLIBCXX_ATOMIC_BUILTINS, 1,
        [Define if builtin atomic operations are supported on this host.])
        enable_atomic_builtins=yes
	atomicity_dir=cpu/generic/atomicity_builtins
      fi
    fi
    CXXFLAGS="$old_CXXFLAGS"
    rm -f conftest*

   # Now, if still generic, set to mutex.
  if test $atomicity_dir = "cpu/generic" ; then
	atomicity_dir=cpu/generic/atomicity_mutex
  fi
 AC_LANG_RESTORE
 AC_MSG_RESULT($enable_atomic_builtins)
])


dnl
dnl Check for exception handling support.  If an explicit enable/disable
dnl sjlj exceptions is given, we don't have to detect.  Otherwise the
dnl target may or may not support call frame exceptions.
dnl
dnl --enable-sjlj-exceptions forces the use of builtin setjmp.
dnl --disable-sjlj-exceptions forces the use of call frame unwinding.
dnl Neither one forces an attempt at detection.
dnl
dnl Defines:
dnl  _GLIBCXX_SJLJ_EXCEPTIONS if the compiler is configured for it
dnl
AC_DEFUN([GLIBCXX_ENABLE_SJLJ_EXCEPTIONS], [
  AC_MSG_CHECKING([for exception model to use])
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS
  GLIBCXX_ENABLE(sjlj-exceptions,auto,,
    [force use of builtin_setjmp for exceptions],
    [permit yes|no|auto])

  if test $enable_sjlj_exceptions = auto; then
    # Botheration.  Now we've got to detect the exception model.  Link tests
    # against libgcc.a are problematic since we've not been given proper -L
    # bits for single-tree newlib and libgloss.
    #
    # Fake what AC_TRY_COMPILE does.  XXX Look at redoing this new-style.
    cat > conftest.$ac_ext << EOF
[#]line __oline__ "configure"
struct S { ~S(); };
void bar();
void foo()
{
  S s;
  bar();
}
EOF
    old_CXXFLAGS="$CXXFLAGS"
    CXXFLAGS=-S
    if AC_TRY_EVAL(ac_compile); then
      if grep _Unwind_SjLj_Resume conftest.s >/dev/null 2>&1 ; then
        enable_sjlj_exceptions=yes
      elif grep _Unwind_Resume conftest.s >/dev/null 2>&1 ; then
        enable_sjlj_exceptions=no
      elif grep __cxa_end_cleanup conftest.s >/dev/null 2>&1 ; then
        enable_sjlj_exceptions=no
      fi
    fi
    CXXFLAGS="$old_CXXFLAGS"
    rm -f conftest*
  fi

  # This is a tad weird, for hysterical raisins.  We have to map
  # enable/disable to two different models.
  case $enable_sjlj_exceptions in
    yes)
      AC_DEFINE(_GLIBCXX_SJLJ_EXCEPTIONS, 1,
        [Define if the compiler is configured for setjmp/longjmp exceptions.])
      ac_exception_model_name=sjlj
      ;;
    no)
      ac_exception_model_name="call frame"
      ;;
    *)
      AC_MSG_ERROR([unable to detect exception model])
      ;;
  esac
 AC_LANG_RESTORE
 AC_MSG_RESULT($ac_exception_model_name)
])


dnl
dnl Allow visibility attributes to be used on namespaces, objects, etc.
dnl
dnl --enable-visibility enables attempt to use visibility attributes.
dnl --disable-visibility turns off all use of visibility attributes.
dnl  +  Usage:  GLIBCXX_ENABLE_VISIBILITY[(DEFAULT)]
dnl       Where DEFAULT is 'yes'.
dnl
AC_DEFUN([GLIBCXX_ENABLE_VISIBILITY], [
GLIBCXX_ENABLE(visibility,$1,,[enables visibility safe usage])

if test x$enable_visibility = xyes ; then
  dnl all hail libgfortran
  dnl Check whether the target supports hidden visibility.
  AC_CACHE_CHECK([whether the target supports hidden visibility],
		 have_attribute_visibility, [
  save_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS -Werror"
  AC_TRY_COMPILE([void __attribute__((visibility("hidden"))) foo(void) { }],
		 [], have_attribute_visibility=yes,
		 have_attribute_visibility=no)
  CFLAGS="$save_CFLAGS"])
  if test $have_attribute_visibility = no; then
    enable_visibility=no
  fi
fi

GLIBCXX_CONDITIONAL(ENABLE_VISIBILITY, test $enable_visibility = yes)
AC_MSG_NOTICE([visibility supported: $enable_visibility])
])


dnl
dnl Add version tags to symbols in shared library (or not), additionally
dnl marking other symbols as private/local (or not).
dnl
dnl --enable-symvers=style adds a version script to the linker call when
dnl       creating the shared library.  The choice of version script is
dnl       controlled by 'style'.
dnl --disable-symvers does not.
dnl  +  Usage:  GLIBCXX_ENABLE_SYMVERS[(DEFAULT)]
dnl       Where DEFAULT is either 'yes' or 'no'.  Passing `yes' tries to
dnl       choose a default style based on linker characteristics.  Passing
dnl       'no' disables versioning.
dnl
AC_DEFUN([GLIBCXX_ENABLE_SYMVERS], [

GLIBCXX_ENABLE(symvers,$1,[=STYLE],
  [enables symbol versioning of the shared library],
  [permit yes|no|gnu|gnu-versioned-namespace|darwin|darwin-export])

# If we never went through the GLIBCXX_CHECK_LINKER_FEATURES macro, then we
# don't know enough about $LD to do tricks...
AC_REQUIRE([GLIBCXX_CHECK_LINKER_FEATURES])

# Turn a 'yes' into a suitable default.
if test x$enable_symvers = xyes ; then
  if test $enable_shared = no || test "x$LD" = x || test x$gcc_no_link = xyes; then
    enable_symvers=no
  else
    if test $with_gnu_ld = yes ; then
      enable_symvers=gnu
    else
      case ${target_os} in
        darwin*)
	  enable_symvers=darwin ;;
        *)
          enable_symvers=no ;;
      esac
    fi
  fi
fi

# Check to see if 'darwin' or 'darwin-export' can win.
if test x$enable_symvers = xdarwin-export ; then
    enable_symvers=darwin
fi

# Check to see if 'gnu' can win.
if test $enable_symvers = gnu || test $enable_symvers = gnu-versioned-namespace; then
  # Check to see if libgcc_s exists, indicating that shared libgcc is possible.
  AC_MSG_CHECKING([for shared libgcc])
  ac_save_CFLAGS="$CFLAGS"
  CFLAGS=' -lgcc_s'
  AC_TRY_LINK(, [return 0;], glibcxx_shared_libgcc=yes, glibcxx_shared_libgcc=no)
  CFLAGS="$ac_save_CFLAGS"
  if test $glibcxx_shared_libgcc = no; then
    cat > conftest.c <<EOF
int main (void) { return 0; }
EOF
changequote(,)dnl
    glibcxx_libgcc_s_suffix=`${CC-cc} $CFLAGS $CPPFLAGS $LDFLAGS \
			     -shared -shared-libgcc -o conftest.so \
			     conftest.c -v 2>&1 >/dev/null \
			     | sed -n 's/^.* -lgcc_s\([^ ]*\) .*$/\1/p'`
changequote([,])dnl
    rm -f conftest.c conftest.so
    if test x${glibcxx_libgcc_s_suffix+set} = xset; then
      CFLAGS=" -lgcc_s$glibcxx_libgcc_s_suffix"
      AC_TRY_LINK(, [return 0;], glibcxx_shared_libgcc=yes)
      CFLAGS="$ac_save_CFLAGS"
    fi
  fi
  AC_MSG_RESULT($glibcxx_shared_libgcc)

  # For GNU ld, we need at least this version.  The format is described in
  # GLIBCXX_CHECK_LINKER_FEATURES above.
  glibcxx_min_gnu_ld_version=21400

  # If no shared libgcc, can't win.
  if test $glibcxx_shared_libgcc != yes; then
      AC_MSG_WARN([=== You have requested GNU symbol versioning, but])
      AC_MSG_WARN([=== you are not building a shared libgcc_s.])
      AC_MSG_WARN([=== Symbol versioning will be disabled.])
      enable_symvers=no
  elif test $with_gnu_ld != yes ; then
    # just fail for now
    AC_MSG_WARN([=== You have requested GNU symbol versioning, but])
    AC_MSG_WARN([=== you are not using the GNU linker.])
    AC_MSG_WARN([=== Symbol versioning will be disabled.])
    enable_symvers=no
  elif test $glibcxx_gnu_ld_version -lt $glibcxx_min_gnu_ld_version ; then
    # The right tools, the right setup, but too old.  Fallbacks?
    AC_MSG_WARN(=== Linker version $glibcxx_gnu_ld_version is too old for)
    AC_MSG_WARN(=== full symbol versioning support in this release of GCC.)
    AC_MSG_WARN(=== You would need to upgrade your binutils to version)
    AC_MSG_WARN(=== $glibcxx_min_gnu_ld_version or later and rebuild GCC.)
    AC_MSG_WARN([=== Symbol versioning will be disabled.])
    enable_symvers=no
  fi
fi

# Everything parsed; figure out what file to use.
case $enable_symvers in
  no)
    SYMVER_FILE=config/abi/pre/none.ver
    ;;
  gnu)
    SYMVER_FILE=config/abi/pre/gnu.ver
    AC_DEFINE(_GLIBCXX_SYMVER_GNU, 1, 
              [Define to use GNU versioning in the shared library.])
    ;;
  gnu-versioned-namespace)
    SYMVER_FILE=config/abi/pre/gnu-versioned-namespace.ver
    AC_DEFINE(_GLIBCXX_SYMVER_GNU_NAMESPACE, 1, 
              [Define to use GNU namespace versioning in the shared library.])
    ;;
  darwin)
    SYMVER_FILE=config/abi/pre/gnu.ver
    AC_DEFINE(_GLIBCXX_SYMVER_DARWIN, 1, 
              [Define to use darwin versioning in the shared library.])
    ;;
esac

if test x$enable_symvers != xno ; then
  AC_DEFINE(_GLIBCXX_SYMVER, 1,
	 [Define to use symbol versioning in the shared library.])
fi

AC_SUBST(SYMVER_FILE)
AC_SUBST(port_specific_symbol_files)
GLIBCXX_CONDITIONAL(ENABLE_SYMVERS, test $enable_symvers != no)
GLIBCXX_CONDITIONAL(ENABLE_SYMVERS_GNU, test $enable_symvers = gnu)
GLIBCXX_CONDITIONAL(ENABLE_SYMVERS_GNU_NAMESPACE, test $enable_symvers = gnu-versioned-namespace)
GLIBCXX_CONDITIONAL(ENABLE_SYMVERS_DARWIN, test $enable_symvers = darwin)
AC_MSG_NOTICE(versioning on shared library symbols is $enable_symvers)

# Now, set up compatibility support, if any.
# In addition, need this to deal with std::size_t mangling in
# src/compatibility.cc.  In a perfect world, could use
# typeid(std::size_t).name()[0] to do direct substitution.
AC_MSG_CHECKING([for size_t as unsigned int])
ac_save_CFLAGS="$CFLAGS"
CFLAGS="-Werror"
AC_TRY_COMPILE(, [__SIZE_TYPE__* stp; unsigned int* uip; stp = uip;], 
	         [glibcxx_size_t_is_i=yes], [glibcxx_size_t_is_i=no])
CFLAGS=$ac_save_CFLAGS
if test "$glibcxx_size_t_is_i" = yes; then
  AC_DEFINE(_GLIBCXX_SIZE_T_IS_UINT, 1, [Define if size_t is unsigned int.])
fi
AC_MSG_RESULT([$glibcxx_size_t_is_i])

AC_MSG_CHECKING([for ptrdiff_t as int])
ac_save_CFLAGS="$CFLAGS"
CFLAGS="-Werror"
AC_TRY_COMPILE(, [__PTRDIFF_TYPE__* ptp; int* ip; ptp = ip;], 
	         [glibcxx_ptrdiff_t_is_i=yes], [glibcxx_ptrdiff_t_is_i=no])
CFLAGS=$ac_save_CFLAGS
if test "$glibcxx_ptrdiff_t_is_i" = yes; then
  AC_DEFINE(_GLIBCXX_PTRDIFF_T_IS_INT, 1, [Define if ptrdiff_t is int.])
fi
AC_MSG_RESULT([$glibcxx_ptrdiff_t_is_i])
])


dnl
dnl Setup to use the gcc gthr.h thread-specific memory and mutex model.
dnl We must stage the required headers so that they will be installed
dnl with the library (unlike libgcc, the STL implementation is provided
dnl solely within headers).  Since we must not inject random user-space
dnl macro names into user-provided C++ code, we first stage into <file>-in
dnl and process to <file> with an output command.  The reason for a two-
dnl stage process here is to correctly handle $srcdir!=$objdir without
dnl having to write complex code (the sed commands to clean the macro
dnl namespace are complex and fragile enough as it is).  We must also
dnl add a relative path so that -I- is supported properly.
dnl
dnl Substs:
dnl  glibcxx_thread_h
dnl
dnl Defines:
dnl  HAVE_GTHR_DEFAULT
dnl
AC_DEFUN([GLIBCXX_ENABLE_THREADS], [
  AC_MSG_CHECKING([for thread model used by GCC])
  target_thread_file=`$CXX -v 2>&1 | sed -n 's/^Thread model: //p'`
  AC_MSG_RESULT([$target_thread_file])

  if test $target_thread_file != single; then
    AC_DEFINE(HAVE_GTHR_DEFAULT, 1,
              [Define if gthr-default.h exists 
              (meaning that threading support is enabled).])
  fi

  glibcxx_thread_h=gthr-$target_thread_file.h

  dnl Check for __GTHREADS define.
  gthread_file=${toplevel_srcdir}/gcc/${glibcxx_thread_h}
  if grep __GTHREADS $gthread_file >/dev/null 2>&1 ; then
    enable_thread=yes
  else
   enable_thread=no
  fi

  AC_SUBST(glibcxx_thread_h)
])


# Check whether LC_MESSAGES is available in <locale.h>.
# Ulrich Drepper <drepper@cygnus.com>, 1995.
#
# This file file be copied and used freely without restrictions.  It can
# be used in projects which are not available under the GNU Public License
# but which still want to provide support for the GNU gettext functionality.
# Please note that the actual code is *not* freely available.

# serial 1
AC_DEFUN([AC_LC_MESSAGES], [
  AC_CHECK_HEADER(locale.h, [
    AC_CACHE_CHECK([for LC_MESSAGES], ac_cv_val_LC_MESSAGES,
      [AC_TRY_LINK([#include <locale.h>], [return LC_MESSAGES],
       ac_cv_val_LC_MESSAGES=yes, ac_cv_val_LC_MESSAGES=no)])
    if test $ac_cv_val_LC_MESSAGES = yes; then
      AC_DEFINE(HAVE_LC_MESSAGES, 1, 
                [Define if LC_MESSAGES is available in <locale.h>.])
    fi
  ])
])

# Macros from the top-level gcc directory.
m4_include([../config/tls.m4])

