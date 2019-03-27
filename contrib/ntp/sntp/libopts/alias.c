
/**
 * \file alias.c
 *
 * Handle options that are aliases for another option.
 *
 * @addtogroup autoopts
 * @{
 */
/*
 *  This routine will forward an option alias to the correct option code.
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

LOCAL tSuccess
too_many_occurrences(tOptions * opts, tOptDesc * od)
{
    if ((opts->fOptSet & OPTPROC_ERRSTOP) != 0) {
        char const * eqv = (od->optEquivIndex != NO_EQUIVALENT) ? zequiv : zNil;

        fprintf(stderr, ztoo_often_fmt, opts->pzProgName);

        if (od->optMaxCt > 1)
            fprintf(stderr, zat_most, od->optMaxCt, od->pz_Name, eqv);
        else
            fprintf(stderr, zonly_one, od->pz_Name, eqv);
        (*opts->pUsageProc)(opts, EXIT_FAILURE);
        /* NOTREACHED */
    }

    return FAILURE;
}

/*=export_func  optionAlias
 * private:
 *
 * what:  relay an option to its alias
 * arg:   + tOptions *   + opts   + program options descriptor  +
 * arg:   + tOptDesc *   + old_od + the descriptor for this arg +
 * arg:   + unsigned int + alias  + the aliased-to option index +
 * ret-type: int
 *
 * doc:
 *  Handle one option as if it had been specified as another.  Exactly.
 *  Returns "-1" if the aliased-to option has appeared too many times.
=*/
int
optionAlias(tOptions * opts, tOptDesc * old_od, unsigned int alias)
{
    tOptDesc * new_od;

    if (opts <= OPTPROC_EMIT_LIMIT)
        return 0;

    new_od = opts->pOptDesc + alias;
    if ((unsigned)opts->optCt <= alias) {
        fputs(zbad_alias_id, stderr);
        option_exits(EXIT_FAILURE);
    }

    /*
     *  Copy over the option instance flags
     */
    new_od->fOptState &= OPTST_PERSISTENT_MASK;
    new_od->fOptState |= (old_od->fOptState & ~OPTST_PERSISTENT_MASK);
    new_od->optArg.argString = old_od->optArg.argString;

    /*
     *  Keep track of count only for DEFINED (command line) options.
     *  IF we have too many, build up an error message and bail.
     */
    if (  (new_od->fOptState & OPTST_DEFINED)
       && (++new_od->optOccCt > new_od->optMaxCt)  )
        return too_many_occurrences(opts, new_od);

    /*
     *  Clear the state bits and counters
     */
    old_od->fOptState &= OPTST_PERSISTENT_MASK;
    old_od->optOccCt   = 0;

    /*
     *  If there is a procedure to call, call it
     */
    if (new_od->pOptProc != NULL)
        (*new_od->pOptProc)(opts, new_od);
    return 0;
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/alias.c */
