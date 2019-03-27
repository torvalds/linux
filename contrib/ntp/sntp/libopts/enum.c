
/**
 * \file enumeration.c
 *
 *  Handle options with enumeration names and bit mask bit names
 *  for their arguments.
 *
 * @addtogroup autoopts
 * @{
 */
/*
 *  This routine will run run-on options through a pager so the
 *  user may examine, print or edit them at their leisure.
 *
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
static void
enum_err(tOptions * pOpts, tOptDesc * pOD,
         char const * const * paz_names, int name_ct);

static uintptr_t
find_name(char const * name, tOptions * pOpts, tOptDesc * pOD,
          char const * const *  paz_names, unsigned int name_ct);

static void
set_memb_shell(tOptions * pOpts, tOptDesc * pOD, char const * const * paz_names,
               unsigned int name_ct);

static void
set_memb_names(tOptions * opts, tOptDesc * od, char const * const * nm_list,
               unsigned int nm_ct);

static uintptr_t
check_membership_start(tOptDesc * od, char const ** argp, bool * invert);

static uintptr_t
find_member_bit(tOptions * opts, tOptDesc * od, char const * pz, int len,
                char const * const * nm_list, unsigned int nm_ct);
/* = = = END-STATIC-FORWARD = = = */

static void
enum_err(tOptions * pOpts, tOptDesc * pOD,
         char const * const * paz_names, int name_ct)
{
    size_t max_len = 0;
    size_t ttl_len = 0;
    int    ct_down = name_ct;
    int    hidden  = 0;

    /*
     *  A real "pOpts" pointer means someone messed up.  Give a real error.
     */
    if (pOpts > OPTPROC_EMIT_LIMIT)
        fprintf(option_usage_fp, pz_enum_err_fmt, pOpts->pzProgName,
                pOD->optArg.argString, pOD->pz_Name);

    fprintf(option_usage_fp, zValidKeys, pOD->pz_Name);

    /*
     *  If the first name starts with this funny character, then we have
     *  a first value with an unspellable name.  You cannot specify it.
     *  So, we don't list it either.
     */
    if (**paz_names == 0x7F) {
        paz_names++;
        hidden  = 1;
        ct_down = --name_ct;
    }

    /*
     *  Figure out the maximum length of any name, plus the total length
     *  of all the names.
     */
    {
        char const * const * paz = paz_names;

        do  {
            size_t len = strlen(*(paz++)) + 1;
            if (len > max_len)
                max_len = len;
            ttl_len += len;
        } while (--ct_down > 0);

        ct_down = name_ct;
    }

    /*
     *  IF any one entry is about 1/2 line or longer, print one per line
     */
    if (max_len > 35) {
        do  {
            fprintf(option_usage_fp, ENUM_ERR_LINE, *(paz_names++));
        } while (--ct_down > 0);
    }

    /*
     *  ELSE IF they all fit on one line, then do so.
     */
    else if (ttl_len < 76) {
        fputc(' ', option_usage_fp);
        do  {
            fputc(' ', option_usage_fp);
            fputs(*(paz_names++), option_usage_fp);
        } while (--ct_down > 0);
        fputc(NL, option_usage_fp);
    }

    /*
     *  Otherwise, columnize the output
     */
    else {
        unsigned int ent_no = 0;
        char  zFmt[16];  /* format for all-but-last entries on a line */

        sprintf(zFmt, ENUM_ERR_WIDTH, (int)max_len);
        max_len = 78 / max_len; /* max_len is now max entries on a line */
        fputs(TWO_SPACES_STR, option_usage_fp);

        /*
         *  Loop through all but the last entry
         */
        ct_down = name_ct;
        while (--ct_down > 0) {
            if (++ent_no == max_len) {
                /*
                 *  Last entry on a line.  Start next line, too.
                 */
                fprintf(option_usage_fp, NLSTR_SPACE_FMT, *(paz_names++));
                ent_no = 0;
            }

            else
                fprintf(option_usage_fp, zFmt, *(paz_names++) );
        }
        fprintf(option_usage_fp, NLSTR_FMT, *paz_names);
    }

    if (pOpts > OPTPROC_EMIT_LIMIT) {
        fprintf(option_usage_fp, zIntRange, hidden, name_ct - 1 + hidden);

        (*(pOpts->pUsageProc))(pOpts, EXIT_FAILURE);
        /* NOTREACHED */
    }

    if (OPTST_GET_ARGTYPE(pOD->fOptState) == OPARG_TYPE_MEMBERSHIP) {
        fprintf(option_usage_fp, zLowerBits, name_ct);
        fputs(zSetMemberSettings, option_usage_fp);
    } else {
        fprintf(option_usage_fp, zIntRange, hidden, name_ct - 1 + hidden);
    }
}

/**
 * Convert a name or number into a binary number.
 * "~0" and "-1" will be converted to the largest value in the enumeration.
 *
 * @param name       the keyword name (number) to convert
 * @param pOpts      the program's option descriptor
 * @param pOD        the option descriptor for this option
 * @param paz_names  the list of keywords for this option
 * @param name_ct    the count of keywords
 */
static uintptr_t
find_name(char const * name, tOptions * pOpts, tOptDesc * pOD,
          char const * const *  paz_names, unsigned int name_ct)
{
    /*
     *  Return the matching index as a pointer sized integer.
     *  The result gets stashed in a char * pointer.
     */
    uintptr_t   res = name_ct;
    size_t      len = strlen(name);
    uintptr_t   idx;

    if (IS_DEC_DIGIT_CHAR(*name)) {
        char * pz;
        unsigned long val = strtoul(name, &pz, 0);
        if ((*pz == NUL) && (val < name_ct))
            return (uintptr_t)val;
        pz_enum_err_fmt = znum_too_large;
        option_usage_fp = stderr;
        enum_err(pOpts, pOD, paz_names, (int)name_ct);
        return name_ct;
    }

    if (IS_INVERSION_CHAR(*name) && (name[2] == NUL)) {
        if (  ((name[0] == '~') && (name[1] == '0'))
           || ((name[0] == '-') && (name[1] == '1')))
        return (uintptr_t)(name_ct - 1);
        goto oops;
    }

    /*
     *  Look for an exact match, but remember any partial matches.
     *  Multiple partial matches means we have an ambiguous match.
     */
    for (idx = 0; idx < name_ct; idx++) {
        if (strncmp(paz_names[idx], name, len) == 0) {
            if (paz_names[idx][len] == NUL)
                return idx;  /* full match */

            if (res == name_ct)
                res = idx; /* save partial match */
            else
                res = (uintptr_t)~0;  /* may yet find full match */
        }
    }

    if (res < name_ct)
        return res; /* partial match */

 oops:

    pz_enum_err_fmt = (res == name_ct) ? zNoKey : zambiguous_key;
    option_usage_fp = stderr;
    enum_err(pOpts, pOD, paz_names, (int)name_ct);
    return name_ct;
}


/*=export_func  optionKeywordName
 * what:  Convert between enumeration values and strings
 * private:
 *
 * arg:   tOptDesc *,    pOD,       enumeration option description
 * arg:   unsigned int,  enum_val,  the enumeration value to map
 *
 * ret_type:  char const *
 * ret_desc:  the enumeration name from const memory
 *
 * doc:   This converts an enumeration value into the matching string.
=*/
char const *
optionKeywordName(tOptDesc * pOD, unsigned int enum_val)
{
    tOptDesc od = { 0 };
    od.optArg.argEnum = enum_val;

    (*(pOD->pOptProc))(OPTPROC_RETURN_VALNAME, &od );
    return od.optArg.argString;
}


/*=export_func  optionEnumerationVal
 * what:  Convert from a string to an enumeration value
 * private:
 *
 * arg:   tOptions *,    pOpts,     the program options descriptor
 * arg:   tOptDesc *,    pOD,       enumeration option description
 * arg:   char const * const *,  paz_names, list of enumeration names
 * arg:   unsigned int,  name_ct,   number of names in list
 *
 * ret_type:  uintptr_t
 * ret_desc:  the enumeration value
 *
 * doc:   This converts the optArg.argString string from the option description
 *        into the index corresponding to an entry in the name list.
 *        This will match the generated enumeration value.
 *        Full matches are always accepted.  Partial matches are accepted
 *        if there is only one partial match.
=*/
uintptr_t
optionEnumerationVal(tOptions * pOpts, tOptDesc * pOD,
                     char const * const * paz_names, unsigned int name_ct)
{
    uintptr_t res = 0UL;

    /*
     *  IF the program option descriptor pointer is invalid,
     *  then it is some sort of special request.
     */
    switch ((uintptr_t)pOpts) {
    case (uintptr_t)OPTPROC_EMIT_USAGE:
        /*
         *  print the list of enumeration names.
         */
        enum_err(pOpts, pOD, paz_names, (int)name_ct);
        break;

    case (uintptr_t)OPTPROC_EMIT_SHELL:
    {
        unsigned int ix = (unsigned int)pOD->optArg.argEnum;
        /*
         *  print the name string.
         */
        if (ix >= name_ct)
            printf(INVALID_FMT, ix);
        else
            fputs(paz_names[ ix ], stdout);

        break;
    }

    case (uintptr_t)OPTPROC_RETURN_VALNAME:
    {
        unsigned int ix = (unsigned int)pOD->optArg.argEnum;
        /*
         *  Replace the enumeration value with the name string.
         */
        if (ix >= name_ct)
            return (uintptr_t)INVALID_STR;

        pOD->optArg.argString = paz_names[ix];
        break;
    }

    default:
        if ((pOD->fOptState & OPTST_RESET) != 0)
            break;

        res = find_name(pOD->optArg.argString, pOpts, pOD, paz_names, name_ct);

        if (pOD->fOptState & OPTST_ALLOC_ARG) {
            AGFREE(pOD->optArg.argString);
            pOD->fOptState &= ~OPTST_ALLOC_ARG;
            pOD->optArg.argString = NULL;
        }
    }

    return res;
}

static void
set_memb_shell(tOptions * pOpts, tOptDesc * pOD, char const * const * paz_names,
               unsigned int name_ct)
{
    /*
     *  print the name string.
     */
    unsigned int ix =  0;
    uintptr_t  bits = (uintptr_t)pOD->optCookie;
    size_t     len  = 0;

    (void)pOpts;
    bits &= ((uintptr_t)1 << (uintptr_t)name_ct) - (uintptr_t)1;

    while (bits != 0) {
        if (bits & 1) {
            if (len++ > 0) fputs(OR_STR, stdout);
            fputs(paz_names[ix], stdout);
        }
        if (++ix >= name_ct) break;
        bits >>= 1;
    }
}

static void
set_memb_names(tOptions * opts, tOptDesc * od, char const * const * nm_list,
               unsigned int nm_ct)
{
    char *     pz;
    uintptr_t  mask = (1UL << (uintptr_t)nm_ct) - 1UL;
    uintptr_t  bits = (uintptr_t)od->optCookie & mask;
    unsigned int ix = 0;
    size_t     len  = 1;

    /*
     *  Replace the enumeration value with the name string.
     *  First, determine the needed length, then allocate and fill in.
     */
    while (bits != 0) {
        if (bits & 1)
            len += strlen(nm_list[ix]) + PLUS_STR_LEN + 1;
        if (++ix >= nm_ct) break;
        bits >>= 1;
    }

    od->optArg.argString = pz = AGALOC(len, "enum");
    bits = (uintptr_t)od->optCookie & mask;
    if (bits == 0) {
        *pz = NUL;
        return;
    }

    for (ix = 0; ; ix++) {
        size_t nln;
        int    doit = bits & 1;

        bits >>= 1;
        if (doit == 0)
            continue;

        nln = strlen(nm_list[ix]);
        memcpy(pz, nm_list[ix], nln);
        pz += nln;
        if (bits == 0)
            break;
        memcpy(pz, PLUS_STR, PLUS_STR_LEN);
        pz += PLUS_STR_LEN;
    }
    *pz = NUL;
    (void)opts;
}

/**
 * Check membership start conditions.  An equal character (@samp{=}) says to
 * clear the result and not carry over any residual value.  A carat
 * (@samp{^}), which may follow the equal character, says to invert the
 * result.  The scanning pointer is advanced past these characters and any
 * leading white space.  Invalid sequences are indicated by setting the
 * scanning pointer to NULL.
 *
 * @param od      the set membership option description
 * @param argp    a pointer to the string scanning pointer
 * @param invert  a pointer to the boolean inversion indicator
 *
 * @returns either zero or the original value for the optCookie.
 */
static uintptr_t
check_membership_start(tOptDesc * od, char const ** argp, bool * invert)
{
    uintptr_t    res = (uintptr_t)od->optCookie;
    char const * arg = SPN_WHITESPACE_CHARS(od->optArg.argString);
    if ((arg == NULL) || (*arg == NUL))
        goto member_start_fail;

    *invert = false;

    switch (*arg) {
    case '=':
        res = 0UL;
        arg = SPN_WHITESPACE_CHARS(arg + 1);
        switch (*arg) {
        case '=': case ',':
            goto member_start_fail;
        case '^':
            goto inversion;
        default:
            break;
        }
        break;

    case '^':
    inversion:
        *invert = true;
        arg = SPN_WHITESPACE_CHARS(arg + 1);
        if (*arg != ',')
            break;
        /* FALLTHROUGH */

    case ',':
        goto member_start_fail;

    default:
        break;
    }

    *argp = arg;
    return res;

member_start_fail:
    *argp = NULL;
    return 0UL;
}

/**
 * convert a name to a bit.  Look up a name string to get a bit number
 * and shift the value "1" left that number of bits.
 *
 * @param opts      program options descriptor
 * @param od        the set membership option description
 * @param pz        address of the start of the bit name
 * @param nm_list   the list of names for this option
 * @param nm_ct     the number of entries in this list
 *
 * @returns 0UL on error, other an unsigned long with the correct bit set.
 */
static uintptr_t
find_member_bit(tOptions * opts, tOptDesc * od, char const * pz, int len,
                char const * const * nm_list, unsigned int nm_ct)
{
    char nm_buf[ AO_NAME_SIZE ];

    memcpy(nm_buf, pz, len);
    nm_buf[len] = NUL;

    {
        unsigned int shift_ct = (unsigned int)
            find_name(nm_buf, opts, od, nm_list, nm_ct);
        if (shift_ct >= nm_ct)
            return 0UL;

        return (uintptr_t)1U << shift_ct;
    }
}

/*=export_func  optionMemberList
 * what:  Get the list of members of a bit mask set
 *
 * arg:   tOptDesc *,  od,   the set membership option description
 *
 * ret_type: char *
 * ret_desc: the names of the set bits
 *
 * doc:   This converts the OPT_VALUE_name mask value to a allocated string.
 *        It is the caller's responsibility to free the string.
=*/
char *
optionMemberList(tOptDesc * od)
{
    uintptr_t    sv = od->optArg.argIntptr;
    char * res;
    (*(od->pOptProc))(OPTPROC_RETURN_VALNAME, od);
    res = VOIDP(od->optArg.argString);
    od->optArg.argIntptr = sv;
    return res;
}

/*=export_func  optionSetMembers
 * what:  Convert between bit flag values and strings
 * private:
 *
 * arg:   tOptions *,     opts,     the program options descriptor
 * arg:   tOptDesc *,     od,       the set membership option description
 * arg:   char const * const *,
 *                       nm_list,  list of enumeration names
 * arg:   unsigned int,  nm_ct,    number of names in list
 *
 * doc:   This converts the optArg.argString string from the option description
 *        into the index corresponding to an entry in the name list.
 *        This will match the generated enumeration value.
 *        Full matches are always accepted.  Partial matches are accepted
 *        if there is only one partial match.
=*/
void
optionSetMembers(tOptions * opts, tOptDesc * od,
                 char const * const * nm_list, unsigned int nm_ct)
{
    /*
     *  IF the program option descriptor pointer is invalid,
     *  then it is some sort of special request.
     */
    switch ((uintptr_t)opts) {
    case (uintptr_t)OPTPROC_EMIT_USAGE:
        enum_err(OPTPROC_EMIT_USAGE, od, nm_list, nm_ct);
        return;

    case (uintptr_t)OPTPROC_EMIT_SHELL:
        set_memb_shell(opts, od, nm_list, nm_ct);
        return;

    case (uintptr_t)OPTPROC_RETURN_VALNAME:
        set_memb_names(opts, od, nm_list, nm_ct);
        return;

    default:
        break;
    }

    if ((od->fOptState & OPTST_RESET) != 0)
        return;

    {
        char const * arg;
        bool         invert;
        uintptr_t    res = check_membership_start(od, &arg, &invert);
        if (arg == NULL)
            goto fail_return;

        while (*arg != NUL) {
            bool inv_val = false;
            int  len;

            switch (*arg) {
            case ',':
                arg = SPN_WHITESPACE_CHARS(arg+1);
                if ((*arg == ',') || (*arg == '|'))
                    goto fail_return;
                continue;

            case '-':
            case '!':
                inv_val = true;
                /* FALLTHROUGH */

            case '+':
            case '|':
                arg = SPN_WHITESPACE_CHARS(arg+1);
            }

            len = (int)(BRK_SET_SEPARATOR_CHARS(arg) - arg);
            if (len == 0)
                break;

            if ((len == 3) && (strncmp(arg, zAll, 3) == 0)) {
                if (inv_val)
                     res = 0;
                else res = ~0UL;
            }
            else if ((len == 4) && (strncmp(arg, zNone, 4) == 0)) {
                if (! inv_val)
                    res = 0;
            }
            else do {
                char *    pz;
                uintptr_t bit = strtoul(arg, &pz, 0);

                if (pz != arg + len) {
                    bit = find_member_bit(opts, od, pz, len, nm_list, nm_ct);
                    if (bit == 0UL)
                        goto fail_return;
                }
                if (inv_val)
                     res &= ~bit;
                else res |= bit;
            } while (false);

            arg = SPN_WHITESPACE_CHARS(arg + len);
        }

        if (invert)
            res ^= ~0UL;

        if (nm_ct < (8 * sizeof(uintptr_t)))
            res &= (1UL << nm_ct) - 1UL;

        od->optCookie = VOIDP(res);
    }
    return;

fail_return:
    od->optCookie = VOIDP(0);
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/enum.c */
