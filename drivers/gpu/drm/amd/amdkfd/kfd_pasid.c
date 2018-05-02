/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

static unsigned int pasid_bits = 16;
static const struct kfd2kgd_calls *kfd2kgd;

bool kfd_set_pasid_limit(unsigned int new_limit)
{
	if (new_limit < 2)
		return false;

	if (new_limit < (1U << pasid_bits)) {
		if (kfd2kgd)
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

unsigned int kfd_pasid_alloc(void)
{
	int r;

	/* Find the first best KFD device for calling KGD */
	if (!kfd2kgd) {
		struct kfd_dev *dev = NULL;
		unsigned int i = 0;

		while ((kfd_topology_enum_kfd_devices(i, &dev)) == 0) {
			if (dev && dev->kfd2kgd) {
				kfd2kgd = dev->kfd2kgd;
				break;
			}
			i++;
		}

		if (!kfd2kgd)
			return false;
	}

	r = kfd2kgd->alloc_pasid(pasid_bits);

	return r > 0 ? r : 0;
}

void kfd_pasid_free(unsigned int pasid)
{
	if (kfd2kgd)
		kfd2kgd->free_pasid(pasid);
}
