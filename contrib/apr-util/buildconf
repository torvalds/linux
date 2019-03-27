#!/bin/sh
#
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
#

if [ "$1" = "--verbose" -o "$1" = "-v" ]; then
    verbose="--verbose"
    shift
fi

# Default place to look for apr source.  Can be overridden with 
#   --with-apr=[directory]
apr_src_dir=../apr

while test $# -gt 0 
do
  # Normalize
  case "$1" in
  -*=*) optarg=`echo "$1" | sed 's/[-_a-zA-Z0-9]*=//'` ;;
  *) optarg= ;;
  esac

  case "$1" in
  --with-apr=*)
  apr_src_dir=$optarg
  ;;
  esac

  shift
done

if [ -f "$apr_src_dir/build/apr_common.m4" ]; then
  apr_src_dir=`cd $apr_src_dir; pwd`
  echo ""
  echo "Looking for apr source in $apr_src_dir"
else
  echo ""
  echo "Problem finding apr source in $apr_src_dir."
  echo "Use:"
  echo "  --with-apr=[directory]" 
  exit 1
fi

set -e

# Remove some files, then copy them from apr source tree
rm -f build/apr_common.m4 build/find_apr.m4 build/install.sh \
      build/config.guess build/config.sub build/get-version.sh
cp -p $apr_src_dir/build/apr_common.m4 $apr_src_dir/build/find_apr.m4 \
      $apr_src_dir/build/install.sh $apr_src_dir/build/config.guess \
      $apr_src_dir/build/config.sub $apr_src_dir/build/get-version.sh \
      build/

# Remove aclocal.m4 as it'll break some builds...
rm -rf aclocal.m4 autom4te*.cache

#
# Generate the autoconf header (include/apu_config.h) and ./configure
#
echo "Creating include/private/apu_config.h ..."
${AUTOHEADER:-autoheader} $verbose

echo "Creating configure ..."
### do some work to toss config.cache?
if ${AUTOCONF:-autoconf} $verbose; then
  :
else
  echo "autoconf failed"
  exit 1
fi

#
# Generate build-outputs.mk for the build system
#
echo "Generating 'make' outputs ..."
$apr_src_dir/build/gen-build.py $verbose make

#
# If Expat has been bundled, then go and configure the thing
#
if [ -f xml/expat/buildconf.sh ]; then
  echo "Invoking xml/expat/buildconf.sh ..."
  (cd xml/expat; ./buildconf.sh $verbose)
fi

# Remove autoconf cache again
rm -rf autom4te*.cache

# Create RPM Spec file
if [ -f `which cut` ]; then
  echo rebuilding rpm spec file
  REVISION=`build/get-version.sh all include/apu_version.h APU`
  VERSION=`echo $REVISION | cut -d- -s -f1`
  RELEASE=`echo $REVISION | cut -d- -s -f2`
  if [ "x$VERSION" = "x" ]; then
      VERSION=$REVISION
      RELEASE=1
  fi
  sed -e "s/APU_VERSION/$VERSION/" -e "s/APU_RELEASE/$RELEASE/" \
    ./build/rpm/apr-util.spec.in > apr-util.spec
fi

