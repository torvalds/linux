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
#
# get-deps.sh -- download the dependencies useful for building Subversion
#

# If changing this file please take care to try to make your changes as
# portable as possible.  That means at a minimum only use POSIX supported
# features and functions.  However, it may be desirable to use an even
# more narrow set of features than POSIX, e.g. Solaris /bin/sh only has
# a subset of the POSIX shell features.  If in doubt, limit yourself to
# features already used in the file.  Reviewing the history of changes
# may be useful as well.

APR_VERSION=${APR_VERSION:-"1.4.6"}
APU_VERSION=${APU_VERSION:-"1.5.1"}
SERF_VERSION=${SERF_VERSION:-"1.3.8"}
ZLIB_VERSION=${ZLIB_VERSION:-"1.2.8"}
SQLITE_VERSION=${SQLITE_VERSION:-"3.8.11.1"}
# Used to construct the SQLite download URL.
SQLITE_VERSION_REL_YEAR=2015
GTEST_VERSION=${GMOCK_VERSION:-"1.7.0"}
GMOCK_VERSION=${GMOCK_VERSION:-"1.7.0"}
HTTPD_VERSION=${HTTPD_VERSION:-"2.4.10"}
APR_ICONV_VERSION=${APR_ICONV_VERSION:-"1.2.1"}

APR=apr-${APR_VERSION}
APR_UTIL=apr-util-${APU_VERSION}
SERF=serf-${SERF_VERSION}
ZLIB=zlib-${ZLIB_VERSION}
SQLITE_VERSION_LIST=`echo $SQLITE_VERSION | sed -e 's/\./ /g'`
SQLITE=sqlite-amalgamation-`printf %d%02d%02d%02d $SQLITE_VERSION_LIST`
GTEST=release-${GTEST_VERSION}
GTEST_URL=https://github.com/google/googletest/archive
GMOCK=release-${GMOCK_VERSION}
GMOCK_URL=https://github.com/google/googlemock/archive

HTTPD=httpd-${HTTPD_VERSION}
APR_ICONV=apr-iconv-${APR_ICONV_VERSION}

BASEDIR=`pwd`
TEMPDIR=$BASEDIR/temp

HTTP_FETCH=
[ -z "$HTTP_FETCH" ] && type wget  >/dev/null 2>&1 && HTTP_FETCH="wget -q -nc"
[ -z "$HTTP_FETCH" ] && type curl  >/dev/null 2>&1 && HTTP_FETCH="curl -sOL"
[ -z "$HTTP_FETCH" ] && type fetch >/dev/null 2>&1 && HTTP_FETCH="fetch -q"

# Need this uncommented if any of the specific versions of the ASF tarballs to
# be downloaded are no longer available on the general mirrors.
APACHE_MIRROR=http://archive.apache.org/dist

# helpers
usage() {
    echo "Usage: $0"
    echo "Usage: $0 [ apr | serf | zlib | sqlite | googlemock ] ..."
    exit $1
}

# getters
get_apr() {
    cd $TEMPDIR
    test -d $BASEDIR/apr      || $HTTP_FETCH $APACHE_MIRROR/apr/$APR.tar.bz2
    test -d $BASEDIR/apr-util || $HTTP_FETCH $APACHE_MIRROR/apr/$APR_UTIL.tar.bz2
    cd $BASEDIR

    test -d $BASEDIR/apr      || bzip2 -dc $TEMPDIR/$APR.tar.bz2 | tar -xf -
    test -d $BASEDIR/apr-util || bzip2 -dc $TEMPDIR/$APR_UTIL.tar.bz2 | tar -xf -

    test -d $BASEDIR/apr      || mv $APR apr
    test -d $BASEDIR/apr-util || mv $APR_UTIL apr-util
}

get_serf() {
    test -d $BASEDIR/serf && return

    cd $TEMPDIR
    $HTTP_FETCH https://archive.apache.org/dist/serf/$SERF.tar.bz2
    cd $BASEDIR

    bzip2 -dc $TEMPDIR/$SERF.tar.bz2 | tar -xf -

    mv $SERF serf
}

get_zlib() {
    test -d $BASEDIR/zlib && return

    cd $TEMPDIR
    $HTTP_FETCH http://sourceforge.net/projects/libpng/files/zlib/$ZLIB_VERSION/$ZLIB.tar.gz
    cd $BASEDIR

    gzip -dc $TEMPDIR/$ZLIB.tar.gz | tar -xf -

    mv $ZLIB zlib
}

get_sqlite() {
    test -d $BASEDIR/sqlite-amalgamation && return

    cd $TEMPDIR
    $HTTP_FETCH https://www.sqlite.org/$SQLITE_VERSION_REL_YEAR/$SQLITE.zip
    cd $BASEDIR

    unzip -q $TEMPDIR/$SQLITE.zip

    mv $SQLITE sqlite-amalgamation

}

get_googlemock() {
    test -d $BASEDIR/googlemock && return

    cd $TEMPDIR
    $HTTP_FETCH ${GTEST_URL}/${GTEST}.zip
    unzip -q ${GTEST}.zip
    rm -f ${GTEST}.zip

    $HTTP_FETCH ${GMOCK_URL}/${GMOCK}.zip
    unzip -q ${GMOCK}.zip
    rm -f ${GMOCK}.zip

    cd $BASEDIR
    mkdir googlemock
    mv $TEMPDIR/googletest-release-${GTEST_VERSION} googlemock/googletest
    mv $TEMPDIR/googlemock-release-${GMOCK_VERSION} googlemock/googlemock
}

# main()
get_deps() {
    mkdir -p $TEMPDIR

    for i in zlib serf sqlite-amalgamation apr apr-util gmock-fused; do
      if [ -d $i ]; then
        echo "Local directory '$i' already exists; the downloaded copy won't be used" >&2
      fi
    done

    if [ $# -gt 0 ]; then
      for target in "$@"; do
        if [ "$target" != "deps" ]; then
          get_$target || usage
        else
          usage
        fi
      done
    else
      get_apr
      get_serf
      get_zlib
      get_sqlite

      echo
      echo "If you require mod_dav_svn, the recommended version of httpd is:"
      echo "   $APACHE_MIRROR/httpd/$HTTPD.tar.bz2"

      echo
      echo "If you require apr-iconv, its recommended version is:"
      echo "   $APACHE_MIRROR/apr/$APR_ICONV.tar.bz2"
    fi

    rm -rf $TEMPDIR
}

get_deps "$@"
