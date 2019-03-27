/**
 * @file check.c
 *
 * @brief option consistency checks.
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
 *  Check for conflicts based on "must" and "cannot" attributes.
 */
static bool
has_conflict(tOptions * pOpts, tOptDesc * od)
{
    if (od->pOptMust != NULL) {
        int const * must = od->pOptMust;

        while (*must != NO_EQUIVALENT) {
            tOptDesc * p = pOpts->pOptDesc + *(must++);
            if (UNUSED_OPT(p)) {
                const tOptDesc * ood = pOpts->pOptDesc + must[-1];
                fprintf(stderr, zneed_fmt, pOpts->pzProgName,
                        od->pz_Name, ood->pz_Name);
                return true;
            }
        }
    }

    if (od->pOptCant != NULL) {
        int const * cant = od->pOptCant;

        while (*cant != NO_EQUIVALENT) {
            tOptDesc * p = pOpts->pOptDesc + *(cant++);
            if (SELECTED_OPT(p)) {
                const tOptDesc * ood = pOpts->pOptDesc + cant[-1];
                fprintf(stderr, zconflict_fmt, pOpts->pzProgName,
                        od->pz_Name, ood->pz_Name);
                return true;
            }
        }
    }

    return false;
}

/**
 *  Check that the option occurs often enough.  Too often is already checked.
 */
static bool
occurs_enough(tOptions * pOpts, tOptDesc * pOD)
{
    (void)pOpts;

    /*
     *  IF the occurrence counts have been satisfied,
     *  THEN there is no problem.
     */
    if (pOD->optOccCt >= pOD->optMinCt)
        return true;

    /*
     *  IF MUST_SET means SET and PRESET are okay,
     *  so min occurrence count doesn't count
     */
    if (  (pOD->fOptState & OPTST_MUST_SET)
       && (pOD->fOptState & (OPTST_PRESET | OPTST_SET)) )
        return true;

    if (pOD->optMinCt > 1)
         fprintf(stderr, zneed_more, pOpts->pzProgName, pOD->pz_Name,
                 pOD->optMinCt);
    else fprintf(stderr, zneed_one,  pOpts->pzProgName, pOD->pz_Name);
    return false;
}

/**
 *  Verify option consistency.
 *
 *  Make sure that the argument list passes our consistency tests.
 */
LOCAL bool
is_consistent(tOptions * pOpts)
{
    tOptDesc * pOD   = pOpts->pOptDesc;
    int        oCt   = pOpts->presetOptCt;

    /*
     *  FOR each of "oCt" options, ...
     */
    for (;;) {
        /*
         *  IF the current option was provided on the command line
         *  THEN ensure that any "MUST" requirements are not
         *       "DEFAULT" (unspecified) *AND* ensure that any
         *       "CANT" options have not been SET or DEFINED.
         */
        if (SELECTED_OPT(pOD)) {
            if (has_conflict(pOpts, pOD))
                return false;
        }

        /*
         *  IF       this option is not equivalenced to another,
         *        OR it is equivalenced to itself (is the equiv. root)
         *  THEN we need to make sure it occurs often enough.
         */
        if (  (pOD->optEquivIndex == NO_EQUIVALENT)
           || (pOD->optEquivIndex == pOD->optIndex) )

            if (! occurs_enough(pOpts, pOD))
                return false;

        if (--oCt <= 0)
            break;
        pOD++;
    }

    /*
     *  IF we are stopping on errors, check to see if any remaining
     *  arguments are required to be there or prohibited from being there.
     */
    if ((pOpts->fOptSet & OPTPROC_ERRSTOP) != 0) {

        /*
         *  Check for prohibition
         */
        if ((pOpts->fOptSet & OPTPROC_NO_ARGS) != 0) {
            if (pOpts->origArgCt > pOpts->curOptIdx) {
                fprintf(stderr, zNoArgs, pOpts->pzProgName);
                return false;
            }
        }

        /*
         *  ELSE not prohibited, check for being required
         */
        else if ((pOpts->fOptSet & OPTPROC_ARGS_REQ) != 0) {
            if (pOpts->origArgCt <= pOpts->curOptIdx) {
                fprintf(stderr, zargs_must, pOpts->pzProgName);
                return false;
            }
        }
    }

    return true;
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/check.c */
