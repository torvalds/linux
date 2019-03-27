/* Extended support for using errno values.
   Written by Fred Fish.  fnf@cygnus.com
   This file is in the public domain.  --Per Bothner.  */

#include "config.h"

#ifdef HAVE_SYS_ERRLIST
/* Note that errno.h (not sure what OS) or stdio.h (BSD 4.4, at least)
   might declare sys_errlist in a way that the compiler might consider
   incompatible with our later declaration, perhaps by using const
   attributes.  So we hide the declaration in errno.h (if any) using a
   macro. */
#define sys_nerr sys_nerr__
#define sys_errlist sys_errlist__
#endif

#include "ansidecl.h"
#include "libiberty.h"

#include <stdio.h>
#include <errno.h>

#ifdef HAVE_SYS_ERRLIST
#undef sys_nerr
#undef sys_errlist
#endif

/*  Routines imported from standard C runtime libraries. */

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#else
extern PTR malloc ();
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#else
extern PTR memset ();
#endif

#ifndef MAX
#  define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

static void init_error_tables (void);

/* Translation table for errno values.  See intro(2) in most UNIX systems
   Programmers Reference Manuals.

   Note that this table is generally only accessed when it is used at runtime
   to initialize errno name and message tables that are indexed by errno
   value.

   Not all of these errnos will exist on all systems.  This table is the only
   thing that should have to be updated as new error numbers are introduced.
   It's sort of ugly, but at least its portable. */

struct error_info
{
  const int value;		/* The numeric value from <errno.h> */
  const char *const name;	/* The equivalent symbolic value */
#ifndef HAVE_SYS_ERRLIST
  const char *const msg;	/* Short message about this value */
#endif
};

#ifndef HAVE_SYS_ERRLIST
#   define ENTRY(value, name, msg)	{value, name, msg}
#else
#   define ENTRY(value, name, msg)	{value, name}
#endif

static const struct error_info error_table[] =
{
#if defined (EPERM)
  ENTRY(EPERM, "EPERM", "Not owner"),
#endif
#if defined (ENOENT)
  ENTRY(ENOENT, "ENOENT", "No such file or directory"),
#endif
#if defined (ESRCH)
  ENTRY(ESRCH, "ESRCH", "No such process"),
#endif
#if defined (EINTR)
  ENTRY(EINTR, "EINTR", "Interrupted system call"),
#endif
#if defined (EIO)
  ENTRY(EIO, "EIO", "I/O error"),
#endif
#if defined (ENXIO)
  ENTRY(ENXIO, "ENXIO", "No such device or address"),
#endif
#if defined (E2BIG)
  ENTRY(E2BIG, "E2BIG", "Arg list too long"),
#endif
#if defined (ENOEXEC)
  ENTRY(ENOEXEC, "ENOEXEC", "Exec format error"),
#endif
#if defined (EBADF)
  ENTRY(EBADF, "EBADF", "Bad file number"),
#endif
#if defined (ECHILD)
  ENTRY(ECHILD, "ECHILD", "No child processes"),
#endif
#if defined (EWOULDBLOCK)	/* Put before EAGAIN, sometimes aliased */
  ENTRY(EWOULDBLOCK, "EWOULDBLOCK", "Operation would block"),
#endif
#if defined (EAGAIN)
  ENTRY(EAGAIN, "EAGAIN", "No more processes"),
#endif
#if defined (ENOMEM)
  ENTRY(ENOMEM, "ENOMEM", "Not enough space"),
#endif
#if defined (EACCES)
  ENTRY(EACCES, "EACCES", "Permission denied"),
#endif
#if defined (EFAULT)
  ENTRY(EFAULT, "EFAULT", "Bad address"),
#endif
#if defined (ENOTBLK)
  ENTRY(ENOTBLK, "ENOTBLK", "Block device required"),
#endif
#if defined (EBUSY)
  ENTRY(EBUSY, "EBUSY", "Device busy"),
#endif
#if defined (EEXIST)
  ENTRY(EEXIST, "EEXIST", "File exists"),
#endif
#if defined (EXDEV)
  ENTRY(EXDEV, "EXDEV", "Cross-device link"),
#endif
#if defined (ENODEV)
  ENTRY(ENODEV, "ENODEV", "No such device"),
#endif
#if defined (ENOTDIR)
  ENTRY(ENOTDIR, "ENOTDIR", "Not a directory"),
#endif
#if defined (EISDIR)
  ENTRY(EISDIR, "EISDIR", "Is a directory"),
#endif
#if defined (EINVAL)
  ENTRY(EINVAL, "EINVAL", "Invalid argument"),
#endif
#if defined (ENFILE)
  ENTRY(ENFILE, "ENFILE", "File table overflow"),
#endif
#if defined (EMFILE)
  ENTRY(EMFILE, "EMFILE", "Too many open files"),
#endif
#if defined (ENOTTY)
  ENTRY(ENOTTY, "ENOTTY", "Not a typewriter"),
#endif
#if defined (ETXTBSY)
  ENTRY(ETXTBSY, "ETXTBSY", "Text file busy"),
#endif
#if defined (EFBIG)
  ENTRY(EFBIG, "EFBIG", "File too large"),
#endif
#if defined (ENOSPC)
  ENTRY(ENOSPC, "ENOSPC", "No space left on device"),
#endif
#if defined (ESPIPE)
  ENTRY(ESPIPE, "ESPIPE", "Illegal seek"),
#endif
#if defined (EROFS)
  ENTRY(EROFS, "EROFS", "Read-only file system"),
#endif
#if defined (EMLINK)
  ENTRY(EMLINK, "EMLINK", "Too many links"),
#endif
#if defined (EPIPE)
  ENTRY(EPIPE, "EPIPE", "Broken pipe"),
#endif
#if defined (EDOM)
  ENTRY(EDOM, "EDOM", "Math argument out of domain of func"),
#endif
#if defined (ERANGE)
  ENTRY(ERANGE, "ERANGE", "Math result not representable"),
#endif
#if defined (ENOMSG)
  ENTRY(ENOMSG, "ENOMSG", "No message of desired type"),
#endif
#if defined (EIDRM)
  ENTRY(EIDRM, "EIDRM", "Identifier removed"),
#endif
#if defined (ECHRNG)
  ENTRY(ECHRNG, "ECHRNG", "Channel number out of range"),
#endif
#if defined (EL2NSYNC)
  ENTRY(EL2NSYNC, "EL2NSYNC", "Level 2 not synchronized"),
#endif
#if defined (EL3HLT)
  ENTRY(EL3HLT, "EL3HLT", "Level 3 halted"),
#endif
#if defined (EL3RST)
  ENTRY(EL3RST, "EL3RST", "Level 3 reset"),
#endif
#if defined (ELNRNG)
  ENTRY(ELNRNG, "ELNRNG", "Link number out of range"),
#endif
#if defined (EUNATCH)
  ENTRY(EUNATCH, "EUNATCH", "Protocol driver not attached"),
#endif
#if defined (ENOCSI)
  ENTRY(ENOCSI, "ENOCSI", "No CSI structure available"),
#endif
#if defined (EL2HLT)
  ENTRY(EL2HLT, "EL2HLT", "Level 2 halted"),
#endif
#if defined (EDEADLK)
  ENTRY(EDEADLK, "EDEADLK", "Deadlock condition"),
#endif
#if defined (ENOLCK)
  ENTRY(ENOLCK, "ENOLCK", "No record locks available"),
#endif
#if defined (EBADE)
  ENTRY(EBADE, "EBADE", "Invalid exchange"),
#endif
#if defined (EBADR)
  ENTRY(EBADR, "EBADR", "Invalid request descriptor"),
#endif
#if defined (EXFULL)
  ENTRY(EXFULL, "EXFULL", "Exchange full"),
#endif
#if defined (ENOANO)
  ENTRY(ENOANO, "ENOANO", "No anode"),
#endif
#if defined (EBADRQC)
  ENTRY(EBADRQC, "EBADRQC", "Invalid request code"),
#endif
#if defined (EBADSLT)
  ENTRY(EBADSLT, "EBADSLT", "Invalid slot"),
#endif
#if defined (EDEADLOCK)
  ENTRY(EDEADLOCK, "EDEADLOCK", "File locking deadlock error"),
#endif
#if defined (EBFONT)
  ENTRY(EBFONT, "EBFONT", "Bad font file format"),
#endif
#if defined (ENOSTR)
  ENTRY(ENOSTR, "ENOSTR", "Device not a stream"),
#endif
#if defined (ENODATA)
  ENTRY(ENODATA, "ENODATA", "No data available"),
#endif
#if defined (ETIME)
  ENTRY(ETIME, "ETIME", "Timer expired"),
#endif
#if defined (ENOSR)
  ENTRY(ENOSR, "ENOSR", "Out of streams resources"),
#endif
#if defined (ENONET)
  ENTRY(ENONET, "ENONET", "Machine is not on the network"),
#endif
#if defined (ENOPKG)
  ENTRY(ENOPKG, "ENOPKG", "Package not installed"),
#endif
#if defined (EREMOTE)
  ENTRY(EREMOTE, "EREMOTE", "Object is remote"),
#endif
#if defined (ENOLINK)
  ENTRY(ENOLINK, "ENOLINK", "Link has been severed"),
#endif
#if defined (EADV)
  ENTRY(EADV, "EADV", "Advertise error"),
#endif
#if defined (ESRMNT)
  ENTRY(ESRMNT, "ESRMNT", "Srmount error"),
#endif
#if defined (ECOMM)
  ENTRY(ECOMM, "ECOMM", "Communication error on send"),
#endif
#if defined (EPROTO)
  ENTRY(EPROTO, "EPROTO", "Protocol error"),
#endif
#if defined (EMULTIHOP)
  ENTRY(EMULTIHOP, "EMULTIHOP", "Multihop attempted"),
#endif
#if defined (EDOTDOT)
  ENTRY(EDOTDOT, "EDOTDOT", "RFS specific error"),
#endif
#if defined (EBADMSG)
  ENTRY(EBADMSG, "EBADMSG", "Not a data message"),
#endif
#if defined (ENAMETOOLONG)
  ENTRY(ENAMETOOLONG, "ENAMETOOLONG", "File name too long"),
#endif
#if defined (EOVERFLOW)
  ENTRY(EOVERFLOW, "EOVERFLOW", "Value too large for defined data type"),
#endif
#if defined (ENOTUNIQ)
  ENTRY(ENOTUNIQ, "ENOTUNIQ", "Name not unique on network"),
#endif
#if defined (EBADFD)
  ENTRY(EBADFD, "EBADFD", "File descriptor in bad state"),
#endif
#if defined (EREMCHG)
  ENTRY(EREMCHG, "EREMCHG", "Remote address changed"),
#endif
#if defined (ELIBACC)
  ENTRY(ELIBACC, "ELIBACC", "Can not access a needed shared library"),
#endif
#if defined (ELIBBAD)
  ENTRY(ELIBBAD, "ELIBBAD", "Accessing a corrupted shared library"),
#endif
#if defined (ELIBSCN)
  ENTRY(ELIBSCN, "ELIBSCN", ".lib section in a.out corrupted"),
#endif
#if defined (ELIBMAX)
  ENTRY(ELIBMAX, "ELIBMAX", "Attempting to link in too many shared libraries"),
#endif
#if defined (ELIBEXEC)
  ENTRY(ELIBEXEC, "ELIBEXEC", "Cannot exec a shared library directly"),
#endif
#if defined (EILSEQ)
  ENTRY(EILSEQ, "EILSEQ", "Illegal byte sequence"),
#endif
#if defined (ENOSYS)
  ENTRY(ENOSYS, "ENOSYS", "Operation not applicable"),
#endif
#if defined (ELOOP)
  ENTRY(ELOOP, "ELOOP", "Too many symbolic links encountered"),
#endif
#if defined (ERESTART)
  ENTRY(ERESTART, "ERESTART", "Interrupted system call should be restarted"),
#endif
#if defined (ESTRPIPE)
  ENTRY(ESTRPIPE, "ESTRPIPE", "Streams pipe error"),
#endif
#if defined (ENOTEMPTY)
  ENTRY(ENOTEMPTY, "ENOTEMPTY", "Directory not empty"),
#endif
#if defined (EUSERS)
  ENTRY(EUSERS, "EUSERS", "Too many users"),
#endif
#if defined (ENOTSOCK)
  ENTRY(ENOTSOCK, "ENOTSOCK", "Socket operation on non-socket"),
#endif
#if defined (EDESTADDRREQ)
  ENTRY(EDESTADDRREQ, "EDESTADDRREQ", "Destination address required"),
#endif
#if defined (EMSGSIZE)
  ENTRY(EMSGSIZE, "EMSGSIZE", "Message too long"),
#endif
#if defined (EPROTOTYPE)
  ENTRY(EPROTOTYPE, "EPROTOTYPE", "Protocol wrong type for socket"),
#endif
#if defined (ENOPROTOOPT)
  ENTRY(ENOPROTOOPT, "ENOPROTOOPT", "Protocol not available"),
#endif
#if defined (EPROTONOSUPPORT)
  ENTRY(EPROTONOSUPPORT, "EPROTONOSUPPORT", "Protocol not supported"),
#endif
#if defined (ESOCKTNOSUPPORT)
  ENTRY(ESOCKTNOSUPPORT, "ESOCKTNOSUPPORT", "Socket type not supported"),
#endif
#if defined (EOPNOTSUPP)
  ENTRY(EOPNOTSUPP, "EOPNOTSUPP", "Operation not supported on transport endpoint"),
#endif
#if defined (EPFNOSUPPORT)
  ENTRY(EPFNOSUPPORT, "EPFNOSUPPORT", "Protocol family not supported"),
#endif
#if defined (EAFNOSUPPORT)
  ENTRY(EAFNOSUPPORT, "EAFNOSUPPORT", "Address family not supported by protocol"),
#endif
#if defined (EADDRINUSE)
  ENTRY(EADDRINUSE, "EADDRINUSE", "Address already in use"),
#endif
#if defined (EADDRNOTAVAIL)
  ENTRY(EADDRNOTAVAIL, "EADDRNOTAVAIL","Cannot assign requested address"),
#endif
#if defined (ENETDOWN)
  ENTRY(ENETDOWN, "ENETDOWN", "Network is down"),
#endif
#if defined (ENETUNREACH)
  ENTRY(ENETUNREACH, "ENETUNREACH", "Network is unreachable"),
#endif
#if defined (ENETRESET)
  ENTRY(ENETRESET, "ENETRESET", "Network dropped connection because of reset"),
#endif
#if defined (ECONNABORTED)
  ENTRY(ECONNABORTED, "ECONNABORTED", "Software caused connection abort"),
#endif
#if defined (ECONNRESET)
  ENTRY(ECONNRESET, "ECONNRESET", "Connection reset by peer"),
#endif
#if defined (ENOBUFS)
  ENTRY(ENOBUFS, "ENOBUFS", "No buffer space available"),
#endif
#if defined (EISCONN)
  ENTRY(EISCONN, "EISCONN", "Transport endpoint is already connected"),
#endif
#if defined (ENOTCONN)
  ENTRY(ENOTCONN, "ENOTCONN", "Transport endpoint is not connected"),
#endif
#if defined (ESHUTDOWN)
  ENTRY(ESHUTDOWN, "ESHUTDOWN", "Cannot send after transport endpoint shutdown"),
#endif
#if defined (ETOOMANYREFS)
  ENTRY(ETOOMANYREFS, "ETOOMANYREFS", "Too many references: cannot splice"),
#endif
#if defined (ETIMEDOUT)
  ENTRY(ETIMEDOUT, "ETIMEDOUT", "Connection timed out"),
#endif
#if defined (ECONNREFUSED)
  ENTRY(ECONNREFUSED, "ECONNREFUSED", "Connection refused"),
#endif
#if defined (EHOSTDOWN)
  ENTRY(EHOSTDOWN, "EHOSTDOWN", "Host is down"),
#endif
#if defined (EHOSTUNREACH)
  ENTRY(EHOSTUNREACH, "EHOSTUNREACH", "No route to host"),
#endif
#if defined (EALREADY)
  ENTRY(EALREADY, "EALREADY", "Operation already in progress"),
#endif
#if defined (EINPROGRESS)
  ENTRY(EINPROGRESS, "EINPROGRESS", "Operation now in progress"),
#endif
#if defined (ESTALE)
  ENTRY(ESTALE, "ESTALE", "Stale NFS file handle"),
#endif
#if defined (EUCLEAN)
  ENTRY(EUCLEAN, "EUCLEAN", "Structure needs cleaning"),
#endif
#if defined (ENOTNAM)
  ENTRY(ENOTNAM, "ENOTNAM", "Not a XENIX named type file"),
#endif
#if defined (ENAVAIL)
  ENTRY(ENAVAIL, "ENAVAIL", "No XENIX semaphores available"),
#endif
#if defined (EISNAM)
  ENTRY(EISNAM, "EISNAM", "Is a named type file"),
#endif
#if defined (EREMOTEIO)
  ENTRY(EREMOTEIO, "EREMOTEIO", "Remote I/O error"),
#endif
  ENTRY(0, NULL, NULL)
};

#ifdef EVMSERR
/* This is not in the table, because the numeric value of EVMSERR (32767)
   lies outside the range of sys_errlist[].  */
static struct { int value; const char *name, *msg; }
  evmserr = { EVMSERR, "EVMSERR", "VMS-specific error" };
#endif

/* Translation table allocated and initialized at runtime.  Indexed by the
   errno value to find the equivalent symbolic value. */

static const char **error_names;
static int num_error_names = 0;

/* Translation table allocated and initialized at runtime, if it does not
   already exist in the host environment.  Indexed by the errno value to find
   the descriptive string.

   We don't export it for use in other modules because even though it has the
   same name, it differs from other implementations in that it is dynamically
   initialized rather than statically initialized. */

#ifndef HAVE_SYS_ERRLIST

#define sys_nerr sys_nerr__
#define sys_errlist sys_errlist__
static int sys_nerr;
static const char **sys_errlist;

#else

extern int sys_nerr;
extern char *sys_errlist[];

#endif

/*

NAME

	init_error_tables -- initialize the name and message tables

SYNOPSIS

	static void init_error_tables ();

DESCRIPTION

	Using the error_table, which is initialized at compile time, generate
	the error_names and the sys_errlist (if needed) tables, which are
	indexed at runtime by a specific errno value.

BUGS

	The initialization of the tables may fail under low memory conditions,
	in which case we don't do anything particularly useful, but we don't
	bomb either.  Who knows, it might succeed at a later point if we free
	some memory in the meantime.  In any case, the other routines know
	how to deal with lack of a table after trying to initialize it.  This
	may or may not be considered to be a bug, that we don't specifically
	warn about this particular failure mode.

*/

static void
init_error_tables (void)
{
  const struct error_info *eip;
  int nbytes;

  /* If we haven't already scanned the error_table once to find the maximum
     errno value, then go find it now. */

  if (num_error_names == 0)
    {
      for (eip = error_table; eip -> name != NULL; eip++)
	{
	  if (eip -> value >= num_error_names)
	    {
	      num_error_names = eip -> value + 1;
	    }
	}
    }

  /* Now attempt to allocate the error_names table, zero it out, and then
     initialize it from the statically initialized error_table. */

  if (error_names == NULL)
    {
      nbytes = num_error_names * sizeof (char *);
      if ((error_names = (const char **) malloc (nbytes)) != NULL)
	{
	  memset (error_names, 0, nbytes);
	  for (eip = error_table; eip -> name != NULL; eip++)
	    {
	      error_names[eip -> value] = eip -> name;
	    }
	}
    }

#ifndef HAVE_SYS_ERRLIST

  /* Now attempt to allocate the sys_errlist table, zero it out, and then
     initialize it from the statically initialized error_table. */

  if (sys_errlist == NULL)
    {
      nbytes = num_error_names * sizeof (char *);
      if ((sys_errlist = (const char **) malloc (nbytes)) != NULL)
	{
	  memset (sys_errlist, 0, nbytes);
	  sys_nerr = num_error_names;
	  for (eip = error_table; eip -> name != NULL; eip++)
	    {
	      sys_errlist[eip -> value] = eip -> msg;
	    }
	}
    }

#endif

}

/*


@deftypefn Extension int errno_max (void)

Returns the maximum @code{errno} value for which a corresponding
symbolic name or message is available.  Note that in the case where we
use the @code{sys_errlist} supplied by the system, it is possible for
there to be more symbolic names than messages, or vice versa.  In
fact, the manual page for @code{perror(3C)} explicitly warns that one
should check the size of the table (@code{sys_nerr}) before indexing
it, since new error codes may be added to the system before they are
added to the table.  Thus @code{sys_nerr} might be smaller than value
implied by the largest @code{errno} value defined in @code{<errno.h>}.

We return the maximum value that can be used to obtain a meaningful
symbolic name or message.

@end deftypefn

*/

int
errno_max (void)
{
  int maxsize;

  if (error_names == NULL)
    {
      init_error_tables ();
    }
  maxsize = MAX (sys_nerr, num_error_names);
  return (maxsize - 1);
}

#ifndef HAVE_STRERROR

/*

@deftypefn Supplemental char* strerror (int @var{errnoval})

Maps an @code{errno} number to an error message string, the contents
of which are implementation defined.  On systems which have the
external variables @code{sys_nerr} and @code{sys_errlist}, these
strings will be the same as the ones used by @code{perror}.

If the supplied error number is within the valid range of indices for
the @code{sys_errlist}, but no message is available for the particular
error number, then returns the string @samp{Error @var{num}}, where
@var{num} is the error number.

If the supplied error number is not a valid index into
@code{sys_errlist}, returns @code{NULL}.

The returned string is only guaranteed to be valid only until the
next call to @code{strerror}.

@end deftypefn

*/

char *
strerror (int errnoval)
{
  const char *msg;
  static char buf[32];

#ifndef HAVE_SYS_ERRLIST

  if (error_names == NULL)
    {
      init_error_tables ();
    }

#endif

  if ((errnoval < 0) || (errnoval >= sys_nerr))
    {
#ifdef EVMSERR
      if (errnoval == evmserr.value)
	msg = evmserr.msg;
      else
#endif
      /* Out of range, just return NULL */
      msg = NULL;
    }
  else if ((sys_errlist == NULL) || (sys_errlist[errnoval] == NULL))
    {
      /* In range, but no sys_errlist or no entry at this index. */
      sprintf (buf, "Error %d", errnoval);
      msg = buf;
    }
  else
    {
      /* In range, and a valid message.  Just return the message. */
      msg = (char *) sys_errlist[errnoval];
    }
  
  return (msg);
}

#endif	/* ! HAVE_STRERROR */


/*

@deftypefn Replacement {const char*} strerrno (int @var{errnum})

Given an error number returned from a system call (typically returned
in @code{errno}), returns a pointer to a string containing the
symbolic name of that error number, as found in @code{<errno.h>}.

If the supplied error number is within the valid range of indices for
symbolic names, but no name is available for the particular error
number, then returns the string @samp{Error @var{num}}, where @var{num}
is the error number.

If the supplied error number is not within the range of valid
indices, then returns @code{NULL}.

The contents of the location pointed to are only guaranteed to be
valid until the next call to @code{strerrno}.

@end deftypefn

*/

const char *
strerrno (int errnoval)
{
  const char *name;
  static char buf[32];

  if (error_names == NULL)
    {
      init_error_tables ();
    }

  if ((errnoval < 0) || (errnoval >= num_error_names))
    {
#ifdef EVMSERR
      if (errnoval == evmserr.value)
	name = evmserr.name;
      else
#endif
      /* Out of range, just return NULL */
      name = NULL;
    }
  else if ((error_names == NULL) || (error_names[errnoval] == NULL))
    {
      /* In range, but no error_names or no entry at this index. */
      sprintf (buf, "Error %d", errnoval);
      name = (const char *) buf;
    }
  else
    {
      /* In range, and a valid name.  Just return the name. */
      name = error_names[errnoval];
    }

  return (name);
}

/*

@deftypefn Extension int strtoerrno (const char *@var{name})

Given the symbolic name of a error number (e.g., @code{EACCES}), map it
to an errno value.  If no translation is found, returns 0.

@end deftypefn

*/

int
strtoerrno (const char *name)
{
  int errnoval = 0;

  if (name != NULL)
    {
      if (error_names == NULL)
	{
	  init_error_tables ();
	}
      for (errnoval = 0; errnoval < num_error_names; errnoval++)
	{
	  if ((error_names[errnoval] != NULL) &&
	      (strcmp (name, error_names[errnoval]) == 0))
	    {
	      break;
	    }
	}
      if (errnoval == num_error_names)
	{
#ifdef EVMSERR
	  if (strcmp (name, evmserr.name) == 0)
	    errnoval = evmserr.value;
	  else
#endif
	  errnoval = 0;
	}
    }
  return (errnoval);
}


/* A simple little main that does nothing but print all the errno translations
   if MAIN is defined and this file is compiled and linked. */

#ifdef MAIN

#include <stdio.h>

int
main (void)
{
  int errn;
  int errnmax;
  const char *name;
  const char *msg;
  char *strerror ();

  errnmax = errno_max ();
  printf ("%d entries in names table.\n", num_error_names);
  printf ("%d entries in messages table.\n", sys_nerr);
  printf ("%d is max useful index.\n", errnmax);

  /* Keep printing values until we get to the end of *both* tables, not
     *either* table.  Note that knowing the maximum useful index does *not*
     relieve us of the responsibility of testing the return pointer for
     NULL. */

  for (errn = 0; errn <= errnmax; errn++)
    {
      name = strerrno (errn);
      name = (name == NULL) ? "<NULL>" : name;
      msg = strerror (errn);
      msg = (msg == NULL) ? "<NULL>" : msg;
      printf ("%-4d%-18s%s\n", errn, name, msg);
    }

  return 0;
}

#endif
