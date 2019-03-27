
/**
 * \file pgusage.c
 *
 *   Automated Options Paged Usage module.
 *
 * @addtogroup autoopts
 * @{
 */
/*
 *  This routine will run run-on options through a pager so the
 *  user may examine, print or edit them at their leisure.
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

#if defined(HAVE_WORKING_FORK)
static inline FILE *
open_tmp_usage(char ** buf)
{
    char * bf;
    size_t bfsz;

    {
        unsigned int my_pid = (unsigned int)getpid();
        char const * tmpdir = getenv(TMPDIR);
        if (tmpdir == NULL)
            tmpdir = tmp_dir;
        bfsz = TMP_FILE_FMT_LEN + strlen(tmpdir) + 10;
        bf   = AGALOC(bfsz, "tmp fil");
        snprintf(bf, bfsz, TMP_FILE_FMT, tmpdir, my_pid);
    }

    {
        static mode_t const cmask = S_IRWXO | S_IRWXG;
        mode_t svmsk = umask(cmask);
        int fd = mkstemp(bf);
        (void)umask(svmsk);

        if (fd < 0) {
            AGFREE(bf);
            return NULL;
        }
        *buf = bf;
        return fdopen(fd, "w");
    }
}

static inline char *
mk_pager_cmd(char const * fname)
{
    /*
     * Page the file and remove it when done.  For shell script processing,
     * we must redirect the output to the current stderr, otherwise stdout.
     */
    fclose(option_usage_fp);
    option_usage_fp = NULL;

    {
        char const * pager  = (char const *)getenv(PAGER_NAME);
        size_t bfsz;
        char * res;

        /*
         *  Use the "more(1)" program if "PAGER" has not been defined
         */
        if (pager == NULL)
            pager = MORE_STR;

        bfsz = 2 * strlen(fname) + strlen(pager) + PAGE_USAGE_FMT_LEN;
        res  = AGALOC(bfsz, "more cmd");
        snprintf(res, bfsz, PAGE_USAGE_FMT, pager, fname);
        AGFREE(fname);
        return res;
    }
}
#endif

/*=export_func  optionPagedUsage
 * private:
 *
 * what:  emit help text and pass through a pager program.
 * arg:   + tOptions * + opts + program options descriptor +
 * arg:   + tOptDesc * + od   + the descriptor for this arg +
 *
 * doc:
 *  Run the usage output through a pager.
 *  This is very handy if it is very long.
 *  This is disabled on platforms without a working fork() function.
=*/
void
optionPagedUsage(tOptions * opts, tOptDesc * od)
{
#if ! defined(HAVE_WORKING_FORK)
    if ((od->fOptState & OPTST_RESET) != 0)
        return;

    (*opts->pUsageProc)(opts, EXIT_SUCCESS);
#else
    static bool sv_print_exit = false;
    static char * fil_name = NULL;

    /*
     *  IF we are being called after the usage proc is done
     *     (and thus has called "exit(2)")
     *  THEN invoke the pager to page through the usage file we created.
     */
    switch (pagerState) {
    case PAGER_STATE_INITIAL:
    {
        if ((od->fOptState & OPTST_RESET) != 0)
            return;
        option_usage_fp = open_tmp_usage(&fil_name);
        if (option_usage_fp == NULL)
            (*opts->pUsageProc)(opts, EXIT_SUCCESS);

        pagerState    = PAGER_STATE_READY;
        sv_print_exit = print_exit;

        /*
         *  Set up so this routine gets called during the exit logic
         */
        atexit((void(*)(void))optionPagedUsage);

        /*
         *  The usage procedure will now put the usage information into
         *  the temporary file we created above.  Keep any shell commands
         *  out of the result.
         */
        print_exit = false;
        (*opts->pUsageProc)(opts, EXIT_SUCCESS);

        /* NOTREACHED */
        _exit(EXIT_FAILURE);
    }

    case PAGER_STATE_READY:
        fil_name = mk_pager_cmd(fil_name);

        if (sv_print_exit) {
            fputs("\nexit 0\n", stdout);
            fclose(stdout);
            dup2(STDERR_FILENO, STDOUT_FILENO);

        } else {
            fclose(stderr);
            dup2(STDOUT_FILENO, STDERR_FILENO);
        }

        ignore_val( system( fil_name));
        AGFREE(fil_name);

    case PAGER_STATE_CHILD:
        /*
         *  This is a child process used in creating shell script usage.
         */
        break;
    }
#endif
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/pgusage.c */
