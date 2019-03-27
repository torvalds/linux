dnl ######################################################################
dnl NTP_VPATH_HACK
dnl
dnl Compiling ntpd doesn't require YACC or Bison unless ntp_parser.y is
dnl modified, because the output ntp_parser.[ch] are committed.  This
dnl raises an issue with Automake-generated Makefiles on non-GNU make
dnl used from a build tree outside the source tree.
dnl
dnl With GNU make, ylwrap updates $srcdir/ntp_parser.[ch] directly.
dnl Under Sun or BSD make, ylwrap needs those files to be in the build
dnl tree.
dnl
dnl With VPATH_HACK enabled, ntpd/Makefile.am includes code to symlink
dnl from ntp_parser.[ch] in the build tree to the corresponding files
dnl in $srcdir, and to check for ylwrap replacing the .h with a normal
dnl file, and in that case copy the updated .h to $srcdir and restore
dnl the symlink.
dnl
dnl if we are building outside the srcdir and either
dnl   force_ntp_vpath_hack is yes
dnl     or
dnl   we're not using GNU make
dnl then we want VPATH_HACK to be true for .am tests
dnl
AC_DEFUN([NTP_VPATH_HACK], [
AC_MSG_CHECKING([to see if we need ylwrap VPATH hack])
ntp_vpath_hack="no"
case "$srcdir::${force_ntp_vpath_hack-no}" in
 .::*)
    ;; # VPATH_HACK path is for VPATH builds only.
 *::yes)
    ntp_vpath_hack="yes"
    ;;
 *::*)
    case "`${MAKE-make} -v -f /dev/null 2>/dev/null | grep 'GNU Make'`" in
     '')
	ntp_vpath_hack="yes"
    esac
esac
AC_MSG_RESULT([$ntp_vpath_hack])
AM_CONDITIONAL([VPATH_HACK], [test x$ntp_vpath_hack = xyes])
])
dnl ======================================================================
