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

#include "apr_file_info.h"
#include "apr_file_io.h"
#include "apr_sdbm.h"

#include "sdbm_private.h"
#include "sdbm_tune.h"

/* NOTE: this function may block until it acquires the lock */
APU_DECLARE(apr_status_t) apr_sdbm_lock(apr_sdbm_t *db, int type)
{
    apr_status_t status;
    int lock_type = type & APR_FLOCK_TYPEMASK;

    if (!(lock_type == APR_FLOCK_SHARED || lock_type == APR_FLOCK_EXCLUSIVE))
        return APR_EINVAL;

    if (db->flags & SDBM_EXCLUSIVE_LOCK) {
        ++db->lckcnt;
        return APR_SUCCESS;
    }
    else if (db->flags & SDBM_SHARED_LOCK) {
        /*
         * Cannot promote a shared lock to an exlusive lock
         * in a cross-platform compatibile manner.
         */
        if (type == APR_FLOCK_EXCLUSIVE)
            return APR_EINVAL;
        ++db->lckcnt;
        return APR_SUCCESS;
    }
    /*
     * zero size: either a fresh database, or one with a single,
     * unsplit data page: dirpage is all zeros.
     */
    if ((status = apr_file_lock(db->dirf, type)) == APR_SUCCESS) 
    {
        apr_finfo_t finfo;
        if ((status = apr_file_info_get(&finfo, APR_FINFO_SIZE, db->dirf))
                != APR_SUCCESS) {
            (void) apr_file_unlock(db->dirf);
            return status;
        }

        SDBM_INVALIDATE_CACHE(db, finfo);

        ++db->lckcnt;
        if (type == APR_FLOCK_SHARED)
            db->flags |= SDBM_SHARED_LOCK;
        else if (type == APR_FLOCK_EXCLUSIVE)
            db->flags |= SDBM_EXCLUSIVE_LOCK;
    }
    return status;
}

APU_DECLARE(apr_status_t) apr_sdbm_unlock(apr_sdbm_t *db)
{
    if (!(db->flags & (SDBM_SHARED_LOCK | SDBM_EXCLUSIVE_LOCK)))
        return APR_EINVAL;
    if (--db->lckcnt > 0)
        return APR_SUCCESS;
    db->flags &= ~(SDBM_SHARED_LOCK | SDBM_EXCLUSIVE_LOCK);
    return apr_file_unlock(db->dirf);
}
