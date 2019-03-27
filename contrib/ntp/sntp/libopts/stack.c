
/**
 * \file stack.c
 *
 *  This is a special option processing routine that will save the
 *  argument to an option in a FIFO queue.
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

#ifdef WITH_LIBREGEX
#  include REGEX_HEADER
#endif

/*=export_func  optionUnstackArg
 * private:
 *
 * what:  Remove option args from a stack
 * arg:   + tOptions * + opts + program options descriptor +
 * arg:   + tOptDesc * + od   + the descriptor for this arg +
 *
 * doc:
 *  Invoked for options that are equivalenced to stacked options.
=*/
void
optionUnstackArg(tOptions * opts, tOptDesc * od)
{
    tArgList * arg_list;

    if (INQUERY_CALL(opts, od))
        return;

    arg_list = (tArgList *)od->optCookie;

    /*
     *  IF we don't have any stacked options,
     *  THEN indicate that we don't have any of these options
     */
    if (arg_list == NULL) {
        od->fOptState &= OPTST_PERSISTENT_MASK;
        if ((od->fOptState & OPTST_INITENABLED) == 0)
            od->fOptState |= OPTST_DISABLED;
        return;
    }

#ifdef WITH_LIBREGEX
    {
        regex_t   re;
        int       i, ct, dIdx;

        if (regcomp(&re, od->optArg.argString, REG_NOSUB) != 0)
            return;

        /*
         *  search the list for the entry(s) to remove.  Entries that
         *  are removed are *not* copied into the result.  The source
         *  index is incremented every time.  The destination only when
         *  we are keeping a define.
         */
        for (i = 0, dIdx = 0, ct = arg_list->useCt; --ct >= 0; i++) {
            char const * pzSrc = arg_list->apzArgs[ i ];
            char *       pzEq  = strchr(pzSrc, '=');
            int          res;


            if (pzEq != NULL)
                *pzEq = NUL;

            res = regexec(&re, pzSrc, (size_t)0, NULL, 0);
            switch (res) {
            case 0:
                /*
                 *  Remove this entry by reducing the in-use count
                 *  and *not* putting the string pointer back into
                 *  the list.
                 */
                AGFREE(pzSrc);
                arg_list->useCt--;
                break;

            default:
            case REG_NOMATCH:
                if (pzEq != NULL)
                    *pzEq = '=';

                /*
                 *  IF we have dropped an entry
                 *  THEN we have to move the current one.
                 */
                if (dIdx != i)
                    arg_list->apzArgs[ dIdx ] = pzSrc;
                dIdx++;
            }
        }

        regfree(&re);
    }
#else  /* not WITH_LIBREGEX */
    {
        int i, ct, dIdx;

        /*
         *  search the list for the entry(s) to remove.  Entries that
         *  are removed are *not* copied into the result.  The source
         *  index is incremented every time.  The destination only when
         *  we are keeping a define.
         */
        for (i = 0, dIdx = 0, ct = arg_list->useCt; --ct >= 0; i++) {
            const char * pzSrc = arg_list->apzArgs[ i ];
            char *       pzEq  = strchr(pzSrc, '=');

            if (pzEq != NULL)
                *pzEq = NUL;

            if (strcmp(pzSrc, od->optArg.argString) == 0) {
                /*
                 *  Remove this entry by reducing the in-use count
                 *  and *not* putting the string pointer back into
                 *  the list.
                 */
                AGFREE(pzSrc);
                arg_list->useCt--;
            } else {
                if (pzEq != NULL)
                    *pzEq = '=';

                /*
                 *  IF we have dropped an entry
                 *  THEN we have to move the current one.
                 */
                if (dIdx != i)
                    arg_list->apzArgs[ dIdx ] = pzSrc;
                dIdx++;
            }
        }
    }
#endif /* WITH_LIBREGEX */
    /*
     *  IF we have unstacked everything,
     *  THEN indicate that we don't have any of these options
     */
    if (arg_list->useCt == 0) {
        od->fOptState &= OPTST_PERSISTENT_MASK;
        if ((od->fOptState & OPTST_INITENABLED) == 0)
            od->fOptState |= OPTST_DISABLED;
        AGFREE(arg_list);
        od->optCookie = NULL;
    }
}


/*
 *  Put an entry into an argument list.  The first argument points to
 *  a pointer to the argument list structure.  It gets passed around
 *  as an opaque address.
 */
LOCAL void
addArgListEntry(void ** ppAL, void * entry)
{
    tArgList * pAL = *(void **)ppAL;

    /*
     *  IF we have never allocated one of these,
     *  THEN allocate one now
     */
    if (pAL == NULL) {
        pAL = (tArgList *)AGALOC(sizeof(*pAL), "new option arg stack");
        if (pAL == NULL)
            return;
        pAL->useCt   = 0;
        pAL->allocCt = MIN_ARG_ALLOC_CT;
        *ppAL = VOIDP(pAL);
    }

    /*
     *  ELSE if we are out of room
     *  THEN make it bigger
     */
    else if (pAL->useCt >= pAL->allocCt) {
        size_t sz = sizeof(*pAL);
        pAL->allocCt += INCR_ARG_ALLOC_CT;

        /*
         *  The base structure contains space for MIN_ARG_ALLOC_CT
         *  pointers.  We subtract it off to find our augment size.
         */
        sz += sizeof(char *) * ((size_t)pAL->allocCt - MIN_ARG_ALLOC_CT);
        pAL = (tArgList *)AGREALOC(VOIDP(pAL), sz, "expanded opt arg stack");
        if (pAL == NULL)
            return;
        *ppAL = VOIDP(pAL);
    }

    /*
     *  Insert the new argument into the list
     */
    pAL->apzArgs[ (pAL->useCt)++ ] = entry;
}


/*=export_func  optionStackArg
 * private:
 *
 * what:  put option args on a stack
 * arg:   + tOptions * + opts + program options descriptor +
 * arg:   + tOptDesc * + od   + the descriptor for this arg +
 *
 * doc:
 *  Keep an entry-ordered list of option arguments.
=*/
void
optionStackArg(tOptions * opts, tOptDesc * od)
{
    char * pz;

    if (INQUERY_CALL(opts, od))
        return;

    if ((od->fOptState & OPTST_RESET) != 0) {
        tArgList * arg_list = od->optCookie;
        int ix;
        if (arg_list == NULL)
            return;

        ix = arg_list->useCt;
        while (--ix >= 0)
            AGFREE(arg_list->apzArgs[ix]);
        AGFREE(arg_list);

    } else {
        if (od->optArg.argString == NULL)
            return;

        AGDUPSTR(pz, od->optArg.argString, "stack arg");
        addArgListEntry(&(od->optCookie), VOIDP(pz));
    }
}
/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/stack.c */
