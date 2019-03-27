m4_include(../googletest/m4/acx_pthread.m4)

AC_INIT([Google C++ Mocking Framework],
        [1.8.0],
        [googlemock@googlegroups.com],
        [gmock])

# Provide various options to initialize the Autoconf and configure processes.
AC_PREREQ([2.59])
AC_CONFIG_SRCDIR([./LICENSE])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([build-aux/config.h])
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([scripts/gmock-config], [chmod +x scripts/gmock-config])

# Initialize Automake with various options. We require at least v1.9, prevent
# pedantic complaints about package files, and enable various distribution
# targets.
AM_INIT_AUTOMAKE([1.9 dist-bzip2 dist-zip foreign subdir-objects])

# Check for programs used in building Google Test.
AC_PROG_CC
AC_PROG_CXX
AC_LANG([C++])
AC_PROG_LIBTOOL

# TODO(chandlerc@google.com): Currently we aren't running the Python tests
# against the interpreter detected by AM_PATH_PYTHON, and so we condition
# HAVE_PYTHON by requiring "python" to be in the PATH, and that interpreter's
# version to be >= 2.3. This will allow the scripts to use a "/usr/bin/env"
# hashbang.
PYTHON=  # We *do not* allow the user to specify a python interpreter
AC_PATH_PROG([PYTHON],[python],[:])
AS_IF([test "$PYTHON" != ":"],
      [AM_PYTHON_CHECK_VERSION([$PYTHON],[2.3],[:],[PYTHON=":"])])
AM_CONDITIONAL([HAVE_PYTHON],[test "$PYTHON" != ":"])

# TODO(chandlerc@google.com) Check for the necessary system headers.

# Configure pthreads.
AC_ARG_WITH([pthreads],
            [AS_HELP_STRING([--with-pthreads],
               [use pthreads (default is yes)])],
            [with_pthreads=$withval],
            [with_pthreads=check])

have_pthreads=no
AS_IF([test "x$with_pthreads" != "xno"],
      [ACX_PTHREAD(
        [],
        [AS_IF([test "x$with_pthreads" != "xcheck"],
               [AC_MSG_FAILURE(
                 [--with-pthreads was specified, but unable to be used])])])
       have_pthreads="$acx_pthread_ok"])
AM_CONDITIONAL([HAVE_PTHREADS],[test "x$have_pthreads" == "xyes"])
AC_SUBST(PTHREAD_CFLAGS)
AC_SUBST(PTHREAD_LIBS)

# GoogleMock currently has hard dependencies upon GoogleTest above and beyond
# running its own test suite, so we both provide our own version in
# a subdirectory and provide some logic to use a custom version or a system
# installed version.
AC_ARG_WITH([gtest],
            [AS_HELP_STRING([--with-gtest],
                            [Specifies how to find the gtest package. If no
                            arguments are given, the default behavior, a
                            system installed gtest will be used if present,
                            and an internal version built otherwise. If a
                            path is provided, the gtest built or installed at
                            that prefix will be used.])],
            [],
            [with_gtest=yes])
AC_ARG_ENABLE([external-gtest],
              [AS_HELP_STRING([--disable-external-gtest],
                              [Disables any detection or use of a system
                              installed or user provided gtest. Any option to
                              '--with-gtest' is ignored. (Default is enabled.)])
              ], [], [enable_external_gtest=yes])
AS_IF([test "x$with_gtest" == "xno"],
      [AC_MSG_ERROR([dnl
Support for GoogleTest was explicitly disabled. Currently GoogleMock has a hard
dependency upon GoogleTest to build, please provide a version, or allow
GoogleMock to use any installed version and fall back upon its internal
version.])])

# Setup various GTEST variables. TODO(chandlerc@google.com): When these are
# used below, they should be used such that any pre-existing values always
# trump values we set them to, so that they can be used to selectively override
# details of the detection process.
AC_ARG_VAR([GTEST_CONFIG],
           [The exact path of Google Test's 'gtest-config' script.])
AC_ARG_VAR([GTEST_CPPFLAGS],
           [C-like preprocessor flags for Google Test.])
AC_ARG_VAR([GTEST_CXXFLAGS],
           [C++ compile flags for Google Test.])
AC_ARG_VAR([GTEST_LDFLAGS],
           [Linker path and option flags for Google Test.])
AC_ARG_VAR([GTEST_LIBS],
           [Library linking flags for Google Test.])
AC_ARG_VAR([GTEST_VERSION],
           [The version of Google Test available.])
HAVE_BUILT_GTEST="no"

GTEST_MIN_VERSION="1.8.0"

AS_IF([test "x${enable_external_gtest}" = "xyes"],
      [# Begin filling in variables as we are able.
      AS_IF([test "x${with_gtest}" != "xyes"],
            [AS_IF([test -x "${with_gtest}/scripts/gtest-config"],
                   [GTEST_CONFIG="${with_gtest}/scripts/gtest-config"],
                   [GTEST_CONFIG="${with_gtest}/bin/gtest-config"])
            AS_IF([test -x "${GTEST_CONFIG}"], [],
                  [AC_MSG_ERROR([dnl
Unable to locate either a built or installed Google Test at '${with_gtest}'.])
                  ])])

      AS_IF([test -x "${GTEST_CONFIG}"], [],
            [AC_PATH_PROG([GTEST_CONFIG], [gtest-config])])
      AS_IF([test -x "${GTEST_CONFIG}"],
            [AC_MSG_CHECKING([for Google Test version >= ${GTEST_MIN_VERSION}])
            AS_IF([${GTEST_CONFIG} --min-version=${GTEST_MIN_VERSION}],
                  [AC_MSG_RESULT([yes])
                  HAVE_BUILT_GTEST="yes"],
                  [AC_MSG_RESULT([no])])])])

AS_IF([test "x${HAVE_BUILT_GTEST}" = "xyes"],
      [GTEST_CPPFLAGS=`${GTEST_CONFIG} --cppflags`
      GTEST_CXXFLAGS=`${GTEST_CONFIG} --cxxflags`
      GTEST_LDFLAGS=`${GTEST_CONFIG} --ldflags`
      GTEST_LIBS=`${GTEST_CONFIG} --libs`
      GTEST_VERSION=`${GTEST_CONFIG} --version`],
      [
      # GTEST_CONFIG needs to be executable both in a Makefile environment and
      # in a shell script environment, so resolve an absolute path for it here.
      GTEST_CONFIG="`pwd -P`/../googletest/scripts/gtest-config"
      GTEST_CPPFLAGS='-I$(top_srcdir)/../googletest/include'
      GTEST_CXXFLAGS='-g'
      GTEST_LDFLAGS=''
      GTEST_LIBS='$(top_builddir)/../googletest/lib/libgtest.la'
      GTEST_VERSION="${GTEST_MIN_VERSION}"])

# TODO(chandlerc@google.com) Check the types, structures, and other compiler
# and architecture characteristics.

# Output the generated files. No further autoconf macros may be used.
AC_OUTPUT
