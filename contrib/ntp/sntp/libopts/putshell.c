
/**
 * \file putshell.c
 *
 *  This module will interpret the options set in the tOptions
 *  structure and print them to standard out in a fashion that
 *  will allow them to be interpreted by the Bourne or Korn shells.
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
static size_t
string_size(char const * scan, size_t nl_len);

static char const *
print_quoted_apostrophes(char const * str);

static void
print_quot_str(char const * str);

static void
print_enumeration(tOptions * pOpts, tOptDesc * pOD);

static void
print_membership(tOptions * pOpts, tOptDesc * pOD);

static void
print_stacked_arg(tOptions * pOpts, tOptDesc * pOD);

static void
print_reordering(tOptions * opts);
/* = = = END-STATIC-FORWARD = = = */

/**
 * Count the number of bytes required to represent a string as a
 * compilable string.
 *
 * @param[in] scan    the text to be rewritten as a C program text string.
 * @param[in] nl_len  the number of bytes used for each embedded newline.
 *
 * @returns the count, including the terminating NUL byte.
 */
static size_t
string_size(char const * scan, size_t nl_len)
{
    /*
     *  Start by counting the start and end quotes, plus the NUL.
     */
    size_t res_ln = 3;

    for (;;) {
        char ch = *(scan++);
        if ((ch >= ' ') && (ch <= '~')) {

            /*
             * a backslash allowance for double quotes and baskslashes
             */
            res_ln += ((ch == '"') || (ch == '\\')) ? 2 : 1;
        }

        /*
         *  When not a normal character, then count the characters
         *  required to represent whatever it is.
         */
        else switch (ch) {
        case NUL:
            return res_ln;

        case NL:
            res_ln += nl_len;
            break;

        case HT:
        case BEL:
        case BS:
        case FF:
        case CR:
        case VT:
            res_ln += 2;
            break;

        default:
            res_ln += 4; /* text len for \xNN */
        }
    }
}

/*=export_func  optionQuoteString
 * private:
 *
 * what:  Print a string as quoted text suitable for a C compiler.
 * arg:   + char const * + text  + a block of text to quote +
 * arg:   + char const * + nl    + line splice text         +
 *
 * ret_type:  char const *
 * ret_desc:  the allocated input string as a quoted string
 *
 * doc:
 *  This is for internal use by autogen and autoopts.
 *  It takes an input string and produces text the C compiler can process
 *  to produce an exact copy of the original string.
 *  The caller must deallocate the result.  Standard C strings and
 *  K&R strings are distinguished by the "nl" string.
=*/
char const *
optionQuoteString(char const * text, char const * nl)
{
    size_t   nl_len = strlen(nl);
    char *   out;
    char *   res = out = AGALOC(string_size(text, nl_len), "quot str");
    *(out++) = '"';

    for (;;) {
        unsigned char ch = (unsigned char)*text;
        if ((ch >= ' ') && (ch <= '~')) {
            if ((ch == '"') || (ch == '\\'))
                /*
                 *  We must escape these characters in the output string
                 */
                *(out++) = '\\';
            *(out++) = (char)ch;

        } else switch (ch) {
#       define   add_esc_ch(_ch)  { *(out++) = '\\'; *(out++) = (_ch); }
        case BEL: add_esc_ch('a'); break;
        case BS:  add_esc_ch('b'); break;
        case HT:  add_esc_ch('t'); break;
        case VT:  add_esc_ch('v'); break;
        case FF:  add_esc_ch('f'); break;
        case CR:  add_esc_ch('r'); break;

        case LF:
            /*
             *  Place contiguous new-lines on a single line.
             *  The current character is a NL, check the next one.
             */
            while (*++text == NL)
                add_esc_ch('n');

            /*
             *  Insert a splice before starting next line
             */
            if (*text != NUL) {
                memcpy(out, nl, nl_len);
                out += nl_len;

                continue; /* text is already at the next character */
            }

            add_esc_ch('n');
            /* FALLTHROUGH */

        case NUL:
            /*
             *  End of string.  Terminate the quoted output.  If necessary,
             *  deallocate the text string.  Return the scan resumption point.
             */
            *(out++) = '"';
            *out = NUL;
            return res;

        default:
            /*
             *  sprintf is safe here, because we already computed
             *  the amount of space we will be using.
             */
            sprintf(out, MK_STR_OCT_FMT, ch);
            out += 4;
        }

        text++;
#       undef add_esc_ch
    }
}

/**
 *  Print out escaped apostorophes.
 *
 *  @param[in] str  the apostrophies to print
 */
static char const *
print_quoted_apostrophes(char const * str)
{
    while (*str == APOSTROPHE) {
        fputs(QUOT_APOS, stdout);
        str++;
    }
    return str;
}

/**
 *  Print a single quote (apostrophe quoted) string.
 *  Other than somersaults for apostrophes, nothing else needs quoting.
 *
 *  @param[in] str  the string to print
 */
static void
print_quot_str(char const * str)
{
    /*
     *  Handle empty strings to make the rest of the logic simpler.
     */
    if ((str == NULL) || (*str == NUL)) {
        fputs(EMPTY_ARG, stdout);
        return;
    }

    /*
     *  Emit any single quotes/apostrophes at the start of the string and
     *  bail if that is all we need to do.
     */
    str = print_quoted_apostrophes(str);
    if (*str == NUL)
        return;

    /*
     *  Start the single quote string
     */
    fputc(APOSTROPHE, stdout);
    for (;;) {
        char const * pz = strchr(str, APOSTROPHE);
        if (pz == NULL)
            break;

        /*
         *  Emit the string up to the single quote (apostrophe) we just found.
         */
        (void)fwrite(str, (size_t)(pz - str), (size_t)1, stdout);

        /*
         * Close the current string, emit the apostrophes and re-open the
         * string (IFF there is more text to print).
         */
        fputc(APOSTROPHE, stdout);
        str = print_quoted_apostrophes(pz);
        if (*str == NUL)
            return;

        fputc(APOSTROPHE, stdout);
    }

    /*
     *  If we broke out of the loop, we must still emit the remaining text
     *  and then close the single quote string.
     */
    fputs(str, stdout);
    fputc(APOSTROPHE, stdout);
}

static void
print_enumeration(tOptions * pOpts, tOptDesc * pOD)
{
    uintptr_t e_val = pOD->optArg.argEnum;
    printf(OPT_VAL_FMT, pOpts->pzPROGNAME, pOD->pz_NAME);

    /*
     *  Convert value to string, print that and restore numeric value.
     */
    (*(pOD->pOptProc))(OPTPROC_RETURN_VALNAME, pOD);
    printf(QUOT_ARG_FMT, pOD->optArg.argString);
    if (pOD->fOptState & OPTST_ALLOC_ARG)
        AGFREE(pOD->optArg.argString);
    pOD->optArg.argEnum = e_val;

    printf(OPT_END_FMT, pOpts->pzPROGNAME, pOD->pz_NAME);
}

static void
print_membership(tOptions * pOpts, tOptDesc * pOD)
{
    char const * svstr = pOD->optArg.argString;
    char const * pz;
    uintptr_t val = 1;
    printf(zOptNumFmt, pOpts->pzPROGNAME, pOD->pz_NAME,
           (int)(uintptr_t)(pOD->optCookie));
    pOD->optCookie = VOIDP(~0UL);
    (*(pOD->pOptProc))(OPTPROC_RETURN_VALNAME, pOD);

    pz = pOD->optArg.argString;
    while (*pz != NUL) {
        printf("readonly %s_", pOD->pz_NAME);
        pz = SPN_PLUS_N_SPACE_CHARS(pz);

        for (;;) {
            int ch = *(pz++);
            if (IS_LOWER_CASE_CHAR(ch))   fputc(toupper(ch), stdout);
            else if (IS_UPPER_CASE_CHAR(ch))   fputc(ch, stdout);
            else if (IS_PLUS_N_SPACE_CHAR(ch)) goto name_done;
            else if (ch == NUL)        { pz--; goto name_done; }
            else fputc('_', stdout);
        } name_done:;
        printf(SHOW_VAL_FMT, (unsigned long)val);
        val <<= 1;
    }

    AGFREE(pOD->optArg.argString);
    pOD->optArg.argString = svstr;
}

static void
print_stacked_arg(tOptions * pOpts, tOptDesc * pOD)
{
    tArgList *      pAL = (tArgList *)pOD->optCookie;
    char const **   ppz = pAL->apzArgs;
    int             ct  = pAL->useCt;

    printf(zOptCookieCt, pOpts->pzPROGNAME, pOD->pz_NAME, ct);

    while (--ct >= 0) {
        printf(ARG_BY_NUM_FMT, pOpts->pzPROGNAME, pOD->pz_NAME,
               pAL->useCt - ct);
        print_quot_str(*(ppz++));
        printf(EXPORT_ARG_FMT, pOpts->pzPROGNAME, pOD->pz_NAME,
               pAL->useCt - ct);
    }
}

/**
 * emit the arguments as readily parsed text.
 * The program options are set by emitting the shell "set" command.
 *
 * @param[in] opts  the program options structure
 */
static void
print_reordering(tOptions * opts)
{
    unsigned int ix;

    fputs(set_dash, stdout);

    for (ix = opts->curOptIdx;
         ix < opts->origArgCt;
         ix++) {
        fputc(' ', stdout);
        print_quot_str(opts->origArgVect[ ix ]);
    }
    fputs(init_optct, stdout);
}

/*=export_func  optionPutShell
 * what:  write a portable shell script to parse options
 * private:
 * arg:   tOptions *, pOpts, the program options descriptor
 * doc:   This routine will emit portable shell script text for parsing
 *        the options described in the option definitions.
=*/
void
optionPutShell(tOptions * pOpts)
{
    int  optIx = 0;

    printf(zOptCtFmt, pOpts->curOptIdx-1);

    do  {
        tOptDesc * pOD = pOpts->pOptDesc + optIx;

        if ((pOD->fOptState & OPTST_NO_OUTPUT_MASK) != 0)
            continue;

        /*
         *  Equivalence classes are hard to deal with.  Where the
         *  option data wind up kind of squishes around.  For the purposes
         *  of emitting shell state, they are not recommended, but we'll
         *  do something.  I guess we'll emit the equivalenced-to option
         *  at the point in time when the base option is found.
         */
        if (pOD->optEquivIndex != NO_EQUIVALENT)
            continue; /* equivalence to a different option */

        /*
         *  Equivalenced to a different option.  Process the current option
         *  as the equivalenced-to option.  Keep the persistent state bits,
         *  but copy over the set-state bits.
         */
        if (pOD->optActualIndex != optIx) {
            tOptDesc * p  = pOpts->pOptDesc + pOD->optActualIndex;
            p->optArg     = pOD->optArg;
            p->fOptState &= OPTST_PERSISTENT_MASK;
            p->fOptState |= pOD->fOptState & ~OPTST_PERSISTENT_MASK;
            printf(zEquivMode, pOpts->pzPROGNAME, pOD->pz_NAME, p->pz_NAME);
            pOD = p;
        }

        /*
         *  If the argument type is a set membership bitmask, then we always
         *  emit the thing.  We do this because it will always have some sort
         *  of bitmask value and we need to emit the bit values.
         */
        if (OPTST_GET_ARGTYPE(pOD->fOptState) == OPARG_TYPE_MEMBERSHIP) {
            print_membership(pOpts, pOD);
            continue;
        }

        /*
         *  IF the option was either specified or it wakes up enabled,
         *  then we will emit information.  Otherwise, skip it.
         *  The idea is that if someone defines an option to initialize
         *  enabled, we should tell our shell script that it is enabled.
         */
        if (UNUSED_OPT(pOD) && DISABLED_OPT(pOD))
            continue;

        /*
         *  Handle stacked arguments
         */
        if (  (pOD->fOptState & OPTST_STACKED)
           && (pOD->optCookie != NULL) )  {
            print_stacked_arg(pOpts, pOD);
            continue;
        }

        /*
         *  If the argument has been disabled,
         *  Then set its value to the disablement string
         */
        if ((pOD->fOptState & OPTST_DISABLED) != 0) {
            printf(zOptDisabl, pOpts->pzPROGNAME, pOD->pz_NAME,
                   (pOD->pz_DisablePfx != NULL)
                   ? pOD->pz_DisablePfx : "false");
            continue;
        }

        /*
         *  If the argument type is numeric, the last arg pointer
         *  is really the VALUE of the string that was pointed to.
         */
        if (OPTST_GET_ARGTYPE(pOD->fOptState) == OPARG_TYPE_NUMERIC) {
            printf(zOptNumFmt, pOpts->pzPROGNAME, pOD->pz_NAME,
                   (int)pOD->optArg.argInt);
            continue;
        }

        /*
         *  If the argument type is an enumeration, then it is much
         *  like a text value, except we call the callback function
         *  to emit the value corresponding to the "optArg" number.
         */
        if (OPTST_GET_ARGTYPE(pOD->fOptState) == OPARG_TYPE_ENUMERATION) {
            print_enumeration(pOpts, pOD);
            continue;
        }

        /*
         *  If the argument type is numeric, the last arg pointer
         *  is really the VALUE of the string that was pointed to.
         */
        if (OPTST_GET_ARGTYPE(pOD->fOptState) == OPARG_TYPE_BOOLEAN) {
            printf(zFullOptFmt, pOpts->pzPROGNAME, pOD->pz_NAME,
                   (pOD->optArg.argBool == 0) ? "false" : "true");
            continue;
        }

        /*
         *  IF the option has an empty value,
         *  THEN we set the argument to the occurrence count.
         */
        if (  (pOD->optArg.argString == NULL)
           || (pOD->optArg.argString[0] == NUL) ) {

            printf(zOptNumFmt, pOpts->pzPROGNAME, pOD->pz_NAME,
                   pOD->optOccCt);
            continue;
        }

        /*
         *  This option has a text value
         */
        printf(OPT_VAL_FMT, pOpts->pzPROGNAME, pOD->pz_NAME);
        print_quot_str(pOD->optArg.argString);
        printf(OPT_END_FMT, pOpts->pzPROGNAME, pOD->pz_NAME);

    } while (++optIx < pOpts->presetOptCt );

    if (  ((pOpts->fOptSet & OPTPROC_REORDER) != 0)
       && (pOpts->curOptIdx < pOpts->origArgCt))
        print_reordering(pOpts);

    fflush(stdout);
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/putshell.c */
