#!/bin/sh
# genscripts.sh - generate the ld-emulation-target specific files
#
# Usage: genscripts_extra.sh \
#          srcdir \
#          libdir \
#          prefix \
#          exec_prefix \
#          host \
#          target \
#          target_alias \
#          default_emulation \
#          native_lib_dirs \
#          use_sysroot \
#          this_emulation \
# optional:
#          tool_dir \
#          customizer_script
#
# Sample usage:
#
#   genscripts_extra.sh \
#    /sources/ld \
#    /usr/local/lib \
#    /usr/local \
#    /usr/local \
#    sparc-sun-sunos4.1.3 \
#    sparc-sun-sunos4.1.3 \
#    sparc-sun-sunos4.1.3 \
#    sun4 \
#    "" \
#    no \
#    sun3 \
#    sparc-sun-sunos4.1.3 \
#    sparc.sh
#
# produces the linker scripts:
#
#   sun3.x       [default linker script]
#   sun3.xbn     [used when the linker is invoked with "-N"]
#   sun3.xn      [used when the linker is invoked with "-n"]
#   sun3.xr      [used when the linker is invoked with "-r"]
#   sun3.xu      [used when the linker is invoked with "-Ur"]
# and maybe:
#   sun3.xc      [used when the linker is invoked with "-z combreloc"]
#   sun3.xsc     [used when the linker is invoked with "--shared"]
#   sun3.xdc     [used when the linker is invoked with "-pie"]
#
# It also produced the C source file:
#
#   em_sun3.c
#
# which is then compiled into the linker.
#
# The linker scripts are created by running the shell script
# /sources/ld/emulparams/sparc.sh to set the value of ${SCRIPT_NAME}
# (and any other variables it wants to).  ${SCRIPT_NAME} is then
# invoked with a variable called ${LD_FLAG} to tell it which version
# of the linker script to create.


srcdir=$1
libdir=$2
prefix=$3
exec_prefix=$4
host=$5
target=$6
target_alias=$7
EMULATION_LIBPATH=$8
NATIVE_LIB_DIRS=$9
shift 9
use_sysroot=$1
EMULATION_NAME=$2
TOOL_LIB=$3
CUSTOMIZER_SCRIPT=$4

# Can't use ${TOOL_LIB:-$target_alias} here due to an Ultrix shell bug.
if [ "x${TOOL_LIB}" = "x" ] ; then
  tool_lib=${exec_prefix}/${target_alias}/lib
else
  tool_lib=${exec_prefix}/${TOOL_LIB}/lib
fi

if [ "x${CUSTOMIZER_SCRIPT}" = "x" ] ; then
  CUSTOMIZER_SCRIPT=${EMULATION_NAME}
fi
CUSTOMIZER_SCRIPT="${srcdir}/emulparams/${CUSTOMIZER_SCRIPT}.sh"

# Include the emulation-specific parameters:
. ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}

if test -d ldscripts; then
  true
else
  mkdir ldscripts
fi

# Set some flags for the emultempl scripts.  USE_LIBPATH will
# be set for any libpath-using emulation; NATIVE will be set for a
# libpath-using emulation where ${host} = ${target}.  NATIVE
# may already have been set by the emulparams file, but that's OK
# (it'll just get set to "yes" twice).

case " $EMULATION_LIBPATH " in
  *" ${EMULATION_NAME} "*)
    if [ "x${host}" = "x${target}" ] ; then
      NATIVE=yes
      USE_LIBPATH=yes
    elif [ "x${use_sysroot}" = "xyes" ] ; then
      USE_LIBPATH=yes
    fi
    ;;
esac

# If the emulparams file sets NATIVE, make sure USE_LIBPATH is set also.
if test "x$NATIVE" = "xyes" ; then
  USE_LIBPATH=yes
fi

# Set the library search path, for libraries named by -lfoo.
# If LIB_PATH is defined (e.g., by Makefile) and non-empty, it is used.
# Otherwise, the default is set here.
#
# The format is the usual list of colon-separated directories.
# To force a logically empty LIB_PATH, do LIBPATH=":".
#
# If we are using a sysroot, prefix library paths with "=" to indicate this.
#
# If the emulparams file set LIBPATH_SUFFIX, prepend an extra copy of
# the library path with the suffix applied.

if [ "x${LIB_PATH}" = "x" ] && [ "x${USE_LIBPATH}" = xyes ] ; then
  LIB_PATH2=

  libs=${NATIVE_LIB_DIRS}
  if [ "x${use_sysroot}" != "xyes" ] ; then
    case " ${libs} " in
      *" ${libdir} "*) ;;
      *) libs="${libdir} ${libs}" ;;
    esac
    case " ${libs} " in
      *" ${tool_lib} "*) ;;
      *) libs="${tool_lib} ${libs}" ;;
    esac
  fi

  for lib in ${libs}; do
    # The "=" is harmless if we aren't using a sysroot, but also needless.
    if [ "x${use_sysroot}" = "xyes" ] ; then
      lib="=${lib}"
    fi
    addsuffix=
    case "${LIBPATH_SUFFIX}:${lib}" in
      :*) ;;
      *:*${LIBPATH_SUFFIX}) ;;
      *) addsuffix=yes ;;
    esac
    if test -n "$addsuffix"; then
      case :${LIB_PATH}: in
	*:${lib}${LIBPATH_SUFFIX}:*) ;;
	::) LIB_PATH=${lib}${LIBPATH_SUFFIX} ;;
	*) LIB_PATH=${LIB_PATH}:${lib}${LIBPATH_SUFFIX} ;;
      esac
      case :${LIB_PATH}:${LIB_PATH2}: in
	*:${lib}:*) ;;
	*::) LIB_PATH2=${lib} ;;
	*) LIB_PATH2=${LIB_PATH2}:${lib} ;;
      esac
    else
      case :${LIB_PATH2}: in
	*:${lib}:*) ;;
	::) LIB_PATH2=${lib} ;;
	*) LIB_PATH2=${LIB_PATH2}:${lib} ;;
      esac
    fi
  done

  case :${LIB_PATH}:${LIB_PATH2}: in
    *:: | ::*) LIB_PATH=${LIB_PATH}${LIB_PATH2} ;;
    *) LIB_PATH=${LIB_PATH}:${LIB_PATH2} ;;
  esac
fi

# Always search $(tooldir)/lib, aka /usr/local/TARGET/lib, except for
# sysrooted configurations and when LIBPATH=":".
if [ "x${use_sysroot}" != "xyes" ] ; then
  case :${LIB_PATH}: in
  ::: | *:${tool_lib}:*) ;;
  ::) LIB_PATH=${tool_lib} ;;
  *) LIB_PATH=${tool_lib}:${LIB_PATH} ;;
  esac
  # For multilib targets, search both $tool_lib dirs
  if [ "x${LIBPATH_SUFFIX}" != "x" ] ; then
    case :${LIB_PATH}: in
      ::: | *:${tool_lib}${LIBPATH_SUFFIX}:*) ;;
      ::) LIB_PATH=${tool_lib}${LIBPATH_SUFFIX} ;;
      *) LIB_PATH=${tool_lib}${LIBPATH_SUFFIX}:${LIB_PATH} ;;
    esac
  fi
fi

LIB_SEARCH_DIRS=`echo ${LIB_PATH} | sed -e 's/:/ /g' -e 's/\([^ ][^ ]*\)/SEARCH_DIR(\\"\1\\");/g'`

# We need it for testsuite.
set $EMULATION_LIBPATH
if [ "x$1" = "x$EMULATION_NAME" ]; then
    test -d tmpdir || mkdir tmpdir
    rm -f tmpdir/libpath.exp
    echo "set libpath \"${LIB_PATH}\"" | sed -e 's/:/ /g' > tmpdir/libpath.exp
fi

# Generate 5 or 6 script files from a master script template in
# ${srcdir}/scripttempl/${SCRIPT_NAME}.sh.  Which one of the 5 or 6
# script files is actually used depends on command line options given
# to ld.  (SCRIPT_NAME was set in the emulparams_file.)
#
# A .x script file is the default script.
# A .xr script is for linking without relocation (-r flag).
# A .xu script is like .xr, but *do* create constructors (-Ur flag).
# A .xn script is for linking with -n flag (mix text and data on same page).
# A .xbn script is for linking with -N flag (mix text and data on same page).
# A .xs script is for generating a shared library with the --shared
#   flag; it is only generated if $GENERATE_SHLIB_SCRIPT is set by the
#   emulation parameters.
# A .xc script is for linking with -z combreloc; it is only generated if
#   $GENERATE_COMBRELOC_SCRIPT is set by the emulation parameters or
#   $SCRIPT_NAME is "elf".
# A .xsc script is for linking with --shared -z combreloc; it is generated
#   if $GENERATE_COMBRELOC_SCRIPT is set by the emulation parameters or
#   $SCRIPT_NAME is "elf" and $GENERATE_SHLIB_SCRIPT is set by the emulation
#   parameters too.

if [ "x$SCRIPT_NAME" = "xelf" ]; then
  GENERATE_COMBRELOC_SCRIPT=yes
fi

SEGMENT_SIZE=${SEGMENT_SIZE-${MAXPAGESIZE-${TARGET_PAGE_SIZE}}}

# Determine DATA_ALIGNMENT for the 5 variants, using
# values specified in the emulparams/<script_to_run>.sh file or default.

DATA_ALIGNMENT_="${DATA_ALIGNMENT_-${DATA_ALIGNMENT-ALIGN(${SEGMENT_SIZE})}}"
DATA_ALIGNMENT_n="${DATA_ALIGNMENT_n-${DATA_ALIGNMENT_}}"
DATA_ALIGNMENT_N="${DATA_ALIGNMENT_N-${DATA_ALIGNMENT-.}}"
DATA_ALIGNMENT_r="${DATA_ALIGNMENT_r-${DATA_ALIGNMENT-}}"
DATA_ALIGNMENT_u="${DATA_ALIGNMENT_u-${DATA_ALIGNMENT_r}}"

LD_FLAG=r
DATA_ALIGNMENT=${DATA_ALIGNMENT_r}
DEFAULT_DATA_ALIGNMENT="ALIGN(${SEGMENT_SIZE})"
( echo "/* Script for ld -r: link without relocation */"
  . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
  . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xr

LD_FLAG=u
DATA_ALIGNMENT=${DATA_ALIGNMENT_u}
CONSTRUCTING=" "
( echo "/* Script for ld -Ur: link w/out relocation, do create constructors */"
  . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
  . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xu

LD_FLAG=
DATA_ALIGNMENT=${DATA_ALIGNMENT_}
RELOCATING=" "
( echo "/* Default linker script, for normal executables */"
  . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
  . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.x

LD_FLAG=n
DATA_ALIGNMENT=${DATA_ALIGNMENT_n}
TEXT_START_ADDR=${NONPAGED_TEXT_START_ADDR-${TEXT_START_ADDR}}
( echo "/* Script for -n: mix text and data on same page */"
  . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
  . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xn

LD_FLAG=N
DATA_ALIGNMENT=${DATA_ALIGNMENT_N}
( echo "/* Script for -N: mix text and data on same page; don't align data */"
  . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
  . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xbn

if test -n "$GENERATE_COMBRELOC_SCRIPT"; then
  DATA_ALIGNMENT=${DATA_ALIGNMENT_c-${DATA_ALIGNMENT_}}
  LD_FLAG=c
  COMBRELOC=ldscripts/${EMULATION_NAME}.xc.tmp
  ( echo "/* Script for -z combreloc: combine and sort reloc sections */"
    . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
    . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
  ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xc
  rm -f ${COMBRELOC}
  LD_FLAG=w
  RELRO_NOW=" "
  COMBRELOC=ldscripts/${EMULATION_NAME}.xw.tmp
  ( echo "/* Script for -z combreloc -z now -z relro: combine and sort reloc sections */"
    . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
    . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
  ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xw
  rm -f ${COMBRELOC}
  COMBRELOC=
  unset RELRO_NOW
fi

if test -n "$GENERATE_SHLIB_SCRIPT"; then
  LD_FLAG=shared
  DATA_ALIGNMENT=${DATA_ALIGNMENT_s-${DATA_ALIGNMENT_}}
  CREATE_SHLIB=" "
  # Note that TEXT_START_ADDR is set to NONPAGED_TEXT_START_ADDR.
  (
    echo "/* Script for ld --shared: link shared library */"
    . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
    . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
  ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xs
  if test -n "$GENERATE_COMBRELOC_SCRIPT"; then
    LD_FLAG=cshared
    DATA_ALIGNMENT=${DATA_ALIGNMENT_sc-${DATA_ALIGNMENT}}
    COMBRELOC=ldscripts/${EMULATION_NAME}.xsc.tmp
    ( echo "/* Script for --shared -z combreloc: shared library, combine & sort relocs */"
      . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
      . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
    ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xsc
    rm -f ${COMBRELOC}
    LD_FLAG=wshared
    RELRO_NOW=" "
    COMBRELOC=ldscripts/${EMULATION_NAME}.xsw.tmp
    ( echo "/* Script for --shared -z combreloc -z now -z relro: shared library, combine & sort relocs */"
      . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
      . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
    ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xsw
    rm -f ${COMBRELOC}
    COMBRELOC=
    unset RELRO_NOW
  fi
  unset CREATE_SHLIB
fi

if test -n "$GENERATE_PIE_SCRIPT"; then
  LD_FLAG=pie
  DATA_ALIGNMENT=${DATA_ALIGNMENT_s-${DATA_ALIGNMENT_}}
  CREATE_PIE=" "
  # Note that TEXT_START_ADDR is set to NONPAGED_TEXT_START_ADDR.
  (
    echo "/* Script for ld -pie: link position independent executable */"
    . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
    . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
  ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xd
  if test -n "$GENERATE_COMBRELOC_SCRIPT"; then
    LD_FLAG=cpie
    DATA_ALIGNMENT=${DATA_ALIGNMENT_sc-${DATA_ALIGNMENT}}
    COMBRELOC=ldscripts/${EMULATION_NAME}.xdc.tmp
    ( echo "/* Script for -pie -z combreloc: position independent executable, combine & sort relocs */"
      . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
      . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
    ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xdc
    rm -f ${COMBRELOC}
    LD_FLAG=wpie
    RELRO_NOW=" "
    COMBRELOC=ldscripts/${EMULATION_NAME}.xdw.tmp
    ( echo "/* Script for -pie -z combreloc -z now -z relro: position independent executable, combine & sort relocs */"
      . ${CUSTOMIZER_SCRIPT} ${EMULATION_NAME}
      . ${srcdir}/scripttempl/${SCRIPT_NAME}.sc
    ) | sed -e '/^ *$/d;s/[ 	]*$//' > ldscripts/${EMULATION_NAME}.xdw
    rm -f ${COMBRELOC}
    COMBRELOC=
    unset RELRO_NOW
  fi
  unset CREATE_PIE
fi

case " $EMULATION_LIBPATH " in
    *" ${EMULATION_NAME} "*) COMPILE_IN=true;;
esac

# Generate e${EMULATION_NAME}.c.
. ${srcdir}/emultempl/${TEMPLATE_NAME-generic}.em
