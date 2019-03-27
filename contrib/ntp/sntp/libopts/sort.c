
/*
 * \file sort.c
 *
 *  This module implements argument sorting.
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
must_arg(tOptions * opts, char * arg_txt, tOptState * pOS,
         char ** opt_txt, uint32_t * opt_idx);

static tSuccess
maybe_arg(tOptions * opts, char * arg_txt, tOptState * pOS,
          char ** opt_txt, uint32_t * opt_idx);

static tSuccess
short_opt_ck(tOptions * opts, char * arg_txt, tOptState * pOS,
             char ** opt_txt, uint32_t * opt_idx);
/* = = = END-STATIC-FORWARD = = = */

/*
 *  "must_arg" and "maybe_arg" are really similar.  The biggest
 *  difference is that "may" will consume the next argument only if it
 *  does not start with a hyphen and "must" will consume it, hyphen or not.
 */
static tSuccess
must_arg(tOptions * opts, char * arg_txt, tOptState * pOS,
         char ** opt_txt, uint32_t * opt_idx)
{
    /*
     *  An option argument is required.  Long options can either have
     *  a separate command line argument, or an argument attached by
     *  the '=' character.  Figure out which.
     */
    switch (pOS->optType) {
    case TOPT_SHORT:
        /*
         *  See if an arg string follows the flag character.  If not,
         *  the next arg must be the option argument.
         */
        if (*arg_txt != NUL)
            return SUCCESS;
        break;

    case TOPT_LONG:
        /*
         *  See if an arg string has already been assigned (glued on
         *  with an `=' character).  If not, the next is the opt arg.
         */
        if (pOS->pzOptArg != NULL)
            return SUCCESS;
        break;

    default:
        return FAILURE;
    }
    if (opts->curOptIdx >= opts->origArgCt)
        return FAILURE;

    opt_txt[ (*opt_idx)++ ] = opts->origArgVect[ (opts->curOptIdx)++ ];
    return SUCCESS;
}

static tSuccess
maybe_arg(tOptions * opts, char * arg_txt, tOptState * pOS,
          char ** opt_txt, uint32_t * opt_idx)
{
    /*
     *  An option argument is optional.
     */
    switch (pOS->optType) {
    case TOPT_SHORT:
        /*
         *  IF nothing is glued on after the current flag character,
         *  THEN see if there is another argument.  If so and if it
         *  does *NOT* start with a hyphen, then it is the option arg.
         */
        if (*arg_txt != NUL)
            return SUCCESS;
        break;

    case TOPT_LONG:
        /*
         *  Look for an argument if we don't already have one (glued on
         *  with a `=' character)
         */
        if (pOS->pzOptArg != NULL)
            return SUCCESS;
        break;

    default:
        return FAILURE;
    }
    if (opts->curOptIdx >= opts->origArgCt)
        return PROBLEM;

    arg_txt = opts->origArgVect[ opts->curOptIdx ];
    if (*arg_txt != '-')
        opt_txt[ (*opt_idx)++ ] = opts->origArgVect[ (opts->curOptIdx)++ ];
    return SUCCESS;
}

/*
 *  Process a string of short options glued together.  If the last one
 *  does or may take an argument, the do the argument processing and leave.
 */
static tSuccess
short_opt_ck(tOptions * opts, char * arg_txt, tOptState * pOS,
             char ** opt_txt, uint32_t * opt_idx)
{
    while (*arg_txt != NUL) {
        if (FAILED(opt_find_short(opts, (uint8_t)*arg_txt, pOS)))
            return FAILURE;

        /*
         *  See if we can have an arg.
         */
        if (OPTST_GET_ARGTYPE(pOS->pOD->fOptState) == OPARG_TYPE_NONE) {
            arg_txt++;

        } else if (pOS->pOD->fOptState & OPTST_ARG_OPTIONAL) {
            /*
             *  Take an argument if it is not attached and it does not
             *  start with a hyphen.
             */
            if (arg_txt[1] != NUL)
                return SUCCESS;

            arg_txt = opts->origArgVect[ opts->curOptIdx ];
            if (*arg_txt != '-')
                opt_txt[ (*opt_idx)++ ] =
                    opts->origArgVect[ (opts->curOptIdx)++ ];
            return SUCCESS;

        } else {
            /*
             *  IF we need another argument, be sure it is there and
             *  take it.
             */
            if (arg_txt[1] == NUL) {
                if (opts->curOptIdx >= opts->origArgCt)
                    return FAILURE;
                opt_txt[ (*opt_idx)++ ] =
                    opts->origArgVect[ (opts->curOptIdx)++ ];
            }
            return SUCCESS;
        }
    }
    return SUCCESS;
}

/*
 *  If the program wants sorted options (separated operands and options),
 *  then this routine will to the trick.
 */
LOCAL void
optionSort(tOptions * opts)
{
    char **  opt_txt;
    char **  ppzOpds;
    uint32_t optsIdx = 0;
    uint32_t opdsIdx = 0;

    tOptState os = OPTSTATE_INITIALIZER(DEFINED);

    /*
     *  Disable for POSIX conformance, or if there are no operands.
     */
    if (  (getenv("POSIXLY_CORRECT") != NULL)
       || NAMED_OPTS(opts))
        return;

    /*
     *  Make sure we can allocate two full-sized arg vectors.
     */
    opt_txt = malloc(opts->origArgCt * sizeof(char *));
    if (opt_txt == NULL)
        goto exit_no_mem;

    ppzOpds = malloc(opts->origArgCt * sizeof(char *));
    if (ppzOpds == NULL) {
        free(opt_txt);
        goto exit_no_mem;
    }

    opts->curOptIdx = 1;
    opts->pzCurOpt  = NULL;

    /*
     *  Now, process all the options from our current position onward.
     *  (This allows interspersed options and arguments for the few
     *  non-standard programs that require it.)
     */
    for (;;) {
        char * arg_txt;
        tSuccess res;

        /*
         *  If we're out of arguments, we're done.  Join the option and
         *  operand lists into the original argument vector.
         */
        if (opts->curOptIdx >= opts->origArgCt) {
            errno = 0;
            goto joinLists;
        }

        arg_txt = opts->origArgVect[ opts->curOptIdx ];
        if (*arg_txt != '-') {
            ppzOpds[ opdsIdx++ ] = opts->origArgVect[ (opts->curOptIdx)++ ];
            continue;
        }

        switch (arg_txt[1]) {
        case NUL:
            /*
             *  A single hyphen is an operand.
             */
            ppzOpds[ opdsIdx++ ] = opts->origArgVect[ (opts->curOptIdx)++ ];
            continue;

        case '-':
            /*
             *  Two consecutive hypens.  Put them on the options list and then
             *  _always_ force the remainder of the arguments to be operands.
             */
            if (arg_txt[2] == NUL) {
                opt_txt[ optsIdx++ ] =
                    opts->origArgVect[ (opts->curOptIdx)++ ];
                goto restOperands;
            }
            res = opt_find_long(opts, arg_txt+2, &os);
            break;

        default:
            /*
             *  If short options are not allowed, then do long
             *  option processing.  Otherwise the character must be a
             *  short (i.e. single character) option.
             */
            if ((opts->fOptSet & OPTPROC_SHORTOPT) == 0) {
                res = opt_find_long(opts, arg_txt+1, &os);
            } else {
                res = opt_find_short(opts, (uint8_t)arg_txt[1], &os);
            }
            break;
        }
        if (FAILED(res)) {
            errno = EINVAL;
            goto freeTemps;
        }

        /*
         *  We've found an option.  Add the argument to the option list.
         *  Next, we have to see if we need to pull another argument to be
         *  used as the option argument.
         */
        opt_txt[ optsIdx++ ] = opts->origArgVect[ (opts->curOptIdx)++ ];

        if (OPTST_GET_ARGTYPE(os.pOD->fOptState) == OPARG_TYPE_NONE) {
            /*
             *  No option argument.  If we have a short option here,
             *  then scan for short options until we get to the end
             *  of the argument string.
             */
            if (  (os.optType == TOPT_SHORT)
               && FAILED(short_opt_ck(opts, arg_txt+2, &os, opt_txt,
                                      &optsIdx)) )  {
                errno = EINVAL;
                goto freeTemps;
            }

        } else if (os.pOD->fOptState & OPTST_ARG_OPTIONAL) {
            switch (maybe_arg(opts, arg_txt+2, &os, opt_txt, &optsIdx)) {
            case FAILURE: errno = EIO; goto freeTemps;
            case PROBLEM: errno = 0;   goto joinLists;
            }

        } else {
            switch (must_arg(opts, arg_txt+2, &os, opt_txt, &optsIdx)) {
            case PROBLEM:
            case FAILURE: errno = EIO; goto freeTemps;
            }
        }
    } /* for (;;) */

 restOperands:
    while (opts->curOptIdx < opts->origArgCt)
        ppzOpds[ opdsIdx++ ] = opts->origArgVect[ (opts->curOptIdx)++ ];

 joinLists:
    if (optsIdx > 0)
        memcpy(opts->origArgVect + 1, opt_txt,
               (size_t)optsIdx * sizeof(char *));
    if (opdsIdx > 0)
        memcpy(opts->origArgVect + 1 + optsIdx, ppzOpds,
               (size_t)opdsIdx * sizeof(char *));

 freeTemps:
    free(opt_txt);
    free(ppzOpds);
    return;

 exit_no_mem:
    errno = ENOMEM;
    return;
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/sort.c */
