/* acconfig.h: Extra commentary for Autoheader

%%% portions-copyright-cmetz-96
Portions of this software are Copyright 1996-1999 by Craig Metz, All Rights
Reserved. The Inner Net License Version 2 applies to these portions of
the software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

/* Define if the closedir function returns void instead of int.  */ 
#undef CLOSEDIR_VOID

/* Define if you want the FTP daemon to support anonymous logins. */
#undef DOANONYMOUS

/* The default value of the PATH environment variable */
#undef DEFAULT_PATH

/* Defined if the file /etc/default/login exists 
   (and, presumably, should be looked at by login) */
#undef HAVE_ETC_DEFAULT_LOGIN

/* Defined to the name of a file that contains a list of files whose
   permissions and ownerships should be changed on login. */
#undef HAVE_LOGIN_PERMFILE

/* Defined to the name of a file that contains a list of environment
   values that should be set on login. */
#undef HAVE_LOGIN_ENVFILE

/* Defined if the file /etc/securetty exists
   (and, presumably, should be looked at by login) */
#undef HAVE_SECURETTY

/* Defined if the file /etc/shadow exists
   (and, presumably, should be looked at for shadow passwords) */
#undef HAVE_ETC_SHADOW

/* The path to the access file, if we're going to use it */
#undef PATH_ACCESS_FILE

/* The path to the mail spool, if we know it */
#undef PATH_MAIL

/* The path to the utmp file, if we know it */
#undef PATH_UTMP_AC

/* The path to the utmpx file, if we know it */
#undef PATH_UTMPX_AC

/* The path to the wtmp file, if we know it */
#undef PATH_WTMP_AC

/* The path to the wtmpx file, if we know it */
#undef PATH_WTMPX_AC

/* Defined if the system's profile (/etc/profile) displays
   the motd file */
#undef HAVE_MOTD_IN_PROFILE

/* Defined if the system's profile (/etc/profile) informs the
   user of new mail */
#undef HAVE_MAILCHECK_IN_PROFILE

/* Define if you have a nonstandard gettimeofday() that takes one argument
   instead of two. */
#undef HAVE_ONE_ARG_GETTIMEOFDAY

/* Define if the system has the getenv function */
#undef HAVE_GETENV

/* Define if the system has the setenv function */
#undef HAVE_SETENV

/* Define if the system has the /var/adm/sulog file */
#undef HAVE_SULOG

/* Define if the system has the unsetenv function */
#undef HAVE_UNSETENV

/* Define if the compiler can handle ANSI-style argument lists */
#undef HAVE_ANSIDECL

/* Define if the compiler can handle ANSI-style prototypes */
#undef HAVE_ANSIPROTO

/* Define if the system has an ANSI-style printf (returns int instead of char *) */
#undef HAVE_ANSISPRINTF

/* Define if the compiler can handle ANSI-style variable argument lists */
#undef HAVE_ANSISTDARG

/* Define if the compiler can handle void argument lists to functions */
#undef HAVE_VOIDARG

/* Define if the compiler can handle void return "values" from functions */
#undef HAVE_VOIDRET

/* Define if the compiler can handle void pointers to our liking */
#undef HAVE_VOIDPTR

/* Define if the /bin/ls command seems to support the -g flag */
#undef HAVE_LS_G_FLAG

/* Define if there is a ut_pid field in struct utmp */
#undef HAVE_UT_PID

/* Define if there is a ut_type field in struct utmp */
#undef HAVE_UT_TYPE

/* Define if there is a ut_user field in struct utmp */
#undef HAVE_UT_USER

/* Define if there is a ut_name field in struct utmp */
#undef HAVE_UT_NAME

/* Define if there is a ut_host field in struct utmp */
#undef HAVE_UT_HOST

/* Define if there is a ut_id field in struct utmp */
#undef HAVE_UT_ID

/* Define if there is a ut_syslen field in struct utmp */
#undef HAVE_UT_SYSLEN

/* Define if there is a utx_syslen field in struct utmpx */
#undef HAVE_UTX_SYSLEN

/* Define if the system has getutline() */
#undef HAVE_GETUTLINE

/* Defined if the system has SunOS C2 security shadow passwords */
#undef HAVE_SUNOS_C2_SHADOW

/* Defined if you want to disable utmp support */
#undef DISABLE_UTMP

/* Defined if you want to disable wtmp support */
#undef DISABLE_WTMP

/* Defined if you want to allow users to override the insecure checks */
#undef INSECURE_OVERRIDE

/* Defined to the default hash value, always defined */
#undef MDX

/* Defined if new-style prompts are to be used */
#undef NEW_PROMPTS

/* Defined to the path of the OPIE lock directory */
#undef OPIE_LOCK_DIR

/* Defined if users are to be asked to re-type secret pass phrases */
#undef RETYPE

/* Defined if su should not switch to disabled accounts */
#undef SU_STAR_CHECK

/* Defined if user locking is to be used */
#undef USER_LOCKING

/* Defined if opieauto is to be used */
#undef OPIEAUTO

/* Define if you have the atexit function.  */
#undef HAVE_ATEXIT

/* Define if you have the endutent function.  */
#undef HAVE_ENDUTENT

/* Define if you have the initgroups function.  */
#undef HAVE_INITGROUPS

/* Define if you have the memcmp function.  */
#undef HAVE_MEMCMP

/* Define if you have the memcpy function.  */
#undef HAVE_MEMCPY

/* Define if you have the memset function.  */
#undef HAVE_MEMSET

/* Define if you have the getcwd function.  */
#undef HAVE_GETCWD

/* Define if you have the getenv function.  */
#undef HAVE_GETENV

/* Define if you have the getutline function.  */
#undef HAVE_GETUTLINE

/* Define if you have the pututline function.  */
#undef HAVE_PUTUTLINE

/* Define if you have the setenv function.  */
#undef HAVE_SETENV

/* Define if you have the setegid function.  */
#undef HAVE_SETEGID

/* Define if you have the seteuid function.  */
#undef HAVE_SETEUID

/* Define if you have the setutent function.  */
#undef HAVE_SETUTENT

/* Define if you have the sigprocmask function.  */
#undef HAVE_SIGPROCMASK

/* Define if you have the strchr function.  */
#undef HAVE_STRCHR

/* Define if you have the strrchr function.  */
#undef HAVE_STRRCHR

/* Define if you have the strtoul function.  */
#undef HAVE_STRTOUL

/* Define if you have the sysconf function.  */
#undef HAVE_SYSCONF

/* Define if you have the uname function.  */
#undef HAVE_UNAME

/* Define if you have the unsetenv function.  */
#undef HAVE_UNSETENV
