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

#ifndef APR_DBM_PRIVATE_H
#define APR_DBM_PRIVATE_H

#include "apr.h"
#include "apr_errno.h"
#include "apr_pools.h"
#include "apr_dbm.h"
#include "apr_file_io.h"

#include "apu.h"

/* ### for now, include the DBM selection; this will go away once we start
   ### building and linking all of the DBMs at once. */
#include "apu_select_dbm.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @internal */

/**
 * Most DBM libraries take a POSIX mode for creating files.  Don't trust
 * the mode_t type, some platforms may not support it, int is safe.
 */
APU_DECLARE(int) apr_posix_perms2mode(apr_fileperms_t perm);

/**
 * Structure to describe the operations of the DBM
 */
typedef struct {
    /** The name of the DBM Type */
    const char *name;

    /** Open the DBM */
    apr_status_t (*open)(apr_dbm_t **pdb, const char *pathname,
                         apr_int32_t mode, apr_fileperms_t perm,
                         apr_pool_t *pool);

    /** Close the DBM */
    void (*close)(apr_dbm_t *dbm);

    /** Fetch a dbm record value by key */
    apr_status_t (*fetch)(apr_dbm_t *dbm, apr_datum_t key,
                                   apr_datum_t * pvalue);

    /** Store a dbm record value by key */
    apr_status_t (*store)(apr_dbm_t *dbm, apr_datum_t key, apr_datum_t value);

    /** Delete a dbm record value by key */
    apr_status_t (*del)(apr_dbm_t *dbm, apr_datum_t key);

    /** Search for a key within the dbm */
    int (*exists)(apr_dbm_t *dbm, apr_datum_t key);

    /** Retrieve the first record key from a dbm */
    apr_status_t (*firstkey)(apr_dbm_t *dbm, apr_datum_t * pkey);

    /** Retrieve the next record key from a dbm */
    apr_status_t (*nextkey)(apr_dbm_t *dbm, apr_datum_t * pkey);

    /** Proactively toss any memory associated with the apr_datum_t. */
    void (*freedatum)(apr_dbm_t *dbm, apr_datum_t data);

    /** Get the names that the DBM will use for a given pathname. */
    void (*getusednames)(apr_pool_t *pool,
                         const char *pathname,
                         const char **used1,
                         const char **used2);

} apr_dbm_type_t;


/**
 * The actual DBM
 */
struct apr_dbm_t
{ 
    /** Associated pool */
    apr_pool_t *pool;

    /** pointer to DB Implementation Specific data */
    void *file;

    /** Current integer error code */
    int errcode;
    /** Current string error code */
    const char *errmsg;

    /** the type of DBM */
    const apr_dbm_type_t *type;
};


/* Declare all of the DBM provider tables */
APU_MODULE_DECLARE_DATA extern const apr_dbm_type_t apr_dbm_type_sdbm;
APU_MODULE_DECLARE_DATA extern const apr_dbm_type_t apr_dbm_type_gdbm;
APU_MODULE_DECLARE_DATA extern const apr_dbm_type_t apr_dbm_type_ndbm;
APU_MODULE_DECLARE_DATA extern const apr_dbm_type_t apr_dbm_type_db;

#ifdef __cplusplus
}
#endif

#endif /* APR_DBM_PRIVATE_H */
