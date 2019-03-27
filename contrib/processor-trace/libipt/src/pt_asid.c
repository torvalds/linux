/*
 * Copyright (c) 2014-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "pt_asid.h"

#include "intel-pt.h"

#include <string.h>


int pt_asid_from_user(struct pt_asid *asid, const struct pt_asid *user)
{
	if (!asid)
		return -pte_internal;

	pt_asid_init(asid);

	if (user) {
		size_t size;

		size = user->size;

		/* Ignore fields in the user's asid we don't know. */
		if (sizeof(*asid) < size)
			size = sizeof(*asid);

		/* Copy (portions of) the user's asid. */
		memcpy(asid, user, size);

		/* We copied user's size - fix it. */
		asid->size = sizeof(*asid);
	}

	return 0;
}

int pt_asid_to_user(struct pt_asid *user, const struct pt_asid *asid,
		    size_t size)
{
	if (!user || !asid)
		return -pte_internal;

	/* We need at least space for the size field. */
	if (size < sizeof(asid->size))
		return -pte_invalid;

	/* Only provide the fields we actually have. */
	if (sizeof(*asid) < size)
		size = sizeof(*asid);

	/* Copy (portions of) our asid to the user's. */
	memcpy(user, asid, size);

	/* We copied our size - fix it. */
	user->size = size;

	return 0;
}

int pt_asid_match(const struct pt_asid *lhs, const struct pt_asid *rhs)
{
	uint64_t lcr3, rcr3, lvmcs, rvmcs;

	if (!lhs || !rhs)
		return -pte_internal;

	lcr3 = lhs->cr3;
	rcr3 = rhs->cr3;

	if (lcr3 != rcr3 && lcr3 != pt_asid_no_cr3 && rcr3 != pt_asid_no_cr3)
		return 0;

	lvmcs = lhs->vmcs;
	rvmcs = rhs->vmcs;

	if (lvmcs != rvmcs && lvmcs != pt_asid_no_vmcs &&
	    rvmcs != pt_asid_no_vmcs)
		return 0;

	return 1;
}
