/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr_arch_threadproc.h"

APR_DECLARE(apr_status_t) apr_proc_detach(int daemonize)
{
    if (chdir("/") == -1) {
        return errno;
    }

#if !defined(MPE) && !defined(OS2) && !defined(TPF) && !defined(BEOS)
    /* Don't detach for MPE because child processes can't survive the death of
     * the parent. */
    if (daemonize) {
        int x;

        if ((x = fork()) > 0) {
            exit(0);
        }
        else if (x == -1) {
            perror("fork");
            fprintf(stderr, "unable to fork new process\n");
            exit(1);  /* we can't do anything here, so just exit. */
        }
        /* RAISE_SIGSTOP(DETACH); */
    }
#endif

#ifdef HAVE_SETSID
    /* A setsid() failure is not fatal if we didn't just fork().
     * The calling process may be the process group leader, in
     * which case setsid() will fail with EPERM.
     */
    if (setsid() == -1 && daemonize) {
        return errno;
    }
#elif defined(NEXT) || defined(NEWSOS)
    if (setpgrp(0, getpid()) == -1) {
        return errno;
    }
#elif defined(OS2) || defined(TPF) || defined(MPE)
    /* do nothing */
#else
    if (setpgid(0, 0) == -1) {
        return errno;
    }
#endif

    /* close out the standard file descriptors */
    if (freopen("/dev/null", "r", stdin) == NULL) {
        return errno;
        /* continue anyhow -- note we can't close out descriptor 0 because we
         * have nothing to replace it with, and if we didn't have a descriptor
         * 0 the next file would be created with that value ... leading to
         * havoc.
         */
    }
    if (freopen("/dev/null", "w", stdout) == NULL) {
        return errno;
    }
     /* We are going to reopen this again in a little while to the error
      * log file, but better to do it twice and suffer a small performance
      * hit for consistancy than not reopen it here.
      */
    if (freopen("/dev/null", "w", stderr) == NULL) {
        return errno;
    }
    return APR_SUCCESS;
}

#if (!HAVE_WAITPID)
/* From ikluft@amdahl.com
 * this is not ideal but it works for SVR3 variants
 * Modified by dwd@bell-labs.com to call wait3 instead of wait because
 *   apache started to use the WNOHANG option.
 */
int waitpid(pid_t pid, int *statusp, int options)
{
    int tmp_pid;
    if (kill(pid, 0) == -1) {
        errno = ECHILD;
        return -1;
    }
    while (((tmp_pid = wait3(statusp, options, 0)) != pid) &&
                (tmp_pid != -1) && (tmp_pid != 0) && (pid != -1))
        ;
    return tmp_pid;
}
#endif

