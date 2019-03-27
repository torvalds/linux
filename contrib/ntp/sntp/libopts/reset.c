
/**
 * \file reset.c
 *
 *  Reset the option state to the compiled state.
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

static void
optionReset(tOptions * pOpts, tOptDesc * pOD)
{
    pOD->fOptState &= OPTST_PERSISTENT_MASK;
    pOD->fOptState |= OPTST_RESET;
    if (pOD->pOptProc != NULL)
        pOD->pOptProc(pOpts, pOD);
    pOD->optArg.argString =
        pOpts->originalOptArgArray[ pOD->optIndex ].argString;
    pOD->optCookie = pOpts->originalOptArgCookie[ pOD->optIndex ];
    pOD->fOptState &= OPTST_PERSISTENT_MASK;
}


static void
optionResetEverything(tOptions * pOpts)
{
    tOptDesc * pOD = pOpts->pOptDesc;
    int        ct  = pOpts->presetOptCt;

    for (;;) {
        optionReset(pOpts, pOD);

        if (--ct <= 0)
            break;
        pOD++;
    }
}


/*=export_func  optionResetOpt
 * private:
 *
 * what:  Reset the value of an option
 * arg:   + tOptions * + pOpts    + program options descriptor  +
 * arg:   + tOptDesc * + pOptDesc + the descriptor for this arg +
 *
 * doc:
 *  This code will cause another option to be reset to its initial state.
 *  For example, --reset=foo will cause the --foo option to be reset.
=*/
void
optionResetOpt(tOptions * pOpts, tOptDesc * pOD)
{
    static bool reset_active = false;

    tOptState opt_state = OPTSTATE_INITIALIZER(DEFINED);
    char const * pzArg = pOD->optArg.argString;
    tSuccess     succ;

    if (pOpts <= OPTPROC_EMIT_LIMIT)
        return;

    if (reset_active)
        return;

    if (  (! HAS_originalOptArgArray(pOpts))
       || (pOpts->originalOptArgCookie == NULL))
        ao_bug(zno_reset);

    if ((pzArg == NULL) || (*pzArg == NUL)) {
        fprintf(stderr, zreset_arg, pOpts->pzProgName, pOD->pz_Name);
        pOpts->pUsageProc(pOpts, EXIT_FAILURE);
        /* NOTREACHED */
        assert(0 == 1);
    }

    reset_active = true;

    if (pzArg[1] == NUL) {
        if (*pzArg == '*') {
            optionResetEverything(pOpts);
            reset_active = false;
            return;
        }

        succ = opt_find_short(pOpts, (uint8_t)*pzArg, &opt_state);
        if (! SUCCESSFUL(succ)) {
            fprintf(stderr, zIllOptChr, pOpts->pzProgPath, *pzArg);
            pOpts->pUsageProc(pOpts, EXIT_FAILURE);
            /* NOTREACHED */
            assert(0 == 1);
        }
    } else {
        succ = opt_find_long(pOpts, pzArg, &opt_state);
        if (! SUCCESSFUL(succ)) {
            fprintf(stderr, zIllOptStr, pOpts->pzProgPath, pzArg);
            pOpts->pUsageProc(pOpts, EXIT_FAILURE);
            /* NOTREACHED */
            assert(0 == 1);
        }
    }

    /*
     *  We've found the indicated option.  Turn off all non-persistent
     *  flags because we're forcing the option back to its initialized state.
     *  Call any callout procedure to handle whatever it needs to.
     *  Finally, clear the reset flag, too.
     */
    optionReset(pOpts, opt_state.pOD);
    reset_active = false;
}
/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/reset.c */
