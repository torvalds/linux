#   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
#   2001, 2002, 2003, 2004, 2005, 2006, 2007 Free Software Foundation, Inc.
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

##############################################################################
### WARNING: this file contains embedded tabs.  Do not run untabify on this file.

sinclude(config/acx.m4)

AC_INIT(move-if-change)
AC_PREREQ(2.59)

# Find the build, host, and target systems.
ACX_NONCANONICAL_BUILD
ACX_NONCANONICAL_HOST
ACX_NONCANONICAL_TARGET

dnl Autoconf 2.5x and later will set a default program prefix if
dnl --target was used, even if it was the same as --host.  Disable
dnl that behavior.  This must be done before AC_CANONICAL_SYSTEM
dnl to take effect.
test "$host_noncanonical" = "$target_noncanonical" &&
  test "$program_prefix$program_suffix$program_transform_name" = \
    NONENONEs,x,x, &&
  program_transform_name=s,y,y,

AC_CANONICAL_SYSTEM
AC_ARG_PROGRAM

m4_pattern_allow([^AS_FOR_TARGET$])dnl

# Get 'install' or 'install-sh' and its variants.
AC_PROG_INSTALL
ACX_PROG_LN
AC_PROG_LN_S

### we might need to use some other shell than /bin/sh for running subshells
### If we are on Windows, search for the shell.  This will permit people
### to not have /bin/sh, but to be able to see /SOME/PATH/sh configure
### without also having to set CONFIG_SHELL.  This code will work when
### using bash, which sets OSTYPE.
case "${OSTYPE}" in
*win32*)
  if test x${CONFIG_SHELL} = x ; then
    if test ! -f /bin/sh ; then
      if test x${SHELL} != x && test -f ${SHELL} ; then
	CONFIG_SHELL=${SHELL}
	export CONFIG_SHELL
      else
	for prog in sh sh.exe bash bash.exe; do
	  IFS="${IFS=	}"; save_ifs="$IFS"; IFS="${IFS}:"
	  for dir in $PATH; do
	    test -z "$dir" && dir=.
	    if test -f $dir/$prog; then
	      CONFIG_SHELL=$dir/$prog
	      export CONFIG_SHELL
	      break
	    fi
	  done
	  IFS="$save_ifs"
	  test -n "${CONFIG_SHELL}" && break
	done
      fi
    fi
  fi
  ;;
esac

config_shell=${CONFIG_SHELL-/bin/sh}

progname=$0
# if PWD already has a value, it is probably wrong.
if test -n "$PWD" ; then PWD=`${PWDCMD-pwd}`; fi

# Export original configure arguments for use by sub-configures.
# Quote arguments with shell meta charatcers.
TOPLEVEL_CONFIGURE_ARGUMENTS=
set -- "$progname" "$@"
for ac_arg
do
  case "$ac_arg" in
  *" "*|*"	"*|*[[\[\]\~\#\$\^\&\*\(\)\{\}\\\|\;\<\>\?\']]*)
    ac_arg=`echo "$ac_arg" | sed "s/'/'\\\\\\\\''/g"`
    # if the argument is of the form -foo=baz, quote the baz part only
    ac_arg=`echo "'$ac_arg'" | sed "s/^'\([[-a-zA-Z0-9]]*=\)/\\1'/"` ;;
  *) ;;
  esac
  # Add the quoted argument to the list.
  TOPLEVEL_CONFIGURE_ARGUMENTS="$TOPLEVEL_CONFIGURE_ARGUMENTS $ac_arg"
done
# Remove the initial space we just introduced and, as these will be
# expanded by make, quote '$'.
TOPLEVEL_CONFIGURE_ARGUMENTS=`echo "x$TOPLEVEL_CONFIGURE_ARGUMENTS" | sed -e 's/^x *//' -e 's,\\$,$$,g'`
AC_SUBST(TOPLEVEL_CONFIGURE_ARGUMENTS)

moveifchange=${srcdir}/move-if-change

srcpwd=`cd ${srcdir} ; ${PWDCMD-pwd}`

# We pass INSTALL explicitly to sub-makes.  Make sure that it is not
# a relative path.
if test "$INSTALL" = "${srcdir}/install-sh -c"; then
  INSTALL="${srcpwd}/install-sh -c"
fi

# Set srcdir to "." if that's what it is.
# This is important for multilib support.
pwd=`${PWDCMD-pwd}`
if test "${pwd}" = "${srcpwd}" ; then
  srcdir=.
fi

topsrcdir=$srcpwd

extra_host_args=

### To add a new directory to the tree, first choose whether it is a target
### or a host dependent tool.  Then put it into the appropriate list
### (library or tools, host or target), doing a dependency sort.

# Subdirs will be configured in the order listed in build_configdirs, 
# configdirs, or target_configdirs; see the serialization section below.

# Dependency sorting is only needed when *configuration* must be done in 
# a particular order.  In all cases a dependency should be specified in 
# the Makefile, whether or not it's implicitly specified here.

# Double entries in build_configdirs, configdirs, or target_configdirs may
# cause circular dependencies and break everything horribly.

# these library is used by various programs built for the build
# environment
#
build_libs="build-libiberty"

# these tools are built for the build environment
build_tools="build-texinfo build-byacc build-flex build-bison build-m4 build-fixincludes"

# these libraries are used by various programs built for the host environment
#
host_libs="intl mmalloc libiberty opcodes bfd readline tcl tk itcl libgui zlib libcpp libdecnumber gmp mpfr"

# these tools are built for the host environment
# Note, the powerpc-eabi build depends on sim occurring before gdb in order to
# know that we are building the simulator.
# binutils, gas and ld appear in that order because it makes sense to run
# "make check" in that particular order.
host_tools="texinfo byacc flex bison binutils gas ld fixincludes gcc sid sim gdb make patch prms send-pr gprof etc expect dejagnu ash bash bzip2 m4 autoconf automake libtool diff rcs fileutils shellutils time textutils wdiff find uudecode hello tar gzip indent recode release sed utils guile perl gawk findutils gettext zip fastjar gnattools"

# libgcj represents the runtime libraries only used by gcj.
libgcj="target-libffi \
	target-zlib \
	target-qthreads \
	target-libjava"

# these libraries are built for the target environment, and are built after
# the host libraries and the host tools (which may be a cross compiler)
#
target_libraries="target-libgcc \
		target-libiberty \
		target-libgloss \
		target-newlib \
		target-libstdc++-v3 \
		target-libmudflap \
		target-libssp \
		target-libgfortran \
		target-boehm-gc \
		${libgcj} \
		target-libobjc \
		target-libada \
		target-libgomp"

# these tools are built using the target libraries, and are intended to
# run only in the target environment
#
# note: any program that *uses* libraries that are in the "target_libraries"
# list belongs in this list.  those programs are also very likely
# candidates for the "native_only" list which follows
#
target_tools="target-examples target-groff target-gperf target-rda"

################################################################################

## All tools belong in one of the four categories, and are assigned above
## We assign ${configdirs} this way to remove all embedded newlines.  This
## is important because configure will choke if they ever get through.
## ${configdirs} is directories we build using the host tools.
## ${target_configdirs} is directories we build using the target tools.
configdirs=`echo ${host_libs} ${host_tools}`
target_configdirs=`echo ${target_libraries} ${target_tools}`
build_configdirs=`echo ${build_libs} ${build_tools}`

################################################################################

srcname="gnu development package"

# This gets set non-empty for some net releases of packages.
appdirs=""

# Define is_cross_compiler to save on calls to 'test'.
is_cross_compiler=
if test x"${host}" = x"${target}" ; then
  is_cross_compiler=no
else
  is_cross_compiler=yes
fi	

# Find the build and target subdir names.
GCC_TOPLEV_SUBDIRS

# Skipdirs are removed silently.
skipdirs=
# Noconfigdirs are removed loudly.
noconfigdirs=""

use_gnu_ld=
# Make sure we don't let GNU ld be added if we didn't want it.
if test x$with_gnu_ld = xno ; then
  use_gnu_ld=no
  noconfigdirs="$noconfigdirs ld"
fi

use_gnu_as=
# Make sure we don't let GNU as be added if we didn't want it.
if test x$with_gnu_as = xno ; then
  use_gnu_as=no
  noconfigdirs="$noconfigdirs gas"
fi

# some tools are so dependent upon X11 that if we're not building with X, 
# it's not even worth trying to configure, much less build, that tool.

case ${with_x} in
  yes | "") ;; # the default value for this tree is that X11 is available
  no)
    skipdirs="${skipdirs} tk itcl libgui"
    # We won't be able to build gdbtk without X.
    enable_gdbtk=no 
    ;;
  *)  echo "*** bad value \"${with_x}\" for -with-x flag; ignored" 1>&2 ;;
esac

# Some tools are only suitable for building in a "native" situation.
# Remove these if host!=target.  
native_only="autoconf automake libtool fileutils find gawk gettext gzip hello indent m4 rcs recode sed shellutils tar textutils uudecode wdiff target-groff guile perl time ash bash bzip2 prms gnuserv target-gperf"

# Similarly, some are only suitable for cross toolchains.
# Remove these if host=target.
cross_only="target-libgloss target-newlib target-opcodes"

case $is_cross_compiler in
  no) skipdirs="${skipdirs} ${cross_only}" ;;
  yes) skipdirs="${skipdirs} ${native_only}" ;;
esac

# If both --with-headers and --with-libs are specified, default to
# --without-newlib.
if test x"${with_headers}" != x && test x"${with_headers}" != xno \
   && test x"${with_libs}" != x && test x"${with_libs}" != xno ; then
  if test x"${with_newlib}" = x ; then
    with_newlib=no
  fi
fi

# Recognize --with-newlib/--without-newlib.
case ${with_newlib} in
  no) skipdirs="${skipdirs} target-newlib" ;;
  yes) skipdirs=`echo " ${skipdirs} " | sed -e 's/ target-newlib / /'` ;;
esac

# Configure extra directories which are host specific

case "${host}" in
  *-cygwin*)
    configdirs="$configdirs libtermcap" ;;
esac

# A target can indicate whether a language isn't supported for some reason.
# Only spaces may be used in this macro; not newlines or tabs.
unsupported_languages=

# Remove more programs from consideration, based on the host or 
# target this usually means that a port of the program doesn't
# exist yet.

case "${host}" in
  hppa*64*-*-*)
    noconfigdirs="$noconfigdirs byacc"
    ;;
  i[[3456789]]86-*-vsta)
    noconfigdirs="$noconfigdirs tcl expect dejagnu make texinfo bison patch flex byacc send-pr gprof uudecode dejagnu diff guile perl itcl gnuserv gettext"
    ;;
  i[[3456789]]86-*-go32* | i[[3456789]]86-*-msdosdjgpp*)
    noconfigdirs="$noconfigdirs tcl tk expect dejagnu send-pr uudecode guile itcl gnuserv libffi"
    ;;
  x86_64-*-mingw*)
    noconfigdirs="$noconfigdirs expect dejagnu autoconf automake send-pr rcs guile perl texinfo libtool newlib"
    ;;
  i[[3456789]]86-*-mingw32*)
    # noconfigdirs="tcl tk expect dejagnu make texinfo bison patch flex byacc send-pr uudecode dejagnu diff guile perl itcl gnuserv"
    noconfigdirs="$noconfigdirs expect dejagnu autoconf automake send-pr rcs guile perl texinfo libtool newlib"
    ;;
  i[[3456789]]86-*-beos*)
    noconfigdirs="$noconfigdirs tk itcl libgui gdb"
    ;;
  *-*-cygwin*)
    noconfigdirs="$noconfigdirs autoconf automake send-pr rcs guile perl"
    ;;
  *-*-netbsd*)
    noconfigdirs="$noconfigdirs rcs"
    ;;
  ppc*-*-pe)
    noconfigdirs="$noconfigdirs patch diff make tk tcl expect dejagnu autoconf automake texinfo bison send-pr gprof rcs guile perl itcl gnuserv"
    ;;
  powerpc-*-beos*)
    noconfigdirs="$noconfigdirs tk itcl libgui gdb dejagnu readline"
    ;;
esac


AC_ARG_ENABLE(libada,
[  --enable-libada        Builds libada directory],
ENABLE_LIBADA=$enableval,
ENABLE_LIBADA=yes)
if test "${ENABLE_LIBADA}" != "yes" ; then
  noconfigdirs="$noconfigdirs gnattools"
fi

AC_ARG_ENABLE(libssp,
[  --enable-libssp        Builds libssp directory],
ENABLE_LIBSSP=$enableval,
ENABLE_LIBSSP=yes)

# Save it here so that, even in case of --enable-libgcj, if the Java
# front-end isn't enabled, we still get libgcj disabled.
libgcj_saved=$libgcj
case $enable_libgcj in
yes)
  # If we reset it here, it won't get added to noconfigdirs in the
  # target-specific build rules, so it will be forcibly enabled
  # (unless the Java language itself isn't enabled).
  libgcj=
  ;;
no)
  # Make sure we get it printed in the list of not supported target libs.
  noconfigdirs="$noconfigdirs ${libgcj}"
  ;;
esac


# Disable libmudflap on some systems.
if test x$enable_libmudflap = x ; then
    case "${target}" in
    *-*-linux* | *-*-gnu* | *-*-k*bsd*-gnu | *-*-linux-uclibc* | *-*-uclinux*)
        # Enable libmudflap by default in GNU and friends.
	;;
    *-*-freebsd*)
        # Enable libmudflap by default in FreeBSD.
	;;
    *)
        # Disable it by default everywhere else.
	noconfigdirs="$noconfigdirs target-libmudflap"
	;;
    esac
fi

# Disable libgomp on non POSIX hosted systems.
if test x$enable_libgomp = x ; then
    # Enable libgomp by default on hosted POSIX systems.
    case "${target}" in
    *-*-linux* | *-*-gnu* | *-*-k*bsd*-gnu)
	;;
    *-*-netbsd* | *-*-freebsd* | *-*-openbsd*)
	;;
    *-*-solaris2* | *-*-sysv4* | *-*-irix6* | *-*-osf* | *-*-hpux11*)
	;;
    *-*-darwin* | *-*-aix*)
	;;
    *)
	noconfigdirs="$noconfigdirs target-libgomp"
	;;
    esac
fi


case "${target}" in
  *-*-chorusos)
    noconfigdirs="$noconfigdirs target-newlib target-libgloss ${libgcj}"
    ;;
  powerpc-*-darwin* | i[[3456789]]86-*-darwin*)
    noconfigdirs="$noconfigdirs bfd binutils ld gas opcodes gdb gprof"
    noconfigdirs="$noconfigdirs sim target-rda"
    ;;
  *-*-darwin*)
    noconfigdirs="$noconfigdirs ld gas gdb gprof"
    noconfigdirs="$noconfigdirs sim target-rda"
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  *-*-freebsd[[12]] | *-*-freebsd[[12]].* | *-*-freebsd*aout*)
    noconfigdirs="$noconfigdirs target-newlib target-libgloss ${libgcj}"
    ;;
  *-*-freebsd*)
    noconfigdirs="$noconfigdirs target-newlib target-libgloss"
    if test "x$with_gmp" = x && test "x$with_gmp_dir" = x \
	&& test -f /usr/local/include/gmp.h; then
      with_gmp=/usr/local
    fi

    # Skip some stuff that's unsupported on some FreeBSD configurations.
    case "${target}" in
      i*86-*-*) ;;
      alpha*-*-*) ;;
      *)
	noconfigdirs="$noconfigdirs ${libgcj}"
	;;
    esac
    ;;
  *-*-kaos*)
    # Remove unsupported stuff on all kaOS configurations.
    skipdirs="target-libiberty ${libgcj} target-libstdc++-v3 target-librx"
    skipdirs="$skipdirs target-libobjc target-examples target-groff target-gperf"
    skipdirs="$skipdirs zlib fastjar target-libjava target-boehm-gc target-zlib"
    noconfigdirs="$noconfigdirs target-libgloss"
    ;;
  *-*-netbsd*)
    # Skip some stuff on all NetBSD configurations.
    noconfigdirs="$noconfigdirs target-newlib target-libiberty target-libgloss"

    # Skip some stuff that's unsupported on some NetBSD configurations.
    case "${target}" in
      i*86-*-netbsdelf*) ;;
      arm*-*-netbsdelf*) ;;
      *)
	noconfigdirs="$noconfigdirs ${libgcj}"
	;;
    esac
    ;;
  *-*-netware*)
    noconfigdirs="$noconfigdirs target-newlib target-libiberty target-libgloss ${libgcj} target-libmudflap"
    ;;
  *-*-rtems*)
    noconfigdirs="$noconfigdirs target-libgloss ${libgcj}"
    ;;
    # The tpf target doesn't support gdb yet.
  *-*-tpf*)
    noconfigdirs="$noconfigdirs target-newlib target-libgloss target-libiberty ${libgcj} target-libmudflap gdb tcl tk libgui itcl"
    ;;
  *-*-uclinux*)
    noconfigdirs="$noconfigdirs target-newlib target-libgloss target-rda ${libgcj}"
    ;;
  *-*-vxworks*)
    noconfigdirs="$noconfigdirs target-newlib target-libgloss target-libiberty target-libstdc++-v3 ${libgcj}"
    ;;
  alpha*-dec-osf*)
    # ld works, but does not support shared libraries.
    # newlib is not 64 bit ready.  I'm not sure about fileutils.
    # gas doesn't generate exception information.
    noconfigdirs="$noconfigdirs gas ld fileutils target-newlib target-libgloss"
    ;;
  alpha*-*-*vms*)
    noconfigdirs="$noconfigdirs gdb ld target-newlib target-libgloss ${libgcj}"
    ;;
  alpha*-*-linux*)
    # newlib is not 64 bit ready
    noconfigdirs="$noconfigdirs target-newlib target-libgloss"
    ;;
  alpha*-*-*)
    # newlib is not 64 bit ready
    noconfigdirs="$noconfigdirs target-newlib target-libgloss ${libgcj}"
    ;;
  am33_2.0-*-linux*)
    noconfigdirs="$noconfigdirs ${libgcj} target-newlib target-libgloss"
    ;;
  sh-*-linux*)
    noconfigdirs="$noconfigdirs ${libgcj} target-newlib target-libgloss"
    ;;    
  sh*-*-pe|mips*-*-pe|*arm-wince-pe)
    noconfigdirs="$noconfigdirs ${libgcj}"
    noconfigdirs="$noconfigdirs target-examples"
    noconfigdirs="$noconfigdirs target-libiberty texinfo send-pr"
    noconfigdirs="$noconfigdirs tcl tk itcl libgui sim"
    noconfigdirs="$noconfigdirs expect dejagnu"
    # the C++ libraries don't build on top of CE's C libraries
    noconfigdirs="$noconfigdirs target-libstdc++-v3"
    noconfigdirs="$noconfigdirs target-newlib"
    case "${host}" in
      *-*-cygwin*) ;; # keep gdb and readline
      *) noconfigdirs="$noconfigdirs gdb readline"
	 ;;
    esac
    ;;
  arc-*-*)
    noconfigdirs="$noconfigdirs target-libgloss ${libgcj}"
    ;;
  arm-semi-aof )
    ;;
  arm-*-coff | strongarm-*-coff | xscale-*-coff)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  arm-*-elf* | strongarm-*-elf* | xscale-*-elf* | arm*-*-eabi* )
    noconfigdirs="$noconfigdirs target-libffi target-qthreads"
    ;;
  arm*-*-linux-gnueabi)
    noconfigdirs="$noconfigdirs target-libffi target-qthreads"
    noconfigdirs="$noconfigdirs target-libjava target-libobjc"
    case ${with_newlib} in
      no) noconfigdirs="$noconfigdirs target-newlib target-libgloss"
    esac
    ;;
  arm*-*-symbianelf*)
    noconfigdirs="$noconfigdirs ${libgcj} target-libiberty"
    ;;
  arm-*-pe*)
    noconfigdirs="$noconfigdirs target-libgloss ${libgcj}"
    ;;
  thumb-*-coff)
    noconfigdirs="$noconfigdirs target-libgloss ${libgcj}"
    ;;
  thumb-*-elf)
    noconfigdirs="$noconfigdirs target-libgloss ${libgcj}"
    ;;
  thumb-*-pe)
    noconfigdirs="$noconfigdirs target-libgloss ${libgcj}"
    ;;
  arm-*-riscix*)
    noconfigdirs="$noconfigdirs ld target-libgloss ${libgcj}"
    ;;
  avr-*-*)
    noconfigdirs="$noconfigdirs target-libiberty target-libstdc++-v3 ${libgcj}"
    ;;
  bfin-*-*)
    noconfigdirs="$noconfigdirs gdb"
    if test x${is_cross_compiler} != xno ; then
      target_configdirs="${target_configdirs} target-bsp target-cygmon"
    fi
    ;;
  c4x-*-* | tic4x-*-*)
    noconfigdirs="$noconfigdirs target-libstdc++-v3 target-libgloss ${libgcj}"
    ;;
  c54x*-*-* | tic54x-*-*)
    noconfigdirs="$noconfigdirs target-libstdc++-v3 target-libgloss ${libgcj} gcc gdb newlib"
    ;;
  cris-*-* | crisv32-*-*)
    unsupported_languages="$unsupported_languages java"
    case "${target}" in
      *-*-aout)
	unsupported_languages="$unsupported_languages fortran"
	noconfigdirs="$noconfigdirs target-libffi target-boehm-gc";;
      *-*-elf)
	unsupported_languages="$unsupported_languages fortran"
	noconfigdirs="$noconfigdirs target-boehm-gc";;
      *-*-linux*)
	noconfigdirs="$noconfigdirs target-newlib target-libgloss";;
      *)
	unsupported_languages="$unsupported_languages fortran"
	noconfigdirs="$noconfigdirs ${libgcj} target-newlib target-libgloss";;
    esac
    ;;
  crx-*-*)
    noconfigdirs="$noconfigdirs target-libstdc++-v3 target-mudflap ${libgcj}"
    ;;
  d10v-*-*)
    noconfigdirs="$noconfigdirs target-libstdc++-v3 target-libgloss ${libgcj}"
    ;;
  d30v-*-*)
    noconfigdirs="$noconfigdirs ${libgcj} gdb"
    ;;
  fr30-*-elf*)
    noconfigdirs="$noconfigdirs ${libgcj} gdb"
    ;;
  frv-*-*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  h8300*-*-*)
    noconfigdirs="$noconfigdirs target-libgloss ${libgcj}"
    ;;
  h8500-*-*)
    noconfigdirs="$noconfigdirs target-libstdc++-v3 target-libgloss ${libgcj}"
    ;;
  hppa1.1-*-osf* | hppa1.1-*-bsd* )
    ;;
  hppa*64*-*-linux* | parisc*64*-*-linux*)
    # In this case, it's because the hppa64-linux target is for
    # the kernel only at this point and has no libc, and thus no
    # headers, crt*.o, etc., all of which are needed by these.
    noconfigdirs="$noconfigdirs target-zlib"
    ;;
  parisc*-*-linux* | hppa*-*-linux*)
    ;;
  hppa*-*-*elf* | \
  hppa*-*-lites* | \
  hppa*-*-openbsd* | \
  hppa*64*-*-*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  hppa*-hp-hpux11*)
    noconfigdirs="$noconfigdirs ld shellutils"
    ;;
  hppa*-*-*)
    # According to Alexandre Oliva <aoliva@redhat.com>, libjava won't
    # build on HP-UX 10.20.
    noconfigdirs="$noconfigdirs ld shellutils ${libgcj}"
    ;;
  i960-*-*)
    noconfigdirs="$noconfigdirs ${libgcj} gdb"
    ;;
  ia64*-*-elf*)
    # No gdb support yet.
    noconfigdirs="$noconfigdirs readline mmalloc libgui itcl gdb"
    ;;
  ia64*-**-hpux*)
    # No gdb or ld support yet.
    noconfigdirs="$noconfigdirs ${libgcj} readline mmalloc libgui itcl gdb ld"
    ;;
  i370-*-opened*)
    ;;
  i[[3456789]]86-*-coff | i[[3456789]]86-*-elf)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  i[[3456789]]86-*-linux*)
    # The GCC port for glibc1 has no MD_FALLBACK_FRAME_STATE_FOR, so let's
    # not build java stuff by default.
    case "${target}" in
      *-*-*libc1*)
	noconfigdirs="$noconfigdirs ${libgcj}";;
    esac

    # This section makes it possible to build newlib natively on linux.
    # If we are using a cross compiler then don't configure newlib.
    if test x${is_cross_compiler} != xno ; then
      noconfigdirs="$noconfigdirs target-newlib"
    fi
    noconfigdirs="$noconfigdirs target-libgloss"
    # If we are not using a cross compiler, do configure newlib.
    # Note however, that newlib will only be configured in this situation
    # if the --with-newlib option has been given, because otherwise
    # 'target-newlib' will appear in skipdirs.
    ;;
  i[[3456789]]86-*-mingw32*)
    target_configdirs="$target_configdirs target-winsup"
    noconfigdirs="$noconfigdirs expect target-libgloss target-newlib ${libgcj}"
    ;;    
  x86_64-*-mingw*)
    target_configdirs="$target_configdirs target-winsup"
    noconfigdirs="$noconfigdirs expect target-libgloss target-newlib ${libgcj}"
    ;;    
  *-*-cygwin*)
    target_configdirs="$target_configdirs target-libtermcap target-winsup"
    noconfigdirs="$noconfigdirs target-gperf target-libgloss ${libgcj}"
    # always build newlib if winsup directory is present.
    if test -d "$srcdir/winsup/cygwin"; then
      skipdirs=`echo " ${skipdirs} " | sed -e 's/ target-newlib / /'`
    elif test -d "$srcdir/newlib"; then
      echo "Warning: winsup/cygwin is missing so newlib can't be built."
    fi
    ;;    
  i[[3456789]]86-moss-msdos | i[[3456789]]86-*-moss* | \
  i[[3456789]]86-*-uwin* | i[[3456789]]86-*-interix* )
    ;;
  i[[3456789]]86-*-pe)
    noconfigdirs="$noconfigdirs target-libstdc++-v3 target-libgloss ${libgcj}"
    ;;
  i[[3456789]]86-*-sco3.2v5*)
    # The linker does not yet know about weak symbols in COFF,
    # and is not configured to handle mixed ELF and COFF.
    noconfigdirs="$noconfigdirs ld target-libgloss ${libgcj}"
    ;;
  i[[3456789]]86-*-sco*)
    noconfigdirs="$noconfigdirs gprof target-libgloss ${libgcj}"
    ;;
  i[[3456789]]86-*-solaris2*)
    noconfigdirs="$noconfigdirs target-libgloss"
    ;;
  i[[3456789]]86-*-sysv4*)
    noconfigdirs="$noconfigdirs target-libgloss ${libgcj}"
    ;;
  i[[3456789]]86-*-beos*)
    noconfigdirs="$noconfigdirs gdb target-newlib target-libgloss ${libgcj}"
    ;;
  i[[3456789]]86-*-rdos*)
    noconfigdirs="$noconfigdirs gdb target-newlib target-libgloss"
    ;;
  m32r-*-*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  m68hc11-*-*|m6811-*-*|m68hc12-*-*|m6812-*-*)
    noconfigdirs="$noconfigdirs target-libiberty target-libstdc++-v3 ${libgcj}"
    ;;
  m68k-*-elf*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  m68k-*-coff*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  mcore-*-pe*)
  # The EPOC C++ environment does not support exceptions or rtti,
  # and so building libstdc++-v3 tends not to always work.
    noconfigdirs="$noconfigdirs target-libstdc++-v3"
    ;;
  mmix-*-*)
    noconfigdirs="$noconfigdirs target-libffi target-boehm-gc gdb libgloss"
    unsupported_languages="$unsupported_languages fortran java"
    ;;
  mn10200-*-*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  mn10300-*-*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  mt-*-*)
    noconfigdirs="$noconfigdirs sim"
    ;;
  powerpc-*-aix*)
    # copied from rs6000-*-* entry
    noconfigdirs="$noconfigdirs gprof target-libgloss target-libssp ${libgcj}"
    ;;
  powerpc*-*-winnt* | powerpc*-*-pe* | ppc*-*-pe)
    target_configdirs="$target_configdirs target-winsup"
    noconfigdirs="$noconfigdirs gdb tcl tk make expect target-libgloss itcl gnuserv ${libgcj}"
    # always build newlib.
    skipdirs=`echo " ${skipdirs} " | sed -e 's/ target-newlib / /'`
    ;;
    # This is temporary until we can link against shared libraries
  powerpcle-*-solaris*)
    noconfigdirs="$noconfigdirs gdb sim make tcl tk expect itcl gnuserv ${libgcj}"
    ;;
  powerpc-*-beos*)
    noconfigdirs="$noconfigdirs gdb target-newlib target-libgloss ${libgcj}"
    ;;
  powerpc-*-eabi)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  powerpc-*-eabi* | powerpcle-*-eabi* | powerpc-*-rtems* )
    ;;
  rs6000-*-lynxos*)
    noconfigdirs="$noconfigdirs target-newlib gprof ${libgcj}"
    ;;
  rs6000-*-aix*)
    noconfigdirs="$noconfigdirs gprof target-libgloss target-libssp ${libgcj}"
    ;;
  rs6000-*-*)
    noconfigdirs="$noconfigdirs gprof ${libgcj}"
    ;;
  m68k-apollo-*)
    noconfigdirs="$noconfigdirs ld binutils gprof target-libgloss ${libgcj}"
    ;;
  mips*-*-irix5*)
    noconfigdirs="$noconfigdirs gprof target-libgloss ${libgcj}"
    ;;
  mips*-*-irix6*)
    # Linking libjava exceeds command-line length limits on at least
    # IRIX 6.2, but not on IRIX 6.5.
    # Also, boehm-gc won't build on IRIX 6.5, according to Jeffrey Oldham
    # <oldham@codesourcery.com>
    noconfigdirs="$noconfigdirs gprof target-libgloss ${libgcj}"
    ;;
  mips*-*-bsd*)
    noconfigdirs="$noconfigdirs gprof target-libgloss ${libgcj}"
    ;;
  mips64*-*-linux*)
    noconfigdirs="$noconfigdirs target-newlib target-libgloss ${libgcj}"
    ;;
  mips*-*-linux*)
    noconfigdirs="$noconfigdirs target-newlib target-libgloss"
    ;;
  mips*-*-*)
    noconfigdirs="$noconfigdirs gprof ${libgcj}"
    ;;
  romp-*-*)
    noconfigdirs="$noconfigdirs bfd binutils ld gas opcodes target-libgloss ${libgcj}"
    ;;
  sh-*-* | sh64-*-*)
    case "${host}" in
      i[[3456789]]86-*-vsta) ;; # don't add gprof back in
      i[[3456789]]86-*-go32*) ;; # don't add gprof back in
      i[[3456789]]86-*-msdosdjgpp*) ;; # don't add gprof back in
      *) skipdirs=`echo " ${skipdirs} " | sed -e 's/ gprof / /'` ;;
    esac
    noconfigdirs="$noconfigdirs target-libgloss ${libgcj}"
    ;;
  sparc-*-elf*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  sparc64-*-elf*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  sparclite-*-*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  sparc-*-sunos4*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    if test x${is_cross_compiler} != xno ; then
           noconfigdirs="$noconfigdirs gdb target-newlib target-libgloss"
    else
           use_gnu_ld=no
    fi
    ;;
  sparc-*-solaris2.[[0-6]] | sparc-*-solaris2.[[0-6]].*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  sparc-*-solaris* | sparc64-*-solaris* | sparcv9-*-solaris*)
    ;;
  spu-*-*)
    skipdirs="target-libssp"
    ;;
  v810-*-*)
    noconfigdirs="$noconfigdirs bfd binutils gas gcc gdb ld target-libstdc++-v3 opcodes target-libgloss ${libgcj}"
    ;;
  v850-*-*)
    noconfigdirs="$noconfigdirs target-libgloss ${libgcj}"
    ;;
  v850e-*-*)
    noconfigdirs="$noconfigdirs target-libgloss ${libgcj}"
    ;;
  v850ea-*-*)
    noconfigdirs="$noconfigdirs target-libgloss ${libgcj}"
    ;;
  vax-*-vms)
    noconfigdirs="$noconfigdirs bfd binutils gdb ld target-newlib opcodes target-libgloss ${libgcj}"
    ;;
  vax-*-*)
    noconfigdirs="$noconfigdirs target-newlib target-libgloss ${libgcj}"
    ;;
  xtensa-*-*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
  ip2k-*-*)
    noconfigdirs="$noconfigdirs target-libiberty target-libstdc++-v3 ${libgcj}"
    ;;
  *-*-linux* | *-*-gnu* | *-*-k*bsd*-gnu)
    noconfigdirs="$noconfigdirs target-newlib target-libgloss"
    ;;
  *-*-lynxos*)
    noconfigdirs="$noconfigdirs target-newlib target-libgloss ${libgcj}"
    ;; 
  *-*-*)
    noconfigdirs="$noconfigdirs ${libgcj}"
    ;;
esac

# If we aren't building newlib, then don't build libgloss, since libgloss
# depends upon some newlib header files.
case "${noconfigdirs}" in
  *target-libgloss*) ;;
  *target-newlib*) noconfigdirs="$noconfigdirs target-libgloss" ;;
esac

# Work in distributions that contain no compiler tools, like Autoconf.
tentative_cc=""
host_makefile_frag=/dev/null
if test -d ${srcdir}/config ; then
case "${host}" in
  m68k-hp-hpux*)
    # Avoid "too much defining" errors from HPUX compiler.
    tentative_cc="cc -Wp,-H256000"
    # If "ar" in $PATH is GNU ar, the symbol table may need rebuilding.
    # If it's HP/UX ar, this should be harmless.
    RANLIB="ar ts"
    ;;
  m68k-apollo-sysv*)
    tentative_cc="cc -A ansi -A runtype,any -A systype,any -U__STDC__ -DUSG"
    ;;
  m68k-apollo-bsd*)
    #None of the Apollo compilers can compile gas or binutils.  The preprocessor
    # chokes on bfd, the compiler won't let you assign integers to enums, and
    # other problems.  Defining CC to gcc is a questionable way to say "don't use
    # the apollo compiler" (the preferred version of GCC could be called cc,
    # or whatever), but I'm not sure leaving CC as cc is any better...
    #CC=cc -A ansi -A runtype,any -A systype,any -U__STDC__ -DNO_STDARG
    # Used to have BISON=yacc.
    tentative_cc=gcc
    ;;
  m88k-dg-dgux*)
    tentative_cc="gcc -Wall -ansi -D__using_DGUX"
    ;;
  m88k-harris-cxux*)
    # Under CX/UX, we want to tell the compiler to use ANSI mode.
    tentative_cc="cc -Xa"
    host_makefile_frag="config/mh-cxux"
    ;;
  m88k-motorola-sysv*)
    ;;
  mips*-dec-ultrix*)
    tentative_cc="cc -Wf,-XNg1000"
    host_makefile_frag="config/mh-decstation"
    ;;
  mips*-nec-sysv4*)
    # The C compiler on NEC MIPS SVR4 needs bigger tables.
    tentative_cc="cc -ZXNd=5000 -ZXNg=1000"
    host_makefile_frag="config/mh-necv4"
    ;;
  mips*-sgi-irix4*)
    # Tell compiler to use K&R C.  We can't compile under the SGI Ansi
    # environment.  Also bump switch table size so that cp-parse will
    # compile.  Bump string length limit so linker builds.
    tentative_cc="cc -cckr -Wf,-XNg1500 -Wf,-XNk1000 -Wf,-XNh2000 -Wf,-XNl8192"
    ;;
  mips*-*-sysv4*)
    host_makefile_frag="config/mh-sysv4"
    ;;
  mips*-*-sysv*)
    # This is for a MIPS running RISC/os 4.52C.

    # This is needed for GDB, but needs to be in the top-level make because
    # if a library is compiled with the bsd headers and gets linked with the
    # sysv system libraries all hell can break loose (e.g. a jmp_buf might be
    # a different size).
    # ptrace(2) apparently has problems in the BSD environment.  No workaround is
    # known except to select the sysv environment.  Could we use /proc instead?
    # These "sysv environments" and "bsd environments" often end up being a pain.
    #
    # This is not part of CFLAGS because perhaps not all C compilers have this
    # option.
    tentative_cc="cc -systype sysv"
    ;;
  i370-ibm-opened*)
    tentative_cc="c89"
    ;;
  i[[3456789]]86-*-sysv5*)
    host_makefile_frag="config/mh-sysv5"
    ;;
  i[[3456789]]86-*-dgux*)
    tentative_cc="gcc -Wall -ansi -D__using_DGUX"
    host_makefile_frag="config/mh-dgux386"
    ;;
  i[[3456789]]86-ncr-sysv4.3*)
    # The MetaWare compiler will generate a copyright message unless you
    # turn it off by adding the -Hnocopyr flag.
    tentative_cc="cc -Hnocopyr"
    ;;
  i[[3456789]]86-ncr-sysv4*)
    # for an NCR 3000 (i486/SVR4) system.
    # The NCR 3000 ships with a MetaWare compiler installed as /bin/cc.
    # This compiler not only emits obnoxious copyright messages every time
    # you run it, but it chokes and dies on a whole bunch of GNU source
    # files.  Default to using the AT&T compiler installed in /usr/ccs/ATT/cc.
    tentative_cc="/usr/ccs/ATT/cc"
    host_makefile_frag="config/mh-ncr3000"
    ;;
  i[[3456789]]86-*-sco3.2v5*)
    ;;
  i[[3456789]]86-*-sco*)
    # The native C compiler botches some simple uses of const.  Unfortunately,
    # it doesn't defined anything like "__sco__" for us to test for in ansidecl.h.
    tentative_cc="cc -Dconst="
    host_makefile_frag="config/mh-sco"
    ;;
  i[[3456789]]86-*-udk*)
    host_makefile_frag="config/mh-sysv5"
    ;;
  i[[3456789]]86-*-solaris2*)
    host_makefile_frag="config/mh-sysv4"
    ;;
  i[[3456789]]86-*-msdosdjgpp*)
    host_makefile_frag="config/mh-djgpp"
    ;;
  *-cygwin*)
    host_makefile_frag="config/mh-cygwin"
    ;;
  *-mingw32*)
    ;;
  *-mingw64*)
    ;;
  *-interix*)
    host_makefile_frag="config/mh-interix"
    ;;
  vax-*-ultrix2*)
    # The old BSD pcc isn't up to compiling parts of gdb so use gcc
    tentative_cc=gcc
    ;;
  *-*-solaris2*)
    host_makefile_frag="config/mh-solaris"
    ;;
  m68k-sun-sunos*)
    # Sun's C compiler needs the -J flag to be able to compile cp-parse.c
    # without overflowing the jump tables (-J says to use a 32 bit table)
    tentative_cc="cc -J"
    ;;
  *-hp-hpux*)
    tentative_cc="cc -Wp,-H256000"
    ;;
  *-*-hiux*)
    tentative_cc="cc -Wp,-H256000"
    ;;
  rs6000-*-lynxos*)
    # /bin/cc is less than useful for our purposes.  Always use GCC
    tentative_cc="/usr/cygnus/progressive/bin/gcc"
    host_makefile_frag="config/mh-lynxrs6k"
    ;;
  powerpc-*-darwin*)
    host_makefile_frag="config/mh-ppc-darwin"
    ;;
  powerpc-*-aix*)
    host_makefile_frag="config/mh-ppc-aix"
    ;;
  rs6000-*-aix*)
    host_makefile_frag="config/mh-ppc-aix"
    ;;
  *-*-lynxos*)
    # /bin/cc is less than useful for our purposes.  Always use GCC
    tentative_cc="/bin/gcc"
    ;;
  *-*-sysv4*)
    host_makefile_frag="config/mh-sysv4"
    ;;
  # This is placed last to prevent interfering with the cases above.
  i[[3456789]]86-*-*)
    # Build the stage2 and stage3 compilers with -fomit-frame-pointer.
    host_makefile_frag="config/mh-x86omitfp"
    ;;
esac
fi

# If we aren't going to be using gcc, see if we can extract a definition
# of CC from the fragment.
# Actually, use the 'pre-extracted' version above.
if test -z "${CC}" && test "${build}" = "${host}" ; then
  IFS="${IFS= 	}"; save_ifs="$IFS"; IFS="${IFS}:"
  found=
  for dir in $PATH; do
    test -z "$dir" && dir=.
    if test -f $dir/gcc; then
      found=yes
      break
    fi
  done
  IFS="$save_ifs"
  if test -z "${found}" && test -n "${tentative_cc}" ; then
    CC=$tentative_cc
  fi
fi

if test "${build}" != "${host}" ; then
  CC_FOR_BUILD=${CC_FOR_BUILD-gcc}
else
  CC_FOR_BUILD="\$(CC)"
fi

AC_PROG_CC
AC_PROG_CXX

# We must set the default linker to the linker used by gcc for the correct
# operation of libtool.  If LD is not defined and we are using gcc, try to
# set the LD default to the ld used by gcc.
if test -z "$LD"; then
  if test "$GCC" = yes; then
    case $build in
    *-*-mingw*)
      gcc_prog_ld=`$CC -print-prog-name=ld 2>&1 | tr -d '\015'` ;;
    *)
      gcc_prog_ld=`$CC -print-prog-name=ld 2>&1` ;;
    esac
    case $gcc_prog_ld in
    # Accept absolute paths.
    [[\\/]* | [A-Za-z]:[\\/]*)]
      LD="$gcc_prog_ld" ;;
    esac
  fi
fi

ACX_PROG_GNAT
ACX_PROG_CMP_IGNORE_INITIAL

# Check for GMP and MPFR
gmplibs="-lmpfr -lgmp"
gmpinc=
have_gmp=no

# Specify a location for mpfr
# check for this first so it ends up on the link line before gmp.
AC_ARG_WITH(mpfr-dir, [  --with-mpfr-dir=PATH    This option has been REMOVED],
  AC_MSG_ERROR([The --with-mpfr-dir=PATH option has been removed.
Use --with-mpfr=PATH or --with-mpfr-include=PATH plus --with-mpfr-lib=PATH]))

AC_ARG_WITH(mpfr, [  --with-mpfr=PATH        Specify prefix directory for installed MPFR package
                          Equivalent to --with-mpfr-include=PATH/include
                          plus --with-mpfr-lib=PATH/lib])
AC_ARG_WITH(mpfr_include, [  --with-mpfr-include=PATH
                          Specify directory for installed MPFR include files])
AC_ARG_WITH(mpfr_lib, [  --with-mpfr-lib=PATH    Specify the directory for the installed MPFR library])

if test "x$with_mpfr" != x; then
  gmplibs="-L$with_mpfr/lib $gmplibs"
  gmpinc="-I$with_mpfr/include"
fi
if test "x$with_mpfr_include" != x; then
  gmpinc="-I$with_mpfr_include"
fi
if test "x$with_mpfr_lib" != x; then
  gmplibs="-L$with_mpfr_lib $gmplibs"
fi
if test "x$with_mpfr$with_mpfr_include$with_mpfr_lib" = x && test -d ${srcdir}/mpfr; then
  gmplibs='-L$$r/$(HOST_SUBDIR)/mpfr/.libs -L$$r/$(HOST_SUBDIR)/mpfr/_libs '"$gmplibs"
  gmpinc='-I$$r/$(HOST_SUBDIR)/mpfr -I$$s/mpfr '"$gmpinc"
  # Do not test the mpfr version.  Assume that it is sufficient, since
  # it is in the source tree, and the library has not been built yet
  # but it would be included on the link line in the version check below
  # hence making the test fail.
  have_gmp=yes
fi

# Specify a location for gmp
AC_ARG_WITH(gmp-dir, [  --with-gmp-dir=PATH     This option has been REMOVED],
  AC_MSG_ERROR([The --with-gmp-dir=PATH option has been removed.
Use --with-gmp=PATH or --with-gmp-include=PATH plus --with-gmp-lib=PATH]))

AC_ARG_WITH(gmp, [  --with-gmp=PATH         Specify prefix directory for the installed GMP package
                          Equivalent to --with-gmp-include=PATH/include
                          plus --with-gmp-lib=PATH/lib])
AC_ARG_WITH(gmp_include, [  --with-gmp-include=PATH Specify directory for installed GMP include files])
AC_ARG_WITH(gmp_lib, [  --with-gmp-lib=PATH     Specify the directory for the installed GMP library])


if test "x$with_gmp" != x; then
  gmplibs="-L$with_gmp/lib $gmplibs"
  gmpinc="-I$with_gmp/include $gmpinc"
fi
if test "x$with_gmp_include" != x; then
  gmpinc="-I$with_gmp_include $gmpinc"
fi
if test "x$with_gmp_lib" != x; then
  gmplibs="-L$with_gmp_lib $gmplibs"
fi
if test "x$with_gmp$with_gmp_include$with_gmp_lib" = x && test -d ${srcdir}/gmp; then
  gmplibs='-L$$r/$(HOST_SUBDIR)/gmp/.libs -L$$r/$(HOST_SUBDIR)/gmp/_libs '"$gmplibs"
  gmpinc='-I$$r/$(HOST_SUBDIR)/gmp -I$$s/gmp '"$gmpinc"
  # Do not test the gmp version.  Assume that it is sufficient, since
  # it is in the source tree, and the library has not been built yet
  # but it would be included on the link line in the version check below
  # hence making the test fail.
  have_gmp=yes
fi

if test -d ${srcdir}/gcc && test "x$have_gmp" = xno; then
  have_gmp=yes
  saved_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $gmpinc"
  # Check GMP actually works
  AC_MSG_CHECKING([for correct version of gmp.h])
  AC_TRY_COMPILE([#include "gmp.h"],[
  #if __GNU_MP_VERSION < 4 || (__GNU_MP_VERSION == 4 && __GNU_MP_VERSION_MINOR < 1)
  choke me
  #endif
  ], [AC_MSG_RESULT([yes])], [AC_MSG_RESULT([no]); have_gmp=no])

  if test x"$have_gmp" = xyes; then
    saved_LIBS="$LIBS"
    LIBS="$LIBS $gmplibs"
    dnl MPFR 2.2.0 is acceptable but buggy, MPFR 2.2.1 is better.
    AC_MSG_CHECKING([for correct version of mpfr.h])
    AC_TRY_LINK([#include <gmp.h>
    #include <mpfr.h>],[
    #if MPFR_VERSION < MPFR_VERSION_NUM(2,2,0)
    choke me
    #endif
    mpfr_t n;
    mpfr_t x;
    int t;
    mpfr_init (n);
    mpfr_init (x);
    mpfr_atan2 (n, n, x, GMP_RNDN);
    mpfr_erfc (n, x, GMP_RNDN);
    mpfr_subnormalize (x, t, GMP_RNDN);
    ], [AC_TRY_LINK([#include <gmp.h>
    #include <mpfr.h>],[
    #if MPFR_VERSION < MPFR_VERSION_NUM(2,2,1)
    choke me
    #endif
    mpfr_t n; mpfr_init(n);
    ], [AC_MSG_RESULT([yes])], [AC_MSG_RESULT([buggy but acceptable])])],
     [AC_MSG_RESULT([no]); have_gmp=no])
      LIBS="$saved_LIBS"
  fi
  CFLAGS="$saved_CFLAGS"

  if test x$have_gmp != xyes; then
    AC_MSG_ERROR([Building GCC requires GMP 4.1+ and MPFR 2.2.1+.
Try the --with-gmp and/or --with-mpfr options to specify their locations.
Copies of these libraries' source code can be found at their respective
hosting sites as well as at ftp://gcc.gnu.org/pub/gcc/infrastructure/.
See also http://gcc.gnu.org/install/prerequisites.html for additional info.
If you obtained GMP and/or MPFR from a vendor distribution package, make
sure that you have installed both the libraries and the header files.
They may be located in separate packages.])
  fi
fi

# Flags needed for both GMP and/or MPFR
AC_SUBST(gmplibs)
AC_SUBST(gmpinc)

# By default, C is the only stage 1 language.
stage1_languages=,c,

# Figure out what language subdirectories are present.
# Look if the user specified --enable-languages="..."; if not, use
# the environment variable $LANGUAGES if defined. $LANGUAGES might
# go away some day.
# NB:  embedded tabs in this IF block -- do not untabify
if test -d ${srcdir}/gcc; then
  if test x"${enable_languages+set}" != xset; then
    if test x"${LANGUAGES+set}" = xset; then
      enable_languages="${LANGUAGES}"
        echo configure.in: warning: setting LANGUAGES is deprecated, use --enable-languages instead 1>&2
    else
      enable_languages=all
    fi
  else
    if test x"${enable_languages}" = x ||
       test x"${enable_languages}" = xyes;
       then
      echo configure.in: --enable-languages needs at least one language argument 1>&2
      exit 1
    fi
  fi
  enable_languages=`echo "${enable_languages}" | sed -e 's/[[ 	,]][[ 	,]]*/,/g' -e 's/,$//'`

  # 'f95' is the old name for the 'fortran' language. We issue a warning
  # and make the substitution.
  case ,${enable_languages}, in
    *,f95,*)
      echo configure.in: warning: 'f95' as language name is deprecated, use 'fortran' instead 1>&2
      enable_languages=`echo "${enable_languages}" | sed -e 's/f95/fortran/g'`
      ;;
  esac

  # First scan to see if an enabled language requires some other language.
  # We assume that a given config-lang.in will list all the language
  # front ends it requires, even if some are required indirectly.
  for lang_frag in ${srcdir}/gcc/*/config-lang.in .. ; do
    case ${lang_frag} in
      ..) ;;
      # The odd quoting in the next line works around
      # an apparent bug in bash 1.12 on linux.
      ${srcdir}/gcc/[[*]]/config-lang.in) ;;
      *)
        # From the config-lang.in, get $language, $lang_requires
        language=
        lang_requires=
        . ${lang_frag}
        for other in ${lang_requires} ; do
          case ,${enable_languages}, in
	    *,$other,*) ;;
	    *,all,*) ;;
	    *,$language,*)
	      echo " \`$other' language required by \`$language'; enabling" 1>&2
	      enable_languages="${enable_languages},${other}"
	      ;;
	  esac
        done
        ;;
    esac
  done

  new_enable_languages=,c,
  missing_languages=`echo ",$enable_languages," | sed -e s/,all,/,/ -e s/,c,/,/ `
  potential_languages=,c,

  for lang_frag in ${srcdir}/gcc/*/config-lang.in .. ; do
    case ${lang_frag} in
      ..) ;;
      # The odd quoting in the next line works around
      # an apparent bug in bash 1.12 on linux.
      ${srcdir}/gcc/[[*]]/config-lang.in) ;;
      *)
        # From the config-lang.in, get $language, $target_libs, 
        # $lang_dirs, $boot_language, and $build_by_default
        language=
        target_libs=
        lang_dirs=
        subdir_requires=
        boot_language=no
        build_by_default=yes
        . ${lang_frag}
        if test x${language} = x; then
          echo "${lang_frag} doesn't set \$language." 1>&2
          exit 1
        fi

        case ,${enable_languages}, in
          *,${language},*)
            # Language was explicitly selected; include it.
            add_this_lang=yes
            ;;
          *,all,*)
            # 'all' was selected, select it if it is a default language
            add_this_lang=${build_by_default}
            ;;
          *)
            add_this_lang=no
            ;;
        esac

        # Disable languages that need other directories if these aren't available.
	for i in $subdir_requires; do
	  test -f "$srcdir/gcc/$i/config-lang.in" && continue
	  case ,${enable_languages}, in
            *,${language},*)
              # Specifically requested language; tell them.
              AC_MSG_ERROR([The gcc/$i directory contains parts of $language but is missing])
              ;;
            *)
              # Silently disable.
              add_this_lang=unsupported
              ;;
          esac
	done

        # Disable Ada if no preexisting GNAT is available.
        case ,${enable_languages},:${language}:${have_gnat} in
          *,${language},*:ada:no)
            # Specifically requested language; tell them.
            AC_MSG_ERROR([GNAT is required to build $language])
            ;;
          *:ada:no)
            # Silently disable.
            add_this_lang=unsupported
            ;;
        esac

	# Disable a language that is unsupported by the target.
	case " $unsupported_languages " in
	  *" $language "*)
	    add_this_lang=unsupported
	    ;;
	esac

	case $add_this_lang in
	  unsupported)
            # Remove language-dependent dirs.
            eval noconfigdirs='"$noconfigdirs "'\"$target_libs $lang_dirs\"
	    ;;
	  no)
            # Remove language-dependent dirs; still show language as supported.
            eval noconfigdirs='"$noconfigdirs "'\"$target_libs $lang_dirs\"
            potential_languages="${potential_languages}${language},"
	    ;;
          yes)
	    new_enable_languages="${new_enable_languages}${language},"
            potential_languages="${potential_languages}${language},"
	    missing_languages=`echo "$missing_languages" | sed "s/,$language,/,/"`
	    case ${boot_language} in
	      yes)
		# Add to (comma-separated) list of stage 1 languages.
		stage1_languages="${stage1_languages}${language},"
		;;
	    esac
	    ;;
        esac
        ;;
    esac
  done

  AC_ARG_ENABLE(stage1-languages,
[  --enable-stage1-languages[=all]   choose additional languages to build during
                          stage1.  Mostly useful for compiler development.],
  [case ,${enable_stage1_languages}, in
    ,no,|,,)
      # Set it to something that will have no effect in the loop below
      enable_stage1_languages=c ;;
    ,yes,)
      enable_stage1_languages=`echo $new_enable_languages | \
	sed -e "s/^,//" -e "s/,$//" ` ;;
    *,all,*)
      enable_stage1_languages=`echo ,$enable_stage1_languages, | \
	sed -e "s/,all,/$new_enable_languages/" -e "s/^,//" -e "s/,$//" ` ;;
  esac

  # Add "good" languages from enable_stage1_languages to stage1_languages,
  # while "bad" languages go in missing_languages.  Leave no duplicates.
  for i in `echo $enable_stage1_languages | sed 's/,/ /g' `; do
    case $potential_languages in
      *,$i,*)
        case $stage1_languages in
          *,$i,*) ;;
          *) stage1_languages="$stage1_languages$i," ;;
        esac ;;
      *)
        case $missing_languages in
          *,$i,*) ;;
          *) missing_languages="$missing_languages$i," ;;
        esac ;;
     esac
  done])

  # Remove leading/trailing commas that were added for simplicity
  potential_languages=`echo "$potential_languages" | sed -e "s/^,//" -e "s/,$//"`
  missing_languages=`echo "$missing_languages" | sed -e "s/^,//" -e "s/,$//"`
  stage1_languages=`echo "$stage1_languages" | sed -e "s/^,//" -e "s/,$//"`
  new_enable_languages=`echo "$new_enable_languages" | sed -e "s/^,//" -e "s/,$//"`

  if test "x$missing_languages" != x; then
    AC_MSG_ERROR([
The following requested languages could not be built: ${missing_languages}
Supported languages are: ${potential_languages}])
  fi
  if test "x$new_enable_languages" != "x$enable_languages"; then
    echo The following languages will be built: ${new_enable_languages}
    enable_languages="$new_enable_languages"
  fi

  AC_SUBST(stage1_languages)
  ac_configure_args=`echo " $ac_configure_args" | sed -e "s/ '--enable-languages=[[^ ]]*'//g" -e "s/$/ '--enable-languages="$enable_languages"'/" `
fi

# Handle --disable-<component> generically.
for dir in $configdirs $build_configdirs $target_configdirs ; do
  dirname=`echo $dir | sed -e s/target-//g -e s/build-//g -e s/-/_/g`
  if eval test x\${enable_${dirname}} "=" xno ; then
    noconfigdirs="$noconfigdirs $dir"
  fi
done

# Check for Boehm's garbage collector
AC_ARG_ENABLE(objc-gc,
[  --enable-objc-gc       enable the use of Boehm's garbage collector with
                          the GNU Objective-C runtime.],
[case ,${enable_languages},:${enable_objc_gc}:${noconfigdirs} in
  *,objc,*:*:yes:*target-boehm-gc*)
    AC_MSG_ERROR([Boehm's garbage collector was requested yet not supported in this configuration])
    ;;
esac])

# Make sure we only build Boehm's garbage collector if required.
case ,${enable_languages},:${enable_objc_gc} in
  *,objc,*:yes)
    # Keep target-boehm-gc if requested for Objective-C.
    ;;
  *)
    # Otherwise remove target-boehm-gc depending on target-libjava.
    if echo " ${noconfigdirs} " | grep "target-libjava" >/dev/null 2>&1; then
      noconfigdirs="$noconfigdirs target-boehm-gc"
    fi
    ;;
esac

# Remove the entries in $skipdirs and $noconfigdirs from $configdirs,
# $build_configdirs and $target_configdirs.
# If we have the source for $noconfigdirs entries, add them to $notsupp.

notsupp=""
for dir in . $skipdirs $noconfigdirs ; do
  dirname=`echo $dir | sed -e s/target-//g -e s/build-//g`
  if test $dir != .  && echo " ${configdirs} " | grep " ${dir} " >/dev/null 2>&1; then
    configdirs=`echo " ${configdirs} " | sed -e "s/ ${dir} / /"`
    if test -r $srcdir/$dirname/configure ; then
      if echo " ${skipdirs} " | grep " ${dir} " >/dev/null 2>&1; then
	true
      else
	notsupp="$notsupp $dir"
      fi
    fi
  fi
  if test $dir != .  && echo " ${build_configdirs} " | grep " ${dir} " >/dev/null 2>&1; then
    build_configdirs=`echo " ${build_configdirs} " | sed -e "s/ ${dir} / /"`
    if test -r $srcdir/$dirname/configure ; then
      if echo " ${skipdirs} " | grep " ${dir} " >/dev/null 2>&1; then
	true
      else
	notsupp="$notsupp $dir"
      fi
    fi
  fi
  if test $dir != . && echo " ${target_configdirs} " | grep " ${dir} " >/dev/null 2>&1; then
    target_configdirs=`echo " ${target_configdirs} " | sed -e "s/ ${dir} / /"`
    if test -r $srcdir/$dirname/configure ; then
      if echo " ${skipdirs} " | grep " ${dir} " >/dev/null 2>&1; then
	true
      else
	notsupp="$notsupp $dir"
      fi
    fi
  fi
done

# Sometimes the tools are distributed with libiberty but with no other
# libraries.  In that case, we don't want to build target-libiberty.
# Don't let libgcc imply libiberty either.
if test -n "${target_configdirs}" ; then
  libgcc=
  others=
  for i in `echo ${target_configdirs} | sed -e s/target-//g` ; do
    if test "$i" = "libgcc"; then
      libgcc=target-libgcc
    elif test "$i" != "libiberty" ; then
      if test -r $srcdir/$i/configure ; then
	others=yes;
	break;
      fi
    fi
  done
  if test -z "${others}" ; then
    target_configdirs=$libgcc
  fi
fi

# Quietly strip out all directories which aren't configurable in this tree.
# This relies on all configurable subdirectories being autoconfiscated, which
# is now the case.
build_configdirs_all="$build_configdirs"
build_configdirs=
for i in ${build_configdirs_all} ; do
  j=`echo $i | sed -e s/build-//g` 
  if test -f ${srcdir}/$j/configure ; then
    build_configdirs="${build_configdirs} $i"
  fi
done

configdirs_all="$configdirs"
configdirs=
for i in ${configdirs_all} ; do
  if test -f ${srcdir}/$i/configure ; then
    configdirs="${configdirs} $i"
  fi
done

target_configdirs_all="$target_configdirs"
target_configdirs=
for i in ${target_configdirs_all} ; do
  j=`echo $i | sed -e s/target-//g` 
  if test -f ${srcdir}/$j/configure ; then
    target_configdirs="${target_configdirs} $i"
  fi
done

# Produce a warning message for the subdirs we can't configure.
# This isn't especially interesting in the Cygnus tree, but in the individual
# FSF releases, it's important to let people know when their machine isn't
# supported by the one or two programs in a package.

if test -n "${notsupp}" && test -z "${norecursion}" ; then
  # If $appdirs is non-empty, at least one of those directories must still
  # be configured, or we error out.  (E.g., if the gas release supports a
  # specified target in some subdirs but not the gas subdir, we shouldn't
  # pretend that all is well.)
  if test -n "$appdirs" ; then
    for dir in $appdirs ; do
      if test -r $dir/Makefile.in ; then
	if echo " ${configdirs} " | grep " ${dir} " >/dev/null 2>&1; then
	  appdirs=""
	  break
	fi
	if echo " ${target_configdirs} " | grep " target-${dir} " >/dev/null 2>&1; then
	  appdirs=""
	  break
	fi
      fi
    done
    if test -n "$appdirs" ; then
      echo "*** This configuration is not supported by this package." 1>&2
      exit 1
    fi
  fi
  # Okay, some application will build, or we don't care to check.  Still
  # notify of subdirs not getting built.
  echo "*** This configuration is not supported in the following subdirectories:" 1>&2
  echo "    ${notsupp}" 1>&2
  echo "    (Any other directories should still work fine.)" 1>&2
fi

case "$host" in
  *msdosdjgpp*)
    enable_gdbtk=no ;;
esac

# To find our prefix, in gcc_cv_tool_prefix.
ACX_TOOL_DIRS

copy_dirs=

AC_ARG_WITH([build-sysroot], 
  [  --with-build-sysroot=sysroot
                          use sysroot as the system root during the build],
  [if test x"$withval" != x ; then
     SYSROOT_CFLAGS_FOR_TARGET="--sysroot=$withval"
   fi],
  [SYSROOT_CFLAGS_FOR_TARGET=])
AC_SUBST(SYSROOT_CFLAGS_FOR_TARGET)

# Handle --with-headers=XXX.  If the value is not "yes", the contents of
# the named directory are copied to $(tooldir)/sys-include.
if test x"${with_headers}" != x && test x"${with_headers}" != xno ; then
  if test x${is_cross_compiler} = xno ; then
    echo 1>&2 '***' --with-headers is only supported when cross compiling
    exit 1
  fi
  if test x"${with_headers}" != xyes ; then
    x=${gcc_cv_tool_prefix}
    copy_dirs="${copy_dirs} ${with_headers} $x/${target_noncanonical}/sys-include"
  fi
fi

# Handle --with-libs=XXX.  If the value is not "yes", the contents of
# the name directories are copied to $(tooldir)/lib.  Multiple directories
# are permitted.
if test x"${with_libs}" != x && test x"${with_libs}" != xno ; then
  if test x${is_cross_compiler} = xno ; then
    echo 1>&2 '***' --with-libs is only supported when cross compiling
    exit 1
  fi
  if test x"${with_libs}" != xyes ; then
    # Copy the libraries in reverse order, so that files in the first named
    # library override files in subsequent libraries.
    x=${gcc_cv_tool_prefix}
    for l in ${with_libs}; do
      copy_dirs="$l $x/${target_noncanonical}/lib ${copy_dirs}"
    done
  fi
fi

# Set with_gnu_as and with_gnu_ld as appropriate.
#
# This is done by determining whether or not the appropriate directory
# is available, and by checking whether or not specific configurations
# have requested that this magic not happen.
# 
# The command line options always override the explicit settings in 
# configure.in, and the settings in configure.in override this magic.
#
# If the default for a toolchain is to use GNU as and ld, and you don't 
# want to do that, then you should use the --without-gnu-as and
# --without-gnu-ld options for the configure script.

if test x${use_gnu_as} = x &&
   echo " ${configdirs} " | grep " gas " > /dev/null 2>&1 ; then
  with_gnu_as=yes
  extra_host_args="$extra_host_args --with-gnu-as"
fi

if test x${use_gnu_ld} = x &&
   echo " ${configdirs} " | grep " ld " > /dev/null 2>&1 ; then
  with_gnu_ld=yes
  extra_host_args="$extra_host_args --with-gnu-ld"
fi

# If using newlib, add --with-newlib to the extra_host_args so that gcc/configure
# can detect this case.

if test x${with_newlib} != xno && echo " ${target_configdirs} " | grep " target-newlib " > /dev/null 2>&1 ; then
  with_newlib=yes
  extra_host_args="$extra_host_args --with-newlib"
fi

# Handle ${copy_dirs}
set fnord ${copy_dirs}
shift
while test $# != 0 ; do
  if test -f $2/COPIED && test x"`cat $2/COPIED`" = x"$1" ; then
    :
  else
    echo Copying $1 to $2

    # Use the install script to create the directory and all required
    # parent directories.
    if test -d $2 ; then
      :
    else
      echo >config.temp
      ${srcdir}/install-sh -c -m 644 config.temp $2/COPIED
    fi

    # Copy the directory, assuming we have tar.
    # FIXME: Should we use B in the second tar?  Not all systems support it.
    (cd $1; tar -cf - .) | (cd $2; tar -xpf -)

    # It is the responsibility of the user to correctly adjust all
    # symlinks.  If somebody can figure out how to handle them correctly
    # here, feel free to add the code.

    echo $1 > $2/COPIED
  fi
  shift; shift
done

# Determine a target-dependent exec_prefix that the installed
# gcc will search in.  Keep this list sorted by triplet, with
# the *-*-osname triplets last.
md_exec_prefix=
case "${target}" in
  alpha*-*-*vms*)
    md_exec_prefix=/gnu/lib/gcc-lib
    ;;
  i[34567]86-pc-msdosdjgpp*)
    md_exec_prefix=/dev/env/DJDIR/bin
    ;;
  i[34567]86-*-sco3.2v5*)
    if test $with_gnu_as = yes; then
      md_exec_prefix=/usr/gnu/bin
    else
      md_exec_prefix=/usr/ccs/bin/elf
    fi
    ;;

  mn10300-*-* | \
  powerpc-*-chorusos* | \
  powerpc*-*-eabi* | \
  powerpc*-*-sysv* | \
  powerpc*-*-kaos* | \
  s390x-ibm-tpf*)
    md_exec_prefix=/usr/ccs/bin
    ;;
  sparc64-*-elf*)
    ;;
  v850*-*-*)
    md_exec_prefix=/usr/ccs/bin
    ;;
  xtensa-*-elf*)
    ;;

  *-*-beos* | \
  *-*-elf* | \
  *-*-hpux* | \
  *-*-netware* | \
  *-*-nto-qnx* | \
  *-*-rtems* | \
  *-*-solaris2* | \
  *-*-sysv[45]* | \
  *-*-vxworks* | \
  *-wrs-windiss)
    md_exec_prefix=/usr/ccs/bin
    ;;
esac

extra_arflags_for_target=
extra_nmflags_for_target=
extra_ranlibflags_for_target=
target_makefile_frag=/dev/null
case "${target}" in
  mep*-*-*)
    target_makefile_frag="config/mt-mep"
    ;;
  spu-*-*)
    target_makefile_frag="config/mt-spu"
    ;;
  *-*-netware*)
    target_makefile_frag="config/mt-netware"
    ;;
  *-*-linux* | *-*-gnu* | *-*-k*bsd*-gnu)
    target_makefile_frag="config/mt-gnu"
    ;;
  *-*-aix4.[[3456789]]* | *-*-aix[[56789]].*)
    # nm and ar from AIX 4.3 and above require -X32_64 flag to all ar and nm 
    # commands to handle both 32-bit and 64-bit objects.  These flags are
    # harmless if we're using GNU nm or ar.
    extra_arflags_for_target=" -X32_64"
    extra_nmflags_for_target=" -B -X32_64"
    ;;
  *-*-darwin*)
    # ranlib from Darwin requires the -c flag to look at common symbols.
    extra_ranlibflags_for_target=" -c"
    ;;
  mips*-*-pe | sh*-*-pe | *arm-wince-pe)
    target_makefile_frag="config/mt-wince"
    ;;
esac

alphaieee_frag=/dev/null
case $target in
  alpha*-*-*)
    # This just makes sure to use the -mieee option to build target libs.
    # This should probably be set individually by each library.
    alphaieee_frag="config/mt-alphaieee"
    ;;
esac

# If --enable-target-optspace always use -Os instead of -O2 to build
# the target libraries, similarly if it is not specified, use -Os
# on selected platforms.
ospace_frag=/dev/null
case "${enable_target_optspace}:${target}" in
  yes:*)
    ospace_frag="config/mt-ospace"
    ;;
  :d30v-*)
    ospace_frag="config/mt-d30v"
    ;;
  :m32r-* | :d10v-* | :fr30-*)
    ospace_frag="config/mt-ospace"
    ;;
  no:* | :*)
    ;;
  *)
    echo "*** bad value \"${enable_target_optspace}\" for --enable-target-optspace flag; ignored" 1>&2
    ;;
esac

# Default to using --with-stabs for certain targets.
if test x${with_stabs} = x ; then
  case "${target}" in
  mips*-*-irix[[56]]*)
    ;;
  mips*-*-* | alpha*-*-osf*)
    with_stabs=yes;
    extra_host_args="${extra_host_args} --with-stabs"
    ;;
  esac
fi

# hpux11 in 64bit mode has libraries in a weird place.  Arrange to find
# them automatically.
case "${host}" in
  hppa*64*-*-hpux11*)	
    extra_host_args="$extra_host_args -x-libraries=/usr/lib/pa20_64 -x-includes=/usr/X11R6/include"
    ;;
esac

# Some systems (e.g., one of the i386-aix systems the gas testers are
# using) don't handle "\$" correctly, so don't use it here.
tooldir='${exec_prefix}'/${target_noncanonical}
build_tooldir=${tooldir}

# Create a .gdbinit file which runs the one in srcdir
# and tells GDB to look there for source files.

if test -r ${srcdir}/.gdbinit ; then
  case ${srcdir} in
    .) ;;
    *) cat > ./.gdbinit <<EOF
# ${NO_EDIT}
dir ${srcdir}
dir .
source ${srcdir}/.gdbinit
EOF
    ;;
  esac
fi

# Make sure that the compiler is able to generate an executable.  If it
# can't, we are probably in trouble.  We don't care whether we can run the
# executable--we might be using a cross compiler--we only care whether it
# can be created.  At this point the main configure script has set CC.
we_are_ok=no
echo "int main () { return 0; }" > conftest.c
${CC} -o conftest ${CFLAGS} ${CPPFLAGS} ${LDFLAGS} conftest.c
if test $? = 0 ; then
  if test -s conftest || test -s conftest.exe ; then
    we_are_ok=yes
  fi
fi 
case $we_are_ok in
  no)
    echo 1>&2 "*** The command '${CC} -o conftest ${CFLAGS} ${CPPFLAGS} ${LDFLAGS} conftest.c' failed."
    echo 1>&2 "*** You must set the environment variable CC to a working compiler."
    rm -f conftest*
    exit 1
    ;;
esac
rm -f conftest*

# The Solaris /usr/ucb/cc compiler does not appear to work.
case "${host}" in
  sparc-sun-solaris2*)
      CCBASE="`echo ${CC-cc} | sed 's/ .*$//'`"
      if test "`type $CCBASE | sed 's/^[[^/]]*//'`" = "/usr/ucb/cc" ; then
          could_use=
          test -d /opt/SUNWspro/bin && could_use="/opt/SUNWspro/bin"
          if test -d /opt/cygnus/bin ; then
              if test "$could_use" = "" ; then
                  could_use="/opt/cygnus/bin"
              else
                  could_use="$could_use or /opt/cygnus/bin"
              fi
          fi
        if test "$could_use" = "" ; then
            echo "Warning: compilation may fail because you're using"
            echo "/usr/ucb/cc.  You should change your PATH or CC "
            echo "variable and rerun configure."
        else
            echo "Warning: compilation may fail because you're using"
            echo "/usr/ucb/cc, when you should use the C compiler from"
            echo "$could_use.  You should change your"
            echo "PATH or CC variable and rerun configure."
        fi
      fi
  ;;
esac

case "${host}" in
  *-*-hpux*) RPATH_ENVVAR=SHLIB_PATH ;;
  *-*-darwin* | *-*-rhapsody* ) RPATH_ENVVAR=DYLD_LIBRARY_PATH ;;
  *) RPATH_ENVVAR=LD_LIBRARY_PATH ;;
esac

# Record target_configdirs and the configure arguments for target and
# build configuration in Makefile.
target_configdirs=`echo "${target_configdirs}" | sed -e 's/target-//g'`
build_configdirs=`echo "${build_configdirs}" | sed -e 's/build-//g'`

# Determine whether gdb needs tk/tcl or not.
# Use 'maybe' since enable_gdbtk might be true even if tk isn't available
# and in that case we want gdb to be built without tk.  Ugh!
# In fact I believe gdb is the *only* package directly dependent on tk,
# so we should be able to put the 'maybe's in unconditionally and
# leave out the maybe dependencies when enable_gdbtk is false.  I'm not
# 100% sure that that's safe though.

gdb_tk="maybe-all-tcl maybe-all-tk maybe-all-itcl maybe-all-libgui"
case "$enable_gdbtk" in
  no)
    GDB_TK="" ;;
  yes)
    GDB_TK="${gdb_tk}" ;;
  *)
    # Only add the dependency on gdbtk when GDBtk is part of the gdb
    # distro.  Eventually someone will fix this and move Insight, nee
    # gdbtk to a separate directory.
    if test -d ${srcdir}/gdb/gdbtk ; then
      GDB_TK="${gdb_tk}"
    else
      GDB_TK=""
    fi
    ;;
esac
CONFIGURE_GDB_TK=`echo ${GDB_TK} | sed s/-all-/-configure-/g`
INSTALL_GDB_TK=`echo ${GDB_TK} | sed s/-all-/-install-/g`

# Strip out unwanted targets.

# While at that, we remove Makefiles if we were started for recursive
# configuration, so that the top-level Makefile reconfigures them,
# like we used to do when configure itself was recursive.

# Loop over modules.  $extrasub must be used with care, limiting as
# much as possible the usage of range addresses.  That's because autoconf
# splits the sed script to overcome limits in the number of commands,
# and relying on carefully-timed sed passes may turn out to be very hard
# to maintain later.  In this particular case, you just have to be careful
# not to nest @if/@endif pairs, because configure will not warn you at all.

AC_ARG_ENABLE([bootstrap],
[  --enable-bootstrap           Enable bootstrapping [yes if native build]],,
enable_bootstrap=default)

# Issue errors and warnings for invalid/strange bootstrap combinations.
case "$configdirs" in
  *gcc*) have_compiler=yes ;;
  *) have_compiler=no ;;
esac

case "$have_compiler:$host:$target:$enable_bootstrap" in
  *:*:*:no) ;;

  # Default behavior.  Enable bootstrap if we have a compiler
  # and we are in a native configuration.
  yes:$build:$build:default)
    enable_bootstrap=yes ;;

  *:*:*:default)
    enable_bootstrap=no ;;

  # We have a compiler and we are in a native configuration, bootstrap is ok
  yes:$build:$build:yes)
    ;;

  # Other configurations, but we have a compiler.  Assume the user knows
  # what he's doing.
  yes:*:*:yes)
    AC_MSG_WARN([trying to bootstrap a cross compiler])
    ;;

  # No compiler: if they passed --enable-bootstrap explicitly, fail
  no:*:*:yes)
    AC_MSG_ERROR([cannot bootstrap without a compiler]) ;;

  # Fail if wrong command line
  *)
    AC_MSG_ERROR([invalid option for --enable-bootstrap])
    ;;
esac

# Adjust the toplevel makefile according to whether bootstrap was selected.
case "$enable_bootstrap" in
  yes)
    bootstrap_suffix=bootstrap ;;
  no)
    bootstrap_suffix=no-bootstrap ;;
esac

for module in ${build_configdirs} ; do
  if test -z "${no_recursion}" \
     && test -f ${build_subdir}/${module}/Makefile; then
    echo 1>&2 "*** removing ${build_subdir}/${module}/Makefile to force reconfigure"
    rm -f ${build_subdir}/${module}/Makefile
  fi
  extrasub="$extrasub
/^@if build-$module\$/d
/^@endif build-$module\$/d
/^@if build-$module-$bootstrap_suffix\$/d
/^@endif build-$module-$bootstrap_suffix\$/d"
done
for module in ${configdirs} ; do
  if test -z "${no_recursion}"; then
    for file in stage*-${module}/Makefile prev-${module}/Makefile ${module}/Makefile; do
      if test -f ${file}; then
	echo 1>&2 "*** removing ${file} to force reconfigure"
	rm -f ${file}
      fi
    done
  fi
  extrasub="$extrasub
/^@if $module\$/d
/^@endif $module\$/d
/^@if $module-$bootstrap_suffix\$/d
/^@endif $module-$bootstrap_suffix\$/d"
done
for module in ${target_configdirs} ; do
  if test -z "${no_recursion}" \
     && test -f ${target_subdir}/${module}/Makefile; then
    echo 1>&2 "*** removing ${target_subdir}/${module}/Makefile to force reconfigure"
    rm -f ${target_subdir}/${module}/Makefile
  fi
  extrasub="$extrasub
/^@if target-$module\$/d
/^@endif target-$module\$/d
/^@if target-$module-$bootstrap_suffix\$/d
/^@endif target-$module-$bootstrap_suffix\$/d"
done

extrasub="$extrasub
/^@if /,/^@endif /d"

# Create the serialization dependencies.  This uses a temporary file.

AC_ARG_ENABLE([serial-configure],
[  --enable-serial-[{host,target,build}-]configure
                          Force sequential configuration of
                          sub-packages for the host, target or build
			  machine, or all sub-packages])

case ${enable_serial_configure} in
  yes)
    enable_serial_build_configure=yes
    enable_serial_host_configure=yes
    enable_serial_target_configure=yes
    ;;
esac

# These force 'configure's to be done one at a time, to avoid problems
# with contention over a shared config.cache.
rm -f serdep.tmp
echo '# serdep.tmp' > serdep.tmp
olditem=
test "x${enable_serial_build_configure}" = xyes &&
for item in ${build_configdirs} ; do
  case ${olditem} in
    "") ;;
    *) echo "configure-build-${item}: configure-build-${olditem}" >> serdep.tmp ;;
  esac
  olditem=${item}
done
olditem=
test "x${enable_serial_host_configure}" = xyes &&
for item in ${configdirs} ; do
  case ${olditem} in
    "") ;;
    *) echo "configure-${item}: configure-${olditem}" >> serdep.tmp ;;
  esac
  olditem=${item}
done
olditem=
test "x${enable_serial_target_configure}" = xyes &&
for item in ${target_configdirs} ; do
  case ${olditem} in
    "") ;;
    *) echo "configure-target-${item}: configure-target-${olditem}" >> serdep.tmp ;;
  esac
  olditem=${item}
done
serialization_dependencies=serdep.tmp
AC_SUBST_FILE(serialization_dependencies)

# Base args.  Strip norecursion, cache-file, srcdir, host, build,
# target, nonopt, and variable assignments.  These are the ones we
# might not want to pass down to subconfigures.  Also strip
# program-prefix, program-suffix, and program-transform-name, so that
# we can pass down a consistent program-transform-name.
baseargs=
keep_next=no
skip_next=no
eval "set -- $ac_configure_args"
for ac_arg
do
  if test X"$skip_next" = X"yes"; then
    skip_next=no
    continue
  fi
  if test X"$keep_next" = X"yes"; then
    case $ac_arg in
      *\'*)
	ac_arg=`echo "$ac_arg" | sed "s/'/'\\\\\\\\''/g"` ;;
    esac
    baseargs="$baseargs '$ac_arg'"
    keep_next=no
    continue
  fi

  # Handle separated arguments.  Based on the logic generated by
  # autoconf 2.59.
  case $ac_arg in
    *=* | --config-cache | -C | -disable-* | --disable-* \
      | -enable-* | --enable-* | -gas | --g* | -nfp | --nf* \
      | -q | -quiet | --q* | -silent | --sil* | -v | -verb* \
      | -with-* | --with-* | -without-* | --without-* | --x)
      separate_arg=no
      ;;
    -*)
      separate_arg=yes
      ;;
    *)
      separate_arg=no
      ;;
  esac

  case "$ac_arg" in
    --no*)
      continue
      ;;
    --c* | \
    --sr* | \
    --ho* | \
    --bu* | \
    --t* | \
    --program-* | \
    -cache_file* | \
    -srcdir* | \
    -host* | \
    -build* | \
    -target* | \
    -program-prefix* | \
    -program-suffix* | \
    -program-transform-name* )
      skip_next=$separate_arg
      continue
      ;;
    -*)
      # An option.  Add it.
      case $ac_arg in
	*\'*)
	  ac_arg=`echo "$ac_arg" | sed "s/'/'\\\\\\\\''/g"` ;;
      esac
      baseargs="$baseargs '$ac_arg'"
      keep_next=$separate_arg
      ;;
    *)
      # Either a variable assignment, or a nonopt (triplet).  Don't
      # pass it down; let the Makefile handle this.
      continue
      ;;
  esac
done
# Remove the initial space we just introduced and, as these will be
# expanded by make, quote '$'.
baseargs=`echo "x$baseargs" | sed -e 's/^x *//' -e 's,\\$,$$,g'`

# Add in --program-transform-name, after --program-prefix and
# --program-suffix have been applied to it.  Autoconf has already
# doubled dollar signs and backslashes in program_transform_name; we want
# the backslashes un-doubled, and then the entire thing wrapped in single
# quotes, because this will be expanded first by make and then by the shell. 
# Also, because we want to override the logic in subdir configure scripts to
# choose program_transform_name, replace any s,x,x, with s,y,y,.
sed -e "s,\\\\\\\\,\\\\,g; s,','\\\\'',g; s/s,x,x,/s,y,y,/" <<EOF_SED > conftestsed.out
${program_transform_name}
EOF_SED
gcc_transform_name=`cat conftestsed.out`
rm -f conftestsed.out
baseargs="$baseargs --program-transform-name='${gcc_transform_name}'"

# For the build-side libraries, we just need to pretend we're native,
# and not use the same cache file.  Multilibs are neither needed nor
# desired.
build_configargs="--cache-file=../config.cache ${baseargs}"

# For host modules, accept cache file option, or specification as blank.
case "${cache_file}" in
"") # empty
  cache_file_option="" ;;
/* | [[A-Za-z]]:[[\\/]]* ) # absolute path
  cache_file_option="--cache-file=${cache_file}" ;;
*) # relative path
  cache_file_option="--cache-file=../${cache_file}" ;;
esac

# Host dirs don't like to share a cache file either, horribly enough.
# This seems to be due to autoconf 2.5x stupidity.
host_configargs="--cache-file=./config.cache ${extra_host_args} ${baseargs}"

target_configargs=${baseargs}

# Passing a --with-cross-host argument lets the target libraries know
# whether they are being built with a cross-compiler or being built
# native.  However, it would be better to use other mechanisms to make the
# sorts of decisions they want to make on this basis.  Please consider
# this option to be deprecated.  FIXME.
if test x${is_cross_compiler} = xyes ; then
  target_configargs="--with-cross-host=${host_noncanonical} ${target_configargs}"
fi

# Default to --enable-multilib.
if test x${enable_multilib} = x ; then
  target_configargs="--enable-multilib ${target_configargs}"
fi

# Pass --with-newlib if appropriate.  Note that target_configdirs has
# changed from the earlier setting of with_newlib.
if test x${with_newlib} != xno && echo " ${target_configdirs} " | grep " newlib " > /dev/null 2>&1 && test -d ${srcdir}/newlib ; then
  target_configargs="--with-newlib ${target_configargs}"
fi

# Different target subdirs use different values of certain variables
# (notably CXX).  Worse, multilibs use *lots* of different values.
# Worse yet, autoconf 2.5x makes some of these 'precious', meaning that
# it doesn't automatically accept command-line overrides of them.
# This means it's not safe for target subdirs to share a cache file,
# which is disgusting, but there you have it.  Hopefully this can be
# fixed in future.  It's still worthwhile to use a cache file for each
# directory.  I think.

# Pass the appropriate --build, --host, --target and --cache-file arguments.
# We need to pass --target, as newer autoconf's requires consistency
# for target_alias and gcc doesn't manage it consistently.
target_configargs="--cache-file=./config.cache ${target_configargs}"

FLAGS_FOR_TARGET=
case " $target_configdirs " in
 *" newlib "*)
  case " $target_configargs " in
  *" --with-newlib "*)
   case "$target" in
   *-cygwin*)
     FLAGS_FOR_TARGET=$FLAGS_FOR_TARGET' -L$$r/$(TARGET_SUBDIR)/winsup -L$$r/$(TARGET_SUBDIR)/winsup/cygwin -L$$r/$(TARGET_SUBDIR)/winsup/w32api/lib -isystem $$s/winsup/include -isystem $$s/winsup/cygwin/include -isystem $$s/winsup/w32api/include' ;;
   esac

   # If we're not building GCC, don't discard standard headers.
   if test -d ${srcdir}/gcc; then
     FLAGS_FOR_TARGET=$FLAGS_FOR_TARGET' -nostdinc'

     if test "${build}" != "${host}"; then
       # On Canadian crosses, CC_FOR_TARGET will have already been set
       # by `configure', so we won't have an opportunity to add -Bgcc/
       # to it.  This is right: we don't want to search that directory
       # for binaries, but we want the header files in there, so add
       # them explicitly.
       FLAGS_FOR_TARGET=$FLAGS_FOR_TARGET' -isystem $$r/$(HOST_SUBDIR)/gcc/include'

       # Someone might think of using the pre-installed headers on
       # Canadian crosses, in case the installed compiler is not fully
       # compatible with the compiler being built.  In this case, it
       # would be better to flag an error than risking having
       # incompatible object files being constructed.  We can't
       # guarantee that an error will be flagged, but let's hope the
       # compiler will do it, when presented with incompatible header
       # files.
     fi
   fi

   case "${target}-${is_cross_compiler}" in
   i[[3456789]]86-*-linux*-no)
      # Here host == target, so we don't need to build gcc,
      # so we don't want to discard standard headers.
      FLAGS_FOR_TARGET=`echo " $FLAGS_FOR_TARGET " | sed -e 's/ -nostdinc / /'`
      ;;
   *)
      # If we're building newlib, use its generic headers last, but search
      # for any libc-related directories first (so make it the last -B
      # switch).
      FLAGS_FOR_TARGET=$FLAGS_FOR_TARGET' -B$$r/$(TARGET_SUBDIR)/newlib/ -isystem $$r/$(TARGET_SUBDIR)/newlib/targ-include -isystem $$s/newlib/libc/include'
      ;;
   esac
   ;;
  esac
  ;;
esac
case "$target" in
*-mingw*)
  # Can't be handled as Cygwin above since Mingw does not use newlib.
  FLAGS_FOR_TARGET=$FLAGS_FOR_TARGET' -L$$r/$(TARGET_SUBDIR)/winsup/mingw -L$$r/$(TARGET_SUBDIR)/winsup/w32api/lib -isystem $$s/winsup/mingw/include -isystem $$s/winsup/w32api/include' ;;
esac

# Allow the user to override the flags for
# our build compiler if desired.
CFLAGS_FOR_BUILD=${CFLAGS_FOR_BUILD-${CFLAGS}}

# On Canadian crosses, we'll be searching the right directories for
# the previously-installed cross compiler, so don't bother to add
# flags for directories within the install tree of the compiler
# being built; programs in there won't even run.
if test "${build}" = "${host}" && test -d ${srcdir}/gcc; then
  # Search for pre-installed headers if nothing else fits.
  FLAGS_FOR_TARGET=$FLAGS_FOR_TARGET' -B$(build_tooldir)/bin/ -B$(build_tooldir)/lib/ -isystem $(build_tooldir)/include -isystem $(build_tooldir)/sys-include'
fi

if test "x${use_gnu_ld}" = x &&
   echo " ${configdirs} " | grep " ld " > /dev/null ; then
  # Arrange for us to find uninstalled linker scripts.
  FLAGS_FOR_TARGET=$FLAGS_FOR_TARGET' -L$$r/$(HOST_SUBDIR)/ld'
fi

# Search for other target-specific linker scripts and such.
case "${target}" in
  m32c-*-* )
    if test -d ${srcdir}/libgloss/m32c; then
      # This is for crt0.o
      FLAGS_FOR_TARGET=$FLAGS_FOR_TARGET' -B$$r/$(TARGET_SUBDIR)/libgloss/m32c'
      # This is for r8c.ld
      FLAGS_FOR_TARGET=$FLAGS_FOR_TARGET' -L$$r/$(TARGET_SUBDIR)/libgloss/m32c'
      # This is for libnosys.a
      FLAGS_FOR_TARGET=$FLAGS_FOR_TARGET' -L$$r/$(TARGET_SUBDIR)/libgloss/libnosys'
    fi
    ;;
  mep*)
    FLAGS_FOR_TARGET="$FLAGS_FOR_TARGET -mlibrary"
    ;;
esac

# Makefile fragments.
for frag in host_makefile_frag target_makefile_frag alphaieee_frag ospace_frag;
do
  eval fragval=\$$frag
  if test $fragval != /dev/null; then
    eval $frag=${srcdir}/$fragval
  fi
done
AC_SUBST_FILE(host_makefile_frag)
AC_SUBST_FILE(target_makefile_frag)
AC_SUBST_FILE(alphaieee_frag)
AC_SUBST_FILE(ospace_frag)

# Miscellanea: directories, flags, etc.
AC_SUBST(RPATH_ENVVAR)
AC_SUBST(tooldir)
AC_SUBST(build_tooldir)
AC_SUBST(CONFIGURE_GDB_TK)
AC_SUBST(GDB_TK)
AC_SUBST(INSTALL_GDB_TK)

# Build module lists & subconfigure args.
AC_SUBST(build_configargs)
AC_SUBST(build_configdirs)

# Host module lists & subconfigure args.
AC_SUBST(host_configargs)
AC_SUBST(configdirs)

# Target module lists & subconfigure args.
AC_SUBST(target_configargs)


# Build tools.
AC_SUBST(CC_FOR_BUILD)
AC_SUBST(config_shell)

# Generate default definitions for YACC, M4, LEX and other programs that run
# on the build machine.  These are used if the Makefile can't locate these
# programs in objdir.
MISSING=`cd $ac_aux_dir && ${PWDCMD-pwd}`/missing

AC_CHECK_PROGS([YACC], ['bison -y' byacc yacc], [$MISSING bison -y])
case " $build_configdirs " in
  *" bison "*) YACC='$$r/$(BUILD_SUBDIR)/bison/tests/bison -y' ;;
  *" byacc "*) YACC='$$r/$(BUILD_SUBDIR)/byacc/byacc' ;;
esac

AC_CHECK_PROGS([BISON], [bison], [$MISSING bison])
case " $build_configdirs " in
  *" bison "*) BISON='$$r/$(BUILD_SUBDIR)/bison/tests/bison' ;;
esac

AC_CHECK_PROGS([M4], [gm4 gnum4 m4], [$MISSING m4])
case " $build_configdirs " in
  *" m4 "*) M4='$$r/$(BUILD_SUBDIR)/m4/m4' ;;
esac

AC_CHECK_PROGS([LEX], [flex lex], [$MISSING flex])
case " $build_configdirs " in
  *" flex "*) LEX='$$r/$(BUILD_SUBDIR)/flex/flex' ;;
  *" lex "*) LEX='$$r/$(BUILD_SUBDIR)/lex/lex' ;;
esac

AC_CHECK_PROGS([FLEX], [flex], [$MISSING flex])
case " $build_configdirs " in
  *" flex "*) FLEX='$$r/$(BUILD_SUBDIR)/flex/flex' ;;
esac

AC_CHECK_PROGS([MAKEINFO], makeinfo, [$MISSING makeinfo])
case " $build_configdirs " in
  *" texinfo "*) MAKEINFO='$$r/$(BUILD_SUBDIR)/texinfo/makeinfo/makeinfo' ;;
  *)
changequote(,)
    # For an installed makeinfo, we require it to be from texinfo 4.4 or
    # higher, else we use the "missing" dummy.
    if ${MAKEINFO} --version \
       | egrep 'texinfo[^0-9]*([1-3][0-9]|4\.[4-9]|[5-9])' >/dev/null 2>&1; then
      :
    else
      MAKEINFO="$MISSING makeinfo"
    fi
    ;;
changequote([,])
esac

# FIXME: expect and dejagnu may become build tools?

AC_CHECK_PROGS(EXPECT, expect, expect)
case " $configdirs " in
  *" expect "*)
    test $host = $build && EXPECT='$$r/$(HOST_SUBDIR)/expect/expect'
    ;;
esac

AC_CHECK_PROGS(RUNTEST, runtest, runtest)
case " $configdirs " in
  *" dejagnu "*)
    test $host = $build && RUNTEST='$$s/$(HOST_SUBDIR)/dejagnu/runtest'
    ;;
esac


# Host tools.
NCN_STRICT_CHECK_TOOLS(AR, ar)
NCN_STRICT_CHECK_TOOLS(AS, as)
NCN_STRICT_CHECK_TOOLS(DLLTOOL, dlltool)
NCN_STRICT_CHECK_TOOLS(LD, ld)
NCN_STRICT_CHECK_TOOLS(LIPO, lipo)
NCN_STRICT_CHECK_TOOLS(NM, nm)
NCN_STRICT_CHECK_TOOLS(RANLIB, ranlib, :)
NCN_STRICT_CHECK_TOOLS(STRIP, strip, :)
NCN_STRICT_CHECK_TOOLS(WINDRES, windres)
NCN_STRICT_CHECK_TOOLS(OBJCOPY, objcopy)
NCN_STRICT_CHECK_TOOLS(OBJDUMP, objdump)
AC_SUBST(CC)
AC_SUBST(CXX)
AC_SUBST(CFLAGS)
AC_SUBST(CFLAGS_FOR_BUILD)
AC_SUBST(CXXFLAGS)

# Target tools.
AC_ARG_WITH([build-time-tools], 
  [  --with-build-time-tools=path
                          use given path to find target tools during the build],
  [case x"$withval" in
     x/*) ;;
     *)
       with_build_time_tools=
       AC_MSG_WARN([argument to --with-build-time-tools must be an absolute path])
       ;;
   esac],
  [with_build_time_tools=])

NCN_STRICT_CHECK_TARGET_TOOLS(CC_FOR_TARGET, cc gcc)
NCN_STRICT_CHECK_TARGET_TOOLS(CXX_FOR_TARGET, c++ g++ cxx gxx)
NCN_STRICT_CHECK_TARGET_TOOLS(GCC_FOR_TARGET, gcc, ${CC_FOR_TARGET})
NCN_STRICT_CHECK_TARGET_TOOLS(GCJ_FOR_TARGET, gcj)
NCN_STRICT_CHECK_TARGET_TOOLS(GFORTRAN_FOR_TARGET, gfortran)

ACX_CHECK_INSTALLED_TARGET_TOOL(AR_FOR_TARGET, ar)
ACX_CHECK_INSTALLED_TARGET_TOOL(AS_FOR_TARGET, as)
ACX_CHECK_INSTALLED_TARGET_TOOL(DLLTOOL_FOR_TARGET, dlltool)
ACX_CHECK_INSTALLED_TARGET_TOOL(LD_FOR_TARGET, ld)
ACX_CHECK_INSTALLED_TARGET_TOOL(LIPO_FOR_TARGET, lipo)
ACX_CHECK_INSTALLED_TARGET_TOOL(NM_FOR_TARGET, nm)
ACX_CHECK_INSTALLED_TARGET_TOOL(OBJDUMP_FOR_TARGET, objdump)
ACX_CHECK_INSTALLED_TARGET_TOOL(RANLIB_FOR_TARGET, ranlib, :)
ACX_CHECK_INSTALLED_TARGET_TOOL(STRIP_FOR_TARGET, strip)
ACX_CHECK_INSTALLED_TARGET_TOOL(WINDRES_FOR_TARGET, windres)

RAW_CXX_FOR_TARGET="$CXX_FOR_TARGET"

GCC_TARGET_TOOL(ar, AR_FOR_TARGET, AR, [binutils/ar])
GCC_TARGET_TOOL(as, AS_FOR_TARGET, AS, [gas/as-new])
GCC_TARGET_TOOL(cc, CC_FOR_TARGET, CC, [gcc/xgcc -B$$r/$(HOST_SUBDIR)/gcc/])
GCC_TARGET_TOOL(c++, CXX_FOR_TARGET, CXX,
		[gcc/g++ -B$$r/$(HOST_SUBDIR)/gcc/ -nostdinc++ `test ! -f $$r/$(TARGET_SUBDIR)/libstdc++-v3/scripts/testsuite_flags || $(SHELL) $$r/$(TARGET_SUBDIR)/libstdc++-v3/scripts/testsuite_flags --build-includes` -L$$r/$(TARGET_SUBDIR)/libstdc++-v3/src -L$$r/$(TARGET_SUBDIR)/libstdc++-v3/src/.libs],
		c++)
GCC_TARGET_TOOL(c++ for libstdc++, RAW_CXX_FOR_TARGET, CXX,
		[gcc/xgcc -shared-libgcc -B$$r/$(HOST_SUBDIR)/gcc -nostdinc++ -L$$r/$(TARGET_SUBDIR)/libstdc++-v3/src -L$$r/$(TARGET_SUBDIR)/libstdc++-v3/src/.libs],
		c++)
GCC_TARGET_TOOL(dlltool, DLLTOOL_FOR_TARGET, DLLTOOL, [binutils/dlltool])
GCC_TARGET_TOOL(gcc, GCC_FOR_TARGET, , [gcc/xgcc -B$$r/$(HOST_SUBDIR)/gcc/])
GCC_TARGET_TOOL(gcj, GCJ_FOR_TARGET, GCJ,
		[gcc/gcj -B$$r/$(HOST_SUBDIR)/gcc/], java)
GCC_TARGET_TOOL(gfortran, GFORTRAN_FOR_TARGET, GFORTRAN,
		[gcc/gfortran -B$$r/$(HOST_SUBDIR)/gcc/], fortran)
GCC_TARGET_TOOL(ld, LD_FOR_TARGET, LD, [ld/ld-new])
GCC_TARGET_TOOL(lipo, LIPO_FOR_TARGET, LIPO)
GCC_TARGET_TOOL(nm, NM_FOR_TARGET, NM, [binutils/nm-new])
GCC_TARGET_TOOL(objdump, OBJDUMP_FOR_TARGET, OBJDUMP, [binutils/objdump])
GCC_TARGET_TOOL(ranlib, RANLIB_FOR_TARGET, RANLIB, [binutils/ranlib])
GCC_TARGET_TOOL(strip, STRIP_FOR_TARGET, STRIP, [binutils/strip])
GCC_TARGET_TOOL(windres, WINDRES_FOR_TARGET, WINDRES, [binutils/windres])

AC_SUBST(FLAGS_FOR_TARGET)
AC_SUBST(RAW_CXX_FOR_TARGET)

# Certain tools may need extra flags.
AR_FOR_TARGET=${AR_FOR_TARGET}${extra_arflags_for_target}
RANLIB_FOR_TARGET=${RANLIB_FOR_TARGET}${extra_ranlibflags_for_target}
NM_FOR_TARGET=${NM_FOR_TARGET}${extra_nmflags_for_target}

# When building target libraries, except in a Canadian cross, we use
# the same toolchain as the compiler we just built.
COMPILER_AS_FOR_TARGET='$(AS_FOR_TARGET)'
COMPILER_LD_FOR_TARGET='$(LD_FOR_TARGET)'
COMPILER_NM_FOR_TARGET='$(NM_FOR_TARGET)'
if test $host = $build; then
  case " $configdirs " in
    *" gcc "*)
      COMPILER_AS_FOR_TARGET='$$r/$(HOST_SUBDIR)/gcc/as'
      COMPILER_LD_FOR_TARGET='$$r/$(HOST_SUBDIR)/gcc/collect-ld'
      COMPILER_NM_FOR_TARGET='$$r/$(HOST_SUBDIR)/gcc/nm'${extra_nmflags_for_target}
      ;;
  esac
fi

AC_SUBST(COMPILER_AS_FOR_TARGET)
AC_SUBST(COMPILER_LD_FOR_TARGET)
AC_SUBST(COMPILER_NM_FOR_TARGET)

AC_MSG_CHECKING([whether to enable maintainer-specific portions of Makefiles])
AC_ARG_ENABLE(maintainer-mode,
[  --enable-maintainer-mode enable make rules and dependencies not useful
                          (and sometimes confusing) to the casual installer],
      USE_MAINTAINER_MODE=$enableval,
      USE_MAINTAINER_MODE=no)
AC_MSG_RESULT($USE_MAINTAINER_MODE)
AC_SUBST(MAINTAINER_MODE_TRUE)
AC_SUBST(MAINTAINER_MODE_FALSE)
if test "$USE_MAINTAINER_MODE" = yes; then
  MAINTAINER_MODE_TRUE=
  MAINTAINER_MODE_FALSE='#'
else
  MAINTAINER_MODE_TRUE='#'
  MAINTAINER_MODE_FALSE=
fi	
MAINT=$MAINTAINER_MODE_TRUE
AC_SUBST(MAINT)dnl

# ---------------------
# GCC bootstrap support
# ---------------------

# Stage specific cflags for build.
stage1_cflags="-g"
case $build in
  vax-*-*)
    case ${GCC} in
      yes) stage1_cflags="-g -Wa,-J" ;;
      *) stage1_cflags="-g -J" ;;
    esac ;;
esac

# This is aimed to mimic bootstrap with a non-GCC compiler to catch problems.
if test "$GCC" = yes; then
  saved_CFLAGS="$CFLAGS"

  # Pass -fkeep-inline-functions for stage 1 if the GCC version supports it.
  CFLAGS="$CFLAGS -fkeep-inline-functions"
  AC_MSG_CHECKING([whether -fkeep-inline-functions is supported])
  AC_TRY_COMPILE([
#if (__GNUC__ < 3) \
    || (__GNUC__ == 3 && (__GNUC_MINOR__ < 3 \
			  || (__GNUC_MINOR__ == 3 && __GNUC_PATCHLEVEL__ < 1)))
#error http://gcc.gnu.org/PR29382
#endif
    ],,
    [AC_MSG_RESULT([yes]); stage1_cflags="$stage1_cflags -fkeep-inline-functions"],
    [AC_MSG_RESULT([no])])

  CFLAGS="$saved_CFLAGS"
fi

AC_SUBST(stage1_cflags)

# Enable --enable-checking in stage1 of the compiler.
AC_ARG_ENABLE(stage1-checking,
[  --enable-stage1-checking[=all]   choose additional checking for stage1
                          of the compiler.],
[stage1_checking=--enable-checking=${enable_stage1_checking}],
[if test "x$enable_checking" = xno; then
  stage1_checking=--enable-checking
else
  stage1_checking=--enable-checking${enable_checking+=}$enable_checking
fi])
AC_SUBST(stage1_checking)

# Enable -Werror in bootstrap stage2 and later.
AC_ARG_ENABLE(werror,
[  --enable-werror         enable -Werror in bootstrap stage2 and later], [],
[if test -d ${srcdir}/gcc && test x"`cat $srcdir/gcc/DEV-PHASE`" = xexperimental; then
  enable_werror=yes
else
  enable_werror=no
fi])
case ${enable_werror} in
  yes) stage2_werror_flag="--enable-werror-always" ;;
  *) stage2_werror_flag="" ;;
esac
AC_SUBST(stage2_werror_flag)

# Flags needed to enable html installing and building
AC_ARG_WITH(datarootdir,
[  --with-datarootdir	Use datarootdir as the data root directory.],
[datarootdir="\${prefix}/${withval}"],
[datarootdir="\${prefix}/share"])

AC_ARG_WITH(docdir,
[  --with-docdir	Install documentation in this directory.],
[docdir="\${prefix}/${withval}"],
[docdir="\${datarootdir}/doc"])

AC_ARG_WITH(pdfdir,
[  --with-pdfdir	Install pdf in this directory.],
[pdfdir="\${prefix}/${withval}"],
[pdfdir="\${docdir}"])

AC_ARG_WITH(htmldir,
[  --with-htmldir	Install html in this directory.],
[htmldir="\${prefix}/${withval}"],
[htmldir="\${docdir}"])

AC_SUBST(datarootdir)
AC_SUBST(docdir)
AC_SUBST(pdfdir)
AC_SUBST(htmldir)

AC_OUTPUT(Makefile)
