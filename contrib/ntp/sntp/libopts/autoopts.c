
/**
 * \file autoopts.c
 *
 *  This file contains all of the routines that must be linked into
 *  an executable to use the generated option processing.  The optional
 *  routines are in separately compiled modules so that they will not
 *  necessarily be linked in.
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

/**
 * The number of tab characters to skip when printing continuation lines.
 */
static unsigned int tab_skip_ct          = 0;

#ifndef HAVE_PATHFIND
#  define  pathfind(_p, _n, _m) option_pathfind(_p, _n, _m)
#  include "compat/pathfind.c"
#endif

#ifndef HAVE_SNPRINTF
#  define vsnprintf       option_vsnprintf
#  define snprintf        option_snprintf
#  include "compat/snprintf.c"
#endif

#ifndef HAVE_STRDUP
#  define  strdup(_s)     option_strdup(_s)
#  include "compat/strdup.c"
#endif

#ifndef HAVE_STRCHR
#  define strrchr(_s, _c) option_strrchr(_s, _c)
#  define strchr(_s, _c)  option_strchr(_s, _c)
#  include "compat/strchr.c"
#endif

LOCAL void *
ao_malloc(size_t sz)
{
    void * res = malloc(sz);
    if (res == NULL) {
        fprintf(stderr, zalloc_fail, (int)sz);
        option_exits(EXIT_FAILURE);
    }
    return res;
}
#undef  malloc
#define malloc(_s)        ao_malloc(_s)

LOCAL void *
ao_realloc(void *p, size_t sz)
{
    void * res = (p == NULL) ? malloc(sz) : realloc(p, sz);
    if (res == NULL) {
        fprintf(stderr, zrealloc_fail, (int)sz, p);
        option_exits(EXIT_FAILURE);
    }
    return res;
}
#undef  realloc
#define realloc(_p,_s)    ao_realloc(_p,_s)

LOCAL char *
ao_strdup(char const *str)
{
    char * res = strdup(str);
    if (res == NULL) {
        fprintf(stderr, zalloc_fail, (int)strlen(str));
        option_exits(EXIT_FAILURE);
    }
    return res;
}
#undef  strdup
#define strdup(_p)        ao_strdup(_p)

/**
 *  handle an option.
 *
 *  This routine handles equivalencing, sets the option state flags and
 *  invokes the handler procedure, if any.
 */
LOCAL tSuccess
handle_opt(tOptions * opts, tOptState * o_st)
{
    /*
     *  Save a copy of the option procedure pointer.
     *  If this is an equivalence class option, we still want this proc.
     */
    tOptDesc *  od = o_st->pOD;
    tOptProc *  opt_proc = od->pOptProc;
    if (od->fOptState & OPTST_ALLOC_ARG)
        AGFREE(od->optArg.argString);

    od->optArg.argString = o_st->pzOptArg;

    /*
     *  IF we are presetting options, then we will ignore any un-presettable
     *  options.  They are the ones either marked as such.
     */
    if (  ((opts->fOptSet & OPTPROC_PRESETTING) != 0)
       && ((od->fOptState & OPTST_NO_INIT) != 0)
       )
        return PROBLEM;

    /*
     *  IF this is an equivalence class option,
     *  THEN
     *      Save the option value that got us to this option
     *      entry.  (It may not be od->optChar[0], if this is an
     *      equivalence entry.)
     *      set the pointer to the equivalence class base
     */
    if (od->optEquivIndex != NO_EQUIVALENT) {
        tOptDesc * eqv_od = opts->pOptDesc + od->optEquivIndex;

        /*
         * IF the current option state has not been defined (set on the
         *    command line), THEN we will allow continued resetting of
         *    the value.  Once "defined", then it must not change.
         */
        if ((od->fOptState & OPTST_DEFINED) != 0) {
            /*
             *  The equivalenced-to option has been found on the command
             *  line before.  Make sure new occurrences are the same type.
             *
             *  IF this option has been previously equivalenced and
             *     it was not the same equivalenced-to option,
             *  THEN we have a usage problem.
             */
            if (eqv_od->optActualIndex != od->optIndex) {
                fprintf(stderr, zmultiway_bug, eqv_od->pz_Name, od->pz_Name,
                        (opts->pOptDesc + eqv_od->optActualIndex)->pz_Name);
                return FAILURE;
            }
        } else {
            /*
             *  Set the equivalenced-to actual option index to no-equivalent
             *  so that we set all the entries below.  This option may either
             *  never have been selected before, or else it was selected by
             *  some sort of "presetting" mechanism.
             */
            eqv_od->optActualIndex = NO_EQUIVALENT;
        }

        if (eqv_od->optActualIndex != od->optIndex) {
            /*
             *  First time through, copy over the state
             *  and add in the equivalence flag
             */
            eqv_od->optActualValue = od->optValue;
            eqv_od->optActualIndex = od->optIndex;
            o_st->flags |= OPTST_EQUIVALENCE;
        }

        /*
         *  Copy the most recent option argument.  set membership state
         *  is kept in 'eqv_od->optCookie'.  Do not overwrite.
         */
        eqv_od->optArg.argString = od->optArg.argString;
        od = eqv_od;

    } else {
        od->optActualValue = od->optValue;
        od->optActualIndex = od->optIndex;
    }

    od->fOptState &= OPTST_PERSISTENT_MASK;
    od->fOptState |= (o_st->flags & ~OPTST_PERSISTENT_MASK);

    /*
     *  Keep track of count only for DEFINED (command line) options.
     *  IF we have too many, build up an error message and bail.
     */
    if (  (od->fOptState & OPTST_DEFINED)
       && (++od->optOccCt > od->optMaxCt)  )
        return too_many_occurrences(opts, od);
    /*
     *  If provided a procedure to call, call it
     */
    if (opt_proc != NULL)
        (*opt_proc)(opts, od);

    return SUCCESS;
}

/**
 *  Find the option descriptor and option argument (if any) for the
 *  next command line argument.  DO NOT modify the descriptor.  Put
 *  all the state in the state argument so that the option can be skipped
 *  without consequence (side effect).
 *
 * @param opts the program option descriptor
 * @param o_st  the state of the next found option
 */
LOCAL tSuccess
next_opt(tOptions * opts, tOptState * o_st)
{
    {
        tSuccess res = find_opt(opts, o_st);
        if (! SUCCESSFUL(res))
            return res;
    }

    if (  ((o_st->flags & OPTST_DEFINED) != 0)
       && ((o_st->pOD->fOptState & OPTST_NO_COMMAND) != 0)) {
        fprintf(stderr, zNotCmdOpt, o_st->pOD->pz_Name);
        return FAILURE;
    }

    return get_opt_arg(opts, o_st);
}

/**
 * Process all the options from our current position onward.  (This allows
 * interspersed options and arguments for the few non-standard programs that
 * require it.)  Thus, do not rewind option indexes because some programs
 * choose to re-invoke after a non-option.
 *
 *  @param[in,out] opts   program options descriptor
 *  @returns SUCCESS or FAILURE
 */
LOCAL tSuccess
regular_opts(tOptions * opts)
{
    /* assert:  opts->fOptSet & OPTPROC_IMMEDIATE == 0 */
    for (;;) {
        tOptState opt_st = OPTSTATE_INITIALIZER(DEFINED);

        switch (next_opt(opts, &opt_st)) {
        case FAILURE: goto   failed_option;
        case PROBLEM: return SUCCESS; /* no more args */
        case SUCCESS: break;
        }

        /*
         *  IF this is an immediate action option,
         *  THEN skip it (unless we are supposed to do it a second time).
         */
        if (! DO_NORMALLY(opt_st.flags)) {
            if (! DO_SECOND_TIME(opt_st.flags))
                continue;
            opt_st.pOD->optOccCt--; /* don't count this repetition */
        }

        if (! SUCCESSFUL(handle_opt(opts, &opt_st)))
            break;
    } failed_option:;

    if ((opts->fOptSet & OPTPROC_ERRSTOP) != 0)
        (*opts->pUsageProc)(opts, EXIT_FAILURE);

    return FAILURE;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *  THESE ROUTINES ARE CALLABLE FROM THE GENERATED OPTION PROCESSING CODE
 */
/*=--subblock=arg=arg_type,arg_name,arg_desc =*/
/*=*
 * library:  opts
 * header:   your-opts.h
 *
 * lib_description:
 *
 *  These are the routines that libopts users may call directly from their
 *  code.  There are several other routines that can be called by code
 *  generated by the libopts option templates, but they are not to be
 *  called from any other user code.  The @file{options.h} header is
 *  fairly clear about this, too.
=*/

/*=export_func optionProcess
 *
 * what: this is the main option processing routine
 *
 * arg:  + tOptions * + opts  + program options descriptor +
 * arg:  + int        + a_ct  + program arg count  +
 * arg:  + char **    + a_v   + program arg vector +
 *
 * ret_type:  int
 * ret_desc:  the count of the arguments processed
 *
 * doc:
 *
 * This is the main entry point for processing options.  It is intended
 * that this procedure be called once at the beginning of the execution of
 * a program.  Depending on options selected earlier, it is sometimes
 * necessary to stop and restart option processing, or to select completely
 * different sets of options.  This can be done easily, but you generally
 * do not want to do this.
 *
 * The number of arguments processed always includes the program name.
 * If one of the arguments is "--", then it is counted and the processing
 * stops.  If an error was encountered and errors are to be tolerated, then
 * the returned value is the index of the argument causing the error.
 * A hyphen by itself ("-") will also cause processing to stop and will
 * @emph{not} be counted among the processed arguments.  A hyphen by itself
 * is treated as an operand.  Encountering an operand stops option
 * processing.
 *
 * err:  Errors will cause diagnostics to be printed.  @code{exit(3)} may
 *       or may not be called.  It depends upon whether or not the options
 *       were generated with the "allow-errors" attribute, or if the
 *       ERRSKIP_OPTERR or ERRSTOP_OPTERR macros were invoked.
=*/
int
optionProcess(tOptions * opts, int a_ct, char ** a_v)
{
    if (! SUCCESSFUL(validate_struct(opts, a_v[0])))
        ao_bug(zbad_data_msg);
    
    /*
     *  Establish the real program name, the program full path,
     *  and do all the presetting the first time thru only.
     */
    if (! ao_initialize(opts, a_ct, a_v))
        return 0;

    /*
     *  IF we are (re)starting,
     *  THEN reset option location
     */
    if (opts->curOptIdx <= 0) {
        opts->curOptIdx = 1;
        opts->pzCurOpt  = NULL;
    }

    if (! SUCCESSFUL(regular_opts(opts)))
        return (int)opts->origArgCt;

    /*
     *  IF    there were no errors
     *    AND we have RC/INI files
     *    AND there is a request to save the files
     *  THEN do that now before testing for conflicts.
     *       (conflicts are ignored in preset options)
     */
    switch (opts->specOptIdx.save_opts) {
    case 0:
    case NO_EQUIVALENT:
        break;
    default:
    {
        tOptDesc * od = opts->pOptDesc + opts->specOptIdx.save_opts;

        if (SELECTED_OPT(od)) {
            optionSaveFile(opts);
            option_exits(EXIT_SUCCESS);
        }
    }
    }

    /*
     *  IF we are checking for errors,
     *  THEN look for too few occurrences of required options
     */
    if (((opts->fOptSet & OPTPROC_ERRSTOP) != 0)
       && (! is_consistent(opts)))
        (*opts->pUsageProc)(opts, EXIT_FAILURE);

    return (int)opts->curOptIdx;
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/autoopts.c */
