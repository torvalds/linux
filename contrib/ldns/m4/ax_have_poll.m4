# ===========================================================================
#       http://www.gnu.org/software/autoconf-archive/ax_have_poll.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_HAVE_POLL([ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
#   AX_HAVE_PPOLL([ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
#
# DESCRIPTION
#
#   This macro determines whether the system supports the poll I/O event
#   interface. A neat usage example would be:
#
#     AX_HAVE_POLL(
#       [AX_CONFIG_FEATURE_ENABLE(poll)],
#       [AX_CONFIG_FEATURE_DISABLE(poll)])
#     AX_CONFIG_FEATURE(
#       [poll], [This platform supports poll(7)],
#       [HAVE_POLL], [This platform supports poll(7).])
#
#   Some systems -- most notably Linux kernel 2.6.16 and later -- also have
#   the variant ppoll(). The availability of that function can be tested
#   with the second macro. Generally speaking, it is safe to assume that
#   AX_HAVE_POLL would succeed if AX_HAVE_PPOLL has, but not the other way
#   round.
#
# LICENSE
#
#   Copyright (c) 2009 Peter Simons <simons@cryp.to>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 7

AC_DEFUN([AX_HAVE_POLL], [dnl
  AC_MSG_CHECKING([for poll(2)])
  AC_CACHE_VAL([ax_cv_have_poll], [dnl
    AC_LINK_IFELSE([dnl
      AC_LANG_PROGRAM(
        [#include <poll.h>],
        [int rc; rc = poll((struct pollfd *)(0), 0, 0);])],
      [ax_cv_have_poll=yes],
      [ax_cv_have_poll=no])])
  AS_IF([test "${ax_cv_have_poll}" = "yes"],
    [AC_MSG_RESULT([yes])
$1],[AC_MSG_RESULT([no])
$2])
])dnl

AC_DEFUN([AX_HAVE_PPOLL], [dnl
  AC_MSG_CHECKING([for ppoll(2)])
  AC_CACHE_VAL([ax_cv_have_ppoll], [dnl
    AC_LINK_IFELSE([dnl
      AC_LANG_PROGRAM(
        [dnl
#include <poll.h>
#include <signal.h>],
        [dnl
int rc;
rc = poll((struct pollfd *)(0), 0, 0);
rc = ppoll((struct pollfd *)(0), 0, (struct timespec const *)(0), (sigset_t const *)(0));])],
      [ax_cv_have_ppoll=yes],
      [ax_cv_have_ppoll=no])])
  AS_IF([test "${ax_cv_have_ppoll}" = "yes"],
    [AC_MSG_RESULT([yes])
$1],[AC_MSG_RESULT([no])
$2])
])
