
/*
 * \file usage.c
 *
 *  This module implements the default usage procedure for
 *  Automated Options.  It may be overridden, of course.
 *
 * @addtogroup autoopts
 * @{
 */
/*
 *  Sort options:
    --start=END-[S]TATIC-FORWARD --patt='^/\*($|[^:])' \
    --out=xx.c key='^[a-zA-Z0-9_]+\(' --trail='^/\*:' \
    --spac=2 --input=usage.c
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
static unsigned int
parse_usage_flags(ao_flag_names_t const * fnt, char const * txt);

static inline bool
do_gnu_usage(tOptions * pOpts);

static inline bool
skip_misuse_usage(tOptions * pOpts);

static void
print_offer_usage(tOptions * opts);

static void
print_usage_details(tOptions * opts, int exit_code);

static void
print_one_paragraph(char const * text, bool plain, FILE * fp);

static void
prt_conflicts(tOptions * opts, tOptDesc * od);

static void
prt_one_vendor(tOptions *    opts,  tOptDesc *   od,
               arg_types_t * argtp, char const * usefmt);

static void
prt_vendor_opts(tOptions * opts, char const * title);

static void
prt_extd_usage(tOptions * opts, tOptDesc * od, char const * title);

static void
prt_ini_list(char const * const * papz, char const * ini_file,
             char const * path_nm);

static void
prt_preamble(tOptions * opts, tOptDesc * od, arg_types_t * at);

static void
prt_one_usage(tOptions * opts, tOptDesc * od, arg_types_t * at);

static void
prt_opt_usage(tOptions * opts, int ex_code, char const * title);

static void
prt_prog_detail(tOptions * opts);

static int
setGnuOptFmts(tOptions * opts, char const ** ptxt);

static int
setStdOptFmts(tOptions * opts, char const ** ptxt);
/* = = = END-STATIC-FORWARD = = = */

/**
 * Parse the option usage flags string.  Any parsing problems yield
 * a zero (no flags set) result.  This function is internal to
 * set_usage_flags().
 *
 * @param[in] fnt   Flag Name Table - maps a name to a mask
 * @param[in] txt   the text to process.  If NULL, then
 *                  getenv("AUTOOPTS_USAGE") is used.
 * @returns a bit mask indicating which \a fnt entries were found.
 */
static unsigned int
parse_usage_flags(ao_flag_names_t const * fnt, char const * txt)
{
    unsigned int res = 0;

    /*
     * The text may be passed in.  If not, use the environment variable.
     */
    if (txt == NULL) {
        txt = getenv("AUTOOPTS_USAGE");
        if (txt == NULL)
            return 0;
    }

    txt = SPN_WHITESPACE_CHARS(txt);
    if (*txt == NUL)
        return 0;

    /*
     * search the string for table entries.  We must understand everything
     * we see in the string, or we give up on it.
     */
    for (;;) {
        int ix = 0;

        for (;;) {
            if (strneqvcmp(txt, fnt[ix].fnm_name, (int)fnt[ix].fnm_len) == 0)
                break;
            if (++ix >= AOUF_COUNT)
                return 0;
        }

        /*
         *  Make sure we have a full match.  Look for whitespace,
         *  a comma, or a NUL byte.
         */
        if (! IS_END_LIST_ENTRY_CHAR(txt[fnt[ix].fnm_len]))
            return 0;

        res |= 1U << ix;
        txt = SPN_WHITESPACE_CHARS(txt + fnt[ix].fnm_len);

        switch (*txt) {
        case NUL:
            return res;

        case ',':
            txt = SPN_WHITESPACE_CHARS(txt + 1);
            /* Something must follow the comma */

        default:
            continue;
        }
    }
}

/**
 * Set option usage flags.  Any parsing problems yield no changes to options.
 * Three different bits may be fiddled: \a OPTPROC_GNUUSAGE, \a OPTPROC_MISUSE
 * and \a OPTPROC_COMPUTE.
 *
 * @param[in] flg_txt   text to parse.  If NULL, then the AUTOOPTS_USAGE
 *                      environment variable is parsed.
 * @param[in,out] opts  the program option descriptor
 */
LOCAL void
set_usage_flags(tOptions * opts, char const * flg_txt)
{
#   define _aof_(_n, _f)   { sizeof(#_n)-1, _f, #_n },
    static ao_flag_names_t const fn_table[AOUF_COUNT] = {
        AOFLAG_TABLE
    };
#   undef  _aof_

    /*
     * the flag word holds a bit for each selected table entry.
     */
    unsigned int flg = parse_usage_flags(fn_table, flg_txt);
    if (flg == 0) return;

    /*
     * Ensure we do not have conflicting selections
     */
    {
        static unsigned int const form_mask =
            AOUF_gnu | AOUF_autoopts;
        static unsigned int const misuse_mask =
            AOUF_no_misuse_usage | AOUF_misuse_usage;
        if (  ((flg & form_mask)   == form_mask)
           || ((flg & misuse_mask) == misuse_mask) )
            return;
    }

    /*
     * Now fiddle the fOptSet bits, based on settings.
     * The OPTPROC_LONGOPT bit is immutable, thus if it is set,
     * then fnm points to a mask off mask.
     */
    {
        ao_flag_names_t const * fnm = fn_table;
        for (;;) {
            if ((flg & 1) != 0) {
                if ((fnm->fnm_mask & OPTPROC_LONGOPT) != 0)
                     opts->fOptSet &= fnm->fnm_mask;
                else opts->fOptSet |= fnm->fnm_mask;
            }
            flg >>= 1;
            if (flg == 0)
                break;
            fnm++;
        }
    }
}

/*
 *  Figure out if we should try to format usage text sort-of like
 *  the way many GNU programs do.
 */
static inline bool
do_gnu_usage(tOptions * pOpts)
{
    return (pOpts->fOptSet & OPTPROC_GNUUSAGE) ? true : false;
}

/*
 *  Figure out if we should try to format usage text sort-of like
 *  the way many GNU programs do.
 */
static inline bool
skip_misuse_usage(tOptions * pOpts)
{
    return (pOpts->fOptSet & OPTPROC_MISUSE) ? true : false;
}


/*=export_func  optionOnlyUsage
 *
 * what:  Print usage text for just the options
 * arg:   + tOptions *  + pOpts    + program options descriptor +
 * arg:   + int         + ex_code  + exit code for calling exit(3) +
 *
 * doc:
 *  This routine will print only the usage for each option.
 *  This function may be used when the emitted usage must incorporate
 *  information not available to AutoOpts.
=*/
void
optionOnlyUsage(tOptions * pOpts, int ex_code)
{
    char const * pOptTitle = NULL;

    set_usage_flags(pOpts, NULL);
    if ((ex_code != EXIT_SUCCESS) &&
        skip_misuse_usage(pOpts))
        return;

    /*
     *  Determine which header and which option formatting strings to use
     */
    if (do_gnu_usage(pOpts))
        (void)setGnuOptFmts(pOpts, &pOptTitle);
    else
        (void)setStdOptFmts(pOpts, &pOptTitle);

    prt_opt_usage(pOpts, ex_code, pOptTitle);

    fflush(option_usage_fp);
    if (ferror(option_usage_fp) != 0)
        fserr_exit(pOpts->pzProgName, zwriting, (option_usage_fp == stderr)
                   ? zstderr_name : zstdout_name);
}

/**
 * Print a message suggesting how to get help.
 *
 * @param[in] opts      the program options
 */
static void
print_offer_usage(tOptions * opts)
{
    char help[24];

    if (HAS_opt_usage_t(opts)) {
        int ix = opts->presetOptCt;
        tOptDesc * od = opts->pOptDesc + ix;
        while (od->optUsage != AOUSE_HELP) {
            if (++ix >= opts->optCt)
                ao_bug(zmissing_help_msg);
            od++;
        }
        switch (opts->fOptSet & (OPTPROC_LONGOPT | OPTPROC_SHORTOPT)) {
        case OPTPROC_SHORTOPT:
            help[0] = '-';
            help[1] = od->optValue;
            help[2] = NUL;
            break;

        case OPTPROC_LONGOPT:
        case (OPTPROC_LONGOPT | OPTPROC_SHORTOPT):
            help[0] = help[1] = '-';
            strncpy(help + 2, od->pz_Name, 20);
            break;
        
        case 0:
            strncpy(help, od->pz_Name, 20);
            break;
        }

    } else {
        switch (opts->fOptSet & (OPTPROC_LONGOPT | OPTPROC_SHORTOPT)) {
        case OPTPROC_SHORTOPT:
            strcpy(help, "-h");
            break;

        case OPTPROC_LONGOPT:
        case (OPTPROC_LONGOPT | OPTPROC_SHORTOPT):
            strcpy(help, "--help");
            break;
        
        case 0:
            strcpy(help, "help");
            break;
        }
    }

    fprintf(option_usage_fp, zoffer_usage_fmt, opts->pzProgName, help);
}

/**
 * Print information about each option.
 *
 * @param[in] opts      the program options
 * @param[in] exit_code whether or not there was a usage error reported.
 *                      used to select full usage versus abbreviated.
 */
static void
print_usage_details(tOptions * opts, int exit_code)
{
    {
        char const * pOptTitle = NULL;
        int flen;

        /*
         *  Determine which header and which option formatting strings to use
         */
        if (do_gnu_usage(opts)) {
            flen = setGnuOptFmts(opts, &pOptTitle);
            sprintf(line_fmt_buf, zFmtFmt, flen);
            fputc(NL, option_usage_fp);
        }
        else {
            flen = setStdOptFmts(opts, &pOptTitle);
            sprintf(line_fmt_buf, zFmtFmt, flen);

            /*
             *  When we exit with EXIT_SUCCESS and the first option is a doc
             *  option, we do *NOT* want to emit the column headers.
             *  Otherwise, we do.
             */
            if (  (exit_code != EXIT_SUCCESS)
               || ((opts->pOptDesc->fOptState & OPTST_DOCUMENT) == 0) )

                fputs(pOptTitle, option_usage_fp);
        }

        flen = 4 - ((flen + 15) / 8);
        if (flen > 0)
            tab_skip_ct = flen;
        prt_opt_usage(opts, exit_code, pOptTitle);
    }

    /*
     *  Describe the mechanics of denoting the options
     */
    switch (opts->fOptSet & OPTPROC_L_N_S) {
    case OPTPROC_L_N_S:     fputs(zFlagOkay, option_usage_fp); break;
    case OPTPROC_SHORTOPT:  break;
    case OPTPROC_LONGOPT:   fputs(zNoFlags,  option_usage_fp); break;
    case 0:                 fputs(zOptsOnly, option_usage_fp); break;
    }

    if ((opts->fOptSet & OPTPROC_NUM_OPT) != 0)
        fputs(zNumberOpt, option_usage_fp);

    if ((opts->fOptSet & OPTPROC_REORDER) != 0)
        fputs(zReorder, option_usage_fp);

    if (opts->pzExplain != NULL)
        fputs(opts->pzExplain, option_usage_fp);

    /*
     *  IF the user is asking for help (thus exiting with SUCCESS),
     *  THEN see what additional information we can provide.
     */
    if (exit_code == EXIT_SUCCESS)
        prt_prog_detail(opts);

    /*
     * Give bug notification preference to the packager information
     */
    if (HAS_pzPkgDataDir(opts) && (opts->pzPackager != NULL))
        fputs(opts->pzPackager, option_usage_fp);

    else if (opts->pzBugAddr != NULL)
        fprintf(option_usage_fp, zPlsSendBugs, opts->pzBugAddr);

    fflush(option_usage_fp);

    if (ferror(option_usage_fp) != 0)
        fserr_exit(opts->pzProgName, zwriting, (option_usage_fp == stderr)
                   ? zstderr_name : zstdout_name);
}

static void
print_one_paragraph(char const * text, bool plain, FILE * fp)
{
    if (plain) {
#ifdef ENABLE_NLS
#ifdef HAVE_LIBINTL_H
#ifdef DEBUG_ENABLED
#undef gettext
#endif
        char * buf = dgettext("libopts", text);
        if (buf == text)
            text = gettext(text);
#endif /* HAVE_LIBINTL_H */
#endif /* ENABLE_NLS */
        fputs(text, fp);
    }

    else {
        char const * t = optionQuoteString(text, LINE_SPLICE);
        fprintf(fp, PUTS_FMT, t);
        AGFREE(t);
    }
}
 
/*=export_func  optionPrintParagraphs
 * private:
 *
 * what:  Print a paragraph of usage text
 * arg:   + char const * + text  + a block of text that has bee i18n-ed +
 * arg:   + bool         + plain + false -> wrap text in fputs()        +
 * arg:   + FILE *       + fp    + the stream file pointer for output   +
 *
 * doc:
 *  This procedure is called in two contexts: when a full or short usage text
 *  has been provided for display, and when autogen is assembling a list of
 *  translatable texts in the optmain.tlib template.  In the former case, \a
 *  plain is set to \a true, otherwise \a false.
 *
 *  Anything less than 256 characters in size is printed as a single unit.
 *  Otherwise, paragraphs are detected.  A paragraph break is defined as just
 *  before a non-empty line preceded by two newlines or a line that starts
 *  with at least one space character but fewer than 8 space characters.
 *  Lines indented with tabs or more than 7 spaces are considered continuation
 *  lines.
 *
 *  If 'plain' is true, we are emitting text for a user to see.  So, if it is
 *  true and NLS is not enabled, then just write the whole thing at once.
=*/
void
optionPrintParagraphs(char const * text, bool plain, FILE * fp)
{
    size_t len = strlen(text);
    char * buf;
#ifndef ENABLE_NLS
    if (plain || (len < 256))
#else
    if (len < 256)
#endif
    {
        print_one_paragraph(text, plain, fp);
        return;
    }

    AGDUPSTR(buf, text, "ppara");
    text = buf;

    for (;;) {
        char * scan;

        if (len < 256) {
        done:
            print_one_paragraph(buf, plain, fp);
            break;
        }
        scan = buf;

    try_longer:
        scan = strchr(scan, NL);
        if (scan == NULL)
            goto done;

        if ((scan - buf) < 40) {
            scan++;
            goto try_longer;
        }

        scan++;
        if ((! isspace((int)*scan)) || (*scan == HT))
            /*
             * line starts with tab or non-whitespace --> continuation
             */
            goto try_longer;

        if (*scan == NL) {
            /*
             * Double newline -> paragraph break
             * Include all newlines in current paragraph.
             */
            while (*++scan == NL)  /*continue*/;

        } else {
            char * p = scan;
            int   sp_ct = 0;

            while (*p == ' ') {
                if (++sp_ct >= 8) {
                    /*
                     * Too many spaces --> continuation line
                     */
                    scan = p;
                    goto try_longer;
                }
                p++;
            }
        }

        /*
         * "scan" points to the first character of a paragraph or the
         * terminating NUL byte.
         */
        {
            char svch = *scan;
            *scan = NUL;
            print_one_paragraph(buf, plain, fp);
            len -= scan - buf;
            if (len <= 0)
                break;
            *scan = svch;
            buf = scan;
        }
    }
    AGFREE(text);
}

/*=export_func  optionUsage
 * private:
 *
 * what:  Print usage text
 * arg:   + tOptions * + opts + program options descriptor +
 * arg:   + int        + exitCode + exit code for calling exit(3) +
 *
 * doc:
 *  This routine will print usage in both GNU-standard and AutoOpts-expanded
 *  formats.  The descriptor specifies the default, but AUTOOPTS_USAGE will
 *  over-ride this, providing the value of it is set to either "gnu" or
 *  "autoopts".  This routine will @strong{not} return.
 *
 *  If "exitCode" is "AO_EXIT_REQ_USAGE" (normally 64), then output will to
 *  to stdout and the actual exit code will be "EXIT_SUCCESS".
=*/
void
optionUsage(tOptions * opts, int usage_exit_code)
{
    int exit_code = (usage_exit_code == AO_EXIT_REQ_USAGE)
        ? EXIT_SUCCESS : usage_exit_code;

    displayEnum = false;
    set_usage_flags(opts, NULL);

    /*
     *  Paged usage will preset option_usage_fp to an output file.
     *  If it hasn't already been set, then set it to standard output
     *  on successful exit (help was requested), otherwise error out.
     *
     *  Test the version before obtaining pzFullUsage or pzShortUsage.
     *  These fields do not exist before revision 30.
     */
    {
        char const * pz;

        if (exit_code == EXIT_SUCCESS) {
            pz = (opts->structVersion >= 30 * 4096)
                ? opts->pzFullUsage : NULL;

            if (option_usage_fp == NULL)
                option_usage_fp = print_exit ? stderr : stdout;

        } else {
            pz = (opts->structVersion >= 30 * 4096)
                ? opts->pzShortUsage : NULL;

            if (option_usage_fp == NULL)
                option_usage_fp = stderr;
        }

        if (((opts->fOptSet & OPTPROC_COMPUTE) == 0) && (pz != NULL)) {
            if ((opts->fOptSet & OPTPROC_TRANSLATE) != 0)
                optionPrintParagraphs(pz, true, option_usage_fp);
            else
                fputs(pz, option_usage_fp);
            goto flush_and_exit;
        }
    }

    fprintf(option_usage_fp, opts->pzUsageTitle, opts->pzProgName);

    if ((exit_code == EXIT_SUCCESS) ||
        (! skip_misuse_usage(opts)))

        print_usage_details(opts, usage_exit_code);
    else
        print_offer_usage(opts);
    
 flush_and_exit:
    fflush(option_usage_fp);
    if (ferror(option_usage_fp) != 0)
        fserr_exit(opts->pzProgName, zwriting, (option_usage_fp == stdout)
                   ? zstdout_name : zstderr_name);

    option_exits(exit_code);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   PER OPTION TYPE USAGE INFORMATION
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 * print option conflicts.
 *
 * @param opts the program option descriptor
 * @param od   the option descriptor
 */
static void
prt_conflicts(tOptions * opts, tOptDesc * od)
{
    const int * opt_no;
    fputs(zTabHyp + tab_skip_ct, option_usage_fp);

    /*
     *  REQUIRED:
     */
    if (od->pOptMust != NULL) {
        opt_no = od->pOptMust;

        if (opt_no[1] == NO_EQUIVALENT) {
            fprintf(option_usage_fp, zReqOne,
                    opts->pOptDesc[*opt_no].pz_Name);
        } else {
            fputs(zReqThese, option_usage_fp);
            for (;;) {
                fprintf(option_usage_fp, zTabout + tab_skip_ct,
                        opts->pOptDesc[*opt_no].pz_Name);
                if (*++opt_no == NO_EQUIVALENT)
                    break;
            }
        }

        if (od->pOptCant != NULL)
            fputs(zTabHypAnd + tab_skip_ct, option_usage_fp);
    }

    /*
     *  CONFLICTS:
     */
    if (od->pOptCant == NULL)
        return;

    opt_no = od->pOptCant;

    if (opt_no[1] == NO_EQUIVALENT) {
        fprintf(option_usage_fp, zProhibOne,
                opts->pOptDesc[*opt_no].pz_Name);
        return;
    }

    fputs(zProhib, option_usage_fp);
    for (;;) {
        fprintf(option_usage_fp, zTabout + tab_skip_ct,
                opts->pOptDesc[*opt_no].pz_Name);
        if (*++opt_no == NO_EQUIVALENT)
            break;
    }
}

/**
 *  Print the usage information for a single vendor option.
 *
 * @param[in] opts    the program option descriptor
 * @param[in] od      the option descriptor
 * @param[in] argtp   names of the option argument types
 * @param[in] usefmt  format for primary usage line
 */
static void
prt_one_vendor(tOptions *    opts,  tOptDesc *   od,
               arg_types_t * argtp, char const * usefmt)
{
    prt_preamble(opts, od, argtp);

    {
        char z[ 80 ];
        char const *  pzArgType;

        /*
         *  Determine the argument type string first on its usage, then,
         *  when the option argument is required, base the type string on the
         *  argument type.
         */
        if (od->fOptState & OPTST_ARG_OPTIONAL) {
            pzArgType = argtp->pzOpt;

        } else switch (OPTST_GET_ARGTYPE(od->fOptState)) {
        case OPARG_TYPE_NONE:        pzArgType = argtp->pzNo;   break;
        case OPARG_TYPE_ENUMERATION: pzArgType = argtp->pzKey;  break;
        case OPARG_TYPE_FILE:        pzArgType = argtp->pzFile; break;
        case OPARG_TYPE_MEMBERSHIP:  pzArgType = argtp->pzKeyL; break;
        case OPARG_TYPE_BOOLEAN:     pzArgType = argtp->pzBool; break;
        case OPARG_TYPE_NUMERIC:     pzArgType = argtp->pzNum;  break;
        case OPARG_TYPE_HIERARCHY:   pzArgType = argtp->pzNest; break;
        case OPARG_TYPE_STRING:      pzArgType = argtp->pzStr;  break;
        case OPARG_TYPE_TIME:        pzArgType = argtp->pzTime; break;
        default:                     goto bogus_desc;
        }

        pzArgType = SPN_WHITESPACE_CHARS(pzArgType);
        if (*pzArgType == NUL)
            snprintf(z, sizeof(z), "%s", od->pz_Name);
        else
            snprintf(z, sizeof(z), "%s=%s", od->pz_Name, pzArgType);
        fprintf(option_usage_fp, usefmt, z, od->pzText);

        switch (OPTST_GET_ARGTYPE(od->fOptState)) {
        case OPARG_TYPE_ENUMERATION:
        case OPARG_TYPE_MEMBERSHIP:
            displayEnum = (od->pOptProc != NULL) ? true : displayEnum;
        }
    }

    return;

 bogus_desc:
    fprintf(stderr, zbad_od, opts->pzProgName, od->pz_Name);
    ao_bug(zbad_arg_type_msg);
}

/**
 * Print the long options processed with "-W".  These options will be the
 * ones that do *not* have flag characters.
 *
 * @param opts  the program option descriptor
 * @param title the title for the options
 */
static void
prt_vendor_opts(tOptions * opts, char const * title)
{
    static unsigned int const not_vended_mask =
        OPTST_NO_USAGE_MASK | OPTST_DOCUMENT;

    static char const vfmtfmt[] = "%%-%us %%s\n";
    char vfmt[sizeof(vfmtfmt)];

    /*
     *  Only handle client specified options.  The "vendor option" follows
     *  "presetOptCt", so we won't loop/recurse indefinitely.
     */
    int          ct     = opts->presetOptCt;
    tOptDesc *   od     = opts->pOptDesc;
    fprintf(option_usage_fp, zTabout + tab_skip_ct, zVendOptsAre);

    {
        size_t   nmlen  = 0;
        do  {
            size_t l;
            if (  ((od->fOptState & not_vended_mask) != 0)
               || IS_GRAPHIC_CHAR(od->optValue))
                continue;

            l = strlen(od->pz_Name);
            if (l > nmlen)  nmlen = l;
        } while (od++, (--ct > 0));

        snprintf(vfmt, sizeof(vfmt), vfmtfmt, (unsigned int)nmlen + 4);
    }

    if (tab_skip_ct > 0)
        tab_skip_ct--;

    ct    = opts->presetOptCt;
    od    = opts->pOptDesc;

    do  {
        if (  ((od->fOptState & not_vended_mask) != 0)
           || IS_GRAPHIC_CHAR(od->optValue))
            continue;

        prt_one_vendor(opts, od, &argTypes, vfmt);
        prt_extd_usage(opts, od, title);

    } while (od++, (--ct > 0));

    /* no need to restore "tab_skip_ct" - options are done now */
}

/**
 * Print extended usage.  Usage/help was requested.
 *
 * @param opts  the program option descriptor
 * @param od   the option descriptor
 * @param title the title for the options
 */
static void
prt_extd_usage(tOptions * opts, tOptDesc * od, char const * title)
{
    if (  ((opts->fOptSet & OPTPROC_VENDOR_OPT) != 0)
       && (od->optActualValue == VENDOR_OPTION_VALUE)) {
        prt_vendor_opts(opts, title);
        return;
    }

    /*
     *  IF there are option conflicts or dependencies,
     *  THEN print them here.
     */
    if ((od->pOptMust != NULL) || (od->pOptCant != NULL))
        prt_conflicts(opts, od);

    /*
     *  IF there is a disablement string
     *  THEN print the disablement info
     */
    if (od->pz_DisableName != NULL )
        fprintf(option_usage_fp, zDis + tab_skip_ct, od->pz_DisableName);

    /*
     *  Check for argument types that have callbacks with magical properties
     */
    switch (OPTST_GET_ARGTYPE(od->fOptState)) {
    case OPARG_TYPE_NUMERIC:
        /*
         *  IF the numeric option has a special callback,
         *  THEN call it, requesting the range or other special info
         */
        if (  (od->pOptProc != NULL)
           && (od->pOptProc != optionNumericVal) ) {
            (*(od->pOptProc))(OPTPROC_EMIT_USAGE, od);
        }
        break;

    case OPARG_TYPE_FILE:
        (*(od->pOptProc))(OPTPROC_EMIT_USAGE, od);
        break;
    }

    /*
     *  IF the option defaults to being enabled,
     *  THEN print that out
     */
    if (od->fOptState & OPTST_INITENABLED)
        fputs(zEnab + tab_skip_ct, option_usage_fp);

    /*
     *  IF  the option is in an equivalence class
     *        AND not the designated lead
     *  THEN print equivalence and leave it at that.
     */
    if (  (od->optEquivIndex != NO_EQUIVALENT)
       && (od->optEquivIndex != od->optActualIndex )  )  {
        fprintf(option_usage_fp, zalt_opt + tab_skip_ct,
                 opts->pOptDesc[ od->optEquivIndex ].pz_Name);
        return;
    }

    /*
     *  IF this particular option can NOT be preset
     *    AND some form of presetting IS allowed,
     *    AND it is not an auto-managed option (e.g. --help, et al.)
     *  THEN advise that this option may not be preset.
     */
    if (  ((od->fOptState & OPTST_NO_INIT) != 0)
       && (  (opts->papzHomeList != NULL)
          || (opts->pzPROGNAME != NULL)
          )
       && (od->optIndex < opts->presetOptCt)
       )

        fputs(zNoPreset + tab_skip_ct, option_usage_fp);

    /*
     *  Print the appearance requirements.
     */
    if (OPTST_GET_ARGTYPE(od->fOptState) == OPARG_TYPE_MEMBERSHIP)
        fputs(zMembers + tab_skip_ct, option_usage_fp);

    else switch (od->optMinCt) {
    case 1:
    case 0:
        switch (od->optMaxCt) {
        case 0:       fputs(zPreset + tab_skip_ct, option_usage_fp); break;
        case NOLIMIT: fputs(zNoLim  + tab_skip_ct, option_usage_fp); break;
        case 1:       break;
            /*
             * IF the max is more than one but limited, print "UP TO" message
             */
        default:
            fprintf(option_usage_fp, zUpTo + tab_skip_ct, od->optMaxCt); break;
        }
        break;

    default:
        /*
         *  More than one is required.  Print the range.
         */
        fprintf(option_usage_fp, zMust + tab_skip_ct,
                od->optMinCt, od->optMaxCt);
    }

    if (  NAMED_OPTS(opts)
       && (opts->specOptIdx.default_opt == od->optIndex))
        fputs(zDefaultOpt + tab_skip_ct, option_usage_fp);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 * Figure out where all the initialization files might live.  This requires
 * translating some environment variables and testing to see if a name is a
 * directory or a file.  It's squishy, but important to tell users how to
 * find these files.
 *
 * @param[in]  papz        search path
 * @param[out] ini_file    an output buffer of AG_PATH_MAX+1 bytes
 * @param[in]  path_nm     the name of the file we're hunting for
 */
static void
prt_ini_list(char const * const * papz, char const * ini_file,
             char const * path_nm)
{
    char pth_buf[AG_PATH_MAX+1];

    fputs(zPresetIntro, option_usage_fp);

    for (;;) {
        char const * path   = *(papz++);
        char const * nm_buf = pth_buf;

        if (path == NULL)
            break;

        /*
         * Ignore any invalid paths
         */
        if (! optionMakePath(pth_buf, (int)sizeof(pth_buf), path, path_nm))
            nm_buf = path;

        /*
         * Expand paths that are relative to the executable or installation
         * directories.  Leave alone paths that use environment variables.
         */
        else if ((*path == '$')
                 && ((path[1] == '$') || (path[1] == '@')))
            path = nm_buf;

        /*
         *  Print the name of the "homerc" file.  If the "rcfile" name is
         *  not empty, we may or may not print that, too...
         */
        fprintf(option_usage_fp, zPathFmt, path);
        if (*ini_file != NUL) {
            struct stat sb;

            /*
             *  IF the "homerc" file is a directory,
             *  then append the "rcfile" name.
             */
            if ((stat(nm_buf, &sb) == 0) && S_ISDIR(sb.st_mode)) {
                fputc(DIRCH,    option_usage_fp);
                fputs(ini_file, option_usage_fp);
            }
        }

        fputc(NL, option_usage_fp);
    }
}

/**
 *  Print the usage line preamble text
 *
 * @param opts  the program option descriptor
 * @param od    the option descriptor
 * @param at    names of the option argument types
 */
static void
prt_preamble(tOptions * opts, tOptDesc * od, arg_types_t * at)
{
    /*
     *  Flag prefix: IF no flags at all, then omit it.  If not printable
     *  (not allowed for this option), then blank, else print it.
     *  Follow it with a comma if we are doing GNU usage and long
     *  opts are to be printed too.
     */
    if ((opts->fOptSet & OPTPROC_SHORTOPT) == 0)
        fputs(at->pzSpc, option_usage_fp);

    else if (! IS_GRAPHIC_CHAR(od->optValue)) {
        if (  (opts->fOptSet & (OPTPROC_GNUUSAGE|OPTPROC_LONGOPT))
           == (OPTPROC_GNUUSAGE|OPTPROC_LONGOPT))
            fputc(' ', option_usage_fp);
        fputs(at->pzNoF, option_usage_fp);

    } else {
        fprintf(option_usage_fp, "   -%c", od->optValue);
        if (  (opts->fOptSet & (OPTPROC_GNUUSAGE|OPTPROC_LONGOPT))
           == (OPTPROC_GNUUSAGE|OPTPROC_LONGOPT))
            fputs(", ", option_usage_fp);
    }
}

/**
 *  Print the usage information for a single option.
 *
 * @param opts  the program option descriptor
 * @param od    the option descriptor
 * @param at    names of the option argument types
 */
static void
prt_one_usage(tOptions * opts, tOptDesc * od, arg_types_t * at)
{
    prt_preamble(opts, od, at);

    {
        char z[80];
        char const * atyp;

        /*
         *  Determine the argument type string first on its usage, then,
         *  when the option argument is required, base the type string on the
         *  argument type.
         */
        if (od->fOptState & OPTST_ARG_OPTIONAL) {
            atyp = at->pzOpt;

        } else switch (OPTST_GET_ARGTYPE(od->fOptState)) {
        case OPARG_TYPE_NONE:        atyp = at->pzNo;   break;
        case OPARG_TYPE_ENUMERATION: atyp = at->pzKey;  break;
        case OPARG_TYPE_FILE:        atyp = at->pzFile; break;
        case OPARG_TYPE_MEMBERSHIP:  atyp = at->pzKeyL; break;
        case OPARG_TYPE_BOOLEAN:     atyp = at->pzBool; break;
        case OPARG_TYPE_NUMERIC:     atyp = at->pzNum;  break;
        case OPARG_TYPE_HIERARCHY:   atyp = at->pzNest; break;
        case OPARG_TYPE_STRING:      atyp = at->pzStr;  break;
        case OPARG_TYPE_TIME:        atyp = at->pzTime; break;
        default:                     goto bogus_desc;
        }

#ifdef _WIN32
        if (at->pzOptFmt == zGnuOptFmt)
            snprintf(z, sizeof(z), "--%s%s", od->pz_Name, atyp);
        else if (at->pzOptFmt == zGnuOptFmt + 2)
            snprintf(z, sizeof(z), "%s%s", od->pz_Name, atyp);
        else
#endif
        snprintf(z, sizeof(z), at->pzOptFmt, atyp, od->pz_Name,
                 (od->optMinCt != 0) ? at->pzReq : at->pzOpt);

        fprintf(option_usage_fp, line_fmt_buf, z, od->pzText);

        switch (OPTST_GET_ARGTYPE(od->fOptState)) {
        case OPARG_TYPE_ENUMERATION:
        case OPARG_TYPE_MEMBERSHIP:
            displayEnum = (od->pOptProc != NULL) ? true : displayEnum;
        }
    }

    return;

 bogus_desc:
    fprintf(stderr, zbad_od, opts->pzProgName, od->pz_Name);
    option_exits(EX_SOFTWARE);
}

/**
 *  Print out the usage information for just the options.
 */
static void
prt_opt_usage(tOptions * opts, int ex_code, char const * title)
{
    int         ct     = opts->optCt;
    int         optNo  = 0;
    tOptDesc *  od     = opts->pOptDesc;
    int         docCt  = 0;

    do  {
        /*
         * no usage --> disallowed on command line (OPTST_NO_COMMAND), or
         * deprecated -- strongly discouraged (OPTST_DEPRECATED), or
         * compiled out of current object code (OPTST_OMITTED)
         */
        if ((od->fOptState & OPTST_NO_USAGE_MASK) != 0) {

            /*
             * IF      this is a compiled-out option
             *   *AND* usage was requested with "omitted-usage"
             *   *AND* this is NOT abbreviated usage
             * THEN display this option.
             */
            if (  (od->fOptState == (OPTST_OMITTED | OPTST_NO_INIT))
               && (od->pz_Name != NULL)
               && (ex_code == EXIT_SUCCESS))  {

                char const * why_pz =
                    (od->pzText == NULL) ? zDisabledWhy : od->pzText;
                prt_preamble(opts, od, &argTypes);
                fprintf(option_usage_fp, zDisabledOpt, od->pz_Name, why_pz);
            }

            continue;
        }

        if ((od->fOptState & OPTST_DOCUMENT) != 0) {
            if (ex_code == EXIT_SUCCESS) {
                fprintf(option_usage_fp, argTypes.pzBrk, od->pzText,
                        title);
                docCt++;
            }

            continue;
        }

        /* Skip name only options when we have a vendor option */
        if (  ((opts->fOptSet & OPTPROC_VENDOR_OPT) != 0)
           && (! IS_GRAPHIC_CHAR(od->optValue)))
            continue;

        /*
         *  IF       this is the first auto-opt maintained option
         *    *AND*  we are doing a full help
         *    *AND*  there are documentation options
         *    *AND*  the last one was not a doc option,
         *  THEN document that the remaining options are not user opts
         */
        if ((docCt > 0) && (ex_code == EXIT_SUCCESS)) {
            if (opts->presetOptCt == optNo) {
                if ((od[-1].fOptState & OPTST_DOCUMENT) == 0)
                    fprintf(option_usage_fp, argTypes.pzBrk, zAuto, title);

            } else if ((ct == 1) &&
                       (opts->fOptSet & OPTPROC_VENDOR_OPT))
                fprintf(option_usage_fp, argTypes.pzBrk, zVendIntro, title);
        }

        prt_one_usage(opts, od, &argTypes);

        /*
         *  IF we were invoked because of the --help option,
         *  THEN print all the extra info
         */
        if (ex_code == EXIT_SUCCESS)
            prt_extd_usage(opts, od, title);

    } while (od++, optNo++, (--ct > 0));

    fputc(NL, option_usage_fp);
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**
 *  Print program details.
 * @param[in] opts  the program option descriptor
 */
static void
prt_prog_detail(tOptions * opts)
{
    bool need_intro = (opts->papzHomeList == NULL);

    /*
     *  Display all the places we look for config files, if we have
     *  a list of directories to search.
     */
    if (! need_intro)
        prt_ini_list(opts->papzHomeList, opts->pzRcName, opts->pzProgPath);

    /*
     *  Let the user know about environment variable settings
     */
    if ((opts->fOptSet & OPTPROC_ENVIRON) != 0) {
        if (need_intro)
            fputs(zPresetIntro, option_usage_fp);

        fprintf(option_usage_fp, zExamineFmt, opts->pzPROGNAME);
    }

    /*
     *  IF we found an enumeration,
     *  THEN hunt for it again.  Call the handler proc with a NULL
     *       option struct pointer.  That tells it to display the keywords.
     */
    if (displayEnum) {
        int        ct     = opts->optCt;
        int        optNo  = 0;
        tOptDesc * od     = opts->pOptDesc;

        fputc(NL, option_usage_fp);
        fflush(option_usage_fp);
        do  {
            switch (OPTST_GET_ARGTYPE(od->fOptState)) {
            case OPARG_TYPE_ENUMERATION:
            case OPARG_TYPE_MEMBERSHIP:
                (*(od->pOptProc))(OPTPROC_EMIT_USAGE, od);
            }
        } while (od++, optNo++, (--ct > 0));
    }

    /*
     *  If there is a detail string, now is the time for that.
     */
    if (opts->pzDetail != NULL)
        fputs(opts->pzDetail, option_usage_fp);
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *   OPTION LINE FORMATTING SETUP
 *
 *  The "OptFmt" formats receive three arguments:
 *  1.  the type of the option's argument
 *  2.  the long name of the option
 *  3.  "YES" or "no ", depending on whether or not the option must appear
 *      on the command line.
 *  These formats are used immediately after the option flag (if used) has
 *  been printed.
 *
 *  Set up the formatting for GNU-style output
 */
static int
setGnuOptFmts(tOptions * opts, char const ** ptxt)
{
    static char const zOneSpace[] = " ";
    int  flen = 22;
    *ptxt = zNoRq_ShrtTtl;

    argTypes.pzStr  = zGnuStrArg;
    argTypes.pzReq  = zOneSpace;
    argTypes.pzNum  = zGnuNumArg;
    argTypes.pzKey  = zGnuKeyArg;
    argTypes.pzKeyL = zGnuKeyLArg;
    argTypes.pzTime = zGnuTimeArg;
    argTypes.pzFile = zGnuFileArg;
    argTypes.pzBool = zGnuBoolArg;
    argTypes.pzNest = zGnuNestArg;
    argTypes.pzOpt  = zGnuOptArg;
    argTypes.pzNo   = zOneSpace;
    argTypes.pzBrk  = zGnuBreak;
    argTypes.pzNoF  = zSixSpaces;
    argTypes.pzSpc  = zThreeSpaces;

    switch (opts->fOptSet & OPTPROC_L_N_S) {
    case OPTPROC_L_N_S:    argTypes.pzOptFmt = zGnuOptFmt;     break;
    case OPTPROC_LONGOPT:  argTypes.pzOptFmt = zGnuOptFmt;     break;
    case 0:                argTypes.pzOptFmt = zGnuOptFmt + 2; break;
    case OPTPROC_SHORTOPT:
        argTypes.pzOptFmt = zShrtGnuOptFmt;
        zGnuStrArg[0] = zGnuNumArg[0] = zGnuKeyArg[0] = zGnuBoolArg[0] = ' ';
        argTypes.pzOpt = " [arg]";
        flen = 8;
        break;
    }

    return flen;
}


/*
 *  Standard (AutoOpts normal) option line formatting
 */
static int
setStdOptFmts(tOptions * opts, char const ** ptxt)
{
    int  flen = 0;

    argTypes.pzStr  = zStdStrArg;
    argTypes.pzReq  = zStdReqArg;
    argTypes.pzNum  = zStdNumArg;
    argTypes.pzKey  = zStdKeyArg;
    argTypes.pzKeyL = zStdKeyLArg;
    argTypes.pzTime = zStdTimeArg;
    argTypes.pzFile = zStdFileArg;
    argTypes.pzBool = zStdBoolArg;
    argTypes.pzNest = zStdNestArg;
    argTypes.pzOpt  = zStdOptArg;
    argTypes.pzNo   = zStdNoArg;
    argTypes.pzBrk  = zStdBreak;
    argTypes.pzNoF  = zFiveSpaces;
    argTypes.pzSpc  = zTwoSpaces;

    switch (opts->fOptSet & (OPTPROC_NO_REQ_OPT | OPTPROC_SHORTOPT)) {
    case (OPTPROC_NO_REQ_OPT | OPTPROC_SHORTOPT):
        *ptxt = zNoRq_ShrtTtl;
        argTypes.pzOptFmt = zNrmOptFmt;
        flen = 19;
        break;

    case OPTPROC_NO_REQ_OPT:
        *ptxt = zNoRq_NoShrtTtl;
        argTypes.pzOptFmt = zNrmOptFmt;
        flen = 19;
        break;

    case OPTPROC_SHORTOPT:
        *ptxt = zReq_ShrtTtl;
        argTypes.pzOptFmt = zReqOptFmt;
        flen = 24;
        break;

    case 0:
        *ptxt = zReq_NoShrtTtl;
        argTypes.pzOptFmt = zReqOptFmt;
        flen = 24;
    }

    return flen;
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/usage.c */
