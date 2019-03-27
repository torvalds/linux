/**
 * \file initialize.c
 *
 *  initialize the libopts data structures.
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
static tSuccess
do_presets(tOptions * opts);
/* = = = END-STATIC-FORWARD = = = */

/**
 *  Make sure the option descriptor is there and that we understand it.
 *  This should be called from any user entry point where one needs to
 *  worry about validity.  (Some entry points are free to assume that
 *  the call is not the first to the library and, thus, that this has
 *  already been called.)
 *
 *  Upon successful completion, pzProgName and pzProgPath are set.
 *
 *  @param[in,out] opts   program options descriptor
 *  @param[in]     pname  name of program, from argv[]
 *  @returns SUCCESS or FAILURE
 */
LOCAL tSuccess
validate_struct(tOptions * opts, char const * pname)
{
    if (opts == NULL) {
        fputs(zno_opt_arg, stderr);
        return FAILURE;
    }
    print_exit = ((opts->fOptSet & OPTPROC_SHELL_OUTPUT) != 0);

    /*
     *  IF the client has enabled translation and the translation procedure
     *  is available, then go do it.
     */
    if (  ((opts->fOptSet & OPTPROC_TRANSLATE) != 0)
       && (opts->pTransProc != NULL)
       && (option_xlateable_txt.field_ct != 0) ) {
        /*
         *  If option names are not to be translated at all, then do not do
         *  it for configuration parsing either.  (That is the bit that really
         *  gets tested anyway.)
         */
        if ((opts->fOptSet & OPTPROC_NO_XLAT_MASK) == OPTPROC_NXLAT_OPT)
            opts->fOptSet |= OPTPROC_NXLAT_OPT_CFG;
        opts->pTransProc();
    }

    /*
     *  IF the struct version is not the current, and also
     *     either too large (?!) or too small,
     *  THEN emit error message and fail-exit
     */
    if (  ( opts->structVersion  != OPTIONS_STRUCT_VERSION  )
       && (  (opts->structVersion > OPTIONS_STRUCT_VERSION  )
          || (opts->structVersion < OPTIONS_MINIMUM_VERSION )
       )  )  {
        fprintf(stderr, zwrong_ver, pname, NUM_TO_VER(opts->structVersion));
        if (opts->structVersion > OPTIONS_STRUCT_VERSION )
            fputs(ztoo_new, stderr);
        else
            fputs(ztoo_old, stderr);

        fwrite(ao_ver_string, sizeof(ao_ver_string) - 1, 1, stderr);
        return FAILURE;
    }

    /*
     *  If the program name hasn't been set, then set the name and the path
     *  and the set of equivalent characters.
     */
    if (opts->pzProgName == NULL) {
        char const *  pz = strrchr(pname, DIRCH);
        char const ** pp = VOIDP(&(opts->pzProgName));

        if (pz != NULL)
            *pp = pz+1;
        else
            *pp = pname;

        pz = pathfind(getenv("PATH"), pname, "rx");
        if (pz != NULL)
            pname = VOIDP(pz);

        pp  = (char const **)VOIDP(&(opts->pzProgPath));
        *pp = pname;

        /*
         *  when comparing long names, these are equivalent
         */
        strequate(zSepChars);
    }

    return SUCCESS;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *  DO PRESETS
 *
 *  The next several routines do the immediate action pass on the command
 *  line options, then the environment variables, then the config files in
 *  reverse order.  Once done with that, the order is reversed and all
 *  the config files and environment variables are processed again, this
 *  time only processing the non-immediate action options.  do_presets()
 *  will then return for optionProcess() to do the final pass on the command
 *  line arguments.
 */

/**
 *  scan the command line for immediate action options.
 *  This is only called the first time through.
 *  While this procedure is active, the OPTPROC_IMMEDIATE is true.
 *
 *  @param pOpts   program options descriptor
 *  @returns SUCCESS or FAILURE
 */
LOCAL tSuccess
immediate_opts(tOptions * opts)
{
    tSuccess  res;

    opts->fOptSet  |= OPTPROC_IMMEDIATE;
    opts->curOptIdx = 1;     /* start by skipping program name */
    opts->pzCurOpt  = NULL;

    /*
     *  Examine all the options from the start.  We process any options that
     *  are marked for immediate processing.
     */
    for (;;) {
        tOptState opt_st = OPTSTATE_INITIALIZER(PRESET);

        res = next_opt(opts, &opt_st);
        switch (res) {
        case FAILURE: goto   failed_option;
        case PROBLEM: res = SUCCESS; goto leave;
        case SUCCESS: break;
        }

        /*
         *  IF this is an immediate-attribute option, then do it.
         */
        if (! DO_IMMEDIATELY(opt_st.flags))
            continue;

        if (! SUCCESSFUL(handle_opt(opts, &opt_st)))
            break;
    } failed_option:;

    if ((opts->fOptSet & OPTPROC_ERRSTOP) != 0)
        (*opts->pUsageProc)(opts, EXIT_FAILURE);

 leave:

    opts->fOptSet &= ~OPTPROC_IMMEDIATE;
    return res;
}

/**
 *  check for preset values from a config files or envrionment variables
 *
 * @param[in,out] opts  the structure with the option names to check
 */
static tSuccess
do_presets(tOptions * opts)
{
    tOptDesc * od = NULL;

    if (! SUCCESSFUL(immediate_opts(opts)))
        return FAILURE;

    /*
     *  IF this option set has a --save-opts option, then it also
     *  has a --load-opts option.  See if a command line option has disabled
     *  option presetting.
     */
    if (  (opts->specOptIdx.save_opts != NO_EQUIVALENT)
       && (opts->specOptIdx.save_opts != 0)) {
        od = opts->pOptDesc + opts->specOptIdx.save_opts + 1;
        if (DISABLED_OPT(od))
            return SUCCESS;
    }

    /*
     *  Until we return from this procedure, disable non-presettable opts
     */
    opts->fOptSet |= OPTPROC_PRESETTING;
    /*
     *  IF there are no config files,
     *  THEN do any environment presets and leave.
     */
    if (opts->papzHomeList == NULL) {
        env_presets(opts, ENV_ALL);
    }
    else {
        env_presets(opts, ENV_IMM);

        /*
         *  Check to see if environment variables have disabled presetting.
         */
        if ((od != NULL) && ! DISABLED_OPT(od))
            intern_file_load(opts);

        /*
         *  ${PROGRAM_LOAD_OPTS} value of "no" cannot disable other environment
         *  variable options.  Only the loading of .rc files.
         */
        env_presets(opts, ENV_NON_IMM);
    }
    opts->fOptSet &= ~OPTPROC_PRESETTING;

    return SUCCESS;
}

/**
 * AutoOpts initialization
 *
 * @param[in,out] opts  the structure to initialize
 * @param[in]     a_ct  program argument count
 * @param[in]     a_v   program argument vector
 */
LOCAL bool
ao_initialize(tOptions * opts, int a_ct, char ** a_v)
{
    if ((opts->fOptSet & OPTPROC_INITDONE) != 0)
        return true;

    opts->origArgCt   = (unsigned int)a_ct;
    opts->origArgVect = a_v;
    opts->fOptSet    |= OPTPROC_INITDONE;

    if (HAS_pzPkgDataDir(opts))
        program_pkgdatadir = opts->pzPkgDataDir;

    if (! SUCCESSFUL(do_presets(opts)))
        return false;

    /*
     *  IF option name conversion was suppressed but it is not suppressed
     *  for the command line, then it's time to translate option names.
     *  Usage text will not get retranslated.
     */
    if (  ((opts->fOptSet & OPTPROC_TRANSLATE) != 0)
       && (opts->pTransProc != NULL)
       && ((opts->fOptSet & OPTPROC_NO_XLAT_MASK) == OPTPROC_NXLAT_OPT_CFG)
       )  {
        opts->fOptSet &= ~OPTPROC_NXLAT_OPT_CFG;
        (*opts->pTransProc)();
    }

    if ((opts->fOptSet & OPTPROC_REORDER) != 0)
        optionSort(opts);

    opts->curOptIdx   = 1;
    opts->pzCurOpt    = NULL;
    return true;
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/initialize.c */
