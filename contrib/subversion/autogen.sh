#!/bin/sh
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#

### Run this to produce everything needed for configuration. ###


# Some shells can produce output when running 'cd' which interferes
# with the construct 'abs=`cd dir && pwd`'.
(unset CDPATH) >/dev/null 2>&1 && unset CDPATH

# Run tests to ensure that our build requirements are met
RELEASE_MODE=""
RELEASE_ARGS=""
SKIP_DEPS=""
while test $# != 0; do
  case "$1" in
    --release)
      RELEASE_MODE="$1"
      RELEASE_ARGS="--release"
      shift
      ;;
    -s)
      SKIP_DEPS="yes"
      shift
      ;;
    --)         # end of option parsing
      break
      ;;
    *)
      echo "invalid parameter: '$1'"
      exit 1
      ;;
  esac
done
# ### The order of parameters is important; buildcheck.sh depends on it and
# ### we don't want to copy the fancy option parsing loop there. For the
# ### same reason, all parameters should be quoted, so that buildcheck.sh
# ### sees an empty arg rather than missing one.
./build/buildcheck.sh "$RELEASE_MODE" || exit 1

# Handle some libtool helper files
#
# ### eventually, we can/should toss this in favor of simply using
# ### APR's libtool. deferring to a second round of change...
#

# Much like APR except we do not prefer libtool 1 over libtool 2.
libtoolize="`./build/PrintPath glibtoolize libtoolize glibtoolize1 libtoolize15 libtoolize14`"
lt_major_version=`$libtoolize --version 2>/dev/null | sed -e 's/^[^0-9]*//' -e 's/\..*//' -e '/^$/d' -e 1q`

if [ "x$libtoolize" = "x" ]; then
    echo "libtoolize not found in path"
    exit 1
fi

rm -f build/config.guess build/config.sub
$libtoolize --copy --automake --force

ltpath="`dirname $libtoolize`"

if [ "x$LIBTOOL_M4" = "x" ]; then
    ltm4_error='(try setting the LIBTOOL_M4 environment variable)'
    if [ -d "$ltpath/../share/aclocal/." ]; then
        ltm4=`cd "$ltpath/../share/aclocal" && pwd`
    else
        echo "Libtool helper path not found $ltm4_error"
        echo "  expected at: '$ltpath/../share/aclocal'"
        exit 1
    fi
else
    ltm4_error="(the LIBTOOL_M4 environment variable is: $LIBTOOL_M4)"
    ltm4="$LIBTOOL_M4"
fi

ltfile="$ltm4/libtool.m4"
if [ ! -f "$ltfile" ]; then
    echo "$ltfile not found $ltm4_error"
    exit 1
fi

echo "Copying libtool helper:  $ltfile"
# An ancient helper might already be present from previous builds,
# and it might be write-protected (e.g. mode 444, seen on FreeBSD).
# This would cause cp to fail and print an error message, but leave
# behind a potentially outdated libtool helper.  So, remove before
# copying:
rm -f build/libtool.m4
cp "$ltfile" build/libtool.m4

for file in ltoptions.m4 ltsugar.m4 ltversion.m4 lt~obsolete.m4; do
    rm -f build/$file

    if [ $lt_major_version -ge 2 ]; then
        ltfile="$ltm4/$file"

        if [ ! -f "$ltfile" ]; then
            echo "$ltfile not found $ltm4_error"
            exit 1
        fi

        echo "Copying libtool helper:  $ltfile"
        cp "$ltfile" "build/$file"
    fi
done

if [ $lt_major_version -ge 2 ]; then
    if [ "x$LIBTOOL_CONFIG" = "x" ]; then
        ltconfig_error='(try setting the LIBTOOL_CONFIG environment variable)'
        if [ -d "$ltpath/../share/libtool/config/." ]; then
            ltconfig=`cd "$ltpath/../share/libtool/config" && pwd`
        elif [ -d "$ltpath/../share/libtool/build-aux/." ]; then
            ltconfig=`cd "$ltpath/../share/libtool/build-aux" && pwd`
        else
            echo "Autoconf helper path not found $ltconfig_error"
            echo "  expected at: '$ltpath/../share/libtool/config'"
            echo "           or: '$ltpath/../share/libtool/build-aux'"
            exit 1
        fi
    else
        ltconfig_error="(the LIBTOOL_CONFIG environment variable is: $LIBTOOL_CONFIG)"
        ltconfig="$LIBTOOL_CONFIG"
    fi

    for file in config.guess config.sub; do
        configfile="$ltconfig/$file"

        if [ ! -f "$configfile" ]; then
            echo "$configfile not found $ltconfig_error"
            exit 1
        fi

        echo "Copying autoconf helper: $configfile"
	cp "$configfile" build/$file
    done
fi

# Create the file detailing all of the build outputs for SVN.
#
# Note: this dependency on Python is fine: only SVN developers use autogen.sh
#       and we can state that dev people need Python on their machine. Note
#       that running gen-make.py requires Python 2.7 or newer.

PYTHON="`./build/find_python.sh`"
if test -z "$PYTHON"; then
  echo "Python 2.7 or later is required to run autogen.sh"
  echo "If you have a suitable Python installed, but not on the"
  echo "PATH, set the environment variable PYTHON to the full path"
  echo "to the Python executable, and re-run autogen.sh"
  exit 1
fi

# Compile SWIG headers into standalone C files if we are in release mode
if test -n "$RELEASE_MODE"; then
  echo "Generating SWIG code..."
  # Generate build-outputs.mk in non-release-mode, so that we can
  # build the SWIG-related files
  "$PYTHON" ./gen-make.py build.conf || gen_failed=1

  # Build the SWIG-related files
  make -f autogen-standalone.mk autogen-swig

  # Remove the .swig_checked file
  rm -f .swig_checked
fi

if test -n "$SKIP_DEPS"; then
  echo "Creating build-outputs.mk (no dependencies)..."
  "$PYTHON" ./gen-make.py $RELEASE_ARGS -s build.conf || gen_failed=1
else
  echo "Creating build-outputs.mk..."
  "$PYTHON" ./gen-make.py $RELEASE_ARGS build.conf || gen_failed=1
fi

if test -n "$RELEASE_MODE"; then
  find build/ -name '*.pyc' -exec rm {} \;
fi

rm autogen-standalone.mk

if test -n "$gen_failed"; then
  echo "ERROR: gen-make.py failed"
  exit 1
fi

# Produce config.h.in
echo "Creating svn_private_config.h.in..."
${AUTOHEADER:-autoheader}

# If there's a config.cache file, we may need to delete it.
# If we have an existing configure script, save a copy for comparison.
if [ -f config.cache ] && [ -f configure ]; then
  cp configure configure.$$.tmp
fi

# Produce ./configure
echo "Creating configure..."
${AUTOCONF:-autoconf}

# If we have a config.cache file, toss it if the configure script has
# changed, or if we just built it for the first time.
if [ -f config.cache ]; then
  (
    [ -f configure.$$.tmp ] && cmp configure configure.$$.tmp > /dev/null 2>&1
  ) || (
    echo "Tossing config.cache, since configure has changed."
    rm config.cache
  )
  rm -f configure.$$.tmp
fi

# Remove autoconf 2.5x's cache directory
rm -rf autom4te*.cache

echo ""
echo "You can run ./configure now."
echo ""
echo "Running autogen.sh implies you are a maintainer.  You may prefer"
echo "to run configure in one of the following ways:"
echo ""
echo "./configure --enable-maintainer-mode"
echo "./configure --disable-shared"
echo "./configure --enable-maintainer-mode --disable-shared"
echo "./configure --disable-optimize --enable-debug"
echo "./configure CFLAGS='--flags-for-C' CXXFLAGS='--flags-for-C++'"
echo ""
echo "Note:  If you wish to run a Subversion HTTP server, you will need"
echo "Apache 2.x.  See the INSTALL file for details."
echo ""
