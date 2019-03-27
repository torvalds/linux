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
#include "apr_strings.h"
#include "apr_portable.h"
#include "apr_signal.h"
#include "apr_random.h"

/* Heavy on no'ops, here's what we want to pass if there is APR_NO_FILE
 * requested for a specific child handle;
 */
static apr_file_t no_file = { NULL, -1, };

APR_DECLARE(apr_status_t) apr_procattr_create(apr_procattr_t **new,
                                              apr_pool_t *pool)
{
    (*new) = (apr_procattr_t *)apr_pcalloc(pool, sizeof(apr_procattr_t));

    if ((*new) == NULL) {
        return APR_ENOMEM;
    }
    (*new)->pool = pool;
    (*new)->cmdtype = APR_PROGRAM;
    (*new)->uid = (*new)->gid = -1;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_procattr_io_set(apr_procattr_t *attr,
                                              apr_int32_t in,
                                              apr_int32_t out,
                                              apr_int32_t err)
{
    apr_status_t rv;

    if ((in != APR_NO_PIPE) && (in != APR_NO_FILE)) {
        /* APR_CHILD_BLOCK maps to APR_WRITE_BLOCK, while
         * APR_PARENT_BLOCK maps to APR_READ_BLOCK, so transpose 
         * the CHILD/PARENT blocking flags for the stdin pipe.
         * stdout/stderr map to the correct mode by default.
         */
        if (in == APR_CHILD_BLOCK)
            in = APR_READ_BLOCK;
        else if (in == APR_PARENT_BLOCK)
            in = APR_WRITE_BLOCK;

        if ((rv = apr_file_pipe_create_ex(&attr->child_in, &attr->parent_in,
                                          in, attr->pool)) == APR_SUCCESS)
            rv = apr_file_inherit_unset(attr->parent_in);
        if (rv != APR_SUCCESS)
            return rv;
    }
    else if (in == APR_NO_FILE)
        attr->child_in = &no_file;

    if ((out != APR_NO_PIPE) && (out != APR_NO_FILE)) {
        if ((rv = apr_file_pipe_create_ex(&attr->parent_out, &attr->child_out,
                                          out, attr->pool)) == APR_SUCCESS)
            rv = apr_file_inherit_unset(attr->parent_out);
        if (rv != APR_SUCCESS)
            return rv;
    }
    else if (out == APR_NO_FILE)
        attr->child_out = &no_file;

    if ((err != APR_NO_PIPE) && (err != APR_NO_FILE)) {
        if ((rv = apr_file_pipe_create_ex(&attr->parent_err, &attr->child_err,
                                          err, attr->pool)) == APR_SUCCESS)
            rv = apr_file_inherit_unset(attr->parent_err);
        if (rv != APR_SUCCESS)
            return rv;
    }
    else if (err == APR_NO_FILE)
        attr->child_err = &no_file;

    return APR_SUCCESS;
}


APR_DECLARE(apr_status_t) apr_procattr_child_in_set(apr_procattr_t *attr,
                                                    apr_file_t *child_in,
                                                    apr_file_t *parent_in)
{
    apr_status_t rv = APR_SUCCESS;

    if (attr->child_in == NULL && attr->parent_in == NULL
           && child_in == NULL && parent_in == NULL)
        if ((rv = apr_file_pipe_create(&attr->child_in, &attr->parent_in,
                                       attr->pool)) == APR_SUCCESS)
            rv = apr_file_inherit_unset(attr->parent_in);

    if (child_in != NULL && rv == APR_SUCCESS) {
        if (attr->child_in && (attr->child_in->filedes != -1))
            rv = apr_file_dup2(attr->child_in, child_in, attr->pool);
        else {
            attr->child_in = NULL;
            if ((rv = apr_file_dup(&attr->child_in, child_in, attr->pool))
                    == APR_SUCCESS)
                rv = apr_file_inherit_set(attr->child_in);
        }
    }

    if (parent_in != NULL && rv == APR_SUCCESS) {
        if (attr->parent_in)
            rv = apr_file_dup2(attr->parent_in, parent_in, attr->pool);
        else
            rv = apr_file_dup(&attr->parent_in, parent_in, attr->pool);
    }

    return rv;
}


APR_DECLARE(apr_status_t) apr_procattr_child_out_set(apr_procattr_t *attr,
                                                     apr_file_t *child_out,
                                                     apr_file_t *parent_out)
{
    apr_status_t rv = APR_SUCCESS;

    if (attr->child_out == NULL && attr->parent_out == NULL
           && child_out == NULL && parent_out == NULL)
        if ((rv = apr_file_pipe_create(&attr->parent_out, &attr->child_out,
                                       attr->pool)) == APR_SUCCESS)
            rv = apr_file_inherit_unset(attr->parent_out);

    if (child_out != NULL && rv == APR_SUCCESS) {
        if (attr->child_out && (attr->child_out->filedes != -1))
            rv = apr_file_dup2(attr->child_out, child_out, attr->pool);
        else {
            attr->child_out = NULL;
            if ((rv = apr_file_dup(&attr->child_out, child_out, attr->pool))
                    == APR_SUCCESS)
                rv = apr_file_inherit_set(attr->child_out);
        }
    }

    if (parent_out != NULL && rv == APR_SUCCESS) {
        if (attr->parent_out)
            rv = apr_file_dup2(attr->parent_out, parent_out, attr->pool);
        else
            rv = apr_file_dup(&attr->parent_out, parent_out, attr->pool);
    }

    return rv;
}


APR_DECLARE(apr_status_t) apr_procattr_child_err_set(apr_procattr_t *attr,
                                                     apr_file_t *child_err,
                                                     apr_file_t *parent_err)
{
    apr_status_t rv = APR_SUCCESS;

    if (attr->child_err == NULL && attr->parent_err == NULL
           && child_err == NULL && parent_err == NULL)
        if ((rv = apr_file_pipe_create(&attr->parent_err, &attr->child_err,
                                      attr->pool)) == APR_SUCCESS)
            rv = apr_file_inherit_unset(attr->parent_err);

    if (child_err != NULL && rv == APR_SUCCESS) {
        if (attr->child_err && (attr->child_err->filedes != -1))
            rv = apr_file_dup2(attr->child_err, child_err, attr->pool);
        else {
            attr->child_err = NULL;
            if ((rv = apr_file_dup(&attr->child_err, child_err, attr->pool))
                    == APR_SUCCESS)
                rv = apr_file_inherit_set(attr->child_err);
        }
    }
    if (parent_err != NULL && rv == APR_SUCCESS) {
        if (attr->parent_err)
            rv = apr_file_dup2(attr->parent_err, parent_err, attr->pool);
        else
            rv = apr_file_dup(&attr->parent_err, parent_err, attr->pool);
    }

    return rv;
}


APR_DECLARE(apr_status_t) apr_procattr_dir_set(apr_procattr_t *attr,
                                               const char *dir)
{
    attr->currdir = apr_pstrdup(attr->pool, dir);
    if (attr->currdir) {
        return APR_SUCCESS;
    }

    return APR_ENOMEM;
}

APR_DECLARE(apr_status_t) apr_procattr_cmdtype_set(apr_procattr_t *attr,
                                                   apr_cmdtype_e cmd)
{
    attr->cmdtype = cmd;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_procattr_detach_set(apr_procattr_t *attr,
                                                  apr_int32_t detach)
{
    attr->detached = detach;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_proc_fork(apr_proc_t *proc, apr_pool_t *pool)
{
    int pid;
    
    memset(proc, 0, sizeof(apr_proc_t));

    if ((pid = fork()) < 0) {
        return errno;
    }
    else if (pid == 0) {
        proc->pid = getpid();

        apr_random_after_fork(proc);

        return APR_INCHILD;
    }

    proc->pid = pid;

    return APR_INPARENT;
}

static apr_status_t limit_proc(apr_procattr_t *attr)
{
#if APR_HAVE_STRUCT_RLIMIT && APR_HAVE_SETRLIMIT
#ifdef RLIMIT_CPU
    if (attr->limit_cpu != NULL) {
        if ((setrlimit(RLIMIT_CPU, attr->limit_cpu)) != 0) {
            return errno;
        }
    }
#endif
#ifdef RLIMIT_NPROC
    if (attr->limit_nproc != NULL) {
        if ((setrlimit(RLIMIT_NPROC, attr->limit_nproc)) != 0) {
            return errno;
        }
    }
#endif
#ifdef RLIMIT_NOFILE
    if (attr->limit_nofile != NULL) {
        if ((setrlimit(RLIMIT_NOFILE, attr->limit_nofile)) != 0) {
            return errno;
        }
    }
#endif
#if defined(RLIMIT_AS)
    if (attr->limit_mem != NULL) {
        if ((setrlimit(RLIMIT_AS, attr->limit_mem)) != 0) {
            return errno;
        }
    }
#elif defined(RLIMIT_DATA)
    if (attr->limit_mem != NULL) {
        if ((setrlimit(RLIMIT_DATA, attr->limit_mem)) != 0) {
            return errno;
        }
    }
#elif defined(RLIMIT_VMEM)
    if (attr->limit_mem != NULL) {
        if ((setrlimit(RLIMIT_VMEM, attr->limit_mem)) != 0) {
            return errno;
        }
    }
#endif
#else
    /*
     * Maybe make a note in error_log that setrlimit isn't supported??
     */

#endif
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_procattr_child_errfn_set(apr_procattr_t *attr,
                                                       apr_child_errfn_t *errfn)
{
    attr->errfn = errfn;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_procattr_error_check_set(apr_procattr_t *attr,
                                                       apr_int32_t chk)
{
    attr->errchk = chk;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_procattr_addrspace_set(apr_procattr_t *attr,
                                                       apr_int32_t addrspace)
{
    /* won't ever be used on this platform, so don't save the flag */
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_procattr_user_set(apr_procattr_t *attr,
                                                const char *username,
                                                const char *password)
{
    apr_status_t rv;
    apr_gid_t    gid;

    if ((rv = apr_uid_get(&attr->uid, &gid, username,
                          attr->pool)) != APR_SUCCESS) {
        attr->uid = -1;
        return rv;
    }
    
    /* Use default user group if not already set */
    if (attr->gid == -1) {
        attr->gid = gid;
    }
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_procattr_group_set(apr_procattr_t *attr,
                                                 const char *groupname)
{
    apr_status_t rv;

    if ((rv = apr_gid_get(&attr->gid, groupname, attr->pool)) != APR_SUCCESS)
        attr->gid = -1;
    return rv;
}

APR_DECLARE(apr_status_t) apr_proc_create(apr_proc_t *new,
                                          const char *progname,
                                          const char * const *args,
                                          const char * const *env,
                                          apr_procattr_t *attr,
                                          apr_pool_t *pool)
{
    int i;
    const char * const empty_envp[] = {NULL};

    if (!env) { /* Specs require an empty array instead of NULL;
                 * Purify will trigger a failure, even if many
                 * implementations don't.
                 */
        env = empty_envp;
    }

    new->in = attr->parent_in;
    new->err = attr->parent_err;
    new->out = attr->parent_out;

    if (attr->errchk) {
        if (attr->currdir) {
            if (access(attr->currdir, X_OK) == -1) {
                /* chdir() in child wouldn't have worked */
                return errno;
            }
        }

        if (attr->cmdtype == APR_PROGRAM ||
            attr->cmdtype == APR_PROGRAM_ENV ||
            *progname == '/') {
            /* for both of these values of cmdtype, caller must pass
             * full path, so it is easy to check;
             * caller can choose to pass full path for other
             * values of cmdtype
             */
            if (access(progname, X_OK) == -1) {
                /* exec*() in child wouldn't have worked */
                return errno;
            }
        }
        else {
            /* todo: search PATH for progname then try to access it */
        }
    }

    if ((new->pid = fork()) < 0) {
        return errno;
    }
    else if (new->pid == 0) {
        /* child process */

        /*
         * If we do exec cleanup before the dup2() calls to set up pipes
         * on 0-2, we accidentally close the pipes used by programs like
         * mod_cgid.
         *
         * If we do exec cleanup after the dup2() calls, cleanup can accidentally
         * close our pipes which replaced any files which previously had
         * descriptors 0-2.
         *
         * The solution is to kill the cleanup for the pipes, then do
         * exec cleanup, then do the dup2() calls.
         */

        if (attr->child_in) {
            apr_pool_cleanup_kill(apr_file_pool_get(attr->child_in),
                                  attr->child_in, apr_unix_file_cleanup);
        }

        if (attr->child_out) {
            apr_pool_cleanup_kill(apr_file_pool_get(attr->child_out),
                                  attr->child_out, apr_unix_file_cleanup);
        }

        if (attr->child_err) {
            apr_pool_cleanup_kill(apr_file_pool_get(attr->child_err),
                                  attr->child_err, apr_unix_file_cleanup);
        }

        apr_pool_cleanup_for_exec();

        if ((attr->child_in) && (attr->child_in->filedes == -1)) {
            close(STDIN_FILENO);
        }
        else if (attr->child_in &&
                 attr->child_in->filedes != STDIN_FILENO) {
            dup2(attr->child_in->filedes, STDIN_FILENO);
            apr_file_close(attr->child_in);
        }

        if ((attr->child_out) && (attr->child_out->filedes == -1)) {
            close(STDOUT_FILENO);
        }
        else if (attr->child_out &&
                 attr->child_out->filedes != STDOUT_FILENO) {
            dup2(attr->child_out->filedes, STDOUT_FILENO);
            apr_file_close(attr->child_out);
        }

        if ((attr->child_err) && (attr->child_err->filedes == -1)) {
            close(STDERR_FILENO);
        }
        else if (attr->child_err &&
                 attr->child_err->filedes != STDERR_FILENO) {
            dup2(attr->child_err->filedes, STDERR_FILENO);
            apr_file_close(attr->child_err);
        }

        apr_signal(SIGCHLD, SIG_DFL); /* not sure if this is needed or not */

        if (attr->currdir != NULL) {
            if (chdir(attr->currdir) == -1) {
                if (attr->errfn) {
                    attr->errfn(pool, errno, "change of working directory failed");
                }
                _exit(-1);   /* We have big problems, the child should exit. */
            }
        }

        /* Only try to switch if we are running as root */
        if (attr->gid != -1 && !geteuid()) {
            if (setgid(attr->gid)) {
                if (attr->errfn) {
                    attr->errfn(pool, errno, "setting of group failed");
                }
                _exit(-1);   /* We have big problems, the child should exit. */
            }
        }

        if (attr->uid != -1 && !geteuid()) {
            if (setuid(attr->uid)) {
                if (attr->errfn) {
                    attr->errfn(pool, errno, "setting of user failed");
                }
                _exit(-1);   /* We have big problems, the child should exit. */
            }
        }

        if (limit_proc(attr) != APR_SUCCESS) {
            if (attr->errfn) {
                attr->errfn(pool, errno, "setting of resource limits failed");
            }
            _exit(-1);   /* We have big problems, the child should exit. */
        }

        if (attr->cmdtype == APR_SHELLCMD ||
            attr->cmdtype == APR_SHELLCMD_ENV) {
            int onearg_len = 0;
            const char *newargs[4];

            newargs[0] = SHELL_PATH;
            newargs[1] = "-c";

            i = 0;
            while (args[i]) {
                onearg_len += strlen(args[i]);
                onearg_len++; /* for space delimiter */
                i++;
            }

            switch(i) {
            case 0:
                /* bad parameters; we're doomed */
                break;
            case 1:
                /* no args, or caller already built a single string from
                 * progname and args
                 */
                newargs[2] = args[0];
                break;
            default:
            {
                char *ch, *onearg;
                
                ch = onearg = apr_palloc(pool, onearg_len);
                i = 0;
                while (args[i]) {
                    size_t len = strlen(args[i]);

                    memcpy(ch, args[i], len);
                    ch += len;
                    *ch = ' ';
                    ++ch;
                    ++i;
                }
                --ch; /* back up to trailing blank */
                *ch = '\0';
                newargs[2] = onearg;
            }
            }

            newargs[3] = NULL;

            if (attr->detached) {
                apr_proc_detach(APR_PROC_DETACH_DAEMONIZE);
            }

            if (attr->cmdtype == APR_SHELLCMD) {
                execve(SHELL_PATH, (char * const *) newargs, (char * const *)env);
            }
            else {
                execv(SHELL_PATH, (char * const *)newargs);
            }
        }
        else if (attr->cmdtype == APR_PROGRAM) {
            if (attr->detached) {
                apr_proc_detach(APR_PROC_DETACH_DAEMONIZE);
            }

            execve(progname, (char * const *)args, (char * const *)env);
        }
        else if (attr->cmdtype == APR_PROGRAM_ENV) {
            if (attr->detached) {
                apr_proc_detach(APR_PROC_DETACH_DAEMONIZE);
            }

            execv(progname, (char * const *)args);
        }
        else {
            /* APR_PROGRAM_PATH */
            if (attr->detached) {
                apr_proc_detach(APR_PROC_DETACH_DAEMONIZE);
            }

            execvp(progname, (char * const *)args);
        }
        if (attr->errfn) {
            char *desc;

            desc = apr_psprintf(pool, "exec of '%s' failed",
                                progname);
            attr->errfn(pool, errno, desc);
        }

        _exit(-1);  /* if we get here, there is a problem, so exit with an
                     * error code. */
    }

    /* Parent process */
    if (attr->child_in && (attr->child_in->filedes != -1)) {
        apr_file_close(attr->child_in);
    }

    if (attr->child_out && (attr->child_out->filedes != -1)) {
        apr_file_close(attr->child_out);
    }

    if (attr->child_err && (attr->child_err->filedes != -1)) {
        apr_file_close(attr->child_err);
    }

    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_proc_wait_all_procs(apr_proc_t *proc,
                                                  int *exitcode,
                                                  apr_exit_why_e *exitwhy,
                                                  apr_wait_how_e waithow,
                                                  apr_pool_t *p)
{
    proc->pid = -1;
    return apr_proc_wait(proc, exitcode, exitwhy, waithow);
}

APR_DECLARE(apr_status_t) apr_proc_wait(apr_proc_t *proc,
                                        int *exitcode, apr_exit_why_e *exitwhy,
                                        apr_wait_how_e waithow)
{
    pid_t pstatus;
    int waitpid_options = WUNTRACED;
    int exit_int;
    int ignore;
    apr_exit_why_e ignorewhy;

    if (exitcode == NULL) {
        exitcode = &ignore;
    }

    if (exitwhy == NULL) {
        exitwhy = &ignorewhy;
    }

    if (waithow != APR_WAIT) {
        waitpid_options |= WNOHANG;
    }

    do {
        pstatus = waitpid(proc->pid, &exit_int, waitpid_options);
    } while (pstatus < 0 && errno == EINTR);

    if (pstatus > 0) {
        proc->pid = pstatus;

        if (WIFEXITED(exit_int)) {
            *exitwhy = APR_PROC_EXIT;
            *exitcode = WEXITSTATUS(exit_int);
        }
        else if (WIFSIGNALED(exit_int)) {
            *exitwhy = APR_PROC_SIGNAL;

#ifdef WCOREDUMP
            if (WCOREDUMP(exit_int)) {
                *exitwhy |= APR_PROC_SIGNAL_CORE;
            }
#endif

            *exitcode = WTERMSIG(exit_int);
        }
        else {
            /* unexpected condition */
            return APR_EGENERAL;
        }

        return APR_CHILD_DONE;
    }
    else if (pstatus == 0) {
        return APR_CHILD_NOTDONE;
    }

    return errno;
}

#if APR_HAVE_STRUCT_RLIMIT
APR_DECLARE(apr_status_t) apr_procattr_limit_set(apr_procattr_t *attr,
                                                 apr_int32_t what,
                                                 struct rlimit *limit)
{
    switch(what) {
        case APR_LIMIT_CPU:
#ifdef RLIMIT_CPU
            attr->limit_cpu = limit;
            break;
#else
            return APR_ENOTIMPL;
#endif

        case APR_LIMIT_MEM:
#if defined(RLIMIT_DATA) || defined(RLIMIT_VMEM) || defined(RLIMIT_AS)
            attr->limit_mem = limit;
            break;
#else
            return APR_ENOTIMPL;
#endif

        case APR_LIMIT_NPROC:
#ifdef RLIMIT_NPROC
            attr->limit_nproc = limit;
            break;
#else
            return APR_ENOTIMPL;
#endif

        case APR_LIMIT_NOFILE:
#ifdef RLIMIT_NOFILE
            attr->limit_nofile = limit;
            break;
#else
            return APR_ENOTIMPL;
#endif

    }

    return APR_SUCCESS;
}
#endif /* APR_HAVE_STRUCT_RLIMIT */

