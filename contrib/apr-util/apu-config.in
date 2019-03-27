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

# APR-util script designed to allow easy command line access to APR-util
# configuration parameters.

APRUTIL_MAJOR_VERSION="@APRUTIL_MAJOR_VERSION@"
APRUTIL_DOTTED_VERSION="@APRUTIL_DOTTED_VERSION@"

prefix="@prefix@"
exec_prefix="@exec_prefix@"
bindir="@bindir@"
libdir="@libdir@"
includedir="@includedir@"

LIBS="@APRUTIL_EXPORT_LIBS@"
INCLUDES="@APRUTIL_INCLUDES@"
LDFLAGS="@APRUTIL_LDFLAGS@"
LDAP_LIBS="@LDADD_ldap@"
DBM_LIBS="@LDADD_dbm_db@ @LDADD_dbm_gdbm@ @LDADD_dbm_ndbm@"

APRUTIL_LIBNAME="@APRUTIL_LIBNAME@"

APU_SOURCE_DIR="@abs_srcdir@"
APU_BUILD_DIR="@abs_builddir@"
APR_XML_EXPAT_OLD="@APR_XML_EXPAT_OLD@"
APU_DB_VERSION="@apu_db_version@"

# NOTE: the following line is modified during 'make install': alter with care!
location=@APU_CONFIG_LOCATION@

show_usage()
{
    cat << EOF
Usage: apu-$APRUTIL_MAJOR_VERSION-config [OPTION]

Known values for OPTION are:
  --prefix[=DIR]    change prefix to DIR
  --bindir          print location where binaries are installed
  --includes        print include information
  --includedir      print location where headers are installed
  --ldflags         print linker flags
  --libs            print library information
  --avoid-ldap      do not include ldap library information with --libs
  --ldap-libs       print library information to link with ldap
  --avoid-dbm       do not include DBM library information with --libs
  --dbm-libs        print additional library information to link with DBM
  --srcdir          print APR-util source directory
  --link-ld         print link switch(es) for linking to APR-util
  --link-libtool    print the libtool inputs for linking to APR-util
  --apu-la-file     print the path to the .la file, if available
  --old-expat       indicate if APR-util was built against an old expat
  --db-version      print the DB version
  --version         print APR-util's version as a dotted triple
  --help            print this help

When linking with libtool, an application should do something like:
  APU_LIBS="\`apu-$APRUTIL_MAJOR_VERSION-config --link-libtool --libs\`"
or when linking directly:
  APU_LIBS="\`apu-$APRUTIL_MAJOR_VERSION-config --link-ld --libs\`"

An application should use the results of --includes, and --ldflags in
their build process.
EOF
}

if test $# -eq 0; then
    show_usage
    exit 1
fi

if test "$location" = "installed"; then
    LA_FILE="$libdir/lib${APRUTIL_LIBNAME}.la"

    LIBS=`echo "$LIBS" | sed -e "s $APU_BUILD_DIR/xml/expat $prefix g" -e "s $prefix/libexpat.la -lexpat g"`
    LDFLAGS=`echo "$LDFLAGS" | sed -e "s $APU_BUILD_DIR/xml/expat $prefix g"`
    INCLUDES=`echo "$INCLUDES" | sed -e "s $APU_BUILD_DIR/xml/expat $prefix g" -e "s -I$prefix/lib  g"`
else
    LA_FILE="$APU_BUILD_DIR/lib${APRUTIL_LIBNAME}.la"
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
    --avoid-ldap)
    LDAP_LIBS=""
    ;;
    --avoid-dbm)
    DBM_LIBS=""
    ;;
    --libs)
    flags="$flags $LDAP_LIBS $DBM_LIBS $LIBS"
    ;;
    --ldap-libs)
    flags="$flags $LDAP_LIBS"
    ;;
    --dbm-libs)
    flags="$flags $DBM_LIBS"
    ;;
    --includedir)
    if test "$location" = "installed"; then
        flags="$includedir"
    elif test "$location" = "source"; then
        flags="$APU_SOURCE_DIR/include"
    else
        # this is for VPATH builds
        flags="$APU_BUILD_DIR/include $APU_SOURCE_DIR/include"
    fi
    echo $flags
    exit 0
    ;;
    --includes)
    if test "$location" = "installed"; then
        flags="$flags -I$includedir $INCLUDES"
    elif test "$location" = "source"; then
        flags="$flags -I$APU_SOURCE_DIR/include $INCLUDES"
    else
        # this is for VPATH builds
        flags="$flags -I$APU_BUILD_DIR/include -I$APU_SOURCE_DIR/include $INCLUDES"
    fi
    ;;
    --ldflags)
    flags="$flags $LDFLAGS"
    ;;
    --srcdir)
    echo $APU_SOURCE_DIR
    exit 0
    ;;
    --version)
    echo $APRUTIL_DOTTED_VERSION
    exit 0
    ;;
    --link-ld)
    if test "$location" = "installed"; then
        ### avoid using -L if libdir is a "standard" location like /usr/lib
        flags="$flags -L$libdir -l$APRUTIL_LIBNAME"
    else
        flags="$flags -L$APU_BUILD_DIR -l$APRUTIL_LIBNAME"
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
        flags="$flags -L$libdir -R$libdir -l$APRUTIL_LIBNAME"
    else
        flags="$flags $LA_FILE"
    fi
    ;;
    --apu-la-file)
    if test -f "$LA_FILE"; then
        flags="$flags $LA_FILE"
    fi
    ;;
    --old-expat)
    if test ! -n "$APR_XML_EXPAT_OLD"; then
        echo "no"
    else
        echo "$APR_XML_EXPAT_OLD"
    fi
    exit 0
    ;;
    --db-version)
    echo $APU_DB_VERSION
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
