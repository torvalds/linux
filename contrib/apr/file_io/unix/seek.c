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

#include "apr_arch_file_io.h"

static apr_status_t setptr(apr_file_t *thefile, apr_off_t pos )
{
    apr_off_t newbufpos;
    apr_status_t rv;

    if (thefile->direction == 1) {
        rv = apr_file_flush_locked(thefile);
        if (rv) {
            return rv;
        }
        thefile->bufpos = thefile->direction = thefile->dataRead = 0;
    }

    newbufpos = pos - (thefile->filePtr - thefile->dataRead);
    if (newbufpos >= 0 && newbufpos <= thefile->dataRead) {
        thefile->bufpos = newbufpos;
        rv = APR_SUCCESS;
    }
    else {
        if (lseek(thefile->filedes, pos, SEEK_SET) != -1) {
            thefile->bufpos = thefile->dataRead = 0;
            thefile->filePtr = pos;
            rv = APR_SUCCESS;
        }
        else {
            rv = errno;
        }
    }

    return rv;
}


APR_DECLARE(apr_status_t) apr_file_seek(apr_file_t *thefile, apr_seek_where_t where, apr_off_t *offset)
{
    apr_off_t rv;

    thefile->eof_hit = 0;

    if (thefile->buffered) {
        int rc = EINVAL;
        apr_finfo_t finfo;

        file_lock(thefile);

        switch (where) {
        case APR_SET:
            rc = setptr(thefile, *offset);
            break;

        case APR_CUR:
            rc = setptr(thefile, thefile->filePtr - thefile->dataRead + thefile->bufpos + *offset);
            break;

        case APR_END:
            rc = apr_file_info_get_locked(&finfo, APR_FINFO_SIZE, thefile);
            if (rc == APR_SUCCESS)
                rc = setptr(thefile, finfo.size + *offset);
            break;
        }

        *offset = thefile->filePtr - thefile->dataRead + thefile->bufpos;

        file_unlock(thefile);

        return rc;
    }
    else {
        rv = lseek(thefile->filedes, *offset, where);
        if (rv == -1) {
            *offset = -1;
            return errno;
        }
        else {
            *offset = rv;
            return APR_SUCCESS;
        }
    }
}

apr_status_t apr_file_trunc(apr_file_t *fp, apr_off_t offset)
{
    if (fp->buffered) {
        int rc = 0;
        file_lock(fp);
        if (fp->direction == 1 && fp->bufpos != 0) {
            apr_off_t len = fp->filePtr + fp->bufpos;
            if (offset < len) {
                /* New file end fall below our write buffer limit.
                 * Figure out if and what needs to be flushed.
                 */
                apr_off_t off = len - offset;
                if (off >= 0 && off <= fp->bufpos)
                    fp->bufpos = fp->bufpos - (size_t)off;
                else
                    fp->bufpos = 0;
            }
            rc = apr_file_flush_locked(fp);
            /* Reset buffer positions for write mode */
            fp->bufpos = fp->direction = fp->dataRead = 0;
        }
        file_unlock(fp);
        if (rc) {
            return rc;
        }
    }
    if (ftruncate(fp->filedes, offset) == -1) {
        return errno;
    }
    return apr_file_seek(fp, APR_SET, &offset);
}
