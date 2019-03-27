# tcl.m4 --
#
#	This file provides a set of autoconf macros to help TEA-enable
#	a Tcl extension.
#
# Copyright (c) 1999-2000 Ajuba Solutions.
# Copyright (c) 2002-2005 ActiveState Corporation.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

AC_PREREQ(2.57)

dnl TEA extensions pass us the version of TEA they think they
dnl are compatible with (must be set in TEA_INIT below)
dnl TEA_VERSION="3.9"

# Possible values for key variables defined:
#
# TEA_WINDOWINGSYSTEM - win32 aqua x11 (mirrors 'tk windowingsystem')
# TEA_PLATFORM        - windows unix
#

#------------------------------------------------------------------------
# TEA_PATH_TCLCONFIG --
#
#	Locate the tclConfig.sh file and perform a sanity check on
#	the Tcl compile flags
#
# Arguments:
#	none
#
# Results:
#
#	Adds the following arguments to configure:
#		--with-tcl=...
#
#	Defines the following vars:
#		TCL_BIN_DIR	Full path to the directory containing
#				the tclConfig.sh file
#------------------------------------------------------------------------

AC_DEFUN([TEA_PATH_TCLCONFIG], [
    dnl TEA specific: Make sure we are initialized
    AC_REQUIRE([TEA_INIT])
    #
    # Ok, lets find the tcl configuration
    # First, look for one uninstalled.
    # the alternative search directory is invoked by --with-tcl
    #

    if test x"${no_tcl}" = x ; then
	# we reset no_tcl in case something fails here
	no_tcl=true
	AC_ARG_WITH(tcl,
	    AC_HELP_STRING([--with-tcl],
		[directory containing tcl configuration (tclConfig.sh)]),
	    with_tclconfig="${withval}")
	AC_MSG_CHECKING([for Tcl configuration])
	AC_CACHE_VAL(ac_cv_c_tclconfig,[

	    # First check to see if --with-tcl was specified.
	    if test x"${with_tclconfig}" != x ; then
		case "${with_tclconfig}" in
		    */tclConfig.sh )
			if test -f "${with_tclconfig}"; then
			    AC_MSG_WARN([--with-tcl argument should refer to directory containing tclConfig.sh, not to tclConfig.sh itself])
			    with_tclconfig="`echo "${with_tclconfig}" | sed 's!/tclConfig\.sh$!!'`"
			fi ;;
		esac
		if test -f "${with_tclconfig}/tclConfig.sh" ; then
		    ac_cv_c_tclconfig="`(cd "${with_tclconfig}"; pwd)`"
		else
		    AC_MSG_ERROR([${with_tclconfig} directory doesn't contain tclConfig.sh])
		fi
	    fi

	    # then check for a private Tcl installation
	    if test x"${ac_cv_c_tclconfig}" = x ; then
		for i in \
			../tcl \
			`ls -dr ../tcl[[8-9]].[[0-9]].[[0-9]]* 2>/dev/null` \
			`ls -dr ../tcl[[8-9]].[[0-9]] 2>/dev/null` \
			`ls -dr ../tcl[[8-9]].[[0-9]]* 2>/dev/null` \
			../../tcl \
			`ls -dr ../../tcl[[8-9]].[[0-9]].[[0-9]]* 2>/dev/null` \
			`ls -dr ../../tcl[[8-9]].[[0-9]] 2>/dev/null` \
			`ls -dr ../../tcl[[8-9]].[[0-9]]* 2>/dev/null` \
			../../../tcl \
			`ls -dr ../../../tcl[[8-9]].[[0-9]].[[0-9]]* 2>/dev/null` \
			`ls -dr ../../../tcl[[8-9]].[[0-9]] 2>/dev/null` \
			`ls -dr ../../../tcl[[8-9]].[[0-9]]* 2>/dev/null` ; do
		    if test "${TEA_PLATFORM}" = "windows" \
			    -a -f "$i/win/tclConfig.sh" ; then
			ac_cv_c_tclconfig="`(cd $i/win; pwd)`"
			break
		    fi
		    if test -f "$i/unix/tclConfig.sh" ; then
			ac_cv_c_tclconfig="`(cd $i/unix; pwd)`"
			break
		    fi
		done
	    fi

	    # on Darwin, check in Framework installation locations
	    if test "`uname -s`" = "Darwin" -a x"${ac_cv_c_tclconfig}" = x ; then
		for i in `ls -d ~/Library/Frameworks 2>/dev/null` \
			`ls -d /Library/Frameworks 2>/dev/null` \
			`ls -d /Network/Library/Frameworks 2>/dev/null` \
			`ls -d /System/Library/Frameworks 2>/dev/null` \
			; do
		    if test -f "$i/Tcl.framework/tclConfig.sh" ; then
			ac_cv_c_tclconfig="`(cd $i/Tcl.framework; pwd)`"
			break
		    fi
		done
	    fi

	    # TEA specific: on Windows, check in common installation locations
	    if test "${TEA_PLATFORM}" = "windows" \
		-a x"${ac_cv_c_tclconfig}" = x ; then
		for i in `ls -d C:/Tcl/lib 2>/dev/null` \
			`ls -d C:/Progra~1/Tcl/lib 2>/dev/null` \
			; do
		    if test -f "$i/tclConfig.sh" ; then
			ac_cv_c_tclconfig="`(cd $i; pwd)`"
			break
		    fi
		done
	    fi

	    # check in a few common install locations
	    if test x"${ac_cv_c_tclconfig}" = x ; then
		for i in `ls -d ${libdir} 2>/dev/null` \
			`ls -d ${exec_prefix}/lib 2>/dev/null` \
			`ls -d ${prefix}/lib 2>/dev/null` \
			`ls -d /usr/local/lib 2>/dev/null` \
			`ls -d /usr/contrib/lib 2>/dev/null` \
			`ls -d /usr/lib 2>/dev/null` \
			`ls -d /usr/lib64 2>/dev/null` \
			`ls -d /usr/lib/tcl8.6 2>/dev/null` \
			`ls -d /usr/lib/tcl8.5 2>/dev/null` \
			; do
		    if test -f "$i/tclConfig.sh" ; then
			ac_cv_c_tclconfig="`(cd $i; pwd)`"
			break
		    fi
		done
	    fi

	    # check in a few other private locations
	    if test x"${ac_cv_c_tclconfig}" = x ; then
		for i in \
			${srcdir}/../tcl \
			`ls -dr ${srcdir}/../tcl[[8-9]].[[0-9]].[[0-9]]* 2>/dev/null` \
			`ls -dr ${srcdir}/../tcl[[8-9]].[[0-9]] 2>/dev/null` \
			`ls -dr ${srcdir}/../tcl[[8-9]].[[0-9]]* 2>/dev/null` ; do
		    if test "${TEA_PLATFORM}" = "windows" \
			    -a -f "$i/win/tclConfig.sh" ; then
			ac_cv_c_tclconfig="`(cd $i/win; pwd)`"
			break
		    fi
		    if test -f "$i/unix/tclConfig.sh" ; then
			ac_cv_c_tclconfig="`(cd $i/unix; pwd)`"
			break
		    fi
		done
	    fi
	])

	if test x"${ac_cv_c_tclconfig}" = x ; then
	    TCL_BIN_DIR="# no Tcl configs found"
	    AC_MSG_ERROR([Can't find Tcl configuration definitions. Use --with-tcl to specify a directory containing tclConfig.sh])
	else
	    no_tcl=
	    TCL_BIN_DIR="${ac_cv_c_tclconfig}"
	    AC_MSG_RESULT([found ${TCL_BIN_DIR}/tclConfig.sh])
	fi
    fi
])

#------------------------------------------------------------------------
# TEA_PATH_TKCONFIG --
#
#	Locate the tkConfig.sh file
#
# Arguments:
#	none
#
# Results:
#
#	Adds the following arguments to configure:
#		--with-tk=...
#
#	Defines the following vars:
#		TK_BIN_DIR	Full path to the directory containing
#				the tkConfig.sh file
#------------------------------------------------------------------------

AC_DEFUN([TEA_PATH_TKCONFIG], [
    #
    # Ok, lets find the tk configuration
    # First, look for one uninstalled.
    # the alternative search directory is invoked by --with-tk
    #

    if test x"${no_tk}" = x ; then
	# we reset no_tk in case something fails here
	no_tk=true
	AC_ARG_WITH(tk,
	    AC_HELP_STRING([--with-tk],
		[directory containing tk configuration (tkConfig.sh)]),
	    with_tkconfig="${withval}")
	AC_MSG_CHECKING([for Tk configuration])
	AC_CACHE_VAL(ac_cv_c_tkconfig,[

	    # First check to see if --with-tkconfig was specified.
	    if test x"${with_tkconfig}" != x ; then
		case "${with_tkconfig}" in
		    */tkConfig.sh )
			if test -f "${with_tkconfig}"; then
			    AC_MSG_WARN([--with-tk argument should refer to directory containing tkConfig.sh, not to tkConfig.sh itself])
			    with_tkconfig="`echo "${with_tkconfig}" | sed 's!/tkConfig\.sh$!!'`"
			fi ;;
		esac
		if test -f "${with_tkconfig}/tkConfig.sh" ; then
		    ac_cv_c_tkconfig="`(cd "${with_tkconfig}"; pwd)`"
		else
		    AC_MSG_ERROR([${with_tkconfig} directory doesn't contain tkConfig.sh])
		fi
	    fi

	    # then check for a private Tk library
	    if test x"${ac_cv_c_tkconfig}" = x ; then
		for i in \
			../tk \
			`ls -dr ../tk[[8-9]].[[0-9]].[[0-9]]* 2>/dev/null` \
			`ls -dr ../tk[[8-9]].[[0-9]] 2>/dev/null` \
			`ls -dr ../tk[[8-9]].[[0-9]]* 2>/dev/null` \
			../../tk \
			`ls -dr ../../tk[[8-9]].[[0-9]].[[0-9]]* 2>/dev/null` \
			`ls -dr ../../tk[[8-9]].[[0-9]] 2>/dev/null` \
			`ls -dr ../../tk[[8-9]].[[0-9]]* 2>/dev/null` \
			../../../tk \
			`ls -dr ../../../tk[[8-9]].[[0-9]].[[0-9]]* 2>/dev/null` \
			`ls -dr ../../../tk[[8-9]].[[0-9]] 2>/dev/null` \
			`ls -dr ../../../tk[[8-9]].[[0-9]]* 2>/dev/null` ; do
		    if test "${TEA_PLATFORM}" = "windows" \
			    -a -f "$i/win/tkConfig.sh" ; then
			ac_cv_c_tkconfig="`(cd $i/win; pwd)`"
			break
		    fi
		    if test -f "$i/unix/tkConfig.sh" ; then
			ac_cv_c_tkconfig="`(cd $i/unix; pwd)`"
			break
		    fi
		done
	    fi

	    # on Darwin, check in Framework installation locations
	    if test "`uname -s`" = "Darwin" -a x"${ac_cv_c_tkconfig}" = x ; then
		for i in `ls -d ~/Library/Frameworks 2>/dev/null` \
			`ls -d /Library/Frameworks 2>/dev/null` \
			`ls -d /Network/Library/Frameworks 2>/dev/null` \
			`ls -d /System/Library/Frameworks 2>/dev/null` \
			; do
		    if test -f "$i/Tk.framework/tkConfig.sh" ; then
			ac_cv_c_tkconfig="`(cd $i/Tk.framework; pwd)`"
			break
		    fi
		done
	    fi

	    # check in a few common install locations
	    if test x"${ac_cv_c_tkconfig}" = x ; then
		for i in `ls -d ${libdir} 2>/dev/null` \
			`ls -d ${exec_prefix}/lib 2>/dev/null` \
			`ls -d ${prefix}/lib 2>/dev/null` \
			`ls -d /usr/local/lib 2>/dev/null` \
			`ls -d /usr/contrib/lib 2>/dev/null` \
			`ls -d /usr/lib 2>/dev/null` \
			`ls -d /usr/lib64 2>/dev/null` \
			; do
		    if test -f "$i/tkConfig.sh" ; then
			ac_cv_c_tkconfig="`(cd $i; pwd)`"
			break
		    fi
		done
	    fi

	    # TEA specific: on Windows, check in common installation locations
	    if test "${TEA_PLATFORM}" = "windows" \
		-a x"${ac_cv_c_tkconfig}" = x ; then
		for i in `ls -d C:/Tcl/lib 2>/dev/null` \
			`ls -d C:/Progra~1/Tcl/lib 2>/dev/null` \
			; do
		    if test -f "$i/tkConfig.sh" ; then
			ac_cv_c_tkconfig="`(cd $i; pwd)`"
			break
		    fi
		done
	    fi

	    # check in a few other private locations
	    if test x"${ac_cv_c_tkconfig}" = x ; then
		for i in \
			${srcdir}/../tk \
			`ls -dr ${srcdir}/../tk[[8-9]].[[0-9]].[[0-9]]* 2>/dev/null` \
			`ls -dr ${srcdir}/../tk[[8-9]].[[0-9]] 2>/dev/null` \
			`ls -dr ${srcdir}/../tk[[8-9]].[[0-9]]* 2>/dev/null` ; do
		    if test "${TEA_PLATFORM}" = "windows" \
			    -a -f "$i/win/tkConfig.sh" ; then
			ac_cv_c_tkconfig="`(cd $i/win; pwd)`"
			break
		    fi
		    if test -f "$i/unix/tkConfig.sh" ; then
			ac_cv_c_tkconfig="`(cd $i/unix; pwd)`"
			break
		    fi
		done
	    fi
	])

	if test x"${ac_cv_c_tkconfig}" = x ; then
	    TK_BIN_DIR="# no Tk configs found"
	    AC_MSG_ERROR([Can't find Tk configuration definitions. Use --with-tk to specify a directory containing tkConfig.sh])
	else
	    no_tk=
	    TK_BIN_DIR="${ac_cv_c_tkconfig}"
	    AC_MSG_RESULT([found ${TK_BIN_DIR}/tkConfig.sh])
	fi
    fi
])

#------------------------------------------------------------------------
# TEA_LOAD_TCLCONFIG --
#
#	Load the tclConfig.sh file
#
# Arguments:
#
#	Requires the following vars to be set:
#		TCL_BIN_DIR
#
# Results:
#
#	Substitutes the following vars:
#		TCL_BIN_DIR
#		TCL_SRC_DIR
#		TCL_LIB_FILE
#------------------------------------------------------------------------

AC_DEFUN([TEA_LOAD_TCLCONFIG], [
    AC_MSG_CHECKING([for existence of ${TCL_BIN_DIR}/tclConfig.sh])

    if test -f "${TCL_BIN_DIR}/tclConfig.sh" ; then
        AC_MSG_RESULT([loading])
	. "${TCL_BIN_DIR}/tclConfig.sh"
    else
        AC_MSG_RESULT([could not find ${TCL_BIN_DIR}/tclConfig.sh])
    fi

    # eval is required to do the TCL_DBGX substitution
    eval "TCL_LIB_FILE=\"${TCL_LIB_FILE}\""
    eval "TCL_STUB_LIB_FILE=\"${TCL_STUB_LIB_FILE}\""

    # If the TCL_BIN_DIR is the build directory (not the install directory),
    # then set the common variable name to the value of the build variables.
    # For example, the variable TCL_LIB_SPEC will be set to the value
    # of TCL_BUILD_LIB_SPEC. An extension should make use of TCL_LIB_SPEC
    # instead of TCL_BUILD_LIB_SPEC since it will work with both an
    # installed and uninstalled version of Tcl.
    if test -f "${TCL_BIN_DIR}/Makefile" ; then
        TCL_LIB_SPEC="${TCL_BUILD_LIB_SPEC}"
        TCL_STUB_LIB_SPEC="${TCL_BUILD_STUB_LIB_SPEC}"
        TCL_STUB_LIB_PATH="${TCL_BUILD_STUB_LIB_PATH}"
    elif test "`uname -s`" = "Darwin"; then
	# If Tcl was built as a framework, attempt to use the libraries
	# from the framework at the given location so that linking works
	# against Tcl.framework installed in an arbitrary location.
	case ${TCL_DEFS} in
	    *TCL_FRAMEWORK*)
		if test -f "${TCL_BIN_DIR}/${TCL_LIB_FILE}"; then
		    for i in "`cd "${TCL_BIN_DIR}"; pwd`" \
			     "`cd "${TCL_BIN_DIR}"/../..; pwd`"; do
			if test "`basename "$i"`" = "${TCL_LIB_FILE}.framework"; then
			    TCL_LIB_SPEC="-F`dirname "$i" | sed -e 's/ /\\\\ /g'` -framework ${TCL_LIB_FILE}"
			    break
			fi
		    done
		fi
		if test -f "${TCL_BIN_DIR}/${TCL_STUB_LIB_FILE}"; then
		    TCL_STUB_LIB_SPEC="-L`echo "${TCL_BIN_DIR}"  | sed -e 's/ /\\\\ /g'` ${TCL_STUB_LIB_FLAG}"
		    TCL_STUB_LIB_PATH="${TCL_BIN_DIR}/${TCL_STUB_LIB_FILE}"
		fi
		;;
	esac
    fi

    # eval is required to do the TCL_DBGX substitution
    eval "TCL_LIB_FLAG=\"${TCL_LIB_FLAG}\""
    eval "TCL_LIB_SPEC=\"${TCL_LIB_SPEC}\""
    eval "TCL_STUB_LIB_FLAG=\"${TCL_STUB_LIB_FLAG}\""
    eval "TCL_STUB_LIB_SPEC=\"${TCL_STUB_LIB_SPEC}\""

    AC_SUBST(TCL_VERSION)
    AC_SUBST(TCL_PATCH_LEVEL)
    AC_SUBST(TCL_BIN_DIR)
    AC_SUBST(TCL_SRC_DIR)

    AC_SUBST(TCL_LIB_FILE)
    AC_SUBST(TCL_LIB_FLAG)
    AC_SUBST(TCL_LIB_SPEC)

    AC_SUBST(TCL_STUB_LIB_FILE)
    AC_SUBST(TCL_STUB_LIB_FLAG)
    AC_SUBST(TCL_STUB_LIB_SPEC)

    AC_MSG_CHECKING([platform])
    hold_cc=$CC; CC="$TCL_CC"
    AC_TRY_COMPILE(,[
	    #ifdef _WIN32
		#error win32
	    #endif
    ], TEA_PLATFORM="unix",
	    TEA_PLATFORM="windows"
    )
    CC=$hold_cc
    AC_MSG_RESULT($TEA_PLATFORM)

    # The BUILD_$pkg is to define the correct extern storage class
    # handling when making this package
    AC_DEFINE_UNQUOTED(BUILD_${PACKAGE_NAME}, [],
	    [Building extension source?])
    # Do this here as we have fully defined TEA_PLATFORM now
    if test "${TEA_PLATFORM}" = "windows" ; then
	EXEEXT=".exe"
	CLEANFILES="$CLEANFILES *.lib *.dll *.pdb *.exp"
    fi

    # TEA specific:
    AC_SUBST(CLEANFILES)
    AC_SUBST(TCL_LIBS)
    AC_SUBST(TCL_DEFS)
    AC_SUBST(TCL_EXTRA_CFLAGS)
    AC_SUBST(TCL_LD_FLAGS)
    AC_SUBST(TCL_SHLIB_LD_LIBS)
])

#------------------------------------------------------------------------
# TEA_LOAD_TKCONFIG --
#
#	Load the tkConfig.sh file
#
# Arguments:
#
#	Requires the following vars to be set:
#		TK_BIN_DIR
#
# Results:
#
#	Sets the following vars that should be in tkConfig.sh:
#		TK_BIN_DIR
#------------------------------------------------------------------------

AC_DEFUN([TEA_LOAD_TKCONFIG], [
    AC_MSG_CHECKING([for existence of ${TK_BIN_DIR}/tkConfig.sh])

    if test -f "${TK_BIN_DIR}/tkConfig.sh" ; then
        AC_MSG_RESULT([loading])
	. "${TK_BIN_DIR}/tkConfig.sh"
    else
        AC_MSG_RESULT([could not find ${TK_BIN_DIR}/tkConfig.sh])
    fi

    # eval is required to do the TK_DBGX substitution
    eval "TK_LIB_FILE=\"${TK_LIB_FILE}\""
    eval "TK_STUB_LIB_FILE=\"${TK_STUB_LIB_FILE}\""

    # If the TK_BIN_DIR is the build directory (not the install directory),
    # then set the common variable name to the value of the build variables.
    # For example, the variable TK_LIB_SPEC will be set to the value
    # of TK_BUILD_LIB_SPEC. An extension should make use of TK_LIB_SPEC
    # instead of TK_BUILD_LIB_SPEC since it will work with both an
    # installed and uninstalled version of Tcl.
    if test -f "${TK_BIN_DIR}/Makefile" ; then
        TK_LIB_SPEC="${TK_BUILD_LIB_SPEC}"
        TK_STUB_LIB_SPEC="${TK_BUILD_STUB_LIB_SPEC}"
        TK_STUB_LIB_PATH="${TK_BUILD_STUB_LIB_PATH}"
    elif test "`uname -s`" = "Darwin"; then
	# If Tk was built as a framework, attempt to use the libraries
	# from the framework at the given location so that linking works
	# against Tk.framework installed in an arbitrary location.
	case ${TK_DEFS} in
	    *TK_FRAMEWORK*)
		if test -f "${TK_BIN_DIR}/${TK_LIB_FILE}"; then
		    for i in "`cd "${TK_BIN_DIR}"; pwd`" \
			     "`cd "${TK_BIN_DIR}"/../..; pwd`"; do
			if test "`basename "$i"`" = "${TK_LIB_FILE}.framework"; then
			    TK_LIB_SPEC="-F`dirname "$i" | sed -e 's/ /\\\\ /g'` -framework ${TK_LIB_FILE}"
			    break
			fi
		    done
		fi
		if test -f "${TK_BIN_DIR}/${TK_STUB_LIB_FILE}"; then
		    TK_STUB_LIB_SPEC="-L` echo "${TK_BIN_DIR}"  | sed -e 's/ /\\\\ /g'` ${TK_STUB_LIB_FLAG}"
		    TK_STUB_LIB_PATH="${TK_BIN_DIR}/${TK_STUB_LIB_FILE}"
		fi
		;;
	esac
    fi

    # eval is required to do the TK_DBGX substitution
    eval "TK_LIB_FLAG=\"${TK_LIB_FLAG}\""
    eval "TK_LIB_SPEC=\"${TK_LIB_SPEC}\""
    eval "TK_STUB_LIB_FLAG=\"${TK_STUB_LIB_FLAG}\""
    eval "TK_STUB_LIB_SPEC=\"${TK_STUB_LIB_SPEC}\""

    # TEA specific: Ensure windowingsystem is defined
    if test "${TEA_PLATFORM}" = "unix" ; then
	case ${TK_DEFS} in
	    *MAC_OSX_TK*)
		AC_DEFINE(MAC_OSX_TK, 1, [Are we building against Mac OS X TkAqua?])
		TEA_WINDOWINGSYSTEM="aqua"
		;;
	    *)
		TEA_WINDOWINGSYSTEM="x11"
		;;
	esac
    elif test "${TEA_PLATFORM}" = "windows" ; then
	TEA_WINDOWINGSYSTEM="win32"
    fi

    AC_SUBST(TK_VERSION)
    AC_SUBST(TK_BIN_DIR)
    AC_SUBST(TK_SRC_DIR)

    AC_SUBST(TK_LIB_FILE)
    AC_SUBST(TK_LIB_FLAG)
    AC_SUBST(TK_LIB_SPEC)

    AC_SUBST(TK_STUB_LIB_FILE)
    AC_SUBST(TK_STUB_LIB_FLAG)
    AC_SUBST(TK_STUB_LIB_SPEC)

    # TEA specific:
    AC_SUBST(TK_LIBS)
    AC_SUBST(TK_XINCLUDES)
])

#------------------------------------------------------------------------
# TEA_PROG_TCLSH
#	Determine the fully qualified path name of the tclsh executable
#	in the Tcl build directory or the tclsh installed in a bin
#	directory. This macro will correctly determine the name
#	of the tclsh executable even if tclsh has not yet been
#	built in the build directory. The tclsh found is always
#	associated with a tclConfig.sh file. This tclsh should be used
#	only for running extension test cases. It should never be
#	or generation of files (like pkgIndex.tcl) at build time.
#
# Arguments:
#	none
#
# Results:
#	Substitutes the following vars:
#		TCLSH_PROG
#------------------------------------------------------------------------

AC_DEFUN([TEA_PROG_TCLSH], [
    AC_MSG_CHECKING([for tclsh])
    if test -f "${TCL_BIN_DIR}/Makefile" ; then
        # tclConfig.sh is in Tcl build directory
        if test "${TEA_PLATFORM}" = "windows"; then
            TCLSH_PROG="${TCL_BIN_DIR}/tclsh${TCL_MAJOR_VERSION}${TCL_MINOR_VERSION}${TCL_DBGX}${EXEEXT}"
        else
            TCLSH_PROG="${TCL_BIN_DIR}/tclsh"
        fi
    else
        # tclConfig.sh is in install location
        if test "${TEA_PLATFORM}" = "windows"; then
            TCLSH_PROG="tclsh${TCL_MAJOR_VERSION}${TCL_MINOR_VERSION}${TCL_DBGX}${EXEEXT}"
        else
            TCLSH_PROG="tclsh${TCL_MAJOR_VERSION}.${TCL_MINOR_VERSION}${TCL_DBGX}"
        fi
        list="`ls -d ${TCL_BIN_DIR}/../bin 2>/dev/null` \
              `ls -d ${TCL_BIN_DIR}/..     2>/dev/null` \
              `ls -d ${TCL_PREFIX}/bin     2>/dev/null`"
        for i in $list ; do
            if test -f "$i/${TCLSH_PROG}" ; then
                REAL_TCL_BIN_DIR="`cd "$i"; pwd`/"
                break
            fi
        done
        TCLSH_PROG="${REAL_TCL_BIN_DIR}${TCLSH_PROG}"
    fi
    AC_MSG_RESULT([${TCLSH_PROG}])
    AC_SUBST(TCLSH_PROG)
])

#------------------------------------------------------------------------
# TEA_PROG_WISH
#	Determine the fully qualified path name of the wish executable
#	in the Tk build directory or the wish installed in a bin
#	directory. This macro will correctly determine the name
#	of the wish executable even if wish has not yet been
#	built in the build directory. The wish found is always
#	associated with a tkConfig.sh file. This wish should be used
#	only for running extension test cases. It should never be
#	or generation of files (like pkgIndex.tcl) at build time.
#
# Arguments:
#	none
#
# Results:
#	Substitutes the following vars:
#		WISH_PROG
#------------------------------------------------------------------------

AC_DEFUN([TEA_PROG_WISH], [
    AC_MSG_CHECKING([for wish])
    if test -f "${TK_BIN_DIR}/Makefile" ; then
        # tkConfig.sh is in Tk build directory
        if test "${TEA_PLATFORM}" = "windows"; then
            WISH_PROG="${TK_BIN_DIR}/wish${TK_MAJOR_VERSION}${TK_MINOR_VERSION}${TK_DBGX}${EXEEXT}"
        else
            WISH_PROG="${TK_BIN_DIR}/wish"
        fi
    else
        # tkConfig.sh is in install location
        if test "${TEA_PLATFORM}" = "windows"; then
            WISH_PROG="wish${TK_MAJOR_VERSION}${TK_MINOR_VERSION}${TK_DBGX}${EXEEXT}"
        else
            WISH_PROG="wish${TK_MAJOR_VERSION}.${TK_MINOR_VERSION}${TK_DBGX}"
        fi
        list="`ls -d ${TK_BIN_DIR}/../bin 2>/dev/null` \
              `ls -d ${TK_BIN_DIR}/..     2>/dev/null` \
              `ls -d ${TK_PREFIX}/bin     2>/dev/null`"
        for i in $list ; do
            if test -f "$i/${WISH_PROG}" ; then
                REAL_TK_BIN_DIR="`cd "$i"; pwd`/"
                break
            fi
        done
        WISH_PROG="${REAL_TK_BIN_DIR}${WISH_PROG}"
    fi
    AC_MSG_RESULT([${WISH_PROG}])
    AC_SUBST(WISH_PROG)
])

#------------------------------------------------------------------------
# TEA_ENABLE_SHARED --
#
#	Allows the building of shared libraries
#
# Arguments:
#	none
#
# Results:
#
#	Adds the following arguments to configure:
#		--enable-shared=yes|no
#
#	Defines the following vars:
#		STATIC_BUILD	Used for building import/export libraries
#				on Windows.
#
#	Sets the following vars:
#		SHARED_BUILD	Value of 1 or 0
#------------------------------------------------------------------------

AC_DEFUN([TEA_ENABLE_SHARED], [
    AC_MSG_CHECKING([how to build libraries])
    AC_ARG_ENABLE(shared,
	AC_HELP_STRING([--enable-shared],
	    [build and link with shared libraries (default: on)]),
	[tcl_ok=$enableval], [tcl_ok=yes])

    if test "${enable_shared+set}" = set; then
	enableval="$enable_shared"
	tcl_ok=$enableval
    else
	tcl_ok=yes
    fi

    if test "$tcl_ok" = "yes" ; then
	AC_MSG_RESULT([shared])
	SHARED_BUILD=1
    else
	AC_MSG_RESULT([static])
	SHARED_BUILD=0
	AC_DEFINE(STATIC_BUILD, 1, [Is this a static build?])
    fi
    AC_SUBST(SHARED_BUILD)
])

#------------------------------------------------------------------------
# TEA_ENABLE_THREADS --
#
#	Specify if thread support should be enabled.  If "yes" is specified
#	as an arg (optional), threads are enabled by default, "no" means
#	threads are disabled.  "yes" is the default.
#
#	TCL_THREADS is checked so that if you are compiling an extension
#	against a threaded core, your extension must be compiled threaded
#	as well.
#
#	Note that it is legal to have a thread enabled extension run in a
#	threaded or non-threaded Tcl core, but a non-threaded extension may
#	only run in a non-threaded Tcl core.
#
# Arguments:
#	none
#
# Results:
#
#	Adds the following arguments to configure:
#		--enable-threads
#
#	Sets the following vars:
#		THREADS_LIBS	Thread library(s)
#
#	Defines the following vars:
#		TCL_THREADS
#		_REENTRANT
#		_THREAD_SAFE
#------------------------------------------------------------------------

AC_DEFUN([TEA_ENABLE_THREADS], [
    AC_ARG_ENABLE(threads,
	AC_HELP_STRING([--enable-threads],
	    [build with threads]),
	[tcl_ok=$enableval], [tcl_ok=yes])

    if test "${enable_threads+set}" = set; then
	enableval="$enable_threads"
	tcl_ok=$enableval
    else
	tcl_ok=yes
    fi

    if test "$tcl_ok" = "yes" -o "${TCL_THREADS}" = 1; then
	TCL_THREADS=1

	if test "${TEA_PLATFORM}" != "windows" ; then
	    # We are always OK on Windows, so check what this platform wants:

	    # USE_THREAD_ALLOC tells us to try the special thread-based
	    # allocator that significantly reduces lock contention
	    AC_DEFINE(USE_THREAD_ALLOC, 1,
		[Do we want to use the threaded memory allocator?])
	    AC_DEFINE(_REENTRANT, 1, [Do we want the reentrant OS API?])
	    if test "`uname -s`" = "SunOS" ; then
		AC_DEFINE(_POSIX_PTHREAD_SEMANTICS, 1,
			[Do we really want to follow the standard? Yes we do!])
	    fi
	    AC_DEFINE(_THREAD_SAFE, 1, [Do we want the thread-safe OS API?])
	    AC_CHECK_LIB(pthread,pthread_mutex_init,tcl_ok=yes,tcl_ok=no)
	    if test "$tcl_ok" = "no"; then
		# Check a little harder for __pthread_mutex_init in the same
		# library, as some systems hide it there until pthread.h is
		# defined.  We could alternatively do an AC_TRY_COMPILE with
		# pthread.h, but that will work with libpthread really doesn't
		# exist, like AIX 4.2.  [Bug: 4359]
		AC_CHECK_LIB(pthread, __pthread_mutex_init,
		    tcl_ok=yes, tcl_ok=no)
	    fi

	    if test "$tcl_ok" = "yes"; then
		# The space is needed
		THREADS_LIBS=" -lpthread"
	    else
		AC_CHECK_LIB(pthreads, pthread_mutex_init,
		    tcl_ok=yes, tcl_ok=no)
		if test "$tcl_ok" = "yes"; then
		    # The space is needed
		    THREADS_LIBS=" -lpthreads"
		else
		    AC_CHECK_LIB(c, pthread_mutex_init,
			tcl_ok=yes, tcl_ok=no)
		    if test "$tcl_ok" = "no"; then
			AC_CHECK_LIB(c_r, pthread_mutex_init,
			    tcl_ok=yes, tcl_ok=no)
			if test "$tcl_ok" = "yes"; then
			    # The space is needed
			    THREADS_LIBS=" -pthread"
			else
			    TCL_THREADS=0
			    AC_MSG_WARN([Do not know how to find pthread lib on your system - thread support disabled])
			fi
		    fi
		fi
	    fi
	fi
    else
	TCL_THREADS=0
    fi
    # Do checking message here to not mess up interleaved configure output
    AC_MSG_CHECKING([for building with threads])
    if test "${TCL_THREADS}" = 1; then
	AC_DEFINE(TCL_THREADS, 1, [Are we building with threads enabled?])
	AC_MSG_RESULT([yes (default)])
    else
	AC_MSG_RESULT([no])
    fi
    # TCL_THREADS sanity checking.  See if our request for building with
    # threads is the same as the way Tcl was built.  If not, warn the user.
    case ${TCL_DEFS} in
	*THREADS=1*)
	    if test "${TCL_THREADS}" = "0"; then
		AC_MSG_WARN([
    Building ${PACKAGE_NAME} without threads enabled, but building against Tcl
    that IS thread-enabled.  It is recommended to use --enable-threads.])
	    fi
	    ;;
	*)
	    if test "${TCL_THREADS}" = "1"; then
		AC_MSG_WARN([
    --enable-threads requested, but building against a Tcl that is NOT
    thread-enabled.  This is an OK configuration that will also run in
    a thread-enabled core.])
	    fi
	    ;;
    esac
    AC_SUBST(TCL_THREADS)
])

#------------------------------------------------------------------------
# TEA_ENABLE_SYMBOLS --
#
#	Specify if debugging symbols should be used.
#	Memory (TCL_MEM_DEBUG) debugging can also be enabled.
#
# Arguments:
#	none
#
#	TEA varies from core Tcl in that C|LDFLAGS_DEFAULT receives
#	the value of C|LDFLAGS_OPTIMIZE|DEBUG already substituted.
#	Requires the following vars to be set in the Makefile:
#		CFLAGS_DEFAULT
#		LDFLAGS_DEFAULT
#
# Results:
#
#	Adds the following arguments to configure:
#		--enable-symbols
#
#	Defines the following vars:
#		CFLAGS_DEFAULT	Sets to $(CFLAGS_DEBUG) if true
#				Sets to "$(CFLAGS_OPTIMIZE) -DNDEBUG" if false
#		LDFLAGS_DEFAULT	Sets to $(LDFLAGS_DEBUG) if true
#				Sets to $(LDFLAGS_OPTIMIZE) if false
#		DBGX		Formerly used as debug library extension;
#				always blank now.
#------------------------------------------------------------------------

AC_DEFUN([TEA_ENABLE_SYMBOLS], [
    dnl TEA specific: Make sure we are initialized
    AC_REQUIRE([TEA_CONFIG_CFLAGS])
    AC_MSG_CHECKING([for build with symbols])
    AC_ARG_ENABLE(symbols,
	AC_HELP_STRING([--enable-symbols],
	    [build with debugging symbols (default: off)]),
	[tcl_ok=$enableval], [tcl_ok=no])
    DBGX=""
    if test "$tcl_ok" = "no"; then
	CFLAGS_DEFAULT="${CFLAGS_OPTIMIZE} -DNDEBUG"
	LDFLAGS_DEFAULT="${LDFLAGS_OPTIMIZE}"
	AC_MSG_RESULT([no])
    else
	CFLAGS_DEFAULT="${CFLAGS_DEBUG}"
	LDFLAGS_DEFAULT="${LDFLAGS_DEBUG}"
	if test "$tcl_ok" = "yes"; then
	    AC_MSG_RESULT([yes (standard debugging)])
	fi
    fi
    # TEA specific:
    if test "${TEA_PLATFORM}" != "windows" ; then
	LDFLAGS_DEFAULT="${LDFLAGS}"
    fi
    AC_SUBST(CFLAGS_DEFAULT)
    AC_SUBST(LDFLAGS_DEFAULT)
    AC_SUBST(TCL_DBGX)

    if test "$tcl_ok" = "mem" -o "$tcl_ok" = "all"; then
	AC_DEFINE(TCL_MEM_DEBUG, 1, [Is memory debugging enabled?])
    fi

    if test "$tcl_ok" != "yes" -a "$tcl_ok" != "no"; then
	if test "$tcl_ok" = "all"; then
	    AC_MSG_RESULT([enabled symbols mem debugging])
	else
	    AC_MSG_RESULT([enabled $tcl_ok debugging])
	fi
    fi
])

#------------------------------------------------------------------------
# TEA_ENABLE_LANGINFO --
#
#	Allows use of modern nl_langinfo check for better l10n.
#	This is only relevant for Unix.
#
# Arguments:
#	none
#
# Results:
#
#	Adds the following arguments to configure:
#		--enable-langinfo=yes|no (default is yes)
#
#	Defines the following vars:
#		HAVE_LANGINFO	Triggers use of nl_langinfo if defined.
#------------------------------------------------------------------------

AC_DEFUN([TEA_ENABLE_LANGINFO], [
    AC_ARG_ENABLE(langinfo,
	AC_HELP_STRING([--enable-langinfo],
	    [use nl_langinfo if possible to determine encoding at startup, otherwise use old heuristic (default: on)]),
	[langinfo_ok=$enableval], [langinfo_ok=yes])

    HAVE_LANGINFO=0
    if test "$langinfo_ok" = "yes"; then
	AC_CHECK_HEADER(langinfo.h,[langinfo_ok=yes],[langinfo_ok=no])
    fi
    AC_MSG_CHECKING([whether to use nl_langinfo])
    if test "$langinfo_ok" = "yes"; then
	AC_CACHE_VAL(tcl_cv_langinfo_h, [
	    AC_TRY_COMPILE([#include <langinfo.h>], [nl_langinfo(CODESET);],
		    [tcl_cv_langinfo_h=yes],[tcl_cv_langinfo_h=no])])
	AC_MSG_RESULT([$tcl_cv_langinfo_h])
	if test $tcl_cv_langinfo_h = yes; then
	    AC_DEFINE(HAVE_LANGINFO, 1, [Do we have nl_langinfo()?])
	fi
    else
	AC_MSG_RESULT([$langinfo_ok])
    fi
])

#--------------------------------------------------------------------
# TEA_CONFIG_SYSTEM
#
#	Determine what the system is (some things cannot be easily checked
#	on a feature-driven basis, alas). This can usually be done via the
#	"uname" command.
#
# Arguments:
#	none
#
# Results:
#	Defines the following var:
#
#	system -	System/platform/version identification code.
#--------------------------------------------------------------------

AC_DEFUN([TEA_CONFIG_SYSTEM], [
    AC_CACHE_CHECK([system version], tcl_cv_sys_version, [
	# TEA specific:
	if test "${TEA_PLATFORM}" = "windows" ; then
	    tcl_cv_sys_version=windows
	else
	    tcl_cv_sys_version=`uname -s`-`uname -r`
	    if test "$?" -ne 0 ; then
		AC_MSG_WARN([can't find uname command])
		tcl_cv_sys_version=unknown
	    else
		if test "`uname -s`" = "AIX" ; then
		    tcl_cv_sys_version=AIX-`uname -v`.`uname -r`
		fi
	    fi
	fi
    ])
    system=$tcl_cv_sys_version
])

#--------------------------------------------------------------------
# TEA_CONFIG_CFLAGS
#
#	Try to determine the proper flags to pass to the compiler
#	for building shared libraries and other such nonsense.
#
# Arguments:
#	none
#
# Results:
#
#	Defines and substitutes the following vars:
#
#	DL_OBJS, DL_LIBS - removed for TEA, only needed by core.
#       LDFLAGS -      Flags to pass to the compiler when linking object
#                       files into an executable application binary such
#                       as tclsh.
#       LD_SEARCH_FLAGS-Flags to pass to ld, such as "-R /usr/local/tcl/lib",
#                       that tell the run-time dynamic linker where to look
#                       for shared libraries such as libtcl.so.  Depends on
#                       the variable LIB_RUNTIME_DIR in the Makefile. Could
#                       be the same as CC_SEARCH_FLAGS if ${CC} is used to link.
#       CC_SEARCH_FLAGS-Flags to pass to ${CC}, such as "-Wl,-rpath,/usr/local/tcl/lib",
#                       that tell the run-time dynamic linker where to look
#                       for shared libraries such as libtcl.so.  Depends on
#                       the variable LIB_RUNTIME_DIR in the Makefile.
#       SHLIB_CFLAGS -  Flags to pass to cc when compiling the components
#                       of a shared library (may request position-independent
#                       code, among other things).
#       SHLIB_LD -      Base command to use for combining object files
#                       into a shared library.
#       SHLIB_LD_LIBS - Dependent libraries for the linker to scan when
#                       creating shared libraries.  This symbol typically
#                       goes at the end of the "ld" commands that build
#                       shared libraries. The value of the symbol defaults to
#                       "${LIBS}" if all of the dependent libraries should
#                       be specified when creating a shared library.  If
#                       dependent libraries should not be specified (as on
#                       SunOS 4.x, where they cause the link to fail, or in
#                       general if Tcl and Tk aren't themselves shared
#                       libraries), then this symbol has an empty string
#                       as its value.
#       SHLIB_SUFFIX -  Suffix to use for the names of dynamically loadable
#                       extensions.  An empty string means we don't know how
#                       to use shared libraries on this platform.
#       LIB_SUFFIX -    Specifies everything that comes after the "libfoo"
#                       in a static or shared library name, using the $PACKAGE_VERSION variable
#                       to put the version in the right place.  This is used
#                       by platforms that need non-standard library names.
#                       Examples:  ${PACKAGE_VERSION}.so.1.1 on NetBSD, since it needs
#                       to have a version after the .so, and ${PACKAGE_VERSION}.a
#                       on AIX, since a shared library needs to have
#                       a .a extension whereas shared objects for loadable
#                       extensions have a .so extension.  Defaults to
#                       ${PACKAGE_VERSION}${SHLIB_SUFFIX}.
#	CFLAGS_DEBUG -
#			Flags used when running the compiler in debug mode
#	CFLAGS_OPTIMIZE -
#			Flags used when running the compiler in optimize mode
#	CFLAGS -	Additional CFLAGS added as necessary (usually 64-bit)
#--------------------------------------------------------------------

AC_DEFUN([TEA_CONFIG_CFLAGS], [
    dnl TEA specific: Make sure we are initialized
    AC_REQUIRE([TEA_INIT])

    # Step 0.a: Enable 64 bit support?

    AC_MSG_CHECKING([if 64bit support is requested])
    AC_ARG_ENABLE(64bit,
	AC_HELP_STRING([--enable-64bit],
	    [enable 64bit support (default: off)]),
	[do64bit=$enableval], [do64bit=no])
    AC_MSG_RESULT([$do64bit])

    # Step 0.b: Enable Solaris 64 bit VIS support?

    AC_MSG_CHECKING([if 64bit Sparc VIS support is requested])
    AC_ARG_ENABLE(64bit-vis,
	AC_HELP_STRING([--enable-64bit-vis],
	    [enable 64bit Sparc VIS support (default: off)]),
	[do64bitVIS=$enableval], [do64bitVIS=no])
    AC_MSG_RESULT([$do64bitVIS])
    # Force 64bit on with VIS
    AS_IF([test "$do64bitVIS" = "yes"], [do64bit=yes])

    # Step 0.c: Check if visibility support is available. Do this here so
    # that platform specific alternatives can be used below if this fails.

    AC_CACHE_CHECK([if compiler supports visibility "hidden"],
	tcl_cv_cc_visibility_hidden, [
	hold_cflags=$CFLAGS; CFLAGS="$CFLAGS -Werror"
	AC_TRY_LINK([
	    extern __attribute__((__visibility__("hidden"))) void f(void);
	    void f(void) {}], [f();], tcl_cv_cc_visibility_hidden=yes,
	    tcl_cv_cc_visibility_hidden=no)
	CFLAGS=$hold_cflags])
    AS_IF([test $tcl_cv_cc_visibility_hidden = yes], [
	AC_DEFINE(MODULE_SCOPE,
	    [extern __attribute__((__visibility__("hidden")))],
	    [Compiler support for module scope symbols])
	AC_DEFINE(HAVE_HIDDEN, [1], [Compiler support for module scope symbols])
    ])

    # Step 0.d: Disable -rpath support?

    AC_MSG_CHECKING([if rpath support is requested])
    AC_ARG_ENABLE(rpath,
	AC_HELP_STRING([--disable-rpath],
	    [disable rpath support (default: on)]),
	[doRpath=$enableval], [doRpath=yes])
    AC_MSG_RESULT([$doRpath])

    # TEA specific: Cross-compiling options for Windows/CE builds?

    AS_IF([test "${TEA_PLATFORM}" = windows], [
	AC_MSG_CHECKING([if Windows/CE build is requested])
	AC_ARG_ENABLE(wince,
	    AC_HELP_STRING([--enable-wince],
		[enable Win/CE support (where applicable)]),
	    [doWince=$enableval], [doWince=no])
	AC_MSG_RESULT([$doWince])
    ])

    # Set the variable "system" to hold the name and version number
    # for the system.

    TEA_CONFIG_SYSTEM

    # Require ranlib early so we can override it in special cases below.

    AC_REQUIRE([AC_PROG_RANLIB])

    # Set configuration options based on system name and version.
    # This is similar to Tcl's unix/tcl.m4 except that we've added a
    # "windows" case and removed some core-only vars.

    do64bit_ok=no
    # default to '{$LIBS}' and set to "" on per-platform necessary basis
    SHLIB_LD_LIBS='${LIBS}'
    # When ld needs options to work in 64-bit mode, put them in
    # LDFLAGS_ARCH so they eventually end up in LDFLAGS even if [load]
    # is disabled by the user. [Bug 1016796]
    LDFLAGS_ARCH=""
    UNSHARED_LIB_SUFFIX=""
    # TEA specific: use PACKAGE_VERSION instead of VERSION
    TCL_TRIM_DOTS='`echo ${PACKAGE_VERSION} | tr -d .`'
    ECHO_VERSION='`echo ${PACKAGE_VERSION}`'
    TCL_LIB_VERSIONS_OK=ok
    CFLAGS_DEBUG=-g
    AS_IF([test "$GCC" = yes], [
	CFLAGS_OPTIMIZE=-O2
	CFLAGS_WARNING="-Wall"
    ], [
	CFLAGS_OPTIMIZE=-O
	CFLAGS_WARNING=""
    ])
    AC_CHECK_TOOL(AR, ar)
    STLIB_LD='${AR} cr'
    LD_LIBRARY_PATH_VAR="LD_LIBRARY_PATH"
    AS_IF([test "x$SHLIB_VERSION" = x],[SHLIB_VERSION="1.0"])
    case $system in
	# TEA specific:
	windows)
	    # This is a 2-stage check to make sure we have the 64-bit SDK
	    # We have to know where the SDK is installed.
	    # This magic is based on MS Platform SDK for Win2003 SP1 - hobbs
	    # MACHINE is IX86 for LINK, but this is used by the manifest,
	    # which requires x86|amd64|ia64.
	    MACHINE="X86"
	    if test "$do64bit" != "no" ; then
		if test "x${MSSDK}x" = "xx" ; then
		    MSSDK="C:/Progra~1/Microsoft Platform SDK"
		fi
		MSSDK=`echo "$MSSDK" | sed -e  's!\\\!/!g'`
		PATH64=""
		case "$do64bit" in
		    amd64|x64|yes)
			MACHINE="AMD64" ; # default to AMD64 64-bit build
			PATH64="${MSSDK}/Bin/Win64/x86/AMD64"
			;;
		    ia64)
			MACHINE="IA64"
			PATH64="${MSSDK}/Bin/Win64"
			;;
		esac
		if test "$GCC" != "yes" -a ! -d "${PATH64}" ; then
		    AC_MSG_WARN([Could not find 64-bit $MACHINE SDK to enable 64bit mode])
		    AC_MSG_WARN([Ensure latest Platform SDK is installed])
		    do64bit="no"
		else
		    AC_MSG_RESULT([   Using 64-bit $MACHINE mode])
		    do64bit_ok="yes"
		fi
	    fi

	    if test "$doWince" != "no" ; then
		if test "$do64bit" != "no" ; then
		    AC_MSG_ERROR([Windows/CE and 64-bit builds incompatible])
		fi
		if test "$GCC" = "yes" ; then
		    AC_MSG_ERROR([Windows/CE and GCC builds incompatible])
		fi
		TEA_PATH_CELIB
		# Set defaults for common evc4/PPC2003 setup
		# Currently Tcl requires 300+, possibly 420+ for sockets
		CEVERSION=420; 		# could be 211 300 301 400 420 ...
		TARGETCPU=ARMV4;	# could be ARMV4 ARM MIPS SH3 X86 ...
		ARCH=ARM;		# could be ARM MIPS X86EM ...
		PLATFORM="Pocket PC 2003"; # or "Pocket PC 2002"
		if test "$doWince" != "yes"; then
		    # If !yes then the user specified something
		    # Reset ARCH to allow user to skip specifying it
		    ARCH=
		    eval `echo $doWince | awk -F, '{ \
	    if (length([$]1)) { printf "CEVERSION=\"%s\"\n", [$]1; \
	    if ([$]1 < 400)   { printf "PLATFORM=\"Pocket PC 2002\"\n" } }; \
	    if (length([$]2)) { printf "TARGETCPU=\"%s\"\n", toupper([$]2) }; \
	    if (length([$]3)) { printf "ARCH=\"%s\"\n", toupper([$]3) }; \
	    if (length([$]4)) { printf "PLATFORM=\"%s\"\n", [$]4 }; \
		    }'`
		    if test "x${ARCH}" = "x" ; then
			ARCH=$TARGETCPU;
		    fi
		fi
		OSVERSION=WCE$CEVERSION;
	    	if test "x${WCEROOT}" = "x" ; then
			WCEROOT="C:/Program Files/Microsoft eMbedded C++ 4.0"
		    if test ! -d "${WCEROOT}" ; then
			WCEROOT="C:/Program Files/Microsoft eMbedded Tools"
		    fi
		fi
		if test "x${SDKROOT}" = "x" ; then
		    SDKROOT="C:/Program Files/Windows CE Tools"
		    if test ! -d "${SDKROOT}" ; then
			SDKROOT="C:/Windows CE Tools"
		    fi
		fi
		WCEROOT=`echo "$WCEROOT" | sed -e 's!\\\!/!g'`
		SDKROOT=`echo "$SDKROOT" | sed -e 's!\\\!/!g'`
		if test ! -d "${SDKROOT}/${OSVERSION}/${PLATFORM}/Lib/${TARGETCPU}" \
		    -o ! -d "${WCEROOT}/EVC/${OSVERSION}/bin"; then
		    AC_MSG_ERROR([could not find PocketPC SDK or target compiler to enable WinCE mode [$CEVERSION,$TARGETCPU,$ARCH,$PLATFORM]])
		    doWince="no"
		else
		    # We could PATH_NOSPACE these, but that's not important,
		    # as long as we quote them when used.
		    CEINCLUDE="${SDKROOT}/${OSVERSION}/${PLATFORM}/include"
		    if test -d "${CEINCLUDE}/${TARGETCPU}" ; then
			CEINCLUDE="${CEINCLUDE}/${TARGETCPU}"
		    fi
		    CELIBPATH="${SDKROOT}/${OSVERSION}/${PLATFORM}/Lib/${TARGETCPU}"
    		fi
	    fi

	    if test "$GCC" != "yes" ; then
	        if test "${SHARED_BUILD}" = "0" ; then
		    runtime=-MT
	        else
		    runtime=-MD
	        fi

                if test "$do64bit" != "no" ; then
		    # All this magic is necessary for the Win64 SDK RC1 - hobbs
		    CC="\"${PATH64}/cl.exe\""
		    CFLAGS="${CFLAGS} -I\"${MSSDK}/Include\" -I\"${MSSDK}/Include/crt\" -I\"${MSSDK}/Include/crt/sys\""
		    RC="\"${MSSDK}/bin/rc.exe\""
		    lflags="-nologo -MACHINE:${MACHINE} -LIBPATH:\"${MSSDK}/Lib/${MACHINE}\""
		    LINKBIN="\"${PATH64}/link.exe\""
		    CFLAGS_DEBUG="-nologo -Zi -Od -W3 ${runtime}d"
		    CFLAGS_OPTIMIZE="-nologo -O2 -W2 ${runtime}"
		    # Avoid 'unresolved external symbol __security_cookie'
		    # errors, c.f. http://support.microsoft.com/?id=894573
		    TEA_ADD_LIBS([bufferoverflowU.lib])
		elif test "$doWince" != "no" ; then
		    CEBINROOT="${WCEROOT}/EVC/${OSVERSION}/bin"
		    if test "${TARGETCPU}" = "X86"; then
			CC="\"${CEBINROOT}/cl.exe\""
		    else
			CC="\"${CEBINROOT}/cl${ARCH}.exe\""
		    fi
		    CFLAGS="$CFLAGS -I\"${CELIB_DIR}/inc\" -I\"${CEINCLUDE}\""
		    RC="\"${WCEROOT}/Common/EVC/bin/rc.exe\""
		    arch=`echo ${ARCH} | awk '{print tolower([$]0)}'`
		    defs="${ARCH} _${ARCH}_ ${arch} PALM_SIZE _MT _WINDOWS"
		    if test "${SHARED_BUILD}" = "1" ; then
			# Static CE builds require static celib as well
		    	defs="${defs} _DLL"
		    fi
		    for i in $defs ; do
			AC_DEFINE_UNQUOTED($i, 1, [WinCE def ]$i)
		    done
		    AC_DEFINE_UNQUOTED(_WIN32_WCE, $CEVERSION, [_WIN32_WCE version])
		    AC_DEFINE_UNQUOTED(UNDER_CE, $CEVERSION, [UNDER_CE version])
		    CFLAGS_DEBUG="-nologo -Zi -Od"
		    CFLAGS_OPTIMIZE="-nologo -Ox"
		    lversion=`echo ${CEVERSION} | sed -e 's/\(.\)\(..\)/\1\.\2/'`
		    lflags="-MACHINE:${ARCH} -LIBPATH:\"${CELIBPATH}\" -subsystem:windowsce,${lversion} -nologo"
		    LINKBIN="\"${CEBINROOT}/link.exe\""
		    AC_SUBST(CELIB_DIR)
		else
		    RC="rc"
		    lflags="-nologo"
		    LINKBIN="link"
		    CFLAGS_DEBUG="-nologo -Z7 -Od -W3 -WX ${runtime}d"
		    CFLAGS_OPTIMIZE="-nologo -O2 -W2 ${runtime}"
		fi
	    fi

	    if test "$GCC" = "yes"; then
		# mingw gcc mode
		AC_CHECK_TOOL(RC, windres)
		CFLAGS_DEBUG="-g"
		CFLAGS_OPTIMIZE="-O2 -fomit-frame-pointer"
		SHLIB_LD='${CC} -shared'
		UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}.a'
		LDFLAGS_CONSOLE="-wl,--subsystem,console ${lflags}"
		LDFLAGS_WINDOW="-wl,--subsystem,windows ${lflags}"

		AC_CACHE_CHECK(for cross-compile version of gcc,
			ac_cv_cross,
			AC_TRY_COMPILE([
			    #ifdef _WIN32
				#error cross-compiler
			    #endif
			], [],
			ac_cv_cross=yes,
			ac_cv_cross=no)
		      )
		      if test "$ac_cv_cross" = "yes"; then
			case "$do64bit" in
			    amd64|x64|yes)
				CC="x86_64-w64-mingw32-gcc"
				LD="x86_64-w64-mingw32-ld"
				AR="x86_64-w64-mingw32-ar"
				RANLIB="x86_64-w64-mingw32-ranlib"
				RC="x86_64-w64-mingw32-windres"
			    ;;
			    *)
				CC="i686-w64-mingw32-gcc"
				LD="i686-w64-mingw32-ld"
				AR="i686-w64-mingw32-ar"
				RANLIB="i686-w64-mingw32-ranlib"
				RC="i686-w64-mingw32-windres"
			    ;;
			esac
		fi

	    else
		SHLIB_LD="${LINKBIN} -dll ${lflags}"
		# link -lib only works when -lib is the first arg
		STLIB_LD="${LINKBIN} -lib ${lflags}"
		UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}.lib'
		PATHTYPE=-w
		# For information on what debugtype is most useful, see:
		# http://msdn.microsoft.com/library/en-us/dnvc60/html/gendepdebug.asp
		# and also
		# http://msdn2.microsoft.com/en-us/library/y0zzbyt4%28VS.80%29.aspx
		# This essentially turns it all on.
		LDFLAGS_DEBUG="-debug -debugtype:cv"
		LDFLAGS_OPTIMIZE="-release"
		if test "$doWince" != "no" ; then
		    LDFLAGS_CONSOLE="-link ${lflags}"
		    LDFLAGS_WINDOW=${LDFLAGS_CONSOLE}
		else
		    LDFLAGS_CONSOLE="-link -subsystem:console ${lflags}"
		    LDFLAGS_WINDOW="-link -subsystem:windows ${lflags}"
		fi
	    fi

	    SHLIB_SUFFIX=".dll"
	    SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}.dll'

	    TCL_LIB_VERSIONS_OK=nodots
    	    ;;
	AIX-*)
	    AS_IF([test "${TCL_THREADS}" = "1" -a "$GCC" != "yes"], [
		# AIX requires the _r compiler when gcc isn't being used
		case "${CC}" in
		    *_r|*_r\ *)
			# ok ...
			;;
		    *)
			# Make sure only first arg gets _r
		    	CC=`echo "$CC" | sed -e 's/^\([[^ ]]*\)/\1_r/'`
			;;
		esac
		AC_MSG_RESULT([Using $CC for compiling with threads])
	    ])
	    LIBS="$LIBS -lc"
	    SHLIB_CFLAGS=""
	    SHLIB_SUFFIX=".so"

	    LD_LIBRARY_PATH_VAR="LIBPATH"

	    # Check to enable 64-bit flags for compiler/linker
	    AS_IF([test "$do64bit" = yes], [
		AS_IF([test "$GCC" = yes], [
		    AC_MSG_WARN([64bit mode not supported with GCC on $system])
		], [
		    do64bit_ok=yes
		    CFLAGS="$CFLAGS -q64"
		    LDFLAGS_ARCH="-q64"
		    RANLIB="${RANLIB} -X64"
		    AR="${AR} -X64"
		    SHLIB_LD_FLAGS="-b64"
		])
	    ])

	    AS_IF([test "`uname -m`" = ia64], [
		# AIX-5 uses ELF style dynamic libraries on IA-64, but not PPC
		SHLIB_LD="/usr/ccs/bin/ld -G -z text"
		AS_IF([test "$GCC" = yes], [
		    CC_SEARCH_FLAGS='-Wl,-R,${LIB_RUNTIME_DIR}'
		], [
		    CC_SEARCH_FLAGS='-R${LIB_RUNTIME_DIR}'
		])
		LD_SEARCH_FLAGS='-R ${LIB_RUNTIME_DIR}'
	    ], [
		AS_IF([test "$GCC" = yes], [
		    SHLIB_LD='${CC} -shared -Wl,-bexpall'
		], [
		    SHLIB_LD="/bin/ld -bhalt:4 -bM:SRE -bexpall -H512 -T512 -bnoentry"
		    LDFLAGS="$LDFLAGS -brtl"
		])
		SHLIB_LD="${SHLIB_LD} ${SHLIB_LD_FLAGS}"
		CC_SEARCH_FLAGS='-L${LIB_RUNTIME_DIR}'
		LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
	    ])
	    ;;
	BeOS*)
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_LD='${CC} -nostart'
	    SHLIB_SUFFIX=".so"

	    #-----------------------------------------------------------
	    # Check for inet_ntoa in -lbind, for BeOS (which also needs
	    # -lsocket, even if the network functions are in -lnet which
	    # is always linked to, for compatibility.
	    #-----------------------------------------------------------
	    AC_CHECK_LIB(bind, inet_ntoa, [LIBS="$LIBS -lbind -lsocket"])
	    ;;
	BSD/OS-4.*)
	    SHLIB_CFLAGS="-export-dynamic -fPIC"
	    SHLIB_LD='${CC} -shared'
	    SHLIB_SUFFIX=".so"
	    LDFLAGS="$LDFLAGS -export-dynamic"
	    CC_SEARCH_FLAGS=""
	    LD_SEARCH_FLAGS=""
	    ;;
	CYGWIN_*)
	    SHLIB_CFLAGS=""
	    SHLIB_LD='${CC} -shared'
	    SHLIB_SUFFIX=".dll"
	    EXEEXT=".exe"
	    do64bit_ok=yes
	    CC_SEARCH_FLAGS=""
	    LD_SEARCH_FLAGS=""
	    ;;
	Haiku*)
	    LDFLAGS="$LDFLAGS -Wl,--export-dynamic"
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_SUFFIX=".so"
	    SHLIB_LD='${CC} -shared ${CFLAGS} ${LDFLAGS}'
	    AC_CHECK_LIB(network, inet_ntoa, [LIBS="$LIBS -lnetwork"])
	    ;;
	HP-UX-*.11.*)
	    # Use updated header definitions where possible
	    AC_DEFINE(_XOPEN_SOURCE_EXTENDED, 1, [Do we want to use the XOPEN network library?])
	    # TEA specific: Needed by Tcl, but not most extensions
	    #AC_DEFINE(_XOPEN_SOURCE, 1, [Do we want to use the XOPEN network library?])
	    #LIBS="$LIBS -lxnet"               # Use the XOPEN network library

	    AS_IF([test "`uname -m`" = ia64], [
		SHLIB_SUFFIX=".so"
		# Use newer C++ library for C++ extensions
		#if test "$GCC" != "yes" ; then
		#   CPPFLAGS="-AA"
		#fi
	    ], [
		SHLIB_SUFFIX=".sl"
	    ])
	    AC_CHECK_LIB(dld, shl_load, tcl_ok=yes, tcl_ok=no)
	    AS_IF([test "$tcl_ok" = yes], [
		LDFLAGS="$LDFLAGS -Wl,-E"
		CC_SEARCH_FLAGS='-Wl,+s,+b,${LIB_RUNTIME_DIR}:.'
		LD_SEARCH_FLAGS='+s +b ${LIB_RUNTIME_DIR}:.'
		LD_LIBRARY_PATH_VAR="SHLIB_PATH"
	    ])
	    AS_IF([test "$GCC" = yes], [
		SHLIB_LD='${CC} -shared'
		LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
	    ], [
		CFLAGS="$CFLAGS -z"
		# Users may want PA-RISC 1.1/2.0 portable code - needs HP cc
		#CFLAGS="$CFLAGS +DAportable"
		SHLIB_CFLAGS="+z"
		SHLIB_LD="ld -b"
	    ])

	    # Check to enable 64-bit flags for compiler/linker
	    AS_IF([test "$do64bit" = "yes"], [
		AS_IF([test "$GCC" = yes], [
		    case `${CC} -dumpmachine` in
			hppa64*)
			    # 64-bit gcc in use.  Fix flags for GNU ld.
			    do64bit_ok=yes
			    SHLIB_LD='${CC} -shared'
			    AS_IF([test $doRpath = yes], [
				CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR}'])
			    LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
			    ;;
			*)
			    AC_MSG_WARN([64bit mode not supported with GCC on $system])
			    ;;
		    esac
		], [
		    do64bit_ok=yes
		    CFLAGS="$CFLAGS +DD64"
		    LDFLAGS_ARCH="+DD64"
		])
	    ]) ;;
	IRIX-6.*)
	    SHLIB_CFLAGS=""
	    SHLIB_LD="ld -n32 -shared -rdata_shared"
	    SHLIB_SUFFIX=".so"
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR}'
		LD_SEARCH_FLAGS='-rpath ${LIB_RUNTIME_DIR}'])
	    AS_IF([test "$GCC" = yes], [
		CFLAGS="$CFLAGS -mabi=n32"
		LDFLAGS="$LDFLAGS -mabi=n32"
	    ], [
		case $system in
		    IRIX-6.3)
			# Use to build 6.2 compatible binaries on 6.3.
			CFLAGS="$CFLAGS -n32 -D_OLD_TERMIOS"
			;;
		    *)
			CFLAGS="$CFLAGS -n32"
			;;
		esac
		LDFLAGS="$LDFLAGS -n32"
	    ])
	    ;;
	IRIX64-6.*)
	    SHLIB_CFLAGS=""
	    SHLIB_LD="ld -n32 -shared -rdata_shared"
	    SHLIB_SUFFIX=".so"
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR}'
		LD_SEARCH_FLAGS='-rpath ${LIB_RUNTIME_DIR}'])

	    # Check to enable 64-bit flags for compiler/linker

	    AS_IF([test "$do64bit" = yes], [
	        AS_IF([test "$GCC" = yes], [
	            AC_MSG_WARN([64bit mode not supported by gcc])
	        ], [
	            do64bit_ok=yes
	            SHLIB_LD="ld -64 -shared -rdata_shared"
	            CFLAGS="$CFLAGS -64"
	            LDFLAGS_ARCH="-64"
	        ])
	    ])
	    ;;
	Linux*|GNU*|NetBSD-Debian)
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_SUFFIX=".so"

	    # TEA specific:
	    CFLAGS_OPTIMIZE="-O2 -fomit-frame-pointer"

	    # TEA specific: use LDFLAGS_DEFAULT instead of LDFLAGS
	    SHLIB_LD='${CC} -shared ${CFLAGS} ${LDFLAGS_DEFAULT}'
	    LDFLAGS="$LDFLAGS -Wl,--export-dynamic"
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR}'])
	    LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
	    AS_IF([test "`uname -m`" = "alpha"], [CFLAGS="$CFLAGS -mieee"])
	    AS_IF([test $do64bit = yes], [
		AC_CACHE_CHECK([if compiler accepts -m64 flag], tcl_cv_cc_m64, [
		    hold_cflags=$CFLAGS
		    CFLAGS="$CFLAGS -m64"
		    AC_TRY_LINK(,, tcl_cv_cc_m64=yes, tcl_cv_cc_m64=no)
		    CFLAGS=$hold_cflags])
		AS_IF([test $tcl_cv_cc_m64 = yes], [
		    CFLAGS="$CFLAGS -m64"
		    do64bit_ok=yes
		])
	   ])

	    # The combo of gcc + glibc has a bug related to inlining of
	    # functions like strtod(). The -fno-builtin flag should address
	    # this problem but it does not work. The -fno-inline flag is kind
	    # of overkill but it works. Disable inlining only when one of the
	    # files in compat/*.c is being linked in.

	    AS_IF([test x"${USE_COMPAT}" != x],[CFLAGS="$CFLAGS -fno-inline"])
	    ;;
	Lynx*)
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_SUFFIX=".so"
	    CFLAGS_OPTIMIZE=-02
	    SHLIB_LD='${CC} -shared'
	    LD_FLAGS="-Wl,--export-dynamic"
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR}'
		LD_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR}'])
	    ;;
	OpenBSD-*)
	    arch=`arch -s`
	    case "$arch" in
	    vax)
		SHLIB_SUFFIX=""
		SHARED_LIB_SUFFIX=""
		LDFLAGS=""
		;;
	    *)
		SHLIB_CFLAGS="-fPIC"
		SHLIB_LD='${CC} -shared ${SHLIB_CFLAGS}'
		SHLIB_SUFFIX=".so"
		AS_IF([test $doRpath = yes], [
		    CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR}'])
		LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
		SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}.so.${SHLIB_VERSION}'
		LDFLAGS="-Wl,-export-dynamic"
		;;
	    esac
	    case "$arch" in
	    vax)
		CFLAGS_OPTIMIZE="-O1"
		;;
	    *)
		CFLAGS_OPTIMIZE="-O2"
		;;
	    esac
	    AS_IF([test "${TCL_THREADS}" = "1"], [
		# On OpenBSD:	Compile with -pthread
		#		Don't link with -lpthread
		LIBS=`echo $LIBS | sed s/-lpthread//`
		CFLAGS="$CFLAGS -pthread"
	    ])
	    # OpenBSD doesn't do version numbers with dots.
	    UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}.a'
	    TCL_LIB_VERSIONS_OK=nodots
	    ;;
	NetBSD-*)
	    # NetBSD has ELF and can use 'cc -shared' to build shared libs
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_LD='${CC} -shared ${SHLIB_CFLAGS}'
	    SHLIB_SUFFIX=".so"
	    LDFLAGS="$LDFLAGS -export-dynamic"
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR}'])
	    LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
	    AS_IF([test "${TCL_THREADS}" = "1"], [
		# The -pthread needs to go in the CFLAGS, not LIBS
		LIBS=`echo $LIBS | sed s/-pthread//`
		CFLAGS="$CFLAGS -pthread"
	    	LDFLAGS="$LDFLAGS -pthread"
	    ])
	    ;;
	FreeBSD-*)
	    # This configuration from FreeBSD Ports.
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_LD="${CC} -shared"
	    TCL_SHLIB_LD_EXTRAS="-Wl,-soname=\$[@]"
	    TK_SHLIB_LD_EXTRAS="-Wl,-soname,\$[@]"
	    SHLIB_SUFFIX=".so"
	    LDFLAGS=""
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR}'
		LD_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR}'])
	    AS_IF([test "${TCL_THREADS}" = "1"], [
		# The -pthread needs to go in the LDFLAGS, not LIBS
		LIBS=`echo $LIBS | sed s/-pthread//`
		CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
		LDFLAGS="$LDFLAGS $PTHREAD_LIBS"])
	    case $system in
	    FreeBSD-3.*)
		# Version numbers are dot-stripped by system policy.
		TCL_TRIM_DOTS=`echo ${VERSION} | tr -d .`
		UNSHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}.a'
		SHARED_LIB_SUFFIX='${TCL_TRIM_DOTS}.so'
		TCL_LIB_VERSIONS_OK=nodots
		;;
	    esac
	    ;;
	Darwin-*)
	    CFLAGS_OPTIMIZE="-Os"
	    SHLIB_CFLAGS="-fno-common"
	    # To avoid discrepancies between what headers configure sees during
	    # preprocessing tests and compiling tests, move any -isysroot and
	    # -mmacosx-version-min flags from CFLAGS to CPPFLAGS:
	    CPPFLAGS="${CPPFLAGS} `echo " ${CFLAGS}" | \
		awk 'BEGIN {FS=" +-";ORS=" "}; {for (i=2;i<=NF;i++) \
		if ([$]i~/^(isysroot|mmacosx-version-min)/) print "-"[$]i}'`"
	    CFLAGS="`echo " ${CFLAGS}" | \
		awk 'BEGIN {FS=" +-";ORS=" "}; {for (i=2;i<=NF;i++) \
		if (!([$]i~/^(isysroot|mmacosx-version-min)/)) print "-"[$]i}'`"
	    AS_IF([test $do64bit = yes], [
		case `arch` in
		    ppc)
			AC_CACHE_CHECK([if compiler accepts -arch ppc64 flag],
				tcl_cv_cc_arch_ppc64, [
			    hold_cflags=$CFLAGS
			    CFLAGS="$CFLAGS -arch ppc64 -mpowerpc64 -mcpu=G5"
			    AC_TRY_LINK(,, tcl_cv_cc_arch_ppc64=yes,
				    tcl_cv_cc_arch_ppc64=no)
			    CFLAGS=$hold_cflags])
			AS_IF([test $tcl_cv_cc_arch_ppc64 = yes], [
			    CFLAGS="$CFLAGS -arch ppc64 -mpowerpc64 -mcpu=G5"
			    do64bit_ok=yes
			]);;
		    i386)
			AC_CACHE_CHECK([if compiler accepts -arch x86_64 flag],
				tcl_cv_cc_arch_x86_64, [
			    hold_cflags=$CFLAGS
			    CFLAGS="$CFLAGS -arch x86_64"
			    AC_TRY_LINK(,, tcl_cv_cc_arch_x86_64=yes,
				    tcl_cv_cc_arch_x86_64=no)
			    CFLAGS=$hold_cflags])
			AS_IF([test $tcl_cv_cc_arch_x86_64 = yes], [
			    CFLAGS="$CFLAGS -arch x86_64"
			    do64bit_ok=yes
			]);;
		    *)
			AC_MSG_WARN([Don't know how enable 64-bit on architecture `arch`]);;
		esac
	    ], [
		# Check for combined 32-bit and 64-bit fat build
		AS_IF([echo "$CFLAGS " |grep -E -q -- '-arch (ppc64|x86_64) ' \
		    && echo "$CFLAGS " |grep -E -q -- '-arch (ppc|i386) '], [
		    fat_32_64=yes])
	    ])
	    # TEA specific: use LDFLAGS_DEFAULT instead of LDFLAGS
	    SHLIB_LD='${CC} -dynamiclib ${CFLAGS} ${LDFLAGS_DEFAULT}'
	    AC_CACHE_CHECK([if ld accepts -single_module flag], tcl_cv_ld_single_module, [
		hold_ldflags=$LDFLAGS
		LDFLAGS="$LDFLAGS -dynamiclib -Wl,-single_module"
		AC_TRY_LINK(, [int i;], tcl_cv_ld_single_module=yes, tcl_cv_ld_single_module=no)
		LDFLAGS=$hold_ldflags])
	    AS_IF([test $tcl_cv_ld_single_module = yes], [
		SHLIB_LD="${SHLIB_LD} -Wl,-single_module"
	    ])
	    # TEA specific: link shlib with current and compatibility version flags
	    vers=`echo ${PACKAGE_VERSION} | sed -e 's/^\([[0-9]]\{1,5\}\)\(\(\.[[0-9]]\{1,3\}\)\{0,2\}\).*$/\1\2/p' -e d`
	    SHLIB_LD="${SHLIB_LD} -current_version ${vers:-0} -compatibility_version ${vers:-0}"
	    SHLIB_SUFFIX=".dylib"
	    # Don't use -prebind when building for Mac OS X 10.4 or later only:
	    AS_IF([test "`echo "${MACOSX_DEPLOYMENT_TARGET}" | awk -F '10\\.' '{print int([$]2)}'`" -lt 4 -a \
		"`echo "${CPPFLAGS}" | awk -F '-mmacosx-version-min=10\\.' '{print int([$]2)}'`" -lt 4], [
		LDFLAGS="$LDFLAGS -prebind"])
	    LDFLAGS="$LDFLAGS -headerpad_max_install_names"
	    AC_CACHE_CHECK([if ld accepts -search_paths_first flag],
		    tcl_cv_ld_search_paths_first, [
		hold_ldflags=$LDFLAGS
		LDFLAGS="$LDFLAGS -Wl,-search_paths_first"
		AC_TRY_LINK(, [int i;], tcl_cv_ld_search_paths_first=yes,
			tcl_cv_ld_search_paths_first=no)
		LDFLAGS=$hold_ldflags])
	    AS_IF([test $tcl_cv_ld_search_paths_first = yes], [
		LDFLAGS="$LDFLAGS -Wl,-search_paths_first"
	    ])
	    AS_IF([test "$tcl_cv_cc_visibility_hidden" != yes], [
		AC_DEFINE(MODULE_SCOPE, [__private_extern__],
		    [Compiler support for module scope symbols])
		tcl_cv_cc_visibility_hidden=yes
	    ])
	    CC_SEARCH_FLAGS=""
	    LD_SEARCH_FLAGS=""
	    LD_LIBRARY_PATH_VAR="DYLD_LIBRARY_PATH"
	    # TEA specific: for combined 32 & 64 bit fat builds of Tk
	    # extensions, verify that 64-bit build is possible.
	    AS_IF([test "$fat_32_64" = yes && test -n "${TK_BIN_DIR}"], [
		AS_IF([test "${TEA_WINDOWINGSYSTEM}" = x11], [
		    AC_CACHE_CHECK([for 64-bit X11], tcl_cv_lib_x11_64, [
			for v in CFLAGS CPPFLAGS LDFLAGS; do
			    eval 'hold_'$v'="$'$v'";'$v'="`echo "$'$v' "|sed -e "s/-arch ppc / /g" -e "s/-arch i386 / /g"`"'
			done
			CPPFLAGS="$CPPFLAGS -I/usr/X11R6/include"
			LDFLAGS="$LDFLAGS -L/usr/X11R6/lib -lX11"
			AC_TRY_LINK([#include <X11/Xlib.h>], [XrmInitialize();],
			    tcl_cv_lib_x11_64=yes, tcl_cv_lib_x11_64=no)
			for v in CFLAGS CPPFLAGS LDFLAGS; do
			    eval $v'="$hold_'$v'"'
			done])
		])
		AS_IF([test "${TEA_WINDOWINGSYSTEM}" = aqua], [
		    AC_CACHE_CHECK([for 64-bit Tk], tcl_cv_lib_tk_64, [
			for v in CFLAGS CPPFLAGS LDFLAGS; do
			    eval 'hold_'$v'="$'$v'";'$v'="`echo "$'$v' "|sed -e "s/-arch ppc / /g" -e "s/-arch i386 / /g"`"'
			done
			CPPFLAGS="$CPPFLAGS -DUSE_TCL_STUBS=1 -DUSE_TK_STUBS=1 ${TCL_INCLUDES} ${TK_INCLUDES}"
			LDFLAGS="$LDFLAGS ${TCL_STUB_LIB_SPEC} ${TK_STUB_LIB_SPEC}"
			AC_TRY_LINK([#include <tk.h>], [Tk_InitStubs(NULL, "", 0);],
			    tcl_cv_lib_tk_64=yes, tcl_cv_lib_tk_64=no)
			for v in CFLAGS CPPFLAGS LDFLAGS; do
			    eval $v'="$hold_'$v'"'
			done])
		])
		# remove 64-bit arch flags from CFLAGS et al. if configuration
		# does not support 64-bit.
		AS_IF([test "$tcl_cv_lib_tk_64" = no -o "$tcl_cv_lib_x11_64" = no], [
		    AC_MSG_NOTICE([Removing 64-bit architectures from compiler & linker flags])
		    for v in CFLAGS CPPFLAGS LDFLAGS; do
			eval $v'="`echo "$'$v' "|sed -e "s/-arch ppc64 / /g" -e "s/-arch x86_64 / /g"`"'
		    done])
	    ])
	    ;;
	OS/390-*)
	    CFLAGS_OPTIMIZE=""		# Optimizer is buggy
	    AC_DEFINE(_OE_SOCKETS, 1,	# needed in sys/socket.h
		[Should OS/390 do the right thing with sockets?])
	    ;;
	OSF1-V*)
	    # Digital OSF/1
	    SHLIB_CFLAGS=""
	    AS_IF([test "$SHARED_BUILD" = 1], [
	        SHLIB_LD='ld -shared -expect_unresolved "*"'
	    ], [
	        SHLIB_LD='ld -non_shared -expect_unresolved "*"'
	    ])
	    SHLIB_SUFFIX=".so"
	    AS_IF([test $doRpath = yes], [
		CC_SEARCH_FLAGS='-Wl,-rpath,${LIB_RUNTIME_DIR}'
		LD_SEARCH_FLAGS='-rpath ${LIB_RUNTIME_DIR}'])
	    AS_IF([test "$GCC" = yes], [CFLAGS="$CFLAGS -mieee"], [
		CFLAGS="$CFLAGS -DHAVE_TZSET -std1 -ieee"])
	    # see pthread_intro(3) for pthread support on osf1, k.furukawa
	    AS_IF([test "${TCL_THREADS}" = 1], [
		CFLAGS="$CFLAGS -DHAVE_PTHREAD_ATTR_SETSTACKSIZE"
		CFLAGS="$CFLAGS -DTCL_THREAD_STACK_MIN=PTHREAD_STACK_MIN*64"
		LIBS=`echo $LIBS | sed s/-lpthreads//`
		AS_IF([test "$GCC" = yes], [
		    LIBS="$LIBS -lpthread -lmach -lexc"
		], [
		    CFLAGS="$CFLAGS -pthread"
		    LDFLAGS="$LDFLAGS -pthread"
		])
	    ])
	    ;;
	QNX-6*)
	    # QNX RTP
	    # This may work for all QNX, but it was only reported for v6.
	    SHLIB_CFLAGS="-fPIC"
	    SHLIB_LD="ld -Bshareable -x"
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    CC_SEARCH_FLAGS=""
	    LD_SEARCH_FLAGS=""
	    ;;
	SCO_SV-3.2*)
	    AS_IF([test "$GCC" = yes], [
		SHLIB_CFLAGS="-fPIC -melf"
		LDFLAGS="$LDFLAGS -melf -Wl,-Bexport"
	    ], [
		SHLIB_CFLAGS="-Kpic -belf"
		LDFLAGS="$LDFLAGS -belf -Wl,-Bexport"
	    ])
	    SHLIB_LD="ld -G"
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    CC_SEARCH_FLAGS=""
	    LD_SEARCH_FLAGS=""
	    ;;
	SunOS-5.[[0-6]])
	    # Careful to not let 5.10+ fall into this case

	    # Note: If _REENTRANT isn't defined, then Solaris
	    # won't define thread-safe library routines.

	    AC_DEFINE(_REENTRANT, 1, [Do we want the reentrant OS API?])
	    AC_DEFINE(_POSIX_PTHREAD_SEMANTICS, 1,
		[Do we really want to follow the standard? Yes we do!])

	    SHLIB_CFLAGS="-KPIC"
	    SHLIB_SUFFIX=".so"
	    AS_IF([test "$GCC" = yes], [
		SHLIB_LD='${CC} -shared'
		CC_SEARCH_FLAGS='-Wl,-R,${LIB_RUNTIME_DIR}'
		LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
	    ], [
		SHLIB_LD="/usr/ccs/bin/ld -G -z text"
		CC_SEARCH_FLAGS='-R ${LIB_RUNTIME_DIR}'
		LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
	    ])
	    ;;
	SunOS-5*)
	    # Note: If _REENTRANT isn't defined, then Solaris
	    # won't define thread-safe library routines.

	    AC_DEFINE(_REENTRANT, 1, [Do we want the reentrant OS API?])
	    AC_DEFINE(_POSIX_PTHREAD_SEMANTICS, 1,
		[Do we really want to follow the standard? Yes we do!])

	    SHLIB_CFLAGS="-KPIC"

	    # Check to enable 64-bit flags for compiler/linker
	    AS_IF([test "$do64bit" = yes], [
		arch=`isainfo`
		AS_IF([test "$arch" = "sparcv9 sparc"], [
		    AS_IF([test "$GCC" = yes], [
			AS_IF([test "`${CC} -dumpversion | awk -F. '{print [$]1}'`" -lt 3], [
			    AC_MSG_WARN([64bit mode not supported with GCC < 3.2 on $system])
			], [
			    do64bit_ok=yes
			    CFLAGS="$CFLAGS -m64 -mcpu=v9"
			    LDFLAGS="$LDFLAGS -m64 -mcpu=v9"
			    SHLIB_CFLAGS="-fPIC"
			])
		    ], [
			do64bit_ok=yes
			AS_IF([test "$do64bitVIS" = yes], [
			    CFLAGS="$CFLAGS -xarch=v9a"
			    LDFLAGS_ARCH="-xarch=v9a"
			], [
			    CFLAGS="$CFLAGS -xarch=v9"
			    LDFLAGS_ARCH="-xarch=v9"
			])
			# Solaris 64 uses this as well
			#LD_LIBRARY_PATH_VAR="LD_LIBRARY_PATH_64"
		    ])
		], [AS_IF([test "$arch" = "amd64 i386"], [
		    AS_IF([test "$GCC" = yes], [
			case $system in
			    SunOS-5.1[[1-9]]*|SunOS-5.[[2-9]][[0-9]]*)
				do64bit_ok=yes
				CFLAGS="$CFLAGS -m64"
				LDFLAGS="$LDFLAGS -m64";;
			    *)
				AC_MSG_WARN([64bit mode not supported with GCC on $system]);;
			esac
		    ], [
			do64bit_ok=yes
			case $system in
			    SunOS-5.1[[1-9]]*|SunOS-5.[[2-9]][[0-9]]*)
				CFLAGS="$CFLAGS -m64"
				LDFLAGS="$LDFLAGS -m64";;
			    *)
				CFLAGS="$CFLAGS -xarch=amd64"
				LDFLAGS="$LDFLAGS -xarch=amd64";;
			esac
		    ])
		], [AC_MSG_WARN([64bit mode not supported for $arch])])])
	    ])

	    SHLIB_SUFFIX=".so"
	    AS_IF([test "$GCC" = yes], [
		SHLIB_LD='${CC} -shared'
		CC_SEARCH_FLAGS='-Wl,-R,${LIB_RUNTIME_DIR}'
		LD_SEARCH_FLAGS=${CC_SEARCH_FLAGS}
		AS_IF([test "$do64bit_ok" = yes], [
		    AS_IF([test "$arch" = "sparcv9 sparc"], [
			# We need to specify -static-libgcc or we need to
			# add the path to the sparv9 libgcc.
			# JH: static-libgcc is necessary for core Tcl, but may
			# not be necessary for extensions.
			SHLIB_LD="$SHLIB_LD -m64 -mcpu=v9 -static-libgcc"
			# for finding sparcv9 libgcc, get the regular libgcc
			# path, remove so name and append 'sparcv9'
			#v9gcclibdir="`gcc -print-file-name=libgcc_s.so` | ..."
			#CC_SEARCH_FLAGS="${CC_SEARCH_FLAGS},-R,$v9gcclibdir"
		    ], [AS_IF([test "$arch" = "amd64 i386"], [
			# JH: static-libgcc is necessary for core Tcl, but may
			# not be necessary for extensions.
			SHLIB_LD="$SHLIB_LD -m64 -static-libgcc"
		    ])])
		])
	    ], [
		case $system in
		    SunOS-5.[[1-9]][[0-9]]*)
			# TEA specific: use LDFLAGS_DEFAULT instead of LDFLAGS
			SHLIB_LD='${CC} -G -z text ${LDFLAGS_DEFAULT}';;
		    *)
			SHLIB_LD='/usr/ccs/bin/ld -G -z text';;
		esac
		CC_SEARCH_FLAGS='-Wl,-R,${LIB_RUNTIME_DIR}'
		LD_SEARCH_FLAGS='-R ${LIB_RUNTIME_DIR}'
	    ])
	    ;;
	UNIX_SV* | UnixWare-5*)
	    SHLIB_CFLAGS="-KPIC"
	    SHLIB_LD='${CC} -G'
	    SHLIB_LD_LIBS=""
	    SHLIB_SUFFIX=".so"
	    # Some UNIX_SV* systems (unixware 1.1.2 for example) have linkers
	    # that don't grok the -Bexport option.  Test that it does.
	    AC_CACHE_CHECK([for ld accepts -Bexport flag], tcl_cv_ld_Bexport, [
		hold_ldflags=$LDFLAGS
		LDFLAGS="$LDFLAGS -Wl,-Bexport"
		AC_TRY_LINK(, [int i;], tcl_cv_ld_Bexport=yes, tcl_cv_ld_Bexport=no)
	        LDFLAGS=$hold_ldflags])
	    AS_IF([test $tcl_cv_ld_Bexport = yes], [
		LDFLAGS="$LDFLAGS -Wl,-Bexport"
	    ])
	    CC_SEARCH_FLAGS=""
	    LD_SEARCH_FLAGS=""
	    ;;
    esac

    AS_IF([test "$do64bit" = yes -a "$do64bit_ok" = no], [
	AC_MSG_WARN([64bit support being disabled -- don't know magic for this platform])
    ])

dnl # Add any CPPFLAGS set in the environment to our CFLAGS, but delay doing so
dnl # until the end of configure, as configure's compile and link tests use
dnl # both CPPFLAGS and CFLAGS (unlike our compile and link) but configure's
dnl # preprocessing tests use only CPPFLAGS.
    AC_CONFIG_COMMANDS_PRE([CFLAGS="${CFLAGS} ${CPPFLAGS}"; CPPFLAGS=""])

    # Add in the arch flags late to ensure it wasn't removed.
    # Not necessary in TEA, but this is aligned with core
    LDFLAGS="$LDFLAGS $LDFLAGS_ARCH"

    # If we're running gcc, then change the C flags for compiling shared
    # libraries to the right flags for gcc, instead of those for the
    # standard manufacturer compiler.

    AS_IF([test "$GCC" = yes], [
	case $system in
	    AIX-*) ;;
	    BSD/OS*) ;;
	    CYGWIN_*|MINGW32_*) ;;
	    IRIX*) ;;
	    NetBSD-*|FreeBSD-*|OpenBSD-*) ;;
	    Darwin-*) ;;
	    SCO_SV-3.2*) ;;
	    windows) ;;
	    *) SHLIB_CFLAGS="-fPIC" ;;
	esac])

    AS_IF([test "$tcl_cv_cc_visibility_hidden" != yes], [
	AC_DEFINE(MODULE_SCOPE, [extern],
	    [No Compiler support for module scope symbols])
    ])

    AS_IF([test "$SHARED_LIB_SUFFIX" = ""], [
    # TEA specific: use PACKAGE_VERSION instead of VERSION
    SHARED_LIB_SUFFIX='${PACKAGE_VERSION}${SHLIB_SUFFIX}'])
    AS_IF([test "$UNSHARED_LIB_SUFFIX" = ""], [
    # TEA specific: use PACKAGE_VERSION instead of VERSION
    UNSHARED_LIB_SUFFIX='${PACKAGE_VERSION}.a'])

    if test "${GCC}" = "yes" -a ${SHLIB_SUFFIX} = ".dll"; then
	AC_CACHE_CHECK(for SEH support in compiler,
	    tcl_cv_seh,
	AC_TRY_RUN([
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN

	    int main(int argc, char** argv) {
		int a, b = 0;
		__try {
		    a = 666 / b;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
		    return 0;
		}
		return 1;
	    }
	],
	    tcl_cv_seh=yes,
	    tcl_cv_seh=no,
	    tcl_cv_seh=no)
	)
	if test "$tcl_cv_seh" = "no" ; then
	    AC_DEFINE(HAVE_NO_SEH, 1,
		    [Defined when mingw does not support SEH])
	fi

	#
	# Check to see if the excpt.h include file provided contains the
	# definition for EXCEPTION_DISPOSITION; if not, which is the case
	# with Cygwin's version as of 2002-04-10, define it to be int,
	# sufficient for getting the current code to work.
	#
	AC_CACHE_CHECK(for EXCEPTION_DISPOSITION support in include files,
	    tcl_cv_eh_disposition,
	    AC_TRY_COMPILE([
#	    define WIN32_LEAN_AND_MEAN
#	    include <windows.h>
#	    undef WIN32_LEAN_AND_MEAN
	    ],[
		EXCEPTION_DISPOSITION x;
	    ],
		tcl_cv_eh_disposition=yes,
		tcl_cv_eh_disposition=no)
	)
	if test "$tcl_cv_eh_disposition" = "no" ; then
	AC_DEFINE(EXCEPTION_DISPOSITION, int,
		[Defined when cygwin/mingw does not support EXCEPTION DISPOSITION])
	fi

	# Check to see if winnt.h defines CHAR, SHORT, and LONG
	# even if VOID has already been #defined. The win32api
	# used by mingw and cygwin is known to do this.

	AC_CACHE_CHECK(for winnt.h that ignores VOID define,
	    tcl_cv_winnt_ignore_void,
	    AC_TRY_COMPILE([
#define VOID void
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
	    ], [
		CHAR c;
		SHORT s;
		LONG l;
	    ],
        tcl_cv_winnt_ignore_void=yes,
        tcl_cv_winnt_ignore_void=no)
	)
	if test "$tcl_cv_winnt_ignore_void" = "yes" ; then
	    AC_DEFINE(HAVE_WINNT_IGNORE_VOID, 1,
		    [Defined when cygwin/mingw ignores VOID define in winnt.h])
	fi
    fi

	# See if the compiler supports casting to a union type.
	# This is used to stop gcc from printing a compiler
	# warning when initializing a union member.

	AC_CACHE_CHECK(for cast to union support,
	    tcl_cv_cast_to_union,
	    AC_TRY_COMPILE([],
	    [
		  union foo { int i; double d; };
		  union foo f = (union foo) (int) 0;
	    ],
	    tcl_cv_cast_to_union=yes,
	    tcl_cv_cast_to_union=no)
	)
	if test "$tcl_cv_cast_to_union" = "yes"; then
	    AC_DEFINE(HAVE_CAST_TO_UNION, 1,
		    [Defined when compiler supports casting to union type.])
	fi

    AC_SUBST(CFLAGS_DEBUG)
    AC_SUBST(CFLAGS_OPTIMIZE)
    AC_SUBST(CFLAGS_WARNING)

    AC_SUBST(STLIB_LD)
    AC_SUBST(SHLIB_LD)

    AC_SUBST(SHLIB_LD_LIBS)
    AC_SUBST(SHLIB_CFLAGS)

    AC_SUBST(LD_LIBRARY_PATH_VAR)

    # These must be called after we do the basic CFLAGS checks and
    # verify any possible 64-bit or similar switches are necessary
    TEA_TCL_EARLY_FLAGS
    TEA_TCL_64BIT_FLAGS
])

#--------------------------------------------------------------------
# TEA_SERIAL_PORT
#
#	Determine which interface to use to talk to the serial port.
#	Note that #include lines must begin in leftmost column for
#	some compilers to recognize them as preprocessor directives,
#	and some build environments have stdin not pointing at a
#	pseudo-terminal (usually /dev/null instead.)
#
# Arguments:
#	none
#
# Results:
#
#	Defines only one of the following vars:
#		HAVE_SYS_MODEM_H
#		USE_TERMIOS
#		USE_TERMIO
#		USE_SGTTY
#--------------------------------------------------------------------

AC_DEFUN([TEA_SERIAL_PORT], [
    AC_CHECK_HEADERS(sys/modem.h)
    AC_CACHE_CHECK([termios vs. termio vs. sgtty], tcl_cv_api_serial, [
    AC_TRY_RUN([
#include <termios.h>

int main() {
    struct termios t;
    if (tcgetattr(0, &t) == 0) {
	cfsetospeed(&t, 0);
	t.c_cflag |= PARENB | PARODD | CSIZE | CSTOPB;
	return 0;
    }
    return 1;
}], tcl_cv_api_serial=termios, tcl_cv_api_serial=no, tcl_cv_api_serial=no)
    if test $tcl_cv_api_serial = no ; then
	AC_TRY_RUN([
#include <termio.h>

int main() {
    struct termio t;
    if (ioctl(0, TCGETA, &t) == 0) {
	t.c_cflag |= CBAUD | PARENB | PARODD | CSIZE | CSTOPB;
	return 0;
    }
    return 1;
}], tcl_cv_api_serial=termio, tcl_cv_api_serial=no, tcl_cv_api_serial=no)
    fi
    if test $tcl_cv_api_serial = no ; then
	AC_TRY_RUN([
#include <sgtty.h>

int main() {
    struct sgttyb t;
    if (ioctl(0, TIOCGETP, &t) == 0) {
	t.sg_ospeed = 0;
	t.sg_flags |= ODDP | EVENP | RAW;
	return 0;
    }
    return 1;
}], tcl_cv_api_serial=sgtty, tcl_cv_api_serial=no, tcl_cv_api_serial=no)
    fi
    if test $tcl_cv_api_serial = no ; then
	AC_TRY_RUN([
#include <termios.h>
#include <errno.h>

int main() {
    struct termios t;
    if (tcgetattr(0, &t) == 0
	|| errno == ENOTTY || errno == ENXIO || errno == EINVAL) {
	cfsetospeed(&t, 0);
	t.c_cflag |= PARENB | PARODD | CSIZE | CSTOPB;
	return 0;
    }
    return 1;
}], tcl_cv_api_serial=termios, tcl_cv_api_serial=no, tcl_cv_api_serial=no)
    fi
    if test $tcl_cv_api_serial = no; then
	AC_TRY_RUN([
#include <termio.h>
#include <errno.h>

int main() {
    struct termio t;
    if (ioctl(0, TCGETA, &t) == 0
	|| errno == ENOTTY || errno == ENXIO || errno == EINVAL) {
	t.c_cflag |= CBAUD | PARENB | PARODD | CSIZE | CSTOPB;
	return 0;
    }
    return 1;
    }], tcl_cv_api_serial=termio, tcl_cv_api_serial=no, tcl_cv_api_serial=no)
    fi
    if test $tcl_cv_api_serial = no; then
	AC_TRY_RUN([
#include <sgtty.h>
#include <errno.h>

int main() {
    struct sgttyb t;
    if (ioctl(0, TIOCGETP, &t) == 0
	|| errno == ENOTTY || errno == ENXIO || errno == EINVAL) {
	t.sg_ospeed = 0;
	t.sg_flags |= ODDP | EVENP | RAW;
	return 0;
    }
    return 1;
}], tcl_cv_api_serial=sgtty, tcl_cv_api_serial=none, tcl_cv_api_serial=none)
    fi])
    case $tcl_cv_api_serial in
	termios) AC_DEFINE(USE_TERMIOS, 1, [Use the termios API for serial lines]);;
	termio)  AC_DEFINE(USE_TERMIO, 1, [Use the termio API for serial lines]);;
	sgtty)   AC_DEFINE(USE_SGTTY, 1, [Use the sgtty API for serial lines]);;
    esac
])

#--------------------------------------------------------------------
# TEA_MISSING_POSIX_HEADERS
#
#	Supply substitutes for missing POSIX header files.  Special
#	notes:
#	    - stdlib.h doesn't define strtol, strtoul, or
#	      strtod in some versions of SunOS
#	    - some versions of string.h don't declare procedures such
#	      as strstr
#
# Arguments:
#	none
#
# Results:
#
#	Defines some of the following vars:
#		NO_DIRENT_H
#		NO_ERRNO_H
#		NO_VALUES_H
#		HAVE_LIMITS_H or NO_LIMITS_H
#		NO_STDLIB_H
#		NO_STRING_H
#		NO_SYS_WAIT_H
#		NO_DLFCN_H
#		HAVE_SYS_PARAM_H
#
#		HAVE_STRING_H ?
#
# tkUnixPort.h checks for HAVE_LIMITS_H, so do both HAVE and
# CHECK on limits.h
#--------------------------------------------------------------------

AC_DEFUN([TEA_MISSING_POSIX_HEADERS], [
    AC_CACHE_CHECK([dirent.h], tcl_cv_dirent_h, [
    AC_TRY_LINK([#include <sys/types.h>
#include <dirent.h>], [
#ifndef _POSIX_SOURCE
#   ifdef __Lynx__
	/*
	 * Generate compilation error to make the test fail:  Lynx headers
	 * are only valid if really in the POSIX environment.
	 */

	missing_procedure();
#   endif
#endif
DIR *d;
struct dirent *entryPtr;
char *p;
d = opendir("foobar");
entryPtr = readdir(d);
p = entryPtr->d_name;
closedir(d);
], tcl_cv_dirent_h=yes, tcl_cv_dirent_h=no)])

    if test $tcl_cv_dirent_h = no; then
	AC_DEFINE(NO_DIRENT_H, 1, [Do we have <dirent.h>?])
    fi

    # TEA specific:
    AC_CHECK_HEADER(errno.h, , [AC_DEFINE(NO_ERRNO_H, 1, [Do we have <errno.h>?])])
    AC_CHECK_HEADER(float.h, , [AC_DEFINE(NO_FLOAT_H, 1, [Do we have <float.h>?])])
    AC_CHECK_HEADER(values.h, , [AC_DEFINE(NO_VALUES_H, 1, [Do we have <values.h>?])])
    AC_CHECK_HEADER(limits.h,
	[AC_DEFINE(HAVE_LIMITS_H, 1, [Do we have <limits.h>?])],
	[AC_DEFINE(NO_LIMITS_H, 1, [Do we have <limits.h>?])])
    AC_CHECK_HEADER(stdlib.h, tcl_ok=1, tcl_ok=0)
    AC_EGREP_HEADER(strtol, stdlib.h, , tcl_ok=0)
    AC_EGREP_HEADER(strtoul, stdlib.h, , tcl_ok=0)
    AC_EGREP_HEADER(strtod, stdlib.h, , tcl_ok=0)
    if test $tcl_ok = 0; then
	AC_DEFINE(NO_STDLIB_H, 1, [Do we have <stdlib.h>?])
    fi
    AC_CHECK_HEADER(string.h, tcl_ok=1, tcl_ok=0)
    AC_EGREP_HEADER(strstr, string.h, , tcl_ok=0)
    AC_EGREP_HEADER(strerror, string.h, , tcl_ok=0)

    # See also memmove check below for a place where NO_STRING_H can be
    # set and why.

    if test $tcl_ok = 0; then
	AC_DEFINE(NO_STRING_H, 1, [Do we have <string.h>?])
    fi

    AC_CHECK_HEADER(sys/wait.h, , [AC_DEFINE(NO_SYS_WAIT_H, 1, [Do we have <sys/wait.h>?])])
    AC_CHECK_HEADER(dlfcn.h, , [AC_DEFINE(NO_DLFCN_H, 1, [Do we have <dlfcn.h>?])])

    # OS/390 lacks sys/param.h (and doesn't need it, by chance).
    AC_HAVE_HEADERS(sys/param.h)
])

#--------------------------------------------------------------------
# TEA_PATH_X
#
#	Locate the X11 header files and the X11 library archive.  Try
#	the ac_path_x macro first, but if it doesn't find the X stuff
#	(e.g. because there's no xmkmf program) then check through
#	a list of possible directories.  Under some conditions the
#	autoconf macro will return an include directory that contains
#	no include files, so double-check its result just to be safe.
#
#	This should be called after TEA_CONFIG_CFLAGS as setting the
#	LIBS line can confuse some configure macro magic.
#
# Arguments:
#	none
#
# Results:
#
#	Sets the following vars:
#		XINCLUDES
#		XLIBSW
#		PKG_LIBS (appends to)
#--------------------------------------------------------------------

AC_DEFUN([TEA_PATH_X], [
    if test "${TEA_WINDOWINGSYSTEM}" = "x11" ; then
	TEA_PATH_UNIX_X
    fi
])

AC_DEFUN([TEA_PATH_UNIX_X], [
    AC_PATH_X
    not_really_there=""
    if test "$no_x" = ""; then
	if test "$x_includes" = ""; then
	    AC_TRY_CPP([#include <X11/Xlib.h>], , not_really_there="yes")
	else
	    if test ! -r $x_includes/X11/Xlib.h; then
		not_really_there="yes"
	    fi
	fi
    fi
    if test "$no_x" = "yes" -o "$not_really_there" = "yes"; then
	AC_MSG_CHECKING([for X11 header files])
	found_xincludes="no"
	AC_TRY_CPP([#include <X11/Xlib.h>], found_xincludes="yes", found_xincludes="no")
	if test "$found_xincludes" = "no"; then
	    dirs="/usr/unsupported/include /usr/local/include /usr/X386/include /usr/X11R6/include /usr/X11R5/include /usr/include/X11R5 /usr/include/X11R4 /usr/openwin/include /usr/X11/include /usr/sww/include"
	    for i in $dirs ; do
		if test -r $i/X11/Xlib.h; then
		    AC_MSG_RESULT([$i])
		    XINCLUDES=" -I$i"
		    found_xincludes="yes"
		    break
		fi
	    done
	fi
    else
	if test "$x_includes" != ""; then
	    XINCLUDES="-I$x_includes"
	    found_xincludes="yes"
	fi
    fi
    if test "$found_xincludes" = "no"; then
	AC_MSG_RESULT([couldn't find any!])
    fi

    if test "$no_x" = yes; then
	AC_MSG_CHECKING([for X11 libraries])
	XLIBSW=nope
	dirs="/usr/unsupported/lib /usr/local/lib /usr/X386/lib /usr/X11R6/lib /usr/X11R5/lib /usr/lib/X11R5 /usr/lib/X11R4 /usr/openwin/lib /usr/X11/lib /usr/sww/X11/lib"
	for i in $dirs ; do
	    if test -r $i/libX11.a -o -r $i/libX11.so -o -r $i/libX11.sl -o -r $i/libX11.dylib; then
		AC_MSG_RESULT([$i])
		XLIBSW="-L$i -lX11"
		x_libraries="$i"
		break
	    fi
	done
    else
	if test "$x_libraries" = ""; then
	    XLIBSW=-lX11
	else
	    XLIBSW="-L$x_libraries -lX11"
	fi
    fi
    if test "$XLIBSW" = nope ; then
	AC_CHECK_LIB(Xwindow, XCreateWindow, XLIBSW=-lXwindow)
    fi
    if test "$XLIBSW" = nope ; then
	AC_MSG_RESULT([could not find any!  Using -lX11.])
	XLIBSW=-lX11
    fi
    # TEA specific:
    if test x"${XLIBSW}" != x ; then
	PKG_LIBS="${PKG_LIBS} ${XLIBSW}"
    fi
])

#--------------------------------------------------------------------
# TEA_BLOCKING_STYLE
#
#	The statements below check for systems where POSIX-style
#	non-blocking I/O (O_NONBLOCK) doesn't work or is unimplemented.
#	On these systems (mostly older ones), use the old BSD-style
#	FIONBIO approach instead.
#
# Arguments:
#	none
#
# Results:
#
#	Defines some of the following vars:
#		HAVE_SYS_IOCTL_H
#		HAVE_SYS_FILIO_H
#		USE_FIONBIO
#		O_NONBLOCK
#--------------------------------------------------------------------

AC_DEFUN([TEA_BLOCKING_STYLE], [
    AC_CHECK_HEADERS(sys/ioctl.h)
    AC_CHECK_HEADERS(sys/filio.h)
    TEA_CONFIG_SYSTEM
    AC_MSG_CHECKING([FIONBIO vs. O_NONBLOCK for nonblocking I/O])
    case $system in
	OSF*)
	    AC_DEFINE(USE_FIONBIO, 1, [Should we use FIONBIO?])
	    AC_MSG_RESULT([FIONBIO])
	    ;;
	*)
	    AC_MSG_RESULT([O_NONBLOCK])
	    ;;
    esac
])

#--------------------------------------------------------------------
# TEA_TIME_HANDLER
#
#	Checks how the system deals with time.h, what time structures
#	are used on the system, and what fields the structures have.
#
# Arguments:
#	none
#
# Results:
#
#	Defines some of the following vars:
#		USE_DELTA_FOR_TZ
#		HAVE_TM_GMTOFF
#		HAVE_TM_TZADJ
#		HAVE_TIMEZONE_VAR
#--------------------------------------------------------------------

AC_DEFUN([TEA_TIME_HANDLER], [
    AC_CHECK_HEADERS(sys/time.h)
    AC_HEADER_TIME
    AC_STRUCT_TIMEZONE

    AC_CHECK_FUNCS(gmtime_r localtime_r)

    AC_CACHE_CHECK([tm_tzadj in struct tm], tcl_cv_member_tm_tzadj, [
	AC_TRY_COMPILE([#include <time.h>], [struct tm tm; tm.tm_tzadj;],
	    tcl_cv_member_tm_tzadj=yes, tcl_cv_member_tm_tzadj=no)])
    if test $tcl_cv_member_tm_tzadj = yes ; then
	AC_DEFINE(HAVE_TM_TZADJ, 1, [Should we use the tm_tzadj field of struct tm?])
    fi

    AC_CACHE_CHECK([tm_gmtoff in struct tm], tcl_cv_member_tm_gmtoff, [
	AC_TRY_COMPILE([#include <time.h>], [struct tm tm; tm.tm_gmtoff;],
	    tcl_cv_member_tm_gmtoff=yes, tcl_cv_member_tm_gmtoff=no)])
    if test $tcl_cv_member_tm_gmtoff = yes ; then
	AC_DEFINE(HAVE_TM_GMTOFF, 1, [Should we use the tm_gmtoff field of struct tm?])
    fi

    #
    # Its important to include time.h in this check, as some systems
    # (like convex) have timezone functions, etc.
    #
    AC_CACHE_CHECK([long timezone variable], tcl_cv_timezone_long, [
	AC_TRY_COMPILE([#include <time.h>],
	    [extern long timezone;
	    timezone += 1;
	    exit (0);],
	    tcl_cv_timezone_long=yes, tcl_cv_timezone_long=no)])
    if test $tcl_cv_timezone_long = yes ; then
	AC_DEFINE(HAVE_TIMEZONE_VAR, 1, [Should we use the global timezone variable?])
    else
	#
	# On some systems (eg IRIX 6.2), timezone is a time_t and not a long.
	#
	AC_CACHE_CHECK([time_t timezone variable], tcl_cv_timezone_time, [
	    AC_TRY_COMPILE([#include <time.h>],
		[extern time_t timezone;
		timezone += 1;
		exit (0);],
		tcl_cv_timezone_time=yes, tcl_cv_timezone_time=no)])
	if test $tcl_cv_timezone_time = yes ; then
	    AC_DEFINE(HAVE_TIMEZONE_VAR, 1, [Should we use the global timezone variable?])
	fi
    fi
])

#--------------------------------------------------------------------
# TEA_BUGGY_STRTOD
#
#	Under Solaris 2.4, strtod returns the wrong value for the
#	terminating character under some conditions.  Check for this
#	and if the problem exists use a substitute procedure
#	"fixstrtod" (provided by Tcl) that corrects the error.
#	Also, on Compaq's Tru64 Unix 5.0,
#	strtod(" ") returns 0.0 instead of a failure to convert.
#
# Arguments:
#	none
#
# Results:
#
#	Might defines some of the following vars:
#		strtod (=fixstrtod)
#--------------------------------------------------------------------

AC_DEFUN([TEA_BUGGY_STRTOD], [
    AC_CHECK_FUNC(strtod, tcl_strtod=1, tcl_strtod=0)
    if test "$tcl_strtod" = 1; then
	AC_CACHE_CHECK([for Solaris2.4/Tru64 strtod bugs], tcl_cv_strtod_buggy,[
	    AC_TRY_RUN([
		extern double strtod();
		int main() {
		    char *infString="Inf", *nanString="NaN", *spaceString=" ";
		    char *term;
		    double value;
		    value = strtod(infString, &term);
		    if ((term != infString) && (term[-1] == 0)) {
			exit(1);
		    }
		    value = strtod(nanString, &term);
		    if ((term != nanString) && (term[-1] == 0)) {
			exit(1);
		    }
		    value = strtod(spaceString, &term);
		    if (term == (spaceString+1)) {
			exit(1);
		    }
		    exit(0);
		}], tcl_cv_strtod_buggy=ok, tcl_cv_strtod_buggy=buggy,
		    tcl_cv_strtod_buggy=buggy)])
	if test "$tcl_cv_strtod_buggy" = buggy; then
	    AC_LIBOBJ([fixstrtod])
	    USE_COMPAT=1
	    AC_DEFINE(strtod, fixstrtod, [Do we want to use the strtod() in compat?])
	fi
    fi
])

#--------------------------------------------------------------------
# TEA_TCL_LINK_LIBS
#
#	Search for the libraries needed to link the Tcl shell.
#	Things like the math library (-lm) and socket stuff (-lsocket vs.
#	-lnsl) are dealt with here.
#
# Arguments:
#	Requires the following vars to be set in the Makefile:
#		DL_LIBS (not in TEA, only needed in core)
#		LIBS
#		MATH_LIBS
#
# Results:
#
#	Substitutes the following vars:
#		TCL_LIBS
#		MATH_LIBS
#
#	Might append to the following vars:
#		LIBS
#
#	Might define the following vars:
#		HAVE_NET_ERRNO_H
#--------------------------------------------------------------------

AC_DEFUN([TEA_TCL_LINK_LIBS], [
    #--------------------------------------------------------------------
    # On a few very rare systems, all of the libm.a stuff is
    # already in libc.a.  Set compiler flags accordingly.
    # Also, Linux requires the "ieee" library for math to work
    # right (and it must appear before "-lm").
    #--------------------------------------------------------------------

    AC_CHECK_FUNC(sin, MATH_LIBS="", MATH_LIBS="-lm")
    AC_CHECK_LIB(ieee, main, [MATH_LIBS="-lieee $MATH_LIBS"])

    #--------------------------------------------------------------------
    # Interactive UNIX requires -linet instead of -lsocket, plus it
    # needs net/errno.h to define the socket-related error codes.
    #--------------------------------------------------------------------

    AC_CHECK_LIB(inet, main, [LIBS="$LIBS -linet"])
    AC_CHECK_HEADER(net/errno.h, [
	AC_DEFINE(HAVE_NET_ERRNO_H, 1, [Do we have <net/errno.h>?])])

    #--------------------------------------------------------------------
    #	Check for the existence of the -lsocket and -lnsl libraries.
    #	The order here is important, so that they end up in the right
    #	order in the command line generated by make.  Here are some
    #	special considerations:
    #	1. Use "connect" and "accept" to check for -lsocket, and
    #	   "gethostbyname" to check for -lnsl.
    #	2. Use each function name only once:  can't redo a check because
    #	   autoconf caches the results of the last check and won't redo it.
    #	3. Use -lnsl and -lsocket only if they supply procedures that
    #	   aren't already present in the normal libraries.  This is because
    #	   IRIX 5.2 has libraries, but they aren't needed and they're
    #	   bogus:  they goof up name resolution if used.
    #	4. On some SVR4 systems, can't use -lsocket without -lnsl too.
    #	   To get around this problem, check for both libraries together
    #	   if -lsocket doesn't work by itself.
    #--------------------------------------------------------------------

    tcl_checkBoth=0
    AC_CHECK_FUNC(connect, tcl_checkSocket=0, tcl_checkSocket=1)
    if test "$tcl_checkSocket" = 1; then
	AC_CHECK_FUNC(setsockopt, , [AC_CHECK_LIB(socket, setsockopt,
	    LIBS="$LIBS -lsocket", tcl_checkBoth=1)])
    fi
    if test "$tcl_checkBoth" = 1; then
	tk_oldLibs=$LIBS
	LIBS="$LIBS -lsocket -lnsl"
	AC_CHECK_FUNC(accept, tcl_checkNsl=0, [LIBS=$tk_oldLibs])
    fi
    AC_CHECK_FUNC(gethostbyname, , [AC_CHECK_LIB(nsl, gethostbyname,
	    [LIBS="$LIBS -lnsl"])])

    # TEA specific: Don't perform the eval of the libraries here because
    # DL_LIBS won't be set until we call TEA_CONFIG_CFLAGS

    TCL_LIBS='${DL_LIBS} ${LIBS} ${MATH_LIBS}'
    AC_SUBST(TCL_LIBS)
    AC_SUBST(MATH_LIBS)
])

#--------------------------------------------------------------------
# TEA_TCL_EARLY_FLAGS
#
#	Check for what flags are needed to be passed so the correct OS
#	features are available.
#
# Arguments:
#	None
#
# Results:
#
#	Might define the following vars:
#		_ISOC99_SOURCE
#		_LARGEFILE64_SOURCE
#		_LARGEFILE_SOURCE64
#--------------------------------------------------------------------

AC_DEFUN([TEA_TCL_EARLY_FLAG],[
    AC_CACHE_VAL([tcl_cv_flag_]translit($1,[A-Z],[a-z]),
	AC_TRY_COMPILE([$2], $3, [tcl_cv_flag_]translit($1,[A-Z],[a-z])=no,
	    AC_TRY_COMPILE([[#define ]$1[ 1
]$2], $3,
		[tcl_cv_flag_]translit($1,[A-Z],[a-z])=yes,
		[tcl_cv_flag_]translit($1,[A-Z],[a-z])=no)))
    if test ["x${tcl_cv_flag_]translit($1,[A-Z],[a-z])[}" = "xyes"] ; then
	AC_DEFINE($1, 1, [Add the ]$1[ flag when building])
	tcl_flags="$tcl_flags $1"
    fi
])

AC_DEFUN([TEA_TCL_EARLY_FLAGS],[
    AC_MSG_CHECKING([for required early compiler flags])
    tcl_flags=""
    TEA_TCL_EARLY_FLAG(_ISOC99_SOURCE,[#include <stdlib.h>],
	[char *p = (char *)strtoll; char *q = (char *)strtoull;])
    TEA_TCL_EARLY_FLAG(_LARGEFILE64_SOURCE,[#include <sys/stat.h>],
	[struct stat64 buf; int i = stat64("/", &buf);])
    TEA_TCL_EARLY_FLAG(_LARGEFILE_SOURCE64,[#include <sys/stat.h>],
	[char *p = (char *)open64;])
    if test "x${tcl_flags}" = "x" ; then
	AC_MSG_RESULT([none])
    else
	AC_MSG_RESULT([${tcl_flags}])
    fi
])

#--------------------------------------------------------------------
# TEA_TCL_64BIT_FLAGS
#
#	Check for what is defined in the way of 64-bit features.
#
# Arguments:
#	None
#
# Results:
#
#	Might define the following vars:
#		TCL_WIDE_INT_IS_LONG
#		TCL_WIDE_INT_TYPE
#		HAVE_STRUCT_DIRENT64
#		HAVE_STRUCT_STAT64
#		HAVE_TYPE_OFF64_T
#--------------------------------------------------------------------

AC_DEFUN([TEA_TCL_64BIT_FLAGS], [
    AC_MSG_CHECKING([for 64-bit integer type])
    AC_CACHE_VAL(tcl_cv_type_64bit,[
	tcl_cv_type_64bit=none
	# See if the compiler knows natively about __int64
	AC_TRY_COMPILE(,[__int64 value = (__int64) 0;],
	    tcl_type_64bit=__int64, tcl_type_64bit="long long")
	# See if we should use long anyway  Note that we substitute in the
	# type that is our current guess for a 64-bit type inside this check
	# program, so it should be modified only carefully...
        AC_TRY_COMPILE(,[switch (0) {
            case 1: case (sizeof(]${tcl_type_64bit}[)==sizeof(long)): ;
        }],tcl_cv_type_64bit=${tcl_type_64bit})])
    if test "${tcl_cv_type_64bit}" = none ; then
	AC_DEFINE(TCL_WIDE_INT_IS_LONG, 1, [Are wide integers to be implemented with C 'long's?])
	AC_MSG_RESULT([using long])
    elif test "${tcl_cv_type_64bit}" = "__int64" \
		-a "${TEA_PLATFORM}" = "windows" ; then
	# TEA specific: We actually want to use the default tcl.h checks in
	# this case to handle both TCL_WIDE_INT_TYPE and TCL_LL_MODIFIER*
	AC_MSG_RESULT([using Tcl header defaults])
    else
	AC_DEFINE_UNQUOTED(TCL_WIDE_INT_TYPE,${tcl_cv_type_64bit},
	    [What type should be used to define wide integers?])
	AC_MSG_RESULT([${tcl_cv_type_64bit}])

	# Now check for auxiliary declarations
	AC_CACHE_CHECK([for struct dirent64], tcl_cv_struct_dirent64,[
	    AC_TRY_COMPILE([#include <sys/types.h>
#include <dirent.h>],[struct dirent64 p;],
		tcl_cv_struct_dirent64=yes,tcl_cv_struct_dirent64=no)])
	if test "x${tcl_cv_struct_dirent64}" = "xyes" ; then
	    AC_DEFINE(HAVE_STRUCT_DIRENT64, 1, [Is 'struct dirent64' in <sys/types.h>?])
	fi

	AC_CACHE_CHECK([for struct stat64], tcl_cv_struct_stat64,[
	    AC_TRY_COMPILE([#include <sys/stat.h>],[struct stat64 p;
],
		tcl_cv_struct_stat64=yes,tcl_cv_struct_stat64=no)])
	if test "x${tcl_cv_struct_stat64}" = "xyes" ; then
	    AC_DEFINE(HAVE_STRUCT_STAT64, 1, [Is 'struct stat64' in <sys/stat.h>?])
	fi

	AC_CHECK_FUNCS(open64 lseek64)
	AC_MSG_CHECKING([for off64_t])
	AC_CACHE_VAL(tcl_cv_type_off64_t,[
	    AC_TRY_COMPILE([#include <sys/types.h>],[off64_t offset;
],
		tcl_cv_type_off64_t=yes,tcl_cv_type_off64_t=no)])
	dnl Define HAVE_TYPE_OFF64_T only when the off64_t type and the
	dnl functions lseek64 and open64 are defined.
	if test "x${tcl_cv_type_off64_t}" = "xyes" && \
	        test "x${ac_cv_func_lseek64}" = "xyes" && \
	        test "x${ac_cv_func_open64}" = "xyes" ; then
	    AC_DEFINE(HAVE_TYPE_OFF64_T, 1, [Is off64_t in <sys/types.h>?])
	    AC_MSG_RESULT([yes])
	else
	    AC_MSG_RESULT([no])
	fi
    fi
])

##
## Here ends the standard Tcl configuration bits and starts the
## TEA specific functions
##

#------------------------------------------------------------------------
# TEA_INIT --
#
#	Init various Tcl Extension Architecture (TEA) variables.
#	This should be the first called TEA_* macro.
#
# Arguments:
#	none
#
# Results:
#
#	Defines and substs the following vars:
#		CYGPATH
#		EXEEXT
#	Defines only:
#		TEA_VERSION
#		TEA_INITED
#		TEA_PLATFORM (windows or unix)
#
# "cygpath" is used on windows to generate native path names for include
# files. These variables should only be used with the compiler and linker
# since they generate native path names.
#
# EXEEXT
#	Select the executable extension based on the host type.  This
#	is a lightweight replacement for AC_EXEEXT that doesn't require
#	a compiler.
#------------------------------------------------------------------------

AC_DEFUN([TEA_INIT], [
    # TEA extensions pass this us the version of TEA they think they
    # are compatible with.
    TEA_VERSION="3.9"

    AC_MSG_CHECKING([for correct TEA configuration])
    if test x"${PACKAGE_NAME}" = x ; then
	AC_MSG_ERROR([
The PACKAGE_NAME variable must be defined by your TEA configure.in])
    fi
    if test x"$1" = x ; then
	AC_MSG_ERROR([
TEA version not specified.])
    elif test "$1" != "${TEA_VERSION}" ; then
	AC_MSG_RESULT([warning: requested TEA version "$1", have "${TEA_VERSION}"])
    else
	AC_MSG_RESULT([ok (TEA ${TEA_VERSION})])
    fi

    # If the user did not set CFLAGS, set it now to keep macros
    # like AC_PROG_CC and AC_TRY_COMPILE from adding "-g -O2".
    if test "${CFLAGS+set}" != "set" ; then
	CFLAGS=""
    fi

    case "`uname -s`" in
	*win32*|*WIN32*|*MINGW32_*)
	    AC_CHECK_PROG(CYGPATH, cygpath, cygpath -w, echo)
	    EXEEXT=".exe"
	    TEA_PLATFORM="windows"
	    ;;
	*CYGWIN_*)
	    CYGPATH=echo
	    EXEEXT=".exe"
	    # TEA_PLATFORM is determined later in LOAD_TCLCONFIG
	    ;;
	*)
	    CYGPATH=echo
	    # Maybe we are cross-compiling....
	    case ${host_alias} in
		*mingw32*)
		EXEEXT=".exe"
		TEA_PLATFORM="windows"
		;;
	    *)
		EXEEXT=""
		TEA_PLATFORM="unix"
		;;
	    esac
	    ;;
    esac

    # Check if exec_prefix is set. If not use fall back to prefix.
    # Note when adjusted, so that TEA_PREFIX can correct for this.
    # This is needed for recursive configures, since autoconf propagates
    # $prefix, but not $exec_prefix (doh!).
    if test x$exec_prefix = xNONE ; then
	exec_prefix_default=yes
	exec_prefix=$prefix
    fi

    AC_MSG_NOTICE([configuring ${PACKAGE_NAME} ${PACKAGE_VERSION}])

    AC_SUBST(EXEEXT)
    AC_SUBST(CYGPATH)

    # This package name must be replaced statically for AC_SUBST to work
    AC_SUBST(PKG_LIB_FILE)
    # Substitute STUB_LIB_FILE in case package creates a stub library too.
    AC_SUBST(PKG_STUB_LIB_FILE)

    # We AC_SUBST these here to ensure they are subst'ed,
    # in case the user doesn't call TEA_ADD_...
    AC_SUBST(PKG_STUB_SOURCES)
    AC_SUBST(PKG_STUB_OBJECTS)
    AC_SUBST(PKG_TCL_SOURCES)
    AC_SUBST(PKG_HEADERS)
    AC_SUBST(PKG_INCLUDES)
    AC_SUBST(PKG_LIBS)
    AC_SUBST(PKG_CFLAGS)
])

#------------------------------------------------------------------------
# TEA_ADD_SOURCES --
#
#	Specify one or more source files.  Users should check for
#	the right platform before adding to their list.
#	It is not important to specify the directory, as long as it is
#	in the generic, win or unix subdirectory of $(srcdir).
#
# Arguments:
#	one or more file names
#
# Results:
#
#	Defines and substs the following vars:
#		PKG_SOURCES
#		PKG_OBJECTS
#------------------------------------------------------------------------
AC_DEFUN([TEA_ADD_SOURCES], [
    vars="$@"
    for i in $vars; do
	case $i in
	    [\$]*)
		# allow $-var names
		PKG_SOURCES="$PKG_SOURCES $i"
		PKG_OBJECTS="$PKG_OBJECTS $i"
		;;
	    *)
		# check for existence - allows for generic/win/unix VPATH
		# To add more dirs here (like 'src'), you have to update VPATH
		# in Makefile.in as well
		if test ! -f "${srcdir}/$i" -a ! -f "${srcdir}/generic/$i" \
		    -a ! -f "${srcdir}/win/$i" -a ! -f "${srcdir}/unix/$i" \
		    -a ! -f "${srcdir}/macosx/$i" \
		    ; then
		    AC_MSG_ERROR([could not find source file '$i'])
		fi
		PKG_SOURCES="$PKG_SOURCES $i"
		# this assumes it is in a VPATH dir
		i=`basename $i`
		# handle user calling this before or after TEA_SETUP_COMPILER
		if test x"${OBJEXT}" != x ; then
		    j="`echo $i | sed -e 's/\.[[^.]]*$//'`.${OBJEXT}"
		else
		    j="`echo $i | sed -e 's/\.[[^.]]*$//'`.\${OBJEXT}"
		fi
		PKG_OBJECTS="$PKG_OBJECTS $j"
		;;
	esac
    done
    AC_SUBST(PKG_SOURCES)
    AC_SUBST(PKG_OBJECTS)
])

#------------------------------------------------------------------------
# TEA_ADD_STUB_SOURCES --
#
#	Specify one or more source files.  Users should check for
#	the right platform before adding to their list.
#	It is not important to specify the directory, as long as it is
#	in the generic, win or unix subdirectory of $(srcdir).
#
# Arguments:
#	one or more file names
#
# Results:
#
#	Defines and substs the following vars:
#		PKG_STUB_SOURCES
#		PKG_STUB_OBJECTS
#------------------------------------------------------------------------
AC_DEFUN([TEA_ADD_STUB_SOURCES], [
    vars="$@"
    for i in $vars; do
	# check for existence - allows for generic/win/unix VPATH
	if test ! -f "${srcdir}/$i" -a ! -f "${srcdir}/generic/$i" \
	    -a ! -f "${srcdir}/win/$i" -a ! -f "${srcdir}/unix/$i" \
	    -a ! -f "${srcdir}/macosx/$i" \
	    ; then
	    AC_MSG_ERROR([could not find stub source file '$i'])
	fi
	PKG_STUB_SOURCES="$PKG_STUB_SOURCES $i"
	# this assumes it is in a VPATH dir
	i=`basename $i`
	# handle user calling this before or after TEA_SETUP_COMPILER
	if test x"${OBJEXT}" != x ; then
	    j="`echo $i | sed -e 's/\.[[^.]]*$//'`.${OBJEXT}"
	else
	    j="`echo $i | sed -e 's/\.[[^.]]*$//'`.\${OBJEXT}"
	fi
	PKG_STUB_OBJECTS="$PKG_STUB_OBJECTS $j"
    done
    AC_SUBST(PKG_STUB_SOURCES)
    AC_SUBST(PKG_STUB_OBJECTS)
])

#------------------------------------------------------------------------
# TEA_ADD_TCL_SOURCES --
#
#	Specify one or more Tcl source files.  These should be platform
#	independent runtime files.
#
# Arguments:
#	one or more file names
#
# Results:
#
#	Defines and substs the following vars:
#		PKG_TCL_SOURCES
#------------------------------------------------------------------------
AC_DEFUN([TEA_ADD_TCL_SOURCES], [
    vars="$@"
    for i in $vars; do
	# check for existence, be strict because it is installed
	if test ! -f "${srcdir}/$i" ; then
	    AC_MSG_ERROR([could not find tcl source file '${srcdir}/$i'])
	fi
	PKG_TCL_SOURCES="$PKG_TCL_SOURCES $i"
    done
    AC_SUBST(PKG_TCL_SOURCES)
])

#------------------------------------------------------------------------
# TEA_ADD_HEADERS --
#
#	Specify one or more source headers.  Users should check for
#	the right platform before adding to their list.
#
# Arguments:
#	one or more file names
#
# Results:
#
#	Defines and substs the following vars:
#		PKG_HEADERS
#------------------------------------------------------------------------
AC_DEFUN([TEA_ADD_HEADERS], [
    vars="$@"
    for i in $vars; do
	# check for existence, be strict because it is installed
	if test ! -f "${srcdir}/$i" ; then
	    AC_MSG_ERROR([could not find header file '${srcdir}/$i'])
	fi
	PKG_HEADERS="$PKG_HEADERS $i"
    done
    AC_SUBST(PKG_HEADERS)
])

#------------------------------------------------------------------------
# TEA_ADD_INCLUDES --
#
#	Specify one or more include dirs.  Users should check for
#	the right platform before adding to their list.
#
# Arguments:
#	one or more file names
#
# Results:
#
#	Defines and substs the following vars:
#		PKG_INCLUDES
#------------------------------------------------------------------------
AC_DEFUN([TEA_ADD_INCLUDES], [
    vars="$@"
    for i in $vars; do
	PKG_INCLUDES="$PKG_INCLUDES $i"
    done
    AC_SUBST(PKG_INCLUDES)
])

#------------------------------------------------------------------------
# TEA_ADD_LIBS --
#
#	Specify one or more libraries.  Users should check for
#	the right platform before adding to their list.  For Windows,
#	libraries provided in "foo.lib" format will be converted to
#	"-lfoo" when using GCC (mingw).
#
# Arguments:
#	one or more file names
#
# Results:
#
#	Defines and substs the following vars:
#		PKG_LIBS
#------------------------------------------------------------------------
AC_DEFUN([TEA_ADD_LIBS], [
    vars="$@"
    for i in $vars; do
	if test "${TEA_PLATFORM}" = "windows" -a "$GCC" = "yes" ; then
	    # Convert foo.lib to -lfoo for GCC.  No-op if not *.lib
	    i=`echo "$i" | sed -e 's/^\([[^-]].*\)\.lib[$]/-l\1/i'`
	fi
	PKG_LIBS="$PKG_LIBS $i"
    done
    AC_SUBST(PKG_LIBS)
])

#------------------------------------------------------------------------
# TEA_ADD_CFLAGS --
#
#	Specify one or more CFLAGS.  Users should check for
#	the right platform before adding to their list.
#
# Arguments:
#	one or more file names
#
# Results:
#
#	Defines and substs the following vars:
#		PKG_CFLAGS
#------------------------------------------------------------------------
AC_DEFUN([TEA_ADD_CFLAGS], [
    PKG_CFLAGS="$PKG_CFLAGS $@"
    AC_SUBST(PKG_CFLAGS)
])

#------------------------------------------------------------------------
# TEA_ADD_CLEANFILES --
#
#	Specify one or more CLEANFILES.
#
# Arguments:
#	one or more file names to clean target
#
# Results:
#
#	Appends to CLEANFILES, already defined for subst in LOAD_TCLCONFIG
#------------------------------------------------------------------------
AC_DEFUN([TEA_ADD_CLEANFILES], [
    CLEANFILES="$CLEANFILES $@"
])

#------------------------------------------------------------------------
# TEA_PREFIX --
#
#	Handle the --prefix=... option by defaulting to what Tcl gave
#
# Arguments:
#	none
#
# Results:
#
#	If --prefix or --exec-prefix was not specified, $prefix and
#	$exec_prefix will be set to the values given to Tcl when it was
#	configured.
#------------------------------------------------------------------------
AC_DEFUN([TEA_PREFIX], [
    if test "${prefix}" = "NONE"; then
	prefix_default=yes
	if test x"${TCL_PREFIX}" != x; then
	    AC_MSG_NOTICE([--prefix defaulting to TCL_PREFIX ${TCL_PREFIX}])
	    prefix=${TCL_PREFIX}
	else
	    AC_MSG_NOTICE([--prefix defaulting to /usr/local])
	    prefix=/usr/local
	fi
    fi
    if test "${exec_prefix}" = "NONE" -a x"${prefix_default}" = x"yes" \
	-o x"${exec_prefix_default}" = x"yes" ; then
	if test x"${TCL_EXEC_PREFIX}" != x; then
	    AC_MSG_NOTICE([--exec-prefix defaulting to TCL_EXEC_PREFIX ${TCL_EXEC_PREFIX}])
	    exec_prefix=${TCL_EXEC_PREFIX}
	else
	    AC_MSG_NOTICE([--exec-prefix defaulting to ${prefix}])
	    exec_prefix=$prefix
	fi
    fi
])

#------------------------------------------------------------------------
# TEA_SETUP_COMPILER_CC --
#
#	Do compiler checks the way we want.  This is just a replacement
#	for AC_PROG_CC in TEA configure.in files to make them cleaner.
#
# Arguments:
#	none
#
# Results:
#
#	Sets up CC var and other standard bits we need to make executables.
#------------------------------------------------------------------------
AC_DEFUN([TEA_SETUP_COMPILER_CC], [
    # Don't put any macros that use the compiler (e.g. AC_TRY_COMPILE)
    # in this macro, they need to go into TEA_SETUP_COMPILER instead.

    AC_PROG_CC
    AC_PROG_CPP

    INSTALL="\$(SHELL) \$(srcdir)/tclconfig/install-sh -c"
    AC_SUBST(INSTALL)
    INSTALL_DATA="\${INSTALL} -m 644"
    AC_SUBST(INSTALL_DATA)
    INSTALL_PROGRAM="\${INSTALL}"
    AC_SUBST(INSTALL_PROGRAM)
    INSTALL_SCRIPT="\${INSTALL}"
    AC_SUBST(INSTALL_SCRIPT)

    #--------------------------------------------------------------------
    # Checks to see if the make program sets the $MAKE variable.
    #--------------------------------------------------------------------

    AC_PROG_MAKE_SET

    #--------------------------------------------------------------------
    # Find ranlib
    #--------------------------------------------------------------------

    AC_CHECK_TOOL(RANLIB, ranlib)

    #--------------------------------------------------------------------
    # Determines the correct binary file extension (.o, .obj, .exe etc.)
    #--------------------------------------------------------------------

    AC_OBJEXT
    AC_EXEEXT
])

#------------------------------------------------------------------------
# TEA_SETUP_COMPILER --
#
#	Do compiler checks that use the compiler.  This must go after
#	TEA_SETUP_COMPILER_CC, which does the actual compiler check.
#
# Arguments:
#	none
#
# Results:
#
#	Sets up CC var and other standard bits we need to make executables.
#------------------------------------------------------------------------
AC_DEFUN([TEA_SETUP_COMPILER], [
    # Any macros that use the compiler (e.g. AC_TRY_COMPILE) have to go here.
    AC_REQUIRE([TEA_SETUP_COMPILER_CC])

    #------------------------------------------------------------------------
    # If we're using GCC, see if the compiler understands -pipe. If so, use it.
    # It makes compiling go faster.  (This is only a performance feature.)
    #------------------------------------------------------------------------

    if test -z "$no_pipe" -a -n "$GCC"; then
	AC_CACHE_CHECK([if the compiler understands -pipe],
	    tcl_cv_cc_pipe, [
	    hold_cflags=$CFLAGS; CFLAGS="$CFLAGS -pipe"
	    AC_TRY_COMPILE(,, tcl_cv_cc_pipe=yes, tcl_cv_cc_pipe=no)
	    CFLAGS=$hold_cflags])
	if test $tcl_cv_cc_pipe = yes; then
	    CFLAGS="$CFLAGS -pipe"
	fi
    fi

    #--------------------------------------------------------------------
    # Common compiler flag setup
    #--------------------------------------------------------------------

    AC_C_BIGENDIAN
    if test "${TEA_PLATFORM}" = "unix" ; then
	TEA_TCL_LINK_LIBS
	TEA_MISSING_POSIX_HEADERS
	# Let the user call this, because if it triggers, they will
	# need a compat/strtod.c that is correct.  Users can also
	# use Tcl_GetDouble(FromObj) instead.
	#TEA_BUGGY_STRTOD
    fi
])

#------------------------------------------------------------------------
# TEA_MAKE_LIB --
#
#	Generate a line that can be used to build a shared/unshared library
#	in a platform independent manner.
#
# Arguments:
#	none
#
#	Requires:
#
# Results:
#
#	Defines the following vars:
#	CFLAGS -	Done late here to note disturb other AC macros
#       MAKE_LIB -      Command to execute to build the Tcl library;
#                       differs depending on whether or not Tcl is being
#                       compiled as a shared library.
#	MAKE_SHARED_LIB	Makefile rule for building a shared library
#	MAKE_STATIC_LIB	Makefile rule for building a static library
#	MAKE_STUB_LIB	Makefile rule for building a stub library
#	VC_MANIFEST_EMBED_DLL Makefile rule for embedded VC manifest in DLL
#	VC_MANIFEST_EMBED_EXE Makefile rule for embedded VC manifest in EXE
#------------------------------------------------------------------------

AC_DEFUN([TEA_MAKE_LIB], [
    if test "${TEA_PLATFORM}" = "windows" -a "$GCC" != "yes"; then
	MAKE_STATIC_LIB="\${STLIB_LD} -out:\[$]@ \$(PKG_OBJECTS)"
	MAKE_SHARED_LIB="\${SHLIB_LD} \${SHLIB_LD_LIBS} \${LDFLAGS_DEFAULT} -out:\[$]@ \$(PKG_OBJECTS)"
	AC_EGREP_CPP([manifest needed], [
#if defined(_MSC_VER) && _MSC_VER >= 1400
print("manifest needed")
#endif
	], [
	# Could do a CHECK_PROG for mt, but should always be with MSVC8+
	VC_MANIFEST_EMBED_DLL="if test -f \[$]@.manifest ; then mt.exe -nologo -manifest \[$]@.manifest -outputresource:\[$]@\;2 ; fi"
	VC_MANIFEST_EMBED_EXE="if test -f \[$]@.manifest ; then mt.exe -nologo -manifest \[$]@.manifest -outputresource:\[$]@\;1 ; fi"
	MAKE_SHARED_LIB="${MAKE_SHARED_LIB} ; ${VC_MANIFEST_EMBED_DLL}"
	TEA_ADD_CLEANFILES([*.manifest])
	])
	MAKE_STUB_LIB="\${STLIB_LD} -nodefaultlib -out:\[$]@ \$(PKG_STUB_OBJECTS)"
    else
	MAKE_STATIC_LIB="\${STLIB_LD} \[$]@ \$(PKG_OBJECTS)"
	MAKE_SHARED_LIB="\${SHLIB_LD} -o \[$]@ \$(PKG_OBJECTS) \${SHLIB_LD_LIBS}"
	MAKE_STUB_LIB="\${STLIB_LD} \[$]@ \$(PKG_STUB_OBJECTS)"
    fi

    if test "${SHARED_BUILD}" = "1" ; then
	MAKE_LIB="${MAKE_SHARED_LIB} "
    else
	MAKE_LIB="${MAKE_STATIC_LIB} "
    fi

    #--------------------------------------------------------------------
    # Shared libraries and static libraries have different names.
    # Use the double eval to make sure any variables in the suffix is
    # substituted. (@@@ Might not be necessary anymore)
    #--------------------------------------------------------------------

    if test "${TEA_PLATFORM}" = "windows" ; then
	if test "${SHARED_BUILD}" = "1" ; then
	    # We force the unresolved linking of symbols that are really in
	    # the private libraries of Tcl and Tk.
	    if test x"${TK_BIN_DIR}" != x ; then
		SHLIB_LD_LIBS="${SHLIB_LD_LIBS} \"`${CYGPATH} ${TK_BIN_DIR}/${TK_STUB_LIB_FILE}`\""
	    fi
	    SHLIB_LD_LIBS="${SHLIB_LD_LIBS} \"`${CYGPATH} ${TCL_BIN_DIR}/${TCL_STUB_LIB_FILE}`\""
	    if test "$GCC" = "yes"; then
		SHLIB_LD_LIBS="${SHLIB_LD_LIBS} -static-libgcc"
	    fi
	    eval eval "PKG_LIB_FILE=${PACKAGE_NAME}${SHARED_LIB_SUFFIX}"
	else
	    eval eval "PKG_LIB_FILE=${PACKAGE_NAME}${UNSHARED_LIB_SUFFIX}"
	    if test "$GCC" = "yes"; then
		PKG_LIB_FILE=lib${PKG_LIB_FILE}
	    fi
	fi
	# Some packages build their own stubs libraries
	eval eval "PKG_STUB_LIB_FILE=${PACKAGE_NAME}stub${UNSHARED_LIB_SUFFIX}"
	if test "$GCC" = "yes"; then
	    PKG_STUB_LIB_FILE=lib${PKG_STUB_LIB_FILE}
	fi
	# These aren't needed on Windows (either MSVC or gcc)
	RANLIB=:
	RANLIB_STUB=:
    else
	RANLIB_STUB="${RANLIB}"
	if test "${SHARED_BUILD}" = "1" ; then
	    SHLIB_LD_LIBS="${SHLIB_LD_LIBS} ${TCL_STUB_LIB_SPEC}"
	    if test x"${TK_BIN_DIR}" != x ; then
		SHLIB_LD_LIBS="${SHLIB_LD_LIBS} ${TK_STUB_LIB_SPEC}"
	    fi
	    eval eval "PKG_LIB_FILE=lib${PACKAGE_NAME}${SHARED_LIB_SUFFIX}"
	    RANLIB=:
	else
	    eval eval "PKG_LIB_FILE=lib${PACKAGE_NAME}${UNSHARED_LIB_SUFFIX}"
	fi
	# Some packages build their own stubs libraries
	eval eval "PKG_STUB_LIB_FILE=lib${PACKAGE_NAME}stub${UNSHARED_LIB_SUFFIX}"
    fi

    # These are escaped so that only CFLAGS is picked up at configure time.
    # The other values will be substituted at make time.
    CFLAGS="${CFLAGS} \${CFLAGS_DEFAULT} \${CFLAGS_WARNING}"
    if test "${SHARED_BUILD}" = "1" ; then
	CFLAGS="${CFLAGS} \${SHLIB_CFLAGS}"
    fi

    AC_SUBST(MAKE_LIB)
    AC_SUBST(MAKE_SHARED_LIB)
    AC_SUBST(MAKE_STATIC_LIB)
    AC_SUBST(MAKE_STUB_LIB)
    AC_SUBST(RANLIB_STUB)
    AC_SUBST(VC_MANIFEST_EMBED_DLL)
    AC_SUBST(VC_MANIFEST_EMBED_EXE)
])

#------------------------------------------------------------------------
# TEA_LIB_SPEC --
#
#	Compute the name of an existing object library located in libdir
#	from the given base name and produce the appropriate linker flags.
#
# Arguments:
#	basename	The base name of the library without version
#			numbers, extensions, or "lib" prefixes.
#	extra_dir	Extra directory in which to search for the
#			library.  This location is used first, then
#			$prefix/$exec-prefix, then some defaults.
#
# Requires:
#	TEA_INIT and TEA_PREFIX must be called first.
#
# Results:
#
#	Defines the following vars:
#		${basename}_LIB_NAME	The computed library name.
#		${basename}_LIB_SPEC	The computed linker flags.
#------------------------------------------------------------------------

AC_DEFUN([TEA_LIB_SPEC], [
    AC_MSG_CHECKING([for $1 library])

    # Look in exec-prefix for the library (defined by TEA_PREFIX).

    tea_lib_name_dir="${exec_prefix}/lib"

    # Or in a user-specified location.

    if test x"$2" != x ; then
	tea_extra_lib_dir=$2
    else
	tea_extra_lib_dir=NONE
    fi

    for i in \
	    `ls -dr ${tea_extra_lib_dir}/$1[[0-9]]*.lib 2>/dev/null ` \
	    `ls -dr ${tea_extra_lib_dir}/lib$1[[0-9]]* 2>/dev/null ` \
	    `ls -dr ${tea_lib_name_dir}/$1[[0-9]]*.lib 2>/dev/null ` \
	    `ls -dr ${tea_lib_name_dir}/lib$1[[0-9]]* 2>/dev/null ` \
	    `ls -dr /usr/lib/$1[[0-9]]*.lib 2>/dev/null ` \
	    `ls -dr /usr/lib/lib$1[[0-9]]* 2>/dev/null ` \
	    `ls -dr /usr/lib64/$1[[0-9]]*.lib 2>/dev/null ` \
	    `ls -dr /usr/lib64/lib$1[[0-9]]* 2>/dev/null ` \
	    `ls -dr /usr/local/lib/$1[[0-9]]*.lib 2>/dev/null ` \
	    `ls -dr /usr/local/lib/lib$1[[0-9]]* 2>/dev/null ` ; do
	if test -f "$i" ; then
	    tea_lib_name_dir=`dirname $i`
	    $1_LIB_NAME=`basename $i`
	    $1_LIB_PATH_NAME=$i
	    break
	fi
    done

    if test "${TEA_PLATFORM}" = "windows"; then
	$1_LIB_SPEC=\"`${CYGPATH} ${$1_LIB_PATH_NAME} 2>/dev/null`\"
    else
	# Strip off the leading "lib" and trailing ".a" or ".so"

	tea_lib_name_lib=`echo ${$1_LIB_NAME}|sed -e 's/^lib//' -e 's/\.[[^.]]*$//' -e 's/\.so.*//'`
	$1_LIB_SPEC="-L${tea_lib_name_dir} -l${tea_lib_name_lib}"
    fi

    if test "x${$1_LIB_NAME}" = x ; then
	AC_MSG_ERROR([not found])
    else
	AC_MSG_RESULT([${$1_LIB_SPEC}])
    fi
])

#------------------------------------------------------------------------
# TEA_PRIVATE_TCL_HEADERS --
#
#	Locate the private Tcl include files
#
# Arguments:
#
#	Requires:
#		TCL_SRC_DIR	Assumes that TEA_LOAD_TCLCONFIG has
#				already been called.
#
# Results:
#
#	Substitutes the following vars:
#		TCL_TOP_DIR_NATIVE
#		TCL_INCLUDES
#------------------------------------------------------------------------

AC_DEFUN([TEA_PRIVATE_TCL_HEADERS], [
    # Allow for --with-tclinclude to take effect and define ${ac_cv_c_tclh}
    AC_REQUIRE([TEA_PUBLIC_TCL_HEADERS])
    AC_MSG_CHECKING([for Tcl private include files])

    TCL_SRC_DIR_NATIVE=`${CYGPATH} ${TCL_SRC_DIR}`
    TCL_TOP_DIR_NATIVE=\"${TCL_SRC_DIR_NATIVE}\"

    # Check to see if tcl<Plat>Port.h isn't already with the public headers
    # Don't look for tclInt.h because that resides with tcl.h in the core
    # sources, but the <plat>Port headers are in a different directory
    if test "${TEA_PLATFORM}" = "windows" -a \
	-f "${ac_cv_c_tclh}/tclWinPort.h"; then
	result="private headers found with public headers"
    elif test "${TEA_PLATFORM}" = "unix" -a \
	-f "${ac_cv_c_tclh}/tclUnixPort.h"; then
	result="private headers found with public headers"
    else
	TCL_GENERIC_DIR_NATIVE=\"${TCL_SRC_DIR_NATIVE}/generic\"
	if test "${TEA_PLATFORM}" = "windows"; then
	    TCL_PLATFORM_DIR_NATIVE=\"${TCL_SRC_DIR_NATIVE}/win\"
	else
	    TCL_PLATFORM_DIR_NATIVE=\"${TCL_SRC_DIR_NATIVE}/unix\"
	fi
	# Overwrite the previous TCL_INCLUDES as this should capture both
	# public and private headers in the same set.
	# We want to ensure these are substituted so as not to require
	# any *_NATIVE vars be defined in the Makefile
	TCL_INCLUDES="-I${TCL_GENERIC_DIR_NATIVE} -I${TCL_PLATFORM_DIR_NATIVE}"
	if test "`uname -s`" = "Darwin"; then
            # If Tcl was built as a framework, attempt to use
            # the framework's Headers and PrivateHeaders directories
            case ${TCL_DEFS} in
	    	*TCL_FRAMEWORK*)
		    if test -d "${TCL_BIN_DIR}/Headers" -a \
			    -d "${TCL_BIN_DIR}/PrivateHeaders"; then
			TCL_INCLUDES="-I\"${TCL_BIN_DIR}/Headers\" -I\"${TCL_BIN_DIR}/PrivateHeaders\" ${TCL_INCLUDES}"
		    else
			TCL_INCLUDES="${TCL_INCLUDES} ${TCL_INCLUDE_SPEC} `echo "${TCL_INCLUDE_SPEC}" | sed -e 's/Headers/PrivateHeaders/'`"
		    fi
	            ;;
	    esac
	    result="Using ${TCL_INCLUDES}"
	else
	    if test ! -f "${TCL_SRC_DIR}/generic/tclInt.h" ; then
		AC_MSG_ERROR([Cannot find private header tclInt.h in ${TCL_SRC_DIR}])
	    fi
	    result="Using srcdir found in tclConfig.sh: ${TCL_SRC_DIR}"
	fi
    fi

    AC_SUBST(TCL_TOP_DIR_NATIVE)

    AC_SUBST(TCL_INCLUDES)
    AC_MSG_RESULT([${result}])
])

#------------------------------------------------------------------------
# TEA_PUBLIC_TCL_HEADERS --
#
#	Locate the installed public Tcl header files
#
# Arguments:
#	None.
#
# Requires:
#	CYGPATH must be set
#
# Results:
#
#	Adds a --with-tclinclude switch to configure.
#	Result is cached.
#
#	Substitutes the following vars:
#		TCL_INCLUDES
#------------------------------------------------------------------------

AC_DEFUN([TEA_PUBLIC_TCL_HEADERS], [
    AC_MSG_CHECKING([for Tcl public headers])

    AC_ARG_WITH(tclinclude, [  --with-tclinclude       directory containing the public Tcl header files], with_tclinclude=${withval})

    AC_CACHE_VAL(ac_cv_c_tclh, [
	# Use the value from --with-tclinclude, if it was given

	if test x"${with_tclinclude}" != x ; then
	    if test -f "${with_tclinclude}/tcl.h" ; then
		ac_cv_c_tclh=${with_tclinclude}
	    else
		AC_MSG_ERROR([${with_tclinclude} directory does not contain tcl.h])
	    fi
	else
	    list=""
	    if test "`uname -s`" = "Darwin"; then
		# If Tcl was built as a framework, attempt to use
		# the framework's Headers directory
		case ${TCL_DEFS} in
		    *TCL_FRAMEWORK*)
			list="`ls -d ${TCL_BIN_DIR}/Headers 2>/dev/null`"
			;;
		esac
	    fi

	    # Look in the source dir only if Tcl is not installed,
	    # and in that situation, look there before installed locations.
	    if test -f "${TCL_BIN_DIR}/Makefile" ; then
		list="$list `ls -d ${TCL_SRC_DIR}/generic 2>/dev/null`"
	    fi

	    # Check order: pkg --prefix location, Tcl's --prefix location,
	    # relative to directory of tclConfig.sh.

	    eval "temp_includedir=${includedir}"
	    list="$list \
		`ls -d ${temp_includedir}        2>/dev/null` \
		`ls -d ${TCL_PREFIX}/include     2>/dev/null` \
		`ls -d ${TCL_BIN_DIR}/../include 2>/dev/null`"
	    if test "${TEA_PLATFORM}" != "windows" -o "$GCC" = "yes"; then
		list="$list /usr/local/include /usr/include"
		if test x"${TCL_INCLUDE_SPEC}" != x ; then
		    d=`echo "${TCL_INCLUDE_SPEC}" | sed -e 's/^-I//'`
		    list="$list `ls -d ${d} 2>/dev/null`"
		fi
	    fi
	    for i in $list ; do
		if test -f "$i/tcl.h" ; then
		    ac_cv_c_tclh=$i
		    break
		fi
	    done
	fi
    ])

    # Print a message based on how we determined the include path

    if test x"${ac_cv_c_tclh}" = x ; then
	AC_MSG_ERROR([tcl.h not found.  Please specify its location with --with-tclinclude])
    else
	AC_MSG_RESULT([${ac_cv_c_tclh}])
    fi

    # Convert to a native path and substitute into the output files.

    INCLUDE_DIR_NATIVE=`${CYGPATH} ${ac_cv_c_tclh}`

    TCL_INCLUDES=-I\"${INCLUDE_DIR_NATIVE}\"

    AC_SUBST(TCL_INCLUDES)
])

#------------------------------------------------------------------------
# TEA_PRIVATE_TK_HEADERS --
#
#	Locate the private Tk include files
#
# Arguments:
#
#	Requires:
#		TK_SRC_DIR	Assumes that TEA_LOAD_TKCONFIG has
#				 already been called.
#
# Results:
#
#	Substitutes the following vars:
#		TK_INCLUDES
#------------------------------------------------------------------------

AC_DEFUN([TEA_PRIVATE_TK_HEADERS], [
    # Allow for --with-tkinclude to take effect and define ${ac_cv_c_tkh}
    AC_REQUIRE([TEA_PUBLIC_TK_HEADERS])
    AC_MSG_CHECKING([for Tk private include files])

    TK_SRC_DIR_NATIVE=`${CYGPATH} ${TK_SRC_DIR}`
    TK_TOP_DIR_NATIVE=\"${TK_SRC_DIR_NATIVE}\"

    # Check to see if tk<Plat>Port.h isn't already with the public headers
    # Don't look for tkInt.h because that resides with tk.h in the core
    # sources, but the <plat>Port headers are in a different directory
    if test "${TEA_PLATFORM}" = "windows" -a \
	-f "${ac_cv_c_tkh}/tkWinPort.h"; then
	result="private headers found with public headers"
    elif test "${TEA_PLATFORM}" = "unix" -a \
	-f "${ac_cv_c_tkh}/tkUnixPort.h"; then
	result="private headers found with public headers"
    else
	TK_GENERIC_DIR_NATIVE=\"${TK_SRC_DIR_NATIVE}/generic\"
	TK_XLIB_DIR_NATIVE=\"${TK_SRC_DIR_NATIVE}/xlib\"
	if test "${TEA_PLATFORM}" = "windows"; then
	    TK_PLATFORM_DIR_NATIVE=\"${TK_SRC_DIR_NATIVE}/win\"
	else
	    TK_PLATFORM_DIR_NATIVE=\"${TK_SRC_DIR_NATIVE}/unix\"
	fi
	# Overwrite the previous TK_INCLUDES as this should capture both
	# public and private headers in the same set.
	# We want to ensure these are substituted so as not to require
	# any *_NATIVE vars be defined in the Makefile
	TK_INCLUDES="-I${TK_GENERIC_DIR_NATIVE} -I${TK_PLATFORM_DIR_NATIVE}"
	# Detect and add ttk subdir
	if test -d "${TK_SRC_DIR}/generic/ttk"; then
	   TK_INCLUDES="${TK_INCLUDES} -I\"${TK_SRC_DIR_NATIVE}/generic/ttk\""
	fi
	if test "${TEA_WINDOWINGSYSTEM}" != "x11"; then
	   TK_INCLUDES="${TK_INCLUDES} -I\"${TK_XLIB_DIR_NATIVE}\""
	fi
	if test "${TEA_WINDOWINGSYSTEM}" = "aqua"; then
	   TK_INCLUDES="${TK_INCLUDES} -I\"${TK_SRC_DIR_NATIVE}/macosx\""
	fi
	if test "`uname -s`" = "Darwin"; then
	    # If Tk was built as a framework, attempt to use
	    # the framework's Headers and PrivateHeaders directories
	    case ${TK_DEFS} in
		*TK_FRAMEWORK*)
			if test -d "${TK_BIN_DIR}/Headers" -a \
				-d "${TK_BIN_DIR}/PrivateHeaders"; then
			    TK_INCLUDES="-I\"${TK_BIN_DIR}/Headers\" -I\"${TK_BIN_DIR}/PrivateHeaders\" ${TK_INCLUDES}"
			else
			    TK_INCLUDES="${TK_INCLUDES} ${TK_INCLUDE_SPEC} `echo "${TK_INCLUDE_SPEC}" | sed -e 's/Headers/PrivateHeaders/'`"
			fi
			;;
	    esac
	    result="Using ${TK_INCLUDES}"
	else
	    if test ! -f "${TK_SRC_DIR}/generic/tkInt.h" ; then
	       AC_MSG_ERROR([Cannot find private header tkInt.h in ${TK_SRC_DIR}])
	    fi
	    result="Using srcdir found in tkConfig.sh: ${TK_SRC_DIR}"
	fi
    fi

    AC_SUBST(TK_TOP_DIR_NATIVE)
    AC_SUBST(TK_XLIB_DIR_NATIVE)

    AC_SUBST(TK_INCLUDES)
    AC_MSG_RESULT([${result}])
])

#------------------------------------------------------------------------
# TEA_PUBLIC_TK_HEADERS --
#
#	Locate the installed public Tk header files
#
# Arguments:
#	None.
#
# Requires:
#	CYGPATH must be set
#
# Results:
#
#	Adds a --with-tkinclude switch to configure.
#	Result is cached.
#
#	Substitutes the following vars:
#		TK_INCLUDES
#------------------------------------------------------------------------

AC_DEFUN([TEA_PUBLIC_TK_HEADERS], [
    AC_MSG_CHECKING([for Tk public headers])

    AC_ARG_WITH(tkinclude, [  --with-tkinclude        directory containing the public Tk header files], with_tkinclude=${withval})

    AC_CACHE_VAL(ac_cv_c_tkh, [
	# Use the value from --with-tkinclude, if it was given

	if test x"${with_tkinclude}" != x ; then
	    if test -f "${with_tkinclude}/tk.h" ; then
		ac_cv_c_tkh=${with_tkinclude}
	    else
		AC_MSG_ERROR([${with_tkinclude} directory does not contain tk.h])
	    fi
	else
	    list=""
	    if test "`uname -s`" = "Darwin"; then
		# If Tk was built as a framework, attempt to use
		# the framework's Headers directory.
		case ${TK_DEFS} in
		    *TK_FRAMEWORK*)
			list="`ls -d ${TK_BIN_DIR}/Headers 2>/dev/null`"
			;;
		esac
	    fi

	    # Look in the source dir only if Tk is not installed,
	    # and in that situation, look there before installed locations.
	    if test -f "${TK_BIN_DIR}/Makefile" ; then
		list="$list `ls -d ${TK_SRC_DIR}/generic 2>/dev/null`"
	    fi

	    # Check order: pkg --prefix location, Tk's --prefix location,
	    # relative to directory of tkConfig.sh, Tcl's --prefix location,
	    # relative to directory of tclConfig.sh.

	    eval "temp_includedir=${includedir}"
	    list="$list \
		`ls -d ${temp_includedir}        2>/dev/null` \
		`ls -d ${TK_PREFIX}/include      2>/dev/null` \
		`ls -d ${TK_BIN_DIR}/../include  2>/dev/null` \
		`ls -d ${TCL_PREFIX}/include     2>/dev/null` \
		`ls -d ${TCL_BIN_DIR}/../include 2>/dev/null`"
	    if test "${TEA_PLATFORM}" != "windows" -o "$GCC" = "yes"; then
		list="$list /usr/local/include /usr/include"
		if test x"${TK_INCLUDE_SPEC}" != x ; then
		    d=`echo "${TK_INCLUDE_SPEC}" | sed -e 's/^-I//'`
		    list="$list `ls -d ${d} 2>/dev/null`"
		fi
	    fi
	    for i in $list ; do
		if test -f "$i/tk.h" ; then
		    ac_cv_c_tkh=$i
		    break
		fi
	    done
	fi
    ])

    # Print a message based on how we determined the include path

    if test x"${ac_cv_c_tkh}" = x ; then
	AC_MSG_ERROR([tk.h not found.  Please specify its location with --with-tkinclude])
    else
	AC_MSG_RESULT([${ac_cv_c_tkh}])
    fi

    # Convert to a native path and substitute into the output files.

    INCLUDE_DIR_NATIVE=`${CYGPATH} ${ac_cv_c_tkh}`

    TK_INCLUDES=-I\"${INCLUDE_DIR_NATIVE}\"

    AC_SUBST(TK_INCLUDES)

    if test "${TEA_WINDOWINGSYSTEM}" != "x11"; then
	# On Windows and Aqua, we need the X compat headers
	AC_MSG_CHECKING([for X11 header files])
	if test ! -r "${INCLUDE_DIR_NATIVE}/X11/Xlib.h"; then
	    INCLUDE_DIR_NATIVE="`${CYGPATH} ${TK_SRC_DIR}/xlib`"
	    TK_XINCLUDES=-I\"${INCLUDE_DIR_NATIVE}\"
	    AC_SUBST(TK_XINCLUDES)
	fi
	AC_MSG_RESULT([${INCLUDE_DIR_NATIVE}])
    fi
])

#------------------------------------------------------------------------
# TEA_PATH_CONFIG --
#
#	Locate the ${1}Config.sh file and perform a sanity check on
#	the ${1} compile flags.  These are used by packages like
#	[incr Tk] that load *Config.sh files from more than Tcl and Tk.
#
# Arguments:
#	none
#
# Results:
#
#	Adds the following arguments to configure:
#		--with-$1=...
#
#	Defines the following vars:
#		$1_BIN_DIR	Full path to the directory containing
#				the $1Config.sh file
#------------------------------------------------------------------------

AC_DEFUN([TEA_PATH_CONFIG], [
    #
    # Ok, lets find the $1 configuration
    # First, look for one uninstalled.
    # the alternative search directory is invoked by --with-$1
    #

    if test x"${no_$1}" = x ; then
	# we reset no_$1 in case something fails here
	no_$1=true
	AC_ARG_WITH($1, [  --with-$1              directory containing $1 configuration ($1Config.sh)], with_$1config=${withval})
	AC_MSG_CHECKING([for $1 configuration])
	AC_CACHE_VAL(ac_cv_c_$1config,[

	    # First check to see if --with-$1 was specified.
	    if test x"${with_$1config}" != x ; then
		case ${with_$1config} in
		    */$1Config.sh )
			if test -f ${with_$1config}; then
			    AC_MSG_WARN([--with-$1 argument should refer to directory containing $1Config.sh, not to $1Config.sh itself])
			    with_$1config=`echo ${with_$1config} | sed 's!/$1Config\.sh$!!'`
			fi;;
		esac
		if test -f "${with_$1config}/$1Config.sh" ; then
		    ac_cv_c_$1config=`(cd ${with_$1config}; pwd)`
		else
		    AC_MSG_ERROR([${with_$1config} directory doesn't contain $1Config.sh])
		fi
	    fi

	    # then check for a private $1 installation
	    if test x"${ac_cv_c_$1config}" = x ; then
		for i in \
			../$1 \
			`ls -dr ../$1*[[0-9]].[[0-9]]*.[[0-9]]* 2>/dev/null` \
			`ls -dr ../$1*[[0-9]].[[0-9]][[0-9]] 2>/dev/null` \
			`ls -dr ../$1*[[0-9]].[[0-9]] 2>/dev/null` \
			`ls -dr ../$1*[[0-9]].[[0-9]]* 2>/dev/null` \
			../../$1 \
			`ls -dr ../../$1*[[0-9]].[[0-9]]*.[[0-9]]* 2>/dev/null` \
			`ls -dr ../../$1*[[0-9]].[[0-9]][[0-9]] 2>/dev/null` \
			`ls -dr ../../$1*[[0-9]].[[0-9]] 2>/dev/null` \
			`ls -dr ../../$1*[[0-9]].[[0-9]]* 2>/dev/null` \
			../../../$1 \
			`ls -dr ../../../$1*[[0-9]].[[0-9]]*.[[0-9]]* 2>/dev/null` \
			`ls -dr ../../../$1*[[0-9]].[[0-9]][[0-9]] 2>/dev/null` \
			`ls -dr ../../../$1*[[0-9]].[[0-9]] 2>/dev/null` \
			`ls -dr ../../../$1*[[0-9]].[[0-9]]* 2>/dev/null` \
			${srcdir}/../$1 \
			`ls -dr ${srcdir}/../$1*[[0-9]].[[0-9]]*.[[0-9]]* 2>/dev/null` \
			`ls -dr ${srcdir}/../$1*[[0-9]].[[0-9]][[0-9]] 2>/dev/null` \
			`ls -dr ${srcdir}/../$1*[[0-9]].[[0-9]] 2>/dev/null` \
			`ls -dr ${srcdir}/../$1*[[0-9]].[[0-9]]* 2>/dev/null` \
			; do
		    if test -f "$i/$1Config.sh" ; then
			ac_cv_c_$1config=`(cd $i; pwd)`
			break
		    fi
		    if test -f "$i/unix/$1Config.sh" ; then
			ac_cv_c_$1config=`(cd $i/unix; pwd)`
			break
		    fi
		done
	    fi

	    # check in a few common install locations
	    if test x"${ac_cv_c_$1config}" = x ; then
		for i in `ls -d ${libdir} 2>/dev/null` \
			`ls -d ${exec_prefix}/lib 2>/dev/null` \
			`ls -d ${prefix}/lib 2>/dev/null` \
			`ls -d /usr/local/lib 2>/dev/null` \
			`ls -d /usr/contrib/lib 2>/dev/null` \
			`ls -d /usr/lib 2>/dev/null` \
			`ls -d /usr/lib64 2>/dev/null` \
			; do
		    if test -f "$i/$1Config.sh" ; then
			ac_cv_c_$1config=`(cd $i; pwd)`
			break
		    fi
		done
	    fi
	])

	if test x"${ac_cv_c_$1config}" = x ; then
	    $1_BIN_DIR="# no $1 configs found"
	    AC_MSG_WARN([Cannot find $1 configuration definitions])
	    exit 0
	else
	    no_$1=
	    $1_BIN_DIR=${ac_cv_c_$1config}
	    AC_MSG_RESULT([found $$1_BIN_DIR/$1Config.sh])
	fi
    fi
])

#------------------------------------------------------------------------
# TEA_LOAD_CONFIG --
#
#	Load the $1Config.sh file
#
# Arguments:
#
#	Requires the following vars to be set:
#		$1_BIN_DIR
#
# Results:
#
#	Substitutes the following vars:
#		$1_SRC_DIR
#		$1_LIB_FILE
#		$1_LIB_SPEC
#------------------------------------------------------------------------

AC_DEFUN([TEA_LOAD_CONFIG], [
    AC_MSG_CHECKING([for existence of ${$1_BIN_DIR}/$1Config.sh])

    if test -f "${$1_BIN_DIR}/$1Config.sh" ; then
        AC_MSG_RESULT([loading])
	. "${$1_BIN_DIR}/$1Config.sh"
    else
        AC_MSG_RESULT([file not found])
    fi

    #
    # If the $1_BIN_DIR is the build directory (not the install directory),
    # then set the common variable name to the value of the build variables.
    # For example, the variable $1_LIB_SPEC will be set to the value
    # of $1_BUILD_LIB_SPEC. An extension should make use of $1_LIB_SPEC
    # instead of $1_BUILD_LIB_SPEC since it will work with both an
    # installed and uninstalled version of Tcl.
    #

    if test -f "${$1_BIN_DIR}/Makefile" ; then
	AC_MSG_WARN([Found Makefile - using build library specs for $1])
        $1_LIB_SPEC=${$1_BUILD_LIB_SPEC}
        $1_STUB_LIB_SPEC=${$1_BUILD_STUB_LIB_SPEC}
        $1_STUB_LIB_PATH=${$1_BUILD_STUB_LIB_PATH}
        $1_INCLUDE_SPEC=${$1_BUILD_INCLUDE_SPEC}
        $1_LIBRARY_PATH=${$1_LIBRARY_PATH}
    fi

    AC_SUBST($1_VERSION)
    AC_SUBST($1_BIN_DIR)
    AC_SUBST($1_SRC_DIR)

    AC_SUBST($1_LIB_FILE)
    AC_SUBST($1_LIB_SPEC)

    AC_SUBST($1_STUB_LIB_FILE)
    AC_SUBST($1_STUB_LIB_SPEC)
    AC_SUBST($1_STUB_LIB_PATH)

    # Allow the caller to prevent this auto-check by specifying any 2nd arg
    AS_IF([test "x$2" = x], [
	# Check both upper and lower-case variants
	# If a dev wanted non-stubs libs, this function could take an option
	# to not use _STUB in the paths below
	AS_IF([test "x${$1_STUB_LIB_SPEC}" = x],
	    [TEA_LOAD_CONFIG_LIB(translit($1,[a-z],[A-Z])_STUB)],
	    [TEA_LOAD_CONFIG_LIB($1_STUB)])
    ])
])

#------------------------------------------------------------------------
# TEA_LOAD_CONFIG_LIB --
#
#	Helper function to load correct library from another extension's
#	${PACKAGE}Config.sh.
#
# Results:
#	Adds to LIBS the appropriate extension library
#------------------------------------------------------------------------
AC_DEFUN([TEA_LOAD_CONFIG_LIB], [
    AC_MSG_CHECKING([For $1 library for LIBS])
    # This simplifies the use of stub libraries by automatically adding
    # the stub lib to your path.  Normally this would add to SHLIB_LD_LIBS,
    # but this is called before CONFIG_CFLAGS.  More importantly, this adds
    # to PKG_LIBS, which becomes LIBS, and that is only used by SHLIB_LD.
    if test "x${$1_LIB_SPEC}" != "x" ; then
	if test "${TEA_PLATFORM}" = "windows" -a "$GCC" != "yes" ; then
	    TEA_ADD_LIBS([\"`${CYGPATH} ${$1_LIB_PATH}`\"])
	    AC_MSG_RESULT([using $1_LIB_PATH ${$1_LIB_PATH}])
	else
	    TEA_ADD_LIBS([${$1_LIB_SPEC}])
	    AC_MSG_RESULT([using $1_LIB_SPEC ${$1_LIB_SPEC}])
	fi
    else
	AC_MSG_RESULT([file not found])
    fi
])

#------------------------------------------------------------------------
# TEA_EXPORT_CONFIG --
#
#	Define the data to insert into the ${PACKAGE}Config.sh file
#
# Arguments:
#
#	Requires the following vars to be set:
#		$1
#
# Results:
#	Substitutes the following vars:
#------------------------------------------------------------------------

AC_DEFUN([TEA_EXPORT_CONFIG], [
    #--------------------------------------------------------------------
    # These are for $1Config.sh
    #--------------------------------------------------------------------

    # pkglibdir must be a fully qualified path and (not ${exec_prefix}/lib)
    eval pkglibdir="[$]{libdir}/$1${PACKAGE_VERSION}"
    if test "${TCL_LIB_VERSIONS_OK}" = "ok"; then
	eval $1_LIB_FLAG="-l$1${PACKAGE_VERSION}${DBGX}"
	eval $1_STUB_LIB_FLAG="-l$1stub${PACKAGE_VERSION}${DBGX}"
    else
	eval $1_LIB_FLAG="-l$1`echo ${PACKAGE_VERSION} | tr -d .`${DBGX}"
	eval $1_STUB_LIB_FLAG="-l$1stub`echo ${PACKAGE_VERSION} | tr -d .`${DBGX}"
    fi
    $1_BUILD_LIB_SPEC="-L`pwd` ${$1_LIB_FLAG}"
    $1_LIB_SPEC="-L${pkglibdir} ${$1_LIB_FLAG}"
    $1_BUILD_STUB_LIB_SPEC="-L`pwd` [$]{$1_STUB_LIB_FLAG}"
    $1_STUB_LIB_SPEC="-L${pkglibdir} [$]{$1_STUB_LIB_FLAG}"
    $1_BUILD_STUB_LIB_PATH="`pwd`/[$]{PKG_STUB_LIB_FILE}"
    $1_STUB_LIB_PATH="${pkglibdir}/[$]{PKG_STUB_LIB_FILE}"

    AC_SUBST($1_BUILD_LIB_SPEC)
    AC_SUBST($1_LIB_SPEC)
    AC_SUBST($1_BUILD_STUB_LIB_SPEC)
    AC_SUBST($1_STUB_LIB_SPEC)
    AC_SUBST($1_BUILD_STUB_LIB_PATH)
    AC_SUBST($1_STUB_LIB_PATH)

    AC_SUBST(MAJOR_VERSION)
    AC_SUBST(MINOR_VERSION)
    AC_SUBST(PATCHLEVEL)
])


#------------------------------------------------------------------------
# TEA_PATH_CELIB --
#
#	Locate Keuchel's celib emulation layer for targeting Win/CE
#
# Arguments:
#	none
#
# Results:
#
#	Adds the following arguments to configure:
#		--with-celib=...
#
#	Defines the following vars:
#		CELIB_DIR	Full path to the directory containing
#				the include and platform lib files
#------------------------------------------------------------------------

AC_DEFUN([TEA_PATH_CELIB], [
    # First, look for one uninstalled.
    # the alternative search directory is invoked by --with-celib

    if test x"${no_celib}" = x ; then
	# we reset no_celib in case something fails here
	no_celib=true
	AC_ARG_WITH(celib,[  --with-celib=DIR        use Windows/CE support library from DIR], with_celibconfig=${withval})
	AC_MSG_CHECKING([for Windows/CE celib directory])
	AC_CACHE_VAL(ac_cv_c_celibconfig,[
	    # First check to see if --with-celibconfig was specified.
	    if test x"${with_celibconfig}" != x ; then
		if test -d "${with_celibconfig}/inc" ; then
		    ac_cv_c_celibconfig=`(cd ${with_celibconfig}; pwd)`
		else
		    AC_MSG_ERROR([${with_celibconfig} directory doesn't contain inc directory])
		fi
	    fi

	    # then check for a celib library
	    if test x"${ac_cv_c_celibconfig}" = x ; then
		for i in \
			../celib-palm-3.0 \
			../celib \
			../../celib-palm-3.0 \
			../../celib \
			`ls -dr ../celib-*3.[[0-9]]* 2>/dev/null` \
			${srcdir}/../celib-palm-3.0 \
			${srcdir}/../celib \
			`ls -dr ${srcdir}/../celib-*3.[[0-9]]* 2>/dev/null` \
			; do
		    if test -d "$i/inc" ; then
			ac_cv_c_celibconfig=`(cd $i; pwd)`
			break
		    fi
		done
	    fi
	])
	if test x"${ac_cv_c_celibconfig}" = x ; then
	    AC_MSG_ERROR([Cannot find celib support library directory])
	else
	    no_celib=
	    CELIB_DIR=${ac_cv_c_celibconfig}
	    CELIB_DIR=`echo "$CELIB_DIR" | sed -e 's!\\\!/!g'`
	    AC_MSG_RESULT([found $CELIB_DIR])
	fi
    fi
])
# Local Variables:
# mode: autoconf
# End:
