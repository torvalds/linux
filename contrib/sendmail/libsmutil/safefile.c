/*
 * Copyright (c) 1998-2004 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>
#include <sm/io.h>
#include <sm/errstring.h>

SM_RCSID("@(#)$Id: safefile.c,v 8.130 2013-11-22 20:51:50 ca Exp $")


/*
**  SAFEFILE -- return 0 if a file exists and is safe for a user.
**
**	Parameters:
**		fn -- filename to check.
**		uid -- user id to compare against.
**		gid -- group id to compare against.
**		user -- user name to compare against (used for group
**			sets).
**		flags -- modifiers:
**			SFF_MUSTOWN -- "uid" must own this file.
**			SFF_NOSLINK -- file cannot be a symbolic link.
**		mode -- mode bits that must match.
**		st -- if set, points to a stat structure that will
**			get the stat info for the file.
**
**	Returns:
**		0 if fn exists, is owned by uid, and matches mode.
**		An errno otherwise.  The actual errno is cleared.
**
**	Side Effects:
**		none.
*/

int
safefile(fn, uid, gid, user, flags, mode, st)
	char *fn;
	UID_T uid;
	GID_T gid;
	char *user;
	long flags;
	int mode;
	struct stat *st;
{
	register char *p;
	register struct group *gr = NULL;
	int file_errno = 0;
	bool checkpath;
	struct stat stbuf;
	struct stat fstbuf;
	char fbuf[MAXPATHLEN];

	if (tTd(44, 4))
		sm_dprintf("safefile(%s, uid=%d, gid=%d, flags=%lx, mode=%o):\n",
			fn, (int) uid, (int) gid, flags, mode);
	errno = 0;
	if (sm_strlcpy(fbuf, fn, sizeof fbuf) >= sizeof fbuf)
	{
		if (tTd(44, 4))
			sm_dprintf("\tpathname too long\n");
		return ENAMETOOLONG;
	}
	fn = fbuf;
	if (st == NULL)
		st = &fstbuf;

	/* ignore SFF_SAFEDIRPATH if we are debugging */
	if (RealUid != 0 && RunAsUid == RealUid)
		flags &= ~SFF_SAFEDIRPATH;

	/* first check to see if the file exists at all */
# if HASLSTAT
	if ((bitset(SFF_NOSLINK, flags) ? lstat(fn, st)
					: stat(fn, st)) < 0)
# else /* HASLSTAT */
	if (stat(fn, st) < 0)
# endif /* HASLSTAT */
	{
		file_errno = errno;
	}
	else if (bitset(SFF_SETUIDOK, flags) &&
		 !bitset(S_IXUSR|S_IXGRP|S_IXOTH, st->st_mode) &&
		 S_ISREG(st->st_mode))
	{
		/*
		**  If final file is set-user-ID, run as the owner of that
		**  file.  Gotta be careful not to reveal anything too
		**  soon here!
		*/

# ifdef SUID_ROOT_FILES_OK
		if (bitset(S_ISUID, st->st_mode))
# else /* SUID_ROOT_FILES_OK */
		if (bitset(S_ISUID, st->st_mode) && st->st_uid != 0 &&
		    st->st_uid != TrustedUid)
# endif /* SUID_ROOT_FILES_OK */
		{
			uid = st->st_uid;
			user = NULL;
		}
# ifdef SUID_ROOT_FILES_OK
		if (bitset(S_ISGID, st->st_mode))
# else /* SUID_ROOT_FILES_OK */
		if (bitset(S_ISGID, st->st_mode) && st->st_gid != 0)
# endif /* SUID_ROOT_FILES_OK */
			gid = st->st_gid;
	}

	checkpath = !bitset(SFF_NOPATHCHECK, flags) ||
		    (uid == 0 && !bitset(SFF_ROOTOK|SFF_OPENASROOT, flags));
	if (bitset(SFF_NOWLINK, flags) && !bitset(SFF_SAFEDIRPATH, flags))
	{
		int ret;

		/* check the directory */
		p = strrchr(fn, '/');
		if (p == NULL)
		{
			ret = safedirpath(".", uid, gid, user,
					  flags|SFF_SAFEDIRPATH, 0, 0);
		}
		else
		{
			*p = '\0';
			ret = safedirpath(fn, uid, gid, user,
					  flags|SFF_SAFEDIRPATH, 0, 0);
			*p = '/';
		}
		if (ret == 0)
		{
			/* directory is safe */
			checkpath = false;
		}
		else
		{
# if HASLSTAT
			/* Need lstat() information if called stat() before */
			if (!bitset(SFF_NOSLINK, flags) && lstat(fn, st) < 0)
			{
				ret = errno;
				if (tTd(44, 4))
					sm_dprintf("\t%s\n", sm_errstring(ret));
				return ret;
			}
# endif /* HASLSTAT */
			/* directory is writable: disallow links */
			flags |= SFF_NOLINK;
		}
	}

	if (checkpath)
	{
		int ret;

		p = strrchr(fn, '/');
		if (p == NULL)
		{
			ret = safedirpath(".", uid, gid, user, flags, 0, 0);
		}
		else
		{
			*p = '\0';
			ret = safedirpath(fn, uid, gid, user, flags, 0, 0);
			*p = '/';
		}
		if (ret != 0)
			return ret;
	}

	/*
	**  If the target file doesn't exist, check the directory to
	**  ensure that it is writable by this user.
	*/

	if (file_errno != 0)
	{
		int ret = file_errno;
		char *dir = fn;

		if (tTd(44, 4))
			sm_dprintf("\t%s\n", sm_errstring(ret));

		errno = 0;
		if (!bitset(SFF_CREAT, flags) || file_errno != ENOENT)
			return ret;

		/* check to see if legal to create the file */
		p = strrchr(dir, '/');
		if (p == NULL)
			dir = ".";
		else if (p == dir)
			dir = "/";
		else
			*p = '\0';
		if (stat(dir, &stbuf) >= 0)
		{
			int md = S_IWRITE|S_IEXEC;

			ret = 0;
			if (stbuf.st_uid == uid)
				/* EMPTY */
				;
			else if (uid == 0 && stbuf.st_uid == TrustedUid)
				/* EMPTY */
				;
			else
			{
				md >>= 3;
				if (stbuf.st_gid == gid)
					/* EMPTY */
					;
# ifndef NO_GROUP_SET
				else if (user != NULL && !DontInitGroups &&
					 ((gr != NULL &&
					   gr->gr_gid == stbuf.st_gid) ||
					  (gr = getgrgid(stbuf.st_gid)) != NULL))
				{
					register char **gp;

					for (gp = gr->gr_mem; *gp != NULL; gp++)
						if (strcmp(*gp, user) == 0)
							break;
					if (*gp == NULL)
						md >>= 3;
				}
# endif /* ! NO_GROUP_SET */
				else
					md >>= 3;
			}
			if ((stbuf.st_mode & md) != md)
				ret = errno = EACCES;
		}
		else
			ret = errno;
		if (tTd(44, 4))
			sm_dprintf("\t[final dir %s uid %d mode %lo] %s\n",
				dir, (int) stbuf.st_uid,
				(unsigned long) stbuf.st_mode,
				sm_errstring(ret));
		if (p != NULL)
			*p = '/';
		st->st_mode = ST_MODE_NOFILE;
		return ret;
	}

# ifdef S_ISLNK
	if (bitset(SFF_NOSLINK, flags) && S_ISLNK(st->st_mode))
	{
		if (tTd(44, 4))
			sm_dprintf("\t[slink mode %lo]\tE_SM_NOSLINK\n",
				(unsigned long) st->st_mode);
		return E_SM_NOSLINK;
	}
# endif /* S_ISLNK */
	if (bitset(SFF_REGONLY, flags) && !S_ISREG(st->st_mode))
	{
		if (tTd(44, 4))
			sm_dprintf("\t[non-reg mode %lo]\tE_SM_REGONLY\n",
				(unsigned long) st->st_mode);
		return E_SM_REGONLY;
	}
	if (bitset(SFF_NOGWFILES, flags) &&
	    bitset(S_IWGRP, st->st_mode))
	{
		if (tTd(44, 4))
			sm_dprintf("\t[write bits %lo]\tE_SM_GWFILE\n",
				(unsigned long) st->st_mode);
		return E_SM_GWFILE;
	}
	if (bitset(SFF_NOWWFILES, flags) &&
	    bitset(S_IWOTH, st->st_mode))
	{
		if (tTd(44, 4))
			sm_dprintf("\t[write bits %lo]\tE_SM_WWFILE\n",
				(unsigned long) st->st_mode);
		return E_SM_WWFILE;
	}
	if (bitset(SFF_NOGRFILES, flags) && bitset(S_IRGRP, st->st_mode))
	{
		if (tTd(44, 4))
			sm_dprintf("\t[read bits %lo]\tE_SM_GRFILE\n",
				(unsigned long) st->st_mode);
		return E_SM_GRFILE;
	}
	if (bitset(SFF_NOWRFILES, flags) && bitset(S_IROTH, st->st_mode))
	{
		if (tTd(44, 4))
			sm_dprintf("\t[read bits %lo]\tE_SM_WRFILE\n",
				(unsigned long) st->st_mode);
		return E_SM_WRFILE;
	}
	if (!bitset(SFF_EXECOK, flags) &&
	    bitset(S_IWUSR|S_IWGRP|S_IWOTH, mode) &&
	    bitset(S_IXUSR|S_IXGRP|S_IXOTH, st->st_mode))
	{
		if (tTd(44, 4))
			sm_dprintf("\t[exec bits %lo]\tE_SM_ISEXEC\n",
				(unsigned long) st->st_mode);
		return E_SM_ISEXEC;
	}
	if (bitset(SFF_NOHLINK, flags) && st->st_nlink != 1)
	{
		if (tTd(44, 4))
			sm_dprintf("\t[link count %d]\tE_SM_NOHLINK\n",
				(int) st->st_nlink);
		return E_SM_NOHLINK;
	}

	if (uid == 0 && bitset(SFF_OPENASROOT, flags))
		/* EMPTY */
		;
	else if (uid == 0 && !bitset(SFF_ROOTOK, flags))
		mode >>= 6;
	else if (st->st_uid == uid)
		/* EMPTY */
		;
	else if (uid == 0 && st->st_uid == TrustedUid)
		/* EMPTY */
		;
	else
	{
		mode >>= 3;
		if (st->st_gid == gid)
			/* EMPTY */
			;
# ifndef NO_GROUP_SET
		else if (user != NULL && !DontInitGroups &&
			 ((gr != NULL && gr->gr_gid == st->st_gid) ||
			  (gr = getgrgid(st->st_gid)) != NULL))
		{
			register char **gp;

			for (gp = gr->gr_mem; *gp != NULL; gp++)
				if (strcmp(*gp, user) == 0)
					break;
			if (*gp == NULL)
				mode >>= 3;
		}
# endif /* ! NO_GROUP_SET */
		else
			mode >>= 3;
	}
	if (tTd(44, 4))
		sm_dprintf("\t[uid %d, nlink %d, stat %lo, mode %lo] ",
			(int) st->st_uid, (int) st->st_nlink,
			(unsigned long) st->st_mode, (unsigned long) mode);
	if ((st->st_uid == uid || st->st_uid == 0 ||
	     st->st_uid == TrustedUid ||
	     !bitset(SFF_MUSTOWN, flags)) &&
	    (st->st_mode & mode) == mode)
	{
		if (tTd(44, 4))
			sm_dprintf("\tOK\n");
		return 0;
	}
	if (tTd(44, 4))
		sm_dprintf("\tEACCES\n");
	return EACCES;
}
/*
**  SAFEDIRPATH -- check to make sure a path to a directory is safe
**
**	Safe means not writable and owned by the right folks.
**
**	Parameters:
**		fn -- filename to check.
**		uid -- user id to compare against.
**		gid -- group id to compare against.
**		user -- user name to compare against (used for group
**			sets).
**		flags -- modifiers:
**			SFF_ROOTOK -- ok to use root permissions to open.
**			SFF_SAFEDIRPATH -- writable directories are considered
**				to be fatal errors.
**		level -- symlink recursive level.
**		offset -- offset into fn to start checking from.
**
**	Returns:
**		0 -- if the directory path is "safe".
**		else -- an error number associated with the path.
*/

int
safedirpath(fn, uid, gid, user, flags, level, offset)
	char *fn;
	UID_T uid;
	GID_T gid;
	char *user;
	long flags;
	int level;
	int offset;
{
	int ret = 0;
	int mode = S_IWOTH;
	char save = '\0';
	char *saveptr = NULL;
	char *p, *enddir;
	register struct group *gr = NULL;
	char s[MAXLINKPATHLEN];
	struct stat stbuf;

	/* make sure we aren't in a symlink loop */
	if (level > MAXSYMLINKS)
		return ELOOP;

	if (level < 0 || offset < 0 || offset > strlen(fn))
		return EINVAL;

	/* special case root directory */
	if (*fn == '\0')
		fn = "/";

	if (tTd(44, 4))
		sm_dprintf("safedirpath(%s, uid=%ld, gid=%ld, flags=%lx, level=%d, offset=%d):\n",
			fn, (long) uid, (long) gid, flags, level, offset);

	if (!bitnset(DBS_GROUPWRITABLEDIRPATHSAFE, DontBlameSendmail))
		mode |= S_IWGRP;

	/* Make a modifiable copy of the filename */
	if (sm_strlcpy(s, fn, sizeof s) >= sizeof s)
		return EINVAL;

	p = s + offset;
	while (p != NULL)
	{
		/* put back character */
		if (saveptr != NULL)
		{
			*saveptr = save;
			saveptr = NULL;
			p++;
		}

		if (*p == '\0')
			break;

		p = strchr(p, '/');

		/* Special case for root directory */
		if (p == s)
		{
			save = *(p + 1);
			saveptr = p + 1;
			*(p + 1) = '\0';
		}
		else if (p != NULL)
		{
			save = *p;
			saveptr = p;
			*p = '\0';
		}

		/* Heuristic: . and .. have already been checked */
		enddir = strrchr(s, '/');
		if (enddir != NULL &&
		    (strcmp(enddir, "/..") == 0 ||
		     strcmp(enddir, "/.") == 0))
			continue;

		if (tTd(44, 20))
			sm_dprintf("\t[dir %s]\n", s);

# if HASLSTAT
		ret = lstat(s, &stbuf);
# else /* HASLSTAT */
		ret = stat(s, &stbuf);
# endif /* HASLSTAT */
		if (ret < 0)
		{
			ret = errno;
			break;
		}

# ifdef S_ISLNK
		/* Follow symlinks */
		if (S_ISLNK(stbuf.st_mode))
		{
			int linklen;
			char *target;
			char buf[MAXPATHLEN];
			char fullbuf[MAXLINKPATHLEN];

			memset(buf, '\0', sizeof buf);
			linklen = readlink(s, buf, sizeof buf);
			if (linklen < 0)
			{
				ret = errno;
				break;
			}
			if (linklen >= sizeof buf)
			{
				/* file name too long for buffer */
				ret = errno = EINVAL;
				break;
			}

			offset = 0;
			if (*buf == '/')
			{
				target = buf;

				/* If path is the same, avoid rechecks */
				while (s[offset] == buf[offset] &&
				       s[offset] != '\0')
					offset++;

				if (s[offset] == '\0' && buf[offset] == '\0')
				{
					/* strings match, symlink loop */
					return ELOOP;
				}

				/* back off from the mismatch */
				if (offset > 0)
					offset--;

				/* Make sure we are at a directory break */
				if (offset > 0 &&
				    s[offset] != '/' &&
				    s[offset] != '\0')
				{
					while (buf[offset] != '/' &&
					       offset > 0)
						offset--;
				}
				if (offset > 0 &&
				    s[offset] == '/' &&
				    buf[offset] == '/')
				{
					/* Include the trailing slash */
					offset++;
				}
			}
			else
			{
				char *sptr;

				sptr = strrchr(s, '/');
				if (sptr != NULL)
				{
					*sptr = '\0';
					offset = sptr + 1 - s;
					if (sm_strlcpyn(fullbuf,
							sizeof fullbuf, 2,
							s, "/") >=
						sizeof fullbuf ||
					    sm_strlcat(fullbuf, buf,
						       sizeof fullbuf) >=
						sizeof fullbuf)
					{
						ret = EINVAL;
						break;
					}
					*sptr = '/';
				}
				else
				{
					if (sm_strlcpy(fullbuf, buf,
						       sizeof fullbuf) >=
						sizeof fullbuf)
					{
						ret = EINVAL;
						break;
					}
				}
				target = fullbuf;
			}
			ret = safedirpath(target, uid, gid, user, flags,
					  level + 1, offset);
			if (ret != 0)
				break;

			/* Don't check permissions on the link file itself */
			continue;
		}
#endif /* S_ISLNK */

		if ((uid == 0 || bitset(SFF_SAFEDIRPATH, flags)) &&
#ifdef S_ISVTX
		    !(bitnset(DBS_TRUSTSTICKYBIT, DontBlameSendmail) &&
		      bitset(S_ISVTX, stbuf.st_mode)) &&
#endif /* S_ISVTX */
		    bitset(mode, stbuf.st_mode))
		{
			if (tTd(44, 4))
				sm_dprintf("\t[dir %s] mode %lo ",
					s, (unsigned long) stbuf.st_mode);
			if (bitset(SFF_SAFEDIRPATH, flags))
			{
				if (bitset(S_IWOTH, stbuf.st_mode))
					ret = E_SM_WWDIR;
				else
					ret = E_SM_GWDIR;
				if (tTd(44, 4))
					sm_dprintf("FATAL\n");
				break;
			}
			if (tTd(44, 4))
				sm_dprintf("WARNING\n");
			if (Verbose > 1)
				message("051 WARNING: %s writable directory %s",
					bitset(S_IWOTH, stbuf.st_mode)
					   ? "World"
					   : "Group",
					s);
		}
		if (uid == 0 && !bitset(SFF_ROOTOK|SFF_OPENASROOT, flags))
		{
			if (bitset(S_IXOTH, stbuf.st_mode))
				continue;
			ret = EACCES;
			break;
		}

		/*
		**  Let OS determine access to file if we are not
		**  running as a privileged user.  This allows ACLs
		**  to work.  Also, if opening as root, assume we can
		**  scan the directory.
		*/
		if (geteuid() != 0 || bitset(SFF_OPENASROOT, flags))
			continue;

		if (stbuf.st_uid == uid &&
		    bitset(S_IXUSR, stbuf.st_mode))
			continue;
		if (stbuf.st_gid == gid &&
		    bitset(S_IXGRP, stbuf.st_mode))
			continue;
# ifndef NO_GROUP_SET
		if (user != NULL && !DontInitGroups &&
		    ((gr != NULL && gr->gr_gid == stbuf.st_gid) ||
		     (gr = getgrgid(stbuf.st_gid)) != NULL))
		{
			register char **gp;

			for (gp = gr->gr_mem; gp != NULL && *gp != NULL; gp++)
				if (strcmp(*gp, user) == 0)
					break;
			if (gp != NULL && *gp != NULL &&
			    bitset(S_IXGRP, stbuf.st_mode))
				continue;
		}
# endif /* ! NO_GROUP_SET */
		if (!bitset(S_IXOTH, stbuf.st_mode))
		{
			ret = EACCES;
			break;
		}
	}
	if (tTd(44, 4))
		sm_dprintf("\t[dir %s] %s\n", fn,
			ret == 0 ? "OK" : sm_errstring(ret));
	return ret;
}
/*
**  SAFEOPEN -- do a file open with extra checking
**
**	Parameters:
**		fn -- the file name to open.
**		omode -- the open-style mode flags.
**		cmode -- the create-style mode flags.
**		sff -- safefile flags.
**
**	Returns:
**		Same as open.
*/

int
safeopen(fn, omode, cmode, sff)
	char *fn;
	int omode;
	int cmode;
	long sff;
{
#if !NOFTRUNCATE
	bool truncate;
#endif /* !NOFTRUNCATE */
	int rval;
	int fd;
	int smode;
	struct stat stb;

	if (tTd(44, 10))
		sm_dprintf("safeopen: fn=%s, omode=%x, cmode=%x, sff=%lx\n",
			   fn, omode, cmode, sff);

	if (bitset(O_CREAT, omode))
		sff |= SFF_CREAT;
	omode &= ~O_CREAT;
	switch (omode & O_ACCMODE)
	{
	  case O_RDONLY:
		smode = S_IREAD;
		break;

	  case O_WRONLY:
		smode = S_IWRITE;
		break;

	  case O_RDWR:
		smode = S_IREAD|S_IWRITE;
		break;

	  default:
		smode = 0;
		break;
	}
	if (bitset(SFF_OPENASROOT, sff))
		rval = safefile(fn, RunAsUid, RunAsGid, RunAsUserName,
				sff, smode, &stb);
	else
		rval = safefile(fn, RealUid, RealGid, RealUserName,
				sff, smode, &stb);
	if (rval != 0)
	{
		errno = rval;
		return -1;
	}
	if (stb.st_mode == ST_MODE_NOFILE && bitset(SFF_CREAT, sff))
		omode |= O_CREAT | (bitset(SFF_NOTEXCL, sff) ? 0 : O_EXCL);
	else if (bitset(SFF_CREAT, sff) && bitset(O_EXCL, omode))
	{
		/* The file exists so an exclusive create would fail */
		errno = EEXIST;
		return -1;
	}

#if !NOFTRUNCATE
	truncate = bitset(O_TRUNC, omode);
	if (truncate)
		omode &= ~O_TRUNC;
#endif /* !NOFTRUNCATE */

	fd = dfopen(fn, omode, cmode, sff);
	if (fd < 0)
		return fd;
	if (filechanged(fn, fd, &stb))
	{
		syserr("554 5.3.0 cannot open: file %s changed after open", fn);
		(void) close(fd);
		errno = E_SM_FILECHANGE;
		return -1;
	}

#if !NOFTRUNCATE
	if (truncate &&
	    ftruncate(fd, (off_t) 0) < 0)
	{
		int save_errno;

		save_errno = errno;
		syserr("554 5.3.0 cannot open: file %s could not be truncated",
		       fn);
		(void) close(fd);
		errno = save_errno;
		return -1;
	}
#endif /* !NOFTRUNCATE */

	return fd;
}
/*
**  SAFEFOPEN -- do a file open with extra checking
**
**	Parameters:
**		fn -- the file name to open.
**		omode -- the open-style mode flags.
**		cmode -- the create-style mode flags.
**		sff -- safefile flags.
**
**	Returns:
**		Same as fopen.
*/

SM_FILE_T *
safefopen(fn, omode, cmode, sff)
	char *fn;
	int omode;
	int cmode;
	long sff;
{
	int fd;
	int save_errno;
	SM_FILE_T *fp;
	int fmode;

	switch (omode & O_ACCMODE)
	{
	  case O_RDONLY:
		fmode = SM_IO_RDONLY;
		break;

	  case O_WRONLY:
		if (bitset(O_APPEND, omode))
			fmode = SM_IO_APPEND;
		else
			fmode = SM_IO_WRONLY;
		break;

	  case O_RDWR:
		if (bitset(O_TRUNC, omode))
			fmode = SM_IO_RDWRTR;
		else if (bitset(O_APPEND, omode))
			fmode = SM_IO_APPENDRW;
		else
			fmode = SM_IO_RDWR;
		break;

	  default:
		syserr("554 5.3.5 safefopen: unknown omode %o", omode);
		fmode = 0;
	}
	fd = safeopen(fn, omode, cmode, sff);
	if (fd < 0)
	{
		save_errno = errno;
		if (tTd(44, 10))
			sm_dprintf("safefopen: safeopen failed: %s\n",
				   sm_errstring(errno));
		errno = save_errno;
		return NULL;
	}
	fp = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT,
			(void *) &fd, fmode, NULL);
	if (fp != NULL)
		return fp;

	save_errno = errno;
	if (tTd(44, 10))
	{
		sm_dprintf("safefopen: fdopen(%s, %d) failed: omode=%x, sff=%lx, err=%s\n",
			   fn, fmode, omode, sff, sm_errstring(errno));
	}
	(void) close(fd);
	errno = save_errno;
	return NULL;
}
/*
**  FILECHANGED -- check to see if file changed after being opened
**
**	Parameters:
**		fn -- pathname of file to check.
**		fd -- file descriptor to check.
**		stb -- stat structure from before open.
**
**	Returns:
**		true -- if a problem was detected.
**		false -- if this file is still the same.
*/

bool
filechanged(fn, fd, stb)
	char *fn;
	int fd;
	struct stat *stb;
{
	struct stat sta;

	if (stb->st_mode == ST_MODE_NOFILE)
	{
# if HASLSTAT && BOGUS_O_EXCL
		/* only necessary if exclusive open follows symbolic links */
		if (lstat(fn, stb) < 0 || stb->st_nlink != 1)
			return true;
# else /* HASLSTAT && BOGUS_O_EXCL */
		return false;
# endif /* HASLSTAT && BOGUS_O_EXCL */
	}
	if (fstat(fd, &sta) < 0)
		return true;

	if (sta.st_nlink != stb->st_nlink ||
	    sta.st_dev != stb->st_dev ||
	    sta.st_ino != stb->st_ino ||
# if HAS_ST_GEN && 0		/* AFS returns garbage in st_gen */
	    sta.st_gen != stb->st_gen ||
# endif /* HAS_ST_GEN && 0 */
	    sta.st_uid != stb->st_uid ||
	    sta.st_gid != stb->st_gid)
	{
		if (tTd(44, 8))
		{
			sm_dprintf("File changed after opening:\n");
			sm_dprintf(" nlink	= %ld/%ld\n",
				(long) stb->st_nlink, (long) sta.st_nlink);
			sm_dprintf(" dev	= %ld/%ld\n",
				(long) stb->st_dev, (long) sta.st_dev);
			sm_dprintf(" ino	= %llu/%llu\n",
				(ULONGLONG_T) stb->st_ino,
				(ULONGLONG_T) sta.st_ino);
# if HAS_ST_GEN
			sm_dprintf(" gen	= %ld/%ld\n",
				(long) stb->st_gen, (long) sta.st_gen);
# endif /* HAS_ST_GEN */
			sm_dprintf(" uid	= %ld/%ld\n",
				(long) stb->st_uid, (long) sta.st_uid);
			sm_dprintf(" gid	= %ld/%ld\n",
				(long) stb->st_gid, (long) sta.st_gid);
		}
		return true;
	}

	return false;
}
/*
**  DFOPEN -- determined file open
**
**	This routine has the semantics of open, except that it will
**	keep trying a few times to make this happen.  The idea is that
**	on very loaded systems, we may run out of resources (inodes,
**	whatever), so this tries to get around it.
*/

int
dfopen(filename, omode, cmode, sff)
	char *filename;
	int omode;
	int cmode;
	long sff;
{
	register int tries;
	int fd = -1;
	struct stat st;

	for (tries = 0; tries < 10; tries++)
	{
		(void) sleep((unsigned) (10 * tries));
		errno = 0;
		fd = open(filename, omode, cmode);
		if (fd >= 0)
			break;
		switch (errno)
		{
		  case ENFILE:		/* system file table full */
		  case EINTR:		/* interrupted syscall */
#ifdef ETXTBSY
		  case ETXTBSY:		/* Apollo: net file locked */
#endif /* ETXTBSY */
			continue;
		}
		break;
	}
	if (!bitset(SFF_NOLOCK, sff) &&
	    fd >= 0 &&
	    fstat(fd, &st) >= 0 &&
	    S_ISREG(st.st_mode))
	{
		int locktype;

		/* lock the file to avoid accidental conflicts */
		if ((omode & O_ACCMODE) != O_RDONLY)
			locktype = LOCK_EX;
		else
			locktype = LOCK_SH;
		if (bitset(SFF_NBLOCK, sff))
			locktype |= LOCK_NB;

		if (!lockfile(fd, filename, NULL, locktype))
		{
			int save_errno = errno;

			(void) close(fd);
			fd = -1;
			errno = save_errno;
		}
		else
			errno = 0;
	}
	return fd;
}
