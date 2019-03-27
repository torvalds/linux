/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */ 


/* Derived from The Open Group Base Specifications Issue 7, IEEE Std 1003.1-2008
 * as described in;
 *   http://pubs.opengroup.org/onlinepubs/9699919799/functions/fnmatch.html
 *
 * Filename pattern matches defined in section 2.13, "Pattern Matching Notation"
 * from chapter 2. "Shell Command Language"
 *   http://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_13
 * where; 1. A bracket expression starting with an unquoted <circumflex> '^' 
 * character CONTINUES to specify a non-matching list; 2. an explicit <period> '.' 
 * in a bracket expression matching list, e.g. "[.abc]" does NOT match a leading 
 * <period> in a filename; 3. a <left-square-bracket> '[' which does not introduce
 * a valid bracket expression is treated as an ordinary character; 4. a differing
 * number of consecutive slashes within pattern and string will NOT match;
 * 5. a trailing '\' in FNM_ESCAPE mode is treated as an ordinary '\' character.
 *
 * Bracket expansion defined in section 9.3.5, "RE Bracket Expression",
 * from chapter 9, "Regular Expressions"
 *   http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html#tag_09_03_05
 * with no support for collating symbols, equivalence class expressions or 
 * character class expressions.  A partial range expression with a leading 
 * hyphen following a valid range expression will match only the ordinary
 * <hyphen> and the ending character (e.g. "[a-m-z]" will match characters 
 * 'a' through 'm', a <hyphen> '-', or a 'z').
 *
 * NOTE: Only POSIX/C single byte locales are correctly supported at this time.
 * Notably, non-POSIX locales with FNM_CASEFOLD produce undefined results,
 * particularly in ranges of mixed case (e.g. "[A-z]") or spanning alpha and
 * nonalpha characters within a range.
 *
 * XXX comments below indicate porting required for multi-byte character sets
 * and non-POSIX locale collation orders; requires mbr* APIs to track shift
 * state of pattern and string (rewinding pattern and string repeatedly).
 *
 * Certain parts of the code assume 0x00-0x3F are unique with any MBCS (e.g.
 * UTF-8, SHIFT-JIS, etc).  Any implementation allowing '\' as an alternate
 * path delimiter must be aware that 0x5C is NOT unique within SHIFT-JIS.
 */

#include "apr_file_info.h"
#include "apr_fnmatch.h"
#include "apr_tables.h"
#include "apr_lib.h"
#include "apr_strings.h"
#include <string.h>
#if APR_HAVE_CTYPE_H
# include <ctype.h>
#endif


/* Most MBCS/collation/case issues handled here.  Wildcard '*' is not handled.
 * EOS '\0' and the FNM_PATHNAME '/' delimiters are not advanced over, 
 * however the "\/" sequence is advanced to '/'.
 *
 * Both pattern and string are **char to support pointer increment of arbitrary
 * multibyte characters for the given locale, in a later iteration of this code
 */
static APR_INLINE int fnmatch_ch(const char **pattern, const char **string, int flags)
{
    const char * const mismatch = *pattern;
    const int nocase = !!(flags & APR_FNM_CASE_BLIND);
    const int escape = !(flags & APR_FNM_NOESCAPE);
    const int slash = !!(flags & APR_FNM_PATHNAME);
    int result = APR_FNM_NOMATCH;
    const char *startch;
    int negate;

    if (**pattern == '[')
    {
        ++*pattern;

        /* Handle negation, either leading ! or ^ operators (never both) */
        negate = ((**pattern == '!') || (**pattern == '^'));
        if (negate)
            ++*pattern;

        /* ']' is an ordinary character at the start of the range pattern */
        if (**pattern == ']')
            goto leadingclosebrace;

        while (**pattern)
        {
            if (**pattern == ']') {
                ++*pattern;
                /* XXX: Fix for MBCS character width */
                ++*string;
                return (result ^ negate);
            }

            if (escape && (**pattern == '\\')) {
                ++*pattern;

                /* Patterns must be terminated with ']', not EOS */
                if (!**pattern)
                    break;
            }

            /* Patterns must be terminated with ']' not '/' */
            if (slash && (**pattern == '/'))
                break;

leadingclosebrace:
            /* Look at only well-formed range patterns; 
             * "x-]" is not allowed unless escaped ("x-\]")
             * XXX: Fix for locale/MBCS character width
             */
            if (((*pattern)[1] == '-') && ((*pattern)[2] != ']'))
            {
                startch = *pattern;
                *pattern += (escape && ((*pattern)[2] == '\\')) ? 3 : 2;

                /* NOT a properly balanced [expr] pattern, EOS terminated 
                 * or ranges containing a slash in FNM_PATHNAME mode pattern
                 * fall out to to the rewind and test '[' literal code path
                 */
                if (!**pattern || (slash && (**pattern == '/')))
                    break;

                /* XXX: handle locale/MBCS comparison, advance by MBCS char width */
                if ((**string >= *startch) && (**string <= **pattern))
                    result = 0;
                else if (nocase && (isupper(**string) || isupper(*startch)
                                                      || isupper(**pattern))
                            && (tolower(**string) >= tolower(*startch)) 
                            && (tolower(**string) <= tolower(**pattern)))
                    result = 0;

                ++*pattern;
                continue;
            }

            /* XXX: handle locale/MBCS comparison, advance by MBCS char width */
            if ((**string == **pattern))
                result = 0;
            else if (nocase && (isupper(**string) || isupper(**pattern))
                            && (tolower(**string) == tolower(**pattern)))
                result = 0;

            ++*pattern;
        }

        /* NOT a properly balanced [expr] pattern; Rewind
         * and reset result to test '[' literal
         */
        *pattern = mismatch;
        result = APR_FNM_NOMATCH;
    }
    else if (**pattern == '?') {
        /* Optimize '?' match before unescaping **pattern */
        if (!**string || (slash && (**string == '/')))
            return APR_FNM_NOMATCH;
        result = 0;
        goto fnmatch_ch_success;
    }
    else if (escape && (**pattern == '\\') && (*pattern)[1]) {
        ++*pattern;
    }

    /* XXX: handle locale/MBCS comparison, advance by the MBCS char width */
    if (**string == **pattern)
        result = 0;
    else if (nocase && (isupper(**string) || isupper(**pattern))
                    && (tolower(**string) == tolower(**pattern)))
        result = 0;

    /* Refuse to advance over trailing slash or nulls
     */
    if (!**string || !**pattern || (slash && ((**string == '/') || (**pattern == '/'))))
        return result;

fnmatch_ch_success:
    ++*pattern;
    ++*string;
    return result;
}


APR_DECLARE(int) apr_fnmatch(const char *pattern, const char *string, int flags)
{
    static const char dummystring[2] = {' ', 0};
    const int escape = !(flags & APR_FNM_NOESCAPE);
    const int slash = !!(flags & APR_FNM_PATHNAME);
    const char *strendseg;
    const char *dummyptr;
    const char *matchptr;
    int wild;
    /* For '*' wild processing only; surpress 'used before initialization'
     * warnings with dummy initialization values;
     */
    const char *strstartseg = NULL;
    const char *mismatch = NULL;
    int matchlen = 0;

    if (*pattern == '*')
        goto firstsegment;

    while (*pattern && *string)
    {
        /* Pre-decode "\/" which has no special significance, and
         * match balanced slashes, starting a new segment pattern
         */
        if (slash && escape && (*pattern == '\\') && (pattern[1] == '/'))
            ++pattern;
        if (slash && (*pattern == '/') && (*string == '/')) {
            ++pattern;
            ++string;
        }            

firstsegment:
        /* At the beginning of each segment, validate leading period behavior.
         */
        if ((flags & APR_FNM_PERIOD) && (*string == '.'))
        {
            if (*pattern == '.')
                ++pattern;
            else if (escape && (*pattern == '\\') && (pattern[1] == '.'))
                pattern += 2;
            else
                return APR_FNM_NOMATCH;
            ++string;
        }

        /* Determine the end of string segment
         *
         * Presumes '/' character is unique, not composite in any MBCS encoding
         */
        if (slash) {
            strendseg = strchr(string, '/');
            if (!strendseg)
                strendseg = strchr(string, '\0');
        }
        else {
            strendseg = strchr(string, '\0');
        }

        /* Allow pattern '*' to be consumed even with no remaining string to match
         */
        while (*pattern)
        {
            if ((string > strendseg)
                || ((string == strendseg) && (*pattern != '*')))
                break;

            if (slash && ((*pattern == '/')
                           || (escape && (*pattern == '\\')
                                      && (pattern[1] == '/'))))
                break;

            /* Reduce groups of '*' and '?' to n '?' matches
             * followed by one '*' test for simplicity
             */
            for (wild = 0; ((*pattern == '*') || (*pattern == '?')); ++pattern)
            {
                if (*pattern == '*') {
                    wild = 1;
                }
                else if (string < strendseg) {  /* && (*pattern == '?') */
                    /* XXX: Advance 1 char for MBCS locale */
                    ++string;
                }
                else {  /* (string >= strendseg) && (*pattern == '?') */
                    return APR_FNM_NOMATCH;
                }
            }

            if (wild)
            {
                strstartseg = string;
                mismatch = pattern;

                /* Count fixed (non '*') char matches remaining in pattern
                 * excluding '/' (or "\/") and '*'
                 */
                for (matchptr = pattern, matchlen = 0; 1; ++matchlen)
                {
                    if ((*matchptr == '\0') 
                        || (slash && ((*matchptr == '/')
                                      || (escape && (*matchptr == '\\')
                                                 && (matchptr[1] == '/')))))
                    {
                        /* Compare precisely this many trailing string chars,
                         * the resulting match needs no wildcard loop
                         */
                        /* XXX: Adjust for MBCS */
                        if (string + matchlen > strendseg)
                            return APR_FNM_NOMATCH;

                        string = strendseg - matchlen;
                        wild = 0;
                        break;
                    }

                    if (*matchptr == '*')
                    {
                        /* Ensure at least this many trailing string chars remain
                         * for the first comparison
                         */
                        /* XXX: Adjust for MBCS */
                        if (string + matchlen > strendseg)
                            return APR_FNM_NOMATCH;

                        /* Begin first wild comparison at the current position */
                        break;
                    }

                    /* Skip forward in pattern by a single character match
                     * Use a dummy fnmatch_ch() test to count one "[range]" escape
                     */ 
                    /* XXX: Adjust for MBCS */
                    if (escape && (*matchptr == '\\') && matchptr[1]) {
                        matchptr += 2;
                    }
                    else if (*matchptr == '[') {
                        dummyptr = dummystring;
                        fnmatch_ch(&matchptr, &dummyptr, flags);
                    }
                    else {
                        ++matchptr;
                    }
                }
            }

            /* Incrementally match string against the pattern
             */
            while (*pattern && (string < strendseg))
            {
                /* Success; begin a new wild pattern search
                 */
                if (*pattern == '*')
                    break;

                if (slash && ((*string == '/')
                              || (*pattern == '/')
                              || (escape && (*pattern == '\\')
                                         && (pattern[1] == '/'))))
                    break;

                /* Compare ch's (the pattern is advanced over "\/" to the '/',
                 * but slashes will mismatch, and are not consumed)
                 */
                if (!fnmatch_ch(&pattern, &string, flags))
                    continue;

                /* Failed to match, loop against next char offset of string segment 
                 * until not enough string chars remain to match the fixed pattern
                 */
                if (wild) {
                    /* XXX: Advance 1 char for MBCS locale */
                    string = ++strstartseg;
                    if (string + matchlen > strendseg)
                        return APR_FNM_NOMATCH;

                    pattern = mismatch;
                    continue;
                }
                else
                    return APR_FNM_NOMATCH;
            }
        }

        if (*string && !(slash && (*string == '/')))
            return APR_FNM_NOMATCH;

        if (*pattern && !(slash && ((*pattern == '/')
                                    || (escape && (*pattern == '\\')
                                               && (pattern[1] == '/')))))
            return APR_FNM_NOMATCH;
    }

    /* Where both pattern and string are at EOS, declare success
     */
    if (!*string && !*pattern)
        return 0;

    /* pattern didn't match to the end of string */
    return APR_FNM_NOMATCH;
}


/* This function is an Apache addition
 * return non-zero if pattern has any glob chars in it
 * @bug Function does not distinguish for FNM_PATHNAME mode, which renders
 * a false positive for test[/]this (which is not a range, but 
 * seperate test[ and ]this segments and no glob.)
 * @bug Function does not distinguish for non-FNM_ESCAPE mode.
 * @bug Function does not parse []] correctly
 * Solution may be to use fnmatch_ch() to walk the patterns?
 */
APR_DECLARE(int) apr_fnmatch_test(const char *pattern)
{
    int nesting;

    nesting = 0;
    while (*pattern) {
        switch (*pattern) {
        case '?':
        case '*':
            return 1;

        case '\\':
            if (*++pattern == '\0') {
                return 0;
            }
            break;

        case '[':         /* '[' is only a glob if it has a matching ']' */
            ++nesting;
            break;

        case ']':
            if (nesting) {
                return 1;
            }
            break;
        }
        ++pattern;    }
    return 0;
}


/* Find all files matching the specified pattern */
APR_DECLARE(apr_status_t) apr_match_glob(const char *pattern, 
                                         apr_array_header_t **result,
                                         apr_pool_t *p)
{
    apr_dir_t *dir;
    apr_finfo_t finfo;
    apr_status_t rv;
    char *path;

    /* XXX So, this is kind of bogus.  Basically, I need to strip any leading
     * directories off the pattern, but there is no portable way to do that.
     * So, for now we just find the last occurance of '/' and if that doesn't
     * return anything, then we look for '\'.  This means that we could
     * screw up on unix if the pattern is something like "foo\.*"  That '\'
     * isn't a directory delimiter, it is a part of the filename.  To fix this,
     * we really need apr_filepath_basename, which will be coming as soon as
     * I get to it.  rbb
     */
    char *idx = strrchr(pattern, '/');
    
    if (idx == NULL) {
        idx = strrchr(pattern, '\\');
    }
    if (idx == NULL) {
        path = ".";
    }
    else {
        path = apr_pstrndup(p, pattern, idx - pattern);
        pattern = idx + 1;
    }

    *result = apr_array_make(p, 0, sizeof(char *));
    rv = apr_dir_open(&dir, path, p);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    while (apr_dir_read(&finfo, APR_FINFO_NAME, dir) == APR_SUCCESS) {
        if (apr_fnmatch(pattern, finfo.name, 0) == APR_SUCCESS) {
            *(const char **)apr_array_push(*result) = apr_pstrdup(p, finfo.name);
        }
    }
    apr_dir_close(dir);
    return APR_SUCCESS;
}
