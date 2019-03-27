/**
 * @file check.c
 *
 * @brief Hunt for options in the option descriptor list
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
static int
parse_opt(char const ** nm_pp, char ** arg_pp, char * buf, size_t bufsz);

static void
opt_ambiguities(tOptions * opts, char const * name, int nm_len);

static int
opt_match_ct(tOptions * opts, char const * name, int nm_len,
             int * ixp, bool * disable);

static tSuccess
opt_set(tOptions * opts, char * arg, int idx, bool disable, tOptState * st);

static tSuccess
opt_unknown(tOptions * opts, char const * name, char * arg, tOptState * st);

static tSuccess
opt_ambiguous(tOptions * opts, char const * name, int match_ct);

static tSuccess
get_opt_arg_must(tOptions * opts, tOptState * o_st);

static tSuccess
get_opt_arg_may(tOptions * pOpts, tOptState * o_st);

static tSuccess
get_opt_arg_none(tOptions * pOpts, tOptState * o_st);
/* = = = END-STATIC-FORWARD = = = */

/**
 * find the name and name length we are looking for
 */
static int
parse_opt(char const ** nm_pp, char ** arg_pp, char * buf, size_t bufsz)
{
    int  res = 0;
    char const * p = *nm_pp;
    *arg_pp  = NULL;

    for (;;) {
        switch (*(p++)) {
        case NUL: return res;

        case '=':
            memcpy(buf, *nm_pp, (size_t)res);

            buf[res] = NUL;
            *nm_pp   = buf;
            *arg_pp  = VOIDP(p);
            return res;

        default:
            if (++res >= (int)bufsz)
                return -1;
        }
    }
}

/**
 *  print out the options that match the given name.
 *
 * @param pOpts      option data
 * @param opt_name   name of option to look for
 */
static void
opt_ambiguities(tOptions * opts, char const * name, int nm_len)
{
    char const * const hyph =
        NAMED_OPTS(opts) ? "" : LONG_OPT_MARKER;

    tOptDesc * pOD = opts->pOptDesc;
    int        idx = 0;

    fputs(zambig_list_msg, stderr);
    do  {
        if (pOD->pz_Name == NULL)
            continue; /* doc option */

        if (strneqvcmp(name, pOD->pz_Name, nm_len) == 0)
            fprintf(stderr, zambig_file, hyph, pOD->pz_Name);

        else if (  (pOD->pz_DisableName != NULL)
                && (strneqvcmp(name, pOD->pz_DisableName, nm_len) == 0)
                )
            fprintf(stderr, zambig_file, hyph, pOD->pz_DisableName);
    } while (pOD++, (++idx < opts->optCt));
}

/**
 *  Determine the number of options that match the name
 *
 * @param pOpts      option data
 * @param opt_name   name of option to look for
 * @param nm_len     length of provided name
 * @param index      pointer to int for option index
 * @param disable    pointer to bool to mark disabled option
 * @return count of options that match
 */
static int
opt_match_ct(tOptions * opts, char const * name, int nm_len,
             int * ixp, bool * disable)
{
    int   matchCt  = 0;
    int   idx      = 0;
    int   idxLim   = opts->optCt;
    tOptDesc * pOD = opts->pOptDesc;

    do  {
        /*
         *  If option disabled or a doc option, skip to next
         */
        if (pOD->pz_Name == NULL)
            continue;

        if (  SKIP_OPT(pOD)
           && (pOD->fOptState != (OPTST_OMITTED | OPTST_NO_INIT)))
            continue;

        if (strneqvcmp(name, pOD->pz_Name, nm_len) == 0) {
            /*
             *  IF we have a complete match
             *  THEN it takes priority over any already located partial
             */
            if (pOD->pz_Name[ nm_len ] == NUL) {
                *ixp = idx;
                return 1;
            }
        }

        /*
         *  IF       there is a disable name
         *     *AND* the option name matches the disable name
         *  THEN ...
         */
        else if (  (pOD->pz_DisableName != NULL)
                && (strneqvcmp(name, pOD->pz_DisableName, nm_len) == 0)
                )  {
            *disable = true;

            /*
             *  IF we have a complete match
             *  THEN it takes priority over any already located partial
             */
            if (pOD->pz_DisableName[ nm_len ] == NUL) {
                *ixp = idx;
                return 1;
            }
        }

        else
            continue; /* does not match any option */

        /*
         *  We found a full or partial match, either regular or disabling.
         *  Remember the index for later.
         */
        *ixp = idx;
        ++matchCt;

    } while (pOD++, (++idx < idxLim));

    return matchCt;
}

/**
 *  Set the option to the indicated option number.
 *
 * @param opts      option data
 * @param arg       option argument (if glued to name)
 * @param idx       option index
 * @param disable   mark disabled option
 * @param st        state about current option
 */
static tSuccess
opt_set(tOptions * opts, char * arg, int idx, bool disable, tOptState * st)
{
    tOptDesc * pOD = opts->pOptDesc + idx;

    if (SKIP_OPT(pOD)) {
        if ((opts->fOptSet & OPTPROC_ERRSTOP) == 0)
            return FAILURE;

        fprintf(stderr, zDisabledErr, opts->pzProgName, pOD->pz_Name);
        if (pOD->pzText != NULL)
            fprintf(stderr, SET_OFF_FMT, pOD->pzText);
        fputc(NL, stderr);
        (*opts->pUsageProc)(opts, EXIT_FAILURE);
        /* NOTREACHED */
        _exit(EXIT_FAILURE); /* to be certain */
    }

    /*
     *  IF we found a disablement name,
     *  THEN set the bit in the callers' flag word
     */
    if (disable)
        st->flags |= OPTST_DISABLED;

    st->pOD      = pOD;
    st->pzOptArg = arg;
    st->optType  = TOPT_LONG;

    return SUCCESS;
}

/**
 *  An option was not found.  Check for default option and set it
 *  if there is one.  Otherwise, handle the error.
 *
 * @param opts   option data
 * @param name   name of option to look for
 * @param arg    option argument
 * @param st     state about current option
 *
 * @return success status
 */
static tSuccess
opt_unknown(tOptions * opts, char const * name, char * arg, tOptState * st)
{
    /*
     *  IF there is no equal sign
     *     *AND* we are using named arguments
     *     *AND* there is a default named option,
     *  THEN return that option.
     */
    if (  (arg == NULL)
       && NAMED_OPTS(opts)
       && (opts->specOptIdx.default_opt != NO_EQUIVALENT)) {

        st->pOD      = opts->pOptDesc + opts->specOptIdx.default_opt;
        st->pzOptArg = name;
        st->optType  = TOPT_DEFAULT;
        return SUCCESS;
    }

    if ((opts->fOptSet & OPTPROC_ERRSTOP) != 0) {
        fprintf(stderr, zIllOptStr, opts->pzProgPath, name);
        (*opts->pUsageProc)(opts, EXIT_FAILURE);
        /* NOTREACHED */
        _exit(EXIT_FAILURE); /* to be certain */
    }

    return FAILURE;
}

/**
 *  Several options match the provided name.
 *
 * @param opts      option data
 * @param name      name of option to look for
 * @param match_ct  number of matching options
 *
 * @return success status (always FAILURE, if it returns)
 */
static tSuccess
opt_ambiguous(tOptions * opts, char const * name, int match_ct)
{
    if ((opts->fOptSet & OPTPROC_ERRSTOP) != 0) {
        fprintf(stderr, zambig_opt_fmt, opts->pzProgPath, name, match_ct);
        if (match_ct <= 4)
            opt_ambiguities(opts, name, (int)strlen(name));
        (*opts->pUsageProc)(opts, EXIT_FAILURE);
        /* NOTREACHED */
        _exit(EXIT_FAILURE); /* to be certain */
    }
    return FAILURE;
}

/*=export_func  optionVendorOption
 * private:
 *
 * what:  Process a vendor option
 * arg:   + tOptions * + pOpts    + program options descriptor +
 * arg:   + tOptDesc * + pOptDesc + the descriptor for this arg +
 *
 * doc:
 *  For POSIX specified utilities, the options are constrained to the options,
 *  @xref{config attributes, Program Configuration}.  AutoOpts clients should
 *  never specify this directly.  It gets referenced when the option
 *  definitions contain a "vendor-opt" attribute.
=*/
void
optionVendorOption(tOptions * pOpts, tOptDesc * pOD)
{
    tOptState     opt_st   = OPTSTATE_INITIALIZER(PRESET);
    char const *  vopt_str = pOD->optArg.argString;

    if (pOpts <= OPTPROC_EMIT_LIMIT)
        return;

    if ((pOD->fOptState & OPTST_RESET) != 0)
        return;

    if ((pOD->fOptState & OPTPROC_IMMEDIATE) == 0)
        opt_st.flags = OPTST_DEFINED;

    if (  ((pOpts->fOptSet & OPTPROC_VENDOR_OPT) == 0)
       || ! SUCCESSFUL(opt_find_long(pOpts, vopt_str, &opt_st))
       || ! SUCCESSFUL(get_opt_arg(pOpts, &opt_st)) )
    {
        fprintf(stderr, zIllVendOptStr, pOpts->pzProgName, vopt_str);
        (*pOpts->pUsageProc)(pOpts, EXIT_FAILURE);
        /* NOTREACHED */
        _exit(EXIT_FAILURE); /* to be certain */
    }

    /*
     *  See if we are in immediate handling state.
     */
    if (pOpts->fOptSet & OPTPROC_IMMEDIATE) {
        /*
         *  See if the enclosed option is okay with that state.
         */
        if (DO_IMMEDIATELY(opt_st.flags))
            (void)handle_opt(pOpts, &opt_st);

    } else {
        /*
         *  non-immediate direction.
         *  See if the enclosed option is okay with that state.
         */
        if (DO_NORMALLY(opt_st.flags) || DO_SECOND_TIME(opt_st.flags))
            (void)handle_opt(pOpts, &opt_st);
    }
}

/**
 *  Find the option descriptor by full name.
 *
 * @param opts      option data
 * @param opt_name  name of option to look for
 * @param state     state about current option
 *
 * @return success status
 */
LOCAL tSuccess
opt_find_long(tOptions * opts, char const * opt_name, tOptState * state)
{
    char    name_buf[128];
    char *  opt_arg;
    int     nm_len = parse_opt(&opt_name, &opt_arg, name_buf, sizeof(name_buf));

    int     idx = 0;
    bool    disable  = false;
    int     ct;

    if (nm_len <= 1) {
        if ((opts->fOptSet & OPTPROC_ERRSTOP) == 0)
            return FAILURE;
        
        fprintf(stderr, zInvalOptName, opts->pzProgName, opt_name);
        (*opts->pUsageProc)(opts, EXIT_FAILURE);
        /* NOTREACHED */
        _exit(EXIT_FAILURE); /* to be certain */
    }

    ct = opt_match_ct(opts, opt_name, nm_len, &idx, &disable);

    /*
     *  See if we found one match, no matches or multiple matches.
     */
    switch (ct) {
    case 1:  return opt_set(opts, opt_arg, idx, disable, state);
    case 0:  return opt_unknown(opts, opt_name, opt_arg, state);
    default: return opt_ambiguous(opts, opt_name, ct);
    }
}


/**
 *  Find the short option descriptor for the current option
 *
 * @param pOpts      option data
 * @param optValue   option flag character
 * @param pOptState  state about current option
 */
LOCAL tSuccess
opt_find_short(tOptions * pOpts, uint_t optValue, tOptState * pOptState)
{
    tOptDesc * pRes = pOpts->pOptDesc;
    int        ct   = pOpts->optCt;

    /*
     *  Search the option list
     */
    do  {
        if (optValue != pRes->optValue)
            continue;

        if (SKIP_OPT(pRes)) {
            if (  (pRes->fOptState == (OPTST_OMITTED | OPTST_NO_INIT))
               && (pRes->pz_Name != NULL)) {
                if ((pOpts->fOptSet & OPTPROC_ERRSTOP) == 0)
                    return FAILURE;
        
                fprintf(stderr, zDisabledErr, pOpts->pzProgPath, pRes->pz_Name);
                if (pRes->pzText != NULL)
                    fprintf(stderr, SET_OFF_FMT, pRes->pzText);
                fputc(NL, stderr);
                (*pOpts->pUsageProc)(pOpts, EXIT_FAILURE);
                /* NOTREACHED */
                _exit(EXIT_FAILURE); /* to be certain */
            }
            goto short_opt_error;
        }

        pOptState->pOD     = pRes;
        pOptState->optType = TOPT_SHORT;
        return SUCCESS;

    } while (pRes++, --ct > 0);

    /*
     *  IF    the character value is a digit
     *    AND there is a special number option ("-n")
     *  THEN the result is the "option" itself and the
     *       option is the specially marked "number" option.
     */
    if (  IS_DEC_DIGIT_CHAR(optValue)
       && (pOpts->specOptIdx.number_option != NO_EQUIVALENT) ) {
        pOptState->pOD = \
        pRes           = pOpts->pOptDesc + pOpts->specOptIdx.number_option;
        (pOpts->pzCurOpt)--;
        pOptState->optType = TOPT_SHORT;
        return SUCCESS;
    }

 short_opt_error:

    /*
     *  IF we are to stop on errors (the default, actually)
     *  THEN call the usage procedure.
     */
    if ((pOpts->fOptSet & OPTPROC_ERRSTOP) != 0) {
        fprintf(stderr, zIllOptChr, pOpts->pzProgPath, optValue);
        (*pOpts->pUsageProc)(pOpts, EXIT_FAILURE);
        /* NOTREACHED */
        _exit(EXIT_FAILURE); /* to be certain */
    }

    return FAILURE;
}

/**
 *  Process option with a required argument.  Long options can either have a
 *  separate command line argument, or an argument attached by the '='
 *  character.  Figure out which.
 *
 *  @param[in,out] opts  the program option descriptor
 *  @param[in,out] o_st  the option processing state
 *  @returns SUCCESS or FAILURE
 */
static tSuccess
get_opt_arg_must(tOptions * opts, tOptState * o_st)
{
    switch (o_st->optType) {
    case TOPT_SHORT:
        /*
         *  See if an arg string follows the flag character
         */
        if (*++(opts->pzCurOpt) == NUL)
            opts->pzCurOpt = opts->origArgVect[ opts->curOptIdx++ ];
        o_st->pzOptArg = opts->pzCurOpt;
        break;

    case TOPT_LONG:
        /*
         *  See if an arg string has already been assigned (glued on
         *  with an `=' character)
         */
        if (o_st->pzOptArg == NULL)
            o_st->pzOptArg = opts->origArgVect[ opts->curOptIdx++ ];
        break;

    default:
#ifdef DEBUG
        fputs("AutoOpts lib error: option type not selected\n", stderr);
        option_exits(EXIT_FAILURE);
#endif

    case TOPT_DEFAULT:
        /*
         *  The option was selected by default.  The current token is
         *  the option argument.
         */
        break;
    }

    /*
     *  Make sure we did not overflow the argument list.
     */
    if (opts->curOptIdx > opts->origArgCt) {
        fprintf(stderr, zMisArg, opts->pzProgPath, o_st->pOD->pz_Name);
        return FAILURE;
    }

    opts->pzCurOpt = NULL;  /* next time advance to next arg */
    return SUCCESS;
}

/**
 * Process an option with an optional argument.  For short options, it looks
 * at the character after the option character, or it consumes the next full
 * argument.  For long options, it looks for an '=' character attachment to
 * the long option name before deciding to take the next command line
 * argument.
 *
 * @param pOpts      the option descriptor
 * @param o_st  a structure for managing the current processing state
 * @returns SUCCESS or does not return
 */
static tSuccess
get_opt_arg_may(tOptions * pOpts, tOptState * o_st)
{
    /*
     *  An option argument is optional.
     */
    switch (o_st->optType) {
    case TOPT_SHORT:
        if (*++pOpts->pzCurOpt != NUL)
            o_st->pzOptArg = pOpts->pzCurOpt;
        else {
            char * pzLA = pOpts->origArgVect[ pOpts->curOptIdx ];

            /*
             *  BECAUSE it is optional, we must make sure
             *  we did not find another flag and that there
             *  is such an argument.
             */
            if ((pzLA == NULL) || (*pzLA == '-'))
                o_st->pzOptArg = NULL;
            else {
                pOpts->curOptIdx++; /* argument found */
                o_st->pzOptArg = pzLA;
            }
        }
        break;

    case TOPT_LONG:
        /*
         *  Look for an argument if we don't already have one (glued on
         *  with a `=' character) *AND* we are not in named argument mode
         */
        if (  (o_st->pzOptArg == NULL)
           && (! NAMED_OPTS(pOpts))) {
            char * pzLA = pOpts->origArgVect[ pOpts->curOptIdx ];

            /*
             *  BECAUSE it is optional, we must make sure
             *  we did not find another flag and that there
             *  is such an argument.
             */
            if ((pzLA == NULL) || (*pzLA == '-'))
                o_st->pzOptArg = NULL;
            else {
                pOpts->curOptIdx++; /* argument found */
                o_st->pzOptArg = pzLA;
            }
        }
        break;

    default:
    case TOPT_DEFAULT:
        ao_bug(zbad_default_msg);
    }

    /*
     *  After an option with an optional argument, we will
     *  *always* start with the next option because if there
     *  were any characters following the option name/flag,
     *  they would be interpreted as the argument.
     */
    pOpts->pzCurOpt = NULL;
    return SUCCESS;
}

/**
 *  Process option that does not have an argument.
 *
 *  @param[in,out] opts  the program option descriptor
 *  @param[in,out] o_st  the option processing state
 *  @returns SUCCESS or FAILURE
 */
static tSuccess
get_opt_arg_none(tOptions * pOpts, tOptState * o_st)
{
    /*
     *  No option argument.  Make sure next time around we find
     *  the correct option flag character for short options
     */
    if (o_st->optType == TOPT_SHORT)
        (pOpts->pzCurOpt)++;

    /*
     *  It is a long option.  Make sure there was no ``=xxx'' argument
     */
    else if (o_st->pzOptArg != NULL) {
        fprintf(stderr, zNoArg, pOpts->pzProgPath, o_st->pOD->pz_Name);
        return FAILURE;
    }

    /*
     *  It is a long option.  Advance to next command line argument.
     */
    else
        pOpts->pzCurOpt = NULL;
    return SUCCESS;
}

/**
 *  Process option.  Figure out whether or not to look for an option argument.
 *
 *  @param[in,out] opts  the program option descriptor
 *  @param[in,out] o_st  the option processing state
 *  @returns SUCCESS or FAILURE
 */
LOCAL tSuccess
get_opt_arg(tOptions * opts, tOptState * o_st)
{
    o_st->flags |= (o_st->pOD->fOptState & OPTST_PERSISTENT_MASK);

    /*
     * Disabled options and options specified to not have arguments
     * are handled with the "none" procedure.  Otherwise, check the
     * optional flag and call either the "may" or "must" function.
     */
    if (  ((o_st->flags & OPTST_DISABLED) != 0)
       || (OPTST_GET_ARGTYPE(o_st->flags) == OPARG_TYPE_NONE))
        return get_opt_arg_none(opts, o_st);
    
    if (o_st->flags & OPTST_ARG_OPTIONAL)
        return get_opt_arg_may( opts, o_st);
    
    return get_opt_arg_must(opts, o_st);
}

/**
 *  Find the option descriptor for the current option.
 *
 *  @param[in,out] opts  the program option descriptor
 *  @param[in,out] o_st  the option processing state
 *  @returns SUCCESS or FAILURE
 */
LOCAL tSuccess
find_opt(tOptions * opts, tOptState * o_st)
{
    /*
     *  IF we are continuing a short option list (e.g. -xyz...)
     *  THEN continue a single flag option.
     *  OTHERWISE see if there is room to advance and then do so.
     */
    if ((opts->pzCurOpt != NULL) && (*opts->pzCurOpt != NUL))
        return opt_find_short(opts, (uint8_t)*(opts->pzCurOpt), o_st);

    if (opts->curOptIdx >= opts->origArgCt)
        return PROBLEM; /* NORMAL COMPLETION */

    opts->pzCurOpt = opts->origArgVect[ opts->curOptIdx ];

    /*
     *  IF all arguments must be named options, ...
     */
    if (NAMED_OPTS(opts)) {
        char *      pz  = opts->pzCurOpt;
        int         def;
        tSuccess    res;
        uint16_t *  def_opt;

        opts->curOptIdx++;

        if (*pz != '-')
            return opt_find_long(opts, pz, o_st);

        /*
         *  The name is prefixed with one or more hyphens.  Strip them off
         *  and disable the "default_opt" setting.  Use heavy recasting to
         *  strip off the "const" quality of the "default_opt" field.
         */
        while (*(++pz) == '-')   ;
        def_opt  = VOIDP(&(opts->specOptIdx.default_opt));
        def      = *def_opt;
        *def_opt = NO_EQUIVALENT;
        res      = opt_find_long(opts, pz, o_st);
        *def_opt = (uint16_t)def;
        return res;
    }

    /*
     *  Note the kind of flag/option marker
     */
    if (*((opts->pzCurOpt)++) != '-')
        return PROBLEM; /* NORMAL COMPLETION - this + rest are operands */

    /*
     *  Special hack for a hyphen by itself
     */
    if (*(opts->pzCurOpt) == NUL)
        return PROBLEM; /* NORMAL COMPLETION - this + rest are operands */

    /*
     *  The current argument is to be processed as an option argument
     */
    opts->curOptIdx++;

    /*
     *  We have an option marker.
     *  Test the next character for long option indication
     */
    if (opts->pzCurOpt[0] == '-') {
        if (*++(opts->pzCurOpt) == NUL)
            /*
             *  NORMAL COMPLETION - NOT this arg, but rest are operands
             */
            return PROBLEM;

        /*
         *  We do not allow the hyphen to be used as a flag value.
         *  Therefore, if long options are not to be accepted, we punt.
         */
        if ((opts->fOptSet & OPTPROC_LONGOPT) == 0) {
            fprintf(stderr, zIllOptStr, opts->pzProgPath, opts->pzCurOpt-2);
            return FAILURE;
        }

        return opt_find_long(opts, opts->pzCurOpt, o_st);
    }

    /*
     *  If short options are not allowed, then do long
     *  option processing.  Otherwise the character must be a
     *  short (i.e. single character) option.
     */
    if ((opts->fOptSet & OPTPROC_SHORTOPT) != 0)
        return opt_find_short(opts, (uint8_t)*(opts->pzCurOpt), o_st);

    return opt_find_long(opts, opts->pzCurOpt, o_st);
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/find.c */
