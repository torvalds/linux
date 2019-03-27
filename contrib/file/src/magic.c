/*
 * Copyright (c) Christos Zoulas 2003.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef WIN32
#include <windows.h>
#include <shlwapi.h>
#endif

#include "file.h"

#ifndef	lint
FILE_RCSID("@(#)$File: magic.c,v 1.102 2017/08/28 13:39:18 christos Exp $")
#endif	/* lint */

#include "magic.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#ifdef QUICK
#include <sys/mman.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>	/* for PIPE_BUF */
#endif

#if defined(HAVE_UTIMES)
# include <sys/time.h>
#elif defined(HAVE_UTIME)
# if defined(HAVE_SYS_UTIME_H)
#  include <sys/utime.h>
# elif defined(HAVE_UTIME_H)
#  include <utime.h>
# endif
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>	/* for read() */
#endif

#ifndef PIPE_BUF
/* Get the PIPE_BUF from pathconf */
#ifdef _PC_PIPE_BUF
#define PIPE_BUF pathconf(".", _PC_PIPE_BUF)
#else
#define PIPE_BUF 512
#endif
#endif

private void close_and_restore(const struct magic_set *, const char *, int,
    const struct stat *);
private int unreadable_info(struct magic_set *, mode_t, const char *);
private const char* get_default_magic(void);
#ifndef COMPILE_ONLY
private const char *file_or_fd(struct magic_set *, const char *, int);
#endif

#ifndef	STDIN_FILENO
#define	STDIN_FILENO	0
#endif

#ifdef WIN32
/* HINSTANCE of this shared library. Needed for get_default_magic() */
static HINSTANCE _w32_dll_instance = NULL;

static void
_w32_append_path(char **hmagicpath, const char *fmt, ...)
{
	char *tmppath;
        char *newpath;
	va_list ap;

	va_start(ap, fmt);
	if (vasprintf(&tmppath, fmt, ap) < 0) {
		va_end(ap);
		return;
	}
	va_end(ap);

	if (access(tmppath, R_OK) == -1)
		goto out;

	if (*hmagicpath == NULL) {
		*hmagicpath = tmppath;
		return;
	}

	if (asprintf(&newpath, "%s%c%s", *hmagicpath, PATHSEP, tmppath) < 0)
		goto out;

	free(*hmagicpath);
	free(tmppath);
	*hmagicpath = newpath;
	return;
out:
	free(tmppath);
}

static void
_w32_get_magic_relative_to(char **hmagicpath, HINSTANCE module)
{
	static const char *trypaths[] = {
		"%s/share/misc/magic.mgc",
		"%s/magic.mgc",
	};
	LPSTR dllpath;
	size_t sp;

	dllpath = calloc(MAX_PATH + 1, sizeof(*dllpath));

	if (!GetModuleFileNameA(module, dllpath, MAX_PATH))
		goto out;

	PathRemoveFileSpecA(dllpath);

	if (module) {
		char exepath[MAX_PATH];
		GetModuleFileNameA(NULL, exepath, MAX_PATH);
		PathRemoveFileSpecA(exepath);
		if (stricmp(exepath, dllpath) == 0)
			goto out;
	}

	sp = strlen(dllpath);
	if (sp > 3 && stricmp(&dllpath[sp - 3], "bin") == 0) {
		_w32_append_path(hmagicpath,
		    "%s/../share/misc/magic.mgc", dllpath);
		goto out;
	}

	for (sp = 0; sp < __arraycount(trypaths); sp++)
		_w32_append_path(hmagicpath, trypaths[sp], dllpath);
out:
	free(dllpath);
}

/* Placate GCC by offering a sacrificial previous prototype */
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID);

BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason,
    LPVOID lpvReserved __attribute__((__unused__)))
{
	if (fdwReason == DLL_PROCESS_ATTACH)
		_w32_dll_instance = hinstDLL;
	return 1;
}
#endif

private const char *
get_default_magic(void)
{
	static const char hmagic[] = "/.magic/magic.mgc";
	static char *default_magic;
	char *home, *hmagicpath;

#ifndef WIN32
	struct stat st;

	if (default_magic) {
		free(default_magic);
		default_magic = NULL;
	}
	if ((home = getenv("HOME")) == NULL)
		return MAGIC;

	if (asprintf(&hmagicpath, "%s/.magic.mgc", home) < 0)
		return MAGIC;
	if (stat(hmagicpath, &st) == -1) {
		free(hmagicpath);
		if (asprintf(&hmagicpath, "%s/.magic", home) < 0)
			return MAGIC;
		if (stat(hmagicpath, &st) == -1)
			goto out;
		if (S_ISDIR(st.st_mode)) {
			free(hmagicpath);
			if (asprintf(&hmagicpath, "%s/%s", home, hmagic) < 0)
				return MAGIC;
			if (access(hmagicpath, R_OK) == -1)
				goto out;
		}
	}

	if (asprintf(&default_magic, "%s:%s", hmagicpath, MAGIC) < 0)
		goto out;
	free(hmagicpath);
	return default_magic;
out:
	default_magic = NULL;
	free(hmagicpath);
	return MAGIC;
#else
	hmagicpath = NULL;

	if (default_magic) {
		free(default_magic);
		default_magic = NULL;
	}

	/* First, try to get a magic file from user-application data */
	if ((home = getenv("LOCALAPPDATA")) != NULL)
		_w32_append_path(&hmagicpath, "%s%s", home, hmagic);

	/* Second, try to get a magic file from the user profile data */
	if ((home = getenv("USERPROFILE")) != NULL)
		_w32_append_path(&hmagicpath,
		    "%s/Local Settings/Application Data%s", home, hmagic);

	/* Third, try to get a magic file from Common Files */
	if ((home = getenv("COMMONPROGRAMFILES")) != NULL)
		_w32_append_path(&hmagicpath, "%s%s", home, hmagic);

	/* Fourth, try to get magic file relative to exe location */
        _w32_get_magic_relative_to(&hmagicpath, NULL);

	/* Fifth, try to get magic file relative to dll location */
        _w32_get_magic_relative_to(&hmagicpath, _w32_dll_instance);

	/* Avoid MAGIC constant - it likely points to a file within MSys tree */
	default_magic = hmagicpath;
	return default_magic;
#endif
}

public const char *
magic_getpath(const char *magicfile, int action)
{
	if (magicfile != NULL)
		return magicfile;

	magicfile = getenv("MAGIC");
	if (magicfile != NULL)
		return magicfile;

	return action == FILE_LOAD ? get_default_magic() : MAGIC;
}

public struct magic_set *
magic_open(int flags)
{
	return file_ms_alloc(flags);
}

private int
unreadable_info(struct magic_set *ms, mode_t md, const char *file)
{
	if (file) {
		/* We cannot open it, but we were able to stat it. */
		if (access(file, W_OK) == 0)
			if (file_printf(ms, "writable, ") == -1)
				return -1;
		if (access(file, X_OK) == 0)
			if (file_printf(ms, "executable, ") == -1)
				return -1;
	}
	if (S_ISREG(md))
		if (file_printf(ms, "regular file, ") == -1)
			return -1;
	if (file_printf(ms, "no read permission") == -1)
		return -1;
	return 0;
}

public void
magic_close(struct magic_set *ms)
{
	if (ms == NULL)
		return;
	file_ms_free(ms);
}

/*
 * load a magic file
 */
public int
magic_load(struct magic_set *ms, const char *magicfile)
{
	if (ms == NULL)
		return -1;
	return file_apprentice(ms, magicfile, FILE_LOAD);
}

#ifndef COMPILE_ONLY
/*
 * Install a set of compiled magic buffers.
 */
public int
magic_load_buffers(struct magic_set *ms, void **bufs, size_t *sizes,
    size_t nbufs)
{
	if (ms == NULL)
		return -1;
	return buffer_apprentice(ms, (struct magic **)bufs, sizes, nbufs);
}
#endif

public int
magic_compile(struct magic_set *ms, const char *magicfile)
{
	if (ms == NULL)
		return -1;
	return file_apprentice(ms, magicfile, FILE_COMPILE);
}

public int
magic_check(struct magic_set *ms, const char *magicfile)
{
	if (ms == NULL)
		return -1;
	return file_apprentice(ms, magicfile, FILE_CHECK);
}

public int
magic_list(struct magic_set *ms, const char *magicfile)
{
	if (ms == NULL)
		return -1;
	return file_apprentice(ms, magicfile, FILE_LIST);
}

private void
close_and_restore(const struct magic_set *ms, const char *name, int fd,
    const struct stat *sb)
{
	if (fd == STDIN_FILENO || name == NULL)
		return;
	(void) close(fd);

	if ((ms->flags & MAGIC_PRESERVE_ATIME) != 0) {
		/*
		 * Try to restore access, modification times if read it.
		 * This is really *bad* because it will modify the status
		 * time of the file... And of course this will affect
		 * backup programs
		 */
#ifdef HAVE_UTIMES
		struct timeval  utsbuf[2];
		(void)memset(utsbuf, 0, sizeof(utsbuf));
		utsbuf[0].tv_sec = sb->st_atime;
		utsbuf[1].tv_sec = sb->st_mtime;

		(void) utimes(name, utsbuf); /* don't care if loses */
#elif defined(HAVE_UTIME_H) || defined(HAVE_SYS_UTIME_H)
		struct utimbuf  utbuf;

		(void)memset(&utbuf, 0, sizeof(utbuf));
		utbuf.actime = sb->st_atime;
		utbuf.modtime = sb->st_mtime;
		(void) utime(name, &utbuf); /* don't care if loses */
#endif
	}
}

#ifndef COMPILE_ONLY

/*
 * find type of descriptor
 */
public const char *
magic_descriptor(struct magic_set *ms, int fd)
{
	if (ms == NULL)
		return NULL;
	return file_or_fd(ms, NULL, fd);
}

/*
 * find type of named file
 */
public const char *
magic_file(struct magic_set *ms, const char *inname)
{
	if (ms == NULL)
		return NULL;
	return file_or_fd(ms, inname, STDIN_FILENO);
}

private const char *
file_or_fd(struct magic_set *ms, const char *inname, int fd)
{
	int	rv = -1;
	unsigned char *buf;
	struct stat	sb;
	ssize_t nbytes = 0;	/* number of bytes read from a datafile */
	int	ispipe = 0;
	off_t	pos = (off_t)-1;

	if (file_reset(ms, 1) == -1)
		goto out;

	/*
	 * one extra for terminating '\0', and
	 * some overlapping space for matches near EOF
	 */
#define SLOP (1 + sizeof(union VALUETYPE))
	if ((buf = CAST(unsigned char *, malloc(ms->bytes_max + SLOP))) == NULL)
		return NULL;

	switch (file_fsmagic(ms, inname, &sb)) {
	case -1:		/* error */
		goto done;
	case 0:			/* nothing found */
		break;
	default:		/* matched it and printed type */
		rv = 0;
		goto done;
	}

#ifdef WIN32
	/* Place stdin in binary mode, so EOF (Ctrl+Z) doesn't stop early. */
	if (fd == STDIN_FILENO)
		_setmode(STDIN_FILENO, O_BINARY);
#endif

	if (inname == NULL) {
		if (fstat(fd, &sb) == 0 && S_ISFIFO(sb.st_mode))
			ispipe = 1;
		else
			pos = lseek(fd, (off_t)0, SEEK_CUR);
	} else {
		int flags = O_RDONLY|O_BINARY;
		int okstat = stat(inname, &sb) == 0;

		if (okstat && S_ISFIFO(sb.st_mode)) {
#ifdef O_NONBLOCK
			flags |= O_NONBLOCK;
#endif
			ispipe = 1;
		}

		errno = 0;
		if ((fd = open(inname, flags)) < 0) {
#ifdef WIN32
			/*
			 * Can't stat, can't open.  It may have been opened in
			 * fsmagic, so if the user doesn't have read permission,
			 * allow it to say so; otherwise an error was probably
			 * displayed in fsmagic.
			 */
			if (!okstat && errno == EACCES) {
				sb.st_mode = S_IFBLK;
				okstat = 1;
			}
#endif
			if (okstat &&
			    unreadable_info(ms, sb.st_mode, inname) == -1)
				goto done;
			rv = 0;
			goto done;
		}
#ifdef O_NONBLOCK
		if ((flags = fcntl(fd, F_GETFL)) != -1) {
			flags &= ~O_NONBLOCK;
			(void)fcntl(fd, F_SETFL, flags);
		}
#endif
	}

	/*
	 * try looking at the first ms->bytes_max bytes
	 */
	if (ispipe) {
		ssize_t r = 0;

		while ((r = sread(fd, (void *)&buf[nbytes],
		    (size_t)(ms->bytes_max - nbytes), 1)) > 0) {
			nbytes += r;
			if (r < PIPE_BUF) break;
		}

		if (nbytes == 0 && inname) {
			/* We can not read it, but we were able to stat it. */
			if (unreadable_info(ms, sb.st_mode, inname) == -1)
				goto done;
			rv = 0;
			goto done;
		}

	} else {
		/* Windows refuses to read from a big console buffer. */
		size_t howmany =
#if defined(WIN32)
				_isatty(fd) ? 8 * 1024 :
#endif
				ms->bytes_max;
		if ((nbytes = read(fd, (char *)buf, howmany)) == -1) {
			if (inname == NULL && fd != STDIN_FILENO)
				file_error(ms, errno, "cannot read fd %d", fd);
			else
				file_error(ms, errno, "cannot read `%s'",
				    inname == NULL ? "/dev/stdin" : inname);
			goto done;
		}
	}

	(void)memset(buf + nbytes, 0, SLOP); /* NUL terminate */
	if (file_buffer(ms, fd, inname, buf, (size_t)nbytes) == -1)
		goto done;
	rv = 0;
done:
	free(buf);
	if (fd != -1) {
		if (pos != (off_t)-1)
			(void)lseek(fd, pos, SEEK_SET);
		close_and_restore(ms, inname, fd, &sb);
	}
out:
	return rv == 0 ? file_getbuffer(ms) : NULL;
}


public const char *
magic_buffer(struct magic_set *ms, const void *buf, size_t nb)
{
	if (ms == NULL)
		return NULL;
	if (file_reset(ms, 1) == -1)
		return NULL;
	/*
	 * The main work is done here!
	 * We have the file name and/or the data buffer to be identified.
	 */
	if (file_buffer(ms, -1, NULL, buf, nb) == -1) {
		return NULL;
	}
	return file_getbuffer(ms);
}
#endif

public const char *
magic_error(struct magic_set *ms)
{
	if (ms == NULL)
		return "Magic database is not open";
	return (ms->event_flags & EVENT_HAD_ERR) ? ms->o.buf : NULL;
}

public int
magic_errno(struct magic_set *ms)
{
	if (ms == NULL)
		return EINVAL;
	return (ms->event_flags & EVENT_HAD_ERR) ? ms->error : 0;
}

public int
magic_getflags(struct magic_set *ms)
{
	if (ms == NULL)
		return -1;

	return ms->flags;
}

public int
magic_setflags(struct magic_set *ms, int flags)
{
	if (ms == NULL)
		return -1;
#if !defined(HAVE_UTIME) && !defined(HAVE_UTIMES)
	if (flags & MAGIC_PRESERVE_ATIME)
		return -1;
#endif
	ms->flags = flags;
	return 0;
}

public int
magic_version(void)
{
	return MAGIC_VERSION;
}

public int
magic_setparam(struct magic_set *ms, int param, const void *val)
{
	switch (param) {
	case MAGIC_PARAM_INDIR_MAX:
		ms->indir_max = (uint16_t)*(const size_t *)val;
		return 0;
	case MAGIC_PARAM_NAME_MAX:
		ms->name_max = (uint16_t)*(const size_t *)val;
		return 0;
	case MAGIC_PARAM_ELF_PHNUM_MAX:
		ms->elf_phnum_max = (uint16_t)*(const size_t *)val;
		return 0;
	case MAGIC_PARAM_ELF_SHNUM_MAX:
		ms->elf_shnum_max = (uint16_t)*(const size_t *)val;
		return 0;
	case MAGIC_PARAM_ELF_NOTES_MAX:
		ms->elf_notes_max = (uint16_t)*(const size_t *)val;
		return 0;
	case MAGIC_PARAM_REGEX_MAX:
		ms->elf_notes_max = (uint16_t)*(const size_t *)val;
		return 0;
	case MAGIC_PARAM_BYTES_MAX:
		ms->bytes_max = *(const size_t *)val;
		return 0;
	default:
		errno = EINVAL;
		return -1;
	}
}

public int
magic_getparam(struct magic_set *ms, int param, void *val)
{
	switch (param) {
	case MAGIC_PARAM_INDIR_MAX:
		*(size_t *)val = ms->indir_max;
		return 0;
	case MAGIC_PARAM_NAME_MAX:
		*(size_t *)val = ms->name_max;
		return 0;
	case MAGIC_PARAM_ELF_PHNUM_MAX:
		*(size_t *)val = ms->elf_phnum_max;
		return 0;
	case MAGIC_PARAM_ELF_SHNUM_MAX:
		*(size_t *)val = ms->elf_shnum_max;
		return 0;
	case MAGIC_PARAM_ELF_NOTES_MAX:
		*(size_t *)val = ms->elf_notes_max;
		return 0;
	case MAGIC_PARAM_REGEX_MAX:
		*(size_t *)val = ms->regex_max;
		return 0;
	case MAGIC_PARAM_BYTES_MAX:
		*(size_t *)val = ms->bytes_max;
		return 0;
	default:
		errno = EINVAL;
		return -1;
	}
}
