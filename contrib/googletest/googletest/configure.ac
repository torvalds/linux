m4_include(m4/acx_pthread.m4)

# At this point, the Xcode project assumes the version string will be three
# integers separated by periods and surrounded by square brackets (e.g.
# "[1.0.1]"). It also asumes that there won't be any closing parenthesis
# between "AC_INIT(" and the closing ")" including comments and strings.
AC_INIT([Google C++ Testing Framework],
        [1.8.0],
        [googletestframework@googlegroups.com],
        [gtest])

# Provide various options to initialize the Autoconf and configure processes.
AC_PREREQ([2.59])
AC_CONFIG_SRCDIR([./LICENSE])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([build-aux/config.h])
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([scripts/gtest-config], [chmod +x scripts/gtest-config])

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
AM_CONDITIONAL([HAVE_PTHREADS],[test "x$have_pthreads" = "xyes"])
AC_SUBST(PTHREAD_CFLAGS)
AC_SUBST(PTHREAD_LIBS)

# TODO(chandlerc@google.com) Check for the necessary system headers.

# TODO(chandlerc@google.com) Check the types, structures, and other compiler
# and architecture characteristics.

# Output the generated files. No further autoconf macros may be used.
AC_OUTPUT
