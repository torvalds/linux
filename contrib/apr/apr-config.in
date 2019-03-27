#!/bin/sh
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# APR script designed to allow easy command line access to APR configuration
# parameters.

APR_MAJOR_VERSION="@APR_MAJOR_VERSION@"
APR_DOTTED_VERSION="@APR_DOTTED_VERSION@"

prefix="@prefix@"
exec_prefix="@exec_prefix@"
bindir="@bindir@"
libdir="@libdir@"
datarootdir="@datadir@"
datadir="@datadir@"
installbuilddir="@installbuilddir@"
includedir="@includedir@"

CC="@CC@"
CPP="@CPP@"
SHELL="@SHELL@"
CPPFLAGS="@EXTRA_CPPFLAGS@"
CFLAGS="@EXTRA_CFLAGS@"
LDFLAGS="@EXTRA_LDFLAGS@"
LIBS="@EXTRA_LIBS@"
EXTRA_INCLUDES="@EXTRA_INCLUDES@"
SHLIBPATH_VAR="@shlibpath_var@"
APR_SOURCE_DIR="@apr_srcdir@"
APR_BUILD_DIR="@apr_builddir@"
APR_SO_EXT="@so_ext@"
APR_LIB_TARGET="@export_lib_target@"
APR_LIBNAME="@APR_LIBNAME@"

# NOTE: the following line is modified during 'make install': alter with care!
location=@APR_CONFIG_LOCATION@

show_usage()
{
    cat << EOF
Usage: apr-$APR_MAJOR_VERSION-config [OPTION]

Known values for OPTION are:
  --prefix[=DIR]    change prefix to DIR
  --bindir          print location where binaries are installed
  --includedir      print location where headers are installed
  --cc              print C compiler name
  --cpp             print C preprocessor name and any required options
  --cflags          print C compiler flags
  --cppflags        print C preprocessor flags
  --includes        print include information
  --ldflags         print linker flags
  --libs            print additional libraries to link against
  --srcdir          print APR source directory
  --installbuilddir print APR build helper directory
  --link-ld         print link switch(es) for linking to APR
  --link-libtool    print the libtool inputs for linking to APR
  --shlib-path-var  print the name of the shared library path env var
  --apr-la-file     print the path to the .la file, if available
  --apr-so-ext      print the extensions of shared objects on this platform
  --apr-lib-target  print the libtool target information
  --apr-libtool     print the path to APR's libtool
  --version         print the APR's version as a dotted triple
  --help            print this help

When linking with libtool, an application should do something like:
  APR_LIBS="\`apr-$APR_MAJOR_VERSION-config --link-libtool --libs\`"
or when linking directly:
  APR_LIBS="\`apr-$APR_MAJOR_VERSION-config --link-ld --libs\`"

An application should use the results of --cflags, --cppflags, --includes,
and --ldflags in their build process.
EOF
}

if test $# -eq 0; then
    show_usage
    exit 1
fi

if test "$location" = "installed"; then
    LA_FILE="$libdir/lib${APR_LIBNAME}.la"
else
    LA_FILE="$APR_BUILD_DIR/lib${APR_LIBNAME}.la"
fi

flags=""

while test $# -gt 0; do
    # Normalize the prefix.
    case "$1" in
    -*=*) optarg=`echo "$1" | sed 's/[-_a-zA-Z0-9]*=//'` ;;
    *) optarg= ;;
    esac

    case "$1" in
    # It is possible for the user to override our prefix.
    --prefix=*)
    prefix=$optarg
    ;;
    --prefix)
    echo $prefix
    exit 0
    ;;
    --bindir)
    echo $bindir
    exit 0
    ;;
    --includedir)
    if test "$location" = "installed"; then
        flags="$includedir"
    elif test "$location" = "source"; then
        flags="$APR_SOURCE_DIR/include"
    else
        # this is for VPATH builds
        flags="$APR_BUILD_DIR/include $APR_SOURCE_DIR/include"
    fi
    echo $flags
    exit 0
    ;;
    --cc)
    echo $CC
    exit 0
    ;;
    --cpp)
    echo $CPP
    exit 0
    ;;
    --cflags)
    flags="$flags $CFLAGS"
    ;;
    --cppflags)
    flags="$flags $CPPFLAGS"
    ;;
    --libs)
    flags="$flags $LIBS"
    ;;
    --ldflags)
    flags="$flags $LDFLAGS"
    ;;
    --includes)
    if test "$location" = "installed"; then
        flags="$flags -I$includedir $EXTRA_INCLUDES"
    elif test "$location" = "source"; then
        flags="$flags -I$APR_SOURCE_DIR/include $EXTRA_INCLUDES"
    else
        # this is for VPATH builds
        flags="$flags -I$APR_BUILD_DIR/include -I$APR_SOURCE_DIR/include $EXTRA_INCLUDES"
    fi
    ;;
    --srcdir)
    echo $APR_SOURCE_DIR
    exit 0
    ;;
    --installbuilddir)
    if test "$location" = "installed"; then
        echo "${installbuilddir}"
    elif test "$location" = "source"; then
        echo "$APR_SOURCE_DIR/build"
    else
        # this is for VPATH builds
        echo "$APR_BUILD_DIR/build"
    fi
    exit 0
    ;;
    --version)
    echo $APR_DOTTED_VERSION
    exit 0
    ;;
    --link-ld)
    if test "$location" = "installed"; then
        ### avoid using -L if libdir is a "standard" location like /usr/lib
        flags="$flags -L$libdir -l${APR_LIBNAME}"
    else
        ### this surely can't work since the library is in .libs?
        flags="$flags -L$APR_BUILD_DIR -l${APR_LIBNAME}"
    fi
    ;;
    --link-libtool)
    # If the LA_FILE exists where we think it should be, use it.  If we're
    # installed and the LA_FILE does not exist, assume to use -L/-l
    # (the LA_FILE may not have been installed).  If we're building ourselves,
    # we'll assume that at some point the .la file be created.
    if test -f "$LA_FILE"; then
        flags="$flags $LA_FILE"
    elif test "$location" = "installed"; then
        ### avoid using -L if libdir is a "standard" location like /usr/lib
        # Since the user is specifying they are linking with libtool, we
        # *know* that -R will be recognized by libtool.
        flags="$flags -L$libdir -R$libdir -l${APR_LIBNAME}"
    else
        flags="$flags $LA_FILE"
    fi
    ;;
    --shlib-path-var)
    echo "$SHLIBPATH_VAR"
    exit 0
    ;;
    --apr-la-file)
    if test -f "$LA_FILE"; then
        flags="$flags $LA_FILE"
    fi
    ;;
    --apr-so-ext)
    echo "$APR_SO_EXT"
    exit 0
    ;;
    --apr-lib-target)
    echo "$APR_LIB_TARGET"
    exit 0
    ;;
    --apr-libtool)
    if test "$location" = "installed"; then
        echo "${installbuilddir}/libtool"
    else
        echo "$APR_BUILD_DIR/libtool"
    fi
    exit 0
    ;;
    --help)
    show_usage
    exit 0
    ;;
    *)
    show_usage
    exit 1
    ;;
    esac

    # Next please.
    shift
done

if test -n "$flags"; then
  echo "$flags"
fi

exit 0
