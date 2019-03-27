dnl     Licensed to the Apache Software Foundation (ASF) under one
dnl     or more contributor license agreements.  See the NOTICE file
dnl     distributed with this work for additional information
dnl     regarding copyright ownership.  The ASF licenses this file
dnl     to you under the Apache License, Version 2.0 (the
dnl     "License"); you may not use this file except in compliance
dnl     with the License.  You may obtain a copy of the License at
dnl    
dnl       http://www.apache.org/licenses/LICENSE-2.0
dnl    
dnl     Unless required by applicable law or agreed to in writing,
dnl     software distributed under the License is distributed on an
dnl     "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
dnl     KIND, either express or implied.  See the License for the
dnl     specific language governing permissions and limitations
dnl     under the License.

dnl configure.ac: Autoconfiscation for Subversion
dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

dnl Get the version of Subversion, using m4's esyscmd() command to do this
dnl at m4-time, since AC_INIT() requires it then.
AC_INIT([subversion],
     [esyscmd(python build/getversion.py SVN subversion/include/svn_version.h)],
     [http://subversion.apache.org/])

AC_CONFIG_SRCDIR(subversion/include/svn_types.h)
AC_CONFIG_AUX_DIR([build])

AC_MSG_NOTICE([Configuring Subversion ]AC_PACKAGE_VERSION)

AC_SUBST([abs_srcdir], ["`cd $srcdir && pwd`"])
AC_SUBST([abs_builddir], ["`pwd`"])
if test "$abs_srcdir" = "$abs_builddir"; then
  canonicalized_srcdir=""
else
  canonicalized_srcdir="$srcdir/"
fi
AC_SUBST([canonicalized_srcdir])

SWIG_LDFLAGS="$LDFLAGS"
AC_SUBST([SWIG_LDFLAGS])

# Generate config.nice early (before the arguments are munged)
SVN_CONFIG_NICE(config.nice)

# ==== Check for programs ====================================================

# Look for a C compiler (before anything can set CFLAGS)
CUSERFLAGS="$CFLAGS"
AC_PROG_CC
SVN_CC_MODE_SETUP

# Look for a C++ compiler (before anything can set CXXFLAGS)
CXXUSERFLAGS="$CXXFLAGS"
AC_PROG_CXX
SVN_CXX_MODE_SETUP

# Look for a C pre-processor
AC_PROG_CPP

# Look for a good sed
# AC_PROG_SED was introduced in Autoconf 2.59b
m4_ifdef([AC_PROG_SED], [AC_PROG_SED], [SED="${SED:-sed}"])

# Grab target_cpu, so we can use it in the Solaris pkginfo file
AC_CANONICAL_TARGET

# Look for an extended grep
AC_PROG_EGREP

AC_PROG_LN_S

AC_PROG_INSTALL
# If $INSTALL is relative path to our fallback install-sh, then convert
# to an absolute path, as in some cases (e.g. Solaris VPATH build), libtool
# may try to use it from a changed working directory.
if test "$INSTALL" = "build/install-sh -c"; then
  INSTALL="$abs_srcdir/$INSTALL"
fi

if test -z "$MKDIR"; then
  MKDIR="$INSTALL -d"
fi
AC_SUBST([MKDIR])

# ==== Libraries, for which we may have source to build ======================

dnl verify apr version and set apr flags
dnl These regular expressions should not contain "\(" and "\)".

APR_VER_REGEXES=["1\.[3-9]\. 2\."]

SVN_LIB_APR($APR_VER_REGEXES)

if test `expr $apr_version : 2` -ne 0; then
  dnl Bump the library so-version to 2 if using APR-2
  dnl (Debian uses so-version 1 for APR-1-with-largefile)
  svn_lib_ver=2
  dnl APR-2 provides APRUTIL
  apu_config=$apr_config
  AC_SUBST(SVN_APRUTIL_INCLUDES)
  AC_SUBST(SVN_APRUTIL_CONFIG, ["$apu_config"])
  AC_SUBST(SVN_APRUTIL_LIBS)
  SVN_APR_MAJOR_VERSION=2
else
  svn_lib_ver=0
  APU_VER_REGEXES=["1\.[3-9]\."]
  SVN_LIB_APRUTIL($APU_VER_REGEXES)
  SVN_APR_MAJOR_VERSION=1
fi
AC_SUBST(SVN_APR_MAJOR_VERSION)
SVN_LT_SOVERSION="-version-info $svn_lib_ver"
AC_SUBST(SVN_LT_SOVERSION)
AC_DEFINE_UNQUOTED(SVN_SOVERSION, $svn_lib_ver,
                   [Subversion library major verson])

dnl Search for pkg-config
AC_PATH_PROG(PKG_CONFIG, pkg-config)

dnl Search for serf
SVN_LIB_SERF(1,3,4)

if test "$svn_lib_serf" = "yes"; then
  AC_DEFINE([SVN_HAVE_SERF], 1,
            [Defined if support for Serf is enabled])
fi

dnl Search for apr_memcache (only affects fs_fs)
SVN_LIB_APR_MEMCACHE

if test "$svn_lib_apr_memcache" = "yes"; then
  AC_DEFINE(SVN_HAVE_MEMCACHE, 1,
            [Defined if apr_memcache (standalone or in apr-util) is present])
fi

AC_ARG_ENABLE(apache-whitelist,
  AS_HELP_STRING([--enable-apache-whitelist=VER],
                 [Whitelist a particular Apache version number,
                  typically used to enable the use of a old version
                  patched by a distribution.]),
                 [apache_whitelist_ver=$enableval],
                 [apache_whitelist_ver=no])
HTTPD_WHITELIST="$apache_whitelist_ver"
AC_SUBST(HTTPD_WHITELIST)

dnl Find Apache with a recent-enough magic module number
SVN_FIND_APACHE(20051115, $apache_whitelist_ver)

dnl Search for SQLite.  If you change SQLITE_URL from a .zip to
dnl something else also update build/ac-macros/sqlite.m4 to reflect
dnl the correct command to unpack the downloaded file.
SQLITE_MINIMUM_VER="3.8.2"
SQLITE_RECOMMENDED_VER="3.8.11.1"
dnl Used to construct the SQLite download URL.
SQLITE_RECOMMENDED_VER_REL_YEAR="2015"
SQLITE_URL="https://www.sqlite.org/$SQLITE_RECOMMENDED_VER_REL_YEAR/sqlite-amalgamation-$(printf %d%02d%02d%02d $(echo ${SQLITE_RECOMMENDED_VER} | sed -e 's/\./ /g')).zip"

SVN_LIB_SQLITE(${SQLITE_MINIMUM_VER}, ${SQLITE_RECOMMENDED_VER},
               ${SQLITE_URL})

AC_ARG_ENABLE(sqlite-compatibility-version,
  AS_HELP_STRING([--enable-sqlite-compatibility-version=X.Y.Z],
                 [Allow binary to run against SQLite as old as ARG]),
  [sqlite_compat_ver=$enableval],[sqlite_compat_ver=no])

if test -n "$sqlite_compat_ver" && test "$sqlite_compat_ver" != no; then
  SVN_SQLITE_VERNUM_PARSE([$sqlite_compat_ver],
                          [sqlite_compat_ver_num])
  CFLAGS="-DSVN_SQLITE_MIN_VERSION='\"$sqlite_compat_ver\"' $CFLAGS"
  CFLAGS="-DSVN_SQLITE_MIN_VERSION_NUMBER=$sqlite_compat_ver_num $CFLAGS"
fi

SVN_CHECK_FOR_ATOMIC_BUILTINS
if test "$svn_cv_atomic_builtins" = "yes"; then
    AC_DEFINE(SVN_HAS_ATOMIC_BUILTINS, 1, [Define if compiler provides atomic builtins])
fi

dnl Set up a number of directories ---------------------

dnl Create SVN_BINDIR for proper substitution
if test "${bindir}" = '${exec_prefix}/bin'; then
        if test "${exec_prefix}" = "NONE"; then
                if test "${prefix}" = "NONE"; then
                        SVN_BINDIR="${ac_default_prefix}/bin"
                else
                        SVN_BINDIR="${prefix}/bin"
                fi
        else
                SVN_BINDIR="${exec_prefix}/bin"
        fi
else
        SVN_BINDIR="${bindir}"
fi

dnl fully evaluate this value. when we substitute it into our tool scripts,
dnl they will not have things such as ${bindir} available
SVN_BINDIR="`eval echo ${SVN_BINDIR}`"
AC_SUBST(SVN_BINDIR)

dnl provide ${bindir} in svn_private_config.h for use in compiled code
AC_DEFINE_UNQUOTED(SVN_BINDIR, "${SVN_BINDIR}",
        [Defined to be the path to the installed binaries])

dnl This purposely does *not* allow for multiple parallel installs.
dnl However, it is compatible with most gettext usages.
localedir='${datadir}/locale'
AC_SUBST(localedir)

dnl For SVN_LOCALE_DIR, we have to expand it to something.  See SVN_BINDIR.
if test "${prefix}" = "NONE" \
  && ( test "${datadir}" = '${prefix}/share' \
       || ( test "${datadir}" = '${datarootdir}' \
            && test "${datarootdir}" = '${prefix}/share' ) ); then
  exp_localedir='${ac_default_prefix}/share/locale'
else
  exp_localedir=$localedir
fi
SVN_EXPAND_VAR(svn_localedir, "${exp_localedir}")
AC_DEFINE_UNQUOTED(SVN_LOCALE_DIR, "${svn_localedir}",
                   [Defined to be the path to the installed locale dirs])

dnl Check for libtool -- we'll definitely need it for all our shared libs!
AC_MSG_NOTICE([configuring libtool now])
ifdef([LT_INIT], [LT_INIT], [AC_PROG_LIBTOOL])
AC_ARG_ENABLE(experimental-libtool,
  AS_HELP_STRING([--enable-experimental-libtool],[Use APR's libtool]),
  [experimental_libtool=$enableval],[experimental_libtool=no])

if test "$experimental_libtool" = "yes"; then
  echo "using APR's libtool"
  sh_libtool="`$apr_config --apr-libtool`"
  LIBTOOL="$sh_libtool"
  SVN_LIBTOOL="$sh_libtool"
else
  sh_libtool="$abs_builddir/libtool"
  SVN_LIBTOOL="\$(SHELL) \"$sh_libtool\""
fi
AC_SUBST(SVN_LIBTOOL)

dnl Determine the libtool version
changequote(, )dnl
lt_pversion=`$LIBTOOL --version 2>/dev/null|$SED -e 's/([^)]*)//g;s/^[^0-9]*//;s/[- ].*//g;q'`
lt_version=`echo $lt_pversion|$SED -e 's/\([a-z]*\)$/.\1/'`
lt_major_version=`echo $lt_version | cut -d'.' -f 1`
changequote([, ])dnl

dnl set the default parameters
svn_enable_static=yes
svn_enable_shared=yes

dnl check for --enable-static option
AC_ARG_ENABLE(static,
  AS_HELP_STRING([--enable-static],
                 [Build static libraries]),
  [svn_enable_static="$enableval"], [svn_enable_static="yes"])

dnl check for --enable-shared option
AC_ARG_ENABLE(shared,
  AS_HELP_STRING([--enable-shared],
                 [Build shared libraries]),
  [svn_enable_shared="$enableval"], [svn_enable_shared="yes"])

if test "$svn_enable_static" = "yes" && test "$svn_enable_shared" = "yes" ; then
  AC_MSG_NOTICE([building both shared and static libraries])
elif test "$svn_enable_static" = "yes" ; then
  AC_MSG_NOTICE([building static libraries only])
  LT_CFLAGS="-static $LT_CFLAGS"
  LT_LDFLAGS="-static $LT_LDFLAGS"
elif test "$svn_enable_shared" = "yes" ; then
  AC_MSG_NOTICE([building shared libraries only])
  if test "$lt_major_version" = "1" ; then
    LT_CFLAGS="-prefer-pic $LT_CFLAGS"
  elif test "$lt_major_version" = "2" ; then
    LT_CFLAGS="-shared $LT_CFLAGS"
  fi
  LT_LDFLAGS="-shared $LT_LDFLAGS"
else
  AC_MSG_ERROR([cannot disable both shared and static libraries])
fi

dnl Check for --enable-all-static option
AC_ARG_ENABLE(all-static,
  AS_HELP_STRING([--enable-all-static],
                 [Build completely static (standalone) binaries.]),
  [
    if test "$enableval" = "yes" ; then
      LT_LDFLAGS="-all-static $LT_LDFLAGS"
    elif test "$enableval" != "no" ; then
      AC_MSG_ERROR([--enable-all-static doesn't accept argument])
    fi
])

AC_SUBST(LT_CFLAGS)
AC_SUBST(LT_LDFLAGS)

AC_ARG_ENABLE(local-library-preloading,
  AS_HELP_STRING([--enable-local-library-preloading], 
                 [Enable preloading of locally built libraries in locally
                  built executables.  This may be necessary for testing
                  prior to installation on some platforms.  It does not
                  work on some platforms (Darwin, OpenBSD, ...).]),
  [
  if test "$enableval" != "no"; then
    if test "$svn_enable_shared" = "yes"; then
      TRANSFORM_LIBTOOL_SCRIPTS="transform-libtool-scripts"
    else
      AC_MSG_ERROR([--enable-local-library-preloading conflicts with --disable-shared])
    fi
  else
    TRANSFORM_LIBTOOL_SCRIPTS=""
  fi
  ], [
  TRANSFORM_LIBTOOL_SCRIPTS=""
])
AC_SUBST(TRANSFORM_LIBTOOL_SCRIPTS)

dnl Check if -no-undefined is needed for the platform.
dnl It should always work but with libtool 1.4.3 on OS X it breaks the build.
dnl So we only turn it on for platforms where we know we really need it.
AC_MSG_CHECKING([whether libtool needs -no-undefined])
case $host in
  *-*-cygwin*)
    AC_MSG_RESULT([yes])
    LT_NO_UNDEFINED="-no-undefined"
    ;;
  *)
    AC_MSG_RESULT([no])
    LT_NO_UNDEFINED=""
    ;;
esac
AC_SUBST(LT_NO_UNDEFINED)

dnl Check for trang.
trang=yes
AC_ARG_WITH(trang,
AS_HELP_STRING([--with-trang=PATH],
               [Specify the command to run the trang schema converter]),
[
    trang="$withval"
])
if test "$trang" = "yes"; then
    AC_PATH_PROG(TRANG, trang, none)
else
    TRANG="$trang"
    AC_SUBST(TRANG)
fi

dnl Check for doxygen
doxygen=yes
AC_ARG_WITH(doxygen,
AC_HELP_STRING([--with-doxygen=PATH],
               [Specify the command to run doxygen]),
[
    doxygen="$withval"
])
if test "$doxygen" = "yes"; then
    AC_PATH_PROG(DOXYGEN, doxygen, none)
else
    DOXYGEN="$doxygen"
    AC_SUBST(DOXYGEN)
fi


dnl Check for libraries --------------------

dnl Expat -------------------

AC_ARG_WITH(expat,
  AS_HELP_STRING([--with-expat=INCLUDES:LIB_SEARCH_DIRS:LIBS], 
                 [Specify location of Expat]),
                 [svn_lib_expat="$withval"],
                 [svn_lib_expat="::expat"])

# APR-util accepts "builtin" as an argument to this option so if the user
# passed "builtin" pretend the user didn't specify the --with-expat option
# at all. Expat will (hopefully) be found in apr-util.
test "_$svn_lib_expat" = "_builtin" && svn_lib_expat="::expat"

AC_MSG_CHECKING([for Expat])
if test -n "`echo "$svn_lib_expat" | $EGREP ":.*:"`"; then
  SVN_XML_INCLUDES=""
  for i in [`echo "$svn_lib_expat" | $SED -e "s/\([^:]*\):.*/\1/"`]; do
    SVN_XML_INCLUDES="$SVN_XML_INCLUDES -I$i"
  done
  SVN_XML_INCLUDES="${SVN_XML_INCLUDES## }"
  for l in [`echo "$svn_lib_expat" | $SED -e "s/.*:\([^:]*\):.*/\1/"`]; do
    LDFLAGS="$LDFLAGS -L$l"
  done
  for l in [`echo "$svn_lib_expat" | $SED -e "s/.*:\([^:]*\)/\1/"`]; do
    SVN_XML_LIBS="$SVN_XML_LIBS -l$l"
  done
  SVN_XML_LIBS="${SVN_XML_LIBS## }"
  old_CPPFLAGS="$CPPFLAGS"
  old_LIBS="$LIBS"
  CPPFLAGS="$CPPFLAGS $SVN_XML_INCLUDES"
  LIBS="$LIBS $SVN_XML_LIBS"
  AC_LINK_IFELSE([AC_LANG_SOURCE([[
#include <expat.h>
int main()
{XML_ParserCreate(NULL);}]])], svn_lib_expat="yes", svn_lib_expat="no")
  LIBS="$old_LIBS"
  if test "$svn_lib_expat" = "yes"; then
    AC_MSG_RESULT([yes])
  else
    SVN_XML_INCLUDES=""
    SVN_XML_LIBS=""
    CPPFLAGS="$CPPFLAGS $SVN_APRUTIL_INCLUDES"
    if test "$enable_all_static" != "yes"; then
      SVN_APRUTIL_LIBS="$SVN_APRUTIL_LIBS `$apu_config --libs`"
    fi
    AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#include <expat.h>
int main()
{XML_ParserCreate(NULL);}]])], svn_lib_expat="yes", svn_lib_expat="no")
    if test "$svn_lib_expat" = "yes"; then
      AC_MSG_RESULT([yes])
      AC_MSG_WARN([Expat found amongst libraries used by APR-Util, but Subversion libraries might be needlessly linked against additional unused libraries. It can be avoided by specifying exact location of Expat in argument of --with-expat option.])
    else
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([Expat not found])
    fi
  fi
  CPPFLAGS="$old_CPPFLAGS"
else
  AC_MSG_RESULT([no])
  if test "$svn_lib_expat" = "yes"; then
    AC_MSG_ERROR([--with-expat option requires argument])
  elif test "$svn_lib_expat" = "no"; then
    AC_MSG_ERROR([Expat is required])
  else
    AC_MSG_ERROR([Invalid syntax of argument of --with-expat option])
  fi
fi
AC_SUBST(SVN_XML_INCLUDES)
AC_SUBST(SVN_XML_LIBS)

dnl Berkeley DB -------------------

# Berkeley DB on SCO OpenServer needs -lsocket
AC_CHECK_LIB(socket, socket)

# Build the BDB filesystem library only if we have an appropriate
# version of Berkeley DB.
case "$host" in
powerpc-apple-darwin*)
    # Berkeley DB 4.0 does not work on OS X.
    SVN_FS_WANT_DB_MAJOR=4
    SVN_FS_WANT_DB_MINOR=1
    SVN_FS_WANT_DB_PATCH=25
    ;;
*)
    SVN_FS_WANT_DB_MAJOR=4
    SVN_FS_WANT_DB_MINOR=0
    SVN_FS_WANT_DB_PATCH=14
    ;;
esac
db_alt_version="5.x"
# Look for libdb4.so first:
SVN_LIB_BERKELEY_DB($SVN_FS_WANT_DB_MAJOR, $SVN_FS_WANT_DB_MINOR,
                    $SVN_FS_WANT_DB_PATCH, [db4 db])

AC_DEFINE_UNQUOTED(SVN_FS_WANT_DB_MAJOR, $SVN_FS_WANT_DB_MAJOR,
                   [The desired major version for the Berkeley DB])
AC_DEFINE_UNQUOTED(SVN_FS_WANT_DB_MINOR, $SVN_FS_WANT_DB_MINOR,
                   [The desired minor version for the Berkeley DB])
AC_DEFINE_UNQUOTED(SVN_FS_WANT_DB_PATCH, $SVN_FS_WANT_DB_PATCH,
                   [The desired patch version for the Berkeley DB])

AC_SUBST(SVN_DB_INCLUDES)
AC_SUBST(SVN_DB_LIBS)

SVN_LIB_SASL

if test "$svn_lib_sasl" = "yes"; then
  AC_DEFINE(SVN_HAVE_SASL, 1,
            [Defined if Cyrus SASL v2 is present on the system])
fi

dnl Mac OS specific features -------------------

SVN_LIB_MACHO_ITERATE
SVN_LIB_MACOS_PLIST
SVN_LIB_MACOS_KEYCHAIN

dnl APR_HAS_DSO -------------------

AC_MSG_CHECKING([whether APR has support for DSOs])
old_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $SVN_APR_INCLUDES"
AC_PREPROC_IFELSE([AC_LANG_SOURCE([[
#include <apr.h>
#if !APR_HAS_DSO
#error
#endif]])],
  APR_HAS_DSO="yes"
  AC_MSG_RESULT([yes]),
  APR_HAS_DSO="no"
  AC_MSG_RESULT([no]))
CPPFLAGS="$old_CPPFLAGS"


dnl D-Bus (required for support for KWallet) -------------------

if test -n "$PKG_CONFIG"; then
  AC_MSG_CHECKING([for D-Bus .pc file])
  if $PKG_CONFIG --exists dbus-1; then
    AC_MSG_RESULT([yes])
    old_CPPFLAGS="$CPPFLAGS"
    old_LIBS="$LIBS"
    DBUS_CPPFLAGS="`$PKG_CONFIG --cflags dbus-1`"
    AC_MSG_CHECKING([D-Bus version])
    DBUS_VERSION="`$PKG_CONFIG --modversion dbus-1`"
    AC_MSG_RESULT([$DBUS_VERSION])
    # D-Bus 0.* requires DBUS_API_SUBJECT_TO_CHANGE
    if test -n ["`echo "$DBUS_VERSION" | $EGREP '^0\.[[:digit:]]+'`"]; then
      DBUS_CPPFLAGS="$DBUS_CPPFLAGS -DDBUS_API_SUBJECT_TO_CHANGE"
    fi
    DBUS_LIBS="`$PKG_CONFIG --libs dbus-1`"
    CPPFLAGS="$CPPFLAGS $DBUS_CPPFLAGS"
    LIBS="$LIBS $DBUS_LIBS"
    AC_MSG_CHECKING([for D-Bus])
    AC_LINK_IFELSE([AC_LANG_SOURCE([[
#include <dbus/dbus.h>
int main()
{dbus_bus_get(DBUS_BUS_SESSION, NULL);}]])], HAVE_DBUS="yes", HAVE_DBUS="no")
    if test "$HAVE_DBUS" = "yes"; then
      AC_MSG_RESULT([yes])
    else
      AC_MSG_RESULT([no])
    fi
    CPPFLAGS="$old_CPPFLAGS"
    LIBS="$old_LIBS"
  else
    AC_MSG_RESULT([no])
  fi
fi

dnl GPG Agent -------------------

AC_ARG_WITH(gpg_agent,
AS_HELP_STRING([--without-gpg-agent], 
               [Disable support for GPG-Agent]),
               [], [with_gpg_agent=yes])
AC_MSG_CHECKING([whether to support GPG-Agent])
if test "$with_gpg_agent" = "yes"; then
  AC_MSG_RESULT([yes])
  AC_DEFINE([SVN_HAVE_GPG_AGENT], [1], 
            [Is GPG Agent support enabled?])
else
  AC_MSG_RESULT([no])
fi

AC_SUBST(SVN_HAVE_GPG_AGENT)

dnl GNOME Keyring -------------------

AC_ARG_WITH(old_gnome_keyring,
  AS_HELP_STRING([--with-old-gnome-keyring], 
                 [Enable old GNOME Keyring for auth credentials (prefer --with-gnome-keyring)]),
                 [with_old_gnome_keyring="$withval"],
                 [with_old_gnome_keyring=no])

found_old_gnome_keyring=no
AC_MSG_CHECKING([whether to look for old GNOME Keyring])
if test "$with_old_gnome_keyring" != "no"; then
  AC_MSG_RESULT([yes])
  case "$host" in
  *-*-darwin*)
    if test "$with_old_gnome_keyring" = "yes"; then
      AC_MSG_ERROR([--with-old-gnome-keyring is not supported on Mac OS X.])
    else
      with_old_gnome_keyring=no
    fi
    ;;
  *)
    if test "$svn_enable_shared" = "yes"; then
      if test "$APR_HAS_DSO" = "yes"; then
        if test -n "$PKG_CONFIG"; then
          AC_MSG_CHECKING([for GLib and GNOME Keyring .pc files])
          if $PKG_CONFIG --exists glib-2.0 gnome-keyring-1; then
            AC_MSG_RESULT([yes])
            old_CPPFLAGS="$CPPFLAGS"
            SVN_GNOME_KEYRING_INCLUDES="`$PKG_CONFIG --cflags glib-2.0 gnome-keyring-1`"
            CPPFLAGS="$CPPFLAGS $SVN_GNOME_KEYRING_INCLUDES"
            AC_CHECK_HEADER(gnome-keyring.h, found_old_gnome_keyring=yes, found_old_gnome_keyring=no)
            AC_MSG_CHECKING([for GNOME Keyring])
            if test "$found_old_gnome_keyring" = "yes"; then
              AC_MSG_RESULT([yes])
              AC_DEFINE([SVN_HAVE_GNOME_KEYRING], [1], 
                        [Is GNOME Keyring support enabled?])
              CPPFLAGS="$old_CPPFLAGS"
              SVN_GNOME_KEYRING_LIBS="`$PKG_CONFIG --libs glib-2.0 gnome-keyring-1`"
            else
              AC_MSG_RESULT([no])
              if test "$with_old_gnome_keyring" = "yes"; then
                AC_MSG_ERROR([cannot find GNOME Keyring])
              fi
            fi
          else
            AC_MSG_RESULT([no])
            if test "$with_old_gnome_keyring" = "yes"; then
              AC_MSG_ERROR([cannot find GLib and GNOME Keyring .pc files.])
            else
              with_old_gnome_keyring=no
            fi
          fi
        else
          if test "$with_old_gnome_keyring" = "yes"; then
            AC_MSG_ERROR([cannot find pkg-config. GNOME Keyring requires this.])
          else
            with_old_gnome_keyring=no
          fi
        fi
      else
        if test "$with_old_gnome_keyring" = "yes"; then
          AC_MSG_ERROR([APR does not have support for DSOs. GNOME Keyring requires this.])
        else
          with_old_gnome_keyring=no
        fi
      fi
    else
      if test "$with_old_gnome_keyring" = "yes"; then
        AC_MSG_ERROR([--with-old-gnome-keyring conflicts with --disable-shared])
      else
        with_old_gnome_keyring=no
      fi
    fi
    ;;
  esac
else
  AC_MSG_RESULT([no])
fi
AC_SUBST(SVN_GNOME_KEYRING_INCLUDES)
AC_SUBST(SVN_GNOME_KEYRING_LIBS)

dnl LibSecret -------------------
SVN_LIB_SECRET

dnl Googlemock -----------------
AC_ARG_ENABLE([googlemock],
  AS_HELP_STRING([--disable-googlemock],
                 [Do not use the Googlemock testing framework]),
  [],
  [enable_googlemock=yes])

AC_SUBST([GOOGLEMOCK_SRCDIR], [$abs_srcdir/googlemock])
AC_MSG_CHECKING([whether use Googlemock])
if test "$enable_googlemock" != "no"; then
  if test -d "$GOOGLEMOCK_SRCDIR"; then
    AC_MSG_RESULT([yes])
    SVN_USE_GOOGLEMOCK=true
  else
    AC_MSG_RESULT([no])
    SVN_USE_GOOGLEMOCK=false
  fi
else
  AC_MSG_RESULT([no])
  SVN_USE_GOOGLEMOCK=false
fi
AC_SUBST([SVN_USE_GOOGLEMOCK])

dnl Ev2 experimental features ----------------------
dnl Note: The Ev2 implementations will be built unconditionally, but by
dnl providing this flag, users can choose to use the currently-shimmed Ev2
dnl editor implementations for various operations.  This will probably
dnl negatively impact performance, but is useful for testing.
AC_ARG_ENABLE(ev2-impl,
  AS_HELP_STRING([--enable-ev2-impl],
                 [Use Ev2 implementations, where available [EXPERIMENTAL]]),
  [enable_ev2_impl=$enableval],[enable_ev2_impl=no])
if test "$enable_ev2_impl" = "yes"; then
  AC_DEFINE(ENABLE_EV2_IMPL, 1,
            [Define to 1 if Ev2 implementations should be used.])
fi


dnl I18n -------------------

AC_ARG_ENABLE(nls,
  AS_HELP_STRING([--disable-nls],[Disable gettext functionality]),
  [enable_nls=$enableval],[enable_nls=yes])

USE_NLS="no"
SVN_INTL_LIBS=""
if test "$enable_nls" = "yes"; then
  dnl First, check to see if there is a working msgfmt.
  AC_PATH_PROG(MSGFMT, msgfmt, none)
  AC_PATH_PROG(MSGMERGE, msgmerge, none)
  AC_PATH_PROG(XGETTEXT, xgettext, none)
  if test "$MSGFMT" != "none"; then
    AC_SEARCH_LIBS(bindtextdomain, [intl],
                   [
                      # in case libintl needs to be linked explicitly,
                      # $ac_cv_search_bindtextdomain contains -l linker flags
                      if echo "$ac_cv_search_bindtextdomain" | grep '^-l' >/dev/null
                      then
                        SVN_INTL_LIBS="$ac_cv_search_bindtextdomain"
                      fi
                   ],
                   [
                    enable_nls="no"
                   ])
    if test "$enable_nls" = "no"; then
      # Destroy the cached result so we can test again
      unset ac_cv_search_bindtextdomain
      # On some systems, libintl needs libiconv to link properly,
      # so try again with -liconv.
      AC_SEARCH_LIBS(bindtextdomain, [intl],
                     [
                      enable_nls="yes"
                      if echo "$ac_cv_search_bindtextdomain" | grep '^-l' >/dev/null
                      then
                        SVN_INTL_LIBS="$ac_cv_search_bindtextdomain"
                      fi
                      # This is here so that -liconv ends up in LIBS
                      # if it worked with -liconv.
                      AC_CHECK_LIB(iconv, libiconv_open)
                     ], 
                     [
                      AC_MSG_WARN([bindtextdomain() not found.  Disabling NLS.])
                      enable_nls="no"
                     ], -liconv)
    fi
    if test "$enable_nls" = "yes"; then
      AC_DEFINE(ENABLE_NLS, 1,
                [Define to 1 if translation of program messages to the user's
                 native language is requested.])
      USE_NLS="yes"
    fi
  fi
fi

AC_SUBST(SVN_INTL_LIBS)

AH_BOTTOM([
/* Indicate to translators that string X should be translated.  Do not look
   up the translation at run time; just expand to X.  This macro is suitable
   for use where a constant string is required at compile time. */
#define N_(x) x
/* Indicate to translators that we have decided the string X should not be
   translated.  Expand to X. */
#define U_(x) x
#ifdef ENABLE_NLS
#include <locale.h>
#include <libintl.h>
/* Indicate to translators that string X should be translated.  At run time,
   look up and return the translation of X. */
#define _(x) dgettext(PACKAGE_NAME, x)
/* Indicate to translators that strings X1 and X2 are singular and plural
   forms of the same message, and should be translated.  At run time, return
   an appropriate translation depending on the number N. */
#define Q_(x1, x2, n) dngettext(PACKAGE_NAME, x1, x2, n)
#else
#define _(x) (x)
#define Q_(x1, x2, n) (((n) == 1) ? x1 : x2)
#define gettext(x) (x)
#define dgettext(domain, x) (x)
#endif

/* compiler hints */
#if defined(__GNUC__) && (__GNUC__ >= 3)
# define SVN__PREDICT_FALSE(x) (__builtin_expect(x, 0))
# define SVN__PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#else
# define SVN__PREDICT_FALSE(x) (x)
# define SVN__PREDICT_TRUE(x) (x)
#endif

#if defined(SVN_DEBUG)
# define SVN__FORCE_INLINE
# define SVN__PREVENT_INLINE
#elif defined(__GNUC__)
# define SVN__FORCE_INLINE APR_INLINE __attribute__ ((always_inline))
# define SVN__PREVENT_INLINE __attribute__ ((noinline))
#else
# define SVN__FORCE_INLINE APR_INLINE
# define SVN__PREVENT_INLINE
#endif

/* Macro used to specify that a variable is intentionally left unused.
   Supresses compiler warnings about the variable being unused.  */
#define SVN_UNUSED(v) ( (void)(v) )
])

dnl Used to simulate makefile conditionals.
GETTEXT_CODESET=\#
NO_GETTEXT_CODESET=\#
if test $USE_NLS = "yes"; then
   AC_CHECK_FUNCS(bind_textdomain_codeset,
                  [ GETTEXT_CODESET="" ],
                  [ NO_GETTEXT_CODESET="" ])
fi
AC_SUBST(GETTEXT_CODESET)
AC_SUBST(NO_GETTEXT_CODESET)

# Check if we are using GNU gettext.
GNU_GETTEXT=no
MSGFMTFLAGS=''
if test $USE_NLS = "yes"; then
   AC_MSG_CHECKING(if we are using GNU gettext)
   if $MSGFMT --version 2>&1 | $EGREP GNU > /dev/null; then
      GNU_GETTEXT=yes
      MSGFMTFLAGS='-c'
   fi
   AC_MSG_RESULT($GNU_GETTEXT)
fi
AC_SUBST(MSGFMTFLAGS)

dnl libmagic -------------------

libmagic_found=no

AC_ARG_WITH(libmagic,AS_HELP_STRING([--with-libmagic=PREFIX],
                                [libmagic filetype detection library]),
[
  if test "$withval" = "yes" ; then
    AC_CHECK_HEADER(magic.h, [
      AC_CHECK_LIB(magic, magic_open, [libmagic_found="builtin"])
    ])
    libmagic_prefix="the default locations"
  elif test "$withval" != "no"; then
    libmagic_prefix=$withval
    save_cppflags="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS -I$libmagic_prefix/include"
    AC_CHECK_HEADERS(magic.h,[
      save_ldflags="$LDFLAGS"
      LDFLAGS="-L$libmagic_prefix/lib $LDFLAGS"
      AC_CHECK_LIB(magic, magic_open, [libmagic_found="yes"])
      LDFLAGS="$save_ldflags"
    ])
    CPPFLAGS="$save_cppflags"
  fi
  if test "$withval" != "no" && test "$libmagic_found" = "no"; then
    AC_MSG_ERROR([[--with-libmagic requested, but libmagic not found at $libmagic_prefix]])
  fi
],
[
  AC_CHECK_HEADER(magic.h, [
    AC_CHECK_LIB(magic, magic_open, [libmagic_found="builtin"])
  ])
])

if test "$libmagic_found" != "no"; then
  AC_DEFINE([SVN_HAVE_LIBMAGIC], [1], [Defined if libmagic support is enabled])
  SVN_MAGIC_LIBS="-lmagic"
fi

if test "$libmagic_found" = "yes"; then
  SVN_MAGIC_INCLUDES="-I$libmagic_prefix/include"
  LDFLAGS="$LDFLAGS `SVN_REMOVE_STANDARD_LIB_DIRS(-L$libmagic_prefix/lib)`"
fi

AC_SUBST(SVN_MAGIC_INCLUDES)
AC_SUBST(SVN_MAGIC_LIBS)

dnl KWallet -------------------
SVN_LIB_KWALLET

if test "$svn_lib_kwallet" = "yes"; then
  AC_DEFINE([SVN_HAVE_KWALLET], 1,
            [Defined if KWallet support is enabled])
fi

dnl plaintext passwords -------------------
AC_ARG_ENABLE(plaintext-password-storage,
AS_HELP_STRING([--disable-plaintext-password-storage],
               [Disable on-disk caching of plaintext passwords and passphrases.
                (Leaving this functionality enabled will not force Subversion
                to store passwords in plaintext, but does permit users to
                explicitly allow that behavior via runtime configuration.)]),
[
   if test "$enableval" = "no"; then
      AC_MSG_NOTICE([Disabling plaintext password/passphrase storage])
      AC_DEFINE(SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE, 1,
                [Defined if plaintext password/passphrase storage is disabled])
   fi
])

dnl Build and install rules -------------------

INSTALL_STATIC_RULES="install-bin install-docs"
INSTALL_RULES="install-fsmod-lib install-ramod-lib install-lib install-include install-static"
INSTALL_RULES="$INSTALL_RULES $INSTALL_APACHE_RULE"
BUILD_RULES="fsmod-lib ramod-lib lib bin test sub-test $BUILD_APACHE_RULE tools"

if test "$svn_lib_berkeley_db" = "yes"; then
  BUILD_RULES="$BUILD_RULES bdb-lib bdb-test"
  INSTALL_RULES="`echo $INSTALL_RULES | $SED 's/install-fsmod-lib/install-fsmod-lib install-bdb-lib/'`"
  INSTALL_STATIC_RULES="$INSTALL_STATIC_RULES install-bdb-lib"
  BDB_TEST_DEPS="\$(BDB_TEST_DEPS)"
  BDB_TEST_PROGRAMS="\$(BDB_TEST_PROGRAMS)"
fi

if test "$svn_lib_serf" = "yes"; then
  BUILD_RULES="$BUILD_RULES serf-lib"
  INSTALL_RULES="`echo $INSTALL_RULES | $SED 's/install-ramod-lib/install-ramod-lib install-serf-lib/'`"
  INSTALL_STATIC_RULES="$INSTALL_STATIC_RULES install-serf-lib"
fi

if test "$svn_lib_kwallet" = "yes"; then
  BUILD_RULES="$BUILD_RULES kwallet-lib"
  INSTALL_RULES="`echo $INSTALL_RULES | $SED 's/install-lib/install-lib install-kwallet-lib/'`"
  INSTALL_STATIC_RULES="$INSTALL_STATIC_RULES install-kwallet-lib"
fi

if test "$found_old_gnome_keyring" = "yes" || test "$found_gnome_keyring" = "yes"; then
  BUILD_RULES="$BUILD_RULES gnome-keyring-lib"
  INSTALL_RULES="`echo $INSTALL_RULES | $SED 's/install-lib/install-lib install-gnome-keyring-lib/'`"
  INSTALL_STATIC_RULES="$INSTALL_STATIC_RULES install-gnome-keyring-lib"
fi

if test "$USE_NLS" = "yes"; then
  BUILD_RULES="$BUILD_RULES locale"
  INSTALL_RULES="$INSTALL_RULES install-locale"
fi

AC_SUBST(BUILD_RULES)
AC_SUBST(INSTALL_STATIC_RULES)
AC_SUBST(INSTALL_RULES)
AC_SUBST(BDB_TEST_DEPS)
AC_SUBST(BDB_TEST_PROGRAMS)

dnl Check for header files ----------------

dnl Standard C headers
AC_HEADER_STDC

dnl Check for typedefs, structures, and compiler characteristics ----------

dnl if compiler doesn't understand `const', then define it empty
AC_C_CONST

dnl if non-existent, define size_t to be `unsigned'
AC_TYPE_SIZE_T


dnl Check for library functions ----------

AC_FUNC_MEMCMP

dnl svn_error's default warning handler uses vfprintf()
AC_FUNC_VPRINTF

dnl check for functions needed in special file handling
AC_CHECK_FUNCS(symlink readlink)

dnl check for uname
AC_CHECK_HEADERS(sys/utsname.h, [AC_CHECK_FUNCS(uname)], [])

dnl check for termios
AC_CHECK_HEADER(termios.h,[
  AC_CHECK_FUNCS(tcgetattr tcsetattr,[
    AC_DEFINE(HAVE_TERMIOS_H,1,[Defined if we have a usable termios library.])
  ])
])

dnl Process some configuration options ----------

AC_ARG_ENABLE(debug,
AS_HELP_STRING([--enable-debug],
               [Turn on debugging]),
[
    if test "$enableval" = "yes" ; then
      enable_debugging="yes"
    else
      enable_debugging="no"
    fi
],
[
    # Neither --enable-debug nor --disable-debug was passed.
    enable_debugging="maybe"
])

AC_ARG_ENABLE(optimize,
AS_HELP_STRING([--enable-optimize],
               [Turn on optimizations]),
[
    if test "$enableval" = "yes" ; then
      enable_optimization="yes"
    else
      enable_optimization="no"
    fi
],
[
    # Neither --enable-optimize nor --disable-optimize was passed.
    enable_optimization="maybe"
])

dnl Use -Wl,--no-undefined during linking of some libraries
AC_ARG_ENABLE(disallowing-of-undefined-references,
  [AS_HELP_STRING([--enable-disallowing-of-undefined-references],
                  [Use -Wl,--no-undefined flag during linking of some libraries to disallow undefined references])])
if test "$enable_disallowing_of_undefined_references" != "yes" && test "`uname`" != "Linux"; then
  enable_disallowing_of_undefined_references="no"
fi
if test "$enable_disallowing_of_undefined_references" != "no"; then
  AC_MSG_CHECKING([for -Wl,--no-undefined])
  old_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS -Wl,--no-undefined"
  AC_LINK_IFELSE([AC_LANG_SOURCE([[int main(){;}]])], [svn_wl_no_undefined="yes"], [svn_wl_no_undefined="no"])
  LDFLAGS="$old_LDFLAGS"
  if test "$svn_wl_no_undefined" = "yes"; then
    AC_MSG_RESULT([yes])
    for library_dir in "$abs_srcdir/subversion/libsvn_"*; do
      eval "`basename $library_dir`_LDFLAGS=-Wl,--no-undefined"
    done
    shared_only_LDFLAGS="-Wl,--no-undefined"
  else
    AC_MSG_RESULT([no])
    if test "$enable_disallowing_of_undefined_references" = "yes"; then
      AC_MSG_ERROR([--enable-disallowing-of-undefined-references explicitly requested, but -Wl,--no-undefined not supported])
    fi
  fi
fi
AC_SUBST([libsvn_client_LDFLAGS])
AC_SUBST([libsvn_delta_LDFLAGS])
AC_SUBST([libsvn_diff_LDFLAGS])
AC_SUBST([libsvn_fs_LDFLAGS])
AC_SUBST([libsvn_fs_base_LDFLAGS])
AC_SUBST([libsvn_fs_fs_LDFLAGS])
AC_SUBST([libsvn_fs_util_LDFLAGS])
AC_SUBST([libsvn_ra_LDFLAGS])
AC_SUBST([libsvn_ra_local_LDFLAGS])
AC_SUBST([libsvn_ra_serf_LDFLAGS])
AC_SUBST([libsvn_ra_svn_LDFLAGS])
AC_SUBST([libsvn_repos_LDFLAGS])
AC_SUBST([libsvn_subr_LDFLAGS])
AC_SUBST([libsvn_wc_LDFLAGS])
AC_SUBST([shared_only_LDFLAGS])


AC_ARG_ENABLE(maintainer-mode,
AS_HELP_STRING([--enable-maintainer-mode],
               [Turn on debugging and very strict compile-time warnings]),
[
    if test "$enableval" = "yes" ; then
      if test "$enable_debugging" = "no" ; then
        AC_MSG_ERROR([Can't have --disable-debug and --enable-maintainer-mode])
      fi
      enable_debugging=yes

      dnl Enable some extra warnings. Put these before the user's flags
      dnl so the user can specify flags that override these.
      if test "$GCC" = "yes"; then
        AC_MSG_NOTICE([maintainer-mode: adding GCC warning flags])

        dnl some additional flags that can be handy for an occasional review,
        dnl but throw too many warnings in svn code, of too little importance,
        dnl to keep these enabled. Remove the "dnl" to do a run with these
        dnl switches enabled.
        dnl ./configure CFLAGS="-Wswitch-enum -Wswitch-default"

        dnl Add each of the following flags only if the C compiler accepts it.
        CFLAGS_KEEP="$CFLAGS"
        CFLAGS=""

        SVN_CFLAGS_ADD_IFELSE([-Werror=implicit-function-declaration])
        SVN_CFLAGS_ADD_IFELSE([-Werror=declaration-after-statement])
        SVN_CFLAGS_ADD_IFELSE([-Wextra-tokens])
        SVN_CFLAGS_ADD_IFELSE([-Wnewline-eof])
        SVN_CFLAGS_ADD_IFELSE([-Wshorten-64-to-32])
        SVN_CFLAGS_ADD_IFELSE([-Wold-style-definition])
        SVN_CFLAGS_ADD_IFELSE([-Wno-system-headers])
        SVN_CFLAGS_ADD_IFELSE([-Wno-format-nonliteral])
        SVN_CFLAGS_ADD_IFELSE([-Wmissing-variable-declarations])
        SVN_CFLAGS_ADD_IFELSE([-Wno-unused-const-variable])

        CMAINTAINERFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS_KEEP"

        dnl Add flags that all versions of GCC (should) support
        CMAINTAINERFLAGS="-Wall -Wpointer-arith -Wwrite-strings -Wshadow -Wformat=2 -Wunused -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wno-multichar -Wredundant-decls -Wnested-externs -Winline -Wno-long-long -Wbad-function-cast $CMAINTAINERFLAGS"
      fi
      if test "$GXX" = "yes"; then
        AC_MSG_NOTICE([maintainer-mode: adding G++ warning flags])

        dnl Add each of the following flags only if the C++ compiler accepts it.
        CXXFLAGS_KEEP="$CXXFLAGS"
        CXXFLAGS=""

        SVN_CXXFLAGS_ADD_IFELSE([-Wextra-tokens])
        SVN_CXXFLAGS_ADD_IFELSE([-Wnewline-eof])
        SVN_CXXFLAGS_ADD_IFELSE([-Wshorten-64-to-32])
        SVN_CXXFLAGS_ADD_IFELSE([-Wno-system-headers])

        CXXMAINTAINERFLAGS="$CXXFLAGS"
        CXXFLAGS="$CXXFLAGS_KEEP"

        dnl Add flags that all versions of G++ (should) support
        CXXMAINTAINERFLAGS="-Wall -Wpointer-arith -Wwrite-strings -Wshadow -Wunused -Wunreachable-code $CXXMAINTAINERFLAGS"
      fi
    fi
])

if test "$enable_debugging" = "yes" ; then
  dnl At the moment, we don't want optimization, because we're
  dnl debugging. Unless optiization was explicitly enabled.
  if test "$enable_optimization" != "yes"; then
    AC_MSG_NOTICE([Disabling optimizations for debugging])
    CFLAGS=["`echo $CFLAGS' ' | $SED -e 's/-O[^ ]* //g'`"]
    CXXFLAGS=["`echo $CXXFLAGS' ' | $SED -e 's/-O[^ ]* //g'`"]
  fi
  dnl Add debugging flags, unless they were set by the user
  if test -z ["`echo $CUSERFLAGS' ' | $EGREP -- '-g[0-9]? '`"]; then
    AC_MSG_NOTICE([Enabling debugging for C])
    CFLAGS=["`echo $CFLAGS' ' | $SED -e 's/-g[0-9] //g' -e 's/-g //g'`"]
    SVN_CFLAGS_ADD_IFELSE([-fno-inline])
    SVN_CFLAGS_ADD_IFELSE([-fno-omit-frame-pointer])
    SVN_CFLAGS_ADD_IFELSE([-g3],[],[
      SVN_CFLAGS_ADD_IFELSE([-g2],[],[
        SVN_CFLAGS_ADD_IFELSE([-g])])])
  fi
  if test -z ["`echo $CXXUSERFLAGS' ' | $EGREP -- '-g[0-9]? '`"]; then
    AC_MSG_NOTICE([Enabling debugging for C++])
    CXXFLAGS=["`echo $CXXFLAGS' ' | $SED -e 's/-g[0-9] //g' -e 's/-g //g'`"]
    SVN_CXXFLAGS_ADD_IFELSE([-fno-inline])
    SVN_CXXFLAGS_ADD_IFELSE([-fno-omit-frame-pointer])
    SVN_CXXFLAGS_ADD_IFELSE([-g3],[],[
      SVN_CXXFLAGS_ADD_IFELSE([-g2],[],[
        SVN_CXXFLAGS_ADD_IFELSE([-g])])])
  fi
  dnl SVN_DEBUG enables specific features for developer builds
  dnl AP_DEBUG enables specific (Apache) features for developer builds
  CFLAGS="$CFLAGS -DSVN_DEBUG -DAP_DEBUG"
  CXXFLAGS="$CXXFLAGS -DSVN_DEBUG -DAP_DEBUG"
elif test "$enable_debugging" = "no" ; then
  AC_MSG_NOTICE([Disabling debugging])
  CFLAGS=["`echo $CFLAGS' ' | $SED -e 's/-g[0-9] //g' -e 's/-g //g'`"]
  CXXFLAGS=["`echo $CXXFLAGS' ' | $SED -e 's/-g[0-9] //g' -e 's/-g //g'`"]
  dnl Compile with NDEBUG to get rid of assertions
  CFLAGS="$CFLAGS -DNDEBUG"
  CXXFLAGS="$CXXFLAGS -DNDEBUG"
# elif test "$enable_debugging" = "maybe" ; then
#   # do nothing
fi

if test "$enable_optimization" = "yes"; then
  dnl Add optimization flags, unless they were set by the user
  if test -z ["`echo $CUSERFLAGS' ' | $EGREP -- '-O[^ ]* '`"]; then
    CFLAGS=["`echo $CFLAGS' ' | $SED -e 's/-O[^ ]* //g'`"]
    if test "$enable_debugging" = "yes"; then
      AC_MSG_NOTICE([Enabling optimizations for C (with debugging enabled)])
      SVN_CFLAGS_ADD_IFELSE([-O1],[],[
        SVN_CFLAGS_ADD_IFELSE([-O])])
    else
      AC_MSG_NOTICE([Enabling optimizations for C])
      SVN_CFLAGS_ADD_IFELSE([-O3],[],[
        SVN_CFLAGS_ADD_IFELSE([-O2],[],[
          SVN_CFLAGS_ADD_IFELSE([-O1],[],[
            SVN_CFLAGS_ADD_IFELSE([-O])])])])
      SVN_CFLAGS_ADD_IFELSE([-Wno-clobbered])
      SVN_CFLAGS_ADD_IFELSE([-flto])
    fi
  fi
  if test -z ["`echo $CXXUSERFLAGS' ' | $EGREP -- '-O[^ ]* '`"]; then
    CXXFLAGS=["`echo $CXXFLAGS' ' | $SED -e 's/-O[^ ]* //g'`"]
    if test "$enable_debugging" = "yes"; then
      AC_MSG_NOTICE([Enabling optimizations for C++ (with debugging enabled)])
      SVN_CXXFLAGS_ADD_IFELSE([-O1],[],[
        SVN_CXXFLAGS_ADD_IFELSE([-O])])
    else
      AC_MSG_NOTICE([Enabling optimizations for C++])
      SVN_CXXFLAGS_ADD_IFELSE([-O3],[],[
        SVN_CXXFLAGS_ADD_IFELSE([-O2],[],[
          SVN_CXXFLAGS_ADD_IFELSE([-O1],[],[
            SVN_CXXFLAGS_ADD_IFELSE([-O])])])])
      SVN_CXXFLAGS_ADD_IFELSE([-Wno-clobbered])
      SVN_CXXFLAGS_ADD_IFELSE([-flto])
    fi
  fi
elif test "$enable_optimization" = "no"; then
  dnl Remove all optimization flags
  AC_MSG_NOTICE([Disabling optimizations])
  CFLAGS=["`echo $CFLAGS' ' | $SED -e 's/-O[^ ]* //g'`"]
  CXXFLAGS=["`echo $CXXFLAGS' ' | $SED -e 's/-O[^ ]* //g'`"]
# elif test "$enable_optimization" = "maybe" ; then
#   # do nothing
fi

dnl Dump the current compiler options
AC_MSG_NOTICE([C compiler flags: $CFLAGS])
AC_MSG_NOTICE([    user-defined: $CUSERFLAGS])
AC_MSG_NOTICE([ maintainer-mode: $CMAINTAINERFLAGS])
AC_MSG_NOTICE([C++ compiler flags: $CXXFLAGS])
AC_MSG_NOTICE([      user-defined: $CXXUSERFLAGS])
AC_MSG_NOTICE([   maintainer-mode: $CXXMAINTAINERFLAGS])

AC_ARG_ENABLE(full-version-match,
AS_HELP_STRING([--disable-full-version-match],
               [Disable the full version match rules when checking
                Subversion library compatibility.]),
[
    if test "$enableval" = "no" ; then
      AC_MSG_NOTICE([Disabling svn full version matching])
      AC_DEFINE(SVN_DISABLE_FULL_VERSION_MATCH, 1,
                [Defined if the full version matching rules are disabled])
    fi
])

AC_ARG_WITH(editor,
AS_HELP_STRING([--with-editor=PATH],
               [Specify a default editor for the subversion client.]),
[

    if test "$withval" = "yes" ; then
      AC_MSG_ERROR([--with-editor requires an argument.])
    else
      SVN_CLIENT_EDITOR=$withval
      AC_DEFINE_UNQUOTED(SVN_CLIENT_EDITOR, "$SVN_CLIENT_EDITOR",
                         [The path of a default editor for the client.])

    fi

])

SVN_LIB_Z

SVN_LZ4

SVN_UTF8PROC

MOD_ACTIVATION=""
AC_ARG_ENABLE(mod-activation,
AS_HELP_STRING([--enable-mod-activation],
               [Enable mod_dav_svn in httpd.conf]),
[
    if test "$enableval" = "yes" ; then
      MOD_ACTIVATION="-a"
      AC_MSG_NOTICE([Enabling apache module activation])
    else
      AC_MSG_NOTICE([Disabling apache module activation])
    fi
])
AC_SUBST(MOD_ACTIVATION)



AC_ARG_ENABLE(gcov,
AC_HELP_STRING([--enable-gcov],
               [Turn on gcov coverage testing (GCC only).]),
[
    if test "$enableval" = "yes" ; then
      dnl Probably other compilers support something similar;
      dnl feel free to extend this to include them.
      if test "$GCC" = "yes"; then
        if test "$svn_enable_shared" = "yes" ; then
          AC_MSG_ERROR([Can't have --enable-gcov without --disable-shared (we
                        recommend also using --enable-all-static).])
        fi
        if test ! "$enable_all_static" = "yes" ; then
          AC_MSG_WARN(We recommend --enable-all-static with --enable-gcov.)
        fi
        AC_MSG_NOTICE([Enabling gcov coverage testing.])
        CFLAGS="$CFLAGS -fprofile-arcs -ftest-coverage"
        CXXFLAGS="$CXXFLAGS -fprofile-arcs -ftest-coverage"
      else
        AC_MSG_ERROR([We only support --enable-gcov with GCC right now.])
      fi
    fi
])

AC_ARG_ENABLE(gprof,
AS_HELP_STRING([--enable-gprof],
               [Produce gprof profiling data in 'gmon.out' (GCC only).]),
[
    if test "$enableval" = "yes" ; then
      dnl Probably other compilers support -pg or something similar;
      dnl feel free to extend this to include them.
      if test "$GCC" = "yes"; then
        if test "$svn_enable_shared" = "yes" ; then
          AC_MSG_ERROR([Can't have --enable-gprof without --disable-shared (we
                        recommend also using --enable-all-static).])
        fi
        if test ! "$enable_all_static" = "yes" ; then
          AC_MSG_WARN(We recommend --enable-all-static with --enable-gprof.)
        fi
        AC_MSG_NOTICE([Enabling gprof profiling data (to gmon.out).])
        CFLAGS="$CFLAGS -pg"
        CXXFLAGS="$CXXFLAGS -pg"
        LT_LDFLAGS="$LT_LDFLAGS -pg"
      else
        AC_MSG_ERROR([We only support --enable-gprof with GCC right now.])
      fi
    fi
])


# Scripting and Bindings languages

# Python: Used for testsuite, and bindings


PYTHON="`$abs_srcdir/build/find_python.sh`"
if test -z "$PYTHON"; then
  AC_MSG_WARN([Python 2.7 or later is required to run the testsuite])
  AC_MSG_WARN([or to use the Subversion Python bindings])
  AC_MSG_WARN([])
  AC_MSG_WARN([If you have a suitable Python installed, but not on the])
  AC_MSG_WARN([PATH, set the environment variable PYTHON to the full path])
  AC_MSG_WARN([to the Python executable, and re-run configure])
fi
AC_PATH_PROGS(PYTHON, "$PYTHON", none)

# The minimum version for the JVM runtime for our Java bytecode.
JAVA_OLDEST_WORKING_VER='1.6'
# SVN_CHECK_JDK sets $JAVA_CLASSPATH
SVN_CHECK_JDK($JAVA_OLDEST_WORKING_VER)

AC_PATH_PROG(PERL, perl, none)

if test -n "$RUBY"; then
  AC_PATH_PROG(RUBY, "$RUBY", none)
else
  AC_PATH_PROGS(RUBY, ruby ruby1 ruby1.8 ruby18 ruby1.9 ruby19 ruby1.9.3 ruby193 ruby2 ruby2.0 ruby20 ruby2.1 ruby21 ruby2.2 ruby22 ruby2.3 ruby23 ruby2.4 ruby24, none)
fi
if test "$RUBY" != "none"; then
  AC_MSG_CHECKING([rb_hash_foreach])
  if "$RUBY" -r mkmf -e 'exit(have_func("rb_hash_foreach") ? 0 : 1)' >/dev/null; then
    AC_MSG_RESULT([yes])
    if test -n "$RDOC"; then
      AC_PATH_PROG(RDOC, "$RDOC", none)
    else
      AC_PATH_PROGS(RUBY, rdoc rdoc1 rdoc1.8 rdoc18 rdoc1.9 rdoc19 rdoc1.9.3 rdoc193 rdoc2 rdoc2.0 rdoc20 rdoc2.1 rdoc21 rdoc2.2 rdoc22 rdoc2.3 rdoc23 rdoc2.4 rdoc24, none)
    fi
    AC_CACHE_CHECK([for Ruby major version], [svn_cv_ruby_major],[
    svn_cv_ruby_major="`$RUBY -rrbconfig -e 'print RbConfig::CONFIG.fetch(%q(MAJOR))'`"
    ])
    RUBY_MAJOR="$svn_cv_ruby_major"

    AC_CACHE_CHECK([for Ruby minor version], [svn_cv_ruby_minor],[
    svn_cv_ruby_minor="`$RUBY -rrbconfig -e 'print RbConfig::CONFIG.fetch(%q(MINOR))'`"
    ])
    RUBY_MINOR="$svn_cv_ruby_minor"

    AC_CACHE_CHECK([for Ruby teeny version], [svn_cv_ruby_teeny],[
    svn_cv_ruby_teeny="`$RUBY -rrbconfig -e 'major, minor, teeny = RUBY_VERSION.split("."); print teeny;'`"
    ])
    RUBY_TEENY="$svn_cv_ruby_teeny"
    
    AC_SUBST(RUBY_MAJOR)
    AC_SUBST(RUBY_MINOR)
    AC_SUBST(RUBY_TEENY)
    if test \( "$RUBY_MAJOR" -eq "1" -a "$RUBY_MINOR" -gt "8" -a "$RUBY_TEENY" -lt "3" \); then
      # Disallow Ruby between 1.8.7 and 1.9.3
      RUBY="none"
      AC_MSG_WARN([The detected Ruby is between 1.9 and 1.9.3])
      AC_MSG_WARN([Only 1.8.x and 1.9.3 or later are supported at this time])
    fi
  else
    AC_MSG_RESULT([no])
    RUBY="none"
    AC_MSG_WARN([The detected Ruby is too old for Subversion to use])
    AC_MSG_WARN([A Ruby which has rb_hash_foreach is required to use the])
    AC_MSG_WARN([Subversion Ruby bindings])
    AC_MSG_WARN([Upgrade to the official 1.8.2 release, or later])
  fi
fi

SVN_CHECK_SWIG
AC_ARG_VAR(SWIG_FEATURES, [SWIG feature flags common to all bindings])
AC_ARG_VAR(SWIG_RB_FEATURES, [SWIG feature flags specific to Ruby bindings])
AC_ARG_VAR(SWIG_PL_FEATURES, [SWIG feature flags specific to Perl bindings])
AC_ARG_VAR(SWIG_PY_FEATURES, [SWIG feature flags specific to Python bindings])

SVN_CHECK_CTYPESGEN

dnl decide whether we want to link against the RA/FS libraries
AC_ARG_ENABLE(runtime-module-search,
AS_HELP_STRING([--enable-runtime-module-search],
               [Turn on dynamic loading of RA/FS libraries including
                third-party FS libraries]),
[
    if test "$enableval" = "yes"; then
      use_dso=yes
      if test "$svn_enable_shared" = "no"; then
        AC_MSG_ERROR([--enable-runtime-module-search conflicts with --disable-shared])
      fi
      AC_DEFINE(SVN_USE_DSO, 1,
                [Defined if svn should try to load DSOs])

      dnl Mac OS X uses libname.MAJOR.SOVERSION.dylib
      dnl Most other unixes use libname.MAJOR.so.SOVERSION
      case "$host" in
        *-*-darwin*)
          AC_DEFINE(SVN_DSO_SUFFIX_FMT, ["%d.%d.dylib"],
                    [Shared library file name suffix format])
          ;;
        *)
          AC_DEFINE(SVN_DSO_SUFFIX_FMT, ["%d.so.%d"],
                    [Shared library file name suffix format])
          ;;
      esac
    fi
])

if test "$svn_enable_shared" = "no" || test "$use_dso" != "yes"; then
  AC_DEFINE(SVN_LIBSVN_RA_LINKS_RA_LOCAL, 1,
      [Defined if libsvn_ra should link against libsvn_ra_local])
  svn_ra_lib_deps="\$(RA_LOCAL_DEPS)"
  svn_ra_lib_install_deps="install-ramod-lib"
  svn_ra_lib_link="\$(RA_LOCAL_LINK)"

  AC_DEFINE(SVN_LIBSVN_RA_LINKS_RA_SVN, 1,
      [Defined if libsvn_ra should link against libsvn_ra_svn])
  svn_ra_lib_deps="$svn_ra_lib_deps \$(RA_SVN_DEPS)"
  svn_ra_lib_link="$svn_ra_lib_link \$(RA_SVN_LINK)"

  if test "$svn_lib_serf" = "yes"; then
    AC_DEFINE(SVN_LIBSVN_RA_LINKS_RA_SERF, 1,
          [Defined if libsvn_ra should link against libsvn_ra_serf])
    svn_ra_lib_deps="$svn_ra_lib_deps \$(RA_SERF_DEPS)"
    svn_ra_lib_install_deps="$svn_ra_lib_install_deps install-serf-lib"
    svn_ra_lib_link="$svn_ra_lib_link \$(RA_SERF_LINK)"
  fi

  SVN_RA_LIB_DEPS=$svn_ra_lib_deps
  SVN_RA_LIB_INSTALL_DEPS=$svn_ra_lib_install_deps
  SVN_RA_LIB_LINK=$svn_ra_lib_link

  AC_DEFINE(SVN_LIBSVN_FS_LINKS_FS_FS, 1,
      [Defined if libsvn_fs should link against libsvn_fs_fs])
  svn_fs_lib_deps="\$(FS_FS_DEPS)"
  svn_fs_lib_install_deps="install-fsmod-lib"
  svn_fs_lib_link="\$(FS_FS_LINK)"

  AC_DEFINE(SVN_LIBSVN_FS_LINKS_FS_X, 1,
      [Defined if libsvn_fs should link against libsvn_fs_x])
  svn_fs_lib_deps="$svn_fs_lib_deps \$(FS_X_DEPS)"
  svn_fs_lib_link="$svn_fs_lib_link \$(FS_X_LINK)"
dnl  FSFS already installs fsmod
dnl  svn_fs_lib_install_deps="$svn_fs_lib_install_deps install-fsmod-lib"

  if test "$svn_lib_berkeley_db" = "yes"; then
  AC_DEFINE(SVN_LIBSVN_FS_LINKS_FS_BASE, 1,
      [Defined if libsvn_fs should link against libsvn_fs_base])
    svn_fs_lib_deps="$svn_fs_lib_deps \$(FS_BASE_DEPS)"
    svn_fs_lib_install_deps="$svn_fs_lib_install_deps install-bdb-lib"
    svn_fs_lib_link="$svn_fs_lib_link \$(FS_BASE_LINK)"
  fi

  SVN_FS_LIB_DEPS=$svn_fs_lib_deps
  SVN_FS_LIB_INSTALL_DEPS=$svn_fs_lib_install_deps
  SVN_FS_LIB_LINK=$svn_fs_lib_link
fi

AC_SUBST(SVN_RA_LIB_DEPS)
AC_SUBST(SVN_RA_LIB_INSTALL_DEPS)
AC_SUBST(SVN_RA_LIB_LINK)
AC_SUBST(SVN_FS_LIB_DEPS)
AC_SUBST(SVN_FS_LIB_INSTALL_DEPS)
AC_SUBST(SVN_FS_LIB_LINK)

# ==== JavaHL ================================================================

dnl Possibly compile JavaHL
do_javahl_build=no
AC_ARG_ENABLE(javahl,
  AS_HELP_STRING([--enable-javahl],
             [Enable compilation of Java high-level bindings (requires C++)]),
  [ if test "$enableval" = "yes" ; then
        do_javahl_build="yes"
    fi
  ])

JAVAHL_OBJDIR=""
INSTALL_EXTRA_JAVAHL_LIB=""
FIX_JAVAHL_LIB=""
JAVAHL_TESTS_TARGET=""
JAVAHL_COMPAT_TESTS_TARGET=""
LT_CXX_LIBADD=""
if test "$do_javahl_build" = "yes"; then
  dnl Check for suitable JDK
  if test "$JDK_SUITABLE" = "no"; then
    AC_MSG_ERROR([Cannot compile JavaHL without a suitable JDK. 
                  Please specify a suitable JDK using the --with-jdk option.])
  fi
  
  dnl The temporary directory where libtool compiles libsvnjavahl.
  JAVAHL_OBJDIR='$(libsvnjavahl_PATH)/.libs'
  
  os_arch=`uname`
  if test "$os_arch" = "Darwin"; then
    dnl On Darwin, JNI libs must be installed as .jnilib
    INSTALL_EXTRA_JAVAHL_LIB='ln -sf $(libdir)/libsvnjavahl-1.dylib $(libdir)/libsvnjavahl-1.jnilib'
    FIX_JAVAHL_LIB="ln -sf libsvnjavahl-1.dylib $JAVAHL_OBJDIR/libsvnjavahl-1.jnilib"
  fi
  # This segment (and the rest of r10800) is very likely unnecessary
  # with libtool 1.5, which automatically adds libstdc++ as a
  # dependency to the C++ libraries it builds.  So at some future time
  # when autogen.sh requires libtool 1.5 or higher, we can get rid of
  # it.
  AC_MSG_CHECKING([for additional flags to link C++ libraries])
  if test "x$ac_compiler_gnu" = "xyes"; then
    case "$host" in
      *freebsd10*)
        AC_MSG_RESULT([none needed])
      ;;
      *)
        LT_CXX_LIBADD="-lstdc++"
        AC_MSG_RESULT([$LT_CXX_LIBADD])
      ;;
    esac
  else
    AC_MSG_RESULT([none needed])
  fi
fi
AC_SUBST(INSTALL_EXTRA_JAVAHL_LIB)
AC_SUBST(JAVAHL_OBJDIR)
AC_SUBST(FIX_JAVAHL_LIB)
AC_SUBST(LT_CXX_LIBADD)

AC_ARG_WITH(junit,
AS_HELP_STRING([--with-junit=PATH],
               [Specify a path to the junit JAR file.]),
[
    if test "$withval" != "no"; then
      if test -n "$JAVA_CLASSPATH"; then
        JAVA_CLASSPATH="$withval:$JAVA_CLASSPATH"
      else
        JAVA_CLASSPATH="$withval"
      fi
      JAVAHL_TESTS_TARGET="javahl-tests"
      JAVAHL_COMPAT_TESTS_TARGET="javahl-compat-tests"
    fi
])
AC_SUBST(JAVA_CLASSPATH)
AC_SUBST(JAVAHL_TESTS_TARGET)
AC_SUBST(JAVAHL_COMPAT_TESTS_TARGET)

# ==== Miscellaneous bits ====================================================

AC_CHECK_HEADERS([stdbool.h stdint.h])

# Strip '-no-cpp-precomp' from CPPFLAGS for the clang compiler
### I think we get this flag from APR, so the fix probably belongs there
if test "$CC" = "clang"; then
  SVN_STRIP_FLAG(CPPFLAGS, [-no-cpp-precomp ])
fi

dnl Since this is used only on Unix-y systems, define the path separator as '/'
AC_DEFINE_UNQUOTED(SVN_PATH_LOCAL_SEPARATOR, '/',
        [Defined to be the path separator used on your local filesystem])

AC_DEFINE_UNQUOTED(SVN_NULL_DEVICE_NAME, "/dev/null",
        [Defined to be the null device for the system])

DEFAULT_FS_TYPE="fsfs"
AC_DEFINE_UNQUOTED(DEFAULT_FS_TYPE, "$DEFAULT_FS_TYPE",
                   [The fs type to use by default])

DEFAULT_HTTP_LIBRARY="serf"
AC_DEFINE_UNQUOTED(DEFAULT_HTTP_LIBRARY, "$DEFAULT_HTTP_LIBRARY",
                   [The http library to use by default])

# BSD/OS (BSDi) needs to use a different include syntax in Makefile
INCLUDE_OUTPUTS="include \$(top_srcdir)/build-outputs.mk"
case "$host" in
  *bsdi*)
    # Check whether they've installed GNU make
    if ! make --version > /dev/null 2>&1; then 
      # BSDi make
      INCLUDE_OUTPUTS=".include \"\$(top_srcdir)/build-outputs.mk\""
    fi
    ;;
esac
AC_SUBST(INCLUDE_OUTPUTS)

# ==== Detection complete - output and run config.status =====================

AC_CONFIG_HEADERS(subversion/svn_private_config.h.tmp:subversion/svn_private_config.h.in)
AC_CONFIG_COMMANDS([svn_private_config.h.tmp],
                   [svn_cf=subversion/svn_private_config.h;
                    $SED -e "s/@SVN_DB_HEADER@/$SVN_DB_HEADER/" $svn_cf.tmp > $svn_cf.tmp.new
                    cmp -s $svn_cf.tmp.new $svn_cf || mv -f $svn_cf.tmp.new $svn_cf
                    rm -f $svn_cf.tmp.new $svn_cf.tmp],
                   [SED="$SED"
                    SVN_DB_HEADER="$SVN_DB_HEADER"])
AC_CONFIG_FILES([Makefile])

# Create pkg-config .pc files from .pc.in files
for pc_in_file in "${abs_srcdir}"/subversion/libsvn_*/*.pc.in; do
  pc_file=${pc_in_file#${abs_srcdir}/}
  pc_file=${pc_file%.in}
  AC_CONFIG_FILES([${pc_file}])
done

SVN_CONFIG_SCRIPT(tools/backup/hot-backup.py)
SVN_CONFIG_SCRIPT(tools/hook-scripts/commit-access-control.pl)
SVN_CONFIG_SCRIPT(subversion/bindings/swig/perl/native/Makefile.PL)
if test -e packages/solaris/pkginfo.in; then
  SVN_CONFIG_SCRIPT(packages/solaris/pkginfo)
fi
AC_SUBST(SVN_CONFIG_SCRIPT_FILES)

# Ensure that SWIG is checked after reconfiguration.
rm -f .swig_checked

dnl Provide ${host} for use in compiled code (for svn --version)
AC_DEFINE_UNQUOTED([SVN_BUILD_HOST], "${host}",
                   [Defined to the config.guess name of the build system])

dnl Provide ${target} for use in compiled code (for user-agent string)
AC_DEFINE_UNQUOTED([SVN_BUILD_TARGET], "${target}",
                   [Defined to the config.guess name of the build target])

AC_OUTPUT

# ==== Print final messages to user ==========================================

dnl Configure is long - users tend to miss warnings printed during it.
dnl Hence, print a warnings about what we did and didn't configure at the 
dnl end, where people will actually see them.

if test "$svn_have_berkeley_db" = "no6" && test "$enable_bdb6" != "no"; then
  AC_MSG_WARN([We have configured without BDB filesystem support


Berkeley DB 6 was found, but not used.  Please re-run configure (see
./config.nice) with the '--enable-bdb6' flag to use it,
or explicitly specify '--disable-bdb6' or '--without-berkeley-db'
to silence this warning.

Please note that some versions of Berkeley DB 6+ are under the GNU Affero
General Public License, version 3:
https://oss.oracle.com/pipermail/bdb/2013-June/000056.html

The AGPL-3.0 licence may impose special requirements for making available
source code of server-side software.  The text of the licence is:
https://www.gnu.org/licenses/agpl-3.0.html
http://opensource.org/licenses/AGPL-3.0

The Berkeley DB backend to Subversion is deprecated; see
http://subversion.apache.org/docs/release-notes/1.8#bdb-deprecated

The Subversion developers have not tested Subversion with Berkeley DB 6 for
technical problems or bugs.
])
fi
