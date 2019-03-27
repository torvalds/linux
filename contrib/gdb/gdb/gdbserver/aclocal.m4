dnl aclocal.m4 generated automatically by aclocal 1.4-p4

dnl Copyright (C) 1994, 1995-8, 1999 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY, to the extent permitted by law; without
dnl even the implied warranty of MERCHANTABILITY or FITNESS FOR A
dnl PARTICULAR PURPOSE.

dnl gdb/gdbserver/configure.in uses BFD_HAVE_SYS_PROCFS_TYPE.
sinclude(../../bfd/acinclude.m4)

AC_DEFUN([SRV_CHECK_THREAD_DB],
[AC_CACHE_CHECK([for libthread_db],[srv_cv_thread_db],
 [old_LIBS="$LIBS"
  LIBS="$LIBS -lthread_db"
  AC_TRY_LINK(
  [void ps_pglobal_lookup() {}
   void ps_pdread() {}
   void ps_pdwrite() {}
   void ps_lgetregs() {}
   void ps_lsetregs() {}
   void ps_lgetfpregs() {}
   void ps_lsetfpregs() {}
   void ps_getpid() {}],
  [td_ta_new();],
  [srv_cv_thread_db="-lthread_db"],
  [srv_cv_thread_db=no

 if test "$prefix" = "/usr" || test "$prefix" = "NONE"; then
  thread_db="/lib/libthread_db.so.1"
 else
  thread_db='$prefix/lib/libthread_db.so.1'
 fi
 LIBS="$old_LIBS `eval echo "$thread_db"`"
 AC_TRY_LINK(
  [void ps_pglobal_lookup() {}
   void ps_pdread() {}
   void ps_pdwrite() {}
   void ps_lgetregs() {}
   void ps_lsetregs() {}
   void ps_lgetfpregs() {}
   void ps_lsetfpregs() {}
   void ps_getpid() {}],
  [td_ta_new();],
  [srv_cv_thread_db="$thread_db"],
  [srv_cv_thread_db=no])
 LIBS="$old_LIBS"
 ]])
)])

