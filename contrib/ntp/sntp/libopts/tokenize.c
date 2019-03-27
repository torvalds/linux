/** \file tokenize.c
 *
 *  Tokenize a string, accommodating quoted strings.
 *
 * @addtogroup autoopts
 * @{
 */
/*
 *  This file defines the string_tokenize interface
 *  This file is part of AutoOpts, a companion to AutoGen.
 *  AutoOpts is free software.
 *  AutoOpts is Copyright (C) 1992-2015 by Bruce Korb - all rights reserved
 *
 *  AutoOpts is available under any one of two licenses.  The license
 *  in use must be one of these two and the choice is under the control
 *  of the user of the license.
 *
 *   The GNU Lesser General Public License, version 3 or later
 *      See the files "COPYING.lgplv3" and "COPYING.gplv3"
 *
 *   The Modified Berkeley Software Distribution License
 *      See the file "COPYING.mbsd"
 *
 *  These files have the following sha256 sums:
 *
 *  8584710e9b04216a394078dc156b781d0b47e1729104d666658aecef8ee32e95  COPYING.gplv3
 *  4379e7444a0e2ce2b12dd6f5a52a27a4d02d39d247901d3285c88cf0d37f477b  COPYING.lgplv3
 *  13aa749a5b0a454917a944ed8fffc530b784f5ead522b1aacaf4ec8aa55a6239  COPYING.mbsd
 */

#include <errno.h>
#include <stdlib.h>

#define cc_t   const unsigned char
#define ch_t   unsigned char

/* = = = START-STATIC-FORWARD = = = */
static void
copy_cooked(ch_t ** ppDest, char const ** ppSrc);

static void
copy_raw(ch_t ** ppDest, char const ** ppSrc);

static token_list_t *
alloc_token_list(char const * str);
/* = = = END-STATIC-FORWARD = = = */

static void
copy_cooked(ch_t ** ppDest, char const ** ppSrc)
{
    ch_t * pDest = (ch_t *)*ppDest;
    const ch_t * pSrc  = (const ch_t *)(*ppSrc + 1);

    for (;;) {
        ch_t ch = *(pSrc++);
        switch (ch) {
        case NUL:   *ppSrc = NULL; return;
        case '"':   goto done;
        case '\\':
            pSrc += ao_string_cook_escape_char((const char *)pSrc, (char *)&ch, 0x7F);
            if (ch == 0x7F)
                break;
            /* FALLTHROUGH */

        default:
            *(pDest++) = ch;
        }
    }

 done:
    *ppDest = (ch_t *)pDest; /* next spot for storing character */
    *ppSrc  = (char const *)pSrc;  /* char following closing quote    */
}


static void
copy_raw(ch_t ** ppDest, char const ** ppSrc)
{
    ch_t * pDest = *ppDest;
    cc_t * pSrc  = (cc_t *) (*ppSrc + 1);

    for (;;) {
        ch_t ch = *(pSrc++);
        switch (ch) {
        case NUL:   *ppSrc = NULL; return;
        case '\'':  goto done;
        case '\\':
            /*
             *  *Four* escapes are handled:  newline removal, escape char
             *  quoting and apostrophe quoting
             */
            switch (*pSrc) {
            case NUL:   *ppSrc = NULL; return;
            case '\r':
                if (*(++pSrc) == NL)
                    ++pSrc;
                continue;

            case NL:
                ++pSrc;
                continue;

            case '\'':
                ch = '\'';
                /* FALLTHROUGH */

            case '\\':
                ++pSrc;
                break;
            }
            /* FALLTHROUGH */

        default:
            *(pDest++) = ch;
        }
    }

 done:
    *ppDest = pDest; /* next spot for storing character */
    *ppSrc  = (char const *) pSrc;  /* char following closing quote    */
}

static token_list_t *
alloc_token_list(char const * str)
{
    token_list_t * res;

    int max_token_ct = 2; /* allow for trailing NULL pointer & NUL on string */

    if (str == NULL) goto enoent_res;

    /*
     *  Trim leading white space.  Use "ENOENT" and a NULL return to indicate
     *  an empty string was passed.
     */
    str = SPN_WHITESPACE_CHARS(str);
    if (*str == NUL)  goto enoent_res;

    /*
     *  Take an approximate count of tokens.  If no quoted strings are used,
     *  it will be accurate.  If quoted strings are used, it will be a little
     *  high and we'll squander the space for a few extra pointers.
     */
    {
        char const * pz = str;

        do {
            max_token_ct++;
            pz = BRK_WHITESPACE_CHARS(pz+1);
            pz = SPN_WHITESPACE_CHARS(pz);
        } while (*pz != NUL);

        res = malloc(sizeof(*res) + (size_t)(pz - str)
                     + ((size_t)max_token_ct * sizeof(ch_t *)));
    }

    if (res == NULL)
        errno = ENOMEM;
    else res->tkn_list[0] = (ch_t *)(res->tkn_list + (max_token_ct - 1));

    return res;

    enoent_res:

    errno = ENOENT;
    return NULL;
}

/*=export_func ao_string_tokenize
 *
 * what: tokenize an input string
 *
 * arg:  + char const * + string + string to be tokenized +
 *
 * ret_type:  token_list_t *
 * ret_desc:  pointer to a structure that lists each token
 *
 * doc:
 *
 * This function will convert one input string into a list of strings.
 * The list of strings is derived by separating the input based on
 * white space separation.  However, if the input contains either single
 * or double quote characters, then the text after that character up to
 * a matching quote will become the string in the list.
 *
 *  The returned pointer should be deallocated with @code{free(3C)} when
 *  are done using the data.  The data are placed in a single block of
 *  allocated memory.  Do not deallocate individual token/strings.
 *
 *  The structure pointed to will contain at least these two fields:
 *  @table @samp
 *  @item tkn_ct
 *  The number of tokens found in the input string.
 *  @item tok_list
 *  An array of @code{tkn_ct + 1} pointers to substring tokens, with
 *  the last pointer set to NULL.
 *  @end table
 *
 * There are two types of quoted strings: single quoted (@code{'}) and
 * double quoted (@code{"}).  Singly quoted strings are fairly raw in that
 * escape characters (@code{\\}) are simply another character, except when
 * preceding the following characters:
 * @example
 * @code{\\}  double backslashes reduce to one
 * @code{'}   incorporates the single quote into the string
 * @code{\n}  suppresses both the backslash and newline character
 * @end example
 *
 * Double quote strings are formed according to the rules of string
 * constants in ANSI-C programs.
 *
 * example:
 * @example
 *    #include <stdlib.h>
 *    int ix;
 *    token_list_t * ptl = ao_string_tokenize(some_string)
 *    for (ix = 0; ix < ptl->tkn_ct; ix++)
 *       do_something_with_tkn(ptl->tkn_list[ix]);
 *    free(ptl);
 * @end example
 * Note that everything is freed with the one call to @code{free(3C)}.
 *
 * err:
 *  NULL is returned and @code{errno} will be set to indicate the problem:
 *  @itemize @bullet
 *  @item
 *  @code{EINVAL} - There was an unterminated quoted string.
 *  @item
 *  @code{ENOENT} - The input string was empty.
 *  @item
 *  @code{ENOMEM} - There is not enough memory.
 *  @end itemize
=*/
token_list_t *
ao_string_tokenize(char const * str)
{
    token_list_t * res = alloc_token_list(str);
    ch_t * pzDest;

    /*
     *  Now copy each token into the output buffer.
     */
    if (res == NULL)
        return res;

    pzDest = (ch_t *)(res->tkn_list[0]);
    res->tkn_ct  = 0;

    do  {
        res->tkn_list[ res->tkn_ct++ ] = pzDest;
        for (;;) {
            int ch = (ch_t)*str;
            if (IS_WHITESPACE_CHAR(ch)) {
            found_white_space:
                str = SPN_WHITESPACE_CHARS(str+1);
                break;
            }

            switch (ch) {
            case '"':
                copy_cooked(&pzDest, &str);
                if (str == NULL) {
                    free(res);
                    errno = EINVAL;
                    return NULL;
                }
                if (IS_WHITESPACE_CHAR(*str))
                    goto found_white_space;
                break;

            case '\'':
                copy_raw(&pzDest, &str);
                if (str == NULL) {
                    free(res);
                    errno = EINVAL;
                    return NULL;
                }
                if (IS_WHITESPACE_CHAR(*str))
                    goto found_white_space;
                break;

            case NUL:
                goto copy_done;

            default:
                str++;
                *(pzDest++) = (unsigned char)ch;
            }
        } copy_done:;

        /*
         * NUL terminate the last token and see if we have any more tokens.
         */
        *(pzDest++) = NUL;
    } while (*str != NUL);

    res->tkn_list[ res->tkn_ct ] = NULL;

    return res;
}

#ifdef TEST
#include <stdio.h>
#include <string.h>

int
main(int argc, char ** argv)
{
    if (argc == 1) {
        printf("USAGE:  %s arg [ ... ]\n", *argv);
        return 1;
    }
    while (--argc > 0) {
        char * arg = *(++argv);
        token_list_t * p = ao_string_tokenize(arg);
        if (p == NULL) {
            printf("Parsing string ``%s'' failed:\n\terrno %d (%s)\n",
                   arg, errno, strerror(errno));
        } else {
            int ix = 0;
            printf("Parsed string ``%s''\ninto %d tokens:\n", arg, p->tkn_ct);
            do {
                printf(" %3d:  ``%s''\n", ix+1, p->tkn_list[ix]);
            } while (++ix < p->tkn_ct);
            free(p);
        }
    }
    return 0;
}
#endif

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/tokenize.c */
