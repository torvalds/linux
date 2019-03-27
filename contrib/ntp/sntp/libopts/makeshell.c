
/**
 * \file makeshell.c
 *
 *  This module will interpret the options set in the tOptions
 *  structure and create a Bourne shell script capable of parsing them.
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

 static inline unsigned char to_uchar (char ch) { return ch; }

#define UPPER(_c) (toupper(to_uchar(_c)))
#define LOWER(_c) (tolower(to_uchar(_c)))

/* = = = START-STATIC-FORWARD = = = */
static void
emit_var_text(char const * prog, char const * var, int fdin);

static void
text_to_var(tOptions * opts, teTextTo which, tOptDesc * od);

static void
emit_usage(tOptions * opts);

static void
emit_wrapup(tOptions * opts);

static void
emit_setup(tOptions * opts);

static void
emit_action(tOptions * opts, tOptDesc * od);

static void
emit_inaction(tOptions * opts, tOptDesc * od);

static void
emit_flag(tOptions * opts);

static void
emit_match_expr(char const * name, tOptDesc * cod, tOptions * opts);

static void
emit_long(tOptions * opts);

static char *
load_old_output(char const * fname, char const * pname);

static void
open_out(char const * fname, char const * pname);
/* = = = END-STATIC-FORWARD = = = */

LOCAL noreturn void
option_exits(int exit_code)
{
    if (print_exit)
        printf("\nexit %d\n", exit_code);
    exit(exit_code);
}

LOCAL noreturn void
ao_bug(char const * msg)
{
    fprintf(stderr, zao_bug_msg, msg);
    option_exits(EX_SOFTWARE);
}

LOCAL void
fserr_warn(char const * prog, char const * op, char const * fname)
{
    fprintf(stderr, zfserr_fmt, prog, errno, strerror(errno),
            op, fname);
}

LOCAL noreturn void
fserr_exit(char const * prog, char const * op, char const * fname)
{
    fserr_warn(prog, op, fname);
    option_exits(EXIT_FAILURE);
}

/*=export_func  optionParseShell
 * private:
 *
 * what:  Decipher a boolean value
 * arg:   + tOptions * + pOpts    + program options descriptor +
 *
 * doc:
 *  Emit a shell script that will parse the command line options.
=*/
void
optionParseShell(tOptions * opts)
{
    /*
     *  Check for our SHELL option now.
     *  IF the output file contains the "#!" magic marker,
     *  it will override anything we do here.
     */
    if (HAVE_GENSHELL_OPT(SHELL))
        shell_prog = GENSHELL_OPT_ARG(SHELL);

    else if (! ENABLED_GENSHELL_OPT(SHELL))
        shell_prog = NULL;

    else if ((shell_prog = getenv("SHELL")),
             shell_prog == NULL)

        shell_prog = POSIX_SHELL;

    /*
     *  Check for a specified output file
     */
    if (HAVE_GENSHELL_OPT(SCRIPT))
        open_out(GENSHELL_OPT_ARG(SCRIPT), opts->pzProgName);
    
    emit_usage(opts);
    emit_setup(opts);

    /*
     *  There are four modes of option processing.
     */
    switch (opts->fOptSet & (OPTPROC_LONGOPT|OPTPROC_SHORTOPT)) {
    case OPTPROC_LONGOPT:
        fputs(LOOP_STR,         stdout);

        fputs(LONG_OPT_MARK,    stdout);
        fputs(INIT_LOPT_STR,    stdout);
        emit_long(opts);
        printf(LOPT_ARG_FMT,    opts->pzPROGNAME);
        fputs(END_OPT_SEL_STR,  stdout);

        fputs(NOT_FOUND_STR,    stdout);
        break;

    case 0:
        fputs(ONLY_OPTS_LOOP,   stdout);
        fputs(INIT_LOPT_STR,    stdout);
        emit_long(opts);
        printf(LOPT_ARG_FMT,    opts->pzPROGNAME);
        break;

    case OPTPROC_SHORTOPT:
        fputs(LOOP_STR,         stdout);

        fputs(FLAG_OPT_MARK,    stdout);
        fputs(INIT_OPT_STR,     stdout);
        emit_flag(opts);
        printf(OPT_ARG_FMT,     opts->pzPROGNAME);
        fputs(END_OPT_SEL_STR,  stdout);

        fputs(NOT_FOUND_STR,    stdout);
        break;

    case OPTPROC_LONGOPT|OPTPROC_SHORTOPT:
        fputs(LOOP_STR,         stdout);

        fputs(LONG_OPT_MARK,    stdout);
        fputs(INIT_LOPT_STR,    stdout);
        emit_long(opts);
        printf(LOPT_ARG_FMT,    opts->pzPROGNAME);
        fputs(END_OPT_SEL_STR,  stdout);

        fputs(FLAG_OPT_MARK,    stdout);
        fputs(INIT_OPT_STR,     stdout);
        emit_flag(opts);
        printf(OPT_ARG_FMT,     opts->pzPROGNAME);
        fputs(END_OPT_SEL_STR,  stdout);

        fputs(NOT_FOUND_STR,    stdout);
        break;
    }

    emit_wrapup(opts);
    if ((script_trailer != NULL) && (*script_trailer != NUL))
        fputs(script_trailer, stdout);
    else if (ENABLED_GENSHELL_OPT(SHELL))
        printf(SHOW_PROG_ENV, opts->pzPROGNAME);

#ifdef HAVE_FCHMOD
    fchmod(STDOUT_FILENO, 0755);
#endif
    fclose(stdout);

    if (ferror(stdout))
        fserr_exit(opts->pzProgName, zwriting, zstdout_name);

    AGFREE(script_text);
    script_leader    = NULL;
    script_trailer   = NULL;
    script_text      = NULL;
}

#ifdef HAVE_WORKING_FORK
/**
 * Print the value of "var" to a file descriptor.
 * The "fdin" is the read end of a pipe to a forked process that
 * is writing usage text to it.  We read that text in and re-emit
 * to standard out, formatting it so that it is assigned to a
 * shell variable.
 *
 * @param[in] prog  The capitalized, c-variable-formatted program name
 * @param[in] var   a similarly formatted type name
 *                  (LONGUSAGE, USAGE or VERSION)
 * @param[in] fdin  the input end of a pipe
 */
static void
emit_var_text(char const * prog, char const * var, int fdin)
{
    FILE * fp   = fdopen(fdin, "r" FOPEN_BINARY_FLAG);
    int    nlct = 0; /* defer newlines and skip trailing ones */

    printf(SET_TEXT_FMT, prog, var);
    if (fp == NULL)
        goto skip_text;

    for (;;) {
        int  ch = fgetc(fp);
        switch (ch) {

        case NL:
            nlct++;
            break;

        case '\'':
            while (nlct > 0) {
                fputc(NL, stdout);
                nlct--;
            }
            fputs(apostrophe, stdout);
            break;

        case EOF:
            goto done;

        default:
            while (nlct > 0) {
                fputc(NL, stdout);
                nlct--;
            }
            fputc(ch, stdout);
            break;
        }
    } done:;

    fclose(fp);

 skip_text:

    fputs(END_SET_TEXT, stdout);
}
#endif

/**
 *  The purpose of this function is to assign "long usage", short usage
 *  and version information to a shell variable.  Rather than wind our
 *  way through all the logic necessary to emit the text directly, we
 *  fork(), have our child process emit the text the normal way and
 *  capture the output in the parent process.
 *
 * @param[in] opts  the program options
 * @param[in] which what to print: long usage, usage or version
 * @param[in] od    for TT_VERSION, it is the version option
 */
static void
text_to_var(tOptions * opts, teTextTo which, tOptDesc * od)
{
#   define _TT_(n) static char const z ## n [] = #n;
    TEXTTO_TABLE
#   undef _TT_
#   define _TT_(n) z ## n ,
      static char const * ttnames[] = { TEXTTO_TABLE };
#   undef _TT_

#if ! defined(HAVE_WORKING_FORK)
    printf(SET_NO_TEXT_FMT, opts->pzPROGNAME, ttnames[which]);
#else
    int  fdpair[2];

    fflush(stdout);
    fflush(stderr);

    if (pipe(fdpair) != 0)
        fserr_exit(opts->pzProgName, "pipe", zinter_proc_pipe);

    switch (fork()) {
    case -1:
        fserr_exit(opts->pzProgName, "fork", opts->pzProgName);
        /* NOTREACHED */

    case 0:
        /*
         * Send both stderr and stdout to the pipe.  No matter which
         * descriptor is used, we capture the output on the read end.
         */
        dup2(fdpair[1], STDERR_FILENO);
        dup2(fdpair[1], STDOUT_FILENO);
        close(fdpair[0]);

        switch (which) {
        case TT_LONGUSAGE:
            (*(opts->pUsageProc))(opts, EXIT_SUCCESS);
            /* NOTREACHED */

        case TT_USAGE:
            (*(opts->pUsageProc))(opts, EXIT_FAILURE);
            /* NOTREACHED */

        case TT_VERSION:
            if (od->fOptState & OPTST_ALLOC_ARG) {
                AGFREE(od->optArg.argString);
                od->fOptState &= ~OPTST_ALLOC_ARG;
            }
            od->optArg.argString = "c";
            optionPrintVersion(opts, od);
            /* NOTREACHED */

        default:
            option_exits(EXIT_FAILURE);
            /* NOTREACHED */
        }
        /* NOTREACHED */

    default:
        close(fdpair[1]);
    }

    emit_var_text(opts->pzPROGNAME, ttnames[which], fdpair[0]);
#endif
}

/**
 * capture usage text in shell variables.
 * 
 */
static void
emit_usage(tOptions * opts)
{
    char tm_nm_buf[AO_NAME_SIZE];

    /*
     *  First, switch stdout to the output file name.
     *  Then, change the program name to the one defined
     *  by the definitions (rather than the current
     *  executable name).  Down case the upper cased name.
     */
    if (script_leader != NULL)
        fputs(script_leader, stdout);

    {
        char const * out_nm;

        {
            time_t    c_tim = time(NULL);
            struct tm * ptm = localtime(&c_tim);
            strftime(tm_nm_buf, AO_NAME_SIZE, TIME_FMT, ptm );
        }

        if (HAVE_GENSHELL_OPT(SCRIPT))
             out_nm = GENSHELL_OPT_ARG(SCRIPT);
        else out_nm = STDOUT;

        if ((script_leader == NULL) && (shell_prog != NULL))
            printf(SHELL_MAGIC, shell_prog);

        printf(PREAMBLE_FMT, START_MARK, out_nm, tm_nm_buf);
    }

    printf(END_PRE_FMT, opts->pzPROGNAME);

    /*
     *  Get a copy of the original program name in lower case and
     *  fill in an approximation of the program name from it.
     */
    {
        char *       pzPN = tm_nm_buf;
        char const * pz   = opts->pzPROGNAME;
        char **      pp;

        /* Copy the program name into the time/name buffer */
        for (;;) {
            if ((*pzPN++ = (char)tolower((unsigned char)*pz++)) == NUL)
                break;
        }

        pp  = VOIDP(&(opts->pzProgPath));
        *pp = tm_nm_buf;
        pp  = VOIDP(&(opts->pzProgName));
        *pp = tm_nm_buf;
    }

    text_to_var(opts, TT_LONGUSAGE, NULL);
    text_to_var(opts, TT_USAGE,     NULL);

    {
        tOptDesc * pOptDesc = opts->pOptDesc;
        int        optionCt = opts->optCt;

        for (;;) {
            if (pOptDesc->pOptProc == optionPrintVersion) {
                text_to_var(opts, TT_VERSION, pOptDesc);
                break;
            }

            if (--optionCt <= 0)
                break;
            pOptDesc++;
        }
    }
}

static void
emit_wrapup(tOptions * opts)
{
    tOptDesc *   od     = opts->pOptDesc;
    int          opt_ct = opts->presetOptCt;
    char const * fmt;

    printf(FINISH_LOOP, opts->pzPROGNAME);
    for (;opt_ct > 0; od++, --opt_ct) {
        /*
         *  Options that are either usage documentation or are compiled out
         *  are not to be processed.
         */
        if (SKIP_OPT(od) || (od->pz_NAME == NULL))
            continue;

        /*
         *  do not presence check if there is no minimum/must-set
         */
        if ((od->optMinCt == 0) && ((od->fOptState & OPTST_MUST_SET) == 0))
            continue;

        if (od->optMaxCt > 1)
             fmt = CHK_MIN_COUNT;
        else fmt = CHK_ONE_REQUIRED;

        {
            int min = (od->optMinCt == 0) ? 1 : od->optMinCt;
            printf(fmt, opts->pzPROGNAME, od->pz_NAME, min);
        }
    }
    fputs(END_MARK, stdout);
}

static void
emit_setup(tOptions * opts)
{
    tOptDesc *   od     = opts->pOptDesc;
    int          opt_ct = opts->presetOptCt;
    char const * fmt;
    char const * def_val;

    for (;opt_ct > 0; od++, --opt_ct) {
        char int_val_buf[32];

        /*
         *  Options that are either usage documentation or are compiled out
         *  are not to be processed.
         */
        if (SKIP_OPT(od) || (od->pz_NAME == NULL))
            continue;

        if (od->optMaxCt > 1)
             fmt = MULTI_DEF_FMT;
        else fmt = SGL_DEF_FMT;

        /*
         *  IF this is an enumeration/bitmask option, then convert the value
         *  to a string before printing the default value.
         */
        switch (OPTST_GET_ARGTYPE(od->fOptState)) {
        case OPARG_TYPE_ENUMERATION:
            (*(od->pOptProc))(OPTPROC_EMIT_SHELL, od );
            def_val = od->optArg.argString;
            break;

        /*
         *  Numeric and membership bit options are just printed as a number.
         */
        case OPARG_TYPE_NUMERIC:
            snprintf(int_val_buf, sizeof(int_val_buf), "%d",
                     (int)od->optArg.argInt);
            def_val = int_val_buf;
            break;

        case OPARG_TYPE_MEMBERSHIP:
            snprintf(int_val_buf, sizeof(int_val_buf), "%lu",
                     (unsigned long)od->optArg.argIntptr);
            def_val = int_val_buf;
            break;

        case OPARG_TYPE_BOOLEAN:
            def_val = (od->optArg.argBool) ? TRUE_STR : FALSE_STR;
            break;

        default:
            if (od->optArg.argString == NULL) {
                if (fmt == SGL_DEF_FMT)
                    fmt = SGL_NO_DEF_FMT;
                def_val = NULL;
            }
            else
                def_val = od->optArg.argString;
        }

        printf(fmt, opts->pzPROGNAME, od->pz_NAME, def_val);
    }
}

static void
emit_action(tOptions * opts, tOptDesc * od)
{
    if (od->pOptProc == optionPrintVersion)
        printf(ECHO_N_EXIT, opts->pzPROGNAME, VER_STR);

    else if (od->pOptProc == optionPagedUsage)
        printf(PAGE_USAGE_TEXT, opts->pzPROGNAME);

    else if (od->pOptProc == optionLoadOpt) {
        printf(LVL3_CMD, NO_LOAD_WARN);
        printf(LVL3_CMD, YES_NEED_OPT_ARG);

    } else if (od->pz_NAME == NULL) {

        if (od->pOptProc == NULL) {
            printf(LVL3_CMD, NO_SAVE_OPTS);
            printf(LVL3_CMD, OK_NEED_OPT_ARG);
        } else
            printf(ECHO_N_EXIT, opts->pzPROGNAME, LONG_USE_STR);

    } else {
        if (od->optMaxCt == 1)
            printf(SGL_ARG_FMT, opts->pzPROGNAME, od->pz_NAME);
        else {
            if ((unsigned)od->optMaxCt < NOLIMIT)
                printf(CHK_MAX_COUNT, opts->pzPROGNAME,
                       od->pz_NAME, od->optMaxCt);

            printf(MULTI_ARG_FMT, opts->pzPROGNAME, od->pz_NAME);
        }

        /*
         *  Fix up the args.
         */
        if (OPTST_GET_ARGTYPE(od->fOptState) == OPARG_TYPE_NONE) {
            printf(SET_MULTI_ARG, opts->pzPROGNAME, od->pz_NAME);
            printf(LVL3_CMD, NO_ARG_NEEDED);

        } else if (od->fOptState & OPTST_ARG_OPTIONAL) {
            printf(SET_MULTI_ARG,  opts->pzPROGNAME, od->pz_NAME);
            printf(LVL3_CMD, OK_NEED_OPT_ARG);

        } else {
            printf(LVL3_CMD, YES_NEED_OPT_ARG);
        }
    }
    fputs(zOptionEndSelect, stdout);
}

static void
emit_inaction(tOptions * opts, tOptDesc * od)
{
    if (od->pOptProc == optionLoadOpt) {
        printf(LVL3_CMD, NO_SUPPRESS_LOAD);

    } else if (od->optMaxCt == 1)
        printf(NO_SGL_ARG_FMT, opts->pzPROGNAME,
               od->pz_NAME, od->pz_DisablePfx);
    else
        printf(NO_MULTI_ARG_FMT, opts->pzPROGNAME,
               od->pz_NAME, od->pz_DisablePfx);

    printf(LVL3_CMD, NO_ARG_NEEDED);
    fputs(zOptionEndSelect, stdout);
}

/**
 * recognize flag options.  These go at the end.
 * At the end, emit code to handle options we don't recognize.
 *
 * @param[in] opts  the program options
 */
static void
emit_flag(tOptions * opts)
{
    tOptDesc * od = opts->pOptDesc;
    int        opt_ct = opts->optCt;

    fputs(zOptionCase, stdout);

    for (;opt_ct > 0; od++, --opt_ct) {

        if (SKIP_OPT(od) || ! IS_GRAPHIC_CHAR(od->optValue))
            continue;

        printf(zOptionFlag, od->optValue);
        emit_action(opts, od);
    }
    printf(UNK_OPT_FMT, FLAG_STR, opts->pzPROGNAME);
}

/**
 *  Emit the match text for a long option.  The passed in \a name may be
 *  either the enablement name or the disablement name.
 *
 * @param[in] name  The current name to check.
 * @param[in] cod   current option descriptor
 * @param[in] opts  the program options
 */
static void
emit_match_expr(char const * name, tOptDesc * cod, tOptions * opts)
{
    char name_bf[32];
    unsigned int    min_match_ct = 2;
    unsigned int    max_match_ct = strlen(name) - 1;

    if (max_match_ct >= sizeof(name_bf) - 1)
        goto leave;
    
    {
        tOptDesc *  od = opts->pOptDesc;
        int         ct = opts->optCt;

        for (; ct-- > 0; od++) {
            unsigned int match_ct = 0;

            /*
             *  Omit the current option, Doc opts and compiled out opts.
             */
            if ((od == cod) || SKIP_OPT(od))
                continue;

            /*
             *  Check each character of the name case insensitively.
             *  They must not be the same.  They cannot be, because it would
             *  not compile correctly if they were.
             */
            while (UPPER(od->pz_Name[match_ct]) == UPPER(name[match_ct]))
                match_ct++;

            if (match_ct > min_match_ct)
                min_match_ct = match_ct;

            /*
             *  Check the disablement name, too.
             */
            if (od->pz_DisableName == NULL)
                continue;

            match_ct = 0;
            while (  toupper((unsigned char)od->pz_DisableName[match_ct])
                  == toupper((unsigned char)name[match_ct]))
                match_ct++;
            if (match_ct > min_match_ct)
                min_match_ct = match_ct;
        }
    }

    /*
     *  Don't bother emitting partial matches if there is only one possible
     *  partial match.
     */
    if (min_match_ct < max_match_ct) {
        char *  pz    = name_bf + min_match_ct;
        int     nm_ix = min_match_ct;

        memcpy(name_bf, name, min_match_ct);

        for (;;) {
            *pz = NUL;
            printf(zOptionPartName, name_bf);
            *pz++ = name[nm_ix++];
            if (name[nm_ix] == NUL) {
                *pz = NUL;
                break;
            }
        }
    }

leave:
    printf(zOptionFullName, name);
}

/**
 *  Emit GNU-standard long option handling code.
 *
 * @param[in] opts  the program options
 */
static void
emit_long(tOptions * opts)
{
    tOptDesc * od = opts->pOptDesc;
    int        ct  = opts->optCt;

    fputs(zOptionCase, stdout);

    /*
     *  do each option, ...
     */
    do  {
        /*
         *  Documentation & compiled-out options
         */
        if (SKIP_OPT(od))
            continue;

        emit_match_expr(od->pz_Name, od, opts);
        emit_action(opts, od);

        /*
         *  Now, do the same thing for the disablement version of the option.
         */
        if (od->pz_DisableName != NULL) {
            emit_match_expr(od->pz_DisableName, od, opts);
            emit_inaction(opts, od);
        }
    } while (od++, --ct > 0);

    printf(UNK_OPT_FMT, OPTION_STR, opts->pzPROGNAME);
}

/**
 * Load the previous shell script output file.  We need to preserve any
 * hand-edited additions outside of the START_MARK and END_MARKs.
 *
 * @param[in] fname  the output file name
 */
static char *
load_old_output(char const * fname, char const * pname)
{
    /*
     *  IF we cannot stat the file,
     *  THEN assume we are creating a new file.
     *       Skip the loading of the old data.
     */
    FILE * fp = fopen(fname, "r" FOPEN_BINARY_FLAG);
    struct stat stbf;
    char * text;
    char * scan;

    if (fp == NULL)
        return NULL;

    /*
     * If we opened it, we should be able to stat it and it needs
     * to be a regular file
     */
    if ((fstat(fileno(fp), &stbf) != 0) || (! S_ISREG(stbf.st_mode)))
        fserr_exit(pname, "fstat", fname);

    scan = text = AGALOC(stbf.st_size + 1, "f data");

    /*
     *  Read in all the data as fast as our OS will let us.
     */
    for (;;) {
        size_t inct = fread(VOIDP(scan), 1, (size_t)stbf.st_size, fp);
        if (inct == 0)
            break;

        stbf.st_size -= (ssize_t)inct;

        if (stbf.st_size == 0)
            break;

        scan += inct;
    }

    *scan = NUL;
    fclose(fp);

    return text;
}

/**
 * Open the specified output file.  If it already exists, load its
 * contents and save the non-generated (hand edited) portions.
 * If a "start mark" is found, everything before it is preserved leader.
 * If not, the entire thing is a trailer.  Assuming the start is found,
 * then everything after the end marker is the trailer.  If the end
 * mark is not found, the file is actually corrupt, but we take the
 * remainder to be the trailer.
 *
 * @param[in] fname  the output file name
 */
static void
open_out(char const * fname, char const * pname)
{

    do  {
        char * txt = script_text = load_old_output(fname, pname);
        char * scn;

        if (txt == NULL)
            break;

        scn = strstr(txt, START_MARK);
        if (scn == NULL) {
            script_trailer = txt;
            break;
        }

        *(scn++) = NUL;
        scn = strstr(scn, END_MARK);
        if (scn == NULL) {
            /*
             * The file is corrupt.  Set the trailer to be everything
             * after the start mark. The user will need to fix it up.
             */
            script_trailer = txt + strlen(txt) + START_MARK_LEN + 1;
            break;
        }

        /*
         *  Check to see if the data contains our marker.
         *  If it does, then we will skip over it
         */
        script_trailer = scn + END_MARK_LEN;
        script_leader  = txt;
    } while (false);

    if (freopen(fname, "w" FOPEN_BINARY_FLAG, stdout) != stdout)
        fserr_exit(pname, "freopen", fname);
}

/*=export_func genshelloptUsage
 * private:
 * what: The usage function for the genshellopt generated program
 *
 * arg:  + tOptions * + opts    + program options descriptor +
 * arg:  + int        + exit_cd + usage text type to produce +
 *
 * doc:
 *  This function is used to create the usage strings for the option
 *  processing shell script code.  Two child processes are spawned
 *  each emitting the usage text in either the short (error exit)
 *  style or the long style.  The generated program will capture this
 *  and create shell script variables containing the two types of text.
=*/
void
genshelloptUsage(tOptions * opts, int exit_cd)
{
#if ! defined(HAVE_WORKING_FORK)
    optionUsage(opts, exit_cd);
#else
    /*
     *  IF not EXIT_SUCCESS,
     *  THEN emit the short form of usage.
     */
    if (exit_cd != EXIT_SUCCESS)
        optionUsage(opts, exit_cd);
    fflush(stderr);
    fflush(stdout);
    if (ferror(stdout) || ferror(stderr))
        option_exits(EXIT_FAILURE);

    option_usage_fp = stdout;

    /*
     *  First, print our usage
     */
    switch (fork()) {
    case -1:
        optionUsage(opts, EXIT_FAILURE);
        /* NOTREACHED */

    case 0:
        pagerState = PAGER_STATE_CHILD;
        optionUsage(opts, EXIT_SUCCESS);
        /* NOTREACHED */
        _exit(EXIT_FAILURE);

    default:
    {
        int  sts;
        wait(&sts);
    }
    }

    /*
     *  Generate the pzProgName, since optionProcess() normally
     *  gets it from the command line
     */
    {
        char *  pz;
        char ** pp = VOIDP(&(optionParseShellOptions->pzProgName));
        AGDUPSTR(pz, optionParseShellOptions->pzPROGNAME, "prog name");
        *pp = pz;
        while (*pz != NUL) {
            *pz = (char)LOWER(*pz);
            pz++;
        }
    }

    /*
     *  Separate the makeshell usage from the client usage
     */
    fprintf(option_usage_fp, zGenshell, optionParseShellOptions->pzProgName);
    fflush(option_usage_fp);

    /*
     *  Now, print the client usage.
     */
    switch (fork()) {
    case 0:
        pagerState = PAGER_STATE_CHILD;
        /*FALLTHROUGH*/
    case -1:
        optionUsage(optionParseShellOptions, EXIT_FAILURE);

    default:
    {
        int  sts;
        wait(&sts);
    }
    }

    fflush(stdout);
    if (ferror(stdout))
        fserr_exit(opts->pzProgName, zwriting, zstdout_name);

    option_exits(EXIT_SUCCESS);
#endif
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/makeshell.c */
