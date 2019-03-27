dnl This file is included into all any other acinclude file that needs
dnl to use these macros.

dnl This is copied from autoconf 2.12, but does calls our own AC_PROG_CC_WORKS,
dnl and doesn't call AC_PROG_CXX_GNU, cause we test for that in  AC_PROG_CC_WORKS.
dnl We are probably using a cross compiler, which will not be able to fully
dnl link an executable.  This should really be fixed in autoconf itself.
dnl Find a working G++ cross compiler. This only works for the GNU C++ compiler.
AC_DEFUN([CYG_AC_PROG_CXX_CROSS],
[AC_BEFORE([$0], [AC_PROG_CXXCPP])
AC_CHECK_PROGS(CXX, $CCC c++ g++ gcc CC cxx cc++, gcc)

CYG_AC_PROG_GXX_WORKS

if test $ac_cv_prog_gxx = yes; then
  GXX=yes
dnl Check whether -g works, even if CXXFLAGS is set, in case the package
dnl plays around with CXXFLAGS (such as to build both debugging and
dnl normal versions of a library), tasteless as that idea is.
  ac_test_CXXFLAGS="${CXXFLAGS+set}"
  ac_save_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS=
  AC_PROG_CXX_G
  if test "$ac_test_CXXFLAGS" = set; then
    CXXFLAGS="$ac_save_CXXFLAGS"
  elif test $ac_cv_prog_cxx_g = yes; then
    CXXFLAGS="-g -O2"
  else
    CXXFLAGS="-O2"
  fi
else
  GXX=
  test "${CXXFLAGS+set}" = set || CXXFLAGS="-g"
fi
])

dnl See if the G++ compiler we found works.
AC_DEFUN([CYG_AC_PROG_GXX_WORKS],
[AC_MSG_CHECKING([whether the G++ compiler ($CXX $CXXFLAGS $LDFLAGS) actually works])
AC_LANG_SAVE
AC_LANG_CPLUSPLUS
dnl Try a test case. We only compile, because it's close to impossible
dnl to get a correct fully linked executable with a cross compiler. For
dnl most cross compilers, this test is bogus. For G++, we can use various
dnl other compile line options to get a decent idea that the cross compiler
dnl actually does work, even though we can't produce an executable without
dnl more info about the target it's being compiled for. This only works
dnl for the GNU C++ compiler.

dnl Transform the name of the compiler to it's cross variant, unless
dnl CXX is set. This is also what CXX gets set to in the generated
dnl Makefile.
if test x"${CXX}" = xc++ ; then
    CXX=`echo gcc | sed -e "${program_transform_name}"`
fi

dnl Get G++'s full path to libgcc.a
libgccpath=`${CXX} --print-libgcc`

dnl If we don't have a path with libgcc.a on the end, this isn't G++.
if test `echo $libgccpath | sed -e 's:/.*/::'` = libgcc.a ; then
   ac_cv_prog_gxx=yes
else
   ac_cv_prog_gxx=no
fi

dnl If we are using G++, look for the files that need to exist if this
dnl compiler works.
if test x"${ac_cv_prog_gxx}" = xyes ; then
    gccfiles=`echo $libgccpath | sed -e 's:/libgcc.a::'`
    if test -f ${gccfiles}/specs -a -f ${gccfiles}/cpp -a -f ${gccfiles}/cc1plus; then
	gccfiles=yes
    else
	gccfiles=no
    fi
    gcclibs=`echo $libgccpath | sed -e 's:lib/gcc-lib/::' -e 's:/libgcc.a::' -e 's,\(.*\)/.*,\1,g'`/lib
    if test -d ${gcclibs}/ldscripts -a -f ${gcclibs}/libc.a -a -f ${gcclibs}/libstdc++.a ; then
	gcclibs=yes
    else
	gcclibs=no
    fi
fi

dnl If everything is OK, then we can safely assume the compiler works.
if test x"${gccfiles}" = xno -o x"${gcclibs}" = xno; then
    ac_cv_prog_cxx_works=no
    AC_MSG_ERROR(${CXX} is a non-working cross compiler)
else
   ac_cv_prog_cxx_works=yes 
fi

AC_LANG_RESTORE
AC_MSG_RESULT($ac_cv_prog_cxx_works)
if test x"$ac_cv_prog_cxx_works" = xno; then
  AC_MSG_ERROR([installation or configuration problem: C++ compiler cannot create executables.])
fi
AC_MSG_CHECKING([whether the G++ compiler ($CXX $CXXFLAGS $LDFLAGS) is a cross-compiler])
AC_MSG_RESULT($ac_cv_prog_cxx_cross)
cross_compiling=$ac_cv_prog_cxx_cross
AC_SUBST(CXX)
])

dnl ====================================================================
dnl Find a working GCC cross compiler. This only works for the GNU gcc compiler.
dnl This is based on the macros above for G++.
AC_DEFUN([CYG_AC_PROG_CC_CROSS],
[AC_BEFORE([$0], [AC_PROG_CCPP])
AC_CHECK_PROGS(CC, cc, gcc)

CYG_AC_PROG_GCC_WORKS

if test $ac_cv_prog_gcc = yes; then
  GCC=yes
dnl Check whether -g works, even if CFLAGS is set, in case the package
dnl plays around with CFLAGS (such as to build both debugging and
dnl normal versions of a library), tasteless as that idea is.
  ac_test_CFLAGS="${CFLAGS+set}"
  ac_save_CFLAGS="$CFLAGS"
  CFLAGS=
  AC_PROG_CC_G
  if test "$ac_test_CFLAGS" = set; then
    CFLAGS="$ac_save_CFLAGS"
  elif test $ac_cv_prog_cc_g = yes; then
    CFLAGS="-g -O2"
  else
    CFLAGS="-O2"
  fi
else
  GXX=
  test "${CFLAGS+set}" = set || CFLAGS="-g"
fi
])

dnl See if the GCC compiler we found works.
AC_DEFUN([CYG_AC_PROG_GCC_WORKS],
[AC_MSG_CHECKING([whether the Gcc compiler ($CC $CFLAGS $LDFLAGS) actually works])
AC_LANG_SAVE
AC_LANG_C
dnl Try a test case. We only compile, because it's close to impossible
dnl to get a correct fully linked executable with a cross
dnl compiler. For most cross compilers, this test is bogus. For G++,
dnl we can use various other compile line options to get a decent idea
dnl that the cross compiler actually does work, even though we can't
dnl produce an executable without more info about the target it's
dnl being compiled for. This only works for the GNU C++ compiler.

dnl Transform the name of the compiler to it's cross variant, unless
dnl CXX is set. This is also what CC gets set to in the generated Makefile.
if test x"${CC}" = xcc ; then
    CC=`echo gcc | sed -e "${program_transform_name}"`
fi

dnl Get Gcc's full path to libgcc.a
libgccpath=`${CC} --print-libgcc`

dnl If we don't have a path with libgcc.a on the end, this isn't G++.
if test `echo $libgccpath | sed -e 's:/.*/::'` = libgcc.a ; then
   ac_cv_prog_gcc=yes
else
   ac_cv_prog_gcc=no
fi

dnl If we are using Gcc, look for the files that need to exist if this
dnl compiler works.
if test x"${ac_cv_prog_gcc}" = xyes ; then
    gccfiles=`echo $libgccpath | sed -e 's:/libgcc.a::'`
    if test -f ${gccfiles}/specs -a -f ${gccfiles}/cpp -a -f ${gccfiles}/cc1plus; then
	gccfiles=yes
    else
	gccfiles=no
    fi
    gcclibs=`echo $libgccpath | sed -e 's:lib/gcc-lib/::' -e 's:/libgcc.a::' -e 's,\(.*\)/.*,\1,g'`/lib
    if test -d ${gcclibs}/ldscripts -a -f ${gcclibs}/libc.a -a -f ${gcclibs}/libstdc++.a ; then
	gcclibs=yes
    else
	gcclibs=no
    fi
fi

dnl If everything is OK, then we can safely assume the compiler works.
if test x"${gccfiles}" = xno -o x"${gcclibs}" = xno; then
    ac_cv_prog_cc_works=no
    AC_MSG_ERROR(${CC} is a non-working cross compiler)    
else
    ac_cv_prog_cc_works=yes
fi

AC_LANG_RESTORE
AC_MSG_RESULT($ac_cv_prog_cc_works)
if test x"$ac_cv_prog_cc_works" = xno; then
  AC_MSG_ERROR([installation or configuration problem: C++ compiler cannot create executables.])
fi
AC_MSG_CHECKING([whether the Gcc compiler ($CC $CFLAGS $LDFLAGS) is a cross-compiler])
AC_MSG_RESULT($ac_cv_prog_cc_cross)
cross_compiling=$ac_cv_prog_cc_cross
AC_SUBST(CC)
])

dnl ====================================================================
dnl Find the BFD library in the build tree. This is used to access and
dnl manipulate object or executable files.
AC_DEFUN([CYG_AC_PATH_BFD], [
AC_MSG_CHECKING(for the bfd header in the build tree)
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
dnl Look for the header file
AC_CACHE_VAL(ac_cv_c_bfdh,[
for i in $dirlist; do
    if test -f "$i/bfd/bfd.h" ; then
	ac_cv_c_bfdh=`(cd $i/bfd; ${PWDCMD-pwd})`
	break
    fi
done
])
if test x"${ac_cv_c_bfdh}" != x; then
    BFDHDIR="-I${ac_cv_c_bfdh}"
    AC_MSG_RESULT(${ac_cv_c_bfdh})
else
    AC_MSG_RESULT(none)
fi
AC_SUBST(BFDHDIR)

dnl Look for the library
AC_MSG_CHECKING(for the bfd library in the build tree)
AC_CACHE_VAL(ac_cv_c_bfdlib,[
for i in $dirlist; do
    if test -f "$i/bfd/Makefile" ; then
	ac_cv_c_bfdlib=`(cd $i/bfd; ${PWDCMD-pwd})`
    fi
done
])
dnl We list two directories cause bfd now uses libtool
if test x"${ac_cv_c_bfdlib}" != x; then
    BFDLIB="-L${ac_cv_c_bfdlib} -L${ac_cv_c_bfdlib}/.libs"
    AC_MSG_RESULT(${ac_cv_c_bfdlib})
else
    AC_MSG_RESULT(none)
fi
AC_SUBST(BFDLIB)
])

dnl ====================================================================
dnl Find the libiberty library. This defines many commonly used C
dnl functions that exists in various states based on the underlying OS.
AC_DEFUN([CYG_AC_PATH_LIBERTY], [
AC_MSG_CHECKING(for the liberty library in the build tree)
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
AC_CACHE_VAL(ac_cv_c_liberty,[
for i in $dirlist; do
    if test -f "$i/libiberty/Makefile" ; then
	ac_cv_c_liberty=`(cd $i/libiberty; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_liberty}" != x; then
    LIBERTY="-L${ac_cv_c_liberty}"
    AC_MSG_RESULT(${ac_cv_c_liberty})
else
    AC_MSG_RESULT(none)
fi
AC_SUBST(LIBERTY)
])

dnl ====================================================================
dnl Find the opcodes library. This is used to do dissasemblies.
AC_DEFUN([CYG_AC_PATH_OPCODES], [
AC_MSG_CHECKING(for the opcodes library in the build tree)
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
AC_CACHE_VAL(ac_cv_c_opc,[
for i in $dirlist; do
    if test -f "$i/opcodes/Makefile" ; then
	ac_cv_c_opc=`(cd $i/opcodes; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_opc}" != x; then
    OPCODESLIB="-L${ac_cv_c_opc}"
    AC_MSG_RESULT(${ac_cv_c_opc})
else
    AC_MSG_RESULT(none)
fi
AC_SUBST(OPCODESLIB)
])

dnl ====================================================================
dnl Look for the DejaGnu header file in the source tree. This file
dnl defines the functions used to testing support.
AC_DEFUN([CYG_AC_PATH_DEJAGNU], [
AC_MSG_CHECKING(for the testing support files in the source tree)
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
AC_CACHE_VAL(ac_cv_c_dejagnu,[
for i in $dirlist; do
    if test -f "$srcdir/$i/ecc/ecc/infra/testlib/current/include/dejagnu.h" ; then
	ac_cv_c_dejagnu=`(cd $srcdir/$i/ecc/ecc/infra/testlib/current/include; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_dejagnu}" != x; then
    DEJAGNUHDIR="-I${ac_cv_c_dejagnu}"
    AC_MSG_RESULT(${ac_cv_c_dejagnu})
else
    AC_MSG_RESULT(none)
fi
AC_CACHE_VAL(ac_cv_c_dejagnulib,[
for i in $dirlist; do
    if test -f "$srcdir/$i/infra/testlib/current/lib/hostutil.exp" ; then
	ac_cv_c_dejagnulib=`(cd $srcdir/$i/infra/testlib/current/lib; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_dejagnulib}" != x; then
    DEJAGNULIB="${ac_cv_c_dejagnulib}"
else
    DEJAGNULIB=""
fi
AC_MSG_CHECKING(for runtest in the source tree)
AC_CACHE_VAL(ac_cv_c_runtest,[
for i in $dirlist; do
    if test -f "$srcdir/$i/dejagnu/runtest" ; then
	ac_cv_c_runtest=`(cd $srcdir/$i/dejagnu; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_runtest}" != x; then
    RUNTESTDIR="${ac_cv_c_runtest}"
   AC_MSG_RESULT(${ac_cv_c_runtest})
else
    RUNTESTDIR=""
    AC_MSG_RESULT(none)
fi
AC_SUBST(RUNTESTDIR)
AC_SUBST(DEJAGNULIB)
AC_SUBST(DEJAGNUHDIR)
])

dnl ====================================================================
dnl Find the libintl library in the build tree. This is for
dnl  internationalization support.
AC_DEFUN([CYG_AC_PATH_INTL], [
AC_MSG_CHECKING(for the intl header in the build tree)
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
dnl Look for the header file
AC_CACHE_VAL(ac_cv_c_intlh,[
for i in $dirlist; do
    if test -f "$i/intl/libintl.h" ; then
	ac_cv_c_intlh=`(cd $i/intl; ${PWDCMD-pwd})`
	break
    fi
done
])
if test x"${ac_cv_c_intlh}" != x; then
    INTLHDIR="-I${ac_cv_c_intlh}"
    AC_MSG_RESULT(${ac_cv_c_intlh})
else
    AC_MSG_RESULT(none)
fi
AC_SUBST(INTLHDIR)

dnl Look for the library
AC_MSG_CHECKING(for the libintl library in the build tree)
AC_CACHE_VAL(ac_cv_c_intllib,[
for i in $dirlist; do
    if test -f "$i/intl/Makefile" ; then
	ac_cv_c_intllib=`(cd $i/intl; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_intllib}" != x; then
    INTLLIB="-L${ac_cv_c_intllib} -lintl"
    AC_MSG_RESULT(${ac_cv_c_intllib})
else
    AC_MSG_RESULT(none)
fi
AC_SUBST(INTLLIB)
])

dnl ====================================================================
dnl Find the simulator library.
AC_DEFUN([CYG_AC_PATH_SIM], [
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.. ../../../../../../../../../.."
case "$target_cpu" in
    powerpc)	target_dir=ppc ;;
    sparc*)	target_dir=erc32 ;;
    mips*)	target_dir=mips ;;
    *)		target_dir=$target_cpu ;;
esac
dnl First look for the header file
AC_MSG_CHECKING(for the simulator header file)
AC_CACHE_VAL(ac_cv_c_simh,[
for i in $dirlist; do
    if test -f "${srcdir}/$i/include/remote-sim.h" ; then
	ac_cv_c_simh=`(cd ${srcdir}/$i/include; ${PWDCMD-pwd})`
	break
    fi
done
])
if test x"${ac_cv_c_simh}" != x; then
    SIMHDIR="-I${ac_cv_c_simh}"
    AC_MSG_RESULT(${ac_cv_c_simh})
else
    AC_MSG_RESULT(none)
fi
AC_SUBST(SIMHDIR)

dnl See whether it's a devo or Foundry branch simulator
AC_MSG_CHECKING(Whether this is a devo simulator )
AC_CACHE_VAL(ac_cv_c_simdevo,[
    CPPFLAGS="$CPPFLAGS $SIMHDIR"
    AC_EGREP_HEADER([SIM_DESC sim_open.*struct _bfd], remote-sim.h,
        ac_cv_c_simdevo=yes,
        ac_cv_c_simdevo=no)
])
if test x"$ac_cv_c_simdevo" = x"yes" ; then
    AC_DEFINE(HAVE_DEVO_SIM)
fi
AC_MSG_RESULT(${ac_cv_c_simdevo})
AC_SUBST(HAVE_DEVO_SIM)

dnl Next look for the library
AC_MSG_CHECKING(for the simulator library)
AC_CACHE_VAL(ac_cv_c_simlib,[
for i in $dirlist; do
    if test -f "$i/sim/$target_dir/Makefile" ; then
	ac_cv_c_simlib=`(cd $i/sim/$target_dir; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_simlib}" != x; then
    SIMLIB="-L${ac_cv_c_simlib}"
else
    AC_MSG_RESULT(none)
    dnl FIXME: this is kinda bogus, cause umtimately the TM will build
    dnl all the libraries for several architectures. But for now, this
    dnl will work till then.
dnl     AC_MSG_CHECKING(for the simulator installed with the compiler libraries)
    dnl Transform the name of the compiler to it's cross variant, unless
    dnl CXX is set. This is also what CXX gets set to in the generated
    dnl Makefile.
    CROSS_GCC=`echo gcc | sed -e "s/^/$target/"`

    dnl Get G++'s full path to libgcc.a
changequote(,)
    gccpath=`${CROSS_GCC} --print-libgcc | sed -e 's:[a-z0-9A-Z\.\-]*/libgcc.a::' -e 's:lib/gcc-lib/::'`lib
changequote([,])
    if test -f $gccpath/libsim.a -o -f $gccpath/libsim.so ; then
        ac_cv_c_simlib="$gccpath/"
        SIMLIB="-L${ac_cv_c_simlib}"
	AC_MSG_RESULT(${ac_cv_c_simlib})
    else
        AM_CONDITIONAL(PSIM, test x$psim = xno)
	SIMLIB=""
	AC_MSG_RESULT(none)
dnl         ac_cv_c_simlib=none
    fi
fi
AC_SUBST(SIMLIB)
])

dnl ====================================================================
dnl Find the libiberty library.
AC_DEFUN([CYG_AC_PATH_LIBIBERTY], [
AC_MSG_CHECKING(for the libiberty library in the build tree)
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
AC_CACHE_VAL(ac_cv_c_libib,[
for i in $dirlist; do
    if test -f "$i/libiberty/Makefile" ; then
	ac_cv_c_libib=`(cd $i/libiberty/; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_libib}" != x; then
    LIBIBERTY="-L${ac_cv_c_libib}"
    AC_MSG_RESULT(${ac_cv_c_libib})
else
    AC_MSG_RESULT(none)
fi
AC_SUBST(LIBIBERTY)
])

dnl ====================================================================
AC_DEFUN([CYG_AC_PATH_DEVO], [
AC_MSG_CHECKING(for devo headers in the source tree)
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
AC_CACHE_VAL(ac_cv_c_devoh,[
for i in $dirlist; do
    if test -f "${srcdir}/$i/include/remote-sim.h" ; then
	ac_cv_c_devoh=`(cd ${srcdir}/$i/include; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_devoh}" != x; then
    DEVOHDIR="-I${ac_cv_c_devoh}"
    AC_MSG_RESULT(${ac_cv_c_devoh})
else
    AC_MSG_RESULT(none)
fi
AC_SUBST(DEVOHDIR)
])

dnl ====================================================================
dnl find the IDE library and headers.
AC_DEFUN([CYG_AC_PATH_IDE], [
AC_MSG_CHECKING(for IDE headers in the source tree)
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
IDEHDIR=
IDELIB=
AC_CACHE_VAL(ac_cv_c_ideh,[
for i in $dirlist; do
    if test -f "${srcdir}/$i/libide/src/event.h" ; then
	ac_cv_c_ideh=`(cd ${srcdir}/$i/libide/src; ${PWDCMD-pwd})`;
    fi
done
])
if test x"${ac_cv_c_ideh}" != x; then
    IDEHDIR="-I${ac_cv_c_ideh}"
    AC_MSG_RESULT(${ac_cv_c_ideh})
else
    AC_MSG_RESULT(none)
fi

AC_MSG_CHECKING(for LIBIDE TCL headers in the source tree)
AC_CACHE_VAL(ac_cv_c_idetclh,[
for i in $dirlist; do
    if test -f "${srcdir}/$i/libidetcl/src/idetcl.h" ; then
	ac_cv_c_idetclh=`(cd ${srcdir}/$i/libidetcl/src; ${PWDCMD-pwd})`;
    fi
done
])
if test x"${ac_cv_c_idetclh}" != x; then
    IDEHDIR="${IDEHDIR} -I${ac_cv_c_idetclh}"
    AC_MSG_RESULT(${ac_cv_c_idetclh})
else
    AC_MSG_RESULT(none)
fi

AC_MSG_CHECKING(for IDE headers in the build tree)
AC_CACHE_VAL(ac_cv_c_ideh2,[
for i in $dirlist; do
    if test -f "$i/libide/src/Makefile" ; then
	ac_cv_c_ideh2=`(cd $i/libide/src; ${PWDCMD-pwd})`;
    fi
done
])
if test x"${ac_cv_c_ideh2}" != x; then
    IDEHDIR="${IDEHDIR} -I${ac_cv_c_ideh2}"
    AC_MSG_RESULT(${ac_cv_c_ideh2})
else
    AC_MSG_RESULT(none)
fi

dnl look for the library
AC_MSG_CHECKING(for IDE library)
AC_CACHE_VAL(ac_cv_c_idelib,[
if test x"${ac_cv_c_idelib}" = x ; then
    for i in $dirlist; do
      if test -f "$i/libide/src/Makefile" ; then
        ac_cv_c_idelib=`(cd $i/libide/src; ${PWDCMD-pwd})`
        break
      fi
    done
fi]) 
if test x"${ac_cv_c_idelib}" != x ; then
     IDELIB="-L${ac_cv_c_idelib}"
     AC_MSG_RESULT(${ac_cv_c_idelib})
else
     AC_MSG_RESULT(none)
fi

dnl find libiddetcl.a if it exists
AC_MSG_CHECKING(for IDE TCL library)
AC_CACHE_VAL(ac_cv_c_idetcllib,[
if test x"${ac_cv_c_idetcllib}" = x ; then
    for i in $dirlist; do
      if test -f "$i/libidetcl/src/Makefile" ; then
        ac_cv_c_idetcllib=`(cd $i/libidetcl/src; ${PWDCMD-pwd})`
        break
      fi
    done
fi
]) 
if test x"${ac_cv_c_idetcllib}" != x ; then
     IDELIB="${IDELIB} -L${ac_cv_c_idetcllib}"
     IDETCLLIB="-lidetcl"
     AC_MSG_RESULT(${ac_cv_c_idetcllib})
else
     AC_MSG_RESULT(none)
fi
AC_SUBST(IDEHDIR)
AC_SUBST(IDELIB)
AC_SUBST(IDETCLLIB)
])

dnl ====================================================================
dnl Find all the ILU headers and libraries
AC_DEFUN([CYG_AC_PATH_ILU], [
AC_MSG_CHECKING(for ILU kernel headers in the source tree)
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
AC_CACHE_VAL(ac_cv_c_iluh,[
for i in $dirlist; do
    if test -f "${srcdir}/$i/ilu/runtime/kernel/method.h" ; then
	ac_cv_c_iluh=`(cd ${srcdir}/$i/ilu/runtime/kernel; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_iluh}" != x; then
    ILUHDIR="-I${ac_cv_c_iluh}"
    AC_MSG_RESULT(${ac_cv_c_iluh})
else
    AC_MSG_RESULT(none)
fi

AC_MSG_CHECKING(for ILU kernel headers in the build tree)
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
AC_CACHE_VAL(ac_cv_c_iluh5,[
for i in $dirlist; do
    if test -f "$i/ilu/runtime/kernel/iluconf.h" ; then
	ac_cv_c_iluh5=`(cd $i/ilu/runtime/kernel; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_iluh5}" != x; then
    ILUHDIR="${ILUHDIR} -I${ac_cv_c_iluh5}"
    AC_MSG_RESULT(${ac_cv_c_iluh5})
else
    AC_MSG_RESULT(none)
fi

AC_MSG_CHECKING(for ILU C++ headers in the source tree)
AC_CACHE_VAL(ac_cv_c_iluh2,[
for i in $dirlist; do
    if test -f "${srcdir}/$i/ilu/stubbers/cpp/resource.h" ; then
	ac_cv_c_iluh2=`(cd ${srcdir}/$i/ilu/stubbers/cpp; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_iluh2}" != x; then
    ILUHDIR="${ILUHDIR} -I${ac_cv_c_iluh2}"
    AC_MSG_RESULT(${ac_cv_c_iluh2})
else
    AC_MSG_RESULT(none)
fi

AC_MSG_CHECKING(for ILU C headers)
AC_CACHE_VAL(ac_cv_c_iluh3,[
for i in $dirlist; do
    if test -f "${srcdir}/$i/ilu/stubbers/c/resource.h" ; then
	ac_cv_c_iluh3=`(cd ${srcdir}/$i/ilu/stubbers/c  ; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_iluh3}" != x; then
    ILUHDIR="${ILUHDIR} -I${ac_cv_c_iluh3}"
    AC_MSG_RESULT(${ac_cv_c_iluh3})
else
    AC_MSG_RESULT(none)
fi

AC_MSG_CHECKING(for ILU C runtime headers)
AC_CACHE_VAL(ac_cv_c_iluh4,[
for i in $dirlist; do
    if test -f "${srcdir}/$i/ilu/runtime/c/ilucstub.h" ; then
	ac_cv_c_iluh4=`(cd ${srcdir}/$i/ilu/runtime/c  ; ${PWDCMD-pwd})`
    fi
done
])
if test x"${ac_cv_c_iluh4}" != x; then
    ILUHDIR="${ILUHDIR} -I${ac_cv_c_iluh4}"
    AC_MSG_RESULT(${ac_cv_c_iluh4})
else
    AC_MSG_RESULT(none)
fi

AC_CACHE_VAL(ac_cv_c_ilupath,[
for i in $dirlist; do
    if test -f "$i/ilu/Makefile" ; then
	ac_cv_c_ilupath=`(cd $i/ilu; ${PWDCMD-pwd})`
	break
    fi
done
])
ILUTOP=${ac_cv_c_ilupath}

AC_MSG_CHECKING(for the ILU library in the build tree)
AC_CACHE_VAL(ac_cv_c_ilulib,[
if test -f "$ac_cv_c_ilupath/runtime/kernel/Makefile" ; then
    ac_cv_c_ilulib=`(cd $ac_cv_c_ilupath/runtime/kernel; ${PWDCMD-pwd})`
    AC_MSG_RESULT(found ${ac_cv_c_ilulib}/libilu.a)
else
    AC_MSG_RESULT(no)
fi])
   
AC_MSG_CHECKING(for the ILU C++ bindings library in the build tree)
AC_CACHE_VAL(ac_cv_c_ilulib2,[
if test -f "$ac_cv_c_ilupath/runtime/cpp/Makefile" ; then
    ac_cv_c_ilulib2=`(cd $ac_cv_c_ilupath/runtime/cpp; ${PWDCMD-pwd})`
    AC_MSG_RESULT(found ${ac_cv_c_ilulib2}/libilu-c++.a)
else
    AC_MSG_RESULT(no)
fi])

AC_MSG_CHECKING(for the ILU C bindings library in the build tree)
AC_CACHE_VAL(ac_cv_c_ilulib3,[
if test -f "$ac_cv_c_ilupath/runtime/c/Makefile" ; then
    ac_cv_c_ilulib3=`(cd $ac_cv_c_ilupath/runtime/c; ${PWDCMD-pwd})`
    AC_MSG_RESULT(found ${ac_cv_c_ilulib3}/libilu-c.a)
else
    AC_MSG_RESULT(no)
fi])

AC_MSG_CHECKING(for the ILU Tk bindings library in the build tree)
AC_CACHE_VAL(ac_cv_c_ilulib4,[
if test -f "$ac_cv_c_ilupath/runtime/mainloop/Makefile" ; then
    ac_cv_c_ilulib4=`(cd $ac_cv_c_ilupath/runtime/mainloop; ${PWDCMD-pwd})`
    AC_MSG_RESULT(found ${ac_cv_c_ilulib4}/libilu-tk.a)
else
    AC_MSG_RESULT(no)
fi])

if test x"${ac_cv_c_ilulib}" = x -a x"${ac_cv_c_ilulib2}" = x; then
  ILUHDIR=""
fi

if test x"${ac_cv_c_ilulib}" != x -a x"${ac_cv_c_ilulib2}" != x; then
    ILULIB="-L${ac_cv_c_ilulib} -L${ac_cv_c_ilulib2} -L${ac_cv_c_ilulib3} -L${ac_cv_c_ilulib4}"
else
    ILULIB=""
fi

if test x"${ILULIB}" = x; then
    AC_MSG_CHECKING(for ILU libraries installed with the compiler)
    AC_CACHE_VAL(ac_cv_c_ilulib5,[
    NATIVE_GCC=`echo gcc | sed -e "${program_transform_name}"`

    dnl Get G++'s full path to it's libraries
    ac_cv_c_ilulib5=`${NATIVE_GCC} --print-libgcc | sed -e 's:lib/gcc-lib/.*::'`lib
    if test -f $ac_cv_c_ilulib5/libilu-c.a -o -f $ac_cv_c_ilulib5/libilu-c.so ; then
        if test x"${ILUHDIR}" = x; then
               ILUHDIR="-I${ac_cv_c_ilulib5}/../include"
        fi
        ILULIB="-L${ac_cv_c_ilulib5}"
        AC_MSG_RESULT(${ac_cv_c_ilulib5})
    else
        ac_cv_c_ilulib=none
        AC_MSG_RESULT(none)
    fi
fi])
AC_SUBST(ILUHDIR)
AC_SUBST(ILULIB)
AC_SUBST(ILUTOP)
])

dnl ====================================================================
dnl This defines the byte order for the host. We can't use
dnl AC_C_BIGENDIAN, cause we want to create a config file and
dnl substitue the real value, so the header files work right
AC_DEFUN([CYG_AC_C_ENDIAN], [
AC_MSG_CHECKING(to see if this is a little endian host)
AC_CACHE_VAL(ac_cv_c_little_endian, [
ac_cv_c_little_endian=unknown
# See if sys/param.h defines the BYTE_ORDER macro.
AC_TRY_COMPILE([#include <sys/types.h>
#include <sys/param.h>], [
#if !BYTE_ORDER || !_BIG_ENDIAN || !_LITTLE_ENDIAN
 bogus endian macros
#endif], [# It does; now see whether it defined to _LITTLE_ENDIAN or not.
AC_TRY_COMPILE([#include <sys/types.h>
#include <sys/param.h>], [
#if BYTE_ORDER != _LITTLE_ENDIAN
 not big endian
#endif], ac_cv_c_little_endian=yes, ac_cv_c_little_endian=no)
])
if test ${ac_cv_c_little_endian} = unknown; then
old_cflags=$CFLAGS
CFLAGS=-g
AC_TRY_RUN([
main () {
  /* Are we little or big endian?  From Harbison&Steele.  */
  union
  {
    long l;
    char c[sizeof (long)];
  } u;
  u.l = 1;
  exit (u.c[0] == 1);
}],
ac_cv_c_little_endian=no,
ac_cv_c_little_endian=yes,[
dnl Yes, this is ugly, and only used for a canadian cross anyway. This
dnl is just to keep configure from stopping here.
case "${host}" in
changequote(,)
   i[3456789]86-*-*) ac_cv_c_little_endian=yes ;;
   sparc*-*-*)    ac_cv_c_little_endian=no ;;
changequote([,])
  *)    AC_MSG_WARN(Can't cross compile this test) ;;
esac])
CFLAGS=$old_cflags
fi])

if test x"${ac_cv_c_little_endian}" = xyes; then
    AC_DEFINE(LITTLE_ENDIAN_HOST)
    ENDIAN="CYG_LSBFIRST";
else
    ENDIAN="CYG_MSBFIRST";
fi
AC_MSG_RESULT(${ac_cv_c_little_endian})
AC_SUBST(ENDIAN)
])

dnl ====================================================================
dnl Look for the path to libgcc, so we can use it to directly link
dnl in libgcc.a with LD.
AC_DEFUN([CYG_AC_PATH_LIBGCC],
[AC_MSG_CHECKING([Looking for the path to libgcc.a])
AC_LANG_SAVE
AC_LANG_C

dnl Get Gcc's full path to libgcc.a
libgccpath=`${CC} --print-libgcc`

dnl If we don't have a path with libgcc.a on the end, this isn't G++.
if test `echo $libgccpath | sed -e 's:/.*/::'` = libgcc.a ; then
   ac_cv_prog_gcc=yes
else
   ac_cv_prog_gcc=no
fi

dnl 
if test x"${ac_cv_prog_gcc}" = xyes ; then
   gccpath=`echo $libgccpath | sed -e 's:/libgcc.a::'`
   LIBGCC="-L${gccpath}"
   AC_MSG_RESULT(${gccpath})
else
   LIBGCC=""
   AC_MSG_ERROR(Not using gcc)
fi

AC_LANG_RESTORE
AC_SUBST(LIBGCC)
])

dnl ====================================================================
dnl Ok, lets find the tcl source trees so we can use the headers
dnl Warning: transition of version 9 to 10 will break this algorithm
dnl because 10 sorts before 9. We also look for just tcl. We have to
dnl be careful that we don't match stuff like tclX by accident.
dnl the alternative search directory is involked by --with-tclinclude
AC_DEFUN([CYG_AC_PATH_TCL], [
    CYG_AC_PATH_TCLH
    CYG_AC_PATH_TCLCONFIG
    CYG_AC_LOAD_TCLCONFIG
])
AC_DEFUN([CYG_AC_PATH_TCLH], [
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
no_tcl=true
AC_MSG_CHECKING(for Tcl headers in the source tree)
AC_ARG_WITH(tclinclude, [  --with-tclinclude       directory where tcl headers are], with_tclinclude=${withval})
AC_CACHE_VAL(ac_cv_c_tclh,[
dnl first check to see if --with-tclinclude was specified
if test x"${with_tclinclude}" != x ; then
  if test -f ${with_tclinclude}/tcl.h ; then
    ac_cv_c_tclh=`(cd ${with_tclinclude}; ${PWDCMD-pwd})`
  elif test -f ${with_tclinclude}/generic/tcl.h ; then
    ac_cv_c_tclh=`(cd ${with_tclinclude}/generic; ${PWDCMD-pwd})`
  else
    AC_MSG_ERROR([${with_tclinclude} directory doesn't contain headers])
  fi
fi

dnl next check if it came with Tcl configuration file
if test x"${ac_cv_c_tclconfig}" != x ; then
  for i in $dirlist; do
    if test -f $ac_cv_c_tclconfig/$i/generic/tcl.h ; then
      ac_cv_c_tclh=`(cd $ac_cv_c_tclconfig/$i/generic; ${PWDCMD-pwd})`
      break
    fi
  done
fi

dnl next check in private source directory
dnl since ls returns lowest version numbers first, reverse its output
if test x"${ac_cv_c_tclh}" = x ; then
    dnl find the top level Tcl source directory
    for i in $dirlist; do
        if test -n "`ls -dr $srcdir/$i/tcl* 2>/dev/null`" ; then
	    tclpath=$srcdir/$i
	    break
	fi
    done

    dnl find the exact Tcl source dir. We do it this way, cause there
    dnl might be multiple version of Tcl, and we want the most recent one.
    for i in `ls -dr $tclpath/tcl* 2>/dev/null ` ; do
        if test -f $i/generic/tcl.h ; then
          ac_cv_c_tclh=`(cd $i/generic; ${PWDCMD-pwd})`
          break
        fi
    done
fi

dnl check if its installed with the compiler
if test x"${ac_cv_c_tclh}" = x ; then
    dnl Get the path to the compiler
    ccpath=`which ${CC}  | sed -e 's:/bin/.*::'`/include
    if test -f $ccpath/tcl.h; then
        ac_cv_c_tclh=$ccpath
    fi
fi

dnl see if one is installed
if test x"${ac_cv_c_tclh}" = x ; then
   AC_MSG_RESULT(none)
   AC_CHECK_HEADER(tcl.h, ac_cv_c_tclh=installed, ac_cv_c_tclh="")
else
   AC_MSG_RESULT(${ac_cv_c_tclh})
fi
])
  TCLHDIR=""
if test x"${ac_cv_c_tclh}" = x ; then
    AC_MSG_ERROR([Can't find any Tcl headers])
fi
if test x"${ac_cv_c_tclh}" != x ; then
    no_tcl=""
    if test x"${ac_cv_c_tclh}" != x"installed" ; then
	if test x"${CC}" = xcl ; then
	    tmp="`cygpath --windows ${ac_cv_c_tclh}`"
	    ac_cv_c_tclh="`echo $tmp | sed -e s#\\\\\\\\#/#g`"
	fi
        AC_MSG_RESULT(${ac_cv_c_tclh})
        TCLHDIR="-I${ac_cv_c_tclh}"
    fi
fi

AC_SUBST(TCLHDIR)
])

dnl ====================================================================
dnl Ok, lets find the tcl configuration
AC_DEFUN([CYG_AC_PATH_TCLCONFIG], [
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
dnl First, look for one uninstalled.  
dnl the alternative search directory is invoked by --with-tclconfig
if test x"${no_tcl}" = x ; then
  dnl we reset no_tcl in case something fails here
    no_tcl=true
    AC_ARG_WITH(tclconfig, [  --with-tclconfig           directory containing tcl configuration (tclConfig.sh)],
         with_tclconfig=${withval})
    AC_MSG_CHECKING([for Tcl configuration script])
    AC_CACHE_VAL(ac_cv_c_tclconfig,[

    dnl First check to see if --with-tclconfig was specified.
    if test x"${with_tclconfig}" != x ; then
        if test -f "${with_tclconfig}/tclConfig.sh" ; then
            ac_cv_c_tclconfig=`(cd ${with_tclconfig}; ${PWDCMD-pwd})`
        else
            AC_MSG_ERROR([${with_tclconfig} directory doesn't contain tclConfig.sh])
        fi
    fi

    dnl next check if it came with Tcl configuration file in the source tree
    if test x"${ac_cv_c_tclconfig}" = x ; then
        for i in $dirlist; do
            dnl need to test both unix and win directories, since 
            dnl cygwin's tkConfig.sh could be in either directory depending
            dnl on the cygwin port of tcl.
            if test -f $srcdir/$i/unix/tclConfig.sh ; then
                ac_cv_c_tclconfig=`(cd $srcdir/$i/unix; ${PWDCMD-pwd})`
	        break
            fi
            if test -f $srcdir/$i/win/tclConfig.sh ; then
                ac_cv_c_tclconfig=`(cd $srcdir/$i/win; ${PWDCMD-pwd})`
	        break
            fi
        done
    fi
    dnl check in a few other locations
    if test x"${ac_cv_c_tclconfig}" = x ; then
        dnl find the top level Tcl source directory
        for i in $dirlist; do
            if test -n "`ls -dr $i/tcl* 2>/dev/null`" ; then
	        tclconfpath=$i
	        break
	    fi
        done

        dnl find the exact Tcl dir. We do it this way, cause there
        dnl might be multiple version of Tcl, and we want the most recent one.
        for i in `ls -dr $tclconfpath/tcl* 2>/dev/null ` ; do
            dnl need to test both unix and win directories, since 
            dnl cygwin's tclConfig.sh could be in either directory depending
            dnl on the cygwin port of tcl.
            if test -f $i/unix/tclConfig.sh ; then
                ac_cv_c_tclconfig=`(cd $i/unix; ${PWDCMD-pwd})`
                break
            fi
            if test -f $i/win/tclConfig.sh ; then
                ac_cv_c_tclconfig=`(cd $i/win; ${PWDCMD-pwd})`
                break
            fi
        done
    fi

    dnl Check to see if it's installed. We have to look in the $CC path
    dnl to find it, cause our $prefix may not match the compilers.
    if test x"${ac_cv_c_tclconfig}" = x ; then
        dnl Get the path to the compiler
	ccpath=`which ${CC}  | sed -e 's:/bin/.*::'`/lib
        if test -f $ccpath/tclConfig.sh; then
	    ac_cv_c_tclconfig=$ccpath
        fi
    fi
    ])	dnl end of cache_val

    if test x"${ac_cv_c_tclconfig}" = x ; then
        TCLCONFIG=""
        AC_MSG_WARN(Can't find Tcl configuration definitions)
    else
        no_tcl=""
        TCLCONFIG=${ac_cv_c_tclconfig}/tclConfig.sh
        AC_MSG_RESULT(${TCLCONFIG})
     fi
fi
AC_SUBST(TCLCONFIG)
])

dnl Defined as a separate macro so we don't have to cache the values
dnl from PATH_TCLCONFIG (because this can also be cached).
AC_DEFUN([CYG_AC_LOAD_TCLCONFIG], [
    . $TCLCONFIG

dnl AC_SUBST(TCL_VERSION)
dnl AC_SUBST(TCL_MAJOR_VERSION)
dnl AC_SUBST(TCL_MINOR_VERSION)
dnl AC_SUBST(TCL_CC)
    AC_SUBST(TCL_DEFS)

dnl not used, don't export to save symbols
    AC_SUBST(TCL_LIB_FILE)
    AC_SUBST(TCL_LIB_FULL_PATH)
    AC_SUBST(TCL_LIBS)
dnl not used, don't export to save symbols
dnl    AC_SUBST(TCL_PREFIX)

    AC_SUBST(TCL_CFLAGS)

dnl not used, don't export to save symbols
dnl    AC_SUBST(TCL_EXEC_PREFIX)

    AC_SUBST(TCL_SHLIB_CFLAGS)
    AC_SUBST(TCL_SHLIB_LD)
dnl don't export, not used outside of configure
dnl AC_SUBST(TCL_SHLIB_LD_LIBS)
dnl AC_SUBST(TCL_SHLIB_SUFFIX)
dnl not used, don't export to save symbols
dnl AC_SUBST(TCL_DL_LIBS)
    AC_SUBST(TCL_LD_FLAGS)
    AC_SUBST(TCL_LD_SEARCH_FLAGS)
dnl don't export, not used outside of configure
dnl AC_SUBST(TCL_COMPAT_OBJS)
    AC_SUBST(TCL_RANLIB)
    AC_SUBST(TCL_BUILD_LIB_SPEC)
    AC_SUBST(TCL_LIB_SPEC)
    AC_SUBST(TCL_BIN_DIR)
dnl AC_SUBST(TCL_LIB_VERSIONS_OK)

dnl not used, don't export to save symbols
dnl    AC_SUBST(TCL_SHARED_LIB_SUFFIX)

dnl not used, don't export to save symbols
dnl    AC_SUBST(TCL_UNSHARED_LIB_SUFFIX)
])

dnl ====================================================================
AC_DEFUN([CYG_AC_PATH_TK], [
    CYG_AC_PATH_TKH
    CYG_AC_PATH_TKCONFIG
    CYG_AC_LOAD_TKCONFIG
])
AC_DEFUN([CYG_AC_PATH_TKH], [
#
# Ok, lets find the tk source trees so we can use the headers
# If the directory (presumably symlink) named "tk" exists, use that one
# in preference to any others.  Same logic is used when choosing library
# and again with Tcl. The search order is the best place to look first, then in
# decreasing significance. The loop breaks if the trigger file is found.
# Note the gross little conversion here of srcdir by cd'ing to the found
# directory. This converts the path from a relative to an absolute, so
# recursive cache variables for the path will work right. We check all
# the possible paths in one loop rather than many separate loops to speed
# things up.
# the alternative search directory is involked by --with-tkinclude
#
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
no_tk=true
AC_MSG_CHECKING(for Tk headers in the source tree)
AC_ARG_WITH(tkinclude, [  --with-tkinclude       directory where tk headers are], with_tkinclude=${withval})
AC_CACHE_VAL(ac_cv_c_tkh,[
dnl first check to see if --with-tkinclude was specified
if test x"${with_tkinclude}" != x ; then
  if test -f ${with_tkinclude}/tk.h ; then
    ac_cv_c_tkh=`(cd ${with_tkinclude}; ${PWDCMD-pwd})`
  elif test -f ${with_tkinclude}/generic/tk.h ; then
    ac_cv_c_tkh=`(cd ${with_tkinclude}/generic; ${PWDCMD-pwd})`
  else
    AC_MSG_ERROR([${with_tkinclude} directory doesn't contain headers])
  fi
fi

dnl next check if it came with Tk configuration file
if test x"${ac_cv_c_tkconfig}" != x ; then
  for i in $dirlist; do
    if test -f $ac_cv_c_tkconfig/$i/generic/tk.h ; then
      ac_cv_c_tkh=`(cd $ac_cv_c_tkconfig/$i/generic; ${PWDCMD-pwd})`
      break
    fi
  done
fi

dnl next check in private source directory
dnl since ls returns lowest version numbers first, reverse its output
if test x"${ac_cv_c_tkh}" = x ; then
    dnl find the top level Tk source directory
    for i in $dirlist; do
        if test -n "`ls -dr $srcdir/$i/tk* 2>/dev/null`" ; then
	    tkpath=$srcdir/$i
	    break
	fi
    done

    dnl find the exact Tk source dir. We do it this way, cause there
    dnl might be multiple version of Tk, and we want the most recent one.
    for i in `ls -dr $tkpath/tk* 2>/dev/null ` ; do
        if test -f $i/generic/tk.h ; then
          ac_cv_c_tkh=`(cd $i/generic; ${PWDCMD-pwd})`
          break
        fi
    done
fi

dnl see if one is installed
if test x"${ac_cv_c_tkh}" = x ; then
    AC_MSG_RESULT(none)
    dnl Get the path to the compiler. We do it this way instead of using
    dnl AC_CHECK_HEADER, cause this doesn't depend in having X configured.
    ccpath=`which ${CC}  | sed -e 's:/bin/.*::'`/include
    if test -f $ccpath/tk.h; then
	ac_cv_c_tkh=$ccpath
    fi
else
   AC_MSG_RESULT(${ac_cv_c_tkh})
fi
])
  TKHDIR=""
if test x"${ac_cv_c_tkh}" = x ; then
    AC_MSG_ERROR([Can't find any Tk headers])
fi
if test x"${ac_cv_c_tkh}" != x ; then
    no_tk=""
    if test x"${ac_cv_c_tkh}" != x"installed" ; then
	if test x"${CC}" = xcl ; then
	    tmp="`cygpath --windows ${ac_cv_c_tkh}`"
	    ac_cv_c_tkh="`echo $tmp | sed -e s#\\\\\\\\#/#g`"
	fi
        AC_MSG_RESULT([found in ${ac_cv_c_tkh}])
        TKHDIR="-I${ac_cv_c_tkh}"
    fi
fi

AC_SUBST(TKHDIR)
])

AC_DEFUN([CYG_AC_PATH_TKCONFIG], [
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
dnl First, look for one uninstalled.  
dnl the alternative search directory is invoked by --with-tkconfig
if test x"${no_tk}" = x ; then
  dnl we reset no_tk in case something fails here
    no_tk=true
    AC_ARG_WITH(tkconfig, [  --with-tkconfig           directory containing tk configuration (tkConfig.sh)],
         with_tkconfig=${withval})
    AC_MSG_CHECKING([for Tk configuration script])
    AC_CACHE_VAL(ac_cv_c_tkconfig,[

    dnl First check to see if --with-tkconfig was specified.
    if test x"${with_tkconfig}" != x ; then
        if test -f "${with_tkconfig}/tkConfig.sh" ; then
            ac_cv_c_tkconfig=`(cd ${with_tkconfig}; ${PWDCMD-pwd})`
        else
            AC_MSG_ERROR([${with_tkconfig} directory doesn't contain tkConfig.sh])
        fi
    fi

    dnl next check if it came with Tk configuration file in the source tree
    if test x"${ac_cv_c_tkconfig}" = x ; then
        for i in $dirlist; do
            dnl need to test both unix and win directories, since 
            dnl cygwin's tkConfig.sh could be in either directory depending
            dnl on the cygwin port of tk.
            if test -f $srcdir/$i/unix/tkConfig.sh ; then
                ac_cv_c_tkconfig=`(cd $srcdir/$i/unix; ${PWDCMD-pwd})`
	        break
            fi
            if test -f $srcdir/$i/win/tkConfig.sh ; then
                ac_cv_c_tkconfig=`(cd $srcdir/$i/unix; ${PWDCMD-pwd})`
	        break
            fi
        done
    fi
    dnl check in a few other locations
    if test x"${ac_cv_c_tkconfig}" = x ; then
        dnl find the top level Tk source directory
        for i in $dirlist; do
            if test -n "`ls -dr $i/tk* 2>/dev/null`" ; then
	        tkconfpath=$i
	        break
	    fi
        done

        dnl find the exact Tk dir. We do it this way, cause there
        dnl might be multiple version of Tk, and we want the most recent one.
        for i in `ls -dr $tkconfpath/tk* 2>/dev/null ` ; do
            dnl need to test both unix and win directories, since 
            dnl cygwin's tkConfig.sh could be in either directory depending
            dnl on the cygwin port of tk.
            if test -f $i/unix/tkConfig.sh ; then
                ac_cv_c_tkconfig=`(cd $i/unix; ${PWDCMD-pwd})`
                break
            fi
            if test -f $i/win/tkConfig.sh ; then
                ac_cv_c_tkconfig=`(cd $i/win; ${PWDCMD-pwd})`
                break
            fi
        done
    fi

    dnl Check to see if it's installed. We have to look in the $CC path
    dnl to find it, cause our $prefix may not match the compilers.
    if test x"${ac_cv_c_tkconfig}" = x ; then
        dnl Get the path to the compiler
	ccpath=`which ${CC}  | sed -e 's:/bin/.*::'`/lib
        if test -f $ccpath/tkConfig.sh; then
	    ac_cv_c_tkconfig=$ccpath
        fi
    fi
    ])	dnl end of cache_val

    if test x"${ac_cv_c_tkconfig}" = x ; then
        TKCONFIG=""
        AC_MSG_WARN(Can't find Tk configuration definitions)
    else
        no_tk=""
        TKCONFIG=${ac_cv_c_tkconfig}/tkConfig.sh
        AC_MSG_RESULT(${TKCONFIG})
     fi
fi
AC_SUBST(TKCONFIG)
])

dnl Defined as a separate macro so we don't have to cache the values
dnl from PATH_TKCONFIG (because this can also be cached).
AC_DEFUN([CYG_AC_LOAD_TKCONFIG], [
    if test -f "$TKCONFIG" ; then
      . $TKCONFIG
    fi

    AC_SUBST(TK_VERSION)
dnl not actually used, don't export to save symbols
dnl    AC_SUBST(TK_MAJOR_VERSION)
dnl    AC_SUBST(TK_MINOR_VERSION)
    AC_SUBST(TK_DEFS)

dnl not used, don't export to save symbols
    AC_SUBST(TK_LIB_FILE)
    AC_SUBST(TK_LIB_FULL_PATH)
    AC_SUBST(TK_LIBS)
dnl not used, don't export to save symbols
dnl    AC_SUBST(TK_PREFIX)

dnl not used, don't export to save symbols
dnl    AC_SUBST(TK_EXEC_PREFIX)
    AC_SUBST(TK_BUILD_INCLUDES)
    AC_SUBST(TK_XINCLUDES)
    AC_SUBST(TK_XLIBSW)
    AC_SUBST(TK_BUILD_LIB_SPEC)
    AC_SUBST(TK_LIB_SPEC)
])

dnl ====================================================================
dnl Ok, lets find the itcl source trees so we can use the headers
dnl the alternative search directory is involked by --with-itclinclude
AC_DEFUN([CYG_AC_PATH_ITCL], [
    CYG_AC_PATH_ITCLH
    CYG_AC_PATH_ITCLLIB
    CYG_AC_PATH_ITCLSH
    CYG_AC_PATH_ITCLMKIDX
])
AC_DEFUN([CYG_AC_PATH_ITCLH], [
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
no_itcl=true
AC_MSG_CHECKING(for Itcl headers in the source tree)
AC_ARG_WITH(itclinclude, [  --with-itclinclude       directory where itcl headers are], with_itclinclude=${withval})
AC_CACHE_VAL(ac_cv_c_itclh,[
dnl first check to see if --with-itclinclude was specified
if test x"${with_itclinclude}" != x ; then
  if test -f ${with_itclinclude}/itcl.h ; then
    ac_cv_c_itclh=`(cd ${with_itclinclude}; ${PWDCMD-pwd})`
  elif test -f ${with_itclinclude}/src/itcl.h ; then
    ac_cv_c_itclh=`(cd ${with_itclinclude}/src; ${PWDCMD-pwd})`
  else
    AC_MSG_ERROR([${with_itclinclude} directory doesn't contain headers])
  fi
fi

dnl next check if it came with Itcl configuration file
if test x"${ac_cv_c_itclconfig}" != x ; then
  for i in $dirlist; do
    if test -f $ac_cv_c_itclconfig/$i/src/itcl.h ; then
      ac_cv_c_itclh=`(cd $ac_cv_c_itclconfig/$i/src; ${PWDCMD-pwd})`
      break
    fi
  done
fi

dnl next check in private source directory
dnl since ls returns lowest version numbers first, reverse its output
if test x"${ac_cv_c_itclh}" = x ; then
    dnl find the top level Itcl source directory
    for i in $dirlist; do
        if test -n "`ls -dr $srcdir/$i/itcl* 2>/dev/null`" ; then
	    itclpath=$srcdir/$i
	    break
	fi
    done

    dnl find the exact Itcl source dir. We do it this way, cause there
    dnl might be multiple version of Itcl, and we want the most recent one.
    for i in `ls -dr $itclpath/itcl* 2>/dev/null ` ; do
        if test -f $i/src/itcl.h ; then
          ac_cv_c_itclh=`(cd $i/src; ${PWDCMD-pwd})`
          break
        fi
    done
fi

dnl see if one is installed
if test x"${ac_cv_c_itclh}" = x ; then
   AC_MSG_RESULT(none)
   AC_CHECK_HEADER(itcl.h, ac_cv_c_itclh=installed, ac_cv_c_itclh="")
else
   AC_MSG_RESULT(${ac_cv_c_itclh})
fi
])
  ITCLHDIR=""
if test x"${ac_cv_c_itclh}" = x ; then
    AC_MSG_ERROR([Can't find any Itcl headers])
fi
if test x"${ac_cv_c_itclh}" != x ; then
    no_itcl=""
    if test x"${ac_cv_c_itclh}" != x"installed" ; then
        AC_MSG_RESULT(${ac_cv_c_itclh})
        ITCLHDIR="-I${ac_cv_c_itclh}"
    fi
fi

AC_SUBST(ITCLHDIR)
])

dnl Ok, lets find the itcl library
dnl First, look for one uninstalled.  
dnl the alternative search directory is invoked by --with-itcllib
AC_DEFUN([CYG_AC_PATH_ITCLLIB], [
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
if test x"${no_itcl}" = x ; then
    dnl we reset no_itcl incase something fails here
    no_itcl=true
    AC_ARG_WITH(itcllib,
	[  --with-itcllib           directory where the itcl library is],
        with_itcllib=${withval})
    AC_MSG_CHECKING([for Itcl library])
    AC_CACHE_VAL(ac_cv_c_itcllib,[
    dnl First check to see if --with-itcllib was specified.
    if test x"${with_itcllib}" != x ; then
        if test -f "${with_itcllib}/libitcl$TCL_SHARED_LIB_SUFFIX" ; then
            ac_cv_c_itcllib=`(cd ${with_itcllib}; ${PWDCMD-pwd})`/libitcl$TCL_SHARED_LIB_SUFFIX
	else
	    if test -f "${with_itcllib}/libitcl$TCL_UNSHARED_LIB_SUFFIX"; then
	 	ac_cv_c_itcllib=`(cd ${with_itcllib}; ${PWDCMD-pwd})`/libitcl$TCL_UNSHARED_LIB_SUFFIX
	    fi
	fi
    fi
    dnl then check for a  Itcl library. Since these are uninstalled,
    dnl use the simple lib name root. 
    if test x"${ac_cv_c_itcllib}" = x ; then
        dnl find the top level Itcl build directory
        for i in $dirlist; do
            if test -n "`ls -dr $i/itcl* 2>/dev/null`" ; then
	        itclpath=$i/itcl
	        break
	    fi
        done
        dnl Itcl 7.5 and greater puts library in subdir.  Look there first.
        if test -f "$itclpath/src/libitcl.$TCL_SHLIB_SUFFIX" ; then
	     ac_cv_c_itcllib=`(cd $itclpath/src; ${PWDCMD-pwd})`
        elif test -f "$itclpath/src/libitcl.a"; then
	     ac_cv_c_itcllib=`(cd $itclpath/src; ${PWDCMD-pwd})`
	fi
    fi
    dnl check in a few other private locations
    if test x"${ac_cv_c_itcllib}" = x ; then
        for i in ${dirlist}; do
            if test -n "`ls -dr ${srcdir}/$i/itcl* 2>/dev/null`" ; then
	        itclpath=${srcdir}/$i
	        break
	    fi
        done
        for i in `ls -dr ${itclpath}/itcl* 2>/dev/null` ; do
            dnl Itcl 7.5 and greater puts library in subdir.  Look there first.
            if test -f "$i/src/libitcl$TCL_SHLIB_SUFFIX" ; then
	        ac_cv_c_itcllib=`(cd $i/src; ${PWDCMD-pwd})`
	        break
            elif test -f "$i/src/libitcl.a"; then
	        ac_cv_c_itcllib=`(cd $i/src; ${PWDCMD-pwd})`
	        break
	    fi	
        done
    fi

    dnl see if one is conveniently installed with the compiler
    if test x"${ac_cv_c_itcllib}" = x ; then
        dnl Get the path to the compiler
	ccpath=`which ${CC}  | sed -e 's:/bin/.*::'`/lib
        dnl Itcl 7.5 and greater puts library in subdir.  Look there first.
        if test -f "${ccpath}/libitcl$TCL_SHLIB_SUFFIX" ; then
	    ac_cv_c_itcllib=`(cd ${ccpath}; ${PWDCMD-pwd})`
        elif test -f "${ccpath}/libitcl.a"; then
	    ac_cv_c_itcllib=`(cd ${ccpath}; ${PWDCMD-pwd})`
        fi
    fi
    ])
    if test x"${ac_cv_c_itcllib}" = x ; then
        ITCLLIB=""
        AC_MSG_WARN(Can't find Itcl library)
    else
        ITCLLIB="-L${ac_cv_c_itcllib}"
        AC_MSG_RESULT(${ac_cv_c_itcllib})
        no_itcl=""
    fi
fi

AC_PROVIDE([$0])
AC_SUBST(ITCLLIB)
])


dnl ====================================================================
dnl Ok, lets find the itcl source trees so we can use the itcl_sh script
dnl the alternative search directory is involked by --with-itclinclude
AC_DEFUN([CYG_AC_PATH_ITCLSH], [
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
no_itcl=true
AC_MSG_CHECKING(for the itcl_sh script)
AC_ARG_WITH(itclinclude, [  --with-itclinclude       directory where itcl headers are], with_itclinclude=${withval})
AC_CACHE_VAL(ac_cv_c_itclsh,[
dnl first check to see if --with-itclinclude was specified
if test x"${with_itclinclude}" != x ; then
  if test -f ${with_itclinclude}/itcl_sh ; then
    ac_cv_c_itclsh=`(cd ${with_itclinclude}; ${PWDCMD-pwd})`
  elif test -f ${with_itclinclude}/src/itcl_sh ; then
    ac_cv_c_itclsh=`(cd ${with_itclinclude}/src; ${PWDCMD-pwd})`
  else
    AC_MSG_ERROR([${with_itclinclude} directory doesn't contain itcl_sh])
  fi
fi

dnl next check in private source directory
dnl since ls returns lowest version numbers first, reverse its output
if test x"${ac_cv_c_itclsh}" = x ; then
    dnl find the top level Itcl source directory
    for i in $dirlist; do
        if test -n "`ls -dr $srcdir/$i/itcl* 2>/dev/null`" ; then
	    itclpath=$srcdir/$i
	    break
	fi
    done

    dnl find the exact Itcl source dir. We do it this way, cause there
    dnl might be multiple version of Itcl, and we want the most recent one.
    for i in `ls -dr $itclpath/itcl* 2>/dev/null ` ; do
        if test -f $i/src/itcl_sh ; then
          ac_cv_c_itclsh=`(cd $i/src; ${PWDCMD-pwd})`/itcl_sh
          break
        fi
    done
fi

dnl see if one is installed
if test x"${ac_cv_c_itclsh}" = x ; then
   AC_MSG_RESULT(none)
   AC_PATH_PROG(ac_cv_c_itclsh, itcl_sh)
else
   AC_MSG_RESULT(${ac_cv_c_itclsh})
fi
])

if test x"${ac_cv_c_itclsh}" = x ; then
    AC_MSG_ERROR([Can't find the itcl_sh script])
fi
if test x"${ac_cv_c_itclsh}" != x ; then
    no_itcl=""
    AC_MSG_RESULT(${ac_cv_c_itclsh})
    ITCLSH="${ac_cv_c_itclsh}"
fi
AC_SUBST(ITCLSH)
])


dnl ====================================================================
dnl Ok, lets find the itcl source trees so we can use the itcl_sh script
dnl the alternative search directory is involked by --with-itclinclude
AC_DEFUN([CYG_AC_PATH_ITCLMKIDX], [
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
no_itcl=true
AC_MSG_CHECKING(for itcl_mkindex.tcl script)
AC_ARG_WITH(itclinclude, [  --with-itclinclude       directory where itcl headers are], with_itclinclude=${withval})
AC_CACHE_VAL(ac_cv_c_itclmkidx,[
dnl first check to see if --with-itclinclude was specified
if test x"${with_itclinclude}" != x ; then
  if test -f ${with_itclinclude}/itcl_sh ; then
    ac_cv_c_itclmkidx=`(cd ${with_itclinclude}; ${PWDCMD-pwd})`
  elif test -f ${with_itclinclude}/src/itcl_sh ; then
    ac_cv_c_itclmkidx=`(cd ${with_itclinclude}/src; ${PWDCMD-pwd})`
  else
    AC_MSG_ERROR([${with_itclinclude} directory doesn't contain itcl_sh])
  fi
fi

dnl next check in private source directory
dnl since ls returns lowest version numbers first, reverse its output
if test x"${ac_cv_c_itclmkidx}" = x ; then
    dnl find the top level Itcl source directory
    for i in $dirlist; do
        if test -n "`ls -dr $srcdir/$i/itcl* 2>/dev/null`" ; then
	    itclpath=$srcdir/$i
	    break
	fi
    done

    dnl find the exact Itcl source dir. We do it this way, cause there
    dnl might be multiple version of Itcl, and we want the most recent one.
    for i in `ls -dr $itclpath/itcl* 2>/dev/null ` ; do
        if test -f $i/library/itcl_mkindex.tcl ; then
          ac_cv_c_itclmkidx=`(cd $i/library; ${PWDCMD-pwd})`/itcl_mkindex.tcl
          break
        fi
    done
fi
if test x"${ac_cv_c_itclmkidx}" = x ; then
    dnl Get the path to the compiler
    ccpath=`which ${CC}  | sed -e 's:/bin/.*::'`/share
    dnl Itcl 7.5 and greater puts library in subdir.  Look there first.
    for i in `ls -dr $ccpath/itcl* 2>/dev/null ` ; do
        if test -f $i/itcl_mkindex.tcl ; then
            ac_cv_c_itclmkidx=`(cd $i; ${PWDCMD-pwd})`/itcl_mkindex.tcl
            break
        fi
    done
fi
])

if test x"${ac_cv_c_itclmkidx}" = x ; then
    AC_MSG_ERROR([Can't find the itcl_mkindex.tcl script])
fi
if test x"${ac_cv_c_itclmkidx}" != x ; then
    no_itcl=""
    AC_MSG_RESULT(${ac_cv_c_itclmkidx})
    ITCLMKIDX="${ac_cv_c_itclmkidx}"
else
   AC_MSG_RESULT(none)
fi
AC_SUBST(ITCLMKIDX)
])

dnl ====================================================================
dnl Ok, lets find the tix source trees so we can use the headers
dnl the alternative search directory is involked by --with-tixinclude
AC_DEFUN([CYG_AC_PATH_TIX], [
    CYG_AC_PATH_TIXH
    CYG_AC_PATH_TIXLIB
])
AC_DEFUN([CYG_AC_PATH_TIXH], [
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
no_tix=true
AC_MSG_CHECKING(for Tix headers in the source tree)
AC_ARG_WITH(tixinclude, [  --with-tixinclude       directory where tix headers are], with_tixinclude=${withval})
AC_CACHE_VAL(ac_cv_c_tixh,[
dnl first check to see if --with-tixinclude was specified
if test x"${with_tixinclude}" != x ; then
  if test -f ${with_tixinclude}/tix.h ; then
    ac_cv_c_tixh=`(cd ${with_tixinclude}; ${PWDCMD-pwd})`
  elif test -f ${with_tixinclude}/generic/tix.h ; then
    ac_cv_c_tixh=`(cd ${with_tixinclude}/generic; ${PWDCMD-pwd})`
  else
    AC_MSG_ERROR([${with_tixinclude} directory doesn't contain headers])
  fi
fi

dnl next check if it came with Tix configuration file
if test x"${ac_cv_c_tixconfig}" != x ; then
  for i in $dirlist; do
    if test -f $ac_cv_c_tixconfig/$i/generic/tix.h ; then
      ac_cv_c_tixh=`(cd $ac_cv_c_tixconfig/$i/generic; ${PWDCMD-pwd})`
      break
    fi
  done
fi

dnl next check in private source directory
dnl since ls returns lowest version numbers first, reverse its output
if test x"${ac_cv_c_tixh}" = x ; then
    dnl find the top level Tix source directory
    for i in $dirlist; do
        if test -n "`ls -dr $srcdir/$i/tix* 2>/dev/null`" ; then
	    tixpath=$srcdir/$i
	    break
	fi
    done

    dnl find the exact Tix source dir. We do it this way, cause there
    dnl might be multiple version of Tix, and we want the most recent one.
    for i in `ls -dr $tixpath/tix* 2>/dev/null ` ; do
        if test -f $i/generic/tix.h ; then
          ac_cv_c_tixh=`(cd $i/generic; ${PWDCMD-pwd})`
          break
        fi
    done
fi

dnl see if one is installed
if test x"${ac_cv_c_tixh}" = x ; then
    AC_MSG_RESULT(none)
    dnl Get the path to the compiler

   dnl Get the path to the compiler. We do it this way instead of using
    dnl AC_CHECK_HEADER, cause this doesn't depend in having X configured.
    ccpath=`which ${CC}  | sed -e 's:/bin/.*::'`/include
    if test -f $ccpath/tix.h; then
	ac_cv_c_tixh=installed
    fi
else
   AC_MSG_RESULT(${ac_cv_c_tixh})
fi
])
if test x"${ac_cv_c_tixh}" = x ; then
    AC_MSG_ERROR([Can't find any Tix headers])
fi
if test x"${ac_cv_c_tixh}" != x ; then
    no_tix=""
    AC_MSG_RESULT(${ac_cv_c_tixh})
    if test x"${ac_cv_c_tixh}" != x"installed" ; then
        TIXHDIR="-I${ac_cv_c_tixh}"
    fi
fi

AC_SUBST(TIXHDIR)
])

AC_DEFUN([CYG_AC_PATH_TIXCONFIG], [
#
# Ok, lets find the tix configuration
# First, look for one uninstalled.  
# the alternative search directory is invoked by --with-tixconfig
#

if test x"${no_tix}" = x ; then
  # we reset no_tix in case something fails here
  no_tix=true
  AC_ARG_WITH(tixconfig, [  --with-tixconfig           directory containing tix configuration (tixConfig.sh)],
         with_tixconfig=${withval})
  AC_MSG_CHECKING([for Tix configuration])
  AC_CACHE_VAL(ac_cv_c_tixconfig,[

  # First check to see if --with-tixconfig was specified.
  if test x"${with_tixconfig}" != x ; then
    if test -f "${with_tixconfig}/tixConfig.sh" ; then
      ac_cv_c_tixconfig=`(cd ${with_tixconfig}; ${PWDCMD-pwd})`
    else
      AC_MSG_ERROR([${with_tixconfig} directory doesn't contain tixConfig.sh])
    fi
  fi

  # then check for a private Tix library
  if test x"${ac_cv_c_tixconfig}" = x ; then
    for i in \
		../tix \
		`ls -dr ../tix[[4]]* 2>/dev/null` \
		../../tix \
		`ls -dr ../../tix[[4]]* 2>/dev/null` \
		../../../tix \
		`ls -dr ../../../tix[[4]]* 2>/dev/null` ; do
      if test -f "$i/tixConfig.sh" ; then
        ac_cv_c_tixconfig=`(cd $i; ${PWDCMD-pwd})`
	break
      fi
    done
  fi
  # check in a few common install locations
  if test x"${ac_cv_c_tixconfig}" = x ; then
    for i in `ls -d ${prefix}/lib /usr/local/lib 2>/dev/null` ; do
      if test -f "$i/tixConfig.sh" ; then
        ac_cv_c_tkconfig=`(cd $i; ${PWDCMD-pwd})`
	break
      fi
    done
  fi
  # check in a few other private locations
  if test x"${ac_cv_c_tixconfig}" = x ; then
    for i in \
		${srcdir}/../tix \
		`ls -dr ${srcdir}/../tix[[4-9]]* 2>/dev/null` ; do
      if test -f "$i/tixConfig.sh" ; then
        ac_cv_c_tixconfig=`(cd $i; ${PWDCMD-pwd})`
	break
      fi
    done
  fi
  ])
  if test x"${ac_cv_c_tixconfig}" = x ; then
    TIXCONFIG="# no Tix configs found"
    AC_MSG_WARN(Can't find Tix configuration definitions)
  else
    no_tix=
    TIXCONFIG=${ac_cv_c_tixconfig}/tixConfig.sh
    AC_MSG_RESULT(found $TIXCONFIG)
  fi
fi

])

# Defined as a separate macro so we don't have to cache the values
# from PATH_TIXCONFIG (because this can also be cached).
AC_DEFUN([CYG_AC_LOAD_TIXCONFIG], [
    if test -f "$TIXCONFIG" ; then
      . $TIXCONFIG
    fi

    AC_SUBST(TIX_BUILD_LIB_SPEC)
    AC_SUBST(TIX_LIB_FULL_PATH)
])

AC_DEFUN([CYG_AC_PATH_ITCLCONFIG], [
#
# Ok, lets find the itcl configuration
# First, look for one uninstalled.  
# the alternative search directory is invoked by --with-itclconfig
#

if test x"${no_itcl}" = x ; then
  # we reset no_itcl in case something fails here
  no_itcl=true
  AC_ARG_WITH(itclconfig, [  --with-itclconfig           directory containing itcl configuration (itclConfig.sh)],
         with_itclconfig=${withval})
  AC_MSG_CHECKING([for Itcl configuration])
  AC_CACHE_VAL(ac_cv_c_itclconfig,[

  # First check to see if --with-itclconfig was specified.
  if test x"${with_itclconfig}" != x ; then
    if test -f "${with_itclconfig}/itclConfig.sh" ; then
      ac_cv_c_itclconfig=`(cd ${with_itclconfig}; ${PWDCMD-pwd})`
    else
      AC_MSG_ERROR([${with_itclconfig} directory doesn't contain itclConfig.sh])
    fi
  fi

  # then check for a private itcl library
  if test x"${ac_cv_c_itclconfig}" = x ; then
    for i in \
		../itcl/itcl \
		`ls -dr ../itcl/itcl[[3]]* 2>/dev/null` \
		../../itcl/itcl \
		`ls -dr ../../itcl/itcl[[3]]* 2>/dev/null` \
		../../../itcl/itcl \
		`ls -dr ../../../itcl/itcl[[3]]* 2>/dev/null` ; do
      if test -f "$i/itclConfig.sh" ; then
        ac_cv_c_itclconfig=`(cd $i; ${PWDCMD-pwd})`
	break
      fi
    done
  fi
  # check in a few common install locations
  if test x"${ac_cv_c_itclconfig}" = x ; then
    for i in `ls -d ${prefix}/lib /usr/local/lib 2>/dev/null` ; do
      if test -f "$i/itclConfig.sh" ; then
        ac_cv_c_itclconfig=`(cd $i; ${PWDCMD-pwd})`
	break
      fi
    done
  fi
  # check in a few other private locations
  if test x"${ac_cv_c_itclconfig}" = x ; then
    for i in \
		${srcdir}/../itcl/itcl \
		`ls -dr ${srcdir}/../itcl/itcl[[3]]* 2>/dev/null` ; do
      if test -f "$i/itcl/itclConfig.sh" ; then
        ac_cv_c_itclconfig=`(cd $i; ${PWDCMD-pwd})`
	break
      fi
    done
  fi
  ])
  if test x"${ac_cv_c_itclconfig}" = x ; then
    ITCLCONFIG="# no itcl configs found"
    AC_MSG_WARN(Can't find itcl configuration definitions)
  else
    no_itcl=
    ITCLCONFIG=${ac_cv_c_itclconfig}/itclConfig.sh
    AC_MSG_RESULT(found $ITCLCONFIG)
  fi
fi

])

# Defined as a separate macro so we don't have to cache the values
# from PATH_ITCLCONFIG (because this can also be cached).
AC_DEFUN([CYG_AC_LOAD_ITCLCONFIG], [
    if test -f "$ITCLCONFIG" ; then
      . $ITCLCONFIG
    fi

    AC_SUBST(ITCL_BUILD_LIB_SPEC)
    AC_SUBST(ITCL_SH)
    AC_SUBST(ITCL_LIB_FILE)
    AC_SUBST(ITCL_LIB_FULL_PATH)

])


AC_DEFUN([CYG_AC_PATH_ITKCONFIG], [
#
# Ok, lets find the itk configuration
# First, look for one uninstalled.  
# the alternative search directory is invoked by --with-itkconfig
#

if test x"${no_itk}" = x ; then
  # we reset no_itk in case something fails here
  no_itk=true
  AC_ARG_WITH(itkconfig, [  --with-itkconfig           directory containing itk configuration (itkConfig.sh)],
         with_itkconfig=${withval})
  AC_MSG_CHECKING([for Itk configuration])
  AC_CACHE_VAL(ac_cv_c_itkconfig,[

  # First check to see if --with-itkconfig was specified.
  if test x"${with_itkconfig}" != x ; then
    if test -f "${with_itkconfig}/itkConfig.sh" ; then
      ac_cv_c_itkconfig=`(cd ${with_itkconfig}; ${PWDCMD-pwd})`
    else
      AC_MSG_ERROR([${with_itkconfig} directory doesn't contain itkConfig.sh])
    fi
  fi

  # then check for a private itk library
  if test x"${ac_cv_c_itkconfig}" = x ; then
    for i in \
		../itcl/itk \
		`ls -dr ../itcl/itk[[3]]* 2>/dev/null` \
		../../itcl/itk \
		`ls -dr ../../itcl/itk[[3]]* 2>/dev/null` \
		../../../itcl/itk \
		`ls -dr ../../../itcl/itk[[3]]* 2>/dev/null` ; do
      if test -f "$i/itkConfig.sh" ; then
        ac_cv_c_itkconfig=`(cd $i; ${PWDCMD-pwd})`
	break
      fi
    done
  fi
  # check in a few common install locations
  if test x"${ac_cv_c_itkconfig}" = x ; then
    for i in `ls -d ${prefix}/lib /usr/local/lib 2>/dev/null` ; do
      if test -f "$i/itcl/itkConfig.sh" ; then
        ac_cv_c_itkconfig=`(cd $i; ${PWDCMD-pwd})`
	break
      fi
    done
  fi
  # check in a few other private locations
  if test x"${ac_cv_c_itkconfig}" = x ; then
    for i in \
		${srcdir}/../itcl/itk \
		`ls -dr ${srcdir}/../itcl/itk[[3]]* 2>/dev/null` ; do
      if test -f "$i/itkConfig.sh" ; then
        ac_cv_c_itkconfig=`(cd $i; ${PWDCMD-pwd})`
	break
      fi
    done
  fi
  ])
  if test x"${ac_cv_c_itkconfig}" = x ; then
    ITCLCONFIG="# no itk configs found"
    AC_MSG_WARN(Can't find itk configuration definitions)
  else
    no_itk=
    ITKCONFIG=${ac_cv_c_itkconfig}/itkConfig.sh
    AC_MSG_RESULT(found $ITKCONFIG)
  fi
fi

])

# Defined as a separate macro so we don't have to cache the values
# from PATH_ITKCONFIG (because this can also be cached).
AC_DEFUN([CYG_AC_LOAD_ITKCONFIG], [
    if test -f "$ITKCONFIG" ; then
      . $ITKCONFIG
    fi

    AC_SUBST(ITK_BUILD_LIB_SPEC)
    AC_SUBST(ITK_LIB_FILE)
    AC_SUBST(ITK_LIB_FULL_PATH)
])


dnl ====================================================================
dnl Ok, lets find the libgui source trees so we can use the headers
dnl the alternative search directory is involked by --with-libguiinclude
AC_DEFUN([CYG_AC_PATH_LIBGUI], [
    CYG_AC_PATH_LIBGUIH
    CYG_AC_PATH_LIBGUILIB
])
AC_DEFUN([CYG_AC_PATH_LIBGUIH], [
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../..../../../../../../../../../../.."
no_libgui=true
AC_MSG_CHECKING(for Libgui headers in the source tree)
AC_ARG_WITH(libguiinclude, [  --with-libguiinclude       directory where libgui headers are], with_libguiinclude=${withval})
AC_CACHE_VAL(ac_cv_c_libguih,[
dnl first check to see if --with-libguiinclude was specified
if test x"${with_libguiinclude}" != x ; then
  if test -f ${with_libguiinclude}/guitcl.h ; then
    ac_cv_c_libguih=`(cd ${with_libguiinclude}; ${PWDCMD-pwd})`
  elif test -f ${with_libguiinclude}/src/guitcl.h ; then
    ac_cv_c_libguih=`(cd ${with_libguiinclude}/src; ${PWDCMD-pwd})`
  else
    AC_MSG_ERROR([${with_libguiinclude} directory doesn't contain headers])
  fi
fi

dnl next check if it came with Libgui configuration file
if test x"${ac_cv_c_libguiconfig}" != x ; then
  for i in $dirlist; do
    if test -f $ac_cv_c_libguiconfig/$i/src/guitcl.h ; then
      ac_cv_c_libguih=`(cd $ac_cv_c_libguiconfig/$i/src; ${PWDCMD-pwd})`
      break
    fi
  done
fi

dnl next check in private source directory
dnl since ls returns lowest version numbers first, reverse its output
if test x"${ac_cv_c_libguih}" = x ; then
    dnl find the top level Libgui source directory
    for i in $dirlist; do
        if test -n "`ls -dr $srcdir/$i/libgui* 2>/dev/null`" ; then
	    libguipath=$srcdir/$i
	    break
	fi
    done

    dnl find the exact Libgui source dir. We do it this way, cause there
    dnl might be multiple version of Libgui, and we want the most recent one.
    for i in `ls -dr $libguipath/libgui* 2>/dev/null ` ; do
        if test -f $i/src/guitcl.h ; then
          ac_cv_c_libguih=`(cd $i/src; ${PWDCMD-pwd})`
          break
        fi
    done
fi

dnl see if one is installed
if test x"${ac_cv_c_libguih}" = x ; then
   AC_MSG_RESULT(none)
   AC_CHECK_HEADER(guitcl.h, ac_cv_c_libguih=installed, ac_cv_c_libguih="")
fi
])
LIBGUIHDIR=""
if test x"${ac_cv_c_libguih}" = x ; then
    AC_MSG_WARN([Can't find any Libgui headers])
fi
if test x"${ac_cv_c_libguih}" != x ; then
    no_libgui=""
    if test x"${ac_cv_c_libguih}" != x"installed" ; then
        LIBGUIHDIR="-I${ac_cv_c_libguih}"
    fi
fi
AC_MSG_RESULT(${ac_cv_c_libguih})
AC_SUBST(LIBGUIHDIR)
])

dnl ====================================================================
dnl find the GUI library
AC_DEFUN([CYG_AC_PATH_LIBGUILIB], [
AC_MSG_CHECKING(for GUI library  in the build tree)
dirlist=".. ../../ ../../../ ../../../../ ../../../../../ ../../../../../../ ../../../../../../.. ../../../../../../../.. ../../../../../../../../.. ../../../../../../../../../.."
dnl look for the library
AC_MSG_CHECKING(for GUI library)
AC_CACHE_VAL(ac_cv_c_libguilib,[
if test x"${ac_cv_c_libguilib}" = x ; then
    for i in $dirlist; do
      if test -f "$i/libgui/src/Makefile" ; then
        ac_cv_c_libguilib=`(cd $i/libgui/src; ${PWDCMD-pwd})`
        break
      fi
    done
fi
]) 
if test x"${ac_cv_c_libguilib}" != x ; then
     GUILIB="${GUILIB} -L${ac_cv_c_libguilib}"
     LIBGUILIB="-lgui"
     AC_MSG_RESULT(${ac_cv_c_libguilib})
else
     AC_MSG_RESULT(none)
fi

AC_SUBST(GUILIB)
AC_SUBST(LIBGUILIB)
])
