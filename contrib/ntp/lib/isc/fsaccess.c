/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000, 2001  Internet Software Consortium.
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

/* $Id: fsaccess.c,v 1.10 2007/06/19 23:47:17 tbox Exp $ */

/*! \file
 * \brief
 * This file contains the OS-independent functionality of the API.
 */
#include <isc/fsaccess.h>
#include <isc/result.h>
#include <isc/util.h>

/*!
 * Shorthand.  Maybe ISC__FSACCESS_PERMISSIONBITS should not even be in
 * <isc/fsaccess.h>.  Could check consistency with sizeof(isc_fsaccess_t)
 * and the number of bits in each function.
 */
#define STEP		(ISC__FSACCESS_PERMISSIONBITS)
#define GROUP		(STEP)
#define OTHER		(STEP * 2)

void
isc_fsaccess_add(int trustee, int permission, isc_fsaccess_t *access) {
	REQUIRE(trustee <= 0x7);
	REQUIRE(permission <= 0xFF);

	if ((trustee & ISC_FSACCESS_OWNER) != 0)
		*access |= permission;

	if ((trustee & ISC_FSACCESS_GROUP) != 0)
		*access |= (permission << GROUP);

	if ((trustee & ISC_FSACCESS_OTHER) != 0)
		*access |= (permission << OTHER);
}

void
isc_fsaccess_remove(int trustee, int permission, isc_fsaccess_t *access) {
	REQUIRE(trustee <= 0x7);
	REQUIRE(permission <= 0xFF);


	if ((trustee & ISC_FSACCESS_OWNER) != 0)
		*access &= ~permission;

	if ((trustee & ISC_FSACCESS_GROUP) != 0)
		*access &= ~(permission << GROUP);

	if ((trustee & ISC_FSACCESS_OTHER) != 0)
		*access &= ~(permission << OTHER);
}

static isc_result_t
check_bad_bits(isc_fsaccess_t access, isc_boolean_t is_dir) {
	isc_fsaccess_t bits;

	/*
	 * Check for disallowed user bits.
	 */
	if (is_dir)
		bits = ISC_FSACCESS_READ |
		       ISC_FSACCESS_WRITE |
		       ISC_FSACCESS_EXECUTE;
	else
		bits = ISC_FSACCESS_CREATECHILD |
		       ISC_FSACCESS_ACCESSCHILD |
		       ISC_FSACCESS_DELETECHILD |
		       ISC_FSACCESS_LISTDIRECTORY;

	/*
	 * Set group bad bits.
	 */
	bits |= bits << STEP;
	/*
	 * Set other bad bits.
	 */
	bits |= bits << STEP;

	if ((access & bits) != 0) {
		if (is_dir)
			return (ISC_R_NOTFILE);
		else
			return (ISC_R_NOTDIRECTORY);
	}

	return (ISC_R_SUCCESS);
}
