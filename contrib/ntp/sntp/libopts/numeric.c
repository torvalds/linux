
/**
 * \file numeric.c
 *
 * Handle options with numeric (integer) arguments.
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

/*=export_func  optionShowRange
 * private:
 *
 * what:  Show info about range constraints
 * arg:   + tOptions * + pOpts     + program options descriptor  +
 * arg:   + tOptDesc * + pOptDesc  + the descriptor for this arg +
 * arg:   + void *     + rng_table + the value range tables      +
 * arg:   + int        + rng_count + the number of entries       +
 *
 * doc:
 *   Show information about a numeric option with range constraints.
=*/
void
optionShowRange(tOptions * pOpts, tOptDesc * pOD, void * rng_table, int rng_ct)
{
    const struct {long const rmin, rmax;} * rng = rng_table;

    char const * pz_indent = zTabHyp + tab_skip_ct;

    /*
     * The range is shown only for full usage requests and an error
     * in this particular option.
     */
    if (pOpts != OPTPROC_EMIT_USAGE) {
        if (pOpts <= OPTPROC_EMIT_LIMIT)
            return;
        pz_indent = ONE_TAB_STR;

        fprintf(option_usage_fp, zRangeErr, pOpts->pzProgName,
                pOD->pz_Name, pOD->optArg.argInt);
        pz_indent = "";
    }

    if (pOD->fOptState & OPTST_SCALED_NUM)
        fprintf(option_usage_fp, zRangeScaled, pz_indent);

    fprintf(option_usage_fp, (rng_ct > 1) ? zRangeLie : zRangeOnly, pz_indent);
    pz_indent = (pOpts != OPTPROC_EMIT_USAGE)
        ? ONE_TAB_STR
        : (zTabSpace + tab_skip_ct);

    for (;;) {
        if (rng->rmax == LONG_MIN)
            fprintf(option_usage_fp, zRangeExact, pz_indent, rng->rmin);
        else if (rng->rmin == LONG_MIN)
            fprintf(option_usage_fp, zRangeUpto, pz_indent, rng->rmax);
        else if (rng->rmax == LONG_MAX)
            fprintf(option_usage_fp, zRangeAbove, pz_indent, rng->rmin);
        else
            fprintf(option_usage_fp, zRange, pz_indent, rng->rmin,
                    rng->rmax);

        if  (--rng_ct <= 0) {
            fputc(NL, option_usage_fp);
            break;
        }
        fputs(zRangeOr, option_usage_fp);
        rng++;
    }

    if (pOpts > OPTPROC_EMIT_LIMIT)
        pOpts->pUsageProc(pOpts, EXIT_FAILURE);
}

/*=export_func  optionNumericVal
 * private:
 *
 * what:  process an option with a numeric value.
 * arg:   + tOptions * + opts + program options descriptor +
 * arg:   + tOptDesc * + od   + the descriptor for this arg +
 *
 * doc:
 *  Decipher a numeric value.
=*/
void
optionNumericVal(tOptions * opts, tOptDesc * od)
{
    char * pz;
    long   val;

    /*
     *  Guard against all the different ways this procedure might get invoked
     *  when there is no string argument provided.
     */
    if (INQUERY_CALL(opts, od) || (od->optArg.argString == NULL))
        return;

    /*
     *  Numeric options may have a range associated with it.
     *  If it does, the usage procedure requests that it be
     *  emitted by passing a NULL od pointer.  Also bail out
     *  if there is no option argument or if we are being reset.
     */
    if (  (od == NULL)
       || (od->optArg.argString == NULL)
       || ((od->fOptState & OPTST_RESET) != 0))
        return;

    errno = 0;
    val = strtol(od->optArg.argString, &pz, 0);
    if ((pz == od->optArg.argString) || (errno != 0))
        goto bad_number;

    if ((od->fOptState & OPTST_SCALED_NUM) != 0)
        switch (*(pz++)) {
        case NUL:  pz--; break;
        case 't':  val *= 1000;
        case 'g':  val *= 1000;
        case 'm':  val *= 1000;
        case 'k':  val *= 1000; break;

        case 'T':  val *= 1024;
        case 'G':  val *= 1024;
        case 'M':  val *= 1024;
        case 'K':  val *= 1024; break;

        default:   goto bad_number;
        }

    if (*pz != NUL)
        goto bad_number;

    if (od->fOptState & OPTST_ALLOC_ARG) {
        AGFREE(od->optArg.argString);
        od->fOptState &= ~OPTST_ALLOC_ARG;
    }

    od->optArg.argInt = val;
    return;

    bad_number:

    fprintf( stderr, zNotNumber, opts->pzProgName, od->optArg.argString );
    if ((opts->fOptSet & OPTPROC_ERRSTOP) != 0)
        (*(opts->pUsageProc))(opts, EXIT_FAILURE);

    errno = EINVAL;
    od->optArg.argInt = ~0;
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/numeric.c */
