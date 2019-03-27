/*
 * Copyright (c) 2001-2002 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: mpeix.c,v 1.8 2013-11-22 20:51:43 ca Exp $")

#ifdef MPE
/*
**	MPE lacks many common functions required across all sendmail programs
**	so we define implementations for these functions here.
*/

# include <errno.h>
# include <fcntl.h>
# include <limits.h>
# include <mpe.h>
# include <netinet/in.h>
# include <pwd.h>
# include <sys/socket.h>
# include <sys/stat.h>
# include <unistd.h>
# include <sm/conf.h>

/*
**  CHROOT -- dummy chroot() function
**
**	The MPE documentation for sendmail says that chroot-based
**	functionality is not implemented because MPE lacks chroot.  But
**	rather than mucking around with all the sendmail calls to chroot,
**	we define this dummy function to return an ENOSYS failure just in
**	case a sendmail user attempts to enable chroot-based functionality.
**
**	Parameters:
**		path -- pathname of new root (ignored).
**
**	Returns:
**		-1 and errno == ENOSYS (function not implemented)
*/

int
chroot(path)
	char *path;
{
	errno = ENOSYS;
	return -1;
}

/*
**  ENDPWENT -- dummy endpwent() function
**
**	Parameters:
**		none
**
**	Returns:
**		none
*/

void
endpwent()
{
	return;
}

/*
**  In addition to missing functions, certain existing MPE functions have
**  slightly different semantics (or bugs) compared to normal Unix OSes.
**
**  Here we define wrappers for these functions to make them behave in the
**  manner expected by sendmail.
*/

/*
**  SENDMAIL_MPE_BIND -- shadow function for the standard socket bind()
**
**	MPE requires GETPRIVMODE() for AF_INET sockets less than port 1024.
**
**	Parameters:
**		sd -- socket descriptor.
**		addr -- socket address.
**		addrlen -- length of socket address.
**
**	Results:
**		0 -- success
**		!= 0 -- failure
*/

#undef bind
int
sendmail_mpe_bind(sd, addr, addrlen)
	int sd;
	void *addr;
	int addrlen;
{
	bool priv = false;
	int result;
	extern void GETPRIVMODE __P((void));
	extern void GETUSERMODE __P((void));

	if (addrlen == sizeof(struct sockaddr_in) &&
	    ((struct sockaddr_in *)addr)->sin_family == AF_INET)
	{
		/* AF_INET */
		if (((struct sockaddr_in *)addr)->sin_port > 0 &&
		    ((struct sockaddr_in *)addr)->sin_port < 1024)
		{
			priv = true;
			GETPRIVMODE();
		}
		((struct sockaddr_in *)addr)->sin_addr.s_addr = 0;
		result = bind(sd, addr, addrlen);
		if (priv)
			GETUSERMODE();
		return result;
	}

	/* AF_UNIX */
	return bind(sd, addr, addrlen);
}

/*
**  SENDMAIL_MPE__EXIT -- wait for children to terminate, then _exit()
**
**	Child processes cannot survive the death of their parent on MPE, so
**	we must call wait() before _exit() in order to prevent this
**	infanticide.
**
**	Parameters:
**		status -- _exit status value.
**
**	Returns:
**		none.
*/

#undef _exit
void
sendmail_mpe__exit(status)
	int status;
{
	int result;

	/* Wait for all children to terminate. */
	do
	{
		result = wait(NULL);
	} while (result > 0 || errno == EINTR);
	_exit(status);
}

/*
**  SENDMAIL_MPE_EXIT -- wait for children to terminate, then exit()
**
**	Child processes cannot survive the death of their parent on MPE, so
**	we must call wait() before exit() in order to prevent this
**	infanticide.
**
**	Parameters:
**		status -- exit status value.
**
**	Returns:
**		none.
*/

#undef exit
void
sendmail_mpe_exit(status)
	int status;
{
	int result;

	/* Wait for all children to terminate. */
	do
	{
		result = wait(NULL);
	} while (result > 0 || errno == EINTR);
	exit(status);
}

/*
**  SENDMAIL_MPE_FCNTL -- shadow function for fcntl()
**
**	MPE requires sfcntl() for sockets, and fcntl() for everything
**	else.  This shadow routine determines the descriptor type and
**	makes the appropriate call.
**
**	Parameters:
**		same as fcntl().
**
**	Returns:
**		same as fcntl().
*/

#undef fcntl
int
sendmail_mpe_fcntl(int fildes, int cmd, ...)
{
	int len, result;
	struct sockaddr sa;

	void *arg;
	va_list ap;

	va_start(ap, cmd);
	arg = va_arg(ap, void *);
	va_end(ap);

	len = sizeof sa;
	if (getsockname(fildes, &sa, &len) == -1)
	{
		if (errno == EAFNOSUPPORT)
		{
			/* AF_UNIX socket */
			return sfcntl(fildes, cmd, arg);
		}
		else if (errno == ENOTSOCK)
		{
			/* file or pipe */
			return fcntl(fildes, cmd, arg);
		}

		/* unknown getsockname() failure */
		return (-1);
	}
	else
	{
		/* AF_INET socket */
		if ((result = sfcntl(fildes, cmd, arg)) != -1 &&
		    cmd == F_GETFL)
			result |= O_RDWR;  /* fill in some missing flags */
		return result;
	}
}

/*
**  SENDMAIL_MPE_GETPWNAM - shadow function for getpwnam()
**
**	Several issues apply here:
**
**	- MPE user names MUST have one '.' separator character
**	- MPE user names MUST be in upper case
**	- MPE does not initialize all fields in the passwd struct
**
**	Parameters:
**		name -- username string.
**
**	Returns:
**		pointer to struct passwd if found else NULL
*/

static char *sendmail_mpe_nullstr = "";

#undef getpwnam
extern struct passwd *getpwnam(const char *);

struct passwd *
sendmail_mpe_getpwnam(name)
	const char *name;
{
	int dots = 0;
	int err;
	int i = strlen(name);
	char *upper;
	struct passwd *result = NULL;

	if (i <= 0)
	{
		errno = EINVAL;
		return result;
	}

	if ((upper = (char *)malloc(i + 1)) != NULL)
	{
		/* upshift the username parameter and count the dots */
		while (i >= 0)
		{
			if (name[i] == '.')
			{
				dots++;
				upper[i] = '.';
			}
			else
				upper[i] = toupper(name[i]);
			i--;
		}

		if (dots != 1)
		{
			/* prevent bug when dots == 0 */
			err = EINVAL;
		}
		else if ((result = getpwnam(upper)) != NULL)
		{
			/* init the uninitialized fields */
			result->pw_gecos = sendmail_mpe_nullstr;
			result->pw_passwd = sendmail_mpe_nullstr;
			result->pw_age = sendmail_mpe_nullstr;
			result->pw_comment = sendmail_mpe_nullstr;
			result->pw_audid = 0;
			result->pw_audflg = 0;
		}
		err = errno;
		free(upper);
	}
	errno = err;
	return result;
}

/*
**  SENDMAIL_MPE_GETPWUID -- shadow function for getpwuid()
**
**	Initializes the uninitalized fields in the passwd struct.
**
**	Parameters:
**		uid -- uid to obtain passwd data for
**
**	Returns:
**		pointer to struct passwd or NULL if not found
*/

#undef getpwuid
extern struct passwd *getpwuid __P((uid_t));

struct passwd *
sendmail_mpe_getpwuid(uid)
	uid_t uid;
{
	struct passwd *result;

	if ((result = getpwuid(uid)) != NULL)
	{
		/* initialize the uninitialized fields */
		result->pw_gecos = sendmail_mpe_nullstr;
		result->pw_passwd = sendmail_mpe_nullstr;
		result->pw_age = sendmail_mpe_nullstr;
		result->pw_comment = sendmail_mpe_nullstr;
		result->pw_audid = 0;
		result->pw_audflg = 0;
	}
	return result;
}

/*
**  OK boys and girls, time for some serious voodoo!
**
**  MPE does not have a complete implementation of POSIX users and groups:
**
**  - there is no uid 0 superuser
**  - setuid/setgid file permission bits exist but have no-op functionality
**  - setgid() exists, but only supports new gid == current gid (boring!)
**  - setuid() forces a gid change to the new uid's primary (and only) gid
**
**  ...all of which thoroughly annoys sendmail.
**
**  So what to do?  We can't go on an #ifdef MPE rampage throughout
**  sendmail, because there are only about a zillion references to uid 0
**  and so success (and security) would probably be rather dubious by the
**  time we finished.
**
**  Instead we take the approach of defining wrapper functions for the
**  gid/uid management functions getegid(), geteuid(), setgid(), and
**  setuid() in order to implement the following model:
**
**  - the sendmail program thinks it is a setuid-root (uid 0) program
**  - uid 0 is recognized as being valid, but does not grant extra powers
**	- MPE priv mode allows sendmail to call setuid(), not uid 0
**	- file access is still controlled by the real non-zero uid
**  - the other programs (vacation, etc) have standard MPE POSIX behavior
**
**  This emulation model is activated by use of the program file setgid and
**  setuid mode bits which exist but are unused by MPE.  If the setgid mode
**  bit is on, then gid emulation will be enabled.  If the setuid mode bit is
**  on, then uid emulation will be enabled.  So for the mail daemon, we need
**  to do chmod u+s,g+s /SENDMAIL/CURRENT/SENDMAIL.
**
**  The following flags determine the current emulation state:
**
**  true == emulation enabled
**  false == emulation disabled, use unmodified MPE semantics
*/

static bool sendmail_mpe_flaginit = false;
static bool sendmail_mpe_gidflag = false;
static bool sendmail_mpe_uidflag = false;

/*
**  SENDMAIL_MPE_GETMODE -- return the mode bits for the current process
**
**	Parameters:
**		none.
**
**	Returns:
**		file mode bits for the current process program file.
*/

mode_t
sendmail_mpe_getmode()
{
	int status = 666;
	int myprogram_length;
	int myprogram_syntax = 2;
	char formaldesig[28];
	char myprogram[PATH_MAX + 2];
	char path[PATH_MAX + 1];
	struct stat st;
	extern HPMYPROGRAM __P((int parms, char *formaldesig, int *status,
				int *length, char *myprogram,
				int *myprogram_length, int *myprogram_syntax));

	myprogram_length = sizeof(myprogram);
	HPMYPROGRAM(6, formaldesig, &status, NULL, myprogram,
		    &myprogram_length, &myprogram_syntax);

	/* should not occur, do not attempt emulation */
	if (status != 0)
		return 0;

	memcpy(&path, &myprogram[1], myprogram_length - 2);
	path[myprogram_length - 2] = '\0';

	/* should not occur, do not attempt emulation */
	if (stat(path, &st) < 0)
		return 0;

	return st.st_mode;
}

/*
**  SENDMAIL_MPE_EMULGID -- should we perform gid emulation?
**
**	If !sendmail_mpe_flaginit then obtain the mode bits to determine
**	if the setgid bit is on, we want gid emulation and so set
**	sendmail_mpe_gidflag to true.  Otherwise we do not want gid emulation
**	and so set sendmail_mpe_gidflag to false.
**
**	Parameters:
**		none.
**
**	Returns:
**		true -- perform gid emulation
**		false -- do not perform gid emulation
*/

bool
sendmail_mpe_emulgid()
{
	if (!sendmail_mpe_flaginit)
	{
		mode_t mode;

		mode = sendmail_mpe_getmode();
		sendmail_mpe_gidflag = ((mode & S_ISGID) == S_ISGID);
		sendmail_mpe_uidflag = ((mode & S_ISUID) == S_ISUID);
		sendmail_mpe_flaginit = true;
	}
	return sendmail_mpe_gidflag;
}

/*
**  SENDMAIL_MPE_EMULUID -- should we perform uid emulation?
**
**	If sendmail_mpe_uidflag == -1 then obtain the mode bits to determine
**	if the setuid bit is on, we want uid emulation and so set
**	sendmail_mpe_uidflag to true.  Otherwise we do not want uid emulation
**	and so set sendmail_mpe_uidflag to false.
**
**	Parameters:
**		none.
**
**	Returns:
**		true -- perform uid emulation
**		false -- do not perform uid emulation
*/

bool
sendmail_mpe_emuluid()
{
	if (!sendmail_mpe_flaginit)
	{
		mode_t mode;

		mode = sendmail_mpe_getmode();
		sendmail_mpe_gidflag = ((mode & S_ISGID) == S_ISGID);
		sendmail_mpe_uidflag = ((mode & S_ISUID) == S_ISUID);
		sendmail_mpe_flaginit = true;
	}
	return sendmail_mpe_uidflag;
}

/*
**  SENDMAIL_MPE_GETEGID -- shadow function for getegid()
**
**	If emulation mode is in effect and the saved egid has been
**	initialized, return the saved egid; otherwise return the value of the
**	real getegid() function.
**
**	Parameters:
**		none.
**
**	Returns:
**		emulated egid if present, else true egid.
*/

static gid_t sendmail_mpe_egid = -1;

#undef getegid
gid_t
sendmail_mpe_getegid()
{
	if (sendmail_mpe_emulgid() && sendmail_mpe_egid != -1)
		return sendmail_mpe_egid;
	return getegid();
}

/*
**  SENDMAIL_MPE_GETEUID -- shadow function for geteuid()
**
**	If emulation mode is in effect, return the saved euid; otherwise
**	return the value of the real geteuid() function.
**
**	Note that the initial value of the saved euid is zero, to simulate
**	a setuid-root program.
**
**	Parameters:
**		none
**
**	Returns:
**		emulated euid if in emulation mode, else true euid.
*/

static uid_t sendmail_mpe_euid = 0;

#undef geteuid
uid_t
sendmail_mpe_geteuid()
{
	if (sendmail_mpe_emuluid())
		return sendmail_mpe_euid;
	return geteuid();
}

/*
**  SENDMAIL_MPE_SETGID -- shadow function for setgid()
**
**	Simulate a call to setgid() without actually calling the real
**	function.  Implement the expected uid 0 semantics.
**
**	Note that sendmail will also be calling setuid() which will force an
**	implicit real setgid() to the proper primary gid.  So it doesn't matter
**	that we don't actually alter the real gid in this shadow function.
**
**	Parameters:
**		gid -- desired gid.
**
**	Returns:
**		0 -- emulated success
**		-1 -- emulated failure
*/

#undef setgid
int
sendmail_mpe_setgid(gid)
	gid_t gid;
{
	if (sendmail_mpe_emulgid())
	{
		if (gid == getgid() || sendmail_mpe_euid == 0)
		{
			sendmail_mpe_egid = gid;
			return 0;
		}
		errno = EINVAL;
		return -1;
	}
	return setgid(gid);
}

/*
**  SENDMAIL_MPE_SETUID -- shadow function for setuid()
**
**	setuid() is broken as of MPE 7.0 in that it changes the current
**	working directory to be the home directory of the new uid.  Thus
**	we must obtain the cwd and restore it after the setuid().
**
**	Note that expected uid 0 semantics have been added, as well as
**	remembering the new uid for later use by the other shadow functions.
**
**	Parameters:
**		uid -- desired uid.
**
**	Returns:
**		0 -- success
**		-1 -- failure
**
**	Globals:
**		sendmail_mpe_euid
*/

#undef setuid
int
sendmail_mpe_setuid(uid)
	uid_t uid;
{
	char *cwd;
	char cwd_buf[PATH_MAX + 1];
	int result;
	extern void GETPRIVMODE __P((void));
	extern void GETUSERMODE __P((void));

	if (sendmail_mpe_emuluid())
	{
		if (uid == 0)
		{
			if (sendmail_mpe_euid != 0)
			{
				errno = EINVAL;
				return -1;
			}
			sendmail_mpe_euid = 0;
			return 0;
		}

		/* Preserve the current working directory */
		if ((cwd = getcwd(cwd_buf, PATH_MAX + 1)) == NULL)
			return -1;

		GETPRIVMODE();
		result = setuid(uid);
		GETUSERMODE();

		/* Restore the current working directory */
		chdir(cwd_buf);

		if (result == 0)
			sendmail_mpe_euid = uid;

		return result;
	}
	return setuid(uid);
}
#endif /* MPE */
