AC_PREREQ(2.61)
AC_INIT([pjdfstest],[0.1],)
AC_CONFIG_AUX_DIR([.])
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([ \
    Makefile \
])

AC_CANONICAL_HOST

AC_PROG_CC([cc])

# For _GNU_SOURCE on Linux, etc.
AC_USE_SYSTEM_EXTENSIONS

AC_CHECK_HEADERS([ \
    sys/mkdev.h \
])

#HAS_FREEBSD_ACL

AC_CHECK_FUNC([bindat],
	      [AC_DEFINE([HAVE_BINDAT], [1], [Define if bindat exists])])
AC_CHECK_FUNC([chflags],
	      [AC_DEFINE([HAVE_CHFLAGS], [1], [Define if chflags exists])])
AC_CHECK_FUNC([chflagsat],
	      [AC_DEFINE([HAVE_CHFLAGSAT], [1], [Define if chflagsat exists])])
AC_CHECK_FUNC([connectat],
	      [AC_DEFINE([HAVE_CONNECTAT], [1], [Define if connectat exists])])
AC_CHECK_FUNC([faccessat],
	      [AC_DEFINE([HAVE_FACCESSAT], [1], [Define if faccessat exists])])
AC_CHECK_FUNC([fchflags],
	      [AC_DEFINE([HAVE_FCHFLAGS], [1], [Define if fchflags exists])])
AC_CHECK_FUNC([fchmodat],
	      [AC_DEFINE([HAVE_FCHMODAT], [1], [Define if fchmodat exists])])
AC_CHECK_FUNC([fchownat],
	      [AC_DEFINE([HAVE_FCHOWNAT], [1], [Define if fchownat exists])])
AC_CHECK_FUNC([fstatat],
	      [AC_DEFINE([HAVE_FSTATAT], [1], [Define if fstatat exists])])
AC_CHECK_FUNC([lchflags],
	      [AC_DEFINE([HAVE_LCHFLAGS], [1], [Define if lchflags exists])])
AC_CHECK_FUNC([lchmod],
	      [AC_DEFINE([HAVE_LCHMOD], [1], [Define if lchmod exists])])
AC_CHECK_FUNC([linkat],
	      [AC_DEFINE([HAVE_LINKAT], [1], [Define if linkat exists])])
AC_CHECK_FUNC([lpathconf],
	      [AC_DEFINE([HAVE_LPATHCONF], [1], [Define if lpathconf exists])])
AC_CHECK_FUNC([mkdirat],
	      [AC_DEFINE([HAVE_MKDIRAT], [1], [Define if mkdirat exists])])
AC_CHECK_FUNC([mkfifoat],
	      [AC_DEFINE([HAVE_MKFIFOAT], [1], [Define if mkfifoat exists])])
AC_CHECK_FUNC([mknodat],
	      [AC_DEFINE([HAVE_MKNODAT], [1], [Define if mknodat exists])])
AC_CHECK_FUNC([openat],
	      [AC_DEFINE([HAVE_OPENAT], [1], [Define if openat exists])])
AC_CHECK_FUNC([posix_fallocate],
	      [AC_DEFINE([HAVE_POSIX_FALLOCATE], [1], [Define if posix_fallocate exists])])
AC_CHECK_FUNC([readlinkat],
	      [AC_DEFINE([HAVE_READLINKAT], [1], [Define if readlinkat exists])])
AC_CHECK_FUNC([renameat],
	      [AC_DEFINE([HAVE_RENAMEAT], [1], [Define if renameat exists])])
AC_CHECK_FUNC([symlinkat],
	      [AC_DEFINE([HAVE_SYMLINKAT], [1], [Define if symlinkat exists])])
AC_CHECK_FUNC([utimensat],
	      [AC_DEFINE([HAVE_UTIMENSAT], [1], [Define if utimensat exists])])

# ACL test battery.
AC_CHECK_HEADER([sys/acl.h], [has_sys_acl_h=yes], [has_sys_acl_h=no])
has_acl_funcs=no
if test x$has_sys_acl_h = xyes; then
	AC_DEFINE([HAVE_SYS_ACL_H], [1],
		  [Define to 1 if sys/acl.h is available])
	AC_CHECK_FUNCS([acl_create_entry_np acl_from_text acl_get_entry acl_get_file acl_set_file],
		       [has_acl_funcs=yes],[])
fi
if test x$has_acl_funcs = xyes; then
	# Check for NFSv4 ACL support.
	AC_CHECK_DECL([ACL_TYPE_NFS4],
		      [has_nfsv4_acl_support=yes], [has_nfsv4_acl_support=no],[[#include <sys/acl.h>]])
	if test x$has_nfsv4_acl_support = xyes; then
	      AC_DEFINE([HAS_NFSV4_ACL_SUPPORT], [1],
			[Define to 1 if NFSv4 ACL support is available])
	fi
fi

AC_CHECK_MEMBERS([struct stat.st_atim, struct stat.st_atimespec], [], [], [[
#include <sys/types.h>
#include <sys/stat.h>
]])

AC_CHECK_MEMBERS([struct stat.st_birthtim, struct stat.st_birthtime, struct stat.st_birthtimespec], [], [], [[
#include <sys/types.h>
#include <sys/stat.h>
]])

AC_CHECK_MEMBERS([struct stat.st_ctim, struct stat.st_ctimespec], [], [], [[
#include <sys/types.h>
#include <sys/stat.h>
]])

AC_CHECK_MEMBERS([struct stat.st_mtim, struct stat.st_mtimespec], [], [], [[
#include <sys/types.h>
#include <sys/stat.h>
]])

AC_OUTPUT
