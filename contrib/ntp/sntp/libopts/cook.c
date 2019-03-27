/**
 * \file cook.c
 *
 *  This file contains the routines that deal with processing quoted strings
 *  into an internal format.
 *
 * @addtogroup autoopts
 * @{
 */
/*
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

/* = = = START-STATIC-FORWARD = = = */
static bool
contiguous_quote(char ** pps, char * pq, int * lnct_p);
/* = = = END-STATIC-FORWARD = = = */

/*=export_func  ao_string_cook_escape_char
 * private:
 *
 * what:  escape-process a string fragment
 * arg:   + char const * + pzScan  + points to character after the escape +
 * arg:   + char *       + pRes    + Where to put the result byte +
 * arg:   + unsigned int + nl_ch   + replacement char if scanned char is \n +
 *
 * ret-type: unsigned int
 * ret-desc: The number of bytes consumed processing the escaped character.
 *
 * doc:
 *
 *  This function converts "t" into "\t" and all your other favorite
 *  escapes, including numeric ones:  hex and ocatal, too.
 *  The returned result tells the caller how far to advance the
 *  scan pointer (passed in).  The default is to just pass through the
 *  escaped character and advance the scan by one.
 *
 *  Some applications need to keep an escaped newline, others need to
 *  suppress it.  This is accomplished by supplying a '\n' replacement
 *  character that is different from \n, if need be.  For example, use
 *  0x7F and never emit a 0x7F.
 *
 * err:  @code{NULL} is returned if the string is mal-formed.
=*/
unsigned int
ao_string_cook_escape_char(char const * pzIn, char * pRes, uint_t nl)
{
    unsigned int res = 1;

    switch (*pRes = *pzIn++) {
    case NUL:         /* NUL - end of input string */
        return 0;
    case '\r':
        if (*pzIn != NL)
            return 1;
        res++;
        /* FALLTHROUGH */
    case NL:        /* NL  - emit newline        */
        *pRes = (char)nl;
        return res;

    case 'a': *pRes = '\a'; break;
    case 'b': *pRes = '\b'; break;
    case 'f': *pRes = '\f'; break;
    case 'n': *pRes = NL;   break;
    case 'r': *pRes = '\r'; break;
    case 't': *pRes = '\t'; break;
    case 'v': *pRes = '\v'; break;

    case 'x':
    case 'X':         /* HEX Escape       */
        if (IS_HEX_DIGIT_CHAR(*pzIn))  {
            char z[4];
            unsigned int ct = 0;

            do  {
                z[ct] = pzIn[ct];
                if (++ct >= 2)
                    break;
            } while (IS_HEX_DIGIT_CHAR(pzIn[ct]));
            z[ct] = NUL;
            *pRes = (char)strtoul(z, NULL, 16);
            return ct + 1;
        }
        break;

    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7':
    {
        /*
         *  IF the character copied was an octal digit,
         *  THEN set the output character to an octal value.
         *  The 3 octal digit result might exceed 0xFF, so check it.
         */
        char z[4];
        unsigned long val;
        unsigned int  ct = 0;

        z[ct++] = *--pzIn;
        while (IS_OCT_DIGIT_CHAR(pzIn[ct])) {
            z[ct] = pzIn[ct];
            if (++ct >= 3)
                break;
        }

        z[ct] = NUL;
        val = strtoul(z, NULL, 8);
        if (val > 0xFF)
            val = 0xFF;
        *pRes = (char)val;
        return ct;
    }

    default: /* quoted character is result character */;
    }

    return res;
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *  A quoted string has been found.
 *  Find the end of it and compress any escape sequences.
 */
static bool
contiguous_quote(char ** pps, char * pq, int * lnct_p)
{
    char * ps = *pps + 1;

    for (;;) {
        while (IS_WHITESPACE_CHAR(*ps))
            if (*(ps++) == NL)
                (*lnct_p)++;

        /*
         *  IF the next character is a quote character,
         *  THEN we will concatenate the strings.
         */
        switch (*ps) {
        case '"':
        case '\'':
            *pq  = *(ps++);  /* assign new quote character and return */
            *pps = ps;
            return true;

        case '/':
            /*
             *  Allow for a comment embedded in the concatenated string.
             */
            switch (ps[1]) {
            default:
                *pps = NULL;
                return false;

            case '/':
                /*
                 *  Skip to end of line
                 */
                ps = strchr(ps, NL);
                if (ps == NULL) {
                    *pps = NULL;
                    return false;
                }
                break;

            case '*':
            {
                char * p = strstr( ps+2, "*/" );
                /*
                 *  Skip to terminating star slash
                 */
                if (p == NULL) {
                    *pps = NULL;
                    return false;
                }

                while (ps < p) {
                    if (*(ps++) == NL)
                        (*lnct_p)++;
                }

                ps = p + 2;
            }
            }
            continue;

        default:
            /*
             *  The next non-whitespace character is not a quote.
             *  The series of quoted strings has come to an end.
             */
            *pps = ps;
            return false;
        }
    }
}

/*=export_func  ao_string_cook
 * private:
 *
 * what:  concatenate and escape-process strings
 * arg:   + char * + pzScan  + The *MODIFIABLE* input buffer +
 * arg:   + int *  + lnct_p  + The (possibly NULL) pointer to a line count +
 *
 * ret-type: char *
 * ret-desc: The address of the text following the processed strings.
 *           The return value is NULL if the strings are ill-formed.
 *
 * doc:
 *
 *  A series of one or more quoted strings are concatenated together.
 *  If they are quoted with double quotes (@code{"}), then backslash
 *  escapes are processed per the C programming language.  If they are
 *  single quote strings, then the backslashes are honored only when they
 *  precede another backslash or a single quote character.
 *
 * err:  @code{NULL} is returned if the string(s) is/are mal-formed.
=*/
char *
ao_string_cook(char * pzScan, int * lnct_p)
{
    int   l = 0;
    char  q = *pzScan;

    /*
     *  It is a quoted string.  Process the escape sequence characters
     *  (in the set "abfnrtv") and make sure we find a closing quote.
     */
    char * pzD = pzScan++;
    char * pzS = pzScan;

    if (lnct_p == NULL)
        lnct_p = &l;

    for (;;) {
        /*
         *  IF the next character is the quote character, THEN we may end the
         *  string.  We end it unless the next non-blank character *after* the
         *  string happens to also be a quote.  If it is, then we will change
         *  our quote character to the new quote character and continue
         *  condensing text.
         */
        while (*pzS == q) {
            *pzD = NUL; /* This is probably the end of the line */
            if (! contiguous_quote(&pzS, &q, lnct_p))
                return pzS;
        }

        /*
         *  We are inside a quoted string.  Copy text.
         */
        switch (*(pzD++) = *(pzS++)) {
        case NUL:
            return NULL;

        case NL:
            (*lnct_p)++;
            break;

        case '\\':
            /*
             *  IF we are escaping a new line,
             *  THEN drop both the escape and the newline from
             *       the result string.
             */
            if (*pzS == NL) {
                pzS++;
                pzD--;
                (*lnct_p)++;
            }

            /*
             *  ELSE IF the quote character is '"' or '`',
             *  THEN we do the full escape character processing
             */
            else if (q != '\'') {
                unsigned int ct;
                ct = ao_string_cook_escape_char(pzS, pzD-1, (uint_t)NL);
                if (ct == 0)
                    return NULL;

                pzS += ct;
            }     /* if (q != '\'')                  */

            /*
             *  OTHERWISE, we only process "\\", "\'" and "\#" sequences.
             *  The latter only to easily hide preprocessing directives.
             */
            else switch (*pzS) {
            case '\\':
            case '\'':
            case '#':
                pzD[-1] = *pzS++;
            }
        }     /* switch (*(pzD++) = *(pzS++))    */
    }         /* for (;;)                        */
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/cook.c */
