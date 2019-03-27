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
