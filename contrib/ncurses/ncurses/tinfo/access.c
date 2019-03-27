/****************************************************************************
 * Copyright (c) 1998-2011,2012 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Thomas E. Dickey                                                *
 ****************************************************************************/

#include <curses.priv.h>

#include <ctype.h>

#include <tic.h>

MODULE_ID("$Id: access.c,v 1.23 2012/09/01 19:21:29 tom Exp $")

#ifdef __TANDEM
#define ROOT_UID 65535
#endif

#ifndef ROOT_UID
#define ROOT_UID 0
#endif

#define LOWERCASE(c) ((isalpha(UChar(c)) && isupper(UChar(c))) ? tolower(UChar(c)) : (c))

NCURSES_EXPORT(char *)
_nc_rootname(char *path)
{
    char *result = _nc_basename(path);
#if !MIXEDCASE_FILENAMES || defined(PROG_EXT)
    static char *temp;
    char *s;

    temp = strdup(result);
    result = temp;
#if !MIXEDCASE_FILENAMES
    for (s = result; *s != '\0'; ++s) {
	*s = (char) LOWERCASE(*s);
    }
#endif
#if defined(PROG_EXT)
    if ((s = strrchr(result, '.')) != 0) {
	if (!strcmp(s, PROG_EXT))
	    *s = '\0';
    }
#endif
#endif
    return result;
}

/*
 * Check if a string appears to be an absolute pathname.
 */
NCURSES_EXPORT(bool)
_nc_is_abs_path(const char *path)
{
#if defined(__EMX__) || defined(__DJGPP__)
#define is_pathname(s) ((((s) != 0) && ((s)[0] == '/')) \
		  || (((s)[0] != 0) && ((s)[1] == ':')))
#else
#define is_pathname(s) ((s) != 0 && (s)[0] == '/')
#endif
    return is_pathname(path);
}

/*
 * Return index of the basename
 */
NCURSES_EXPORT(unsigned)
_nc_pathlast(const char *path)
{
    const char *test = strrchr(path, '/');
#ifdef __EMX__
    if (test == 0)
	test = strrchr(path, '\\');
#endif
    if (test == 0)
	test = path;
    else
	test++;
    return (unsigned) (test - path);
}

NCURSES_EXPORT(char *)
_nc_basename(char *path)
{
    return path + _nc_pathlast(path);
}

NCURSES_EXPORT(int)
_nc_access(const char *path, int mode)
{
    int result;

    if (path == 0) {
	result = -1;
    } else if (access(path, mode) < 0) {
	if ((mode & W_OK) != 0
	    && errno == ENOENT
	    && strlen(path) < PATH_MAX) {
	    char head[PATH_MAX];
	    char *leaf;

	    _nc_STRCPY(head, path, sizeof(head));
	    leaf = _nc_basename(head);
	    if (leaf == 0)
		leaf = head;
	    *leaf = '\0';
	    if (head == leaf)
		_nc_STRCPY(head, ".", sizeof(head));

	    result = access(head, R_OK | W_OK | X_OK);
	} else {
	    result = -1;
	}
    } else {
	result = 0;
    }
    return result;
}

NCURSES_EXPORT(bool)
_nc_is_dir_path(const char *path)
{
    bool result = FALSE;
    struct stat sb;

    if (stat(path, &sb) == 0
	&& S_ISDIR(sb.st_mode)) {
	result = TRUE;
    }
    return result;
}

NCURSES_EXPORT(bool)
_nc_is_file_path(const char *path)
{
    bool result = FALSE;
    struct stat sb;

    if (stat(path, &sb) == 0
	&& S_ISREG(sb.st_mode)) {
	result = TRUE;
    }
    return result;
}

#ifndef USE_ROOT_ENVIRON
/*
 * Returns true if we allow application to use environment variables that are
 * used for searching lists of directories, etc.
 */
NCURSES_EXPORT(int)
_nc_env_access(void)
{
#if HAVE_ISSETUGID
    if (issetugid())
	return FALSE;
#elif HAVE_GETEUID && HAVE_GETEGID
    if (getuid() != geteuid()
	|| getgid() != getegid())
	return FALSE;
#endif
    /* ...finally, disallow root */
    return (getuid() != ROOT_UID) && (geteuid() != ROOT_UID);
}
#endif
