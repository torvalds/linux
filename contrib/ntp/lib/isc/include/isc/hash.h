/*
 * Copyright (C) 2004-2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: hash.h,v 1.12 2009/01/17 23:47:43 tbox Exp $ */

#ifndef ISC_HASH_H
#define ISC_HASH_H 1

/*****
 ***** Module Info
 *****/

/*! \file isc/hash.h
 *
 * \brief The hash API
 *	provides an unpredictable hash value for variable length data.
 *	A hash object contains a random vector (which is hidden from clients
 *	of this API) to make the actual hash value unpredictable.
 *
 *	The algorithm used in the API guarantees the probability of hash
 *	collision; in the current implementation, as long as the values stored
 *	in the random vector are unpredictable, the probability of hash
 *	collision between arbitrary two different values is at most 1/2^16.
 *
 *	Although the API is generic about the hash keys, it mainly expects
 *	DNS names (and sometimes IPv4/v6 addresses) as inputs.  It has an
 *	upper limit of the input length, and may run slow to calculate the
 *	hash values for large inputs.
 *
 *	This API is designed to be general so that it can provide multiple
 *	different hash contexts that have different random vectors.  However,
 *	it should be typical to have a single context for an entire system.
 *	To support such cases, the API also provides a single-context mode.
 *
 * \li MP:
 *	The hash object is almost read-only.  Once the internal random vector
 *	is initialized, no write operation will occur, and there will be no
 *	need to lock the object to calculate actual hash values.
 *
 * \li Reliability:
 *	In some cases this module uses low-level data copy to initialize the
 *	random vector.  Errors in this part are likely to crash the server or
 *	corrupt memory.
 *
 * \li Resources:
 *	A buffer, used as a random vector for calculating hash values.
 *
 * \li Security:
 *	This module intends to provide unpredictable hash values in
 *	adversarial environments in order to avoid denial of service attacks
 *	to hash buckets.
 *	Its unpredictability relies on the quality of entropy to build the
 *	random vector.
 *
 * \li Standards:
 *	None.
 */

/***
 *** Imports
 ***/

#include <isc/types.h>

/***
 *** Functions
 ***/
ISC_LANG_BEGINDECLS

isc_result_t
isc_hash_ctxcreate(isc_mem_t *mctx, isc_entropy_t *entropy, unsigned int limit,
		   isc_hash_t **hctx);
isc_result_t
isc_hash_create(isc_mem_t *mctx, isc_entropy_t *entropy, size_t limit);
/*!<
 * \brief Create a new hash object.
 *
 * isc_hash_ctxcreate() creates a different object.
 *
 * isc_hash_create() creates a module-internal object to support the
 * single-context mode.  It should be called only once.
 *
 * 'entropy' must be NULL or a valid entropy object.  If 'entropy' is NULL,
 * pseudo random values will be used to build the random vector, which may
 * weaken security.
 *
 * 'limit' specifies the maximum number of hash keys.  If it is too large,
 * these functions may fail.
 */

void
isc_hash_ctxattach(isc_hash_t *hctx, isc_hash_t **hctxp);
/*!<
 * \brief Attach to a hash object.
 *
 * This function is only necessary for the multiple-context mode.
 */

void
isc_hash_ctxdetach(isc_hash_t **hctxp);
/*!<
 * \brief Detach from a hash object.
 *
 * This function  is for the multiple-context mode, and takes a valid
 * hash object as an argument.
 */

void
isc_hash_destroy(void);
/*!<
 * \brief This function is for the single-context mode, and is expected to be used
 * as a counterpart of isc_hash_create().
 *
 * A valid module-internal hash object must have been created, and this
 * function should be called only once.
 */

/*@{*/
void
isc_hash_ctxinit(isc_hash_t *hctx);
void
isc_hash_init(void);
/*!<
 * \brief Initialize a hash object.
 *
 * It fills in the random vector with a proper
 * source of entropy, which is typically from the entropy object specified
 * at the creation.  Thus, it is desirable to call these functions after
 * initializing the entropy object with some good entropy sources.
 *
 * These functions should be called before the first hash calculation.
 *
 * isc_hash_ctxinit() is for the multiple-context mode, and takes a valid hash
 * object as an argument.
 *
 * isc_hash_init() is for the single-context mode.  A valid module-internal
 * hash object must have been created, and this function should be called only
 * once.
 */
/*@}*/

/*@{*/
unsigned int
isc_hash_ctxcalc(isc_hash_t *hctx, const unsigned char *key,
		 unsigned int keylen, isc_boolean_t case_sensitive);
unsigned int
isc_hash_calc(const unsigned char *key, unsigned int keylen,
	      isc_boolean_t case_sensitive);
/*!<
 * \brief Calculate a hash value.
 *
 * isc_hash_ctxinit() is for the multiple-context mode, and takes a valid hash
 * object as an argument.
 *
 * isc_hash_init() is for the single-context mode.  A valid module-internal
 * hash object must have been created.
 *
 * 'key' is the hash key, which is a variable length buffer.
 *
 * 'keylen' specifies the key length, which must not be larger than the limit
 * specified for the corresponding hash object.
 *
 * 'case_sensitive' specifies whether the hash key should be treated as
 * case_sensitive values.  It should typically be ISC_FALSE if the hash key
 * is a DNS name.
 */
/*@}*/

ISC_LANG_ENDDECLS

#endif /* ISC_HASH_H */
