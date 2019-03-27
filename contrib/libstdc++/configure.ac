# Process this file with autoreconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(package-unused, version-unused,, libstdc++)
AC_CONFIG_SRCDIR(src/ios.cc)
AC_CONFIG_HEADER(config.h)

# This works around the fact that libtool configuration may change LD
# for this particular configuration, but some shells, instead of
# keeping the changes in LD private, export them just because LD is
# exported.  Only used at the end of this file.
### am handles this now?  ORIGINAL_LD_FOR_MULTILIBS=$LD

# For libtool versioning info, format is CURRENT:REVISION:AGE
libtool_VERSION=6:9:0
AC_SUBST(libtool_VERSION)

# Find the rest of the source tree framework.
AM_ENABLE_MULTILIB(, ..)

# Gets build, host, target, *_vendor, *_cpu, *_os, etc.
#
# You will slowly go insane if you do not grok the following fact:  when
# building v3 as part of the compiler, the top-level /target/ becomes the
# library's /host/.  configure then causes --target to default to --host,
# exactly like any other package using autoconf.  Therefore, 'target' and
# 'host' will always be the same.  This makes sense both for native and
# cross compilers, just think about it for a little while.  :-)
#
# Also, if v3 is being configured as part of a cross compiler, the top-level
# configure script will pass the "real" host as $with_cross_host.
#
# Do not delete or change the following two lines.  For why, see
# http://gcc.gnu.org/ml/libstdc++/2003-07/msg00451.html
AC_CANONICAL_SYSTEM
target_alias=${target_alias-$host_alias}

# Handy for debugging:
#AC_MSG_NOTICE($build / $host / $target / $host_alias / $target_alias); sleep 5

if test "$build" != "$host"; then
  # We are being configured with some form of cross compiler.
  GLIBCXX_IS_NATIVE=false
  case "$host","$target" in
    # Darwin crosses can use the host system's libraries and headers,
    # because of the fat library support.  Of course, it must be the
    # same version of Darwin on both sides.  Allow the user to
    # just say --target=foo-darwin without a version number to mean
    # "the version on this system".
      *-*-darwin*,*-*-darwin*)
	hostos=`echo $host | sed 's/.*-darwin/darwin/'`
	targetos=`echo $target | sed 's/.*-darwin/darwin/'`
	if test $hostos = $targetos -o $targetos = darwin ; then
	  GLIBCXX_IS_NATIVE=true
	fi
	;;

      *)
	GCC_NO_EXECUTABLES
	;;
  esac
else
  GLIBCXX_IS_NATIVE=true
fi

# Sets up automake.  Must come after AC_CANONICAL_SYSTEM.  Each of the
# following is magically included in AUTOMAKE_OPTIONS in each Makefile.am.
#  1.x:  minimum required version
#  no-define:  PACKAGE and VERSION will not be #define'd in config.h (a bunch
#              of other PACKAGE_* variables will, however, and there's nothing
#              we can do about that; they come from AC_INIT).
#  foreign:  we don't follow the normal rules for GNU packages (no COPYING
#            file in the top srcdir, etc, etc), so stop complaining.
#  no-dependencies:  turns off auto dependency generation (just for now)
#  -Wall:  turns on all automake warnings...
#  -Wno-portability:  ...except this one, since GNU make is now required.
AM_INIT_AUTOMAKE([1.9.3 no-define foreign no-dependencies -Wall -Wno-portability -Wno-override])
AH_TEMPLATE(PACKAGE, [Name of package])
AH_TEMPLATE(VERSION, [Version number of package])

# Runs configure.host, finds CC, CXX, and assorted other critical bits.  Sets
# up critical shell variables.
GLIBCXX_CONFIGURE

AC_LIBTOOL_DLOPEN
AM_PROG_LIBTOOL
AC_SUBST(enable_shared)
AC_SUBST(enable_static)

# Possibly disable most of the library.
## TODO: Consider skipping unncessary tests altogether in this case, rather
## than just ignoring the results.  Faster /and/ more correct, win win.
GLIBCXX_ENABLE_HOSTED

# Check for compiler support that doesn't require linking.
GLIBCXX_ENABLE_SJLJ_EXCEPTIONS
GLIBCXX_ENABLE_PCH($is_hosted)

# Enable all the variable C++ runtime options.  
GLIBCXX_ENABLE_CSTDIO
GLIBCXX_ENABLE_CLOCALE
GLIBCXX_ENABLE_ALLOCATOR
GLIBCXX_ENABLE_CHEADERS($c_model)  dnl c_model from configure.host
GLIBCXX_ENABLE_LONG_LONG([yes])
GLIBCXX_ENABLE_WCHAR_T([yes])
GLIBCXX_ENABLE_C99([yes])
GLIBCXX_ENABLE_CONCEPT_CHECKS([no])
GLIBCXX_ENABLE_DEBUG_FLAGS(["-g3 -O0"])
GLIBCXX_ENABLE_DEBUG([no])
GLIBCXX_ENABLE_CXX_FLAGS
GLIBCXX_ENABLE_FULLY_DYNAMIC_STRING([no])

# No surprises, no surprises...
GLIBCXX_ENABLE_THREADS
GLIBCXX_ENABLE_ATOMIC_BUILTINS
if test $atomicity_dir = cpu/generic/atomic_mutex ; then
  AC_MSG_WARN([No native atomic operations are provided for this platform.])
  if test $target_thread_file = single; then
    AC_MSG_WARN([They cannot be faked when thread support is disabled.])
    AC_MSG_WARN([Thread-safety of certain classes is not guaranteed.])
  else
    AC_MSG_WARN([They will be faked using a mutex.])
    AC_MSG_WARN([Performance of certain classes will degrade as a result.])
  fi
fi

GLIBCXX_CHECK_COMPILER_FEATURES

if $GLIBCXX_IS_NATIVE; then

  # We can do more elaborate tests that assume a working linker.
  CANADIAN=no

  # Check for available headers.
  AC_CHECK_HEADERS([nan.h ieeefp.h endian.h sys/isa_defs.h machine/endian.h \
  machine/param.h sys/machine.h fp.h locale.h float.h inttypes.h gconv.h \
  sys/types.h sys/ipc.h sys/sem.h])

  GLIBCXX_CHECK_LINKER_FEATURES
  GLIBCXX_CHECK_MATH_SUPPORT
  GLIBCXX_CHECK_BUILTIN_MATH_SUPPORT
  GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT
  GLIBCXX_CHECK_ICONV_SUPPORT
  GLIBCXX_CHECK_STDLIB_SUPPORT

  # For showmanyc_helper().
  AC_CHECK_HEADERS(sys/ioctl.h sys/filio.h)
  GLIBCXX_CHECK_POLL
  GLIBCXX_CHECK_S_ISREG_OR_S_IFREG

  # For xsputn_2().
  AC_CHECK_HEADERS(sys/uio.h)
  GLIBCXX_CHECK_WRITEV

  # For the __streamoff_base_type typedef.
  GLIBCXX_CHECK_INT64_T

  # For LFS support.
  GLIBCXX_CHECK_LFS

  # For C99 support to TR1.
  GLIBCXX_CHECK_C99_TR1

  # For dev/random and dev/urandom for TR1.
  GLIBCXX_CHECK_RANDOM_TR1

  # For TLS support.
  GCC_CHECK_TLS

  # For _Unwind_GetIPInfo.
  GCC_CHECK_UNWIND_GETIPINFO

  AC_LC_MESSAGES

  AC_TRY_COMPILE(
    [#include <setjmp.h>],
    [sigjmp_buf env;
     while (! sigsetjmp (env, 1))
       siglongjmp (env, 1);
    ],
    [AC_DEFINE(HAVE_SIGSETJMP, 1, [Define if sigsetjmp is available.])])

  AC_FUNC_MMAP

else

  # This lets us hard-code the functionality we know we'll have in the cross
  # target environment.  "Let" is a sugar-coated word placed on an especially
  # dull and tedious hack, actually.
  #
  # Here's why GLIBCXX_CHECK_MATH_SUPPORT, and other autoconf macros
  # that involve linking, can't be used:
  #    "cannot open sim-crt0.o"
  #    "cannot open crt0.o"
  # etc.  All this is because there currently exists no unified, consistent
  # way for top level CC information to be passed down to target directories:
  # newlib includes, newlib linking info, libgloss versus newlib crt0.o, etc.
  # When all of that is done, all of this hokey, excessive AC_DEFINE junk for
  # crosses can be removed.

  # If Canadian cross, then don't pick up tools from the build directory.
  # Used only in GLIBCXX_EXPORT_INCLUDES.
  if test -n "$with_cross_host" &&
     test x"$build_alias" != x"$with_cross_host" &&
     test x"$build" != x"$target";
  then
    CANADIAN=yes
  else
    CANADIAN=no
  fi

  # Construct crosses by hand, eliminating bits that need ld...
  # GLIBCXX_CHECK_BUILTIN_MATH_SUPPORT
  # GLIBCXX_CHECK_MATH_SUPPORT

  # First, test for "known" system libraries.  We may be using newlib even
  # on a hosted environment.
  if test "x${with_newlib}" = "xyes"; then
    os_include_dir="os/newlib"
    AC_DEFINE(HAVE_HYPOT)

    # GLIBCXX_CHECK_STDLIB_SUPPORT
    AC_DEFINE(HAVE_STRTOF)        
    # AC_FUNC_MMAP
    AC_DEFINE(HAVE_MMAP)

    AC_DEFINE(HAVE_ACOSF)
    AC_DEFINE(HAVE_ASINF)
    AC_DEFINE(HAVE_ATAN2F)
    AC_DEFINE(HAVE_ATANF)
    AC_DEFINE(HAVE_CEILF)
    AC_DEFINE(HAVE_COPYSIGN)
    AC_DEFINE(HAVE_COPYSIGNF)
    AC_DEFINE(HAVE_COSF)
    AC_DEFINE(HAVE_COSHF)
    AC_DEFINE(HAVE_EXPF)
    AC_DEFINE(HAVE_FABSF)
    AC_DEFINE(HAVE_FLOORF)
    AC_DEFINE(HAVE_FMODF)
    AC_DEFINE(HAVE_FREXPF)
    AC_DEFINE(HAVE_LDEXPF)
    AC_DEFINE(HAVE_LOG10F)
    AC_DEFINE(HAVE_LOGF)
    AC_DEFINE(HAVE_MODFF)
    AC_DEFINE(HAVE_POWF)
    AC_DEFINE(HAVE_SINF)
    AC_DEFINE(HAVE_SINHF)
    AC_DEFINE(HAVE_SQRTF)
    AC_DEFINE(HAVE_TANF)
    AC_DEFINE(HAVE_TANHF)

    AC_DEFINE(HAVE_S_ISREG)
    AC_DEFINE(HAVE_S_IFREG)
  else
    GLIBCXX_CROSSCONFIG
  fi

  # At some point, we should differentiate between architectures
  # like x86, which have long double versions, and alpha/powerpc/etc.,
  # which don't. For the time being, punt.
  if test x"long_double_math_on_this_cpu" = x"yes"; then
    AC_DEFINE(HAVE_ACOSL)
    AC_DEFINE(HAVE_ASINL)
    AC_DEFINE(HAVE_ATAN2L)
    AC_DEFINE(HAVE_ATANL)
    AC_DEFINE(HAVE_CEILL)
    AC_DEFINE(HAVE_COPYSIGNL)
    AC_DEFINE(HAVE_COSL)
    AC_DEFINE(HAVE_COSHL)
    AC_DEFINE(HAVE_EXPL)
    AC_DEFINE(HAVE_FABSL)
    AC_DEFINE(HAVE_FLOORL)
    AC_DEFINE(HAVE_FMODL)
    AC_DEFINE(HAVE_FREXPL)
    AC_DEFINE(HAVE_LDEXPL)
    AC_DEFINE(HAVE_LOG10L)
    AC_DEFINE(HAVE_LOGL)
    AC_DEFINE(HAVE_MODFL)
    AC_DEFINE(HAVE_POWL)
    AC_DEFINE(HAVE_SINCOSL)
    AC_DEFINE(HAVE_SINL)
    AC_DEFINE(HAVE_SINHL)
    AC_DEFINE(HAVE_SQRTL)
    AC_DEFINE(HAVE_TANL)
    AC_DEFINE(HAVE_TANHL)
  fi

  # Assume we have _Unwind_GetIPInfo for cross-compiles.
  AC_DEFINE(HAVE_GETIPINFO)

fi

# This depends on GLIBCXX CHECK_LINKER_FEATURES, but without it assumes no.
GLIBCXX_ENABLE_SYMVERS([yes])
GLIBCXX_ENABLE_VISIBILITY([yes])

ac_ldbl_compat=no
case "$target" in
  powerpc*-*-*gnu* | \
  sparc*-*-linux* | \
  s390*-*-linux* | \
  alpha*-*-linux*)
  AC_TRY_COMPILE(, [
#if !defined __LONG_DOUBLE_128__ || (defined(__sparc__) && defined(__arch64__))
#error no need for long double compatibility
#endif
  ], [ac_ldbl_compat=yes], [ac_ldbl_compat=no])
  if test "$ac_ldbl_compat" = yes; then
    AC_DEFINE([_GLIBCXX_LONG_DOUBLE_COMPAT],1,
	      [Define if compatibility should be provided for -mlong-double-64.])
    port_specific_symbol_files="\$(top_srcdir)/config/os/gnu-linux/ldbl-extra.ver"
  fi
esac
GLIBCXX_CONDITIONAL(GLIBCXX_LDBL_COMPAT, test $ac_ldbl_compat = yes)

# This depends on GLIBCXX_ENABLE_SYMVERS and GLIBCXX_IS_NATIVE.
GLIBCXX_CONFIGURE_TESTSUITE

# Propagate the target-specific source directories through the build chain.
ATOMICITY_SRCDIR=config/${atomicity_dir}
ATOMIC_WORD_SRCDIR=config/${atomic_word_dir}
CPU_DEFINES_SRCDIR=config/${cpu_defines_dir}
OS_INC_SRCDIR=config/${os_include_dir}
ABI_TWEAKS_SRCDIR=config/${abi_tweaks_dir}
AC_SUBST(ATOMICITY_SRCDIR)
AC_SUBST(ATOMIC_WORD_SRCDIR)
AC_SUBST(CPU_DEFINES_SRCDIR)
AC_SUBST(ABI_TWEAKS_SRCDIR)
AC_SUBST(OS_INC_SRCDIR)


# Determine cross-compile flags and AM_CONDITIONALs.
#AC_SUBST(GLIBCXX_IS_NATIVE)
#AM_CONDITIONAL(CANADIAN, test $CANADIAN = yes)
# from GLIBCXX_CHECK_COMPLEX_MATH_SUPPORT:
#AM_CONDITIONAL(GLIBCXX_BUILD_LIBMATH,  test $need_libmath = yes)
GLIBCXX_EVALUATE_CONDITIONALS
 
AC_CACHE_SAVE

if test ${multilib} = yes; then
  multilib_arg="--enable-multilib"
else
  multilib_arg=
fi

# Export all the install information.
GLIBCXX_EXPORT_INSTALL_INFO

# Export all the include and flag information to Makefiles.
GLIBCXX_EXPORT_INCLUDES
GLIBCXX_EXPORT_FLAGS

if ${CONFIG_SHELL-/bin/sh} ./libtool --tag CXX --features |
   grep "enable shared" > /dev/null;
then
  LIBSUPCXX_PICFLAGS=-prefer-pic
else
  LIBSUPCXX_PICFLAGS=
fi
AC_SUBST(LIBSUPCXX_PICFLAGS)

dnl In autoconf 2.5x, AC_OUTPUT is replaced by four AC_CONFIG_* macros,
dnl which can all be called multiple times as needed, plus one (different)
dnl AC_OUTPUT macro.  This one lists the files to be created:
AC_CONFIG_FILES( \
  Makefile \
  AC_FOREACH([DIR], glibcxx_SUBDIRS, [DIR/Makefile ])
  )
AC_CONFIG_FILES([scripts/testsuite_flags],[chmod +x scripts/testsuite_flags])

dnl These commands are run at the end of config.status:
AC_CONFIG_COMMANDS([default],
[if test -n "$CONFIG_FILES"; then
   # Multilibs need MULTISUBDIR defined correctly in certain makefiles so
   # that multilib installs will end up installed in the correct place.
   # The testsuite needs it for multilib-aware ABI baseline files.
   # To work around this not being passed down from config-ml.in ->
   # srcdir/Makefile.am -> srcdir/{src,libsupc++,...}/Makefile.am, manually
   # append it here.  Only modify Makefiles that have just been created.
   #
   # Also, get rid of this simulated-VPATH thing that automake does.
   cat > vpsed << \_EOF
s!`test -f '$<' || echo '$(srcdir)/'`!!
_EOF
   for i in $SUBDIRS; do
    case $CONFIG_FILES in
     *${i}/Makefile*)
       #echo "Adding MULTISUBDIR to $i/Makefile"
       sed -f vpsed $i/Makefile > tmp
       grep '^MULTISUBDIR =' Makefile >> tmp
       mv tmp $i/Makefile
       ;;
    esac
   done
   rm vpsed
 fi
 (cd include && ${MAKE-make})
],
[
# Variables needed in config.status (file generation) which aren't already
# passed by autoconf.
SUBDIRS="$SUBDIRS"
])

dnl And this actually makes things happen:
AC_OUTPUT
