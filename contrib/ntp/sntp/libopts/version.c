
/** \file version.c
 *
 *  This module implements the default usage procedure for
 *  Automated Options.  It may be overridden, of course.
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

/*=export_func  optionVersion
 *
 * what:     return the compiled AutoOpts version number
 * ret_type: char const *
 * ret_desc: the version string in constant memory
 * doc:
 *  Returns the full version string compiled into the library.
 *  The returned string cannot be modified.
=*/
char const *
optionVersion(void)
{
    static char const ver[] = OPTIONS_DOTTED_VERSION;
    return ver;
}

static void
emit_first_line(
    FILE * fp, char const * alt1, char const * alt2, char const * alt3)
{
    char const * p = (alt1 != NULL) ? alt1 : ((alt2 != NULL) ? alt2 : alt3);
    char const * e;
    if (p == NULL)
        return;
    e = strchr(p, NL);
    if (e == NULL)
        fputs(p, fp);
    else
        fwrite(p, 1, (e - p), fp);
    fputc(NL, fp);
}

/**
 * Select among various ways to emit version information.
 *
 * @param[in] o   the option descriptor
 * @param[in] fp  the output stream
 */
static void
emit_simple_ver(tOptions * o, FILE * fp)
{
    emit_first_line(fp, o->pzFullVersion, o->pzCopyright, o->pzUsageTitle);
}

/**
 * print the version with a copyright notice.
 *
 * @param[in] o   the option descriptor
 * @param[in] fp  the output stream
 */
static void
emit_copy_full(tOptions * o, FILE * fp)
{
    if (o->pzCopyright != NULL)
        fputs(o->pzCopyright, fp);

    else if (o->pzFullVersion != NULL)
        fputs(o->pzFullVersion, fp);

    else
        emit_first_line(fp, o->pzUsageTitle, NULL, NULL);
    
    if (HAS_pzPkgDataDir(o) && (o->pzPackager != NULL)) {
        fputc(NL, fp);
        fputs(o->pzPackager, fp);

    } else if (o->pzBugAddr != NULL) {
        fputc(NL, fp);
        fprintf(fp, zPlsSendBugs, o->pzBugAddr);
    }
}

/**
 * print the version and any copyright notice.
 * The version with a full copyright and additional notes.
 *
 * @param[in] opts  the option descriptor
 * @param[in] fp    the output stream
 */
static void
emit_copy_note(tOptions * opts, FILE * fp)
{
    if (opts->pzCopyright != NULL)
        fputs(opts->pzCopyright, fp);

    if (opts->pzCopyNotice != NULL)
        fputs(opts->pzCopyNotice, fp);

    fputc(NL, fp);
    fprintf(fp, zao_ver_fmt, optionVersion());
    
    if (HAS_pzPkgDataDir(opts) && (opts->pzPackager != NULL)) {
        fputc(NL, fp);
        fputs(opts->pzPackager, fp);

    } else if (opts->pzBugAddr != NULL) {
        fputc(NL, fp);
        fprintf(fp, zPlsSendBugs, opts->pzBugAddr);
    }
}

/**
 * Handle the version printing.  We must see how much information
 * is being requested and select the correct printing routine.
 */
static void
print_ver(tOptions * opts, tOptDesc * od, FILE * fp, bool call_exit)
{
    char ch;

    if (opts <= OPTPROC_EMIT_LIMIT)
        return;

    /*
     *  IF we have an argument for this option, use it
     *  Otherwise, default to version only or copyright note,
     *  depending on whether the layout is GNU standard form or not.
     */
    if (  (od->fOptState & OPTST_ARG_OPTIONAL)
       && (od->optArg.argString != NULL)
       && (od->optArg.argString[0] != NUL))

        ch = od->optArg.argString[0];

    else {
        set_usage_flags(opts, NULL);
        ch = (opts->fOptSet & OPTPROC_GNUUSAGE) ? 'c' : 'v';
    }

    switch (ch) {
    case NUL: /* arg provided, but empty */
    case 'v': case 'V': emit_simple_ver(opts, fp); break;
    case 'c': case 'C': emit_copy_full( opts, fp); break;
    case 'n': case 'N': emit_copy_note( opts, fp); break;

    default:
        fprintf(stderr, zBadVerArg, ch);
        option_exits(EXIT_FAILURE);
    }

    fflush(fp);
    if (ferror(fp))
        fserr_exit(opts->pzProgName, zwriting,
                   (fp == stdout) ? zstdout_name : zstderr_name);

    if (call_exit)
        option_exits(EXIT_SUCCESS);
}

/*=export_func  optionPrintVersion
 *
 * what:  Print the program version
 * arg:   + tOptions * + opts + program options descriptor +
 * arg:   + tOptDesc * + od   + the descriptor for this arg +
 *
 * doc:
 *  This routine will print the version to stdout.
=*/
void
optionPrintVersion(tOptions * opts, tOptDesc * od)
{
    print_ver(opts, od, print_exit ? stderr : stdout, true);
}

/*=export_func  optionPrintVersionAndReturn
 *
 * what:  Print the program version
 * arg:   + tOptions * + opts + program options descriptor +
 * arg:   + tOptDesc * + od   + the descriptor for this arg +
 *
 * doc:
 *  This routine will print the version to stdout and return
 *  instead of exiting.  Please see the source for the
 *  @code{print_ver} funtion for details on selecting how
 *  verbose to be after this function returns.
=*/
void
optionPrintVersionAndReturn(tOptions * opts, tOptDesc * od)
{
    print_ver(opts, od, print_exit ? stderr : stdout, false);
}

/*=export_func  optionVersionStderr
 * private:
 *
 * what:  Print the program version to stderr
 * arg:   + tOptions * + opts + program options descriptor +
 * arg:   + tOptDesc * + od   + the descriptor for this arg +
 *
 * doc:
 *  This routine will print the version to stderr.
=*/
void
optionVersionStderr(tOptions * opts, tOptDesc * od)
{
    print_ver(opts, od, stderr, true);
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/version.c */
