AC_PREREQ(2.12)

AC_DEFUN(CL_PROG_INSTALL,
[dnl This is mostly copied from AC_PROG_INSTALL.
# Find a good install program.  We prefer a C program (faster),
# so one script is as good as another.  But avoid the broken or
# incompatible versions:
# SysV /etc/install, /usr/sbin/install
# SunOS /usr/etc/install
# IRIX /sbin/install
# AIX /bin/install
# AFS /usr/afsws/bin/install, which mishandles nonexistent args
# SVR4 /usr/ucb/install, which tries to use the nonexistent group "staff"
# ./install, which can be erroneously created by make from ./install.sh.
AC_MSG_CHECKING(for a BSD compatible install)
if test -z "$INSTALL"; then
AC_CACHE_VAL(cl_cv_path_install,
[  IFS="${IFS= 	}"; ac_save_ifs="$IFS"; IFS="${IFS}:"
  for ac_dir in $PATH; do
    # Account for people who put trailing slashes in PATH elements.
    case "$ac_dir/" in
    /|./|.//|/etc/*|/usr/sbin/*|/usr/etc/*|/sbin/*|/usr/afsws/bin/*|/usr/ucb/*) ;;
    *)
      # OSF1 and SCO ODT 3.0 have their own names for install.
      for ac_prog in ginstall installbsd scoinst install; do
        if test -f $ac_dir/$ac_prog; then
	  if test $ac_prog = install &&
            grep dspmsg $ac_dir/$ac_prog >/dev/null 2>&1; then
	    # AIX install.  It has an incompatible calling convention.
	    # OSF/1 installbsd also uses dspmsg, but is usable.
	    :
	  else
	    if test $ac_prog = installbsd &&
	      grep src/bos $ac_dir/$ac_prog >/dev/null 2>&1; then
	      # AIX installbsd doesn't work without option "-g".
	      :
	    else
	      cl_cv_path_install="$ac_dir/$ac_prog -c"
	      break 2
	    fi
	  fi
	fi
      done
      ;;
    esac
  done
  IFS="$ac_save_ifs"
  # As a last resort, use cp.
  test -z "$cl_cv_path_install" && cl_cv_path_install="cp"
])dnl
  INSTALL="$cl_cv_path_install"
fi
dnl We do special magic for INSTALL instead of AC_SUBST, to get
dnl relative paths right. 
AC_MSG_RESULT($INSTALL)
AC_SUBST(INSTALL)dnl
# Use test -z because SunOS4 sh mishandles braces in ${var-val}.
# It thinks the first close brace ends the variable substitution.
test -z "$INSTALL_PROGRAM" && INSTALL_PROGRAM='$(INSTALL)'
AC_SUBST(INSTALL_PROGRAM)dnl
if test -z "$INSTALL_DATA"; then
  case "$INSTALL" in
    cp | */cp ) INSTALL_DATA='$(INSTALL)' ;;
    * )         INSTALL_DATA='$(INSTALL) -m 644' ;;
  esac
fi
AC_SUBST(INSTALL_DATA)dnl
])

