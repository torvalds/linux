// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/types.h>
#include "kfd_priv.h"
#include "amdgpu_ids.h"

static unsigned int pasid_bits = 16;
static bool pasids_allocated; /* = false */

bool kfd_set_pasid_limit(unsigned int new_limit)
{
	if (new_limit < 2)
		return false;

	if (new_limit < (1U << pasid_bits)) {
		if (pasids_allocated)
			/* We've already allocated user PASIDs, too late to
			 * change the limit
			 */
			return false;

		while (new_limit < (1U << pasid_bits))
			pasid_bits--;
	}

	return true;
}

unsigned int kfd_get_pasid_limit(void)
{
	return 1U << pasid_bits;
}

u32 kfd_pasid_alloc(void)
{
	int r = amdgpu_pasid_alloc(pasid_bits);

	if (r > 0) {
		pasids_allocated = true;
		return r;
	}

	return 0;
}

void kfd_pasid_free(u32 pasid)
{
	amdgpu_pasid_free(pasid);
}
