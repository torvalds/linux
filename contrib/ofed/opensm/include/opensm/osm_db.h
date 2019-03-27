/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _OSM_DB_H_
#define _OSM_DB_H_

/*
 * Abstract:
 * Declaration of the DB interface.
 */

#include <complib/cl_list.h>
#include <complib/cl_spinlock.h>

struct osm_log;

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Database
* NAME
*	Database
*
* DESCRIPTION
*	The OpenSM database interface provide the means to restore persistent
*  data, query, modify, delete and eventually commit it back to the
*  persistent media.
*
*  The interface is defined such that it can is not "data dependent":
*  All keys and data items are texts.
*
*	The DB implementation should be thread safe, thus callers do not need to
*  provide serialization.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Eitan Zahavi, Mellanox Technologies LTD
*
*********/
/****s* OpenSM: Database/osm_db_domain_t
* NAME
*	osm_db_domain_t
*
* DESCRIPTION
*	A domain of the database. Can be viewed as a database table.
*
*	The osm_db_domain_t object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_db_domain {
	struct osm_db *p_db;
	void *p_domain_imp;
} osm_db_domain_t;
/*
* FIELDS
*	p_db
*		Pointer to the parent database object.
*
*	p_domain_imp
*		Pointer to the db implementation object
*
* SEE ALSO
* osm_db_t
*********/

/****s* OpenSM: Database/osm_db_t
* NAME
*	osm_db_t
*
* DESCRIPTION
*	The main database object.
*
*	The osm_db_t object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_db {
	void *p_db_imp;
	struct osm_log *p_log;
	cl_list_t domains;
} osm_db_t;
/*
* FIELDS
*	p_db_imp
*		Pointer to the database implementation object
*
*	p_log
*		Pointer to the OSM logging facility
*
*  domains
*     List of initialize domains
*
* SEE ALSO
*********/

/****f* OpenSM: Database/osm_db_construct
* NAME
*	osm_db_construct
*
* DESCRIPTION
*	Construct a database.
*
* SYNOPSIS
*/
void osm_db_construct(IN osm_db_t * p_db);
/*
* PARAMETERS
*	p_db
*		[in] Pointer to the database object to construct
*
* RETURN VALUES
*	NONE
*
* SEE ALSO
*	Database, osm_db_init, osm_db_destroy
*********/

/****f* OpenSM: Database/osm_db_destroy
* NAME
*	osm_db_destroy
*
* DESCRIPTION
*	Destroys the osm_db_t structure.
*
* SYNOPSIS
*/
void osm_db_destroy(IN osm_db_t * p_db);
/*
* PARAMETERS
*	p_db
*		[in] Pointer to osm_db_t structure to destroy
*
* SEE ALSO
*	Database, osm_db_construct, osm_db_init
*********/

/****f* OpenSM: Database/osm_db_init
* NAME
*	osm_db_init
*
* DESCRIPTION
*	Initializes the osm_db_t structure.
*
* SYNOPSIS
*/
int osm_db_init(IN osm_db_t * p_db, IN struct osm_log * p_log);
/*
* PARAMETERS
*
*	p_db
*		[in] Pointer to the database object to initialize
*
*	p_log
*		[in] Pointer to the OSM logging facility
*
* RETURN VALUES
*	0 on success 1 otherwise
*
* SEE ALSO
*	Database, osm_db_construct, osm_db_destroy
*********/

/****f* OpenSM: Database/osm_db_domain_init
* NAME
*	osm_db_domain_init
*
* DESCRIPTION
*	Initializes the osm_db_domain_t structure.
*
* SYNOPSIS
*/
osm_db_domain_t *osm_db_domain_init(IN osm_db_t * p_db, IN const char *domain_name);
/*
* PARAMETERS
*
*	p_db
*		[in] Pointer to the database object to initialize
*
*	domain_name
*		[in] a char array with the domain name.
*
* RETURN VALUES
*	pointer to the new domain object or NULL if failed.
*
* SEE ALSO
*	Database, osm_db_construct, osm_db_destroy
*********/

/****f* OpenSM: Database/osm_db_restore
* NAME
*	osm_db_restore
*
* DESCRIPTION
*	Reads the entire domain from persistent storage - overrides all
*  existing cached data (if any).
*
* SYNOPSIS
*/
int osm_db_restore(IN osm_db_domain_t * p_domain);
/*
* PARAMETERS
*
*	p_domain
*		[in] Pointer to the database domain object to restore
*		     from persistent db
*
* RETURN VALUES
*	0 if successful 1 otherwize
*
* SEE ALSO
*	Database, osm_db_domain_init, osm_db_clear, osm_db_store,
*  osm_db_keys, osm_db_lookup, osm_db_update, osm_db_delete
*********/

/****f* OpenSM: Database/osm_db_clear
* NAME
*	osm_db_clear
*
* DESCRIPTION
*	Clears the entire domain values from/in the cache
*
* SYNOPSIS
*/
int osm_db_clear(IN osm_db_domain_t * p_domain);
/*
* PARAMETERS
*
*	p_domain
*		[in] Pointer to the database domain object to clear
*
* RETURN VALUES
*	0 if successful 1 otherwize
*
* SEE ALSO
*	Database, osm_db_domain_init, osm_db_restore, osm_db_store,
*  osm_db_keys, osm_db_lookup, osm_db_update, osm_db_delete
*********/

/****f* OpenSM: Database/osm_db_store
* NAME
*	osm_db_store
*
* DESCRIPTION
*	Store the domain cache back to the database (commit)
*
* SYNOPSIS
*/
int osm_db_store(IN osm_db_domain_t * p_domain,
		 IN boolean_t fsync_high_avail_files);
/*
* PARAMETERS
*
*	p_domain
*		[in] Pointer to the database domain object to restore from
*		     persistent db
*
*	fsync_high_avail_files
*		[in] Boolean that indicates whether or not to synchronize
*		     in-memory high availability files with storage
*
* RETURN VALUES
*	0 if successful 1 otherwize
*
* SEE ALSO
*	Database, osm_db_domain_init, osm_db_restore, osm_db_clear,
*  osm_db_keys, osm_db_lookup, osm_db_update, osm_db_delete
*********/

/****f* OpenSM: Database/osm_db_keys
* NAME
*	osm_db_keys
*
* DESCRIPTION
*	Retrive all keys of the domain
*
* SYNOPSIS
*/
int osm_db_keys(IN osm_db_domain_t * p_domain, OUT cl_list_t * p_key_list);
/*
* PARAMETERS
*
* p_domain
*    [in] Pointer to the database domain object
*
* p_key_list
*    [out] List of key values. It should be PRE constructed and initialized.
*
* RETURN VALUES
*	0 if successful 1 otherwize
*
* NOTE: the caller needs to free and destruct the list,
*       the keys returned are intrnal to the hash and should NOT be free'ed
*
* SEE ALSO
*	Database, osm_db_domain_init, osm_db_restore, osm_db_clear, osm_db_store,
*  osm_db_lookup, osm_db_update, osm_db_delete
*********/

/****f* OpenSM: Database/osm_db_lookup
* NAME
*	osm_db_lookup
*
* DESCRIPTION
*	Lookup an entry in the domain by the given key
*
* SYNOPSIS
*/
/* lookup value by key */
char *osm_db_lookup(IN osm_db_domain_t * p_domain, IN char *p_key);
/*
* PARAMETERS
*
*  p_domain
*    [in] Pointer to the database domain object
*
*	key
*		[in] The key to look for
*
* RETURN VALUES
*  the value as char * or NULL if not found
*
* SEE ALSO
*	Database, osm_db_domain_init, osm_db_restore, osm_db_clear, osm_db_store,
*  osm_db_keys, osm_db_update, osm_db_delete
*********/

/****f* OpenSM: Database/osm_db_update
* NAME
*	osm_db_update
*
* DESCRIPTION
*	Set the value of the given key
*
* SYNOPSIS
*/
int osm_db_update(IN osm_db_domain_t * p_domain, IN char *p_key, IN char *p_val);
/*
* PARAMETERS
*
*  p_domain
*    [in] Pointer to the database domain object
*
*	p_key
*		[in] The key to update
*
*	p_val
*		[in] The value to update
*
* RETURN VALUES
*  0 on success
*
* NOTE: the value will be duplicated so can be free'ed
*
* SEE ALSO
*	Database, osm_db_domain_init, osm_db_restore, osm_db_clear, osm_db_store,
*  osm_db_keys, osm_db_lookup, osm_db_delete
*********/

/****f* OpenSM: Database/osm_db_delete
* NAME
*	osm_db_delete
*
* DESCRIPTION
*	Delete an entry by the given key
*
* SYNOPSIS
*/
int osm_db_delete(IN osm_db_domain_t * p_domain, IN char *p_key);
/*
* PARAMETERS
*
*  p_domain
*    [in] Pointer to the database domain object
*
*	p_key
*		[in] The key to look for
*
* RETURN VALUES
*  0 on success
*
* SEE ALSO
*	Database, osm_db_domain_init, osm_db_restore, osm_db_clear, osm_db_store,
*  osm_db_keys, osm_db_lookup, osm_db_update
*********/

END_C_DECLS
#endif				/* _OSM_DB_H_ */
