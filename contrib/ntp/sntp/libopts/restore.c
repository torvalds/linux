
/*
 * \file restore.c
 *
 *  This module's routines will save the current option state to memory
 *  and restore it.  If saved prior to the initial optionProcess call,
 *  then the initial state will be restored.
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

/*
 *  optionFixupSavedOpts  Really, it just wipes out option state for
 *  options that are troublesome to copy.  viz., stacked strings and
 *  hierarcicaly valued option args.  We do duplicate string args that
 *  have been marked as allocated though.
 */
static void
fixupSavedOptionArgs(tOptions * pOpts)
{
    tOptions * p   = pOpts->pSavedState;
    tOptDesc * pOD = pOpts->pOptDesc;
    int        ct  = pOpts->optCt;

    /*
     *  Make sure that allocated stuff is only referenced in the
     *  archived copy of the data.
     */
    for (; ct-- > 0; pOD++)  {
        switch (OPTST_GET_ARGTYPE(pOD->fOptState)) {
        case OPARG_TYPE_STRING:
            if (pOD->fOptState & OPTST_STACKED) {
                tOptDesc * q = p->pOptDesc + (pOD - pOpts->pOptDesc);
                q->optCookie = NULL;
            }
            if (pOD->fOptState & OPTST_ALLOC_ARG) {
                tOptDesc * q = p->pOptDesc + (pOD - pOpts->pOptDesc);
                AGDUPSTR(q->optArg.argString, pOD->optArg.argString, "arg");
            }
            break;

        case OPARG_TYPE_HIERARCHY:
        {
            tOptDesc * q = p->pOptDesc + (pOD - pOpts->pOptDesc);
            q->optCookie = NULL;
        }
        }
    }
}

/*=export_func optionSaveState
 *
 * what:  saves the option state to memory
 * arg:   tOptions *, pOpts, program options descriptor
 *
 * doc:
 *
 *  This routine will allocate enough memory to save the current option
 *  processing state.  If this routine has been called before, that memory
 *  will be reused.  You may only save one copy of the option state.  This
 *  routine may be called before optionProcess(3AO).  If you do call it
 *  before the first call to optionProcess, then you may also change the
 *  contents of argc/argv after you call optionRestore(3AO)
 *
 *  In fact, more strongly put: it is safest to only use this function
 *  before having processed any options.  In particular, the saving and
 *  restoring of stacked string arguments and hierarchical values is
 *  disabled.  The values are not saved.
 *
 * err:   If it fails to allocate the memory,
 *        it will print a message to stderr and exit.
 *        Otherwise, it will always succeed.
=*/
void
optionSaveState(tOptions * pOpts)
{
    tOptions * p = (tOptions *)pOpts->pSavedState;

    if (p == NULL) {
        size_t sz = sizeof(*pOpts)
            + ((size_t)pOpts->optCt * sizeof(tOptDesc));
        p = AGALOC(sz, "saved option state");

        pOpts->pSavedState = p;
    }

    memcpy(p, pOpts, sizeof(*p));
    memcpy(p + 1, pOpts->pOptDesc, (size_t)p->optCt * sizeof(tOptDesc));

    fixupSavedOptionArgs(pOpts);
}


/*=export_func optionRestore
 *
 * what:  restore option state from memory copy
 * arg:   tOptions *, pOpts, program options descriptor
 *
 * doc:  Copy back the option state from saved memory.
 *       The allocated memory is left intact, so this routine can be
 *       called repeatedly without having to call optionSaveState again.
 *       If you are restoring a state that was saved before the first call
 *       to optionProcess(3AO), then you may change the contents of the
 *       argc/argv parameters to optionProcess.
 *
 * err:  If you have not called @code{optionSaveState} before, a diagnostic is
 *       printed to @code{stderr} and exit is called.
=*/
void
optionRestore(tOptions * pOpts)
{
    tOptions * p = (tOptions *)pOpts->pSavedState;

    if (p == NULL) {
        char const * pzName = pOpts->pzProgName;
        if (pzName == NULL) {
            pzName = pOpts->pzPROGNAME;
            if (pzName == NULL)
                pzName = zNil;
        }
        fprintf(stderr, zNoState, pzName);
        option_exits(EXIT_FAILURE);
    }

    pOpts->pSavedState = NULL;
    optionFree(pOpts);

    memcpy(pOpts, p, sizeof(*p));
    memcpy(pOpts->pOptDesc, p+1, (size_t)p->optCt * sizeof(tOptDesc));
    pOpts->pSavedState = p;

    fixupSavedOptionArgs(pOpts);
}

/* = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = */

/*=export_func optionFree
 *
 * what:  free allocated option processing memory
 * arg:   tOptions *, pOpts, program options descriptor
 *
 * doc:   AutoOpts sometimes allocates memory and puts pointers to it in the
 *        option state structures.  This routine deallocates all such memory.
 *
 * err:   As long as memory has not been corrupted,
 *        this routine is always successful.
=*/
void
optionFree(tOptions * pOpts)
{
 free_saved_state:
    {
        tOptDesc * p = pOpts->pOptDesc;
        int ct = pOpts->optCt;
        do  {
            if (p->fOptState & OPTST_ALLOC_ARG) {
                AGFREE(p->optArg.argString);
                p->optArg.argString = NULL;
                p->fOptState &= ~OPTST_ALLOC_ARG;
            }

            switch (OPTST_GET_ARGTYPE(p->fOptState)) {
            case OPARG_TYPE_STRING:
#ifdef WITH_LIBREGEX
                if (  (p->fOptState & OPTST_STACKED)
                   && (p->optCookie != NULL)) {
                    p->optArg.argString = ".*";
                    optionUnstackArg(pOpts, p);
                }
#else
                /* leak memory */;
#endif
                break;

            case OPARG_TYPE_HIERARCHY:
                if (p->optCookie != NULL)
                    unload_arg_list(p->optCookie);
                break;
            }

            p->optCookie = NULL;
        } while (p++, --ct > 0);
    }
    if (pOpts->pSavedState != NULL) {
        tOptions * p = (tOptions *)pOpts->pSavedState;
        memcpy(pOpts, p, sizeof(*p));
        memcpy(pOpts->pOptDesc, p+1, (size_t)p->optCt * sizeof(tOptDesc));
        AGFREE(pOpts->pSavedState);
        pOpts->pSavedState = NULL;
        goto free_saved_state;
    }
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/restore.c */
