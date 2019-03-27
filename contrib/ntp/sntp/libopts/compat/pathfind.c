/*  -*- Mode: C -*-  */

/* pathfind.c --- find a FILE  MODE along PATH */

/* Author: Gary V Vaughan <gvaughan@oranda.demon.co.uk> */

/* Code: */

static char *
pathfind( char const * path,
          char const * fname,
          char const * mode );

#include "compat.h"
#ifndef HAVE_PATHFIND
#if defined(__windows__) && !defined(__CYGWIN__)
static char *
pathfind( char const * path,
          char const * fname,
          char const * mode )
{
    return strdup(fname);
}
#else

static char * make_absolute(char const * string, char const * dot_path);
static char * canonicalize_pathname(char * path);
static char * extract_colon_unit(char * dir, char const * string, int * p_index);

/**
 * local implementation of pathfind.
 * @param[in] path  colon separated list of directories
 * @param[in] fname the name we are hunting for
 * @param[in] mode  the required file mode
 * @returns an allocated string with the full path, or NULL
 */
static char *
pathfind( char const * path,
          char const * fname,
          char const * mode )
{
    int    p_index   = 0;
    int    mode_bits = 0;
    char * res_path  = NULL;
    char   zPath[ AG_PATH_MAX + 1 ];

    if (strchr( mode, 'r' )) mode_bits |= R_OK;
    if (strchr( mode, 'w' )) mode_bits |= W_OK;
    if (strchr( mode, 'x' )) mode_bits |= X_OK;

    /*
     *  FOR each non-null entry in the colon-separated path, DO ...
     */
    for (;;) {
        DIR  * dirP;
        char * colon_unit = extract_colon_unit( zPath, path, &p_index );

        if (colon_unit == NULL)
            break;

        dirP = opendir( colon_unit );

        /*
         *  IF the directory is inaccessable, THEN next directory
         */
        if (dirP == NULL)
            continue;

        for (;;) {
            struct dirent *entP = readdir( dirP );

            if (entP == (struct dirent *)NULL)
                break;

            /*
             *  IF the file name matches the one we are looking for, ...
             */
            if (strcmp(entP->d_name, fname) == 0) {
                char * abs_name = make_absolute(fname, colon_unit);

                /*
                 *  Make sure we can access it in the way we want
                 */
                if (access(abs_name, mode_bits) >= 0) {
                    /*
                     *  We can, so normalize the name and return it below
                     */
                    res_path = canonicalize_pathname(abs_name);
                }

                free(abs_name);
                break;
            }
        }

        closedir( dirP );

        if (res_path != NULL)
            break;
    }

    return res_path;
}

/*
 * Turn STRING  (a pathname) into an  absolute  pathname, assuming  that
 * DOT_PATH contains the symbolic location of  `.'.  This always returns
 * a new string, even if STRING was an absolute pathname to begin with.
 */
static char *
make_absolute( char const * string, char const * dot_path )
{
    char * result;
    int result_len;

    if (!dot_path || *string == '/') {
        result = strdup( string );
	if (result == NULL) {
	return NULL;    /* couldn't allocate memory    */
	}
    } else {
        if (dot_path && dot_path[0]) {
            result = malloc( 2 + strlen( dot_path ) + strlen( string ) );
		if (result == NULL) {
		return NULL;    /* couldn't allocate memory    */
		}		
            strcpy( result, dot_path );
            result_len = (int)strlen(result);
            if (result[result_len - 1] != '/') {
                result[result_len++] = '/';
                result[result_len] = '\0';
            }
        } else {
            result = malloc( 3 + strlen( string ) );
		if (result == NULL) {
		return NULL;    /* couldn't allocate memory    */
		}
            result[0] = '.'; result[1] = '/'; result[2] = '\0';
            result_len = 2;
        }

        strcpy( result + result_len, string );
    }

    return result;
}

/*
 * Canonicalize PATH, and return a  new path.  The new path differs from
 * PATH in that:
 *
 *    Multiple `/'s     are collapsed to a single `/'.
 *    Leading `./'s     are removed.
 *    Trailing `/.'s    are removed.
 *    Trailing `/'s     are removed.
 *    Non-leading `../'s and trailing `..'s are handled by removing
 *                    portions of the path.
 */
static char *
canonicalize_pathname( char *path )
{
    int i, start;
    char stub_char, *result;

    /* The result cannot be larger than the input PATH. */
    result = strdup( path );
	if (result == NULL) {
	return NULL;    /* couldn't allocate memory    */
	}
    stub_char = (*path == '/') ? '/' : '.';

    /* Walk along RESULT looking for things to compact. */
    i = 0;
    while (result[i]) {
        while (result[i] != '\0' && result[i] != '/')
            i++;

        start = i++;

        /* If we didn't find any  slashes, then there is nothing left to
         * do.
         */
        if (!result[start])
            break;

        /* Handle multiple `/'s in a row. */
        while (result[i] == '/')
            i++;

#if !defined (apollo)
        if ((start + 1) != i)
#else
        if ((start + 1) != i && (start != 0 || i != 2))
#endif /* apollo */
        {
            strcpy( result + start + 1, result + i );
            i = start + 1;
        }

        /* Handle backquoted `/'. */
        if (start > 0 && result[start - 1] == '\\')
            continue;

        /* Check for trailing `/', and `.' by itself. */
        if ((start && !result[i])
            || (result[i] == '.' && !result[i+1])) {
            result[--i] = '\0';
            break;
        }

        /* Check for `../', `./' or trailing `.' by itself. */
        if (result[i] == '.') {
            /* Handle `./'. */
            if (result[i + 1] == '/') {
                strcpy( result + i, result + i + 1 );
                i = (start < 0) ? 0 : start;
                continue;
            }

            /* Handle `../' or trailing `..' by itself. */
            if (result[i + 1] == '.' &&
                (result[i + 2] == '/' || !result[i + 2])) {
                while (--start > -1 && result[start] != '/')
                    ;
                strcpy( result + start + 1, result + i + 2 );
                i = (start < 0) ? 0 : start;
                continue;
            }
        }
    }

    if (!*result) {
        *result = stub_char;
        result[1] = '\0';
    }

    return result;
}

/*
 * Given a  string containing units of information separated  by colons,
 * return the next one  pointed to by (P_INDEX), or NULL if there are no
 * more.  Advance (P_INDEX) to the character after the colon.
 */
static char *
extract_colon_unit(char * pzDir, char const * string, int * p_index)
{
    char * pzDest = pzDir;
    int    ix     = *p_index;

    if (string == NULL)
        return NULL;

    if ((unsigned)ix >= strlen( string ))
        return NULL;

    {
        char const * pzSrc = string + ix;

        while (*pzSrc == ':')  pzSrc++;

        for (;;) {
            char ch = (*(pzDest++) = *(pzSrc++));
            switch (ch) {
            case ':':
                pzDest[-1] = NUL;
                /* FALLTHROUGH */
            case NUL:
                goto copy_done;
            }

            if ((unsigned long)(pzDest - pzDir) >= AG_PATH_MAX)
                break;
        } copy_done:;

        ix = (int)(pzSrc - string);
    }

    if (*pzDir == NUL)
        return NULL;

    *p_index = ix;
    return pzDir;
}
#endif /* __windows__ / __CYGWIN__ */
#endif /* HAVE_PATHFIND */

/*
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of compat/pathfind.c */
