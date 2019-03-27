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

#include "apr.h"
#include "apr_arch_misc.h"
#include "apr_arch_threadproc.h"
#include "apr_arch_file_io.h"

#if APR_HAS_OTHER_CHILD

#ifdef HAVE_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#if APR_HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef BEOS
#include <sys/socket.h> /* for fd_set definition! */
#endif

static apr_other_child_rec_t *other_children = NULL;

static apr_status_t other_child_cleanup(void *data)
{
    apr_other_child_rec_t **pocr, *nocr;

    for (pocr = &other_children; *pocr; pocr = &(*pocr)->next) {
        if ((*pocr)->data == data) {
            nocr = (*pocr)->next;
            (*(*pocr)->maintenance) (APR_OC_REASON_UNREGISTER, (*pocr)->data, -1);
            *pocr = nocr;
            /* XXX: um, well we've just wasted some space in pconf ? */
            return APR_SUCCESS;
        }
    }
    return APR_SUCCESS;
}

APR_DECLARE(void) apr_proc_other_child_register(apr_proc_t *proc,
                     void (*maintenance) (int reason, void *, int status),
                     void *data, apr_file_t *write_fd, apr_pool_t *p)
{
    apr_other_child_rec_t *ocr;

    ocr = apr_palloc(p, sizeof(*ocr));
    ocr->p = p;
    ocr->proc = proc;
    ocr->maintenance = maintenance;
    ocr->data = data;
    if (write_fd == NULL) {
        ocr->write_fd = (apr_os_file_t) -1;
    }
    else {
#ifdef WIN32
        /* This should either go away as part of eliminating apr_proc_probe_writable_fds
         * or write_fd should point to an apr_file_t
         */
        ocr->write_fd = write_fd->filehand; 
#else
        ocr->write_fd = write_fd->filedes;
#endif

    }
    ocr->next = other_children;
    other_children = ocr;
    apr_pool_cleanup_register(p, ocr->data, other_child_cleanup, 
                              apr_pool_cleanup_null);
}

APR_DECLARE(void) apr_proc_other_child_unregister(void *data)
{
    apr_other_child_rec_t *cur;

    cur = other_children;
    while (cur) {
        if (cur->data == data) {
            break;
        }
        cur = cur->next;
    }

    /* segfault if this function called with invalid parm */
    apr_pool_cleanup_kill(cur->p, cur->data, other_child_cleanup);
    other_child_cleanup(data);
}

APR_DECLARE(apr_status_t) apr_proc_other_child_alert(apr_proc_t *proc,
                                                     int reason,
                                                     int status)
{
    apr_other_child_rec_t *ocr, *nocr;

    for (ocr = other_children; ocr; ocr = nocr) {
        nocr = ocr->next;
        if (ocr->proc->pid != proc->pid)
            continue;

        ocr->proc = NULL;
        (*ocr->maintenance) (reason, ocr->data, status);
        return APR_SUCCESS;
    }
    return APR_EPROC_UNKNOWN;
}

APR_DECLARE(void) apr_proc_other_child_refresh(apr_other_child_rec_t *ocr,
                                               int reason)
{
    /* Todo: 
     * Implement code to detect if pipes are still alive.
     */
#ifdef WIN32
    DWORD status;

    if (ocr->proc == NULL)
        return;

    if (!ocr->proc->hproc) {
        /* Already mopped up, perhaps we apr_proc_kill'ed it,
         * they should have already unregistered!
         */
        ocr->proc = NULL;
        (*ocr->maintenance) (APR_OC_REASON_LOST, ocr->data, -1);
    }
    else if (!GetExitCodeProcess(ocr->proc->hproc, &status)) {
        CloseHandle(ocr->proc->hproc);
        ocr->proc->hproc = NULL;
        ocr->proc = NULL;
        (*ocr->maintenance) (APR_OC_REASON_LOST, ocr->data, -1);
    }
    else if (status == STILL_ACTIVE) {
        (*ocr->maintenance) (reason, ocr->data, -1);
    }
    else {
        CloseHandle(ocr->proc->hproc);
        ocr->proc->hproc = NULL;
        ocr->proc = NULL;
        (*ocr->maintenance) (APR_OC_REASON_DEATH, ocr->data, status);
    }

#else /* ndef Win32 */
    pid_t waitret; 
    int status;

    if (ocr->proc == NULL)
        return;

    waitret = waitpid(ocr->proc->pid, &status, WNOHANG);
    if (waitret == ocr->proc->pid) {
        ocr->proc = NULL;
        (*ocr->maintenance) (APR_OC_REASON_DEATH, ocr->data, status);
    }
    else if (waitret == 0) {
        (*ocr->maintenance) (reason, ocr->data, -1);
    }
    else if (waitret == -1) {
        /* uh what the heck? they didn't call unregister? */
        ocr->proc = NULL;
        (*ocr->maintenance) (APR_OC_REASON_LOST, ocr->data, -1);
    }
#endif
}

APR_DECLARE(void) apr_proc_other_child_refresh_all(int reason)
{
    apr_other_child_rec_t *ocr, *next_ocr;

    for (ocr = other_children; ocr; ocr = next_ocr) {
        next_ocr = ocr->next;
        apr_proc_other_child_refresh(ocr, reason);
    }
}

#else /* !APR_HAS_OTHER_CHILD */

APR_DECLARE(void) apr_proc_other_child_register(apr_proc_t *proc,
                     void (*maintenance) (int reason, void *, int status),
                     void *data, apr_file_t *write_fd, apr_pool_t *p)
{
    return;
}

APR_DECLARE(void) apr_proc_other_child_unregister(void *data)
{
    return;
}

APR_DECLARE(apr_status_t) apr_proc_other_child_alert(apr_proc_t *proc,
                                                     int reason,
                                                     int status)
{
    return APR_ENOTIMPL;
}

APR_DECLARE(void) apr_proc_other_child_refresh(apr_other_child_rec_t *ocr,
                                               int reason)
{
    return;
}

APR_DECLARE(void) apr_proc_other_child_refresh_all(int reason)
{
    return;
}

#endif /* APR_HAS_OTHER_CHILD */
