/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  PD functions
 *
 *  Authors: Christoph Raisch <raisch@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <asm/current.h>

#include "ehca_tools.h"
#include "ehca_iverbs.h"

static struct kmem_cache *pd_cache;

struct ib_pd *ehca_alloc_pd(struct ib_device *device,
			    struct ib_ucontext *context, struct ib_udata *udata)
{
	struct ehca_pd *pd;

	pd = kmem_cache_zalloc(pd_cache, GFP_KERNEL);
	if (!pd) {
		ehca_err(device, "device=%p context=%p out of memory",
			 device, context);
		return ERR_PTR(-ENOMEM);
	}

	pd->ownpid = current->tgid;

	/*
	 * Kernel PD: when device = -1, 0
	 * User   PD: when context != -1
	 */
	if (!context) {
		/*
		 * Kernel PDs after init reuses always
		 * the one created in ehca_shca_reopen()
		 */
		struct ehca_shca *shca = container_of(device, struct ehca_shca,
						      ib_device);
		pd->fw_pd.value = shca->pd->fw_pd.value;
	} else
		pd->fw_pd.value = (u64)pd;

	return &pd->ib_pd;
}

int ehca_dealloc_pd(struct ib_pd *pd)
{
	u32 cur_pid = current->tgid;
	struct ehca_pd *my_pd = container_of(pd, struct ehca_pd, ib_pd);

	if (my_pd->ib_pd.uobject && my_pd->ib_pd.uobject->context &&
	    my_pd->ownpid != cur_pid) {
		ehca_err(pd->device, "Invalid caller pid=%x ownpid=%x",
			 cur_pid, my_pd->ownpid);
		return -EINVAL;
	}

	kmem_cache_free(pd_cache,
			container_of(pd, struct ehca_pd, ib_pd));

	return 0;
}

int ehca_init_pd_cache(void)
{
	pd_cache = kmem_cache_create("ehca_cache_pd",
				     sizeof(struct ehca_pd), 0,
				     SLAB_HWCACHE_ALIGN,
				     NULL);
	if (!pd_cache)
		return -ENOMEM;
	return 0;
}

void ehca_cleanup_pd_cache(void)
{
	if (pd_cache)
		kmem_cache_destroy(pd_cache);
}
