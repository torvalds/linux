
#-----------------------------------------------------------------------
# Supports the following non-standard switches.
#
#   --enable-threadsafe
#   --enable-readline
#   --enable-editline
#   --enable-static-shell
#   --enable-dynamic-extensions
#

AC_PREREQ(2.61)
AC_INIT(sqlite, 3.26.0, http://www.sqlite.org)
AC_CONFIG_SRCDIR([sqlite3.c])
AC_CONFIG_AUX_DIR([.])

# Use automake.
AM_INIT_AUTOMAKE([foreign])

AC_SYS_LARGEFILE

# Check for required programs.
AC_PROG_CC
AC_PROG_LIBTOOL
AC_PROG_MKDIR_P

# Check for library functions that SQLite can optionally use.
AC_CHECK_FUNCS([fdatasync usleep fullfsync localtime_r gmtime_r])
AC_FUNC_STRERROR_R

AC_CONFIG_FILES([Makefile sqlite3.pc])
BUILD_CFLAGS=
AC_SUBST(BUILD_CFLAGS)

#-------------------------------------------------------------------------
# Two options to enable readline compatible libraries: 
#
#   --enable-editline
#   --enable-readline
#
# Both are enabled by default. If, after command line processing both are
# still enabled, the script searches for editline first and automatically
# disables readline if it is found. So, to use readline explicitly, the
# user must pass "--disable-editline". To disable command line editing
# support altogether, "--disable-editline --disable-readline".
#
# When searching for either library, check for headers before libraries 
# as some distros supply packages that contain libraries but not header
# files, which come as a separate development package.
#
AC_ARG_ENABLE(editline, [AS_HELP_STRING([--enable-editline],[use BSD libedit])])
AC_ARG_ENABLE(readline, [AS_HELP_STRING([--enable-readline],[use readline])])

AS_IF([ test x"$enable_editline" != xno ],[
  AC_CHECK_HEADERS([editline/readline.h],[
    sLIBS=$LIBS
    LIBS=""
    AC_SEARCH_LIBS([readline],[edit],[
      AC_DEFINE([HAVE_EDITLINE],1,Define to use BSD editline)
      READLINE_LIBS="$LIBS -ltinfo"
      enable_readline=no
    ],[],[-ltinfo])
    AS_UNSET(ac_cv_search_readline)
    LIBS=$sLIBS
  ])
])

AS_IF([ test x"$enable_readline" != xno ],[
  AC_CHECK_HEADERS([readline/readline.h],[
    sLIBS=$LIBS
    LIBS=""
    AC_SEARCH_LIBS(tgetent, termcap curses ncurses ncursesw, [], [])
    AC_SEARCH_LIBS(readline,[readline edit], [
      AC_DEFINE([HAVE_READLINE],1,Define to use readline or wrapper)
      READLINE_LIBS=$LIBS
    ])
    LIBS=$sLIBS
  ])
])

AC_SUBST(READLINE_LIBS)
#-----------------------------------------------------------------------

#-----------------------------------------------------------------------
#   --enable-threadsafe
#
AC_ARG_ENABLE(threadsafe, [AS_HELP_STRING(
  [--enable-threadsafe], [build a thread-safe library [default=yes]])], 
  [], [enable_threadsafe=yes])
if test x"$enable_threadsafe" != "xno"; then
  BUILD_CFLAGS="$BUILD_CFLAGS -D_REENTRANT=1 -DSQLITE_THREADSAFE=1"
  AC_SEARCH_LIBS(pthread_create, pthread)
  AC_SEARCH_LIBS(pthread_mutexattr_init, pthread)
fi
#-----------------------------------------------------------------------

#-----------------------------------------------------------------------
#   --enable-dynamic-extensions
#
AC_ARG_ENABLE(dynamic-extensions, [AS_HELP_STRING(
  [--enable-dynamic-extensions], [support loadable extensions [default=yes]])], 
  [], [enable_dynamic_extensions=yes])
if test x"$enable_dynamic_extensions" != "xno"; then
  AC_SEARCH_LIBS(dlopen, dl)
else
  BUILD_CFLAGS="$BUILD_CFLAGS -DSQLITE_OMIT_LOAD_EXTENSION=1"
fi
AC_MSG_CHECKING([for whether to support dynamic extensions])
AC_MSG_RESULT($enable_dynamic_extensions)
#-----------------------------------------------------------------------

#-----------------------------------------------------------------------
#   --enable-fts4
#
AC_ARG_ENABLE(fts4, [AS_HELP_STRING(
  [--enable-fts4], [include fts4 support [default=yes]])], 
  [], [enable_fts4=yes])
if test x"$enable_fts4" = "xyes"; then
  BUILD_CFLAGS="$BUILD_CFLAGS -DSQLITE_ENABLE_FTS4"
fi
#-----------------------------------------------------------------------

#-----------------------------------------------------------------------
#   --enable-fts3
#
AC_ARG_ENABLE(fts3, [AS_HELP_STRING(
  [--enable-fts3], [include fts3 support [default=no]])], 
  [], [])
if test x"$enable_fts3" = "xyes" -a x"$enable_fts4" = "xno"; then
  BUILD_CFLAGS="$BUILD_CFLAGS -DSQLITE_ENABLE_FTS3"
fi
#-----------------------------------------------------------------------

#-----------------------------------------------------------------------
#   --enable-fts5
#
AC_ARG_ENABLE(fts5, [AS_HELP_STRING(
  [--enable-fts5], [include fts5 support [default=yes]])], 
  [], [enable_fts5=yes])
if test x"$enable_fts5" = "xyes"; then
  AC_SEARCH_LIBS(log, m)
  BUILD_CFLAGS="$BUILD_CFLAGS -DSQLITE_ENABLE_FTS5"
fi
#-----------------------------------------------------------------------

#-----------------------------------------------------------------------
#   --enable-json1
#
AC_ARG_ENABLE(json1, [AS_HELP_STRING(
  [--enable-json1], [include json1 support [default=yes]])], 
  [],[enable_json1=yes])
if test x"$enable_json1" = "xyes"; then
  BUILD_CFLAGS="$BUILD_CFLAGS -DSQLITE_ENABLE_JSON1"
fi
#-----------------------------------------------------------------------

#-----------------------------------------------------------------------
#   --enable-rtree
#
AC_ARG_ENABLE(rtree, [AS_HELP_STRING(
  [--enable-rtree], [include rtree support [default=yes]])], 
  [], [enable_rtree=yes])
if test x"$enable_rtree" = "xyes"; then
  BUILD_CFLAGS="$BUILD_CFLAGS -DSQLITE_ENABLE_RTREE"
fi
#-----------------------------------------------------------------------

#-----------------------------------------------------------------------
#   --enable-session
#
AC_ARG_ENABLE(session, [AS_HELP_STRING(
  [--enable-session], [enable the session extension [default=no]])], 
  [], [])
if test x"$enable_session" = "xyes"; then
  BUILD_CFLAGS="$BUILD_CFLAGS -DSQLITE_ENABLE_SESSION -DSQLITE_ENABLE_PREUPDATE_HOOK"
fi
#-----------------------------------------------------------------------

#-----------------------------------------------------------------------
#   --enable-debug
#
AC_ARG_ENABLE(debug, [AS_HELP_STRING(
  [--enable-debug], [build with debugging features enabled [default=no]])], 
  [], [])
if test x"$enable_debug" = "xyes"; then
  BUILD_CFLAGS="$BUILD_CFLAGS -DSQLITE_DEBUG -DSQLITE_ENABLE_SELECTTRACE -DSQLITE_ENABLE_WHERETRACE"
  CFLAGS="-g -O0"
fi
#-----------------------------------------------------------------------

#-----------------------------------------------------------------------
#   --enable-static-shell
#
AC_ARG_ENABLE(static-shell, [AS_HELP_STRING(
  [--enable-static-shell], 
  [statically link libsqlite3 into shell tool [default=yes]])], 
  [], [enable_static_shell=yes])
if test x"$enable_static_shell" = "xyes"; then
  EXTRA_SHELL_OBJ=sqlite3-sqlite3.$OBJEXT
else
  EXTRA_SHELL_OBJ=libsqlite3.la
fi
AC_SUBST(EXTRA_SHELL_OBJ)
#-----------------------------------------------------------------------

AC_CHECK_FUNCS(posix_fallocate)
AC_CHECK_HEADERS(zlib.h,[
  AC_SEARCH_LIBS(deflate,z,[BUILD_CFLAGS="$BUILD_CFLAGS -DSQLITE_HAVE_ZLIB"])
])

AC_SEARCH_LIBS(system,,,[SHELL_CFLAGS="-DSQLITE_NOHAVE_SYSTEM"])
AC_SUBST(SHELL_CFLAGS)

#-----------------------------------------------------------------------
# UPDATE: Maybe it's better if users just set CFLAGS before invoking
# configure. This option doesn't really add much...
#
#   --enable-tempstore
#
# AC_ARG_ENABLE(tempstore, [AS_HELP_STRING(
#   [--enable-tempstore], 
#   [in-memory temporary tables (never, no, yes, always) [default=no]])], 
#   [], [enable_tempstore=no])
# AC_MSG_CHECKING([for whether or not to store temp tables in-memory])
# case "$enable_tempstore" in
#   never )  TEMP_STORE=0 ;;
#   no )     TEMP_STORE=1 ;;
#   always ) TEMP_STORE=3 ;;
#   yes )    TEMP_STORE=3 ;;
#   * )
#     TEMP_STORE=1
#     enable_tempstore=yes
#   ;;
# esac
# AC_MSG_RESULT($enable_tempstore)
# AC_SUBST(TEMP_STORE)
#-----------------------------------------------------------------------

AC_OUTPUT
