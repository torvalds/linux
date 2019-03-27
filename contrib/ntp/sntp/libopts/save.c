
/*
 * \file save.c
 *
 *  This module's routines will take the currently set options and
 *  store them into an ".rc" file for re-interpretation the next
 *  time the invoking program is run.
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
static char const *
find_dir_name(tOptions * opts, int * p_free);

static char const *
find_file_name(tOptions * opts, int * p_free_name);

static void
prt_entry(FILE * fp, tOptDesc * od, char const * l_arg);

static void
prt_value(FILE * fp, int depth, tOptDesc * pOD, tOptionValue const * ovp);

static void
prt_string(FILE * fp, char const * name, char const * pz);

static void
prt_val_list(FILE * fp, char const * name, tArgList * al);

static void
prt_nested(FILE * fp, tOptDesc * p);

static FILE *
open_sv_file(tOptions * opts);

static void
prt_no_arg_opt(FILE * fp, tOptDesc * p, tOptDesc * pOD);

static void
prt_str_arg(FILE * fp, tOptDesc * pOD);

static void
prt_enum_arg(FILE * fp, tOptDesc * od);

static void
prt_set_arg(FILE * fp, tOptDesc * od);

static void
prt_file_arg(FILE * fp, tOptDesc * od, tOptions * opts);
/* = = = END-STATIC-FORWARD = = = */

/**
 */
static char const *
find_dir_name(tOptions * opts, int * p_free)
{
    char const * pzDir;

    if (  (opts->specOptIdx.save_opts == NO_EQUIVALENT)
       || (opts->specOptIdx.save_opts == 0))
        return NULL;

    pzDir = opts->pOptDesc[ opts->specOptIdx.save_opts ].optArg.argString;
    if ((pzDir != NULL) && (*pzDir != NUL))
        return pzDir;

    /*
     *  This function only works if there is a directory where
     *  we can stash the RC (INI) file.
     */
    {
        char const * const * papz = opts->papzHomeList;
        if (papz == NULL)
            return NULL;

        while (papz[1] != NULL) papz++;
        pzDir = *papz;
    }

    /*
     *  IF it does not require deciphering an env value, then just copy it
     */
    if (*pzDir != '$')
        return pzDir;

    {
        char const * pzEndDir = strchr(++pzDir, DIRCH);
        char * pzFileName;
        char * pzEnv;

        if (pzEndDir != NULL) {
            char z[ AO_NAME_SIZE ];
            if ((pzEndDir - pzDir) > AO_NAME_LIMIT )
                return NULL;
            memcpy(z, pzDir, (size_t)(pzEndDir - pzDir));
            z[pzEndDir - pzDir] = NUL;
            pzEnv = getenv(z);
        } else {

            /*
             *  Make sure we can get the env value (after stripping off
             *  any trailing directory or file names)
             */
            pzEnv = getenv(pzDir);
        }

        if (pzEnv == NULL) {
            fprintf(stderr, zsave_warn, opts->pzProgName);
            fprintf(stderr, zNotDef, pzDir);
            return NULL;
        }

        if (pzEndDir == NULL)
            return pzEnv;

        {
            size_t sz = strlen(pzEnv) + strlen(pzEndDir) + 2;
            pzFileName = (char *)AGALOC(sz, "dir name");
        }

        if (pzFileName == NULL)
            return NULL;

        *p_free = 1;
        /*
         *  Glue together the full name into the allocated memory.
         *  FIXME: We lose track of this memory.
         */
        sprintf(pzFileName, "%s/%s", pzEnv, pzEndDir);
        return pzFileName;
    }
}

/**
 */
static char const *
find_file_name(tOptions * opts, int * p_free_name)
{
    struct stat stBuf;
    int    free_dir_name = 0;

    char const * pzDir = find_dir_name(opts, &free_dir_name);
    if (pzDir == NULL)
        return NULL;

    /*
     *  See if we can find the specified directory.  We use a once-only loop
     *  structure so we can bail out early.
     */
    if (stat(pzDir, &stBuf) != 0) do {
        char z[AG_PATH_MAX];
        char * dirchp;

        /*
         *  IF we could not, check to see if we got a full
         *  path to a file name that has not been created yet.
         */
        if (errno != ENOENT) {
        bogus_name:
            fprintf(stderr, zsave_warn, opts->pzProgName);
            fprintf(stderr, zNoStat, errno, strerror(errno), pzDir);
            if (free_dir_name)
                AGFREE(pzDir);
            return NULL;
        }

        /*
         *  Strip off the last component, stat the remaining string and
         *  that string must name a directory
         */
        dirchp = strrchr(pzDir, DIRCH);
        if (dirchp == NULL) {
            stBuf.st_mode = S_IFREG;
            break; /* found directory -- viz.,  "." */
        }

        if ((size_t)(dirchp - pzDir) >= sizeof(z))
            goto bogus_name;

        memcpy(z, pzDir, (size_t)(dirchp - pzDir));
        z[dirchp - pzDir] = NUL;

        if ((stat(z, &stBuf) != 0) || ! S_ISDIR(stBuf.st_mode))
            goto bogus_name;
        stBuf.st_mode = S_IFREG; /* file within this directory */
    } while (false);

    /*
     *  IF what we found was a directory,
     *  THEN tack on the config file name
     */
    if (S_ISDIR(stBuf.st_mode)) {
        size_t sz = strlen(pzDir) + strlen(opts->pzRcName) + 2;

        {
            char * pzPath = (char *)AGALOC(sz, "file name");
#ifdef HAVE_SNPRINTF
            snprintf(pzPath, sz, "%s/%s", pzDir, opts->pzRcName);
#else
            sprintf(pzPath, "%s/%s", pzDir, opts->pzRcName);
#endif
            if (free_dir_name)
                AGFREE(pzDir);
            pzDir = pzPath;
            free_dir_name = 1;
        }

        /*
         *  IF we cannot stat the object for any reason other than
         *     it does not exist, then we bail out
         */
        if (stat(pzDir, &stBuf) != 0) {
            if (errno != ENOENT) {
                fprintf(stderr, zsave_warn, opts->pzProgName);
                fprintf(stderr, zNoStat, errno, strerror(errno),
                        pzDir);
                AGFREE(pzDir);
                return NULL;
            }

            /*
             *  It does not exist yet, but it will be a regular file
             */
            stBuf.st_mode = S_IFREG;
        }
    }

    /*
     *  Make sure that whatever we ultimately found, that it either is
     *  or will soon be a file.
     */
    if (! S_ISREG(stBuf.st_mode)) {
        fprintf(stderr, zsave_warn, opts->pzProgName, pzDir);
        if (free_dir_name)
            AGFREE(pzDir);
        return NULL;
    }

    /*
     *  Get rid of the old file
     */
    unlink(pzDir);
    *p_free_name = free_dir_name;
    return pzDir;
}

/**
 * print one option entry to the save file.
 *
 * @param[in] fp    the file pointer for the save file
 * @param[in] od    the option descriptor to print
 * @param[in] l_arg the last argument for the option
 */
static void
prt_entry(FILE * fp, tOptDesc * od, char const * l_arg)
{
    int space_ct;

    /*
     *  There is an argument.  Pad the name so values line up.
     *  Not disabled *OR* this got equivalenced to another opt,
     *  then use current option name.
     *  Otherwise, there must be a disablement name.
     */
    {
        char const * pz =
            (! DISABLED_OPT(od) || (od->optEquivIndex != NO_EQUIVALENT))
            ? od->pz_Name
            : od->pz_DisableName;
        space_ct = 17 - strlen(pz);
        fputs(pz, fp);
    }

    if (  (l_arg == NULL)
       && (OPTST_GET_ARGTYPE(od->fOptState) != OPARG_TYPE_NUMERIC))
        goto end_entry;

    fputs(" = ", fp);
    while (space_ct-- > 0)  fputc(' ', fp);

    /*
     *  IF the option is numeric only,
     *  THEN the char pointer is really the number
     */
    if (OPTST_GET_ARGTYPE(od->fOptState) == OPARG_TYPE_NUMERIC)
        fprintf(fp, "%d", (int)(intptr_t)l_arg);

    else {
        for (;;) {
            char const * eol = strchr(l_arg, NL);

            /*
             *  IF this is the last line
             *  THEN bail and print it
             */
            if (eol == NULL)
                break;

            /*
             *  Print the continuation and the text from the current line
             */
            (void)fwrite(l_arg, (size_t)(eol - l_arg), (size_t)1, fp);
            l_arg = eol+1; /* advance the Last Arg pointer */
            fputs("\\\n", fp);
        }

        /*
         *  Terminate the entry
         */
        fputs(l_arg, fp);
    }

end_entry:
    fputc(NL, fp);
}

/**
 */
static void
prt_value(FILE * fp, int depth, tOptDesc * pOD, tOptionValue const * ovp)
{
    while (--depth >= 0)
        putc(' ', fp), putc(' ', fp);

    switch (ovp->valType) {
    default:
    case OPARG_TYPE_NONE:
        fprintf(fp, NULL_ATR_FMT, ovp->pzName);
        break;

    case OPARG_TYPE_STRING:
        prt_string(fp, ovp->pzName, ovp->v.strVal);
        break;

    case OPARG_TYPE_ENUMERATION:
    case OPARG_TYPE_MEMBERSHIP:
        if (pOD != NULL) {
            uint32_t  opt_state = pOD->fOptState;
            uintptr_t val = pOD->optArg.argEnum;
            char const * typ = (ovp->valType == OPARG_TYPE_ENUMERATION)
                ? "keyword" : "set-membership";

            fprintf(fp, TYPE_ATR_FMT, ovp->pzName, typ);

            /*
             *  This is a magic incantation that will convert the
             *  bit flag values back into a string suitable for printing.
             */
            (*(pOD->pOptProc))(OPTPROC_RETURN_VALNAME, pOD );
            if (pOD->optArg.argString != NULL) {
                fputs(pOD->optArg.argString, fp);

                if (ovp->valType != OPARG_TYPE_ENUMERATION) {
                    /*
                     *  set membership strings get allocated
                     */
                    AGFREE(pOD->optArg.argString);
                }
            }

            pOD->optArg.argEnum = val;
            pOD->fOptState = opt_state;
            fprintf(fp, END_XML_FMT, ovp->pzName);
            break;
        }
        /* FALLTHROUGH */

    case OPARG_TYPE_NUMERIC:
        fprintf(fp, NUMB_ATR_FMT, ovp->pzName, ovp->v.longVal);
        break;

    case OPARG_TYPE_BOOLEAN:
        fprintf(fp, BOOL_ATR_FMT, ovp->pzName,
                ovp->v.boolVal ? "true" : "false");
        break;

    case OPARG_TYPE_HIERARCHY:
        prt_val_list(fp, ovp->pzName, ovp->v.nestVal);
        break;
    }
}

/**
 */
static void
prt_string(FILE * fp, char const * name, char const * pz)
{
    fprintf(fp, OPEN_XML_FMT, name);
    for (;;) {
        int ch = ((int)*(pz++)) & 0xFF;

        switch (ch) {
        case NUL: goto string_done;

        case '&':
        case '<':
        case '>':
#if __GNUC__ >= 4
        case 1 ... (' ' - 1):
        case ('~' + 1) ... 0xFF:
#endif
            emit_special_char(fp, ch);
            break;

        default:
#if __GNUC__ < 4
            if (  ((ch >= 1) && (ch <= (' ' - 1)))
               || ((ch >= ('~' + 1)) && (ch <= 0xFF)) ) {
                emit_special_char(fp, ch);
                break;
            }
#endif
            putc(ch, fp);
        }
    } string_done:;
    fprintf(fp, END_XML_FMT, name);
}

/**
 */
static void
prt_val_list(FILE * fp, char const * name, tArgList * al)
{
    static int depth = 1;

    int sp_ct;
    int opt_ct;
    void ** opt_list;

    if (al == NULL)
        return;
    opt_ct   = al->useCt;
    opt_list = VOIDP(al->apzArgs);

    if (opt_ct <= 0) {
        fprintf(fp, OPEN_CLOSE_FMT, name);
        return;
    }

    fprintf(fp, NESTED_OPT_FMT, name);

    depth++;
    while (--opt_ct >= 0) {
        tOptionValue const * ovp = *(opt_list++);

        prt_value(fp, depth, NULL, ovp);
    }
    depth--;

    for (sp_ct = depth; --sp_ct >= 0;)
        putc(' ', fp), putc(' ', fp);
    fprintf(fp, "</%s>\n", name);
}

/**
 */
static void
prt_nested(FILE * fp, tOptDesc * p)
{
    int opt_ct;
    tArgList * al = p->optCookie;
    void ** opt_list;

    if (al == NULL)
        return;

    opt_ct   = al->useCt;
    opt_list = VOIDP(al->apzArgs);

    if (opt_ct <= 0)
        return;

    do  {
        tOptionValue const * base = *(opt_list++);
        tOptionValue const * ovp = optionGetValue(base, NULL);

        if (ovp == NULL)
            continue;

        fprintf(fp, NESTED_OPT_FMT, p->pz_Name);

        do  {
            prt_value(fp, 1, p, ovp);

        } while (ovp = optionNextValue(base, ovp),
                 ovp != NULL);

        fprintf(fp, "</%s>\n", p->pz_Name);
    } while (--opt_ct > 0);
}

/**
 * open the file for saving option state.
 *
 * @param[in] opts  the program options structure
 * @returns the open file pointer.  It may be NULL.
 */
static FILE *
open_sv_file(tOptions * opts)
{
    FILE * fp;

    {
        int   free_name = 0;
        char const * pzFName = find_file_name(opts, &free_name);
        if (pzFName == NULL)
            return NULL;

        fp = fopen(pzFName, "w" FOPEN_BINARY_FLAG);
        if (fp == NULL) {
            fprintf(stderr, zsave_warn, opts->pzProgName);
            fprintf(stderr, zNoCreat, errno, strerror(errno), pzFName);
            if (free_name)
                AGFREE(pzFName);
            return fp;
        }

        if (free_name)
            AGFREE(pzFName);
    }

    fputs("#  ", fp);
    {
        char const * e = strchr(opts->pzUsageTitle, NL);
        if (e++ != NULL)
            fwrite(opts->pzUsageTitle, 1, e - opts->pzUsageTitle, fp);
    }

    {
        time_t  cur_time = time(NULL);
        char *  time_str = ctime(&cur_time);

        fprintf(fp, zPresetFile, time_str);
#ifdef HAVE_ALLOCATED_CTIME
        /*
         *  The return values for ctime(), localtime(), and gmtime()
         *  normally point to static data that is overwritten by each call.
         *  The test to detect allocated ctime, so we leak the memory.
         */
        AGFREE(time_str);
#endif
    }

    return fp;
}

/**
 */
static void
prt_no_arg_opt(FILE * fp, tOptDesc * p, tOptDesc * pOD)
{
    /*
     * The aliased to argument indicates whether or not the option
     * is "disabled".  However, the original option has the name
     * string, so we get that there, not with "p".
     */
    char const * pznm =
        (DISABLED_OPT(p)) ? pOD->pz_DisableName : pOD->pz_Name;
    /*
     *  If the option was disabled and the disablement name is NULL,
     *  then the disablement was caused by aliasing.
     *  Use the name as the string to emit.
     */
    if (pznm == NULL)
        pznm = pOD->pz_Name;

    fprintf(fp, "%s\n", pznm);
}

/**
 */
static void
prt_str_arg(FILE * fp, tOptDesc * pOD)
{
    if (pOD->fOptState & OPTST_STACKED) {
        tArgList * pAL = (tArgList *)pOD->optCookie;
        int        uct = pAL->useCt;
        char const ** ppz = pAL->apzArgs;

        /*
         *  un-disable multiple copies of disabled options.
         */
        if (uct > 1)
            pOD->fOptState &= ~OPTST_DISABLED;

        while (uct-- > 0)
            prt_entry(fp, pOD, *(ppz++));
    } else {
        prt_entry(fp, pOD, pOD->optArg.argString);
    }
}

/**
 * print the string value of an enumeration.
 *
 * @param[in] fp  the file pointer to write to
 * @param[in] od  the option descriptor with the enumerated value
 */
static void
prt_enum_arg(FILE * fp, tOptDesc * od)
{
    uintptr_t val = od->optArg.argEnum;

    /*
     *  This is a magic incantation that will convert the
     *  bit flag values back into a string suitable for printing.
     */
    (*(od->pOptProc))(OPTPROC_RETURN_VALNAME, od);
    prt_entry(fp, od, VOIDP(od->optArg.argString));

    od->optArg.argEnum = val;
}

/**
 * Print the bits set in a bit mask option.
 * We call the option handling function with a magic value for
 * the options pointer and it allocates and fills in the string.
 * We print that with a call to prt_entry().
 *
 * @param[in] fp  the file pointer to write to
 * @param[in] od  the option descriptor with a bit mask value type
 */
static void
prt_set_arg(FILE * fp, tOptDesc * od)
{
    char * list = optionMemberList(od);
    size_t len  = strlen(list);
    char * buf  = (char *)AGALOC(len + 3, "dir name");
    *buf= '=';
    memcpy(buf+1, list, len + 1);
    prt_entry(fp, od, buf);
    AGFREE(buf);
    AGFREE(list);
}

/**
 * figure out what the option file name argument is.
 * If one can be found, call prt_entry() to emit it.
 *
 * @param[in] fp   the file pointer to write to.
 * @param[in] od   the option descriptor with a bit mask value type
 * @param[in] opts the program options descriptor
 */
static void
prt_file_arg(FILE * fp, tOptDesc * od, tOptions * opts)
{
    /*
     *  If the cookie is not NULL, then it has the file name, period.
     *  Otherwise, if we have a non-NULL string argument, then....
     */
    if (od->optCookie != NULL)
        prt_entry(fp, od, od->optCookie);

    else if (HAS_originalOptArgArray(opts)) {
        char const * orig =
            opts->originalOptArgArray[od->optIndex].argString;

        if (od->optArg.argString == orig)
            return;

        prt_entry(fp, od, od->optArg.argString);
    }
}

/*=export_func  optionSaveFile
 *
 * what:  saves the option state to a file
 *
 * arg:   tOptions *,   opts,  program options descriptor
 *
 * doc:
 *
 * This routine will save the state of option processing to a file.  The name
 * of that file can be specified with the argument to the @code{--save-opts}
 * option, or by appending the @code{rcfile} attribute to the last
 * @code{homerc} attribute.  If no @code{rcfile} attribute was specified, it
 * will default to @code{.@i{programname}rc}.  If you wish to specify another
 * file, you should invoke the @code{SET_OPT_SAVE_OPTS(@i{filename})} macro.
 *
 * The recommend usage is as follows:
 * @example
 *    optionProcess(&progOptions, argc, argv);
 *    if (i_want_a_non_standard_place_for_this)
 *        SET_OPT_SAVE_OPTS("myfilename");
 *    optionSaveFile(&progOptions);
 * @end example
 *
 * err:
 *
 * If no @code{homerc} file was specified, this routine will silently return
 * and do nothing.  If the output file cannot be created or updated, a message
 * will be printed to @code{stderr} and the routine will return.
=*/
void
optionSaveFile(tOptions * opts)
{
    tOptDesc *  od;
    int         ct;
    FILE *      fp = open_sv_file(opts);

    if (fp == NULL)
        return;

    /*
     *  FOR each of the defined options, ...
     */
    ct = opts->presetOptCt;
    od = opts->pOptDesc;
    do  {
        tOptDesc * p;

        /*
         *  IF    the option has not been defined
         *     OR it does not take an initialization value
         *     OR it is equivalenced to another option
         *  THEN continue (ignore it)
         *
         *  Equivalenced options get picked up when the equivalenced-to
         *  option is processed.
         */
        if (UNUSED_OPT(od))
            continue;

        if ((od->fOptState & OPTST_DO_NOT_SAVE_MASK) != 0)
            continue;

        if (  (od->optEquivIndex != NO_EQUIVALENT)
           && (od->optEquivIndex != od->optIndex))
            continue;

        /*
         *  The option argument data are found at the equivalenced-to option,
         *  but the actual option argument type comes from the original
         *  option descriptor.  Be careful!
         */
        p = ((od->fOptState & OPTST_EQUIVALENCE) != 0)
            ? (opts->pOptDesc + od->optActualIndex) : od;

        switch (OPTST_GET_ARGTYPE(od->fOptState)) {
        case OPARG_TYPE_NONE:
            prt_no_arg_opt(fp, p, od);
            break;

        case OPARG_TYPE_NUMERIC:
            prt_entry(fp, p, VOIDP(p->optArg.argInt));
            break;

        case OPARG_TYPE_STRING:
            prt_str_arg(fp, p);
            break;

        case OPARG_TYPE_ENUMERATION:
            prt_enum_arg(fp, p);
            break;

        case OPARG_TYPE_MEMBERSHIP:
            prt_set_arg(fp, p);
            break;

        case OPARG_TYPE_BOOLEAN:
            prt_entry(fp, p, p->optArg.argBool ? "true" : "false");
            break;

        case OPARG_TYPE_HIERARCHY:
            prt_nested(fp, p);
            break;

        case OPARG_TYPE_FILE:
            prt_file_arg(fp, p, opts);
            break;

        default:
            break; /* cannot handle - skip it */
        }
    } while (od++, (--ct > 0));

    fclose(fp);
}
/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/save.c */
