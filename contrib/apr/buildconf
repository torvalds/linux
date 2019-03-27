#!/bin/sh
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
# 
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#

# buildconf: Build the support scripts needed to compile from a
#            checked-out version of the source code.

if [ "$1" = "--verbose" -o "$1" = "-v" ]; then
    verbose="--verbose"
    shift
fi

# Verify that the builder has the right config tools installed
#
build/buildcheck.sh $verbose || exit 1

libtoolize=`build/PrintPath glibtoolize1 glibtoolize libtoolize15 libtoolize14 libtoolize`
if [ "x$libtoolize" = "x" ]; then
    echo "libtoolize not found in path"
    exit 1
fi

# Create the libtool helper files
#
# Note: we copy (rather than link) them to simplify distribution.
# Note: APR supplies its own config.guess and config.sub -- we do not
#       rely on libtool's versions
#
echo "buildconf: copying libtool helper files using $libtoolize"

# Remove any libtool files so one can switch between libtool versions
# by simply rerunning the buildconf script.
rm -f aclocal.m4 libtool.m4
(cd build ; rm -f ltconfig ltmain.sh argz.m4 libtool.m4 ltoptions.m4 ltsugar.m4 ltversion.m4 lt~obsolete.m4)

# Determine libtool version, because --copy behaves differently
# w.r.t. copying libtool.m4
lt_pversion=`$libtoolize --version 2>/dev/null|sed -e 's/([^)]*)//g;s/^[^0-9]*//;s/[- ].*//g;q'`
lt_version=`echo $lt_pversion|sed -e 's/\([a-z]*\)$/.\1/'`
IFS=.; set $lt_version; IFS=' '

# libtool 1
if test "$1" = "1"; then
  $libtoolize --copy --automake
  # Unlikely, maybe for old versions the file exists
  if [ -f libtool.m4 ]; then 
    ltfile=`pwd`/libtool.m4
  else

    # Extract all lines setting variables from libtoolize up until
    # libtool_m4 gets set
    ltfindcmd="`sed -n \"/=[^\\\`]/p;/libtool_m4=/{s/.*=/echo /p;q;}\" \
                   < $libtoolize`"

    # Get path to libtool.m4 either from LIBTOOL_M4 env var or our libtoolize based script
    ltfile=${LIBTOOL_M4-`eval "$ltfindcmd"`}

    # Expecting the code above to be very portable, but just in case...
    if [ -z "$ltfile" -o ! -f "$ltfile" ]; then
      ltpath=`dirname $libtoolize`
      ltfile=`cd $ltpath/../share/aclocal ; pwd`/libtool.m4
    fi
  fi
  if [ ! -f $ltfile ]; then
    echo "$ltfile not found"
    exit 1
  fi
  # Do we need this anymore?
  echo "buildconf: Using libtool.m4 at ${ltfile}."
  rm -f build/libtool.m4
  cp -p $ltfile build/libtool.m4

# libtool 2
elif test "$1" = "2"; then
  $libtoolize --copy --quiet $verbose
fi

# Replace top_builddir by apr_builddir.
# Wouldn't it just be better to define top_builddir??
# Not sure, would it interfere with httpd top_builddir when bundled?
mv build/libtool.m4 build/libtool.m4.$$
sed -e 's/\(LIBTOOL=.*\)top_build/\1apr_build/' < build/libtool.m4.$$ > build/libtool.m4
rm -f build/libtool.m4.$$

# Clean up any leftovers
rm -f aclocal.m4 libtool.m4

#
# Generate the autoconf header and ./configure
#
echo "buildconf: creating include/arch/unix/apr_private.h.in ..."
${AUTOHEADER:-autoheader} $verbose

echo "buildconf: creating configure ..."
### do some work to toss config.cache?
${AUTOCONF:-autoconf} $verbose

# Remove autoconf 2.5x's cache directory
rm -rf autom4te*.cache

echo "buildconf: generating 'make' outputs ..."
build/gen-build.py $verbose make

# Create RPM Spec file
if [ -f `which cut` ]; then
  echo "buildconf: rebuilding rpm spec file"
  ( REVISION=`build/get-version.sh all include/apr_version.h APR`
    VERSION=`echo $REVISION | cut -d- -s -f1`
    RELEASE=`echo $REVISION | cut -d- -s -f2`
    if [ "x$VERSION" = "x" ]; then
      VERSION=$REVISION
      RELEASE=1
    fi
    cat ./build/rpm/apr.spec.in | \
    sed -e "s/APR_VERSION/$VERSION/" \
        -e "s/APR_RELEASE/$RELEASE/" \
    > apr.spec )
fi

exit 0
