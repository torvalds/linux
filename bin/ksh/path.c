/*	$OpenBSD: path.c,v 1.23 2019/06/28 13:34:59 deraadt Exp $	*/

#include <sys/stat.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "sh.h"

/*
 *	Contains a routine to search a : separated list of
 *	paths (a la CDPATH) and make appropriate file names.
 *	Also contains a routine to simplify .'s and ..'s out of
 *	a path name.
 *
 *	Larry Bouzane (larry@cs.mun.ca)
 */

static char	*do_phys_path(XString *, char *, const char *);

/*
 *	Makes a filename into result using the following algorithm.
 *	- make result NULL
 *	- if file starts with '/', append file to result & set cdpathp to NULL
 *	- if file starts with ./ or ../ append cwd and file to result
 *	  and set cdpathp to NULL
 *	- if the first element of cdpathp doesnt start with a '/' xx or '.' xx
 *	  then cwd is appended to result.
 *	- the first element of cdpathp is appended to result
 *	- file is appended to result
 *	- cdpathp is set to the start of the next element in cdpathp (or NULL
 *	  if there are no more elements.
 *	The return value indicates whether a non-null element from cdpathp
 *	was appended to result.
 */
int
make_path(const char *cwd, const char *file,
    char **cdpathp,		/* & of : separated list */
    XString *xsp,
    int *phys_pathp)
{
	int	rval = 0;
	int	use_cdpath = 1;
	char	*plist;
	int	len;
	int	plen = 0;
	char	*xp = Xstring(*xsp, xp);

	if (!file)
		file = null;

	if (file[0] == '/') {
		*phys_pathp = 0;
		use_cdpath = 0;
	} else {
		if (file[0] == '.') {
			char c = file[1];

			if (c == '.')
				c = file[2];
			if (c == '/' || c == '\0')
				use_cdpath = 0;
		}

		plist = *cdpathp;
		if (!plist)
			use_cdpath = 0;
		else if (use_cdpath) {
			char *pend;

			for (pend = plist; *pend && *pend != ':'; pend++)
				;
			plen = pend - plist;
			*cdpathp = *pend ? ++pend : NULL;
		}

		if ((use_cdpath == 0 || !plen || plist[0] != '/') &&
		    (cwd && *cwd)) {
			len = strlen(cwd);
			XcheckN(*xsp, xp, len);
			memcpy(xp, cwd, len);
			xp += len;
			if (cwd[len - 1] != '/')
				Xput(*xsp, xp, '/');
		}
		*phys_pathp = Xlength(*xsp, xp);
		if (use_cdpath && plen) {
			XcheckN(*xsp, xp, plen);
			memcpy(xp, plist, plen);
			xp += plen;
			if (plist[plen - 1] != '/')
				Xput(*xsp, xp, '/');
			rval = 1;
		}
	}

	len = strlen(file) + 1;
	XcheckN(*xsp, xp, len);
	memcpy(xp, file, len);

	if (!use_cdpath)
		*cdpathp = NULL;

	return rval;
}

/*
 * Simplify pathnames containing "." and ".." entries.
 * ie, simplify_path("/a/b/c/./../d/..") returns "/a/b"
 */
void
simplify_path(char *path)
{
	char	*cur;
	char	*t;
	int	isrooted;
	char	*very_start = path;
	char	*start;

	if (!*path)
		return;

	if ((isrooted = (path[0] == '/')))
		very_start++;

	/* Before			After
	 *  /foo/			/foo
	 *  /foo/../../bar		/bar
	 *  /foo/./blah/..		/foo
	 *  .				.
	 *  ..				..
	 *  ./foo			foo
	 *  foo/../../../bar		../../bar
	 */

	for (cur = t = start = very_start; ; ) {
		/* treat multiple '/'s as one '/' */
		while (*t == '/')
			t++;

		if (*t == '\0') {
			if (cur == path)
				/* convert empty path to dot */
				*cur++ = '.';
			*cur = '\0';
			break;
		}

		if (t[0] == '.') {
			if (!t[1] || t[1] == '/') {
				t += 1;
				continue;
			} else if (t[1] == '.' && (!t[2] || t[2] == '/')) {
				if (!isrooted && cur == start) {
					if (cur != very_start)
						*cur++ = '/';
					*cur++ = '.';
					*cur++ = '.';
					start = cur;
				} else if (cur != start)
					while (--cur > start && *cur != '/')
						;
				t += 2;
				continue;
			}
		}

		if (cur != very_start)
			*cur++ = '/';

		/* find/copy next component of pathname */
		while (*t && *t != '/')
			*cur++ = *t++;
	}
}


void
set_current_wd(char *path)
{
	int len;
	char *p = path;

	if (!p && !(p = ksh_get_wd(NULL, 0)))
		p = null;

	len = strlen(p) + 1;

	if (len > current_wd_size)
		current_wd = aresize(current_wd, current_wd_size = len, APERM);
	memcpy(current_wd, p, len);
	if (p != path && p != null)
		afree(p, ATEMP);
}

char *
get_phys_path(const char *path)
{
	XString xs;
	char *xp;

	Xinit(xs, xp, strlen(path) + 1, ATEMP);

	xp = do_phys_path(&xs, xp, path);

	if (!xp)
		return NULL;

	if (Xlength(xs, xp) == 0)
		Xput(xs, xp, '/');
	Xput(xs, xp, '\0');

	return Xclose(xs, xp);
}

static char *
do_phys_path(XString *xsp, char *xp, const char *path)
{
	const char *p, *q;
	int len, llen;
	int savepos;
	char lbuf[PATH_MAX];

	Xcheck(*xsp, xp);
	for (p = path; p; p = q) {
		while (*p == '/')
			p++;
		if (!*p)
			break;
		len = (q = strchr(p, '/')) ? (size_t)(q - p) : strlen(p);
		if (len == 1 && p[0] == '.')
			continue;
		if (len == 2 && p[0] == '.' && p[1] == '.') {
			while (xp > Xstring(*xsp, xp)) {
				xp--;
				if (*xp == '/')
					break;
			}
			continue;
		}

		savepos = Xsavepos(*xsp, xp);
		Xput(*xsp, xp, '/');
		XcheckN(*xsp, xp, len + 1);
		memcpy(xp, p, len);
		xp += len;
		*xp = '\0';

		llen = readlink(Xstring(*xsp, xp), lbuf, sizeof(lbuf) - 1);
		if (llen == -1) {
			/* EINVAL means it wasn't a symlink... */
			if (errno != EINVAL)
				return NULL;
			continue;
		}
		lbuf[llen] = '\0';

		/* If absolute path, start from scratch.. */
		xp = lbuf[0] == '/' ? Xstring(*xsp, xp) :
		    Xrestpos(*xsp, xp, savepos);
		if (!(xp = do_phys_path(xsp, xp, lbuf)))
			return NULL;
	}
	return xp;
}
