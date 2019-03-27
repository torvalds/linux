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

#ifndef APR_DBM_H
#define APR_DBM_H

#include "apu.h"
#include "apr.h"
#include "apr_errno.h"
#include "apr_pools.h"
#include "apr_file_info.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file apr_dbm.h
 * @brief APR-UTIL DBM library
 */
/** 
 * @defgroup APR_Util_DBM DBM routines
 * @ingroup APR_Util
 * @{
 */
/**
 * Structure for referencing a dbm
 */
typedef struct apr_dbm_t apr_dbm_t;

/**
 * Structure for referencing the datum record within a dbm
 */
typedef struct
{
    /** pointer to the 'data' to retrieve/store in the DBM */
    char *dptr;
    /** size of the 'data' to retrieve/store in the DBM */
    apr_size_t dsize;
} apr_datum_t;

/* modes to open the DB */
#define APR_DBM_READONLY        1       /**< open for read-only access */
#define APR_DBM_READWRITE       2       /**< open for read-write access */
#define APR_DBM_RWCREATE        3       /**< open for r/w, create if needed */
#define APR_DBM_RWTRUNC         4       /**< open for r/w, truncating an existing
                                          DB if present */
/**
 * Open a dbm file by file name and type of DBM
 * @param dbm The newly opened database
 * @param type The type of the DBM (not all may be available at run time)
 * <pre>
 *  db   for Berkeley DB files
 *  gdbm for GDBM files
 *  ndbm for NDBM files
 *  sdbm for SDBM files (always available)
 *  default for the default DBM type
 *  </pre>
 * @param name The dbm file name to open
 * @param mode The flag value
 * <PRE>
 *           APR_DBM_READONLY   open for read-only access
 *           APR_DBM_READWRITE  open for read-write access
 *           APR_DBM_RWCREATE   open for r/w, create if needed
 *           APR_DBM_RWTRUNC    open for r/w, truncate if already there
 * </PRE>
 * @param perm Permissions to apply to if created
 * @param cntxt The pool to use when creating the dbm
 * @remark The dbm name may not be a true file name, as many dbm packages
 * append suffixes for seperate data and index files.
 * @bug In apr-util 0.9 and 1.x, the type arg was case insensitive.  This
 * was highly inefficient, and as of 2.x the dbm name must be provided in
 * the correct case (lower case for all bundled providers)
 */

APU_DECLARE(apr_status_t) apr_dbm_open_ex(apr_dbm_t **dbm, const char* type, 
                                       const char *name, 
                                       apr_int32_t mode, apr_fileperms_t perm,
                                       apr_pool_t *cntxt);


/**
 * Open a dbm file by file name
 * @param dbm The newly opened database
 * @param name The dbm file name to open
 * @param mode The flag value
 * <PRE>
 *           APR_DBM_READONLY   open for read-only access
 *           APR_DBM_READWRITE  open for read-write access
 *           APR_DBM_RWCREATE   open for r/w, create if needed
 *           APR_DBM_RWTRUNC    open for r/w, truncate if already there
 * </PRE>
 * @param perm Permissions to apply to if created
 * @param cntxt The pool to use when creating the dbm
 * @remark The dbm name may not be a true file name, as many dbm packages
 * append suffixes for seperate data and index files.
 */
APU_DECLARE(apr_status_t) apr_dbm_open(apr_dbm_t **dbm, const char *name, 
                                       apr_int32_t mode, apr_fileperms_t perm,
                                       apr_pool_t *cntxt);

/**
 * Close a dbm file previously opened by apr_dbm_open
 * @param dbm The database to close
 */
APU_DECLARE(void) apr_dbm_close(apr_dbm_t *dbm);

/**
 * Fetch a dbm record value by key
 * @param dbm The database 
 * @param key The key datum to find this record
 * @param pvalue The value datum retrieved for this record
 */
APU_DECLARE(apr_status_t) apr_dbm_fetch(apr_dbm_t *dbm, apr_datum_t key,
                                        apr_datum_t *pvalue);
/**
 * Store a dbm record value by key
 * @param dbm The database 
 * @param key The key datum to store this record by
 * @param value The value datum to store in this record
 */
APU_DECLARE(apr_status_t) apr_dbm_store(apr_dbm_t *dbm, apr_datum_t key, 
                                        apr_datum_t value);

/**
 * Delete a dbm record value by key
 * @param dbm The database 
 * @param key The key datum of the record to delete
 * @remark It is not an error to delete a non-existent record.
 */
APU_DECLARE(apr_status_t) apr_dbm_delete(apr_dbm_t *dbm, apr_datum_t key);

/**
 * Search for a key within the dbm
 * @param dbm The database 
 * @param key The datum describing a key to test
 */
APU_DECLARE(int) apr_dbm_exists(apr_dbm_t *dbm, apr_datum_t key);

/**
 * Retrieve the first record key from a dbm
 * @param dbm The database 
 * @param pkey The key datum of the first record
 */
APU_DECLARE(apr_status_t) apr_dbm_firstkey(apr_dbm_t *dbm, apr_datum_t *pkey);

/**
 * Retrieve the next record key from a dbm
 * @param dbm The database 
 * @param pkey The key datum of the next record
 */
APU_DECLARE(apr_status_t) apr_dbm_nextkey(apr_dbm_t *dbm, apr_datum_t *pkey);

/**
 * Proactively toss any memory associated with the apr_datum_t.
 * @param dbm The database 
 * @param data The datum to free.
 */
APU_DECLARE(void) apr_dbm_freedatum(apr_dbm_t *dbm, apr_datum_t data);

/**
 * Report more information when an apr_dbm function fails.
 * @param dbm The database
 * @param errcode A DBM-specific value for the error (for logging). If this
 *                isn't needed, it may be NULL.
 * @param errbuf Location to store the error text
 * @param errbufsize The size of the provided buffer
 * @return The errbuf parameter, for convenience.
 */
APU_DECLARE(char *) apr_dbm_geterror(apr_dbm_t *dbm, int *errcode,
                                     char *errbuf, apr_size_t errbufsize);
/**
 * If the specified file/path were passed to apr_dbm_open(), return the
 * actual file/path names which would be (created and) used. At most, two
 * files may be used; used2 may be NULL if only one file is used.
 * @param pool The pool for allocating used1 and used2.
 * @param type The type of DBM you require info on @see apr_dbm_open_ex
 * @param pathname The path name to generate used-names from.
 * @param used1 The first pathname used by the apr_dbm implementation.
 * @param used2 The second pathname used by apr_dbm. If only one file is
 *              used by the specific implementation, this will be set to NULL.
 * @return An error if the specified type is invalid.
 * @remark The dbm file(s) don't need to exist. This function only manipulates
 *      the pathnames.
 */
APU_DECLARE(apr_status_t) apr_dbm_get_usednames_ex(apr_pool_t *pool,
                                                   const char *type,
                                                   const char *pathname,
                                                   const char **used1,
                                                   const char **used2);

/**
 * If the specified file/path were passed to apr_dbm_open(), return the
 * actual file/path names which would be (created and) used. At most, two
 * files may be used; used2 may be NULL if only one file is used.
 * @param pool The pool for allocating used1 and used2.
 * @param pathname The path name to generate used-names from.
 * @param used1 The first pathname used by the apr_dbm implementation.
 * @param used2 The second pathname used by apr_dbm. If only one file is
 *              used by the specific implementation, this will be set to NULL.
 * @remark The dbm file(s) don't need to exist. This function only manipulates
 *      the pathnames.
 */
APU_DECLARE(void) apr_dbm_get_usednames(apr_pool_t *pool,
                                        const char *pathname,
                                        const char **used1,
                                        const char **used2);

/** @} */
#ifdef __cplusplus
}
#endif

#endif	/* !APR_DBM_H */
